#ifndef MK_FDT_H
#define MK_FDT_H

#include <stdint.h>

#define MK_FDT_MAGIC 0xd00dfeedU
#define MK_FDT_BEGIN_NODE 1U
#define MK_FDT_END_NODE 2U
#define MK_FDT_PROP 3U
#define MK_FDT_NOP 4U
#define MK_FDT_END 9U
#define MK_MAX_FDT_DEPTH 32

typedef struct {
	uint64_t addr;
	uint64_t size;
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	const char *format;
} simplefb_info_t;

/* Read-only FDT queries (from kernel_payload_main.c) */
int mk_fdt_find_chosen_prop(const void *fdt, const char *prop_name,
			    const uint8_t **out_value, uint32_t *out_len);
const char *mk_fdt_find_chosen_string(const void *fdt, const char *prop_name);
int mk_fdt_find_chosen_u64(const void *fdt, const char *prop_name, uint64_t *out_value);
int mk_fdt_find_compatible_prop(const void *fdt, const char *needle,
				const char *prop_name, const uint8_t **out_value,
				uint32_t *out_len);
int mk_fdt_root_has_compatible(const void *fdt, const char *needle);
int mk_fdt_find_compatible_reg(const void *fdt, const char *needle,
			       uint64_t *out_base);

/* Display info from FDT */
void mk_fdt_parse_simplefb(const void *fdt, simplefb_info_t *info);
void mk_fdt_parse_videolfb_from_chosen(const void *fdt, simplefb_info_t *info);

/* FDT patching (from mk_boot.c) */
uint32_t mk_fdt_str_append(uint8_t *fdt, const char *s);
void mk_fdt_struct_insert(uint8_t *fdt, uint32_t struct_off,
			  const uint8_t *data, uint32_t data_len);
uint32_t mk_fdt_build_prop(uint8_t *buf, uint32_t nameoff,
			   const uint8_t *val, uint32_t val_len);
uint32_t mk_fdt_chosen_end_off(const uint8_t *fdt);
uint32_t mk_fdt_node_end_off(const uint8_t *fdt, const char *nodename);
int mk_fdt_chosen_find_prop(const uint8_t *fdt, const char *name,
			    uint32_t *out_off, uint32_t *out_len);
void mk_fdt_patch_oplus_project(uint8_t *fdt);

/* FDT header accessors */
uint32_t mk_fdt_hdr32(const uint8_t *fdt, uint32_t off);
void mk_fdt_hdr32w(uint8_t *fdt, uint32_t off, uint32_t v);

#endif /* MK_FDT_H */
