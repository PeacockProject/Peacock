#include <stdint.h>
#include "mk_common.h"
#include "mk_fdt.h"
#include "mtk_panel.h"
#include "mk_wdt.h"
#include "mk_pmic.h"
#include "mtk_usb.h"
#include "mtk_gpio.h"
#include "peacock_logo_asset.h"
#include "mk_ui.h"

/* Timer functions from kernel_payload_main.c */
extern uint64_t read_cntfrq_el0(void);
extern uint64_t read_cntpct_el0(void);

/* ------------------------------------------------------------------ */
/* Keypad hardware codes                                               */
/* ------------------------------------------------------------------ */

#define KEY_VOLUMEUP 115U
#define KEY_VOLUMEDOWN 114U

#define KP_BASE 0x10010000ULL
#define KP_MEM1 0x0004U
#define KP_MEM2 0x0008U
#define KP_MEM3 0x000cU
#define KP_MEM4 0x0010U
#define KP_MEM5 0x0014U

/* ------------------------------------------------------------------ */
/* MediaTek OVL0 overlay plane registers                               */
/* ------------------------------------------------------------------ */

#define MTK_DISP_OVL0_BASE 0x1400b000ULL
#define MTK_DISP_OVL0_2L_BASE 0x1400c000ULL
#define MTK_DISP_OVL_TRIG 0x010U
#define MTK_DISP_OVL_SRC_CON 0x02cU
#define MTK_DISP_OVL_L0_CON 0x030U
#define MTK_DISP_OVL_L0_SRC_SIZE 0x038U
#define MTK_DISP_OVL_L0_OFFSET 0x03cU
#define MTK_DISP_OVL_L0_PITCH 0x044U
#define MTK_DISP_OVL_L0_ADDR 0x0f40U
#define MTK_DISP_OVL_L0_CLEAR 0x25cU
#define MTK_DISP_OVL_L3_CON 0x090U
#define MTK_DISP_OVL_L3_SRC_SIZE 0x098U
#define MTK_DISP_OVL_L3_OFFSET 0x09cU
#define MTK_DISP_OVL_L3_ADDR 0x0fa0U
#define MTK_DISP_OVL_L3_PITCH 0x0a4U
#define MTK_DISP_OVL_RDMA3_CTRL 0x120U
#define MTK_DISP_OVL_L3_CLEAR 0x268U
#define MTK_DISP_OVL_DATAPATH_EXT_CON 0x324U
#define MTK_DISP_OVL_EL2_CON 0x370U
#define MTK_DISP_OVL_EL2_SRC_SIZE 0x378U
#define MTK_DISP_OVL_EL2_OFFSET 0x37cU
#define MTK_DISP_OVL_EL2_ADDR 0x0fb8U
#define MTK_DISP_OVL_EL2_PITCH 0x384U
#define MTK_DISP_OVL_EL2_CLEAR 0x398U

#define MTK_OPPO_A16_WARNBUF_ADDR 0x41438fc0ULL
#define MTK_OPPO_A16_WARNBUF_WIDTH 720U
#define MTK_OPPO_A16_WARNBUF_HEIGHT 101U
#define MTK_OPPO_A16_WARNBUF_PITCH 1440U

#ifndef MK_DEVICE_HAS_FASTBOOT_USB
#define MK_DEVICE_HAS_FASTBOOT_USB 0
#endif

/* ------------------------------------------------------------------ */
/* Button state                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
	uint32_t vol_up_gpio;
	uint32_t vol_down_gpio;
	uint32_t vol_up_hwcode;
	uint32_t vol_down_hwcode;
	uint8_t last_up_raw;
	uint8_t last_down_raw;
	uint8_t last_power_raw;
	uint8_t stable_up_pressed;
	uint8_t stable_down_pressed;
	uint8_t stable_power_pressed;
	uint8_t up_stable_count;
	uint8_t down_stable_count;
	uint8_t power_stable_count;
	uint8_t has_any;
} menu_button_state_t;

static menu_button_state_t g_menu_buttons = {
	.vol_up_gpio = MK_STAGE0_GPIO_NONE,
	.vol_down_gpio = MK_STAGE0_GPIO_NONE,
	.vol_up_hwcode = 0xffffffffU,
	.vol_down_hwcode = 0xffffffffU,
	.last_up_raw = 0U,
	.last_down_raw = 0U,
	.last_power_raw = 0U,
	.stable_up_pressed = 0U,
	.stable_down_pressed = 0U,
	.stable_power_pressed = 0U,
	.up_stable_count = 0U,
	.down_stable_count = 0U,
	.power_stable_count = 0U,
	.has_any = 0U,
};

/* ------------------------------------------------------------------ */
/* Display OVL state                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
	uint8_t valid;
	uint32_t ovl0_src_con;
	uint32_t ovl0_l0_con;
	uint32_t ovl0_l0_src_size;
	uint32_t ovl0_l0_offset;
	uint32_t ovl0_l0_pitch;
	uint32_t ovl0_l0_addr;
	uint32_t ovl0_l3_con;
	uint32_t ovl0_l3_src_size;
	uint32_t ovl0_l3_offset;
	uint32_t ovl0_l3_addr;
	uint32_t ovl0_l3_pitch;
	uint32_t ovl0_datapath_ext_con;
	uint32_t ovl0_el2_con;
	uint32_t ovl0_el2_src_size;
	uint32_t ovl0_el2_offset;
	uint32_t ovl0_el2_addr;
	uint32_t ovl0_el2_pitch;
	uint32_t ovl02_src_con;
	uint32_t ovl02_l0_con;
	uint32_t ovl02_l0_src_size;
	uint32_t ovl02_l0_offset;
	uint32_t ovl02_l0_pitch;
	uint32_t ovl02_l0_addr;
} mk_stage0_display_ovl_state_t;

static mk_stage0_display_ovl_state_t g_display_ovl_state;

void snapshot_display_ovl_state_once(void)
{
	mk_stage0_display_ovl_state_t *s = &g_display_ovl_state;

	if (s->valid != 0U) {
		return;
	}

	s->ovl0_src_con = mmio_read32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_SRC_CON);
	s->ovl0_l0_con = mmio_read32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L0_CON);
	s->ovl0_l0_src_size = mmio_read32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L0_SRC_SIZE);
	s->ovl0_l0_offset = mmio_read32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L0_OFFSET);
	s->ovl0_l0_pitch = mmio_read32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L0_PITCH);
	s->ovl0_l0_addr = mmio_read32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L0_ADDR);
	s->ovl0_l3_con = mmio_read32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_CON);
	s->ovl0_l3_src_size = mmio_read32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_SRC_SIZE);
	s->ovl0_l3_offset = mmio_read32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_OFFSET);
	s->ovl0_l3_addr = mmio_read32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_ADDR);
	s->ovl0_l3_pitch = mmio_read32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_PITCH);
	s->ovl0_datapath_ext_con = mmio_read32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_DATAPATH_EXT_CON);
	s->ovl0_el2_con = mmio_read32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_EL2_CON);
	s->ovl0_el2_src_size = mmio_read32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_EL2_SRC_SIZE);
	s->ovl0_el2_offset = mmio_read32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_EL2_OFFSET);
	s->ovl0_el2_addr = mmio_read32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_EL2_ADDR);
	s->ovl0_el2_pitch = mmio_read32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_EL2_PITCH);
	s->ovl02_src_con = mmio_read32(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_SRC_CON);
	s->ovl02_l0_con = mmio_read32(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_CON);
	s->ovl02_l0_src_size = mmio_read32(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_SRC_SIZE);
	s->ovl02_l0_offset = mmio_read32(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_OFFSET);
	s->ovl02_l0_pitch = mmio_read32(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_PITCH);
	s->ovl02_l0_addr = mmio_read32(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_ADDR);
	s->valid = 1U;
	uart_puts_all("[mk] display ovl: snapshot\r\n");
}

void mk_stage0_display_restore_for_linux(void)
{
	mk_stage0_display_ovl_state_t *s = &g_display_ovl_state;

	if (s->valid == 0U) {
		uart_puts_all("[mk] display ovl: no snapshot\r\n");
		return;
	}

	mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_SRC_CON, s->ovl0_src_con);
	mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L0_CON, s->ovl0_l0_con);
	mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L0_SRC_SIZE, s->ovl0_l0_src_size);
	mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L0_OFFSET, s->ovl0_l0_offset);
	mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L0_PITCH, s->ovl0_l0_pitch);
	mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L0_ADDR, s->ovl0_l0_addr);
	mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_CON, s->ovl0_l3_con);
	mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_SRC_SIZE, s->ovl0_l3_src_size);
	mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_OFFSET, s->ovl0_l3_offset);
	mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_ADDR, s->ovl0_l3_addr);
	mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_PITCH, s->ovl0_l3_pitch);
	mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_DATAPATH_EXT_CON,
			 s->ovl0_datapath_ext_con);
	mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_EL2_CON, s->ovl0_el2_con);
	mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_EL2_SRC_SIZE, s->ovl0_el2_src_size);
	mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_EL2_OFFSET, s->ovl0_el2_offset);
	mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_EL2_ADDR, s->ovl0_el2_addr);
	mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_EL2_PITCH, s->ovl0_el2_pitch);
	mmio_write32(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_SRC_CON, s->ovl02_src_con);
	mmio_write32(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_CON, s->ovl02_l0_con);
	mmio_write32(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_SRC_SIZE, s->ovl02_l0_src_size);
	mmio_write32(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_OFFSET, s->ovl02_l0_offset);
	mmio_write32(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_PITCH, s->ovl02_l0_pitch);
	mmio_write32(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_ADDR, s->ovl02_l0_addr);
	mmio_write32(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_TRIG, 1U);
	mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_TRIG, 1U);
	uart_puts_all("[mk] display ovl: restored\r\n");
}

/* ------------------------------------------------------------------ */
/* Keypad / button helpers                                             */
/* ------------------------------------------------------------------ */

static uint32_t parse_gpio_pin_from_prop(const uint8_t *value, uint32_t len, uint32_t fallback)
{
	if (value == 0 || len < 8U) {
		return fallback;
	}
	return be32_read(value + 4);
}

static void keypad_raw_read(uint16_t state[5])
{
	state[0] = (uint16_t) mmio_read32(KP_BASE + KP_MEM1);
	state[1] = (uint16_t) mmio_read32(KP_BASE + KP_MEM2);
	state[2] = (uint16_t) mmio_read32(KP_BASE + KP_MEM3);
	state[3] = (uint16_t) mmio_read32(KP_BASE + KP_MEM4);
	state[4] = (uint16_t) mmio_read32(KP_BASE + KP_MEM5);
}

static uint8_t keypad_hwcode_pressed(uint32_t hwcode)
{
	uint16_t state[5];
	uint32_t idx;
	uint32_t bit;

	if (hwcode == 0xffffffffU || hwcode >= 72U) {
		return 0U;
	}

	keypad_raw_read(state);
	idx = hwcode / 16U;
	bit = hwcode % 16U;
	if (idx >= 5U) {
		return 0U;
	}
	return ((state[idx] & (uint16_t) (1U << bit)) == 0U) ? 1U : 0U;
}

void init_menu_buttons_from_fdt(const void *fdt)
{
	const uint8_t *value = 0;
	uint32_t len = 0;
	uint32_t map_num = 0U;
	uint32_t i;

	g_menu_buttons.vol_up_gpio = MK_STAGE0_GPIO_NONE;
	g_menu_buttons.vol_down_gpio = MK_STAGE0_GPIO_NONE;
	g_menu_buttons.vol_up_hwcode = 0xffffffffU;
	g_menu_buttons.vol_down_hwcode = 0xffffffffU;
	g_menu_buttons.last_up_raw = 0U;
	g_menu_buttons.last_down_raw = 0U;
	g_menu_buttons.last_power_raw = 0U;
	g_menu_buttons.stable_up_pressed = 0U;
	g_menu_buttons.stable_down_pressed = 0U;
	g_menu_buttons.stable_power_pressed = 0U;
	g_menu_buttons.up_stable_count = 0U;
	g_menu_buttons.down_stable_count = 0U;
	g_menu_buttons.power_stable_count = 0U;
	g_menu_buttons.has_any = 0U;

	if (mk_fdt_find_compatible_prop(fdt, "mediatek,kp", "keypad,volume-up", &value, &len)) {
		g_menu_buttons.vol_up_gpio = parse_gpio_pin_from_prop(value, len, MK_STAGE0_GPIO_NONE);
	}
	if (mk_fdt_find_compatible_prop(fdt, "mediatek,kp", "keypad,volume-down", &value, &len)) {
		g_menu_buttons.vol_down_gpio = parse_gpio_pin_from_prop(value, len, MK_STAGE0_GPIO_NONE);
	}

	if (mk_fdt_find_compatible_prop(fdt, "mediatek,kp", "mediatek,kpd-hw-map-num", &value, &len) &&
	    len >= 4U) {
		map_num = be32_read(value);
		if (map_num > 72U) {
			map_num = 72U;
		}
	}
	if (map_num != 0U &&
	    mk_fdt_find_compatible_prop(fdt, "mediatek,kp", "mediatek,kpd-hw-init-map", &value, &len) &&
	    len >= (map_num * 4U)) {
		for (i = 0; i < map_num; i++) {
			uint32_t keycode = be32_read(value + (i * 4U));
			if (keycode == KEY_VOLUMEUP && g_menu_buttons.vol_up_hwcode == 0xffffffffU) {
				g_menu_buttons.vol_up_hwcode = i;
			}
			if (keycode == KEY_VOLUMEDOWN && g_menu_buttons.vol_down_hwcode == 0xffffffffU) {
				g_menu_buttons.vol_down_hwcode = i;
			}
		}
	}

	if (g_menu_buttons.vol_up_gpio != MK_STAGE0_GPIO_NONE ||
	    g_menu_buttons.vol_down_gpio != MK_STAGE0_GPIO_NONE ||
	    g_menu_buttons.vol_up_hwcode != 0xffffffffU ||
	    g_menu_buttons.vol_down_hwcode != 0xffffffffU) {
		g_menu_buttons.has_any = 1U;
	}
	if (mmio_read32(0x1000d000ULL + 0x0a0U) != 0U) {
		g_menu_buttons.has_any = 1U;
	}

	if (g_menu_buttons.has_any != 0U) {
		uart_puts_all("[mk] menu keys up.gpio=0x");
		uart_puthex64_all(g_menu_buttons.vol_up_gpio);
		uart_puts_all(" down.gpio=0x");
		uart_puthex64_all(g_menu_buttons.vol_down_gpio);
		uart_puts_all(" up.hw=0x");
		uart_puthex64_all(g_menu_buttons.vol_up_hwcode);
		uart_puts_all(" down.hw=0x");
		uart_puthex64_all(g_menu_buttons.vol_down_hwcode);
		uart_puts_all(" pwrap.init=0x");
		uart_puthex64_all(mmio_read32(0x1000d000ULL + 0x0a0U));
		uart_puts_all("\r\n");
	}
}

static uint8_t menu_button_is_pressed_gpio(uint32_t pin)
{
	if (pin == MK_STAGE0_GPIO_NONE) {
		return 0U;
	}
	return mk_stage0_mtk_gpio_read(pin) == 0U ? 1U : 0U;
}

uint8_t vol_down_held(void)
{
	if (g_menu_buttons.vol_down_hwcode != 0xffffffffU) {
		return keypad_hwcode_pressed(g_menu_buttons.vol_down_hwcode);
	}
	if (g_menu_buttons.vol_down_gpio != MK_STAGE0_GPIO_NONE) {
		return menu_button_is_pressed_gpio(g_menu_buttons.vol_down_gpio);
	}
	return 0U;
}

static uint8_t menu_update_stable_signal(uint8_t raw, uint8_t *last_raw,
					 uint8_t *stable, uint8_t *stable_count)
{
	uint8_t edge = 0U;

	if (raw == *last_raw) {
		if (*stable_count < 5U) {
			*stable_count = (uint8_t) (*stable_count + 1U);
		}
	} else {
		*last_raw = raw;
		*stable_count = 0U;
	}

	if (*stable_count >= 3U && *stable != raw) {
		*stable = raw;
		if (raw != 0U) {
			edge = 1U;
		}
	}

	return edge;
}

static void menu_buttons_poll_edges(uint8_t *up_pressed_edge, uint8_t *down_pressed_edge,
				    uint8_t *select_pressed_edge)
{
	uint8_t up_raw = 0U;
	uint8_t down_raw = 0U;
	uint8_t power_raw = 0U;
	uint8_t home_up_raw = 0U;

	if (up_pressed_edge == 0 || down_pressed_edge == 0 || select_pressed_edge == 0) {
		return;
	}
	*up_pressed_edge = 0U;
	*down_pressed_edge = 0U;
	*select_pressed_edge = 0U;

	if (g_menu_buttons.has_any == 0U) {
		return;
	}

	/* Prefer keypad HW map over GPIO to avoid floating GPIO-only reads. */
	if (g_menu_buttons.vol_up_hwcode != 0xffffffffU) {
		up_raw = keypad_hwcode_pressed(g_menu_buttons.vol_up_hwcode);
	} else if (g_menu_buttons.vol_up_gpio != MK_STAGE0_GPIO_NONE) {
		up_raw = menu_button_is_pressed_gpio(g_menu_buttons.vol_up_gpio);
	} else {
		home_up_raw = mk_pmic_homekey_pressed();
		up_raw = home_up_raw;
	}

	if (g_menu_buttons.vol_down_hwcode != 0xffffffffU) {
		down_raw = keypad_hwcode_pressed(g_menu_buttons.vol_down_hwcode);
	} else if (g_menu_buttons.vol_down_gpio != MK_STAGE0_GPIO_NONE) {
		down_raw = menu_button_is_pressed_gpio(g_menu_buttons.vol_down_gpio);
	}

	power_raw = mk_pmic_power_pressed();

	*up_pressed_edge = menu_update_stable_signal(up_raw, &g_menu_buttons.last_up_raw,
						     &g_menu_buttons.stable_up_pressed,
						     &g_menu_buttons.up_stable_count);
	*down_pressed_edge = menu_update_stable_signal(down_raw, &g_menu_buttons.last_down_raw,
						       &g_menu_buttons.stable_down_pressed,
						       &g_menu_buttons.down_stable_count);
	*select_pressed_edge = menu_update_stable_signal(power_raw, &g_menu_buttons.last_power_raw,
							 &g_menu_buttons.stable_power_pressed,
							 &g_menu_buttons.power_stable_count);

	(void) home_up_raw;
}

/* ------------------------------------------------------------------ */
/* Menu rendering helpers                                              */
/* ------------------------------------------------------------------ */

static uint32_t menu_bpp_from_format(const char *fmt)
{
	if (fmt != 0 && str_eq(fmt, "r5g6b5")) {
		return 2U;
	}
	return 4U;
}

static uint32_t menu_resolve_fb(const simplefb_info_t *info, uint32_t fallback_width,
				uint32_t fallback_height, uint32_t fallback_align,
				volatile uint8_t **out_fb, uint32_t *out_w,
				uint32_t *out_h, uint32_t *out_stride)
{
	uint32_t w;
	uint32_t h;
	uint32_t stride;

	if (info == 0 || out_fb == 0 || out_w == 0 || out_h == 0 || out_stride == 0) {
		return 0U;
	}
	if (info->addr == 0U || menu_bpp_from_format(info->format) != 4U) {
		return 0U;
	}

	w = info->width;
	h = info->height;
	stride = info->stride;
	if ((w == 0U || h == 0U) && fallback_width != 0U && fallback_height != 0U) {
		w = fallback_width;
		h = fallback_height;
		if (stride == 0U) {
			stride = align_up_u32(w, fallback_align) * 4U;
		}
	}
	if (w == 0U || h == 0U || stride == 0U) {
		return 0U;
	}

	*out_fb = (volatile uint8_t *) (uintptr_t) info->addr;
	*out_w = w;
	*out_h = h;
	*out_stride = stride;
	return 1U;
}

static void menu_fill_rect32(volatile uint8_t *fb, uint32_t stride,
			     uint32_t fb_w, uint32_t fb_h,
			     uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t argb)
{
	uint32_t iy;
	uint32_t ix;
	uint32_t x_end;
	uint32_t y_end;

	if (fb == 0 || w == 0U || h == 0U || x >= fb_w || y >= fb_h) {
		return;
	}

	x_end = x + w;
	y_end = y + h;
	if (x_end > fb_w) {
		x_end = fb_w;
	}
	if (y_end > fb_h) {
		y_end = fb_h;
	}

	for (iy = y; iy < y_end; iy++) {
		volatile uint32_t *line = (volatile uint32_t *) (fb + ((uint64_t) iy * stride));
		for (ix = x; ix < x_end; ix++) {
			line[ix] = argb;
		}
	}
}

static uint8_t menu_glyph_row_5x7(char c, uint32_t row)
{
	static const uint8_t font5x7[128][7] = {
		['A'] = {0x0eU, 0x11U, 0x11U, 0x1fU, 0x11U, 0x11U, 0x11U},
		['B'] = {0x1eU, 0x11U, 0x11U, 0x1eU, 0x11U, 0x11U, 0x1eU},
		['C'] = {0x0eU, 0x11U, 0x10U, 0x10U, 0x10U, 0x11U, 0x0eU},
		['D'] = {0x1eU, 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x1eU},
		['E'] = {0x1fU, 0x10U, 0x10U, 0x1eU, 0x10U, 0x10U, 0x1fU},
		['F'] = {0x1fU, 0x10U, 0x10U, 0x1eU, 0x10U, 0x10U, 0x10U},
		['G'] = {0x0fU, 0x10U, 0x10U, 0x13U, 0x11U, 0x11U, 0x0fU},
		['H'] = {0x11U, 0x11U, 0x11U, 0x1fU, 0x11U, 0x11U, 0x11U},
		['I'] = {0x1fU, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U, 0x1fU},
		['J'] = {0x01U, 0x01U, 0x01U, 0x01U, 0x11U, 0x11U, 0x0eU},
		['K'] = {0x11U, 0x12U, 0x14U, 0x18U, 0x14U, 0x12U, 0x11U},
		['L'] = {0x10U, 0x10U, 0x10U, 0x10U, 0x10U, 0x10U, 0x1fU},
		['M'] = {0x11U, 0x1bU, 0x15U, 0x15U, 0x11U, 0x11U, 0x11U},
		['N'] = {0x11U, 0x11U, 0x19U, 0x15U, 0x13U, 0x11U, 0x11U},
		['O'] = {0x0eU, 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x0eU},
		['P'] = {0x1eU, 0x11U, 0x11U, 0x1eU, 0x10U, 0x10U, 0x10U},
		['Q'] = {0x0eU, 0x11U, 0x11U, 0x11U, 0x15U, 0x12U, 0x0dU},
		['R'] = {0x1eU, 0x11U, 0x11U, 0x1eU, 0x14U, 0x12U, 0x11U},
		['S'] = {0x0fU, 0x10U, 0x10U, 0x0eU, 0x01U, 0x01U, 0x1eU},
		['T'] = {0x1fU, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U},
		['U'] = {0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x0eU},
		['V'] = {0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x0aU, 0x04U},
		['W'] = {0x11U, 0x11U, 0x11U, 0x15U, 0x15U, 0x15U, 0x0aU},
		['X'] = {0x11U, 0x11U, 0x0aU, 0x04U, 0x0aU, 0x11U, 0x11U},
		['Y'] = {0x11U, 0x11U, 0x0aU, 0x04U, 0x04U, 0x04U, 0x04U},
		['Z'] = {0x1fU, 0x01U, 0x02U, 0x04U, 0x08U, 0x10U, 0x1fU},
		['0'] = {0x0eU, 0x11U, 0x13U, 0x15U, 0x19U, 0x11U, 0x0eU},
		['1'] = {0x04U, 0x0cU, 0x04U, 0x04U, 0x04U, 0x04U, 0x0eU},
		['2'] = {0x0eU, 0x11U, 0x01U, 0x02U, 0x04U, 0x08U, 0x1fU},
		['3'] = {0x1eU, 0x01U, 0x01U, 0x0eU, 0x01U, 0x01U, 0x1eU},
		['4'] = {0x02U, 0x06U, 0x0aU, 0x12U, 0x1fU, 0x02U, 0x02U},
		['5'] = {0x1fU, 0x10U, 0x10U, 0x1eU, 0x01U, 0x01U, 0x1eU},
		['6'] = {0x0eU, 0x10U, 0x10U, 0x1eU, 0x11U, 0x11U, 0x0eU},
		['7'] = {0x1fU, 0x01U, 0x02U, 0x04U, 0x08U, 0x08U, 0x08U},
		['8'] = {0x0eU, 0x11U, 0x11U, 0x0eU, 0x11U, 0x11U, 0x0eU},
		['9'] = {0x0eU, 0x11U, 0x11U, 0x0fU, 0x01U, 0x01U, 0x0eU},
		['-'] = {0x00U, 0x00U, 0x00U, 0x0eU, 0x00U, 0x00U, 0x00U},
		[':'] = {0x00U, 0x04U, 0x00U, 0x00U, 0x04U, 0x00U, 0x00U},
		['>'] = {0x00U, 0x10U, 0x08U, 0x04U, 0x08U, 0x10U, 0x00U},
	};
	uint8_t uc = (uint8_t) c;

	if (row >= 7U) {
		return 0U;
	}
	if (uc >= 'a' && uc <= 'z') {
		uc = (uint8_t) (uc - 'a' + 'A');
	}
	if (uc >= 128U) {
		return 0U;
	}
	return font5x7[uc][row];
}

static void menu_draw_text_5x7(volatile uint8_t *fb, uint32_t stride,
			       uint32_t fb_w, uint32_t fb_h, uint32_t x, uint32_t y,
			       uint32_t scale, uint32_t argb, const char *text)
{
	uint32_t i;
	uint32_t row;
	uint32_t col;
	uint32_t sx;
	uint32_t sy;
	uint64_t write_count = 0U;
	char first = '\0';
	uint8_t first_bits = 0U;
	uint8_t first_logged = 0U;

	if (fb == 0 || text == 0 || scale == 0U) {
		return;
	}

	for (i = 0; text[i] != '\0'; i++) {
		uint32_t char_x = x + i * (6U * scale);
		char c = text[i];
		if (c >= 'a' && c <= 'z') {
			c = (char) (c - 'a' + 'A');
		}
		for (row = 0; row < 7U; row++) {
			uint8_t bits = menu_glyph_row_5x7(c, row);
			if (first_logged == 0U && c != ' ' && row == 0U) {
				first = c;
				first_bits = bits;
				first_logged = 1U;
			}
			for (col = 0; col < 5U; col++) {
				if ((bits & (1U << (4U - col))) == 0U) {
					continue;
				}
				for (sy = 0; sy < scale; sy++) {
					uint32_t py = y + row * scale + sy;
					volatile uint32_t *line;
					if (py >= fb_h) {
						continue;
					}
					line = (volatile uint32_t *) (fb + ((uint64_t) py * stride));
					for (sx = 0; sx < scale; sx++) {
						uint32_t px = char_x + col * scale + sx;
						if (px < fb_w) {
							line[px] = argb;
							write_count++;
						}
					}
				}
			}
		}
	}
	uart_puts_all("[mk] menu text dbg first=0x");
	uart_puthex64_all((uint64_t) (uint8_t) first);
	uart_puts_all(" bits0=0x");
	uart_puthex64_all(first_bits);
	uart_puts_all(" writes=0x");
	uart_puthex64_all(write_count);
	uart_puts_all("\r\n");
}

static void menu_draw_dbg_step(const char *tag)
{
	uart_puts_all("[mk] menu draw step=");
	uart_puts_all(tag);
	uart_puts_all("\r\n");
	delay_ms_calibrated(3U);
}

static uint32_t menu_u32_to_dec(char *dst, uint32_t cap, uint32_t value)
{
	char tmp[16];
	uint32_t n = 0U;
	uint32_t i;

	if (dst == 0 || cap < 2U) {
		return 0U;
	}
	if (value == 0U) {
		dst[0] = '0';
		dst[1] = '\0';
		return 1U;
	}

	while (value != 0U && n < (uint32_t) sizeof(tmp)) {
		tmp[n++] = (char) ('0' + (value % 10U));
		value /= 10U;
	}
	if (n + 1U > cap) {
		n = cap - 1U;
	}
	for (i = 0; i < n; i++) {
		dst[i] = tmp[n - 1U - i];
	}
	dst[n] = '\0';
	return n;
}

/* ------------------------------------------------------------------ */
/* Fastboot menu items                                                 */
/* ------------------------------------------------------------------ */

static uint32_t fastboot_menu_item_count(uint8_t continue_available)
{
	return (continue_available != 0U) ? 3U : 2U;
}

static const char *fastboot_menu_label(uint8_t continue_available, uint32_t menu_index)
{
	if (continue_available != 0U) {
		if (menu_index == 0U) {
			return "continue-boot";
		}
		if (menu_index == 1U) {
			return "reboot-recovery";
		}
		return "power-off";
	}

	if (menu_index == 0U) {
		return "reboot-recovery";
	}
	return "power-off";
}

static const char *fastboot_menu_row_text(uint8_t continue_available, uint32_t menu_index)
{
	if (continue_available != 0U) {
		if (menu_index == 0U) {
			return "CONTINUE BOOT";
		}
		if (menu_index == 1U) {
			return "REBOOT RECOVERY";
		}
		return "POWER OFF";
	}

	if (menu_index == 0U) {
		return "REBOOT RECOVERY";
	}
	return "POWER OFF";
}

static uint8_t fastboot_menu_select_action(uint8_t continue_available, uint32_t menu_index)
{
	if (continue_available != 0U) {
		if (menu_index == 0U) {
			return MK_FASTBOOT_ACTION_CONTINUE;
		}
		if (menu_index == 1U) {
			return MK_FASTBOOT_ACTION_REBOOT_RECOVERY;
		}
		return MK_FASTBOOT_ACTION_POWEROFF;
	}

	if (menu_index == 0U) {
		return MK_FASTBOOT_ACTION_REBOOT_RECOVERY;
	}
	return MK_FASTBOOT_ACTION_POWEROFF;
}

/* ------------------------------------------------------------------ */
/* render_fastboot_menu_overlay                                        */
/* ------------------------------------------------------------------ */

void render_fastboot_menu_overlay(const simplefb_info_t *info,
					 uint32_t fallback_width, uint32_t fallback_height,
					 uint32_t fallback_align, uint32_t menu_index, uint32_t secs_left,
					 uint8_t continue_available)
{
	volatile uint8_t *fb = 0;
	volatile uint8_t *fb_page1 = 0;
	uint32_t w = 0U;
	uint32_t h = 0U;
	uint32_t stride = 0U;
	uint32_t x0 = 20U;
	uint32_t y0 = 0U;
	uint32_t box_w = 680U;
	uint32_t box_h = (continue_available != 0U) ? 304U : 250U;
	uint32_t bg = 0xf0181818U;
	uint32_t accent = 0xff2e8b57U;
	uint32_t row_sel = 0xff2e8b57U;
	uint32_t row_unsel = 0xff303030U;
	uint32_t fg = 0xffffffffU;
	uint32_t fg_sel = 0xff081808U;
	uint32_t item_count = fastboot_menu_item_count(continue_available);
	uint32_t row_y[3] = {44U, 98U, 152U};
	uint32_t help_y = (continue_available != 0U) ? 220U : 166U;
	uint64_t ovl_addr = 0U;
	uint64_t ovl0_l0_addr = 0U;
	uint64_t ovl0_l3_addr = 0U;
	uint64_t ovl0_el2_addr = 0U;
	uint64_t ovl0_l0_pitch = 0U;
	uint64_t ovl02_l0_pitch = 0U;
	uint64_t ovl0_src = 0U;
	uint64_t ovl02_src = 0U;
	uint64_t fb_base = 0U;
	uint64_t fb_limit = 0U;
	uint64_t sample_before = 0U;
	uint64_t sample_after = 0U;
	uint64_t page_bytes = 0U;
	uint64_t flush_len;
	uintptr_t flush_start;
	char secs_buf[16];
	char countdown[40];
	const char *help_text = "DOWN NEXT  UP PREV  PWR SELECT";
	uint32_t secs_len;
	uint32_t i;

	if (menu_resolve_fb(info, fallback_width, fallback_height, fallback_align, &fb, &w, &h, &stride) == 0U) {
		uart_puts_all("[mk] menu draw skip: fb unresolved\r\n");
		return;
	}

	uart_puts_all("[mk] menu draw px begin fb=0x");
	uart_puthex64_all((uint64_t) (uintptr_t) fb);
	uart_puts_all(" stride=0x");
	uart_puthex64_all(stride);
	uart_puts_all(" wh=0x");
	uart_puthex64_all(w);
	uart_puts_all("x");
	uart_puthex64_all(h);
	uart_puts_all("\r\n");

	if (w == 0U || h == 0U || stride < 4U || w <= x0 || h <= y0) {
		uart_puts_all("[mk] menu draw skip: bad geometry\r\n");
		return;
	}
	if (box_w > (w - x0)) {
		box_w = w - x0;
	}
	if (box_h > (h - y0)) {
		box_h = h - y0;
	}
	/* Keep the menu docked near the bottom edge. */
	if (h > box_h + 24U) {
		y0 = h - box_h - 24U;
	} else {
		y0 = 0U;
	}
	if (box_w < 80U || box_h < 120U) {
		uart_puts_all("[mk] menu draw skip: box too small\r\n");
		return;
	}
	page_bytes = (uint64_t) stride * h;
	fb_base = (uint64_t) (uintptr_t) fb;
	fb_limit = fb_base + ((info != 0 && info->size != 0U) ? info->size : page_bytes);
	snapshot_display_ovl_state_once();
	ovl0_src = mmio_read32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_SRC_CON);
	ovl02_src = mmio_read32(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_SRC_CON);
	ovl_addr = mmio_read32(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_ADDR);
	ovl0_l0_addr = mmio_read32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L0_ADDR);
	ovl0_l3_addr = mmio_read32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_ADDR);
	ovl0_el2_addr = mmio_read32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_EL2_ADDR);
	ovl0_l0_pitch = mmio_read32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L0_PITCH);
	ovl02_l0_pitch = mmio_read32(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_PITCH);
	uart_puts_all("[mk] menu draw ovl.l0=0x");
	uart_puthex64_all(ovl_addr);
	uart_puts_all(" fb_base=0x");
	uart_puthex64_all(fb_base);
	uart_puts_all(" fb_limit=0x");
	uart_puthex64_all(fb_limit);
	uart_puts_all(" page_bytes=0x");
	uart_puthex64_all(page_bytes);
	uart_puts_all("\r\n");
	uart_puts_all("[mk] menu draw ovl src0=0x");
	uart_puthex64_all(ovl0_src);
	uart_puts_all(" src2l=0x");
	uart_puthex64_all(ovl02_src);
	uart_puts_all(" l0=0x");
	uart_puthex64_all(ovl0_l0_addr);
	uart_puts_all(" l3=0x");
	uart_puthex64_all(ovl0_l3_addr);
	uart_puts_all(" el2=0x");
	uart_puthex64_all(ovl0_el2_addr);
	uart_puts_all(" p0=0x");
	uart_puthex64_all(ovl0_l0_pitch);
	uart_puts_all(" p2l=0x");
	uart_puthex64_all(ovl02_l0_pitch);
	uart_puts_all("\r\n");
	delay_ms_calibrated(3U);
	if ((ovl0_src & 0x0eU) != 0U) {
		mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_SRC_CON, 0x1U);
		mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_CON, 0U);
		mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_SRC_SIZE, 0U);
		mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_OFFSET, 0U);
		mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_ADDR, 0U);
		mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_PITCH, 0U);
		mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_CLEAR, 1U);
		mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_TRIG, 1U);
		uart_puts_all("[mk] menu draw: forced ovl0 src=l0-only\r\n");
		uart_puts_all("[mk] menu draw: ovl0 src now=0x");
		uart_puthex64_all(mmio_read32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_SRC_CON));
		uart_puts_all("\r\n");
		delay_ms_calibrated(3U);
	}
	if ((ovl0_l0_pitch & 0xffffU) >= (uint64_t) (w * 4U) &&
	    (ovl0_l0_pitch & 0xffffU) <= (uint64_t) (w * 6U)) {
		stride = (uint32_t) (ovl0_l0_pitch & 0xffffU);
		uart_puts_all("[mk] menu draw: using ovl0 pitch stride=0x");
		uart_puthex64_all(stride);
		uart_puts_all("\r\n");
		delay_ms_calibrated(3U);
	} else if ((ovl02_l0_pitch & 0xffffU) >= (uint64_t) (w * 4U) &&
		   (ovl02_l0_pitch & 0xffffU) <= (uint64_t) (w * 6U)) {
		stride = (uint32_t) (ovl02_l0_pitch & 0xffffU);
		uart_puts_all("[mk] menu draw: using ovl0_2l pitch stride=0x");
		uart_puthex64_all(stride);
		uart_puts_all("\r\n");
		delay_ms_calibrated(3U);
	}
	if (ovl_addr >= fb_base && (ovl_addr + (uint64_t) stride) <= fb_limit) {
		fb = (volatile uint8_t *) (uintptr_t) ovl_addr;
		uart_puts_all("[mk] menu draw using active l0 fb=0x");
		uart_puthex64_all((uint64_t) (uintptr_t) fb);
		uart_puts_all("\r\n");
		delay_ms_calibrated(3U);
	}
	page_bytes = (uint64_t) stride * h;
	flush_start = (uintptr_t) (fb + (uint64_t) y0 * stride);
	flush_len = (uint64_t) box_h * stride;

	menu_draw_dbg_step("p0-fill-bg");
	menu_fill_rect32(fb, stride, w, h, x0, y0, box_w, box_h, bg);
	menu_draw_dbg_step("p0-fill-header");
	menu_fill_rect32(fb, stride, w, h, x0 + 4U, y0 + 4U, box_w - 8U, 26U, accent);
	for (i = 0U; i < item_count; i++) {
		uart_puts_all("[mk] menu draw row fill idx=0x");
		uart_puthex64_all(i);
		uart_puts_all("\r\n");
		menu_fill_rect32(fb, stride, w, h, x0 + 12U, y0 + row_y[i], box_w - 24U, 48U,
				(menu_index == i) ? row_sel : row_unsel);
	}
	menu_draw_dbg_step("p0-text-title");
	sample_before = ((volatile uint32_t *) (fb + (uint64_t) (y0 + 10U) * stride))[x0 + 12U];
	uart_puts_all("[mk] menu text sample before=0x");
	uart_puthex64_all(sample_before);
	uart_puts_all("\r\n");
	delay_ms_calibrated(1U);
	menu_draw_text_5x7(fb, stride, w, h, x0 + 12U, y0 + 10U, 2U, fg, "FASTBOOT MENU");
	sample_after = ((volatile uint32_t *) (fb + (uint64_t) (y0 + 10U) * stride))[x0 + 12U];
	uart_puts_all("[mk] menu text sample after=0x");
	uart_puthex64_all(sample_after);
	uart_puts_all("\r\n");
	delay_ms_calibrated(1U);
	clean_dcache_range(flush_start, flush_len);
	menu_draw_dbg_step("p0-text-title-flush");
	for (i = 0U; i < item_count; i++) {
		uart_puts_all("[mk] menu draw row text idx=0x");
		uart_puthex64_all(i);
		uart_puts_all("\r\n");
		menu_draw_text_5x7(fb, stride, w, h, x0 + 24U, y0 + row_y[i] + 14U, 2U,
				  (menu_index == i) ? fg_sel : fg,
				  fastboot_menu_row_text(continue_available, i));
		clean_dcache_range(flush_start, flush_len);
	}
	menu_draw_dbg_step("p0-text-help");
	menu_draw_text_5x7(fb, stride, w, h, x0 + 12U, y0 + help_y, 2U, fg, help_text);
	clean_dcache_range(flush_start, flush_len);
	menu_draw_dbg_step("p0-text-help-flush");

	/* Temporarily skip countdown drawing while isolating menu render crash. */
	secs_len = menu_u32_to_dec(secs_buf, sizeof(secs_buf), secs_left);
	for (i = 0U; i < sizeof(countdown); i++) {
		countdown[i] = '\0';
	}
	(void) secs_len;
	(void) secs_buf;
	(void) countdown;
	menu_draw_dbg_step("p0-countdown-skip");

	menu_draw_dbg_step("p0-flush");
	clean_dcache_range(flush_start, flush_len);

	uart_puts_all("[mk] menu draw mirror-check size=0x");
	uart_puthex64_all((info != 0) ? info->size : 0U);
	uart_puts_all(" need=0x");
	uart_puthex64_all(page_bytes * 2U);
	uart_puts_all("\r\n");
	delay_ms_calibrated(3U);
	if (info != 0 && info->size >= (page_bytes * 2U)) {
		fb_page1 = fb + page_bytes;
		menu_draw_dbg_step("p1-fill-bg");
		menu_fill_rect32(fb_page1, stride, w, h, x0, y0, box_w, box_h, bg);
		menu_draw_dbg_step("p1-fill-header");
		menu_fill_rect32(fb_page1, stride, w, h, x0 + 4U, y0 + 4U, box_w - 8U, 26U, accent);
		for (i = 0U; i < item_count; i++) {
			menu_fill_rect32(fb_page1, stride, w, h, x0 + 12U, y0 + row_y[i], box_w - 24U, 48U,
					(menu_index == i) ? row_sel : row_unsel);
		}
		menu_draw_dbg_step("p1-text-title");
		menu_draw_text_5x7(fb_page1, stride, w, h, x0 + 12U, y0 + 10U, 2U, fg, "FASTBOOT MENU");
		for (i = 0U; i < item_count; i++) {
			menu_draw_text_5x7(fb_page1, stride, w, h, x0 + 24U, y0 + row_y[i] + 14U, 2U,
					  (menu_index == i) ? fg_sel : fg,
					  fastboot_menu_row_text(continue_available, i));
		}
		menu_draw_dbg_step("p1-text-help");
		menu_draw_text_5x7(fb_page1, stride, w, h, x0 + 12U, y0 + help_y, 2U, fg, help_text);
		menu_draw_dbg_step("p1-countdown-skip");
		flush_start = (uintptr_t) (fb_page1 + (uint64_t) y0 * stride);
		menu_draw_dbg_step("p1-flush");
		clean_dcache_range(flush_start, flush_len);
		uart_puts_all("[mk] menu draw mirrored page1\r\n");
		delay_ms_calibrated(3U);
	}
	uart_puts_all("[mk] menu draw px end\r\n");
	delay_ms_calibrated(3U);
}

/* ------------------------------------------------------------------ */
/* Logo / splash rendering                                             */
/* ------------------------------------------------------------------ */

static uint32_t bpp_from_format(const char *fmt)
{
	if (fmt == 0) {
		return 4;
	}
	if (str_eq(fmt, "r5g6b5")) {
		return 2;
	}
	return 4;
}

static uint8_t glyph_row_for_char(char c, uint32_t row)
{
	static const uint8_t glyph_M[7] = {0x11U, 0x1bU, 0x15U, 0x15U, 0x11U, 0x11U, 0x11U};
	static const uint8_t glyph_i[7] = {0x04U, 0x00U, 0x0cU, 0x04U, 0x04U, 0x04U, 0x0eU};
	static const uint8_t glyph_n[7] = {0x00U, 0x00U, 0x1aU, 0x15U, 0x11U, 0x11U, 0x11U};
	static const uint8_t glyph_K[7] = {0x11U, 0x12U, 0x14U, 0x18U, 0x14U, 0x12U, 0x11U};
	static const uint8_t glyph_e[7] = {0x00U, 0x00U, 0x0eU, 0x11U, 0x1fU, 0x10U, 0x0fU};
	static const uint8_t glyph_r[7] = {0x00U, 0x00U, 0x16U, 0x19U, 0x10U, 0x10U, 0x10U};
	static const uint8_t glyph_l[7] = {0x0cU, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U, 0x0eU};
	const uint8_t *glyph;

	if (row >= 7U) {
		return 0U;
	}

	switch (c) {
	case 'M':
		glyph = glyph_M;
		break;
	case 'i':
		glyph = glyph_i;
		break;
	case 'n':
		glyph = glyph_n;
		break;
	case 'K':
		glyph = glyph_K;
		break;
	case 'e':
		glyph = glyph_e;
		break;
	case 'r':
		glyph = glyph_r;
		break;
	case 'l':
		glyph = glyph_l;
		break;
	default:
		return 0U;
	}

	return glyph[row];
}

static void draw_label_32(volatile uint8_t *fb, uint32_t stride, uint32_t w, uint32_t h)
{
	static const char text[] = "MinKernel";
	const uint32_t glyph_w = 5U;
	const uint32_t glyph_h = 7U;
	const uint32_t scale = 4U;
	const uint32_t spacing = 4U;
	const uint32_t text_w =
		((sizeof(text) - 1U) * glyph_w * scale) +
		((sizeof(text) - 2U) * spacing);
	uint32_t base_x = 0U;
	uint32_t base_y = 56U;
	uint32_t i;
	uint32_t row;
	uint32_t col;
	uint32_t sy;
	uint32_t sx;

	if (fb == 0 || stride == 0U || w == 0U || h == 0U) {
		return;
	}

	if (w > text_w) {
		base_x = (w - text_w) / 2U;
	}
	if (base_y + (glyph_h * scale) >= h) {
		return;
	}

	for (i = 0; i < (sizeof(text) - 1U); i++) {
		uint32_t char_x = base_x + (i * ((glyph_w * scale) + spacing));
		for (row = 0; row < glyph_h; row++) {
			uint8_t bits = glyph_row_for_char(text[i], row);
			for (col = 0; col < glyph_w; col++) {
				if ((bits & (1U << (glyph_w - 1U - col))) == 0U) {
					continue;
				}
				for (sy = 0; sy < scale; sy++) {
					volatile uint32_t *line32 =
						(volatile uint32_t *) (fb + ((uint64_t) (base_y + (row * scale) + sy) * stride));
					for (sx = 0; sx < scale; sx++) {
						line32[char_x + (col * scale) + sx] = 0xffffffffU;
					}
				}
			}
		}
	}
}

static void render_logo_page_rgba(volatile uint8_t *fb, uint32_t stride, uint32_t w, uint32_t h)
{
	uint32_t logo_w = MK_STAGE0_PEACOCK_LOGO_WIDTH;
	uint32_t logo_h = MK_STAGE0_PEACOCK_LOGO_HEIGHT;
	uint32_t logo_x = 0U;
	uint32_t logo_y = 0U;
	uint32_t stride_px = stride / 4U;
	uint32_t x;
	uint32_t y;

	if (fb == 0 || stride == 0U || w == 0U || h == 0U) {
		return;
	}

	if (logo_w > w) {
		logo_w = w;
	}
	if (logo_h > h) {
		logo_h = h;
	}
	if (w > logo_w) {
		logo_x = (w - logo_w) / 2U;
	}
	if (h > logo_h) {
		logo_y = (h - logo_h) / 2U;
	}

	for (y = 0; y < h; y++) {
		volatile uint32_t *line32 =
			(volatile uint32_t *) (fb + ((uint64_t) y * stride));
		for (x = 0; x < stride_px; x++) {
			line32[x] = 0xff000000U;
		}
	}

	draw_label_32(fb, stride, w, h);

	for (y = 0; y < logo_h; y++) {
		volatile uint32_t *line32 =
			(volatile uint32_t *) (fb + ((uint64_t) (logo_y + y) * stride));
		const uint8_t *src =
			&g_peacock_logo_index[(uint64_t) y * (uint64_t) MK_STAGE0_PEACOCK_LOGO_WIDTH];
		for (x = 0; x < logo_w; x++) {
			line32[logo_x + x] = g_peacock_logo_palette[src[x]];
		}
	}
}

void try_direct_link_flip_and_disable_strip(const simplefb_info_t *info,
					    uint32_t fallback_width,
					    uint32_t fallback_height,
					    uint32_t fallback_align)
{
	uint32_t bpp;
	uint32_t w;
	uint32_t h;
	uint32_t stride;
	uint64_t page0_addr;
	uint32_t src_con;
	uint32_t src_con_2l;

	if (info == 0 || info->addr == 0U) {
		return;
	}

	bpp = bpp_from_format(info->format);
	w = info->width;
	h = info->height;
	stride = info->stride;
	if ((w == 0U || h == 0U) && fallback_width != 0U && fallback_height != 0U) {
		w = fallback_width;
		h = fallback_height;
		if (stride == 0U) {
			stride = align_up_u32(w, fallback_align) * 4U;
		}
	}
	if (bpp != 4U || w == 0U || h == 0U || stride == 0U) {
		return;
	}

	page0_addr = info->addr;
	snapshot_display_ovl_state_once();

	src_con = mmio_read32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_SRC_CON);
	src_con &= ~0x8U;
	mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_SRC_CON, src_con);
	mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_CON, 0U);
	mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_SRC_SIZE, 0U);
	mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_OFFSET, 0U);
	mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_ADDR, 0U);
	mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_PITCH, 0U);
	mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_CLEAR, 1U);

	src_con_2l = mmio_read32(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_SRC_CON);
	src_con_2l |= 0x1U;
	mmio_write32(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_SRC_CON, src_con_2l);
	mmio_write32(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_ADDR, (uint32_t) page0_addr);
	mmio_write32(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_OFFSET, 0U);
	mmio_write32(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_SRC_SIZE,
			 (h << 16) | (w & 0xfffU));
	mmio_write32(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_CLEAR, 1U);
	mmio_write32(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_TRIG, 1U);
	mmio_write32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_TRIG, 1U);

	uart_puts_all("[mk] relatch page0=0x");
	uart_puthex64_all(page0_addr);
	uart_puts_all(" ovl0.src=0x");
	uart_puthex64_all(mmio_read32(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_SRC_CON));
	uart_puts_all(" ovl0_2l.src=0x");
	uart_puthex64_all(mmio_read32(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_SRC_CON));
	uart_puts_all("\r\n");
}

/* ------------------------------------------------------------------ */
/* Fastboot menu input handling                                        */
/* ------------------------------------------------------------------ */

static uint8_t handle_fastboot_menu_input(uint32_t *menu_index, uint64_t *last_event_ticks,
					  uint64_t now_ticks, uint64_t freq,
					  uint8_t *menu_dirty, uint8_t continue_available)
{
	uint8_t up_edge = 0U;
	uint8_t down_edge = 0U;
	uint8_t select_edge = 0U;
	static uint8_t up_latched = 0U;
	static uint8_t down_latched = 0U;
	static uint8_t select_latched = 0U;
	uint8_t up_pressed;
	uint8_t down_pressed;
	uint8_t select_pressed;
	uint8_t action;
	uint64_t debounce_ticks = (freq != 0U) ? (freq / 20ULL) : 0ULL;
	uint32_t item_count = fastboot_menu_item_count(continue_available);

	if (menu_dirty != 0) {
		*menu_dirty = 0U;
	}
	if (menu_index == 0 || last_event_ticks == 0 || g_menu_buttons.has_any == 0U || item_count == 0U) {
		return 0U;
	}

	menu_buttons_poll_edges(&up_edge, &down_edge, &select_edge);
	(void) up_edge;
	(void) down_edge;
	(void) select_edge;

	up_pressed = g_menu_buttons.stable_up_pressed;
	down_pressed = g_menu_buttons.stable_down_pressed;
	select_pressed = g_menu_buttons.stable_power_pressed;

	if (up_pressed == 0U) {
		up_latched = 0U;
	}
	if (down_pressed == 0U) {
		down_latched = 0U;
	}
	if (select_pressed == 0U) {
		select_latched = 0U;
	}

	if ((up_pressed == 0U || up_latched != 0U) &&
	    (down_pressed == 0U || down_latched != 0U) &&
	    (select_pressed == 0U || select_latched != 0U)) {
		return 0U;
	}

	if (debounce_ticks != 0U && (now_ticks - *last_event_ticks) < debounce_ticks) {
		return 0U;
	}
	*last_event_ticks = now_ticks;

	if (up_pressed != 0U && up_latched == 0U) {
		up_latched = 1U;
		*menu_index = (*menu_index == 0U) ? (item_count - 1U) : (*menu_index - 1U);
		if (menu_dirty != 0) {
			*menu_dirty = 1U;
		}
		if (g_menu_buttons.vol_up_gpio == MK_STAGE0_GPIO_NONE &&
		    g_menu_buttons.vol_up_hwcode == 0xffffffffU) {
			uart_puts_all("[mk] menu input: homekey-as-up\r\n");
		} else {
			uart_puts_all("[mk] menu input: volume-up\r\n");
		}
		uart_puts_all("[mk] menu select=");
		uart_puts_all(fastboot_menu_label(continue_available, *menu_index));
		uart_puts_all("\r\n");
	}

	if (down_pressed != 0U && down_latched == 0U) {
		down_latched = 1U;
		*menu_index = (*menu_index + 1U) % item_count;
		if (menu_dirty != 0) {
			*menu_dirty = 1U;
		}
		uart_puts_all("[mk] menu select=");
		uart_puts_all(fastboot_menu_label(continue_available, *menu_index));
		uart_puts_all("\r\n");
	}

	if (select_pressed != 0U && select_latched == 0U) {
		select_latched = 1U;
		if (menu_dirty != 0) {
			*menu_dirty = 1U;
		}
		uart_puts_all("[mk] menu input: power\r\n");
		action = fastboot_menu_select_action(continue_available, *menu_index);
		if (action == MK_FASTBOOT_ACTION_CONTINUE) {
			uart_puts_all("[mk] menu action: continue boot\r\n");
		} else if (action == MK_FASTBOOT_ACTION_POWEROFF) {
			uart_puts_all("[mk] menu action: power off\r\n");
		} else {
			uart_puts_all("[mk] menu action: reboot recovery\r\n");
		}
		return action;
	}

	return 0U;
}

static uint32_t fastboot_timeout_secs_left(uint64_t start_ticks, uint64_t now_ticks,
					   uint64_t timeout_ticks, uint64_t freq)
{
	uint64_t elapsed;
	uint64_t remaining;

	if (timeout_ticks == 0U || freq == 0U) {
		return 0U;
	}
	if (now_ticks <= start_ticks) {
		return (uint32_t) ((timeout_ticks + freq - 1ULL) / freq);
	}
	elapsed = now_ticks - start_ticks;
	if (elapsed >= timeout_ticks) {
		return 0U;
	}
	remaining = timeout_ticks - elapsed;
	return (uint32_t) ((remaining + freq - 1ULL) / freq);
}

/* ------------------------------------------------------------------ */
/* enter_fastboot_fallback                                             */
/* ------------------------------------------------------------------ */

uint8_t enter_fastboot_fallback(const simplefb_info_t *info,
				uint32_t fallback_width,
				uint32_t fallback_height,
				uint32_t fallback_align,
				uint8_t continue_available)
{
	uint32_t heartbeat = 0U;
	uint32_t menu_index = 0U;
	uint32_t secs_left = 0U;
	uint32_t spin;
	uint64_t now_ticks;
	uint64_t menu_last_event_ticks = 0U;
	uint64_t next_ui_ticks = 0U;
	uint64_t ui_interval_ticks;
	uint64_t start_ticks;
	uint64_t freq;
	uint64_t timeout_ticks;
	uint8_t menu_dirty = 0U;
	uint8_t draw_pending = 0U;
	uint8_t fb_action = MK_FASTBOOT_ACTION_NONE;

	uart_puts_all("[mk] fastboot fallback: holding\r\n");
	if (g_menu_buttons.has_any != 0U) {
		uart_puts_all("[mk] menu controls: vol-down=next vol-up=prev power=select\r\n");
		uart_puts_all("[mk] menu select=");
		uart_puts_all(fastboot_menu_label(continue_available, menu_index));
		uart_puts_all("\r\n");
		menu_dirty = 1U;
		draw_pending = 1U;
	}
	freq = read_cntfrq_el0();
	start_ticks = read_cntpct_el0();
	timeout_ticks = 0ULL;
	uart_puts_all("[mk] fastboot fallback: auto-reboot disabled\r\n");
	ui_interval_ticks = (freq != 0U) ? (freq / 100ULL) : 1ULL;
	if (ui_interval_ticks == 0U) {
		ui_interval_ticks = 1ULL;
	}
	next_ui_ticks = start_ticks;
	if (MK_DEVICE_HAS_FASTBOOT_USB != 0) {
		if (mk_stage0_mtk_usb_fastboot_init() == 0) {
			uart_puts_all("[mk] fastboot fallback: usb ep0 online\r\n");
				for (;;) {
					now_ticks = read_cntpct_el0();
					mk_stage0_mtk_usb_fastboot_poll();
					fb_action = mk_stage0_mtk_usb_fastboot_take_action();
					if (fb_action != MK_FASTBOOT_ACTION_NONE) {
						uart_puts_all("[mk] fastboot action: ");
						if (fb_action == MK_FASTBOOT_ACTION_REBOOT_RECOVERY) {
							uart_puts_all("reboot-recovery\r\n");
						} else if (fb_action == MK_FASTBOOT_ACTION_REBOOT_BOOTLOADER) {
							uart_puts_all("reboot-bootloader\r\n");
						} else if (fb_action == MK_FASTBOOT_ACTION_BOOT_STAGED_KERNEL) {
							uart_puts_all("boot-staged-kernel\r\n");
							if (continue_available != 0U) {
								return MK_FASTBOOT_ACTION_BOOT_STAGED_KERNEL;
							}
							uart_puts_all("[mk] boot-staged-kernel ignored: peacock boot unavailable\r\n");
							continue;
						} else if (fb_action == MK_FASTBOOT_ACTION_CONTINUE) {
							uart_puts_all("continue\r\n");
							if (continue_available != 0U) {
								return MK_FASTBOOT_ACTION_CONTINUE;
							}
							uart_puts_all("[mk] continue ignored: peacock boot unavailable\r\n");
							continue;
						} else {
							uart_puts_all("reboot\r\n");
						}
						delay_ms_calibrated(30U);
						if (fb_action == MK_FASTBOOT_ACTION_REBOOT_RECOVERY) {
							arm_recovery_wdt();
						}
						arm_normal_wdt();
					}
					if (timeout_ticks != 0U && (now_ticks - start_ticks) >= timeout_ticks) {
						uart_puts_all("[mk] fastboot fallback: timeout, rebooting recovery\r\n");
						arm_recovery_wdt();
					delay_ms_calibrated(50U);
					trigger_recovery_wdt_reset();
				}
				if (mk_stage0_mtk_usb_fastboot_downloading() != 0U) {
					for (spin = 0; spin < 64U; spin++) {
						__asm__ volatile("");
					}
					pet_wdt();
					continue;
				}
				if (now_ticks >= next_ui_ticks) {
					fb_action = handle_fastboot_menu_input(&menu_index, &menu_last_event_ticks,
									      now_ticks, freq, &menu_dirty,
									      continue_available);
					if (fb_action == MK_FASTBOOT_ACTION_REBOOT_RECOVERY) {
						arm_recovery_wdt();
						delay_ms_calibrated(50U);
						trigger_recovery_wdt_reset();
					}
					if (fb_action == MK_FASTBOOT_ACTION_CONTINUE) {
						return MK_FASTBOOT_ACTION_CONTINUE;
					}
					if (fb_action == MK_FASTBOOT_ACTION_POWEROFF) {
						return MK_FASTBOOT_ACTION_POWEROFF;
					}
					if (menu_dirty != 0U && g_menu_buttons.has_any != 0U) {
						draw_pending = 1U;
					}
					if (draw_pending != 0U) {
						secs_left = fastboot_timeout_secs_left(start_ticks, now_ticks,
									      timeout_ticks, freq);
						uart_puts_all("[mk] menu draw call\r\n");
						render_fastboot_menu_overlay(info, fallback_width, fallback_height,
									 fallback_align, menu_index, secs_left,
									 continue_available);
						uart_puts_all("[mk] menu draw done\r\n");
						draw_pending = 0U;
						menu_dirty = 0U;
					}
					next_ui_ticks = now_ticks + ui_interval_ticks;
				}
				for (spin = 0; spin < 2048U; spin++) {
					__asm__ volatile("");
				}
				pet_wdt();
			}
		}
		uart_puts_all("[mk] fastboot fallback: usb init failed\r\n");
	}

	for (;;) {
		now_ticks = read_cntpct_el0();
		fb_action = mk_stage0_mtk_usb_fastboot_take_action();
		if (fb_action != MK_FASTBOOT_ACTION_NONE) {
			uart_puts_all("[mk] fastboot action: ");
			if (fb_action == MK_FASTBOOT_ACTION_REBOOT_RECOVERY) {
				uart_puts_all("reboot-recovery\r\n");
			} else if (fb_action == MK_FASTBOOT_ACTION_BOOT_STAGED_KERNEL) {
				uart_puts_all("boot-staged-kernel\r\n");
				if (continue_available != 0U) {
					return MK_FASTBOOT_ACTION_BOOT_STAGED_KERNEL;
				}
				uart_puts_all("[mk] boot-staged-kernel ignored: peacock boot unavailable\r\n");
				continue;
			} else if (fb_action == MK_FASTBOOT_ACTION_CONTINUE) {
				uart_puts_all("continue\r\n");
				if (continue_available != 0U) {
					return MK_FASTBOOT_ACTION_CONTINUE;
				}
				uart_puts_all("[mk] continue ignored: peacock boot unavailable\r\n");
				continue;
			} else {
				uart_puts_all("reboot\r\n");
			}
			delay_ms_calibrated(30U);
			if (fb_action == MK_FASTBOOT_ACTION_REBOOT_RECOVERY) {
				arm_recovery_wdt();
			}
			arm_normal_wdt();
		}
		if (timeout_ticks != 0U && (now_ticks - start_ticks) >= timeout_ticks) {
			uart_puts_all("[mk] fastboot fallback: timeout, rebooting recovery\r\n");
			arm_recovery_wdt();
			delay_ms_calibrated(50U);
			trigger_recovery_wdt_reset();
		}
		if (now_ticks >= next_ui_ticks) {
			fb_action = handle_fastboot_menu_input(&menu_index, &menu_last_event_ticks,
						       now_ticks, freq, &menu_dirty,
						       continue_available);
			if (fb_action == MK_FASTBOOT_ACTION_REBOOT_RECOVERY) {
				arm_recovery_wdt();
				delay_ms_calibrated(50U);
				trigger_recovery_wdt_reset();
			}
			if (fb_action == MK_FASTBOOT_ACTION_CONTINUE) {
				return MK_FASTBOOT_ACTION_CONTINUE;
			}
			if (fb_action == MK_FASTBOOT_ACTION_POWEROFF) {
				return MK_FASTBOOT_ACTION_POWEROFF;
			}
			if (menu_dirty != 0U && g_menu_buttons.has_any != 0U) {
				draw_pending = 1U;
			}
			if (draw_pending != 0U) {
				secs_left = fastboot_timeout_secs_left(start_ticks, now_ticks, timeout_ticks, freq);
				uart_puts_all("[mk] menu draw call\r\n");
				render_fastboot_menu_overlay(info, fallback_width, fallback_height,
							 fallback_align, menu_index, secs_left,
							 continue_available);
				uart_puts_all("[mk] menu draw done\r\n");
				draw_pending = 0U;
				menu_dirty = 0U;
			}
			next_ui_ticks = now_ticks + ui_interval_ticks;
		}
		for (spin = 0; spin < 400000U; spin++) {
			if ((spin & 0x3fffU) == 0U) {
				pet_wdt();
			}
			__asm__ volatile("");
		}
		if ((heartbeat++ & 0x0fU) == 0U) {
			uart_puts_all("[mk] fastboot fallback: alive\r\n");
		}
	}
}

/* ------------------------------------------------------------------ */
/* Offline charging mode                                               */
/* ------------------------------------------------------------------ */

#define MK_CHARGING_EXIT_BOOT  1U
#define MK_CHARGING_EXIT_MENU  2U
#define MK_CHARGING_EXIT_OFF   3U

static void render_charging_screen(volatile uint8_t *fb, uint32_t stride,
				   uint32_t w, uint32_t h)
{
	uint32_t cx = w / 2U;
	uint32_t cy = h / 2U;

	/* Battery outline: 80x140 centered */
	uint32_t bx = cx - 40U;
	uint32_t by = cy - 70U;
	uint32_t bw = 80U;
	uint32_t bh = 140U;
	uint32_t cap_w = 30U;
	uint32_t cap_h = 10U;

	uint32_t bg = 0xff000000U;
	uint32_t outline = 0xff808080U;
	uint32_t fill_color = 0xff2e8b57U;
	uint32_t text_color = 0xffccccccU;
	uint32_t bar_h;

	/* Clear screen black */
	menu_fill_rect32(fb, stride, w, h, 0U, 0U, w, h, bg);

	/* Battery cap (top nub) */
	menu_fill_rect32(fb, stride, w, h,
			 cx - cap_w / 2U, by - cap_h, cap_w, cap_h, outline);

	/* Battery outline */
	menu_fill_rect32(fb, stride, w, h, bx, by, bw, 4U, outline);         /* top */
	menu_fill_rect32(fb, stride, w, h, bx, by + bh - 4U, bw, 4U, outline); /* bottom */
	menu_fill_rect32(fb, stride, w, h, bx, by, 4U, bh, outline);         /* left */
	menu_fill_rect32(fb, stride, w, h, bx + bw - 4U, by, 4U, bh, outline); /* right */

	/* Fill bar — animate 40% as a static indication */
	bar_h = (bh - 12U) * 40U / 100U;
	menu_fill_rect32(fb, stride, w, h,
			 bx + 6U, by + bh - 6U - bar_h,
			 bw - 12U, bar_h, fill_color);

	/* "CHARGING" text below battery */
	menu_draw_text_5x7(fb, stride, w, h, cx - 48U, by + bh + 20U, 2U,
			   text_color, "CHARGING");

	/* "PWR BOOT  DOWN MENU" hint */
	menu_draw_text_5x7(fb, stride, w, h, cx - 108U, by + bh + 52U, 2U,
			   text_color, "PWR BOOT  DOWN MENU");

	clean_dcache_range((uintptr_t) fb, (uint64_t) stride * (uint64_t) h);
}

uint8_t enter_offline_charging(const simplefb_info_t *info,
			       uint32_t fallback_width,
			       uint32_t fallback_height,
			       uint32_t fallback_align)
{
	volatile uint8_t *fb;
	uint32_t w;
	uint32_t h;
	uint32_t stride;
	uint64_t freq;
	uint64_t now;
	uint64_t next_poll;
	uint64_t poll_interval;
	uint32_t spin;
	uint32_t heartbeat = 0U;
	uint8_t drawn = 0U;
	uint8_t pwr_latched = 1U;
	uint8_t vol_latched = 1U;

	uart_puts_all("[mk] offline charging: enter\r\n");

	freq = read_cntfrq_el0();
	poll_interval = (freq != 0U) ? (freq / 20ULL) : 1ULL;
	if (poll_interval == 0U) {
		poll_interval = 1ULL;
	}
	next_poll = read_cntpct_el0();

	if (menu_resolve_fb(info, fallback_width, fallback_height,
			    fallback_align, &fb, &w, &h, &stride) != 0U) {
		render_charging_screen(fb, stride, w, h);
		drawn = 1U;
		uart_puts_all("[mk] offline charging: screen drawn\r\n");
	}

	for (;;) {
		now = read_cntpct_el0();

		if (now >= next_poll) {
			uint8_t pwr_now = mk_pmic_power_pressed();
			uint8_t vol_now = vol_down_held();
			uint8_t chr_now = mk_pmic_charger_connected();

			/* Power key: boot on release */
			if (pwr_now != 0U) {
				pwr_latched = 1U;
			} else if (pwr_latched != 0U) {
				pwr_latched = 0U;
				uart_puts_all("[mk] offline charging: power key -> boot\r\n");
				return MK_CHARGING_EXIT_BOOT;
			}

			/* Volume down: menu on release */
			if (vol_now != 0U) {
				vol_latched = 1U;
			} else if (vol_latched != 0U) {
				vol_latched = 0U;
				uart_puts_all("[mk] offline charging: vol-down -> menu\r\n");
				return MK_CHARGING_EXIT_MENU;
			}

			/* Charger removed: power off */
			if (chr_now == 0U) {
				uart_puts_all("[mk] offline charging: charger removed -> power off\r\n");
				return MK_CHARGING_EXIT_OFF;
			}

			next_poll = now + poll_interval;
		}

		for (spin = 0; spin < 4096U; spin++) {
			__asm__ volatile("");
		}
		pet_wdt();

		if ((heartbeat++ & 0xffU) == 0U && drawn != 0U) {
			uart_puts_all("[mk] offline charging: alive\r\n");
		}
	}
}

/* ------------------------------------------------------------------ */
/* Boot status display                                                 */
/* ------------------------------------------------------------------ */

static struct {
	volatile uint8_t *fb;
	uint32_t w;
	uint32_t h;
	uint32_t stride;
	uint8_t  valid;
} g_boot_fb;

void mk_ui_set_boot_fb(const simplefb_info_t *info,
			uint32_t fallback_width, uint32_t fallback_height,
			uint32_t fallback_align)
{
	volatile uint8_t *fb;
	uint32_t w;
	uint32_t h;
	uint32_t stride;

	if (menu_resolve_fb(info, fallback_width, fallback_height,
			    fallback_align, &fb, &w, &h, &stride) == 0U) {
		g_boot_fb.valid = 0U;
		return;
	}
	g_boot_fb.fb = fb;
	g_boot_fb.w = w;
	g_boot_fb.h = h;
	g_boot_fb.stride = stride;
	g_boot_fb.valid = 1U;
}

void mk_ui_boot_status(const char *msg)
{
	uint32_t len;
	uint32_t text_w;
	uint32_t x;
	uint32_t y;

	if (g_boot_fb.valid == 0U) {
		return;
	}

	/* Clear screen black */
	menu_fill_rect32(g_boot_fb.fb, g_boot_fb.stride,
			 g_boot_fb.w, g_boot_fb.h,
			 0U, 0U, g_boot_fb.w, g_boot_fb.h, 0xff000000U);

	/* Center text at scale 2 (each char is 5*2=10px wide + 2px gap = 12px) */
	len = mk_strlen(msg);
	text_w = len * 12U;
	x = (g_boot_fb.w > text_w) ? (g_boot_fb.w - text_w) / 2U : 0U;
	y = g_boot_fb.h / 2U - 7U;

	menu_draw_text_5x7(g_boot_fb.fb, g_boot_fb.stride,
			   g_boot_fb.w, g_boot_fb.h,
			   x, y, 2U, 0xffffffffU, msg);

	clean_dcache_range((uintptr_t) g_boot_fb.fb,
			   (uint64_t) g_boot_fb.stride * (uint64_t) g_boot_fb.h);
}

/* ------------------------------------------------------------------ */
/* draw_pattern                                                        */
/* ------------------------------------------------------------------ */

void draw_pattern(const simplefb_info_t *info,
		  uint32_t fallback_width,
		  uint32_t fallback_height,
		  uint32_t fallback_align)
{
	volatile uint8_t *fb;
	volatile uint32_t *fb32;
	uint64_t frame_bytes;
	uint64_t pages_to_paint;
	uint64_t page;
	uint32_t bpp;
	uint32_t x;
	uint32_t y;
	uint32_t w;
	uint32_t h;
	uint32_t stride;

	if (info == 0 || info->addr == 0) {
		return;
	}

	fb = (volatile uint8_t *) (uintptr_t) info->addr;
	fb32 = (volatile uint32_t *) (uintptr_t) info->addr;
	bpp = bpp_from_format(info->format);
	w = info->width;
	h = info->height;
	stride = info->stride;
	if ((w == 0U || h == 0U) && fallback_width != 0U && fallback_height != 0U) {
		w = fallback_width;
		h = fallback_height;
		if (stride == 0U) {
			stride = align_up_u32(w, fallback_align) * 4U;
		}
	}
	if (w == 0 || h == 0) {
		uint64_t i;
		uint64_t words = info->size / 4U;
		if (words == 0U) {
			return;
		}
		for (i = 0; i < words; i++) {
			fb32[i] = 0xffffffffU;
		}
		clean_dcache_range((uintptr_t) fb32, words * 4U);
		return;
	}
	if (stride == 0) {
		stride = w * bpp;
	}
	frame_bytes = (uint64_t) stride * (uint64_t) h;
	pages_to_paint = 1U;
	if (frame_bytes != 0U && info->size >= frame_bytes) {
		pages_to_paint = info->size / frame_bytes;
		if (pages_to_paint == 0U) {
			pages_to_paint = 1U;
		}
		if (pages_to_paint > 6U) {
			pages_to_paint = 6U;
		}
	}
	if (bpp == 4U && fallback_width != 0U && fallback_height != 0U &&
	    w == fallback_width && h == fallback_height) {
		render_logo_page_rgba(fb, stride, w, h);
		clean_dcache_range((uintptr_t) fb, frame_bytes);
		return;
	}

	for (page = 0; page < pages_to_paint; page++) {
		volatile uint8_t *page_fb = fb + (page * frame_bytes);
		for (y = 0; y < h; y++) {
			volatile uint8_t *line = page_fb + (uint64_t) y * (uint64_t) stride;
			for (x = 0; x < w; x++) {
				if (bpp == 2) {
					uint16_t c = (x < 8 || y < 8 || x > w - 9 || y > h - 9) ? 0xffffU : 0x07e0U;
					volatile uint16_t *p16 = (volatile uint16_t *) (line + x * 2U);
					*p16 = c;
				} else {
					volatile uint8_t *p32 = line + x * 4U;
					uint8_t r = (x < 8 || y < 8 || x > w - 9 || y > h - 9) ? 0xffU : (uint8_t) (x & 0xffU);
					uint8_t g = (uint8_t) (y & 0xffU);
					uint8_t b = 0x40U;
					/* B,G,R,A write works for common x8r8g8b8/a8r8g8b8 paths. */
					p32[0] = b;
					p32[1] = g;
					p32[2] = r;
					p32[3] = 0xffU;
				}
			}
		}
	}

	clean_dcache_range((uintptr_t) fb, frame_bytes * pages_to_paint);
}
