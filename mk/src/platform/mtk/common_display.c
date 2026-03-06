#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "common_display.h"

static const char *find_panel_from_compat(const mk_device_profile_t *profile, const char *compat, size_t compat_len)
{
	size_t i;
	size_t off = 0;

	for (off = 0; off < compat_len;) {
		const char *token = compat + off;
		size_t token_len = 0;
		while ((off + token_len) < compat_len && token[token_len] != '\0') {
			token_len++;
		}

		if (token_len == 0) {
			off++;
			continue;
		}

		for (i = 0; i < profile->panel_compatible_count; i++) {
			const char *panel = profile->panel_compatibles[i];
			size_t panel_len;
			if (panel == NULL || panel[0] == '\0') {
				continue;
			}

			panel_len = strlen(panel);
			if ((token_len == panel_len && strncmp(token, panel, token_len) == 0) ||
			    strstr(token, panel) != NULL) {
				return panel;
			}
		}

		off += token_len + 1;
	}

	return NULL;
}

mk_status_t mtk_display_init(mtk_display_ctx_t *ctx, const char *soc_name, const mk_device_profile_t *profile)
{
	char compat[4096];
	ssize_t nread;
	int fd;

	if (ctx == NULL || soc_name == NULL || profile == NULL) {
		return MK_ERR;
	}

	memset(ctx, 0, sizeof(*ctx));
	ctx->soc_name = soc_name;
	ctx->profile = profile;

	if (profile->panel_compatibles == NULL || profile->panel_compatible_count == 0) {
		fprintf(stderr, "mk_hal[%s]: no panel list for device=%s\n", soc_name, profile->name);
		return MK_ERR_NOT_FOUND;
	}

	fd = open("/proc/device-tree/compatible", O_RDONLY);
	if (fd >= 0) {
		nread = read(fd, compat, sizeof(compat) - 1);
		(void) close(fd);
		if (nread > 0) {
			compat[nread] = '\0';
			ctx->selected_panel = find_panel_from_compat(profile, compat, (size_t) nread);
		}
	}

	if (ctx->selected_panel == NULL) {
		ctx->selected_panel = profile->panel_compatibles[0];
		printf("mk_hal[%s]: panel fallback=%s\n", soc_name, ctx->selected_panel);
	} else {
		printf("mk_hal[%s]: panel matched=%s\n", soc_name, ctx->selected_panel);
	}

	return MK_OK;
}

const char *mtk_display_selected_panel(const mtk_display_ctx_t *ctx)
{
	if (ctx == NULL) {
		return NULL;
	}
	return ctx->selected_panel;
}
