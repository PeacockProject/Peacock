#ifndef MK_STAGE0_MTK_USB_H
#define MK_STAGE0_MTK_USB_H

#include <stdint.h>

int mk_stage0_mtk_usb_fastboot_init(void);
void mk_stage0_mtk_usb_fastboot_poll(void);
void mk_stage0_mtk_usb_fastboot_quiesce(void);
void mk_stage0_mtk_usb_platform_restore_for_linux(void);
uint8_t mk_stage0_mtk_usb_fastboot_downloading(void);
uint8_t mk_stage0_mtk_usb_fastboot_take_action(void);
void mk_stage0_mtk_usb_set_serial_ascii(const char *serial);
void mk_stage0_mtk_usb_set_lk_bootargs(const char *bootargs);
const char *mk_stage0_mtk_usb_get_lk_bootargs(void);
void mk_stage0_fastboot_action_immediate(uint8_t action);
const uint8_t *mk_stage0_mtk_usb_fastboot_download_buf(void);
uint32_t mk_stage0_mtk_usb_fastboot_download_size(void);

#define MK_FASTBOOT_ACTION_NONE 0U
#define MK_FASTBOOT_ACTION_REBOOT 1U
#define MK_FASTBOOT_ACTION_REBOOT_BOOTLOADER 2U
#define MK_FASTBOOT_ACTION_CONTINUE 3U
#define MK_FASTBOOT_ACTION_REBOOT_RECOVERY 4U
#define MK_FASTBOOT_ACTION_BOOT_STAGED_KERNEL 5U

#endif
