/*
 * Minimal ext2/ext4 filesystem reader for MK stage0.
 * Supports: block pointers (ext2) and extent trees (ext4, depth<=1).
 * Directory lookup via linear scan. No writes.
 */

#include <stdint.h>
#include "mk_ext2.h"
#include "mtk_storage.h"

/* ------------------------------------------------------------------ */
/* Byte helpers                                                         */
/* ------------------------------------------------------------------ */

static uint16_t e2_le16(const uint8_t *p)
{
	return (uint16_t) ((uint16_t) p[0] | ((uint16_t) p[1] << 8));
}

static uint32_t e2_le32(const uint8_t *p)
{
	return (uint32_t) p[0] |
	       ((uint32_t) p[1] << 8) |
	       ((uint32_t) p[2] << 16) |
	       ((uint32_t) p[3] << 24);
}

static int e2_name_eq(const uint8_t *entry_name, uint8_t name_len,
		      const char *needle)
{
	uint32_t i;

	for (i = 0U; i < (uint32_t) name_len; i++) {
		if (needle[i] == '\0' || entry_name[i] != (uint8_t) needle[i]) {
			return 0;
		}
	}
	return needle[name_len] == '\0';
}

/* ------------------------------------------------------------------ */
/* Block I/O                                                            */
/* ------------------------------------------------------------------ */

static int e2_read_block_into(const mk_ext2_t *ctx, uint32_t block_no,
			      uint8_t *dst, uint32_t max_bytes)
{
	uint64_t lba = ctx->part_lba +
		       (uint64_t) block_no * (uint64_t) ctx->sectors_per_block;
	uint32_t s;
	uint32_t n_sectors = ctx->sectors_per_block;

	if (n_sectors * 512U > max_bytes) {
		n_sectors = max_bytes / 512U;
	}

	for (s = 0U; s < n_sectors; s++) {
		mk_stage0_storage_pet_wdt();
		if (!mk_stage0_storage_read_sector(lba + s, dst + s * 512U)) {
			return 0;
		}
	}
	return 1;
}

/* ------------------------------------------------------------------ */
/* Inode lookup                                                         */
/* ------------------------------------------------------------------ */

/*
 * Superblock field offsets (byte offsets from start of superblock).
 * ext2/ext4 on-disk format; all fields are little-endian.
 */
#define SB_FIRST_DATA_BLOCK   20U   /* u32 */
#define SB_LOG_BLOCK_SIZE     24U   /* u32: block_size = 1024 << value */
#define SB_BLOCKS_PER_GROUP   32U   /* u32 */
#define SB_INODES_PER_GROUP   40U   /* u32 */
#define SB_MAGIC              56U   /* u16: 0xEF53 */
#define SB_REV_LEVEL          76U   /* u32 */
#define SB_INODE_SIZE         88U   /* u16 (dynamic rev only) */
#define SB_FEATURE_INCOMPAT   96U   /* u32 (dynamic rev only) */

/*
 * Inode field offsets within the inode record.
 */
#define INO_SIZE    4U   /* u32: file size (low 32 bits) */
#define INO_FLAGS  32U   /* u32: inode flags, EXT4_EXTENTS_FL = 0x80000 */
#define INO_BLOCK  40U   /* u32[15]: block pointers or extent tree root */

/*
 * Read inode ino_num (1-based) into dst_inode.
 * dst_inode must be ctx->inode_size bytes.
 * Uses a single 512-byte sector buffer on the stack.
 */
static int e2_read_inode(const mk_ext2_t *ctx, uint32_t ino_num,
			 uint8_t *dst_inode)
{
	uint32_t group_idx;
	uint32_t ino_idx_in_group;
	uint32_t desc_block;
	uint32_t desc_byte;
	uint64_t desc_lba;
	uint32_t inode_table_block;
	uint32_t ino_byte_in_table;
	uint32_t ino_sector_off;
	uint64_t ino_lba;
	uint8_t sector[512];
	uint32_t copy_src;
	uint32_t i;

	if (ino_num < 1U) {
		return 0;
	}

	ino_num--;  /* convert from 1-based to 0-based */
	group_idx         = ino_num / ctx->inodes_per_group;
	ino_idx_in_group  = ino_num % ctx->inodes_per_group;

	/*
	 * Group descriptor table starts at block (first_data_block + 1).
	 * Each descriptor is 32 bytes.
	 */
	desc_block = ctx->first_data_block + 1U;
	desc_byte  = group_idx * 32U;

	desc_lba = ctx->part_lba +
		   (uint64_t) desc_block * (uint64_t) ctx->sectors_per_block +
		   (uint64_t) (desc_byte / 512U);

	if (!mk_stage0_storage_read_sector(desc_lba, sector)) {
		return 0;
	}

	/* bg_inode_table is at offset +8 within each 32-byte group descriptor. */
	inode_table_block = e2_le32(sector + (desc_byte % 512U) + 8U);

	/* Byte offset of this inode within the inode table. */
	ino_byte_in_table = ino_idx_in_group * ctx->inode_size;
	ino_sector_off    = ino_byte_in_table / 512U;
	ino_lba = ctx->part_lba +
		  (uint64_t) inode_table_block * (uint64_t) ctx->sectors_per_block +
		  (uint64_t) ino_sector_off;

	if (!mk_stage0_storage_read_sector(ino_lba, sector)) {
		return 0;
	}

	copy_src = ino_byte_in_table % 512U;

	if (copy_src + ctx->inode_size <= 512U) {
		/* Fits in one sector. */
		for (i = 0U; i < ctx->inode_size; i++) {
			dst_inode[i] = sector[copy_src + i];
		}
	} else {
		/* Spans two sectors. */
		uint32_t first_chunk = 512U - copy_src;
		uint8_t sector2[512];

		for (i = 0U; i < first_chunk; i++) {
			dst_inode[i] = sector[copy_src + i];
		}
		if (!mk_stage0_storage_read_sector(ino_lba + 1U, sector2)) {
			return 0;
		}
		for (i = 0U; i < ctx->inode_size - first_chunk; i++) {
			dst_inode[first_chunk + i] = sector2[i];
		}
	}

	return 1;
}

/* ------------------------------------------------------------------ */
/* Block iteration callbacks                                            */
/* ------------------------------------------------------------------ */

/*
 * Callback invoked for each (logical_block, physical_block) pair.
 * Return 0 to stop, 1 to continue.
 */
typedef int (*e2_block_cb)(void *arg, uint32_t log_block, uint32_t phys_block);

/* ------------------------------------------------------------------ */
/* Extent tree (ext4)                                                   */
/* ------------------------------------------------------------------ */

#define EXT4_EH_MAGIC 0xF30AU

static int e2_iter_extents(const mk_ext2_t *ctx, const uint8_t *hdr,
			   e2_block_cb cb, void *arg)
{
	uint16_t magic;
	uint16_t entries;
	uint16_t depth;
	uint32_t i;

	magic   = e2_le16(hdr);
	entries = e2_le16(hdr + 2U);
	depth   = e2_le16(hdr + 6U);

	if (magic != EXT4_EH_MAGIC) {
		return 0;
	}

	if (depth == 0U) {
		/* Leaf: entries are ext4_extent records (12 bytes each). */
		for (i = 0U; i < (uint32_t) entries; i++) {
			const uint8_t *ext = hdr + 12U + i * 12U;
			uint32_t ee_block  = e2_le32(ext);
			uint32_t ee_len    = (uint32_t) e2_le16(ext + 4U) & 0x7FFFU;
			uint32_t ee_start  = e2_le32(ext + 8U);
			uint32_t j;

			for (j = 0U; j < ee_len; j++) {
				if (!cb(arg, ee_block + j, ee_start + j)) {
					return 1;
				}
			}
		}
	} else {
		/*
		 * Index: entries are ext4_extent_idx records (12 bytes each).
		 * Each points to a child block. We support exactly one level.
		 */
		uint8_t child[4096];

		if (ctx->block_size > sizeof(child)) {
			return 0;
		}

		for (i = 0U; i < (uint32_t) entries; i++) {
			const uint8_t *idx = hdr + 12U + i * 12U;
			uint32_t child_phys = e2_le32(idx + 4U);

			if (!e2_read_block_into(ctx, child_phys, child,
						sizeof(child))) {
				return 0;
			}
			if (!e2_iter_extents(ctx, child, cb, arg)) {
				return 0;
			}
		}
	}

	return 1;
}

/* ------------------------------------------------------------------ */
/* Classic ext2 indirect block iteration                               */
/* ------------------------------------------------------------------ */

static int e2_iter_indirect(const mk_ext2_t *ctx, uint32_t ind_phys,
			    uint32_t *log_block_p,
			    e2_block_cb cb, void *arg)
{
	uint8_t block_buf[4096];
	uint32_t ptrs_per_block;
	uint32_t i;

	if (ind_phys == 0U) {
		*log_block_p += ctx->block_size / 4U;
		return 1;
	}
	if (ctx->block_size > sizeof(block_buf)) {
		return 0;
	}
	if (!e2_read_block_into(ctx, ind_phys, block_buf, sizeof(block_buf))) {
		return 0;
	}

	ptrs_per_block = ctx->block_size / 4U;
	for (i = 0U; i < ptrs_per_block; i++) {
		uint32_t phys = e2_le32(block_buf + i * 4U);

		if (phys != 0U) {
			if (!cb(arg, *log_block_p, phys)) {
				return 0;
			}
		}
		(*log_block_p)++;
	}
	return 1;
}

static int e2_iter_blocks(const mk_ext2_t *ctx, const uint8_t *inode,
			  e2_block_cb cb, void *arg)
{
	const uint8_t *iblock = inode + INO_BLOCK;
	uint32_t flags = e2_le32(inode + INO_FLAGS);
	uint32_t log_block;
	uint32_t i;

	if ((flags & MK_EXT4_EXTENTS_FL) != 0U) {
		return e2_iter_extents(ctx, iblock, cb, arg);
	}

	/* Classic ext2: direct (0-11), then single indirect (12). */
	log_block = 0U;

	for (i = 0U; i < 12U; i++) {
		uint32_t phys = e2_le32(iblock + i * 4U);

		if (phys != 0U) {
			if (!cb(arg, log_block, phys)) {
				return 1;
			}
		}
		log_block++;
	}

	/* Single indirect. */
	if (!e2_iter_indirect(ctx, e2_le32(iblock + 12U * 4U),
			      &log_block, cb, arg)) {
		return 0;
	}

	/* Double indirect. */
	{
		uint32_t dind_phys = e2_le32(iblock + 13U * 4U);

		if (dind_phys != 0U) {
			uint8_t dind_buf[4096];
			uint32_t ptrs_per_block = ctx->block_size / 4U;
			uint32_t j;

			if (ctx->block_size > sizeof(dind_buf)) {
				return 0;
			}
			if (!e2_read_block_into(ctx, dind_phys, dind_buf,
						sizeof(dind_buf))) {
				return 0;
			}
			for (j = 0U; j < ptrs_per_block; j++) {
				uint32_t ind_phys = e2_le32(dind_buf + j * 4U);

				if (!e2_iter_indirect(ctx, ind_phys, &log_block,
						      cb, arg)) {
					return 0;
				}
			}
		}
	}

	/* Triple indirect not needed for current boot assets. */
	return 1;
}

/* ------------------------------------------------------------------ */
/* Directory lookup                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
	const mk_ext2_t *ctx;
	const char      *name;
	uint32_t         found_ino;
} e2_lookup_t;

static int e2_dir_block_cb(void *arg, uint32_t log_block, uint32_t phys_block)
{
	e2_lookup_t *lc = (e2_lookup_t *) arg;
	uint8_t block_buf[4096];
	uint32_t off;

	(void) log_block;

	if (lc->ctx->block_size > sizeof(block_buf)) {
		return 0;
	}
	if (!e2_read_block_into(lc->ctx, phys_block, block_buf,
				sizeof(block_buf))) {
		return 0;
	}

	off = 0U;
	while (off + 8U <= lc->ctx->block_size) {
		uint32_t ino      = e2_le32(block_buf + off);
		uint16_t rec_len  = e2_le16(block_buf + off + 4U);
		uint8_t  name_len = block_buf[off + 6U];

		if (rec_len < 8U) {
			break;
		}
		if (ino != 0U && name_len > 0U &&
		    e2_name_eq(block_buf + off + 8U, name_len, lc->name)) {
			lc->found_ino = ino;
			return 0;  /* stop */
		}
		off += (uint32_t) rec_len;
	}

	return 1;  /* continue */
}

static uint32_t e2_dir_lookup(const mk_ext2_t *ctx, uint32_t dir_ino,
			      const char *name)
{
	uint8_t inode_buf[256];
	e2_lookup_t lc;

	if (ctx->inode_size > sizeof(inode_buf)) {
		return 0U;
	}
	if (!e2_read_inode(ctx, dir_ino, inode_buf)) {
		return 0U;
	}

	lc.ctx       = ctx;
	lc.name      = name;
	lc.found_ino = 0U;

	(void) e2_iter_blocks(ctx, inode_buf, e2_dir_block_cb, &lc);

	return lc.found_ino;
}

/* ------------------------------------------------------------------ */
/* File load                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
	const mk_ext2_t *ctx;
	uint8_t         *dst;
	uint32_t         dst_max;
	uint32_t         file_size;
	uint32_t         written;   /* highest byte index written + 1 */
	int              error;
} e2_load_t;

static int e2_load_block_cb(void *arg, uint32_t log_block, uint32_t phys_block)
{
	e2_load_t *lc = (e2_load_t *) arg;
	uint32_t block_off = log_block * lc->ctx->block_size;
	uint32_t to_copy;
	uint32_t s;
	uint8_t tmp[512];

	if (block_off >= lc->file_size) {
		return 0;
	}
	to_copy = lc->ctx->block_size;
	if (block_off + to_copy > lc->file_size) {
		to_copy = lc->file_size - block_off;
	}
	if (block_off + to_copy > lc->dst_max) {
		lc->error = 1;
		return 0;
	}

	for (s = 0U; s < lc->ctx->sectors_per_block; s++) {
		uint64_t lba = lc->ctx->part_lba +
			       (uint64_t) phys_block *
			       (uint64_t) lc->ctx->sectors_per_block +
			       (uint64_t) s;
		uint32_t sector_off = s * 512U;
		uint32_t chunk;
		uint32_t i;

		if (sector_off >= to_copy) {
			break;
		}
		chunk = 512U;
		if (sector_off + chunk > to_copy) {
			chunk = to_copy - sector_off;
		}

		mk_stage0_storage_pet_wdt();
		if (!mk_stage0_storage_read_sector(lba, tmp)) {
			lc->error = 1;
			return 0;
		}

		for (i = 0U; i < chunk; i++) {
			lc->dst[block_off + sector_off + i] = tmp[i];
		}
	}

	if (block_off + to_copy > lc->written) {
		lc->written = block_off + to_copy;
	}

	return (block_off + to_copy < lc->file_size) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

int mk_ext2_open(uint64_t part_lba, mk_ext2_t *ctx)
{
	uint8_t sb[512];
	uint16_t magic;
	uint32_t log_block_size;
	uint32_t rev_level;

	/* Superblock is at byte offset 1024 from partition start = LBA+2. */
	if (!mk_stage0_storage_read_sector(part_lba + 2U, sb)) {
		return 0;
	}

	magic = e2_le16(sb + SB_MAGIC);
	if (magic != MK_EXT2_MAGIC) {
		return 0;
	}

	log_block_size         = e2_le32(sb + SB_LOG_BLOCK_SIZE);
	ctx->block_size        = 1024U << log_block_size;
	ctx->sectors_per_block = ctx->block_size / 512U;
	ctx->first_data_block  = e2_le32(sb + SB_FIRST_DATA_BLOCK);
	ctx->blocks_per_group  = e2_le32(sb + SB_BLOCKS_PER_GROUP);
	ctx->inodes_per_group  = e2_le32(sb + SB_INODES_PER_GROUP);
	ctx->part_lba          = part_lba;

	rev_level = e2_le32(sb + SB_REV_LEVEL);
	if (rev_level >= 1U) {
		ctx->inode_size       = (uint32_t) e2_le16(sb + SB_INODE_SIZE);
		ctx->feature_incompat = e2_le32(sb + SB_FEATURE_INCOMPAT);
	} else {
		ctx->inode_size       = 128U;
		ctx->feature_incompat = 0U;
	}

	if (ctx->block_size < 512U || ctx->block_size > 4096U) {
		return 0;
	}
	if (ctx->sectors_per_block == 0U) {
		return 0;
	}
	if (ctx->inode_size < 128U || ctx->inode_size > 512U) {
		return 0;
	}
	if (ctx->inodes_per_group == 0U || ctx->blocks_per_group == 0U) {
		return 0;
	}

	return 1;
}

int mk_ext2_load_file(const mk_ext2_t *ctx, const char *path,
		      uint8_t *dst, uint32_t dst_max)
{
	uint32_t cur_ino = MK_EXT2_ROOT_INO;
	const char *p = path;
	uint8_t inode_buf[256];
	e2_load_t lc;

	if (ctx->inode_size > sizeof(inode_buf)) {
		return -1;
	}

	/* Walk path components. */
	while (*p == '/') {
		p++;
	}

	while (*p != '\0') {
		char component[256];
		uint32_t clen = 0U;
		uint32_t next_ino;

		while (p[clen] != '/' && p[clen] != '\0' && clen < 255U) {
			component[clen] = p[clen];
			clen++;
		}
		component[clen] = '\0';
		p += clen;
		while (*p == '/') {
			p++;
		}

		next_ino = e2_dir_lookup(ctx, cur_ino, component);
		if (next_ino == 0U) {
			return -1;
		}
		cur_ino = next_ino;
	}

	if (!e2_read_inode(ctx, cur_ino, inode_buf)) {
		return -1;
	}

	lc.ctx       = ctx;
	lc.dst       = dst;
	lc.dst_max   = dst_max;
	lc.file_size = e2_le32(inode_buf + INO_SIZE);
	lc.written   = 0U;
	lc.error     = 0;

	(void) e2_iter_blocks(ctx, inode_buf, e2_load_block_cb, &lc);

	if (lc.error) {
		return -1;
	}

	return (int) lc.written;
}
