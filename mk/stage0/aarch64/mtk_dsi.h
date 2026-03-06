#ifndef MK_STAGE0_MTK_DSI_H
#define MK_STAGE0_MTK_DSI_H

#include <stdint.h>
#include "mtk_panel.h"

int mk_stage0_mtk_dsi_host_init(const mk_stage0_panel_t *panel);
int mk_stage0_mtk_dsi_write(uint8_t cmd, uint8_t count, const uint8_t *data);
int mk_stage0_mtk_dsi_dcs_write0(uint8_t cmd);
int mk_stage0_mtk_dsi_dcs_write1(uint8_t cmd, uint8_t value);
void mk_stage0_mtk_dsi_set_mode(mk_stage0_dsi_mode_t mode);

#endif
