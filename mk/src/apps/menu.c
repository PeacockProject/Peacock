#include <stdio.h>
#include "menu.h"

void mk_menu_show_boot_summary(const char *device_name, const char *kernel_path, const char *initrd_path, const char *dtb_path)
{
	printf("mk: device=%s\n", device_name);
	printf("mk: kernel=%s\n", kernel_path);
	printf("mk: initrd=%s\n", initrd_path);
	printf("mk: dtb=%s\n", dtb_path);
}
