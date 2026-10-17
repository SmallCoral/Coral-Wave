/* Coral-Wave: WM8978 master, 12.288 MHz MCLK, 48 kHz / 16-bit I2S.
 * USB runs from Y1; DMA and explicit feedback bridge the independent clocks.
 */
#include "audio.h"
#include "sam.h"
#include "tusb.h"
#include <string.h>

#define BLOCK_FRAMES 48u
#define DMA_BLOCKS 4u
#define RING_FRAMES 1024u
#define RING_MASK (RING_FRAMES - 1u)
#define PLAY_TARGET 192u
#define I2CM (SERCOM3->I2CM)
static volatile uint32_t tx_dma[DMA_BLOCKS][BLOCK_FRAMES];
static volatile uint32_t rx_dma[DMA_BLOCKS][BLOCK_FRAMES];
static DmacDescriptor head[2] __attribute__((aligned(16)));
static DmacDescriptor tail[2][DMA_BLOCKS - 1] __attribute__((aligned(16)));
static DmacDescriptor writeback[2] __attribute__((aligned(16)));
static unsigned dma_index[2];
static uint32_t play_ring[RING_FRAMES];
static int16_t capture_ring[RING_FRAMES];
static volatile uint32_t play_write, play_read, capture_write, capture_read;
static volatile uint32_t gain_q15[2][2] = {{32768, 32768}, {32768, 32768}};
static volatile bool primed;
static bool controls_dirty = true, dac_unmuted;
static uint32_t feedback_avg = PLAY_TARGET << 8, feedback_ms;
static uint16_t codec_shadow[58];
static bool quiesced;

static uint32_t cycles(void) {
  uint32_t a, b, value;
  do { a = status->millis; value = SysTick->VAL; b = status->millis; } while (a != b);
  return a * 48000u + (47999u - value);
}
static bool i2c_sync(void) {
  uint32_t start = tusb_time_millis_api();
  while (I2CM.SYNCBUSY.reg) if (tusb_time_millis_api() - start > 20) return false;
  return true;
}
static bool i2c_ack(void) {
  uint32_t start = tusb_time_millis_api();
  while (!(I2CM.INTFLAG.reg & SERCOM_I2CM_INTFLAG_MB)) {
    if ((I2CM.INTFLAG.reg & SERCOM_I2CM_INTFLAG_ERROR) ||
        tusb_time_millis_api() - start > 20) return false;
  }
  return !(I2CM.STATUS.reg & SERCOM_I2CM_STATUS_RXNACK);
}
static bool codec_write(uint8_t reg, uint16_t value) {
  I2CM.ADDR.reg = 0x34;
  bool ok = i2c_ack();
  if (ok) { I2CM.DATA.reg = (reg << 1) | ((value >> 8) & 1); ok = i2c_ack(); }
  if (ok) { I2CM.DATA.reg = value & 255; ok = i2c_ack(); }
  I2CM.CTRLB.reg = SERCOM_I2CM_CTRLB_CMD(3);
  ok = i2c_sync() && ok;
  if (ok) { ++status->codec_writes; status->codec_ack = 1; if (reg<58) codec_shadow[reg]=value; }
  else { status->codec_ack = 0; status->failed_register = reg; }
  return ok;
}
bool audio_codec_write(uint8_t reg, uint16_t value) { return codec_write(reg,value); }
uint16_t audio_codec_shadow(uint8_t reg) { return reg<58 ? codec_shadow[reg] : 0; }
void audio_quiesce(void) {
  quiesced=true;
  codec_write(10,codec_shadow[10]|0x040);
  NVIC_DisableIRQ(DMAC_IRQn);DMAC->CTRL.reg=0;I2S->CTRLA.reg=0;
}
static bool codec_init(void) {
  PM->APBCMASK.reg |= PM_APBCMASK_SERCOM3;
  GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID_SERCOM3_CORE | GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_CLKEN;
  while (GCLK->STATUS.bit.SYNCBUSY) { }
  PORT->Group[0].DIRCLR.reg = (1u << 22) | (1u << 23);
  PORT->Group[0].PMUX[11].reg = 0x22;
  PORT->Group[0].PINCFG[22].reg = PORT_PINCFG_PMUXEN | PORT_PINCFG_INEN;
  PORT->Group[0].PINCFG[23].reg = PORT_PINCFG_PMUXEN | PORT_PINCFG_INEN;
  I2CM.CTRLA.reg = SERCOM_I2CM_CTRLA_SWRST;
  if (!i2c_sync()) return false;
  I2CM.CTRLA.reg = SERCOM_I2CM_CTRLA_MODE_I2C_MASTER | SERCOM_I2CM_CTRLA_SDAHOLD(2);
  I2CM.CTRLB.reg = 0;
  I2CM.BAUD.reg = SERCOM_I2CM_BAUD_BAUD(235);
  I2CM.CTRLA.reg |= SERCOM_I2CM_CTRLA_ENABLE;
  if (!i2c_sync()) return false;
  I2CM.STATUS.reg = SERCOM_I2CM_STATUS_BUSSTATE(1);
  if (!i2c_sync() || !codec_write(0, 0)) return false;
  tusb_time_delay_ms_api(10);
  static const uint16_t before_bias[][2] = {
    {52,0x140}, {53,0x140}, {54,0x140}, {55,0x140},
    {10,0x040}, {3,0x00F}, {1,0x007}
  };
  for (unsigned i = 0; i < sizeof(before_bias)/sizeof(before_bias[0]); ++i)
    if (!codec_write(before_bias[i][0], before_bias[i][1])) return false;
  tusb_time_delay_ms_api(600);
  static const uint16_t setup[][2] = {
    {1,0x01D},              /* MICBIAS, analog bias, VMID 75k; PLL disabled */
    {2,0x195},              /* headphones, left PGA/boost/ADC; right ADC off */
    {4,0x010}, {5,0},       /* 16-bit I2S, companding/loopback disabled */
    {6,0x009}, {7,0},       /* direct MCLK /1, BCLK /4, master, 48 kHz */
    {11,0x1FF}, {12,0x1FF}, /* DAC digital gain 0 dB */
    {14,0x100}, {15,0x1FF}, {16,0x1FF}, /* ADC high-pass, digital 0 dB */
    {44,0x003}, {45,0x128}, {46,0x140}, /* LIP/LIN differential mic, PGA +18 dB */
    {47,0x100}, {48,0},     /* microphone boost +20 dB; aux inputs off */
    {49,0x002}, {50,1}, {51,1}, /* DACs to corresponding headphone mixers */
    {52,0x027}, {53,0x127}  /* fixed headphone gain -18 dB; USB gain <= 0 dB */
  };
  for (unsigned i = 0; i < sizeof(setup)/sizeof(setup[0]); ++i)
    if (!codec_write(setup[i][0], setup[i][1])) return false;
  tusb_time_delay_ms_api(100);
  return true;
}
static bool measure_lrclk(void) {
  const uint32_t mask = (1u << 10) | (1u << 11);
  PORT->Group[0].DIRCLR.reg = mask;
  PORT->Group[0].CTRL.reg |= mask;
  PORT->Group[0].PINCFG[10].reg = PORT_PINCFG_INEN;
  PORT->Group[0].PINCFG[11].reg = PORT_PINCFG_INEN;
  uint32_t previous = PORT->Group[0].IN.reg & (1u << 11);
  uint32_t start = cycles(), first = 0, elapsed = 0, edges = 0;
  while (cycles() - start < 4800000) {
    uint32_t current = PORT->Group[0].IN.reg & (1u << 11);
    if (current && !previous) {
      if (edges++ == 0) first = cycles();
      else if (edges == 257) { elapsed = cycles() - first; break; }
    }
    previous = current;
  }
  if (!elapsed) return false;
  status->lrclk_hz = (uint32_t)((48000000ull * 256u) / elapsed);
  return status->lrclk_hz > 47500 && status->lrclk_hz < 48500;
}
static bool i2s_sync(void) {
  uint32_t start = tusb_time_millis_api();
  while (I2S->SYNCBUSY.reg) if (tusb_time_millis_api() - start > 20) return false;
  return true;
}
static DmacDescriptor *descriptor(unsigned channel, unsigned block) {
  return block == 0 ? &head[channel] : &tail[channel][block - 1];
}
static bool dma_init(void) {
  PM->AHBMASK.reg |= PM_AHBMASK_DMAC;
  PM->APBBMASK.reg |= PM_APBBMASK_DMAC;
  DMAC->CTRL.reg = DMAC_CTRL_SWRST;
  uint32_t start = tusb_time_millis_api();
  while (DMAC->CTRL.bit.SWRST) if (tusb_time_millis_api() - start > 20) return false;
  for (unsigned ch = 0; ch < 2; ++ch) {
    for (unsigned block = 0; block < DMA_BLOCKS; ++block) {
      DmacDescriptor *d = descriptor(ch, block);
      d->BTCTRL.reg = DMAC_BTCTRL_VALID | DMAC_BTCTRL_BEATSIZE_WORD | DMAC_BTCTRL_BLOCKACT_INT |
        (ch == 0 ? DMAC_BTCTRL_SRCINC : DMAC_BTCTRL_DSTINC);
      d->BTCNT.reg = BLOCK_FRAMES;
      d->SRCADDR.reg = ch == 0 ? (uint32_t)&tx_dma[block][BLOCK_FRAMES] : (uint32_t)&I2S->DATA[1].reg;
      d->DSTADDR.reg = ch == 0 ? (uint32_t)&I2S->DATA[0].reg : (uint32_t)&rx_dma[block][BLOCK_FRAMES];
      d->DESCADDR.reg = (uint32_t)descriptor(ch, (block + 1) % DMA_BLOCKS);
    }
  }
  DMAC->BASEADDR.reg = (uint32_t)head;
  DMAC->WRBADDR.reg = (uint32_t)writeback;
  DMAC->CTRL.reg = DMAC_CTRL_DMAENABLE | DMAC_CTRL_LVLEN(15);
  for (unsigned ch = 0; ch < 2; ++ch) {
    DMAC->CHID.reg = ch;
    DMAC->CHCTRLB.reg = DMAC_CHCTRLB_TRIGSRC(ch == 0 ? I2S_DMAC_ID_TX_0 : I2S_DMAC_ID_RX_1) |
      DMAC_CHCTRLB_TRIGACT_BEAT | DMAC_CHCTRLB_LVL(ch == 0 ? 2 : 3);
    DMAC->CHINTENSET.reg = DMAC_CHINTENSET_TCMPL | DMAC_CHINTENSET_TERR;
    DMAC->CHCTRLA.reg = DMAC_CHCTRLA_ENABLE;
  }
  NVIC_SetPriority(DMAC_IRQn, 1);
  NVIC_ClearPendingIRQ(DMAC_IRQn);
  NVIC_EnableIRQ(DMAC_IRQn);
  return true;
}
bool audio_init(void) {
  status->failed_register = 0xFFFFFFFF;
  if (!codec_init()) { status->error = 4; return false; }
  if (!measure_lrclk()) { status->error = 5; return false; }
  PM->APBCMASK.reg |= PM_APBCMASK_I2S;
  GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID_I2S_0 | GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_CLKEN;
  while (GCLK->STATUS.bit.SYNCBUSY) { }
  I2S->CTRLA.reg = I2S_CTRLA_SWRST;
  if (!i2s_sync()) return false;
  const unsigned pins[] = {7, 8, 10, 11};
  for (unsigned i = 0; i < 4; ++i) {
    unsigned p = pins[i], shift = (p & 1) * 4;
    PORT->Group[0].PMUX[p/2].reg = (PORT->Group[0].PMUX[p/2].reg & ~(15u << shift)) | (6u << shift);
    PORT->Group[0].PINCFG[p].reg = PORT_PINCFG_PMUXEN | PORT_PINCFG_INEN;
  }
  I2S->CLKCTRL[0].reg = I2S_CLKCTRL_SLOTSIZE_32 | I2S_CLKCTRL_NBSLOTS(1) |
    I2S_CLKCTRL_FSWIDTH_SLOT | I2S_CLKCTRL_BITDELAY_I2S |
    I2S_CLKCTRL_FSSEL_FSPIN | I2S_CLKCTRL_SCKSEL_SCKPIN;
  /* Compact stereo preserves L/R pairing in one 32-bit DMA beat. */
  I2S->SERCTRL[0].reg = I2S_SERCTRL_SERMODE_TX | I2S_SERCTRL_DATASIZE_16C | I2S_SERCTRL_SLOTADJ_LEFT;
  I2S->SERCTRL[1].reg = I2S_SERCTRL_SERMODE_RX | I2S_SERCTRL_DATASIZE_16C | I2S_SERCTRL_SLOTADJ_LEFT;
  I2S->CTRLA.reg = I2S_CTRLA_ENABLE;
  if (!i2s_sync() || !dma_init()) return false;
  I2S->CTRLA.reg |= I2S_CTRLA_CKEN0;
  if (!i2s_sync()) return false;
  I2S->CTRLA.reg |= I2S_CTRLA_SEREN0 | I2S_CTRLA_SEREN1;
  if (!i2s_sync()) return false;
  tusb_time_delay_ms_api(10);
  I2S->INTFLAG.reg = I2S_INTFLAG_TXUR0 | I2S_INTFLAG_RXOR1;
  status->i2s_tx_underruns = status->i2s_rx_overruns = 0;
  return true;
}
static unsigned peak(int16_t value) { return value < 0 ? -(int)value : value; }
static void transmit_block(unsigned block) {
  uint32_t read = play_read, count = play_write - read;
  if (!primed && count >= PLAY_TARGET) primed = true;
  bool running = status->playback_active && primed;
  bool underrun = false;
  unsigned block_peak = 0;
  for (unsigned i = 0; i < BLOCK_FRAMES; ++i) {
    uint32_t frame = 0;
    if (running && read != play_write) frame = play_ring[(read++) & RING_MASK];
    else if (running) underrun = true;
    int16_t left = (int16_t)(((int32_t)(int16_t)frame * (int32_t)gain_q15[0][0]) >> 15);
    int16_t right = (int16_t)(((int32_t)(int16_t)(frame >> 16) * (int32_t)gain_q15[0][1]) >> 15);
    /* Compact I2S is L in the upper half; USB PCM is L in the lower half. */
    tx_dma[block][i] = ((uint32_t)(uint16_t)left << 16) | (uint16_t)right;
    unsigned p = peak(left); if (p > block_peak) block_peak = p;
    p = peak(right); if (p > block_peak) block_peak = p;
  }
  __DMB(); play_read = read;
  if (underrun) { ++status->playback_underflows; primed = false; }
  status->play_fifo_frames = play_write - read;
  status->current_dac_peak = block_peak;
  if (block_peak > status->dac_peak) status->dac_peak = block_peak;
  ++status->dma_tx_blocks; status->tx_frames += BLOCK_FRAMES;
}
static void receive_block(unsigned block) {
  uint32_t write = capture_write;
  for (unsigned i = 0; i < BLOCK_FRAMES; ++i) {
    uint32_t frame = rx_dma[block][i];
    int16_t left = (int16_t)(frame >> 16);
    int16_t right = (int16_t)frame;
    unsigned p = peak(left); if (p > status->left_adc_peak) status->left_adc_peak = p;
    p = peak(right); if (p > status->right_adc_peak) status->right_adc_peak = p;
    int16_t sample = (int16_t)(((int32_t)left * (int32_t)gain_q15[1][0]) >> 15);
    p = peak(sample); if (p > status->capture_peak) status->capture_peak = p;
    if (sample != 0) ++status->capture_nonzero;
    if (status->capture_active) {
      if (write - capture_read < RING_FRAMES) capture_ring[(write++) & RING_MASK] = sample;
      else ++status->capture_overflows;
    }
  }
  __DMB(); capture_write = write;
  ++status->dma_rx_blocks; status->rx_frames += BLOCK_FRAMES;
}
void DMAC_Handler(void) {
  while (DMAC->INTSTATUS.reg & 3) {
    uint16_t pending = DMAC->INTPEND.reg;
    unsigned ch = pending & DMAC_INTPEND_ID_Msk;
    if (ch > 1) break;
    /* INTPEND atomically selects and clears the reported channel. Avoid the
     * separate banked CHID/CHINTFLAG transaction in an interrupt handler. */
    DMAC->INTPEND.reg = DMAC_INTPEND_ID(ch) |
      (pending & (DMAC_INTPEND_TERR | DMAC_INTPEND_TCMPL | DMAC_INTPEND_SUSP));
    __DSB();
    if (pending & DMAC_INTPEND_TERR) ++status->dma_errors;
    if (pending & DMAC_INTPEND_TCMPL) {
      unsigned block = dma_index[ch];
      dma_index[ch] = (block + 1) % DMA_BLOCKS;
      if (ch == 0) transmit_block(block); else receive_block(block);
    }
  }
  uint16_t flags = I2S->INTFLAG.reg;
  if (flags & I2S_INTFLAG_TXUR0) { ++status->i2s_tx_underruns; I2S->INTFLAG.reg = I2S_INTFLAG_TXUR0; }
  if (flags & I2S_INTFLAG_RXOR1) { ++status->i2s_rx_overruns; I2S->INTFLAG.reg = I2S_INTFLAG_RXOR1; }
  __DSB();
}
void audio_stream(unsigned path, bool active) {
  uint32_t irq = __get_PRIMASK(); __disable_irq();
  if (path == 0) {
    status->playback_active = active;
    play_read = play_write = 0; primed = false;
    for (unsigned b = 0; b < DMA_BLOCKS; ++b)
      for (unsigned i = 0; i < BLOCK_FRAMES; ++i) tx_dma[b][i] = 0;
    feedback_avg = PLAY_TARGET << 8;
    controls_dirty = true;
  } else {
    status->capture_active = active;
    capture_read = capture_write = 0;
  }
  __DMB(); __set_PRIMASK(irq);
}
/* Q15 amplitudes at integer dB; interpolate for the UAC 8.8 dB control. */
static const uint16_t db_gain[] = {
  32768, 29205, 26029, 23198, 20675, 18427, 16423, 14637, 13045, 11627, 10362, 9235, 8231,
  7336, 6538, 5827, 5193, 4629, 4125, 3677, 3277, 2920, 2603, 2320, 2068, 1843,
  1642, 1464, 1305, 1163, 1036, 924, 823, 734, 654, 583, 519, 463, 413,
  368, 328, 292, 260, 232, 207, 184, 164, 146, 130, 116, 104, 92,
  82, 73, 65, 58, 52, 46, 41, 37, 33, 29, 26, 23, 21,
  18, 16, 15, 13, 12, 10, 9, 8, 7, 7, 6, 5, 5,
  4, 4, 3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1
};
static uint32_t attenuation(int db256) {
  if (db256 >= 0) return 32768;
  if (db256 <= -90 * 256) return db_gain[90];
  unsigned position = -db256, index = position >> 8, fraction = position & 255;
  return db_gain[index] - (((db_gain[index] - db_gain[index + 1]) * fraction + 128) >> 8);
}
void audio_controls(unsigned path, const uint8_t *mute, const int16_t *volume) {
  for (unsigned ch = 0; ch < (path == 0 ? 2u : 1u); ++ch)
    gain_q15[path][ch] = mute[0] || mute[ch + 1] ? 0 : attenuation(volume[0] + volume[ch + 1]);
  status->playback_gain_left_q15 = gain_q15[0][0];
  status->playback_gain_right_q15 = gain_q15[0][1];
  status->capture_gain_q15 = gain_q15[1][0];
  controls_dirty = true;
}
bool tud_audio_rx_done_isr(uint8_t rhport, uint16_t bytes, uint8_t function, uint8_t ep, uint8_t alt) {
  (void)rhport; (void)bytes; (void)function; (void)ep; (void)alt;
  const tu_fifo_t *fifo = tud_audio_get_ep_out_ff();
  if (tu_ff_overflow_count(fifo->depth, fifo->wr_idx, fifo->rd_idx) > fifo->depth)
    ++status->playback_overflows;
  return true;
}
void audio_task(void) {
  if (quiesced) return;
  uint32_t frame[49];
  while (status->playback_active && tud_audio_available() >= 4) {
    uint32_t room = RING_FRAMES - (play_write - play_read);
    if (!room) break;
    uint16_t bytes = (uint16_t)(room > 49 ? 196 : room * 4);
    bytes = tud_audio_read(frame, bytes) & ~3u;
    uint32_t write = play_write;
    for (unsigned i = 0; i < bytes / 4u; ++i) play_ring[(write++) & RING_MASK] = frame[i];
    __DMB(); play_write = write;
    status->playback_bytes += bytes;
    if (bytes == 0) break;
  }
  if (!status->playback_active && tud_audio_available()) {
    status->playback_bytes += tud_audio_read(frame, sizeof(frame));
  }
  if (status->capture_active) {
    int16_t samples[48];
    uint32_t read = capture_read, count = capture_write - read;
    if (count > 48) count = 48;
    uint32_t room = tu_fifo_remaining(tud_audio_get_ep_in_ff()) / 2;
    if (count > room) count = room;
    for (unsigned i = 0; i < count; ++i) samples[i] = capture_ring[(read + i) & RING_MASK];
    uint16_t written = tud_audio_write(samples, count * 2);
    __DMB(); capture_read = read + written / 2;
    status->capture_bytes += written;
  }
  if (status->playback_active && feedback_ms != status->millis) {
    feedback_ms = status->millis;
    uint32_t count = play_write - play_read;
    feedback_avg = (feedback_avg * 63u + (count << 8)) / 64u;
    int32_t correction = ((int32_t)PLAY_TARGET - (int32_t)(feedback_avg >> 8)) * 128;
    if (correction > 32768) correction = 32768;
    if (correction < -32768) correction = -32768;
    status->feedback_16_16 = (48u << 16) + correction;
    tud_audio_fb_set(status->feedback_16_16);
  }
  if (controls_dirty) {
    bool unmute = status->playback_active && (gain_q15[0][0] || gain_q15[0][1]);
    if (unmute != dac_unmuted) {
      if (!codec_write(10, (codec_shadow[10]&~0x040u) | (unmute ? 0 : 0x040))) status->error = 4;
      else dac_unmuted = unmute;
    }
    controls_dirty = false;
  }
}
