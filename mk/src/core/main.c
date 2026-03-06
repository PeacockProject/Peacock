#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mk/common.h"
#include "mk/hal.h"
#include "mk/device_profile.h"
#include "gpt.h"
#include "extlinux.h"
#include "../apps/menu.h"

static void print_usage(const char *argv0)
{
	printf("usage: %s [--device <name>] [--soc <name>] [--list] [--reboot-recovery] [label] [block_path]\n", argv0);
	printf("       %s --list-devices\n", argv0);
}

static void list_devices(void)
{
	size_t i;
	for (i = 0; i < mk_device_profile_count(); i++) {
		const mk_device_profile_t *p = mk_device_profile_at(i);
		if (p == NULL) {
			continue;
		}
		printf("%s soc=%s boot=%s root=%s panels=%zu probe_paths=%zu\n",
		       p->name,
		       p->soc,
		       p->default_boot_partition,
		       p->default_root_partition,
		       p->panel_compatible_count,
		       p->block_probe_path_count);
	}
}

int main(int argc, char **argv)
{
	mk_status_t rc;
	mk_partition_t boot_part;
	mk_boot_entry_t entry;
	const char *target_label = NULL;
	const char *block_path = NULL;
	const char *device_name = NULL;
	const char *soc_override = NULL;
	const char *env_device;
	const mk_device_profile_t *profile;
	int list_mode = 0;
	int reboot_recovery_mode = 0;
	int i;
	int positional = 0;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (strcmp(arg, "--list") == 0) {
			list_mode = 1;
		} else if (strcmp(arg, "--reboot-recovery") == 0) {
			reboot_recovery_mode = 1;
		} else if (strcmp(arg, "--list-devices") == 0) {
			list_devices();
			return 0;
		} else if (strcmp(arg, "--device") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "mk: --device requires a value\n");
				print_usage(argv[0]);
				return 1;
			}
			device_name = argv[++i];
		} else if (strcmp(arg, "--soc") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "mk: --soc requires a value\n");
				print_usage(argv[0]);
				return 1;
			}
			soc_override = argv[++i];
		} else if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
			print_usage(argv[0]);
			return 0;
		} else if (arg[0] == '-') {
			fprintf(stderr, "mk: unknown option %s\n", arg);
			print_usage(argv[0]);
			return 1;
		} else if (positional == 0) {
			if (list_mode) {
				block_path = arg;
			} else {
				target_label = arg;
			}
			positional++;
		} else if (positional == 1) {
			block_path = arg;
			positional++;
		} else {
			fprintf(stderr, "mk: too many positional arguments\n");
			print_usage(argv[0]);
			return 1;
		}
	}

	if (device_name == NULL || device_name[0] == '\0') {
		env_device = getenv("MK_DEVICE");
		if (env_device != NULL && env_device[0] != '\0') {
			device_name = env_device;
		}
	}

	profile = (device_name != NULL && device_name[0] != '\0')
		      ? mk_device_profile_find(device_name)
		      : mk_device_profile_default();
	if (profile == NULL) {
		fprintf(stderr, "mk: unknown device profile '%s'\n", device_name);
		return 1;
	}

	if (block_path != NULL) {
		mk_hal_set_block_device_path(block_path);
	}
	if (soc_override != NULL && soc_override[0] != '\0') {
		mk_hal_set_soc_name(soc_override);
	}

	rc = mk_hal_init(profile);
	if (rc != MK_OK) {
		fprintf(stderr, "mk: HAL init failed (device=%s)\n", profile->name);
		return 1;
	}

	if (list_mode) {
		rc = mk_gpt_list_partitions();
		return rc == MK_OK ? 0 : 2;
	}

	if (reboot_recovery_mode || target_label == NULL) {
		mk_hal_reboot_recovery();
		return 0;
	}

	rc = mk_gpt_find_partition(target_label, &boot_part);
	if (rc != MK_OK) {
		fprintf(stderr, "mk: partition lookup failed for '%s'\n", target_label);
		return 2;
	}
	printf("mk: found %s at lba=%llu count=%llu\n",
	       target_label,
	       (unsigned long long) boot_part.lba_start,
	       (unsigned long long) boot_part.lba_count);

	rc = mk_extlinux_load_default("/boot/extlinux/extlinux.conf", &entry);
	if (rc != MK_OK) {
		fprintf(stderr, "mk: extlinux parse failed\n");
		return 3;
	}

	mk_menu_show_boot_summary(profile->name, entry.kernel_path, entry.initrd_path, entry.dtb_path);
	printf("mk: handoff not implemented yet\n");
	return 0;
}
