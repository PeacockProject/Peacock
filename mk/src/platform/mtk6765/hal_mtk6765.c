#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/reboot.h>
#include <unistd.h>
#include "mk/hal_driver.h"
#include "../mtk/common_block.h"
#include "../mtk/common_display.h"

static mtk_block_ctx_t g_ctx;
static mtk_display_ctx_t g_display;
static const mk_device_profile_t *g_profile;

#define PHX_RECORD_SIZE 16U
#define PHX_FLAG_BASE 8U
#define PHX_FLAG_SLOTS 3U
#define PHX_COUNT_OFF 12U

static int path_exists(const char *path)
{
	return path != NULL && path[0] != '\0' && access(path, F_OK) == 0;
}

static int write_proc_command(const char *action, const char *value)
{
	char cmd[256];
	int fd;
	int len;
	ssize_t wrote;

	if (!path_exists("/proc/phoenix") || action == NULL || value == NULL) {
		return -1;
	}

	len = snprintf(cmd, sizeof(cmd), "%s@%s", action, value);
	if (len <= 0 || (size_t) len >= sizeof(cmd)) {
		return -1;
	}

	fd = open("/proc/phoenix", O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "mk_hal[mt6765]: open /proc/phoenix failed: %s\n", strerror(errno));
		return -1;
	}

	wrote = write(fd, cmd, (size_t) len);
	close(fd);
	if (wrote != len) {
		fprintf(stderr, "mk_hal[mt6765]: write /proc/phoenix failed: %s\n", strerror(errno));
		return -1;
	}

	printf("mk_hal[mt6765]: phoenix cmd %s\n", cmd);
	return 0;
}

static const char *resolve_phoenix_partition_path(void)
{
	static char path_buf[64];
	const char *name = NULL;

	if (g_profile == NULL) {
		return NULL;
	}

	if (g_profile->phoenix_primary_partition != NULL) {
		snprintf(path_buf, sizeof(path_buf), "/dev/block/by-name/%s",
			 g_profile->phoenix_primary_partition);
		if (path_exists(path_buf)) {
			return path_buf;
		}
	}

	if (g_profile->phoenix_fallback_partition != NULL) {
		snprintf(path_buf, sizeof(path_buf), "/dev/block/by-name/%s",
			 g_profile->phoenix_fallback_partition);
		if (path_exists(path_buf)) {
			return path_buf;
		}
	}

	name = g_profile->phoenix_primary_partition != NULL
		     ? g_profile->phoenix_primary_partition
		     : g_profile->phoenix_fallback_partition;
	if (name == NULL) {
		return NULL;
	}
	snprintf(path_buf, sizeof(path_buf), "/dev/block/by-name/%s", name);
	return path_buf;
}

static uint32_t read_le32(const unsigned char *p)
{
	return (uint32_t) p[0] |
	       ((uint32_t) p[1] << 8) |
	       ((uint32_t) p[2] << 16) |
	       ((uint32_t) p[3] << 24);
}

static void write_le32(unsigned char *p, uint32_t v)
{
	p[0] = (unsigned char) (v & 0xffU);
	p[1] = (unsigned char) ((v >> 8) & 0xffU);
	p[2] = (unsigned char) ((v >> 16) & 0xffU);
	p[3] = (unsigned char) ((v >> 24) & 0xffU);
}

static int write_phoenix_recovery_flag(void)
{
	const char *path;
	unsigned char rec[PHX_RECORD_SIZE];
	const char *magic;
	uint64_t off;
	uint32_t count;
	size_t magic_len;
	int fd;
	int is_ufs;
	ssize_t n;

	if (g_profile == NULL) {
		return -1;
	}

	path = resolve_phoenix_partition_path();
	magic = g_profile->phoenix_record_magic;
	if (path == NULL || magic == NULL || magic[0] == '\0') {
		fprintf(stderr, "mk_hal[mt6765]: phoenix reserve metadata missing\n");
		return -1;
	}

	is_ufs = path_exists("/proc/devinfo/ufs");
	off = is_ufs ? g_profile->phoenix_ufs_offset : g_profile->phoenix_emmc_offset;
	if (off == 0) {
		fprintf(stderr, "mk_hal[mt6765]: phoenix reserve offset missing\n");
		return -1;
	}

	fd = open(path, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "mk_hal[mt6765]: open %s failed: %s\n", path, strerror(errno));
		return -1;
	}

	memset(rec, 0, sizeof(rec));
	if (lseek(fd, (off_t) off, SEEK_SET) < 0) {
		fprintf(stderr, "mk_hal[mt6765]: seek %s @0x%llx failed: %s\n",
			path,
			(unsigned long long) off,
			strerror(errno));
		close(fd);
		return -1;
	}

	n = read(fd, rec, sizeof(rec));
	if (n != (ssize_t) sizeof(rec)) {
		fprintf(stderr, "mk_hal[mt6765]: read %s @0x%llx failed: %s\n",
			path,
			(unsigned long long) off,
			n < 0 ? strerror(errno) : "short read");
		close(fd);
		return -1;
	}

	magic_len = strlen(magic);
	if (magic_len > 8U) {
		magic_len = 8U;
	}
	if (memcmp(rec, magic, magic_len) != 0) {
		memset(rec, 0, sizeof(rec));
		memcpy(rec, magic, magic_len);
	}

	count = read_le32(rec + PHX_COUNT_OFF);
	rec[PHX_FLAG_BASE + (count % PHX_FLAG_SLOTS)] = 0x7cU;
	count++;
	write_le32(rec + PHX_COUNT_OFF, count);

	if (lseek(fd, (off_t) off, SEEK_SET) < 0) {
		fprintf(stderr, "mk_hal[mt6765]: seek %s @0x%llx failed: %s\n",
			path,
			(unsigned long long) off,
			strerror(errno));
		close(fd);
		return -1;
	}

	n = write(fd, rec, sizeof(rec));
	if (n != (ssize_t) sizeof(rec)) {
		fprintf(stderr, "mk_hal[mt6765]: write %s @0x%llx failed: %s\n",
			path,
			(unsigned long long) off,
			n < 0 ? strerror(errno) : "short write");
		close(fd);
		return -1;
	}

	fsync(fd);
	close(fd);
	printf("mk_hal[mt6765]: phoenix recovery flag set path=%s off=0x%llx count=%u slot=%u\n",
	       path,
	       (unsigned long long) off,
	       count,
	       (unsigned) ((count - 1U) % PHX_FLAG_SLOTS));
	return 0;
}

static mk_status_t mtk6765_open_hook(mtk_block_ctx_t *ctx)
{
	(void) ctx;
	return MK_OK;
}

static mk_status_t mtk6765_init(const mk_device_profile_t *profile, const char *block_path)
{
	mk_status_t rc;

	g_profile = profile;
	rc = mtk_display_init(&g_display, "mt6765", profile);
	if (rc != MK_OK) {
		return rc;
	}
	printf("mk_hal[mt6765]: display init panel=%s\n", mtk_display_selected_panel(&g_display));

	return mtk_block_open(&g_ctx, "mt6765", profile, block_path, mtk6765_open_hook);
}

static mk_status_t mtk6765_read_blocks(uint64_t lba, uint32_t count, void *out_buf, size_t out_size)
{
	return mtk_block_read(&g_ctx, lba, count, out_buf, out_size);
}

static uint32_t mtk6765_block_size(void)
{
	return mtk_block_size(&g_ctx);
}

static void mtk6765_reboot_recovery(void)
{
	if (g_profile != NULL && g_profile->phoenix_bootstage != NULL) {
		(void) write_proc_command("SET_BOOTSTAGE", g_profile->phoenix_bootstage);
	}
	(void) write_proc_command("SET_BOOTERROR", "ERROR_NATIVE_REBOOT_INTO_RECOVERY");
	(void) write_phoenix_recovery_flag();
	printf("mk_hal[mt6765]: reboot -> recovery\n");
	(void) reboot(RB_AUTOBOOT);
}

static void mtk6765_reboot_bootloader(void)
{
	printf("mk_hal[mt6765]: reboot -> bootloader (stub)\n");
}

const mk_hal_driver_t mk_hal_driver_mtk6765 = {
	.soc_name = "mt6765",
	.init = mtk6765_init,
	.read_blocks = mtk6765_read_blocks,
	.block_size = mtk6765_block_size,
	.reboot_recovery = mtk6765_reboot_recovery,
	.reboot_bootloader = mtk6765_reboot_bootloader,
};
