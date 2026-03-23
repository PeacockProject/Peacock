#ifndef MK_STAGE0_MTK_STORAGE_H
#define MK_STAGE0_MTK_STORAGE_H

#include <stdint.h>

int mk_stage0_storage_prepare(void);
int mk_stage0_storage_find_partition(const char *label, uint64_t *out_start_lba, uint64_t *out_lba_count);
int mk_stage0_storage_find_partition_within(const char *container_label,
	const char *inner_label, uint64_t *out_start_lba, uint64_t *out_lba_count);
int mk_stage0_storage_read_sector(uint64_t lba, uint8_t *out512);
int mk_stage0_storage_write_sector(uint64_t lba, const uint8_t *in512);
int mk_stage0_storage_write_sectors(uint64_t lba, const uint8_t *in, uint32_t sector_count);
int mk_stage0_storage_capacity_bytes(uint64_t *out_bytes);
int mk_stage0_storage_flush(void);
void mk_stage0_storage_clr_write_prot_range(uint64_t start_lba, uint64_t sector_count);
void mk_stage0_storage_pet_wdt(void);

#endif
