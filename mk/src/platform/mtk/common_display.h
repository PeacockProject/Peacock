#ifndef MK_PLATFORM_MTK_COMMON_DISPLAY_H
#define MK_PLATFORM_MTK_COMMON_DISPLAY_H

#include <stddef.h>
#include "mk/common.h"
#include "mk/hal.h"

typedef struct {
	const char *soc_name;
	const mk_device_profile_t *profile;
	const char *selected_panel;
} mtk_display_ctx_t;

mk_status_t mtk_display_init(mtk_display_ctx_t *ctx, const char *soc_name, const mk_device_profile_t *profile);
const char *mtk_display_selected_panel(const mtk_display_ctx_t *ctx);

#endif
