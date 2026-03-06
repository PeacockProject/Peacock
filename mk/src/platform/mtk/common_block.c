#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/fs.h>
#include <sys/ioctl.h>
#endif
#include "common_block.h"

static int try_open_block(const char *soc_name, const char *path)
{
	int fd;
	if (path == NULL || path[0] == '\0') {
		return -1;
	}
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "mk_hal[%s]: open failed for %s: %s\n", soc_name, path, strerror(errno));
	}
	return fd;
}

mk_status_t mtk_block_open(mtk_block_ctx_t *ctx,
			   const char *soc_name,
			   const mk_device_profile_t *profile,
			   const char *preferred_path,
			   mtk_block_open_hook_t open_hook)
{
	size_t i;

	if (ctx == NULL || soc_name == NULL || profile == NULL) {
		return MK_ERR;
	}

	memset(ctx, 0, sizeof(*ctx));
	ctx->soc_name = soc_name;
	ctx->profile = profile;
	ctx->block_path = (preferred_path != NULL && preferred_path[0] != '\0') ? preferred_path : "/dev/block/by-name/userdata";
	ctx->block_fd = -1;
	ctx->block_size = 512U;

	if (open_hook != NULL) {
		mk_status_t rc = open_hook(ctx);
		if (rc != MK_OK) {
			return rc;
		}
	}

	if (preferred_path != NULL && preferred_path[0] != '\0') {
		ctx->block_fd = try_open_block(ctx->soc_name, preferred_path);
		if (ctx->block_fd >= 0) {
			ctx->block_path = preferred_path;
		}
	}

	if (ctx->block_fd < 0 && profile->block_probe_paths != NULL) {
		for (i = 0; i < profile->block_probe_path_count; i++) {
			const char *path = profile->block_probe_paths[i];
			if (path == NULL || path[0] == '\0') {
				continue;
			}
			ctx->block_fd = try_open_block(ctx->soc_name, path);
			if (ctx->block_fd >= 0) {
				ctx->block_path = path;
				break;
			}
		}
	}

	if (ctx->block_fd < 0 && strcmp(ctx->block_path, "/dev/mmcblk0") != 0) {
		ctx->block_fd = try_open_block(ctx->soc_name, "/dev/mmcblk0");
		if (ctx->block_fd >= 0) {
			ctx->block_path = "/dev/mmcblk0";
		}
	}

	if (ctx->block_fd < 0) {
		return MK_ERR_NOT_FOUND;
	}

#ifdef __linux__
	{
		int sz = 0;
		if (ioctl(ctx->block_fd, BLKSSZGET, &sz) == 0 && sz > 0) {
			ctx->block_size = (uint32_t) sz;
		}
	}
#endif

	printf("mk_hal[%s]: init device=%s block=%s block_size=%u\n",
	       ctx->soc_name,
	       ctx->profile->name,
	       ctx->block_path,
	       ctx->block_size);
	return MK_OK;
}

mk_status_t mtk_block_read(const mtk_block_ctx_t *ctx, uint64_t lba, uint32_t count, void *out_buf, size_t out_size)
{
	ssize_t got;
	size_t need;
	off_t off;

	if (ctx == NULL || ctx->block_fd < 0 || out_buf == NULL || count == 0U) {
		return MK_ERR;
	}

	need = (size_t) count * (size_t) ctx->block_size;
	if (need != out_size) {
		return MK_ERR;
	}

	off = (off_t) (lba * (uint64_t) ctx->block_size);
	if (lseek(ctx->block_fd, off, SEEK_SET) < 0) {
		fprintf(stderr,
		        "mk_hal[%s]: seek failed lba=%llu err=%s\n",
		        ctx->soc_name,
		        (unsigned long long) lba,
		        strerror(errno));
		return MK_ERR;
	}

	got = read(ctx->block_fd, out_buf, need);
	if (got < 0 || (size_t) got != need) {
		fprintf(stderr,
		        "mk_hal[%s]: read failed lba=%llu count=%u got=%zd need=%zu err=%s\n",
		        ctx->soc_name,
		        (unsigned long long) lba,
		        count,
		        got,
		        need,
		        strerror(errno));
		return MK_ERR;
	}

	return MK_OK;
}

uint32_t mtk_block_size(const mtk_block_ctx_t *ctx)
{
	if (ctx == NULL || ctx->block_size == 0U) {
		return 512U;
	}
	return ctx->block_size;
}
