#ifndef MK_STAGE0_MTK_PANEL_H
#define MK_STAGE0_MTK_PANEL_H

#include <stdint.h>

#define MK_STAGE0_GPIO_NONE 0xffffffffU
#define MK_STAGE0_I2C_NONE 0xffffffffffffffffULL
#define MK_STAGE0_LCD_BIAS_SKIP_REG 0xffffffffU
#define MK_STAGE0_MIPITX_LANE_COUNT 6U

typedef enum {
	MK_STAGE0_DSI_MODE_CMD = 0,
	MK_STAGE0_DSI_MODE_SYNC_PULSE_VDO = 1,
} mk_stage0_dsi_mode_t;

typedef enum {
	MK_STAGE0_PANEL_ID_NONE = 0,
	MK_STAGE0_PANEL_ID_NT35695B_FHD_DSI_CMD_AUO_RT5081 = 1,
	MK_STAGE0_PANEL_ID_ILT7807S_HLT_JELLY_HDP_DSI_VDO = 2,
} mk_stage0_panel_id_t;

typedef enum {
	MK_STAGE0_PANEL_BACKEND_NONE = 0,
	MK_STAGE0_PANEL_BACKEND_NT35695B_FHD_CMD_AUO_RT5081 = 1,
	MK_STAGE0_PANEL_BACKEND_ILT7807S_HLT_JELLY_HDP_VDO = 2,
} mk_stage0_panel_backend_t;

typedef struct mk_stage0_panel {
	mk_stage0_panel_id_t id;
	mk_stage0_panel_backend_t backend;
	uint32_t fb_width;
	uint32_t fb_height;
	uint32_t fb_align;
	uint32_t dsi_lanes;
	uint32_t dsi_packet_size;
	uint32_t dsi_pll_clock_cmd;
	uint32_t dsi_pll_clock_vdo;
	uint32_t dsi_vsync_active;
	uint32_t dsi_vback_porch;
	uint32_t dsi_vfront_porch;
	uint32_t dsi_hsync_active;
	uint32_t dsi_hback_porch;
	uint32_t dsi_hfront_porch;
	uint32_t lane_swap_enable;
	uint32_t lane_swap[MK_STAGE0_MIPITX_LANE_COUNT];
	uint32_t reset_delay0_ms;
	uint32_t reset_delay1_ms;
	uint32_t reset_delay2_ms;
	uint32_t reset_delay3_ms;
	uint32_t reset_gpio;
	uint32_t bias_enp_gpio;
	uint32_t bias_enn_gpio;
	uint32_t vddio_gpio;
	uint64_t runtime_fb_addr;
	uint64_t runtime_fb_size;
	uint32_t runtime_fb_stride;
	uint64_t lcd_bias_i2c_base;
	uint32_t lcd_bias_i2c_addr;
	uint32_t lcd_bias_voltage_reg;
	uint32_t lcd_bias_ctrl3_reg;
	mk_stage0_dsi_mode_t dsi_mode;
} mk_stage0_panel_t;

const mk_stage0_panel_t *mk_stage0_panel_find(const char *boot_name);
const mk_stage0_panel_t *mk_stage0_panel_resolve(const char *boot_name,
						 const char *target_name);
const char *mk_stage0_panel_name(const mk_stage0_panel_t *panel);
const char *mk_stage0_panel_backend_name(const mk_stage0_panel_t *panel);

#endif
