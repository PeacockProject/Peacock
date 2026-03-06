#ifndef MK_DEVICE_PROFILE_H
#define MK_DEVICE_PROFILE_H

#include <stddef.h>
#include "mk/hal.h"

const mk_device_profile_t *mk_device_profile_find(const char *name);
const mk_device_profile_t *mk_device_profile_default(void);
size_t mk_device_profile_count(void);
const mk_device_profile_t *mk_device_profile_at(size_t index);

#endif
