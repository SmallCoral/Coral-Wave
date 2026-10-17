/* USB Audio Class 1.0, 48 kHz, 16-bit stereo playback and mono capture. */
#include <stdint.h>
#include <string.h>
#include "sam.h"
#include "tusb.h"

/* TinyUSB's development VID. Replace with an assigned VID for a product. */
static const tusb_desc_device_t device = {
  .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE,
  .bcdUSB = 0x0200, .bDeviceClass = 0, .bDeviceSubClass = 0, .bDeviceProtocol = 0,
  .bMaxPacketSize0 = 64, .idVendor = 0xCAFE, .idProduct = 0xC0A4,
  .bcdDevice = 0x0300, .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3,
  .bNumConfigurations = 1
};

#define AC_LEN (TUD_AUDIO10_DESC_INPUT_TERM_LEN * 2 + \
  TUD_AUDIO10_DESC_FEATURE_UNIT_LEN(2) + TUD_AUDIO10_DESC_FEATURE_UNIT_LEN(1) + \
  TUD_AUDIO10_DESC_OUTPUT_TERM_LEN * 2)
#define STREAM_LEN (TUD_AUDIO10_DESC_STD_AS_LEN * 2 + TUD_AUDIO10_DESC_CS_AS_INT_LEN + \
  TUD_AUDIO10_DESC_TYPE_I_FORMAT_LEN(1) + TUD_AUDIO10_DESC_STD_AS_ISO_EP_LEN + \
  TUD_AUDIO10_DESC_CS_AS_ISO_EP_LEN)
#define TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_AUDIO10_DESC_STD_AC_LEN + \
  TUD_AUDIO10_DESC_CS_AC_LEN(2) + AC_LEN + STREAM_LEN * 2 + TUD_AUDIO10_DESC_STD_AS_ISO_SYNC_EP_LEN + TUD_HID_DESC_LEN)

static const uint8_t configuration[] = {
  TUD_CONFIG_DESCRIPTOR(1, 4, 0, TOTAL_LEN, 0, 100),
  TUD_AUDIO10_DESC_STD_AC(0, 0, 0),
  TUD_AUDIO10_DESC_CS_AC(0x0100, AC_LEN, 1, 2),
  TUD_AUDIO10_DESC_INPUT_TERM(1, AUDIO_TERM_TYPE_USB_STREAMING, 0, 2,
    AUDIO10_CHANNEL_CONFIG_LEFT_FRONT | AUDIO10_CHANNEL_CONFIG_RIGHT_FRONT, 0, 4),
  TUD_AUDIO10_DESC_FEATURE_UNIT(2, 1, 0,
    AUDIO10_FU_CONTROL_BM_MUTE | AUDIO10_FU_CONTROL_BM_VOLUME, 0, 0),
  TUD_AUDIO10_DESC_OUTPUT_TERM(3, AUDIO_TERM_TYPE_OUT_HEADPHONES, 0, 2, 4),
  TUD_AUDIO10_DESC_INPUT_TERM(0x11, AUDIO_TERM_TYPE_IN_GENERIC_MIC, 0, 1, 0, 0, 5),
  TUD_AUDIO10_DESC_FEATURE_UNIT(0x12, 0x11, 0,
    AUDIO10_FU_CONTROL_BM_MUTE | AUDIO10_FU_CONTROL_BM_VOLUME, 0),
  TUD_AUDIO10_DESC_OUTPUT_TERM(0x13, AUDIO_TERM_TYPE_USB_STREAMING, 0, 0x12, 5),
  TUD_AUDIO10_DESC_STD_AS_INT(1, 0, 0, 4),
  TUD_AUDIO10_DESC_STD_AS_INT(1, 1, 2, 4),
  TUD_AUDIO10_DESC_CS_AS_INT(1, 1, AUDIO10_DATA_FORMAT_TYPE_I_PCM),
  TUD_AUDIO10_DESC_TYPE_I_FORMAT(2, 2, 16, 48000),
  TUD_AUDIO10_DESC_STD_AS_ISO_EP(0x01, TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_ASYNCHRONOUS,
    196, 1, 0x82),
  TUD_AUDIO10_DESC_CS_AS_ISO_EP(AUDIO10_CS_AS_ISO_DATA_EP_ATT_SAMPLING_FRQ, 0, 0),
  TUD_AUDIO10_DESC_STD_AS_ISO_SYNC_EP(0x82, 1),
  TUD_AUDIO10_DESC_STD_AS_INT(2, 0, 0, 5),
  TUD_AUDIO10_DESC_STD_AS_INT(2, 1, 1, 5),
  TUD_AUDIO10_DESC_CS_AS_INT(0x13, 1, AUDIO10_DATA_FORMAT_TYPE_I_PCM),
  TUD_AUDIO10_DESC_TYPE_I_FORMAT(1, 2, 16, 48000),
  TUD_AUDIO10_DESC_STD_AS_ISO_EP(0x81, TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_ASYNCHRONOUS,
    98, 1, 0),
  TUD_AUDIO10_DESC_CS_AS_ISO_EP(AUDIO10_CS_AS_ISO_DATA_EP_ATT_SAMPLING_FRQ, 0, 0),
  TUD_HID_DESCRIPTOR(3, 6, HID_ITF_PROTOCOL_NONE, 29, 0x83, 8, 10)
};
TU_VERIFY_STATIC(sizeof(configuration) == TOTAL_LEN, "Configuration length mismatch");

const uint8_t *tud_descriptor_device_cb(void) { return (const uint8_t *)&device; }
const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index; return configuration;
}
const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  static uint16_t descriptor[64];
  static char serial[33];
  const char *strings[] = { "", "SmallCoral", "Coral-Wave", serial,
    "Coral-Wave Playback", "Coral-Wave Microphone", "Coral-Wave Control" };
  if (index == 0) { descriptor[0] = (TUSB_DESC_STRING << 8) | 4; descriptor[1] = 0x0409; return descriptor; }
  if (index >= sizeof(strings) / sizeof(strings[0])) return NULL;
  if (index == 3 && serial[0] == 0) {
    const uint32_t uid[] = { *(const uint32_t *)0x0080A00C,
      *(const uint32_t *)0x0080A040, *(const uint32_t *)0x0080A044,
      *(const uint32_t *)0x0080A048 };
    const char hex[] = "0123456789ABCDEF";
    for (unsigned i = 0; i < 32; ++i) serial[i] = hex[(uid[i / 8] >> (28 - 4 * (i % 8))) & 15];
  }
  unsigned count = strlen(strings[index]);
  if (count > 63) count = 63;
  for (unsigned i = 0; i < count; ++i) descriptor[i + 1] = (uint8_t)strings[index][i];
  descriptor[0] = (TUSB_DESC_STRING << 8) | (2 * count + 2);
  return descriptor;
}
