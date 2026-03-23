#ifndef MK_EXT2_H
#define MK_EXT2_H

#include <stdint.h>

#define MK_EXT2_MAGIC         0xEF53U
#define MK_EXT2_ROOT_INO      2U
#define MK_EXT4_INCOMPAT_EXTENTS 0x0040U
#define MK_EXT4_EXTENTS_FL    0x00080000U

typedef struct {
	uint64_t part_lba;
	uint32_t block_size;
	uint32_t sectors_per_block;
	uint32_t inodes_per_group;
	uint32_t inode_size;
	uint32_t first_data_block;
	uint32_t blocks_per_group;
	uint32_t feature_incompat;
} mk_ext2_t;

/* Returns 1 on success, 0 on error. */
int mk_ext2_open(uint64_t part_lba, mk_ext2_t *ctx);

/*
 * Load a file by absolute path from the ext2/ext4 filesystem.
 * Returns number of bytes written into dst, or -1 on error.
 */
int mk_ext2_load_file(const mk_ext2_t *ctx, const char *path,
		      uint8_t *dst, uint32_t dst_max);

#endif /* MK_EXT2_H */
