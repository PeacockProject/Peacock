#ifndef MK_COMMON_H
#define MK_COMMON_H

#include <stdint.h>

typedef enum {
	MK_OK = 0,
	MK_ERR = -1,
	MK_ERR_NOT_FOUND = -2,
	MK_ERR_PARSE = -3,
} mk_status_t;

typedef struct {
	const char *label;
	uint64_t lba_start;
	uint64_t lba_count;
} mk_partition_t;

typedef struct {
	const char *kernel_path;
	const char *initrd_path;
	const char *dtb_path;
	const char *append;
} mk_boot_entry_t;

#endif
