# Third-party sources

| Component | Source | License |
| --- | --- | --- |
| TinyUSB | [0.21.0](https://github.com/hathach/tinyusb/releases/tag/0.21.0), device stack source | MIT, [license](tinyusb/LICENSE) |
| SAMD21 headers | [hathach/microchip_driver at 9e8b37e](https://github.com/hathach/microchip_driver/tree/9e8b37e307d8404033bb881623a113931e1edf27/samd21/include), classic Microchip register API | Apache-2.0, header notices and [license](samd21/LICENSE) |
| CMSIS Core | [5.9.0](https://github.com/ARM-software/CMSIS_5/tree/5.9.0/CMSIS/Core/Include), required Cortex-M0+ headers | Apache-2.0, [license](cmsis/LICENSE) |

Local changes are limited to `tinyusb/src/portable/microchip/samd/dcd_samd.c`. The SAMD isochronous allocation hook validates static descriptor banks, activation uses the existing endpoint-open code, and packet-size selection now groups the shift before comparison. These changes allow UAC1 streaming on the Coral-Wave SAMD21 board. Other upstream files are unchanged.
