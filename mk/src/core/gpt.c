#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mk/hal.h"
#include "gpt.h"

#define GPT_HEADER_LBA 1U
#define GPT_SIGNATURE "EFI PART"
#define GPT_ENTRY_MIN_SIZE 128U
#define GPT_ENTRY_NAME_U16_COUNT 36U
#define GPT_MAX_ENTRIES 4096U

typedef struct {
	uint64_t base_lba;
	uint64_t entries_lba;
	uint32_t num_entries;
	uint32_t entry_size;
} gpt_layout_t;

static uint32_t le32_read(const uint8_t *buf)
{
	return (uint32_t) buf[0] |
	       ((uint32_t) buf[1] << 8) |
	       ((uint32_t) buf[2] << 16) |
	       ((uint32_t) buf[3] << 24);
}

static uint64_t le64_read(const uint8_t *buf)
{
	return (uint64_t) le32_read(buf) | ((uint64_t) le32_read(buf + 4) << 32);
}

static bool guid_is_zero(const uint8_t *buf, size_t len)
{
	size_t i;
	for (i = 0; i < len; i++) {
		if (buf[i] != 0U) {
			return false;
		}
	}
	return true;
}

static void utf16le_name_to_ascii(const uint8_t *in_u16, char *out, size_t out_len)
{
	size_t i;
	size_t j = 0U;

	if (out_len == 0U) {
		return;
	}

	for (i = 0; i < GPT_ENTRY_NAME_U16_COUNT && j + 1U < out_len; i++) {
		uint16_t cp = (uint16_t) in_u16[i * 2U] | ((uint16_t) in_u16[i * 2U + 1U] << 8);
		if (cp == 0U) {
			break;
		}
		/* Keep names ASCII-only for early bring-up logs. */
		out[j++] = (cp >= 0x20U && cp < 0x7FU) ? (char) cp : '?';
	}
	out[j] = '\0';
}

static mk_status_t gpt_read_layout_at(uint64_t base_lba, gpt_layout_t *out)
{
	uint8_t sector[512];
	uint32_t header_size;

	if (out == NULL) {
		return MK_ERR;
	}

	if (mk_hal_block_size() != sizeof(sector)) {
		fprintf(stderr, "mk_gpt: unsupported block size %u (expected 512)\n", mk_hal_block_size());
		return MK_ERR;
	}

	if (mk_hal_read_blocks(base_lba + GPT_HEADER_LBA, 1U, sector, sizeof(sector)) != MK_OK) {
		fprintf(stderr, "mk_gpt: failed reading GPT header @%llu\n",
			(unsigned long long) (base_lba + GPT_HEADER_LBA));
		return MK_ERR;
	}

	if (memcmp(sector, GPT_SIGNATURE, 8) != 0) {
		return MK_ERR_PARSE;
	}

	header_size = le32_read(sector + 12);
	if (header_size < 92U || header_size > 512U) {
		fprintf(stderr, "mk_gpt: invalid header size %u\n", header_size);
		return MK_ERR_PARSE;
	}

	out->base_lba = base_lba;
	out->entries_lba = base_lba + le64_read(sector + 72);
	out->num_entries = le32_read(sector + 80);
	out->entry_size = le32_read(sector + 84);

	if (out->entry_size < GPT_ENTRY_MIN_SIZE) {
		fprintf(stderr, "mk_gpt: invalid entry size %u\n", out->entry_size);
		return MK_ERR_PARSE;
	}
	if (out->num_entries == 0U || out->num_entries > GPT_MAX_ENTRIES) {
		fprintf(stderr, "mk_gpt: suspicious entry count %u\n", out->num_entries);
		return MK_ERR_PARSE;
	}

	return MK_OK;
}

static mk_status_t gpt_read_entry(const gpt_layout_t *layout, uint32_t index, uint8_t *entry_buf)
{
	uint64_t byte_offset;
	uint64_t lba;
	uint32_t in_sector;
	uint8_t sector[512];

	if (layout == NULL || entry_buf == NULL || index >= layout->num_entries) {
		return MK_ERR;
	}

	byte_offset = (uint64_t) index * (uint64_t) layout->entry_size;
	lba = layout->entries_lba + (byte_offset / 512U);
	in_sector = (uint32_t) (byte_offset % 512U);

	if (in_sector + layout->entry_size > 512U) {
		/* Keep parser simple for now: require entry to fit in one sector. */
		return MK_ERR_PARSE;
	}

	if (mk_hal_read_blocks(lba, 1U, sector, sizeof(sector)) != MK_OK) {
		return MK_ERR;
	}

	memcpy(entry_buf, sector + in_sector, layout->entry_size);
	return MK_OK;
}

static mk_status_t gpt_parse_partition_entry(const uint8_t *entry,
					     uint64_t lba_bias,
					     mk_partition_t *out_partition,
					     char *out_name,
					     size_t out_name_len)
{
	char name[80];
	uint64_t first_lba;
	uint64_t last_lba;

	if (entry == NULL) {
		return MK_ERR;
	}
	if (guid_is_zero(entry, 16U)) {
		return MK_ERR_NOT_FOUND;
	}

	utf16le_name_to_ascii(entry + 56U, name, sizeof(name));
	first_lba = le64_read(entry + 32U);
	last_lba = le64_read(entry + 40U);

	if (out_name != NULL && out_name_len != 0U) {
		size_t len = strlen(name);
		if (len + 1U > out_name_len) {
			len = out_name_len - 1U;
		}
		memcpy(out_name, name, len);
		out_name[len] = '\0';
	}

	if (out_partition != NULL) {
		out_partition->label = NULL;
		out_partition->lba_start = lba_bias + first_lba;
		out_partition->lba_count = (last_lba >= first_lba) ? (last_lba - first_lba + 1U) : 0U;
	}

	return MK_OK;
}

static int partition_looks_like_nested_disk(const mk_partition_t *part)
{
	return part != NULL && part->lba_count > (GPT_HEADER_LBA + 33U);
}

static mk_status_t gpt_find_partition_in_layout(const gpt_layout_t *layout,
						const char *label,
						uint64_t lba_bias,
						mk_partition_t *out_partition)
{
	uint8_t *entry;
	uint32_t i;

	if (layout == NULL || label == NULL || out_partition == NULL) {
		return MK_ERR;
	}

	entry = (uint8_t *) malloc(layout->entry_size);
	if (entry == NULL) {
		return MK_ERR;
	}

	for (i = 0; i < layout->num_entries; i++) {
		char name[80];
		mk_status_t rc = gpt_read_entry(layout, i, entry);
		if (rc != MK_OK) {
			free(entry);
			return rc;
		}
		if (gpt_parse_partition_entry(entry, lba_bias, out_partition, name, sizeof(name)) != MK_OK) {
			continue;
		}
		if (strcmp(name, label) != 0) {
			continue;
		}

		out_partition->label = label;
		free(entry);
		return MK_OK;
	}

	free(entry);
	return MK_ERR_NOT_FOUND;
}

mk_status_t mk_gpt_find_partition_relative(uint64_t base_lba,
					   const char *label,
					   mk_partition_t *out_partition)
{
	gpt_layout_t layout;

	if (label == NULL || out_partition == NULL) {
		return MK_ERR;
	}

	if (gpt_read_layout_at(base_lba, &layout) != MK_OK) {
		return MK_ERR_PARSE;
	}

	return gpt_find_partition_in_layout(&layout, label, base_lba, out_partition);
}

mk_status_t mk_gpt_find_partition(const char *label, mk_partition_t *out_partition)
{
	gpt_layout_t layout;
	uint8_t *entry;
	uint32_t i;
	mk_status_t rc;

	if (label == NULL || out_partition == NULL) {
		return MK_ERR;
	}

	rc = mk_gpt_find_partition_relative(0U, label, out_partition);
	if (rc == MK_OK) {
		return MK_OK;
	}
	if (rc != MK_ERR_NOT_FOUND) {
		return rc;
	}

	if (gpt_read_layout_at(0U, &layout) != MK_OK) {
		return MK_ERR_PARSE;
	}

	entry = (uint8_t *) malloc(layout.entry_size);
	if (entry == NULL) {
		return MK_ERR;
	}

	for (i = 0; i < layout.num_entries; i++) {
		char name[80];
		mk_partition_t container;

		rc = gpt_read_entry(&layout, i, entry);
		if (rc != MK_OK) {
			free(entry);
			return rc;
		}
		if (gpt_parse_partition_entry(entry, 0U, &container, name, sizeof(name)) != MK_OK) {
			continue;
		}
		if (!partition_looks_like_nested_disk(&container)) {
			continue;
		}

		rc = mk_gpt_find_partition_relative(container.lba_start, label, out_partition);
		if (rc == MK_OK) {
			out_partition->label = label;
			printf("mk_gpt: found nested %s inside %s (base_lba=%llu)\n",
			       label,
			       name[0] != '\0' ? name : "<unnamed>",
			       (unsigned long long) container.lba_start);
			free(entry);
			return MK_OK;
		}
		if (rc != MK_ERR_NOT_FOUND && rc != MK_ERR_PARSE) {
			free(entry);
			return rc;
		}
	}

	free(entry);
	fprintf(stderr, "mk_gpt: partition not found: %s\n", label);
	return MK_ERR_NOT_FOUND;
}

mk_status_t mk_gpt_list_partitions(void)
{
	gpt_layout_t layout;
	uint8_t *entry;
	uint32_t i;

	if (gpt_read_layout_at(0U, &layout) != MK_OK) {
		return MK_ERR_PARSE;
	}

	entry = (uint8_t *) malloc(layout.entry_size);
	if (entry == NULL) {
		return MK_ERR;
	}

	printf("mk_gpt: entries=%u entry_size=%u entries_lba=%llu\n",
	       layout.num_entries,
	       layout.entry_size,
	       (unsigned long long) layout.entries_lba);

	for (i = 0; i < layout.num_entries; i++) {
		char name[80];
		mk_partition_t part;
		gpt_layout_t nested_layout;
		uint8_t *nested_entry;
		uint32_t j;

		if (gpt_read_entry(&layout, i, entry) != MK_OK) {
			break;
		}
		if (gpt_parse_partition_entry(entry, 0U, &part, name, sizeof(name)) != MK_OK) {
			continue;
		}

		printf("  - %s (lba=%llu..%llu)\n",
		       name[0] != '\0' ? name : "<unnamed>",
		       (unsigned long long) part.lba_start,
		       (unsigned long long) (part.lba_count != 0U
						      ? (part.lba_start + part.lba_count - 1U)
						      : part.lba_start));

		if (!partition_looks_like_nested_disk(&part) ||
		    gpt_read_layout_at(part.lba_start, &nested_layout) != MK_OK) {
			continue;
		}

		nested_entry = (uint8_t *) malloc(nested_layout.entry_size);
		if (nested_entry == NULL) {
			continue;
		}

		for (j = 0; j < nested_layout.num_entries; j++) {
			char nested_name[80];
			mk_partition_t nested_part;
			if (gpt_read_entry(&nested_layout, j, nested_entry) != MK_OK) {
				break;
			}
			if (gpt_parse_partition_entry(nested_entry,
						      part.lba_start,
						      &nested_part,
						      nested_name,
						      sizeof(nested_name)) != MK_OK) {
				continue;
			}
			printf("      * %s (sub lba=%llu..%llu, parent=%s)\n",
			       nested_name[0] != '\0' ? nested_name : "<unnamed>",
			       (unsigned long long) nested_part.lba_start,
			       (unsigned long long) (nested_part.lba_count != 0U
							      ? (nested_part.lba_start + nested_part.lba_count - 1U)
							      : nested_part.lba_start),
			       name[0] != '\0' ? name : "<unnamed>");
		}

		free(nested_entry);
	}

	free(entry);
	return MK_OK;
}
