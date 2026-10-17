#ifndef CORAL_WAVE_AUDIO_H
#define CORAL_WAVE_AUDIO_H
#include <stdbool.h>
#include <stdint.h>

/* The first fourteen words retain the original bring-up mailbox layout. */
typedef struct {
  uint32_t magic, version, stage, error, millis, mounted, mount_count;
  uint32_t playback_active, capture_active, playback_bytes, capture_bytes;
  uint32_t control_requests, codec_ack, suspend_count;
  uint32_t lrclk_hz, dma_tx_blocks, dma_rx_blocks, tx_frames, rx_frames;
  uint32_t playback_underflows, capture_overflows, dma_errors;
  uint32_t i2s_tx_underruns, i2s_rx_overruns, play_fifo_frames;
  uint32_t capture_peak, capture_nonzero, codec_writes, failed_register;
  uint32_t dac_peak, feedback_16_16, left_adc_peak, right_adc_peak;
  uint32_t playback_overflows, capture_drops;
  uint32_t playback_gain_left_q15, playback_gain_right_q15, capture_gain_q15, current_dac_peak;
  uint32_t fault_pc, fault_lr, fault_xpsr, fault_exception_return;
} audio_status_t;
extern volatile audio_status_t *const status;
uint32_t tusb_time_millis_api(void);
void tusb_time_delay_ms_api(uint32_t ms);
bool audio_init(void);
void audio_task(void);
void audio_stream(unsigned path, bool active);
void audio_controls(unsigned path, const uint8_t *mute, const int16_t *volume);
bool audio_codec_write(uint8_t reg, uint16_t value);
uint16_t audio_codec_shadow(uint8_t reg);
void audio_quiesce(void);
void DMAC_Handler(void);
#endif
