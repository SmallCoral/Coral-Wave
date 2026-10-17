#include "tusb.h"
#include "sam.h"
#include <string.h>
static const tusb_desc_device_t device={
 .bLength=18,.bDescriptorType=TUSB_DESC_DEVICE,.bcdUSB=0x0200,.bMaxPacketSize0=64,
 .idVendor=0xCAFE,.idProduct=0xC0B0,.bcdDevice=0x0100,
 .iManufacturer=1,.iProduct=2,.iSerialNumber=3,.bNumConfigurations=1
};
static const uint8_t config[]={
 TUD_CONFIG_DESCRIPTOR(1,1,0,TUD_CONFIG_DESC_LEN+TUD_HID_DESC_LEN,0,100),
 TUD_HID_DESCRIPTOR(0,0,HID_ITF_PROTOCOL_NONE,29,0x83,8,10)
};
const uint8_t *tud_descriptor_device_cb(void){return (const uint8_t *)&device;}
const uint8_t *tud_descriptor_configuration_cb(uint8_t i){(void)i;return config;}
const uint16_t *tud_descriptor_string_cb(uint8_t index,uint16_t language){
 (void)language; static uint16_t result[64]; static char serial[33];
 const char *strings[]={"","SmallCoral","Coral-Wave Bootloader",serial};
 if(!index){result[0]=0x0304;result[1]=0x0409;return result;}
 if(index>3)return NULL;
 if(index==3&&!serial[0]){
  const uint32_t uid[]={*(const uint32_t *)0x0080A00C,*(const uint32_t *)0x0080A040,
    *(const uint32_t *)0x0080A044,*(const uint32_t *)0x0080A048};
  const char hex[]="0123456789ABCDEF";
  for(unsigned n=0;n<32;++n)serial[n]=hex[(uid[n/8]>>(28-4*(n%8)))&15];
 }
 unsigned len=strlen(strings[index]); for(unsigned n=0;n<len;++n)result[n+1]=strings[index][n];
 result[0]=0x0300|(2*len+2);return result;
}
