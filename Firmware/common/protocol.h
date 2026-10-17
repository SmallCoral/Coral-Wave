#ifndef CORAL_PROTOCOL_H
#define CORAL_PROTOCOL_H
#include <stdint.h>
#include <stdbool.h>
#define CW_PROTOCOL 1
#define CW_BOARD 0x53443231u
#define CW_VERSION 0x00030000u
#define CW_APP_BASE 0x4000u
#define CW_META 0x3FC00u
#define CW_APP_MAX (CW_META - CW_APP_BASE)
#define CW_SETTINGS_A 0x3FD00u
#define CW_SETTINGS_B 0x3FE00u
#define CW_BOOT_KEY ((volatile uint32_t *)0x200070F0)
#define CW_BOOT_MAGIC 0x43574254u
enum { CW_INFO=1, CW_STATUS=2, CW_GET_PARAMS=3, CW_SET_PARAMS=4,
  CW_SAVE_PARAMS=5, CW_BOOT=6, CW_REGISTERS=7,
  CW_HOLD=16, CW_BEGIN=17, CW_WRITE=18, CW_FINISH=19, CW_RUN=20, CW_ABORT=21 };
enum { CW_OK, CW_BAD_REQUEST, CW_BAD_STATE, CW_FLASH_ERROR, CW_CRC_ERROR, CW_CODEC_ERROR, CW_BUSY };
typedef struct { uint8_t magic[2], version, command, sequence, result, length, reserved, data[55]; } cw_packet_t;
typedef struct { uint32_t magic, length, crc, version, board, base, check; } cw_metadata_t;
/* Byte layout is shared with the Windows application. All gains are hardware codes. */
typedef struct {
  uint8_t schema, headphone, mic_pga, mic_boost, hpf, hpf_cut, oversample, eq_path;
  int8_t eq_gain[5];
  uint8_t eq_frequency[5], eq_wide, depth3d, limiter, limiter_level;
  uint8_t alc, alc_target, noise_gate, gate_threshold;
  uint8_t dac_attenuation, reserved[5];
} cw_params_t;
_Static_assert(sizeof(cw_packet_t) == 63, "HID report payload");
_Static_assert(sizeof(cw_params_t) == 32, "Parameter layout");
uint32_t cw_crc32(const void *data, uint32_t length);
bool cw_flash_erase(uint32_t address);
bool cw_flash_page(uint32_t address, const void *page);
void cw_hid_task(void);
void cw_command(const cw_packet_t *request, cw_packet_t *response);
void cw_info(cw_packet_t *response, bool bootloader);
void cw_reset_to_boot(void);
#endif
