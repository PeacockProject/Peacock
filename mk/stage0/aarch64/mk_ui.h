#ifndef MK_UI_H
#define MK_UI_H

#include <stdint.h>
#include "mk_fdt.h"  /* for simplefb_info_t */

/* Display OVL snapshot/restore */
void snapshot_display_ovl_state_once(void);
void mk_stage0_display_restore_for_linux(void);

/* Button/keypad init */
void init_menu_buttons_from_fdt(const void *fdt);
uint8_t vol_down_held(void);

/* Fastboot menu and rendering */
void render_fastboot_menu_overlay(const simplefb_info_t *info,
    uint32_t fallback_width, uint32_t fallback_height,
    uint32_t fallback_align, uint32_t menu_index, uint32_t secs_left,
    uint8_t continue_available);
uint8_t enter_fastboot_fallback(const simplefb_info_t *info,
    uint32_t fallback_width, uint32_t fallback_height,
    uint32_t fallback_align, uint8_t continue_available);

/* Offline charging */
#define MK_CHARGING_EXIT_BOOT  1U
#define MK_CHARGING_EXIT_MENU  2U
#define MK_CHARGING_EXIT_OFF   3U
uint8_t enter_offline_charging(const simplefb_info_t *info,
    uint32_t fallback_width, uint32_t fallback_height,
    uint32_t fallback_align);

/* Boot status display */
void mk_ui_set_boot_fb(const simplefb_info_t *info,
    uint32_t fallback_width, uint32_t fallback_height,
    uint32_t fallback_align);
void mk_ui_boot_status(const char *msg);

/* Splash/pattern drawing */
void draw_pattern(const simplefb_info_t *info,
    uint32_t fallback_width, uint32_t fallback_height,
    uint32_t fallback_align);
void try_direct_link_flip_and_disable_strip(const simplefb_info_t *info,
    uint32_t fallback_width, uint32_t fallback_height,
    uint32_t fallback_align);

#endif /* MK_UI_H */
