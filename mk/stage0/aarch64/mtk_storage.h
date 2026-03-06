#ifndef MK_STAGE0_MTK_STORAGE_H
#define MK_STAGE0_MTK_STORAGE_H

#include <stdint.h>

int mk_stage0_storage_prepare(void);
int mk_stage0_storage_find_partition(const char *label, uint64_t *out_start_lba, uint64_t *out_lba_count);
int mk_stage0_storage_read_sector(uint64_t lba, uint8_t *out512);
int mk_stage0_storage_capacity_bytes(uint64_t *out_bytes);

#endif
