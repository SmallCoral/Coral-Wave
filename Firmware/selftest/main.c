/* SAMD21E18A / WM8978 board bring-up. Loaded into SRAM through SWD.
 * No flash programming, USB stack, bootloader or UART is needed.
 * Wiring: PA18 oscillator enable; PA22 SDA; PA23 SCL;
 * PA07 I2S serializer 0 TX; PA08 serializer 1 RX; PA10 SCK0; PA11 FS0.
 * Codec control is write-only. ACK and clocks are evidence, not an ID read.
 */
#include "samd21e18a.h"
#include <stdbool.h>
#include <stddef.h>

#define GPIO (&PORT_IOBUS_REGS->GROUP[0])
#define BIT(n) (1u << (n))
#define CPU_HZ 48000000u
#define MAILBOX_ADDR 0x20007000u
#define SCL 23u
#define SDA 22u
#ifndef WM8978_CONTROL_ONLY
#define WM8978_CONTROL_ONLY 0
#endif
#ifndef I2C_HALF_US
#define I2C_HALF_US 5
#endif
#ifndef WM8978_HARDWARE_I2C
#define WM8978_HARDWARE_I2C 0
#endif
#ifndef WM8978_PLL_FROM_12M
#define WM8978_PLL_FROM_12M 0
#endif
#define I2CM (&SERCOM3_REGS->I2CM)

enum { E_CLOCK = 1, E_BUS = 2, E_CODEC = 3, E_AUDIO_CLOCK = 4,
       E_I2S = 5, E_DMA = 6, E_HARDFAULT = 7, E_SERCOM = 8 };
/* All mailbox entries are 32 bits. Host reads this while target is halted.
 * command: 0 = acquisition only, 1 = 500/1000 Hz tone, 2 = ADC->DAC loopback.
 * Any other command = mute. Audible tests auto-mute after 10 seconds.
 */
typedef struct {
    uint32_t magic, version, stage, error, device_id, cpu_hz;
    uint32_t bus_idle, codec_ack_mask, writes_ok, failed_register;
    uint32_t lrclk_hz_approx, bclk_transitions, rx_words;
    int32_t adc_min[2], adc_max[2];
    uint32_t adc_nonzero[2], tx_underruns, rx_overruns, dma_blocks;
    uint32_t elapsed_ms, command, active_command, done;
    uint32_t i2c_ack_count, i2c_ack_bitmap[4], bus_drive_check;
} report_t;
static volatile report_t * const result = (volatile report_t *)MAILBOX_ADDR;
static uint32_t last_tick, tick_remainder, millis;
static uint32_t tone[192]; /* 96 stereo frames = 500 Hz at 48 kHz. */
static dmac_descriptor_registers_t desc[1] __attribute__((aligned(16)));
static dmac_descriptor_registers_t wrb[1] __attribute__((aligned(16)));
extern uint32_t __bss_start__, __bss_end__;
void Reset_Handler(void);
void HardFault_Handler(void);

__attribute__((section(".vectors"), used))
const uintptr_t vectors[48] = {
    [0] = 0x20008000u, [1] = (uintptr_t)Reset_Handler,
    [2] = (uintptr_t)HardFault_Handler, [3] = (uintptr_t)HardFault_Handler
};

static void fail(uint32_t code) {
    result->error = code;
    result->done = 1;
    for (;;) __NOP();
}
void HardFault_Handler(void) { fail(E_HARDFAULT); }

static bool wait_dfll(void) {
    for (uint32_t i = 0; i < 1000000; ++i)
        if (SYSCTRL_REGS->SYSCTRL_PCLKSR & SYSCTRL_PCLKSR_DFLLRDY_Msk)
            return true;
    return false;
}
static bool wait_gclk(void) {
    for (uint32_t i = 0; i < 1000000; ++i)
        if (!(GCLK_REGS->GCLK_STATUS & GCLK_STATUS_SYNCBUSY_Msk)) return true;
    return false;
}
static void clock_init(void) {
    /* Internal clock keeps SWD bring-up independent of the soldered Y1. */
    SYSCTRL_REGS->SYSCTRL_OSC8M &= ~SYSCTRL_OSC8M_PRESC_Msk;
    GCLK_REGS->GCLK_GENCTRL = GCLK_GENCTRL_ID(0) |
        GCLK_GENCTRL_SRC_OSC8M | GCLK_GENCTRL_GENEN_Msk;
    if (!wait_gclk()) fail(E_CLOCK);
    PM_REGS->PM_CPUSEL = 0;
    PM_REGS->PM_APBASEL = 0;
    PM_REGS->PM_APBBSEL = 0;
    PM_REGS->PM_APBCSEL = 0;
    NVMCTRL_REGS->NVMCTRL_CTRLB = NVMCTRL_CTRLB_RWS(1);
    /* SAMD21 errata: write ENABLE alone before programming DFLL. */
    if (!wait_dfll()) fail(E_CLOCK);
    SYSCTRL_REGS->SYSCTRL_DFLLCTRL = SYSCTRL_DFLLCTRL_ENABLE_Msk;
    if (!wait_dfll()) fail(E_CLOCK);
    uint32_t coarse = (*(const uint32_t *)0x00806024u >> 26) & 63u;
    if (coarse == 63) coarse = 31;
    SYSCTRL_REGS->SYSCTRL_DFLLVAL = SYSCTRL_DFLLVAL_COARSE(coarse) |
        SYSCTRL_DFLLVAL_FINE(512);
    if (!wait_dfll()) fail(E_CLOCK);
    GCLK_REGS->GCLK_GENCTRL = GCLK_GENCTRL_ID(0) |
        GCLK_GENCTRL_SRC_DFLL48M | GCLK_GENCTRL_GENEN_Msk | GCLK_GENCTRL_IDC_Msk;
    if (!wait_gclk()) fail(E_CLOCK);
    result->cpu_hz = CPU_HZ; /* Factory calibrated, approximate in open-loop. */
    SysTick->LOAD = 0xFFFFFFu;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_ENABLE_Msk | SysTick_CTRL_CLKSOURCE_Msk;
    last_tick = SysTick->VAL;
}
static void update_time(void) {
    uint32_t now = SysTick->VAL;
    tick_remainder += (last_tick - now) & 0xFFFFFFu;
    last_tick = now;
    while (tick_remainder >= CPU_HZ / 1000u) {
        tick_remainder -= CPU_HZ / 1000u;
        ++millis;
    }
    result->elapsed_ms = millis;
}
static void delay_us(uint32_t us) {
    uint32_t start = SysTick->VAL;
    uint32_t ticks = us * (CPU_HZ / 1000000u);
    while (((start - SysTick->VAL) & 0xFFFFFFu) < ticks) __NOP();
    update_time();
}
static void delay_ms(uint32_t ms) {
    for (uint32_t i = 0; i < ms; ++i) delay_us(1000);
}
static void input_pin(uint32_t pin) {
    GPIO->PORT_DIRCLR = BIT(pin);
    GPIO->PORT_PINCFG[pin] = PORT_PINCFG_INEN_Msk;
    /* IOBUS reads require continuous input sampling (PORT.CTRL). */
    GPIO->PORT_CTRL |= BIT(pin);
}
static void observe_y1(void) {
    /* Separate diagnostic at 0x20007100. Activity, not a frequency meter.
     * PA14 is wired to Y1.OUT. Varied sample spacing avoids a fixed alias.
     */
    volatile uint32_t *probe = (volatile uint32_t *)0x20007100u;
    probe[0] = 0x5931434B; probe[1] = probe[2] = probe[3] = 0;
    input_pin(14);
    uint32_t previous = GPIO->PORT_IN & BIT(14);
    for (uint32_t i = 0; i < 10000; ++i) {
        uint32_t sample = GPIO->PORT_IN & BIT(14);
        if (sample) ++probe[1]; else ++probe[2];
        if (sample != previous) ++probe[3];
        previous = sample;
        if (i & 1) __NOP();
        if (i & 2) { __NOP(); __NOP(); }
    }
    update_time();
}
static void release(uint32_t pin) { GPIO->PORT_DIRCLR = BIT(pin); }
static void low(uint32_t pin) {
    GPIO->PORT_OUTCLR = BIT(pin);
    GPIO->PORT_DIRSET = BIT(pin);
}
static bool high(uint32_t pin) { return (GPIO->PORT_IN & BIT(pin)) != 0; }
static bool scl_high(void) {
    release(SCL);
    for (uint32_t i = 0; i < 1000; ++i) {
        if (high(SCL)) { delay_us(I2C_HALF_US); return true; }
        delay_us(1);
    }
    return false;
}
static void stop(void) {
    low(SCL); low(SDA); delay_us(I2C_HALF_US);
    (void)scl_high(); release(SDA); delay_us(I2C_HALF_US);
}
static bool start(void) {
    release(SDA);
    if (!scl_high() || !high(SDA)) return false;
    low(SDA); delay_us(I2C_HALF_US); low(SCL); return true;
}
static bool write_byte(uint8_t byte) {
    for (unsigned i = 0; i < 8; ++i) {
        if (byte & 0x80u) release(SDA); else low(SDA);
        delay_us(I2C_HALF_US);
        if (!scl_high()) { low(SCL); return false; }
        low(SCL); byte <<= 1;
    }
    release(SDA); delay_us(I2C_HALF_US);
    if (!scl_high()) { low(SCL); return false; }
    bool ack = !high(SDA);
    low(SCL); delay_us(I2C_HALF_US); return ack;
}
static void hw_sync(void) {
    uint32_t begin = SysTick->VAL;
    while (I2CM->SERCOM_SYNCBUSY) {
        if (((begin - SysTick->VAL) & 0xFFFFFFu) > CPU_HZ / 20) fail(E_SERCOM);
    }
}
static void hw_init(void) {
    release(SDA); release(SCL);
    PM_REGS->PM_APBCMASK |= PM_APBCMASK_SERCOM3_Msk;
    GCLK_REGS->GCLK_CLKCTRL = GCLK_CLKCTRL_ID_SERCOM3_CORE |
        GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_CLKEN_Msk;
    if (!wait_gclk()) fail(E_CLOCK);
    I2CM->SERCOM_CTRLA = SERCOM_I2CM_CTRLA_SWRST_Msk;
    hw_sync();
    GPIO->PORT_PMUX[11] = 0x22; /* PA22/23 peripheral C, SERCOM3 PAD0/1. */
    GPIO->PORT_PINCFG[SDA] = PORT_PINCFG_PMUXEN_Msk | PORT_PINCFG_INEN_Msk;
    GPIO->PORT_PINCFG[SCL] = PORT_PINCFG_PMUXEN_Msk | PORT_PINCFG_INEN_Msk;
    I2CM->SERCOM_CTRLA = SERCOM_I2CM_CTRLA_MODE_I2C_MASTER |
        SERCOM_I2CM_CTRLA_SDAHOLD_450NS;
    I2CM->SERCOM_CTRLB = 0;
    /* 48 MHz / (2*(235+5)) = nominal 100 kHz before rise-time correction. */
    I2CM->SERCOM_BAUD = SERCOM_I2CM_BAUD_BAUD(235);
    I2CM->SERCOM_CTRLA |= SERCOM_I2CM_CTRLA_ENABLE_Msk;
    hw_sync();
    I2CM->SERCOM_STATUS = SERCOM_I2CM_STATUS_BUSSTATE(1); /* idle */
    hw_sync();
}
static bool hw_ack(void) {
    uint32_t begin = SysTick->VAL;
    for (;;) {
        if (I2CM->SERCOM_INTFLAG & SERCOM_I2CM_INTFLAG_ERROR_Msk) fail(E_SERCOM);
        if (I2CM->SERCOM_INTFLAG & SERCOM_I2CM_INTFLAG_MB_Msk)
            return !(I2CM->SERCOM_STATUS & SERCOM_I2CM_STATUS_RXNACK_Msk);
        if (((begin - SysTick->VAL) & 0xFFFFFFu) > CPU_HZ / 20) fail(E_SERCOM);
    }
}
static void hw_stop(void) {
    I2CM->SERCOM_CTRLB = SERCOM_I2CM_CTRLB_CMD(3);
    hw_sync();
    delay_us(10);
}
static void hw_scan(void) {
    result->i2c_ack_count = 0;
    for (unsigned i = 0; i < 4; ++i) result->i2c_ack_bitmap[i] = 0;
    for (uint32_t address = 8; address < 0x78; ++address) {
        I2CM->SERCOM_ADDR = address << 1;
        if (hw_ack()) {
            ++result->i2c_ack_count;
            result->i2c_ack_bitmap[address / 32] |= BIT(address % 32);
        }
        hw_stop();
    }
}
static bool hw_codec_write(uint8_t reg, uint16_t value) {
    uint32_t mask = 0;
    I2CM->SERCOM_ADDR = 0x34;
    if (hw_ack()) {
        mask |= 1;
        I2CM->SERCOM_DATA = (uint8_t)((reg << 1) | ((value >> 8) & 1u));
        if (hw_ack()) {
            mask |= 2;
            I2CM->SERCOM_DATA = (uint8_t)value;
            if (hw_ack()) mask |= 4;
        }
    }
    hw_stop(); result->codec_ack_mask = mask;
    if (mask != 7) { result->failed_register = reg; return false; }
    ++result->writes_ok; return true;
}
static bool codec_write(uint8_t reg, uint16_t value) {
    if (WM8978_HARDWARE_I2C) return hw_codec_write(reg, value);
    if (!start()) { stop(); result->failed_register = reg; return false; }
    uint32_t mask = 0;
    if (write_byte(0x34)) {
        mask |= 1;
        if (write_byte((uint8_t)((reg << 1) | ((value >> 8) & 1u)))) {
            mask |= 2;
            if (write_byte((uint8_t)value)) mask |= 4;
        }
    }
    stop(); result->codec_ack_mask = mask;
    if (mask != 7) { result->failed_register = reg; return false; }
    ++result->writes_ok; return true;
}
static void checked_write(uint8_t reg, uint16_t value) {
    if (!codec_write(reg, value)) fail(E_CODEC);
}
static void bus_init(void) {
    input_pin(SDA); input_pin(SCL);
    /* Open drain only: external 3.3 V pull-ups supply the high level. */
    GPIO->PORT_OUTCLR = BIT(SDA) | BIT(SCL);
    if (!scl_high()) fail(E_BUS);
    for (unsigned i = 0; i < 9 && !high(SDA); ++i) {
        low(SCL); delay_us(5);
        if (!scl_high()) fail(E_BUS);
    }
    stop();
    result->bus_idle = (high(SDA) ? 1u : 0u) | (high(SCL) ? 2u : 0u);
    if (result->bus_idle != 3) fail(E_BUS);
}
static void scan_bus(void) {
    /* Address-only writes: no configuration data sent to unknown addresses. */
    low(SCL); delay_us(10);
    if (!high(SCL)) result->bus_drive_check |= 1;
    release(SCL); delay_us(10);
    if (high(SCL)) result->bus_drive_check |= 2;
    low(SCL); low(SDA); delay_us(10);
    if (!high(SDA)) result->bus_drive_check |= 4;
    release(SDA); delay_us(10);
    if (high(SDA)) result->bus_drive_check |= 8;
    stop();
    for (uint32_t address = 8; address < 0x78; ++address) {
        if (start() && write_byte((uint8_t)(address << 1))) {
            ++result->i2c_ack_count;
            result->i2c_ack_bitmap[address / 32] |= BIT(address % 32);
        }
        stop();
    }
}
static void codec_init(void) {
    checked_write(0, 0); delay_ms(10);
    checked_write(52, 0x140); checked_write(53, 0x140);
    checked_write(54, 0x140); checked_write(55, 0x140);
    checked_write(10, 0x040); /* soft mute DAC */
    checked_write(3, 0x00F);  /* stereo DACs and output mixers, no speakers */
    checked_write(1, 0x007);  /* BUFIO + fast VMID, bias still off */
    delay_ms(600);
    checked_write(1, 0x01D);  /* MICBIAS, BIAS, BUFIO, VMID 75k; PLL off */
    checked_write(2, 0x197);  /* headphones, left PGA/boost, both ADCs */
    checked_write(4, 0x010);  /* I2S, 16-bit, normal polarity */
    checked_write(5, 0x000);  /* no loopback until commanded */
    if (WM8978_PLL_FROM_12M) {
        /* Datasheet p76 example: 12 MHz -> 98.304 MHz VCO -> /4 -> /2.
         * Requires a physical jumper Y1.OUT -> empty Y2.OUT pad, via R73.
         * PLL N=8, K=0x3126E9, prescale /1; final SYSCLK=12.288 MHz.
         */
        checked_write(36, 0x008);
        checked_write(37, 0x00C);
        checked_write(38, 0x093);
        checked_write(39, 0x0E9);
        checked_write(1, 0x03D); /* add PLLEN, keep VMID and bias enabled */
        delay_ms(20);
        checked_write(6, 0x149); /* PLL, /2, BCLK /4, master */
    } else {
        checked_write(6, 0x009); /* direct 12.288 MHz, /1, BCLK /4, master */
    }
    checked_write(7, 0x000);  /* 48 kHz filter coefficients */
    checked_write(11, 0x1FF); checked_write(12, 0x1FF);
    checked_write(14, 0x100); /* ADC high-pass enabled */
    checked_write(15, 0x1FF); checked_write(16, 0x1FF);
    checked_write(44, 0x003); /* left LIP/LIN only, MICBIAS 0.9 AVDD */
    checked_write(45, 0x118); /* left PGA +6 dB, update */
    checked_write(46, 0x140); /* right PGA mute, update */
    checked_write(47, 0x000); /* no additional 20 dB boost */
    checked_write(48, 0x000);
    checked_write(49, 0x002); /* preserve thermal shutdown, boost off */
    checked_write(50, 0x001); checked_write(51, 0x001);
    delay_ms(100);
}
static void measure_clocks(void) {
    input_pin(10); input_pin(11);
    GPIO->PORT_CTRL |= BIT(10) | BIT(11); /* continuous GPIO sampling */
    uint32_t prev = high(11), edges = 0, t0 = 0, t1 = 0;
    uint32_t timeout = SysTick->VAL;
    while (((timeout - SysTick->VAL) & 0xFFFFFFu) < CPU_HZ / 10) {
        uint32_t level = high(11);
        if (level && !prev) {
            if (edges == 0) t0 = SysTick->VAL;
            if (++edges == 257) { t1 = SysTick->VAL; break; }
        }
        prev = level;
    }
    uint32_t ticks = (t0 - t1) & 0xFFFFFFu;
    if (edges != 257 || !ticks) fail(E_AUDIO_CLOCK);
    result->lrclk_hz_approx = (CPU_HZ / ticks) * 256u +
        ((CPU_HZ % ticks) * 256u) / ticks;
    prev = high(10);
    for (unsigned i = 0; i < 4096; ++i) {
        uint32_t level = high(10);
        if (level != prev) ++result->bclk_transitions;
        prev = level;
    }
    update_time();
    if (result->lrclk_hz_approx < 43000 || result->lrclk_hz_approx > 53000 ||
        !result->bclk_transitions) fail(E_AUDIO_CLOCK);
}
static bool i2s_sync(void) {
    uint32_t begin = SysTick->VAL;
    while (I2S_REGS->I2S_SYNCBUSY) {
        if (((begin - SysTick->VAL) & 0xFFFFFFu) > CPU_HZ / 10) return false;
    }
    return true;
}
static void mux_i2s(uint32_t pin) {
    uint32_t shift = (pin & 1u) * 4;
    GPIO->PORT_PMUX[pin / 2] = (GPIO->PORT_PMUX[pin / 2] & ~(15u << shift)) |
        (6u << shift); /* peripheral G */
    GPIO->PORT_PINCFG[pin] = PORT_PINCFG_PMUXEN_Msk | PORT_PINCFG_INEN_Msk;
}
static void audio_init(void) {
    PM_REGS->PM_APBCMASK |= PM_APBCMASK_I2S_Msk;
    GCLK_REGS->GCLK_CLKCTRL = GCLK_CLKCTRL_ID_I2S_0 |
        GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_CLKEN_Msk;
    if (!wait_gclk()) fail(E_CLOCK);
    I2S_REGS->I2S_CTRLA = I2S_CTRLA_SWRST_Msk;
    if (!i2s_sync()) fail(E_I2S);
    mux_i2s(7); mux_i2s(8); mux_i2s(10); mux_i2s(11);
    I2S_REGS->I2S_CLKCTRL[0] = I2S_CLKCTRL_SLOTSIZE_32 | I2S_CLKCTRL_NBSLOTS(1) |
        I2S_CLKCTRL_FSWIDTH_SLOT | I2S_CLKCTRL_BITDELAY_I2S |
        I2S_CLKCTRL_FSSEL_FSPIN | I2S_CLKCTRL_SCKSEL_SCKPIN;
    I2S_REGS->I2S_SERCTRL[0] = I2S_SERCTRL_SERMODE_TX |
        I2S_SERCTRL_DATASIZE_16 | I2S_SERCTRL_SLOTADJ_LEFT;
    I2S_REGS->I2S_SERCTRL[1] = I2S_SERCTRL_SERMODE_RX |
        I2S_SERCTRL_DATASIZE_16 | I2S_SERCTRL_SLOTADJ_LEFT;
    I2S_REGS->I2S_CTRLA = I2S_CTRLA_ENABLE_Msk;
    if (!i2s_sync()) fail(E_I2S);
    I2S_REGS->I2S_CTRLA |= I2S_CTRLA_CKEN0_Msk;
    if (!i2s_sync()) fail(E_I2S);
    I2S_REGS->I2S_CTRLA |= I2S_CTRLA_SEREN0_Msk | I2S_CTRLA_SEREN1_Msk;
    if (!i2s_sync()) fail(E_I2S);
    PM_REGS->PM_AHBMASK |= PM_AHBMASK_DMAC_Msk;
    PM_REGS->PM_APBBMASK |= PM_APBBMASK_DMAC_Msk;
    DMAC_REGS->DMAC_CTRL = DMAC_CTRL_SWRST_Msk;
    for (uint32_t i = 0; DMAC_REGS->DMAC_CTRL & DMAC_CTRL_SWRST_Msk; ++i)
        if (i > 100000) fail(E_DMA);
    desc[0].DMAC_BTCTRL = DMAC_BTCTRL_VALID_Msk | DMAC_BTCTRL_BEATSIZE_WORD |
        DMAC_BTCTRL_SRCINC_Msk | DMAC_BTCTRL_BLOCKACT_INT;
    desc[0].DMAC_BTCNT = 192;
    desc[0].DMAC_SRCADDR = (uint32_t)(tone + 192);
    desc[0].DMAC_DSTADDR = (uint32_t)&I2S_REGS->I2S_DATA[0];
    desc[0].DMAC_DESCADDR = (uint32_t)desc; /* circular */
    DMAC_REGS->DMAC_BASEADDR = (uint32_t)desc;
    DMAC_REGS->DMAC_WRBADDR = (uint32_t)wrb;
    DMAC_REGS->DMAC_CTRL = DMAC_CTRL_DMAENABLE_Msk | DMAC_CTRL_LVLEN0_Msk;
    DMAC_REGS->DMAC_CHID = 0;
    /* SAMD21 datasheet Table 22-11: 0x2B = I2S TX serializer 0. */
    DMAC_REGS->DMAC_CHCTRLB = DMAC_CHCTRLB_TRIGSRC(0x2B) | DMAC_CHCTRLB_TRIGACT_BEAT;
    DMAC_REGS->DMAC_CHCTRLA = DMAC_CHCTRLA_ENABLE_Msk;
}
static void fill_tone(void) {
    /* Integer triangle wave, peak +/-2048 (-24 dBFS), no math library. */
    for (uint32_t i = 0; i < 96; ++i) {
        int32_t l = (i < 48 ? (int32_t)i : (int32_t)(96 - i)) * 4096 / 48 - 2048;
        uint32_t rphase = (i * 2) % 96;
        int32_t r = (rphase < 48 ? (int32_t)rphase : (int32_t)(96 - rphase)) * 4096 / 48 - 2048;
        tone[2 * i] = (uint16_t)l;
        tone[2 * i + 1] = (uint16_t)r;
    }
}
static void set_mode(uint32_t mode) {
    checked_write(10, 0x040);
    checked_write(52, 0x140); checked_write(53, 0x140);
    checked_write(5, mode == 2 ? 1 : 0);
    if (mode == 1 || mode == 2) {
        checked_write(52, 0x024); checked_write(53, 0x124); /* -21 dB */
        checked_write(10, 0);
    }
    result->active_command = mode;
}
static void run(void) {
    fill_tone();
    result->stage = 1; clock_init();
    result->device_id = DSU_REGS->DSU_DID;
    GPIO->PORT_PINCFG[18] = 0;
    GPIO->PORT_OUTSET = BIT(18); GPIO->PORT_DIRSET = BIT(18);
    delay_ms(5);
    if (WM8978_PLL_FROM_12M) observe_y1();
    result->stage = 2; bus_init();
    scan_bus();
    if (WM8978_HARDWARE_I2C) { hw_init(); hw_scan(); }
    result->stage = 3; codec_init();
    if (WM8978_CONTROL_ONLY) {
        /* Supply/ACK check is useful with Y2 removed. Never start audio. */
        input_pin(10); input_pin(11);
        uint32_t previous = GPIO->PORT_IN;
        for (unsigned i = 0; i < 10000; ++i) {
            uint32_t sample = GPIO->PORT_IN;
            if ((sample ^ previous) & BIT(10)) ++result->bclk_transitions;
            previous = sample;
            delay_us(1);
        }
        checked_write(2, 0); checked_write(3, 0); checked_write(1, 0);
        GPIO->PORT_OUTCLR = BIT(18);
        result->stage = 7; result->done = 1;
        for (;;) __NOP();
    }
    result->stage = 4; measure_clocks();
    result->stage = 5; audio_init();
    result->adc_min[0] = result->adc_min[1] = 32767;
    result->adc_max[0] = result->adc_max[1] = -32768;
    result->stage = 6;
    uint32_t mode_start = 0, start_ms = millis;
    for (;;) {
        uint16_t flags = I2S_REGS->I2S_INTFLAG;
        if ((flags & I2S_INTFLAG_RXRDY1_Msk) &&
            !(I2S_REGS->I2S_SYNCBUSY & I2S_SYNCBUSY_DATA1_Msk)) {
            int32_t sample = (int16_t)I2S_REGS->I2S_DATA[1];
            unsigned ch = result->rx_words & 1u;
            if (sample < result->adc_min[ch]) result->adc_min[ch] = sample;
            if (sample > result->adc_max[ch]) result->adc_max[ch] = sample;
            if (sample) ++result->adc_nonzero[ch];
            ++result->rx_words;
        }
        if (flags & I2S_INTFLAG_RXOR1_Msk) {
            ++result->rx_overruns;
            I2S_REGS->I2S_INTFLAG = I2S_INTFLAG_RXOR1_Msk;
        }
        if (flags & I2S_INTFLAG_TXUR0_Msk) {
            ++result->tx_underruns;
            I2S_REGS->I2S_INTFLAG = I2S_INTFLAG_TXUR0_Msk;
        }
        if (DMAC_REGS->DMAC_CHINTFLAG & DMAC_CHINTFLAG_TCMPL_Msk) {
            ++result->dma_blocks;
            DMAC_REGS->DMAC_CHINTFLAG = DMAC_CHINTFLAG_TCMPL_Msk;
        }
        if (DMAC_REGS->DMAC_CHINTFLAG & DMAC_CHINTFLAG_TERR_Msk) fail(E_DMA);
        update_time();
        if (result->command != result->active_command) {
            set_mode(result->command); mode_start = millis;
        }
        if ((result->active_command == 1 || result->active_command == 2) &&
            millis - mode_start > 10000) {
            result->command = 0; set_mode(0);
        }
        if (millis - start_ms > 1000 && !result->rx_words) fail(E_I2S);
    }
}
void Reset_Handler(void) {
    __disable_irq();
    SCB->VTOR = (uint32_t)vectors;
    for (uint32_t *p = &__bss_start__; p < &__bss_end__; ++p) *p = 0;
    for (uint32_t i = 0; i < sizeof(report_t) / 4; ++i)
        ((volatile uint32_t *)result)[i] = 0;
    result->magic = 0x574D3837; result->version = 2;
    result->failed_register = 0xFFFFFFFF;
    run();
}
