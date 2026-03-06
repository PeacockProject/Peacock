#include <stdio.h>
#include "extlinux.h"

mk_status_t mk_extlinux_load_default(const char *config_path, mk_boot_entry_t *out_entry)
{
	if (config_path == NULL || out_entry == NULL) {
		return MK_ERR;
	}

	/*
	 * Development stub. Real implementation should parse extlinux.conf and
	 * resolve FILESYSTEM-relative paths.
	 */
	out_entry->kernel_path = "/boot/Image.gz";
	out_entry->initrd_path = "/boot/initramfs.img";
	out_entry->dtb_path = "/boot/dtb.img";
	out_entry->append = "quiet";

	printf("mk_extlinux: using config %s (stub)\n", config_path);
	return MK_OK;
}
