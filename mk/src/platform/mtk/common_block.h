#ifndef MK_PLATFORM_MTK_COMMON_BLOCK_H
#define MK_PLATFORM_MTK_COMMON_BLOCK_H

#include <stddef.h>
#include <stdint.h>
#include "mk/common.h"
#include "mk/hal.h"

typedef struct {
	const char *soc_name;
	const mk_device_profile_t *profile;
	const char *block_path;
	int block_fd;
	uint32_t block_size;
} mtk_block_ctx_t;

typedef mk_status_t (*mtk_block_open_hook_t)(mtk_block_ctx_t *ctx);

mk_status_t mtk_block_open(mtk_block_ctx_t *ctx,
			   const char *soc_name,
			   const mk_device_profile_t *profile,
			   const char *preferred_path,
			   mtk_block_open_hook_t open_hook);
mk_status_t mtk_block_read(const mtk_block_ctx_t *ctx, uint64_t lba, uint32_t count, void *out_buf, size_t out_size);
uint32_t mtk_block_size(const mtk_block_ctx_t *ctx);

#endif
