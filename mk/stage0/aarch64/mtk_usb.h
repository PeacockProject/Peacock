#ifndef MK_STAGE0_MTK_USB_H
#define MK_STAGE0_MTK_USB_H

#include <stdint.h>

int mk_stage0_mtk_usb_fastboot_init(void);
void mk_stage0_mtk_usb_fastboot_poll(void);
uint8_t mk_stage0_mtk_usb_fastboot_downloading(void);
void mk_stage0_mtk_usb_set_serial_ascii(const char *serial);

#endif
