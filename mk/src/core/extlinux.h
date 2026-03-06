#ifndef MK_EXTLINUX_H
#define MK_EXTLINUX_H

#include "mk/common.h"

mk_status_t mk_extlinux_load_default(const char *config_path, mk_boot_entry_t *out_entry);

#endif
