#ifndef MK_STAGE0_MTK_DISPLAY_H
#define MK_STAGE0_MTK_DISPLAY_H

#include <stdint.h>
#include "mtk_panel.h"

typedef enum {
	MK_STAGE0_DISPLAY_NOT_NEEDED = 0,
	MK_STAGE0_DISPLAY_READY = 1,
	MK_STAGE0_DISPLAY_PENDING = 2,
	MK_STAGE0_DISPLAY_UNSUPPORTED = 3,
	MK_STAGE0_DISPLAY_BAD_STATE = 4,
} mk_stage0_display_status_t;

typedef enum {
	MK_STAGE0_DISPLAY_FAIL_NONE = 0,
	MK_STAGE0_DISPLAY_FAIL_HOST_INIT = 1,
	MK_STAGE0_DISPLAY_FAIL_INIT_TABLE = 2,
	MK_STAGE0_DISPLAY_FAIL_POST_BRIGHTNESS = 3,
	MK_STAGE0_DISPLAY_FAIL_BIAS_I2C = 4,
} mk_stage0_display_fail_stage_t;

typedef struct mk_stage0_display_ctx {
	const char *runtime_lcm_name;
	uint64_t videolfb_found;
	uint64_t videolfb_inited;
} mk_stage0_display_ctx_t;

mk_stage0_display_status_t mk_stage0_display_prepare(const mk_stage0_panel_t *panel,
						     const mk_stage0_display_ctx_t *ctx);
const char *mk_stage0_display_status_string(mk_stage0_display_status_t status);
mk_stage0_display_fail_stage_t mk_stage0_display_last_fail_stage(void);
uint32_t mk_stage0_display_last_fail_index(void);
uint32_t mk_stage0_display_last_fail_cmd(void);

#endif
