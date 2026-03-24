/*
 * mk_fdt.c — Flattened Device Tree read/query and patching routines.
 *
 * Extracted from kernel_payload_main.c (read-only queries) and
 * mk_boot.c (patching helpers).
 */

#include "mk_common.h"
#include "mk_fdt.h"
#include "mtk_usb.h"

/* ------------------------------------------------------------------ */
/* FDT header field offset constants (byte offsets into the FDT blob) */
/* ------------------------------------------------------------------ */

#define FDT_OFF_TOTALSIZE   4U
#define FDT_OFF_STRUCT      8U
#define FDT_OFF_STRINGS     12U
#define FDT_OFF_SIZE_STR    32U
#define FDT_OFF_SIZE_STRUCT 36U

/* ------------------------------------------------------------------ */
/* Static helpers                                                     */
/* ------------------------------------------------------------------ */

static void parse_reg(const uint8_t *buf, uint32_t len, simplefb_info_t *info)
{
	if (len >= 16) {
		info->addr = be64_read(buf);
		info->size = be64_read(buf + 8);
	} else if (len >= 8) {
		info->addr = (uint64_t) be32_read(buf);
		info->size = (uint64_t) be32_read(buf + 4);
	} else if (len >= 4) {
		info->addr = (uint64_t) be32_read(buf);
	}
}

static int mk_fdt_name_eq(const char *a, const char *b)
{
	while (*a != '\0' && *b != '\0') {
		if (*a != *b) { return 0; }
		a++;
		b++;
	}
	return (*a == '\0' && *b == '\0') ? 1 : 0;
}

/*
 * Search bootargs for key=value token starting with prefix.
 * Returns pointer to start of the match, or 0 if not found.
 */
static const char *
mk_fdt_find_bootarg(const char *bootargs, const char *prefix, uint32_t prefix_len)
{
	uint32_t i;

	if (bootargs == 0 || prefix == 0) {
		return 0;
	}
	for (i = 0U; bootargs[i] != '\0'; i++) {
		uint32_t j;

		if (i != 0U && bootargs[i - 1U] != ' ') {
			continue;
		}
		for (j = 0U; j < prefix_len && bootargs[i + j] == prefix[j]; j++) {
		}
		if (j == prefix_len) {
			return &bootargs[i];
		}
	}
	return 0;
}

static uint32_t mk_fdt_parse_decimal_u32(const char *s)
{
	uint32_t val = 0U;

	while (*s >= '0' && *s <= '9') {
		val = val * 10U + (uint32_t) (*s - '0');
		s++;
	}
	return val;
}

/* Large scratch buffer for FDT property records (BSS, not stack).
 * Aligned to 8 so the compiler can emit str-x without faulting when
 * SCTLR_EL1.A (alignment check) is enabled by LK. */
static uint8_t __attribute__((aligned(8))) s_prop_buf[2100];

/* ------------------------------------------------------------------ */
/* FDT header accessors                                               */
/* ------------------------------------------------------------------ */

uint32_t mk_fdt_hdr32(const uint8_t *fdt, uint32_t off)
{
	return be32_read(fdt + off);
}

void mk_fdt_hdr32w(uint8_t *fdt, uint32_t off, uint32_t v)
{
	uint8_t *p = fdt + off;

	p[0] = (uint8_t) (v >> 24);
	p[1] = (uint8_t) (v >> 16);
	p[2] = (uint8_t) (v >> 8);
	p[3] = (uint8_t) v;
}

/* ------------------------------------------------------------------ */
/* Read-only FDT queries (from kernel_payload_main.c)                 */
/* ------------------------------------------------------------------ */

int mk_fdt_find_chosen_prop(const void *fdt, const char *prop_name,
			    const uint8_t **out_value, uint32_t *out_len)
{
	const uint8_t *base = (const uint8_t *) fdt;
	uint32_t off_struct;
	uint32_t off_strings;
	uint32_t size_struct;
	uint32_t size_strings;
	const uint8_t *p;
	const uint8_t *struct_end;
	const uint8_t *strings;
	const uint8_t *strings_end;
	uint8_t chosen_stack[MK_MAX_FDT_DEPTH] = {0};
	int depth = -1;

	if (base == 0 || prop_name == 0 || out_value == 0 || out_len == 0) {
		return 0;
	}
	if (be32_read(base) != MK_FDT_MAGIC) {
		return 0;
	}

	off_struct = be32_read(base + 8);
	off_strings = be32_read(base + 12);
	size_strings = be32_read(base + 32);
	size_struct = be32_read(base + 36);

	p = base + off_struct;
	struct_end = p + size_struct;
	strings = base + off_strings;
	strings_end = strings + size_strings;

	while (p + 4 <= struct_end) {
		uint32_t token = be32_read(p);
		p += 4;

		if (token == MK_FDT_BEGIN_NODE) {
			const char *node_name = (const char *) p;

			depth++;
			if (depth >= MK_MAX_FDT_DEPTH) {
				return 0;
			}
			chosen_stack[depth] = 0;
			if (depth == 1 && str_eq(node_name, "chosen")) {
				chosen_stack[depth] = 1;
			} else if (depth > 1 && chosen_stack[depth - 1] != 0) {
				chosen_stack[depth] = 1;
			}

			while (p < struct_end && *p != '\0') {
				p++;
			}
			if (p < struct_end) {
				p++;
			}
			while (((uintptr_t) p & 3U) != 0U && p < struct_end) {
				p++;
			}
			continue;
		}

		if (token == MK_FDT_END_NODE) {
			if (depth >= 0) {
				depth--;
			}
			continue;
		}

		if (token == MK_FDT_NOP) {
			continue;
		}

		if (token == MK_FDT_END) {
			break;
		}

		if (token == MK_FDT_PROP) {
			const uint8_t *value;
			uint32_t len;
			uint32_t nameoff;
			const char *name;

			if (p + 8 > struct_end) {
				return 0;
			}
			len = be32_read(p);
			nameoff = be32_read(p + 4);
			p += 8;
			value = p;
			p += len;
			while (((uintptr_t) p & 3U) != 0U && p < struct_end) {
				p++;
			}
			if (nameoff >= size_strings || strings + nameoff >= strings_end) {
				continue;
			}
			if (depth < 1 || chosen_stack[depth] == 0) {
				continue;
			}

			name = (const char *) (strings + nameoff);
			if (str_eq(name, prop_name)) {
				*out_value = value;
				*out_len = len;
				return 1;
			}
		}
	}

	return 0;
}

const char *mk_fdt_find_chosen_string(const void *fdt, const char *prop_name)
{
	const uint8_t *value = 0;
	uint32_t len = 0;

	if (!mk_fdt_find_chosen_prop(fdt, prop_name, &value, &len) || len == 0) {
		return 0;
	}
	return (const char *) value;
}

int mk_fdt_find_chosen_u64(const void *fdt, const char *prop_name, uint64_t *out_value)
{
	const uint8_t *value = 0;
	uint32_t len = 0;

	if (out_value == 0) {
		return 0;
	}
	if (!mk_fdt_find_chosen_prop(fdt, prop_name, &value, &len)) {
		return 0;
	}
	if (len >= 8) {
		*out_value = be64_read(value);
		return 1;
	}
	if (len >= 4) {
		*out_value = (uint64_t) be32_read(value);
		return 1;
	}
	return 0;
}

int mk_fdt_find_compatible_prop(const void *fdt, const char *needle,
				const char *prop_name, const uint8_t **out_value,
				uint32_t *out_len)
{
	const uint8_t *base = (const uint8_t *) fdt;
	uint32_t off_struct;
	uint32_t off_strings;
	uint32_t size_struct;
	uint32_t size_strings;
	const uint8_t *p;
	const uint8_t *struct_end;
	const uint8_t *strings;
	const uint8_t *strings_end;
	uint8_t match_stack[MK_MAX_FDT_DEPTH] = {0};
	int depth = -1;

	if (base == 0 || needle == 0 || prop_name == 0 || out_value == 0 || out_len == 0) {
		return 0;
	}
	if (be32_read(base) != MK_FDT_MAGIC) {
		return 0;
	}

	off_struct = be32_read(base + 8);
	off_strings = be32_read(base + 12);
	size_strings = be32_read(base + 32);
	size_struct = be32_read(base + 36);

	p = base + off_struct;
	struct_end = p + size_struct;
	strings = base + off_strings;
	strings_end = strings + size_strings;

	while (p + 4 <= struct_end) {
		uint32_t token = be32_read(p);
		p += 4;

		if (token == MK_FDT_BEGIN_NODE) {
			depth++;
			if (depth >= MK_MAX_FDT_DEPTH) {
				return 0;
			}
			match_stack[depth] = 0;
			while (p < struct_end && *p != '\0') {
				p++;
			}
			if (p < struct_end) {
				p++;
			}
			while (((uintptr_t) p & 3U) != 0U && p < struct_end) {
				p++;
			}
			continue;
		}
		if (token == MK_FDT_END_NODE) {
			if (depth >= 0) {
				depth--;
			}
			continue;
		}
		if (token == MK_FDT_NOP) {
			continue;
		}
		if (token == MK_FDT_END) {
			break;
		}
		if (token != MK_FDT_PROP) {
			continue;
		}

		if (p + 8 > struct_end) {
			return 0;
		}
		{
			uint32_t len = be32_read(p);
			uint32_t nameoff = be32_read(p + 4);
			const uint8_t *value;
			const char *name;

			p += 8;
			value = p;
			p += len;
			while (((uintptr_t) p & 3U) != 0U && p < struct_end) {
				p++;
			}

			if (nameoff >= size_strings || strings + nameoff >= strings_end) {
				continue;
			}
			name = (const char *) (strings + nameoff);
			if (depth >= 0 && str_eq(name, "compatible") &&
			    value_has_string(value, len, needle)) {
				match_stack[depth] = 1;
				continue;
			}
			if (depth < 0 || match_stack[depth] == 0) {
				continue;
			}
			if (!str_eq(name, prop_name)) {
				continue;
			}
			*out_value = value;
			*out_len = len;
			return 1;
		}
	}

	return 0;
}

int mk_fdt_root_has_compatible(const void *fdt, const char *needle)
{
	const uint8_t *base = (const uint8_t *) fdt;
	uint32_t off_struct;
	uint32_t off_strings;
	uint32_t size_struct;
	uint32_t size_strings;
	const uint8_t *p;
	const uint8_t *struct_end;
	const uint8_t *strings;
	const uint8_t *strings_end;
	int depth = -1;

	if (base == 0 || needle == 0 || be32_read(base) != MK_FDT_MAGIC) {
		return 0;
	}

	off_struct = be32_read(base + 8);
	off_strings = be32_read(base + 12);
	size_strings = be32_read(base + 32);
	size_struct = be32_read(base + 36);

	p = base + off_struct;
	struct_end = p + size_struct;
	strings = base + off_strings;
	strings_end = strings + size_strings;

	while (p + 4 <= struct_end) {
		uint32_t token = be32_read(p);
		p += 4;

		if (token == MK_FDT_BEGIN_NODE) {
			depth++;
			while (p < struct_end && *p != '\0') {
				p++;
			}
			if (p < struct_end) {
				p++;
			}
			while (((uintptr_t) p & 3U) != 0U && p < struct_end) {
				p++;
			}
			continue;
		}
		if (token == MK_FDT_END_NODE) {
			if (depth >= 0) {
				depth--;
			}
			continue;
		}
		if (token == MK_FDT_NOP) {
			continue;
		}
		if (token == MK_FDT_END) {
			break;
		}
		if (token != MK_FDT_PROP) {
			continue;
		}

		if (p + 8 > struct_end) {
			return 0;
		}
		{
			uint32_t len = be32_read(p);
			uint32_t nameoff = be32_read(p + 4);
			const uint8_t *value;
			const char *name;

			p += 8;
			value = p;
			p += len;
			while (((uintptr_t) p & 3U) != 0U && p < struct_end) {
				p++;
			}

			if (nameoff >= size_strings || strings + nameoff >= strings_end) {
				continue;
			}
			name = (const char *) (strings + nameoff);
			if (depth == 0 && str_eq(name, "compatible") &&
			    value_has_string(value, len, needle)) {
				return 1;
			}
		}
	}

	return 0;
}

int mk_fdt_find_compatible_reg(const void *fdt, const char *needle,
			       uint64_t *out_base)
{
	const uint8_t *base = (const uint8_t *) fdt;
	uint32_t off_struct;
	uint32_t off_strings;
	uint32_t size_struct;
	uint32_t size_strings;
	const uint8_t *p;
	const uint8_t *struct_end;
	const uint8_t *strings;
	const uint8_t *strings_end;
	uint8_t match_stack[MK_MAX_FDT_DEPTH] = {0};
	int depth = -1;

	if (base == 0 || needle == 0 || out_base == 0 || be32_read(base) != MK_FDT_MAGIC) {
		return 0;
	}

	off_struct = be32_read(base + 8);
	off_strings = be32_read(base + 12);
	size_strings = be32_read(base + 32);
	size_struct = be32_read(base + 36);

	p = base + off_struct;
	struct_end = p + size_struct;
	strings = base + off_strings;
	strings_end = strings + size_strings;

	while (p + 4 <= struct_end) {
		uint32_t token = be32_read(p);
		p += 4;

		if (token == MK_FDT_BEGIN_NODE) {
			depth++;
			if (depth >= MK_MAX_FDT_DEPTH) {
				return 0;
			}
			match_stack[depth] = 0;
			while (p < struct_end && *p != '\0') {
				p++;
			}
			if (p < struct_end) {
				p++;
			}
			while (((uintptr_t) p & 3U) != 0U && p < struct_end) {
				p++;
			}
			continue;
		}
		if (token == MK_FDT_END_NODE) {
			if (depth >= 0) {
				depth--;
			}
			continue;
		}
		if (token == MK_FDT_NOP) {
			continue;
		}
		if (token == MK_FDT_END) {
			break;
		}
		if (token != MK_FDT_PROP) {
			continue;
		}

		if (p + 8 > struct_end) {
			return 0;
		}
		{
			uint32_t len = be32_read(p);
			uint32_t nameoff = be32_read(p + 4);
			const uint8_t *value;
			const char *name;
			simplefb_info_t reg;

			p += 8;
			value = p;
			p += len;
			while (((uintptr_t) p & 3U) != 0U && p < struct_end) {
				p++;
			}

			if (nameoff >= size_strings || strings + nameoff >= strings_end) {
				continue;
			}
			name = (const char *) (strings + nameoff);
			if (depth >= 0 && str_eq(name, "compatible") &&
			    value_has_string(value, len, needle)) {
				match_stack[depth] = 1;
				continue;
			}
			if (depth < 0 || match_stack[depth] == 0) {
				continue;
			}
			if (!str_eq(name, "reg")) {
				continue;
			}

			reg.addr = 0;
			reg.size = 0;
			reg.width = 0;
			reg.height = 0;
			reg.stride = 0;
			reg.format = 0;
			parse_reg(value, len, &reg);
			if (reg.addr != 0) {
				*out_base = reg.addr;
				return 1;
			}
		}
	}

	return 0;
}

/* ------------------------------------------------------------------ */
/* Display info from FDT                                              */
/* ------------------------------------------------------------------ */

void mk_fdt_parse_simplefb(const void *fdt, simplefb_info_t *info)
{
	const uint8_t *base = (const uint8_t *) fdt;
	uint32_t off_struct;
	uint32_t off_strings;
	uint32_t size_struct;
	uint32_t size_strings;
	const uint8_t *p;
	const uint8_t *struct_end;
	const uint8_t *strings;
	const uint8_t *strings_end;
	uint8_t simple_stack[MK_MAX_FDT_DEPTH] = {0};
	int depth = -1;

	if (base == 0 || info == 0) {
		return;
	}
	if (be32_read(base) != MK_FDT_MAGIC) {
		return;
	}

	off_struct = be32_read(base + 8);
	off_strings = be32_read(base + 12);
	size_strings = be32_read(base + 32);
	size_struct = be32_read(base + 36);

	p = base + off_struct;
	struct_end = p + size_struct;
	strings = base + off_strings;
	strings_end = strings + size_strings;

	while (p + 4 <= struct_end) {
		uint32_t token = be32_read(p);
		p += 4;

		if (token == MK_FDT_BEGIN_NODE) {
			depth++;
			if (depth >= MK_MAX_FDT_DEPTH) {
				return;
			}
			simple_stack[depth] = 0;
			while (p < struct_end && *p != '\0') {
				p++;
			}
			if (p < struct_end) {
				p++;
			}
			while (((uintptr_t) p & 3U) != 0U && p < struct_end) {
				p++;
			}
			continue;
		}

		if (token == MK_FDT_END_NODE) {
			if (depth >= 0) {
				depth--;
			}
			continue;
		}

		if (token == MK_FDT_NOP) {
			continue;
		}

		if (token == MK_FDT_END) {
			break;
		}

		if (token == MK_FDT_PROP) {
			const uint8_t *value;
			uint32_t len;
			uint32_t nameoff;
			const char *name;
			if (p + 8 > struct_end) {
				return;
			}
			len = be32_read(p);
			nameoff = be32_read(p + 4);
			p += 8;
			value = p;
			p += len;
			while (((uintptr_t) p & 3U) != 0U && p < struct_end) {
				p++;
			}
			if (nameoff >= size_strings || strings + nameoff >= strings_end) {
				continue;
			}
			name = (const char *) (strings + nameoff);

			if (depth >= 0 && str_eq(name, "compatible") &&
			    value_has_string(value, len, "simple-framebuffer")) {
				simple_stack[depth] = 1;
				continue;
			}

			if (depth < 0 || simple_stack[depth] == 0) {
				continue;
			}

			if (str_eq(name, "reg")) {
				parse_reg(value, len, info);
			} else if (str_eq(name, "width") && len >= 4) {
				info->width = be32_read(value);
			} else if (str_eq(name, "height") && len >= 4) {
				info->height = be32_read(value);
			} else if (str_eq(name, "stride") && len >= 4) {
				info->stride = be32_read(value);
			} else if (str_eq(name, "format")) {
				info->format = (const char *) value;
			}
		}
	}
}

void mk_fdt_parse_videolfb_from_chosen(const void *fdt, simplefb_info_t *info)
{
	uint64_t fb_hi;
	uint64_t fb_lo;
	uint64_t fb_size;

	if (info == 0 || info->addr != 0) {
		return;
	}

	fb_hi = 0;
	fb_lo = 0;
	fb_size = 0;

	(void) mk_fdt_find_chosen_u64(fdt, "atag,videolfb-fb_base_h", &fb_hi);
	(void) mk_fdt_find_chosen_u64(fdt, "atag,videolfb-fb_base_l", &fb_lo);
	(void) mk_fdt_find_chosen_u64(fdt, "atag,videolfb-vramSize", &fb_size);

	if (fb_lo == 0 || fb_size == 0) {
		return;
	}

	info->addr = (fb_hi << 32) | (fb_lo & 0xffffffffULL);
	info->size = fb_size;
}

/* ------------------------------------------------------------------ */
/* FDT patching (from mk_boot.c)                                     */
/* ------------------------------------------------------------------ */

/*
 * Append a null-terminated string to the strings section.
 * Returns its nameoff within the strings section.
 */
uint32_t mk_fdt_str_append(uint8_t *fdt, const char *s)
{
	uint32_t off_str   = mk_fdt_hdr32(fdt, FDT_OFF_STRINGS);
	uint32_t sz_str    = mk_fdt_hdr32(fdt, FDT_OFF_SIZE_STR);
	uint32_t totalsize = mk_fdt_hdr32(fdt, FDT_OFF_TOTALSIZE);
	uint32_t nameoff   = sz_str;
	uint32_t slen      = mk_strlen(s) + 1U;
	uint32_t i;

	for (i = 0U; i < slen; i++) {
		fdt[off_str + sz_str + i] = (uint8_t) s[i];
	}

	mk_fdt_hdr32w(fdt, FDT_OFF_SIZE_STR, sz_str + slen);
	mk_fdt_hdr32w(fdt, FDT_OFF_TOTALSIZE, totalsize + slen);
	return nameoff;
}

/*
 * Insert 'data_len' bytes into the struct section at struct-relative byte
 * 'struct_off'. Shifts the entire remainder of the FDT (including the
 * strings section) to make room.
 */
void mk_fdt_struct_insert(uint8_t *fdt, uint32_t struct_off,
			  const uint8_t *data, uint32_t data_len)
{
	uint32_t off_struct  = mk_fdt_hdr32(fdt, FDT_OFF_STRUCT);
	uint32_t sz_struct   = mk_fdt_hdr32(fdt, FDT_OFF_SIZE_STRUCT);
	uint32_t off_strings = mk_fdt_hdr32(fdt, FDT_OFF_STRINGS);
	uint32_t totalsize   = mk_fdt_hdr32(fdt, FDT_OFF_TOTALSIZE);
	uint8_t *insert_at   = fdt + off_struct + struct_off;
	/* Move everything from insert point to end of FDT (includes strings). */
	uint32_t bytes_to_move = totalsize - off_struct - struct_off;
	uint32_t rem;

	uart_puts_all("[mk] fdt: insert struct_off=0x");
	uart_puthex64_all((uint64_t) struct_off);
	uart_puts_all(" len=0x");
	uart_puthex64_all((uint64_t) data_len);
	uart_puts_all(" off_struct=0x");
	uart_puthex64_all((uint64_t) off_struct);
	uart_puts_all(" sz_struct=0x");
	uart_puthex64_all((uint64_t) sz_struct);
	uart_puts_all("\r\n");
	uart_puts_all("[mk] fdt: insert off_strings=0x");
	uart_puthex64_all((uint64_t) off_strings);
	uart_puts_all(" totalsize=0x");
	uart_puthex64_all((uint64_t) totalsize);
	uart_puts_all(" bytes_to_move=0x");
	uart_puthex64_all((uint64_t) bytes_to_move);
	uart_puts_all("\r\n");
	uart_puts_all("[mk] fdt: insert memmove begin\r\n");
	/*
	 * Do the overlap copy explicitly here instead of relying on the
	 * compiler's inlined memmove lowering. This keeps the copy shape
	 * simple and lets us pet the watchdog inside long copies.
	 */
	rem = bytes_to_move;
	while (rem > 0U) {
		rem--;
		insert_at[data_len + rem] = insert_at[rem];
		if ((rem & 0xFFFFU) == 0U) {
			pet_wdt();
			uart_puts_all("[mk] fdt: insert rem=0x");
			uart_puthex64_all((uint64_t) rem);
			uart_puts_all("\r\n");
		}
	}
	uart_puts_all("[mk] fdt: insert memmove done\r\n");
	mk_memcpy(insert_at, data, data_len);
	uart_puts_all("[mk] fdt: insert memcpy done\r\n");

	mk_fdt_hdr32w(fdt, FDT_OFF_SIZE_STRUCT, sz_struct + data_len);
	/* Strings section moves right if it lives after the struct. */
	if (off_strings > off_struct + struct_off) {
		mk_fdt_hdr32w(fdt, FDT_OFF_STRINGS, off_strings + data_len);
	}
	mk_fdt_hdr32w(fdt, FDT_OFF_TOTALSIZE, totalsize + data_len);
	uart_puts_all("[mk] fdt: insert hdr done\r\n");
}

/*
 * Build a FDT_PROP record into buf and return its byte length.
 * buf must be at least 12 + ((val_len + 3) & ~3) bytes.
 */
uint32_t mk_fdt_build_prop(uint8_t *buf, uint32_t nameoff,
			   const uint8_t *val, uint32_t val_len)
{
	uint32_t padded = (val_len + 3U) & ~3U;
	uint8_t tmp[4];

	tmp[0] = (uint8_t) (MK_FDT_PROP >> 24);
	tmp[1] = (uint8_t) (MK_FDT_PROP >> 16);
	tmp[2] = (uint8_t) (MK_FDT_PROP >> 8);
	tmp[3] = (uint8_t) MK_FDT_PROP;
	mk_memcpy(buf, tmp, 4U);

	tmp[0] = (uint8_t) (val_len >> 24);
	tmp[1] = (uint8_t) (val_len >> 16);
	tmp[2] = (uint8_t) (val_len >> 8);
	tmp[3] = (uint8_t) val_len;
	mk_memcpy(buf + 4U, tmp, 4U);

	tmp[0] = (uint8_t) (nameoff >> 24);
	tmp[1] = (uint8_t) (nameoff >> 16);
	tmp[2] = (uint8_t) (nameoff >> 8);
	tmp[3] = (uint8_t) nameoff;
	mk_memcpy(buf + 8U, tmp, 4U);

	mk_memcpy(buf + 12U, val, val_len);
	mk_memset(buf + 12U + val_len, 0U, padded - val_len);

	return 12U + padded;
}

/*
 * Find the struct-section byte offset of the FDT_END_NODE that closes
 * /chosen (depth 1). We insert new properties just before this offset.
 * Returns 0 on failure.
 */
uint32_t mk_fdt_chosen_end_off(const uint8_t *fdt)
{
	uint32_t off_struct = mk_fdt_hdr32(fdt, FDT_OFF_STRUCT);
	uint32_t sz_struct  = mk_fdt_hdr32(fdt, FDT_OFF_SIZE_STRUCT);
	const uint8_t *p    = fdt + off_struct;
	const uint8_t *end  = p + sz_struct;
	int depth    = -1;
	int in_chose = 0;

	while (p + 4U <= end) {
		uint32_t token   = be32_read(p);

		p += 4U;

		if (token == MK_FDT_BEGIN_NODE) {
			const char *nm = (const char *) p;

			depth++;

			if (depth == 1) {
				/* Check if node name is "chosen". */
				if (nm[0] == 'c' && nm[1] == 'h' && nm[2] == 'o' &&
				    nm[3] == 's' && nm[4] == 'e' && nm[5] == 'n' &&
				    nm[6] == '\0') {
					in_chose = 1;
				}
			}
			while (p < end && *p != '\0') { p++; }
			if (p < end) { p++; }
			while (((uintptr_t) p & 3U) != 0U && p < end) { p++; }

		} else if (token == MK_FDT_END_NODE) {
			uint32_t cur_off = (uint32_t) ((p - 4U) - (fdt + off_struct));
			if (in_chose && depth == 1) {
				/* Found it: insert BEFORE this token. */
				return cur_off;
			}
			if (depth >= 0) { depth--; }
			if (depth < 1)  { in_chose = 0; }

		} else if (token == MK_FDT_PROP) {
			uint32_t val_len;
			uint32_t padded;

			if (p + 8U > end) { break; }
			val_len = be32_read(p);
			padded  = (val_len + 3U) & ~3U;
			p += 8U + padded;

		} else if (token == MK_FDT_NOP) {
			/* nothing */
		} else {
			break;
		}
	}

	return 0U;
}

/*
 * Find the struct-section byte offset of the FDT_END_NODE that closes
 * the first node whose name matches 'nodename' (at any depth).
 * Returns 0 on failure.
 */
uint32_t mk_fdt_node_end_off(const uint8_t *fdt, const char *nodename)
{
	uint32_t off_struct = mk_fdt_hdr32(fdt, FDT_OFF_STRUCT);
	uint32_t sz_struct  = mk_fdt_hdr32(fdt, FDT_OFF_SIZE_STRUCT);
	const uint8_t *base = fdt + off_struct;
	const uint8_t *p    = base;
	const uint8_t *end  = p + sz_struct;
	int depth        = -1;
	int target_depth = -1;

	while (p + 4U <= end) {
		uint32_t token = be32_read(p);

		p += 4U;

		if (token == MK_FDT_BEGIN_NODE) {
			const char *nm = (const char *) p;
			uint32_t ni = 0U;
			int match = 1;

			depth++;
			if (target_depth < 0) {
				/* compare node name with nodename */
				while (nodename[ni] != '\0') {
					if (nm[ni] != nodename[ni]) {
						match = 0;
						break;
					}
					ni++;
				}
				if (match && nm[ni] != '\0' && nm[ni] != '@') {
					match = 0;
				}
				if (match) {
					target_depth = depth;
				}
			}
			while (p < end && *p != '\0') { p++; }
			if (p < end) { p++; }
			while (((uintptr_t) p & 3U) != 0U && p < end) { p++; }

		} else if (token == MK_FDT_END_NODE) {
			if (target_depth >= 0 && depth == target_depth) {
				return (uint32_t) ((p - 4U) - base);
			}
			if (depth >= 0) { depth--; }

		} else if (token == MK_FDT_PROP) {
			uint32_t val_len;
			uint32_t padded;

			if (p + 8U > end) { break; }
			val_len = be32_read(p);
			padded  = (val_len + 3U) & ~3U;
			p += 8U + padded;

		} else if (token == MK_FDT_NOP) {
			/* nothing */
		} else {
			break;
		}
	}

	return 0U;
}

int mk_fdt_chosen_find_prop(const uint8_t *fdt, const char *name,
			    uint32_t *val_off, uint32_t *val_len)
{
	uint32_t off_struct  = mk_fdt_hdr32(fdt, FDT_OFF_STRUCT);
	uint32_t off_strings = mk_fdt_hdr32(fdt, FDT_OFF_STRINGS);
	uint32_t sz_struct   = mk_fdt_hdr32(fdt, FDT_OFF_SIZE_STRUCT);
	const uint8_t *p     = fdt + off_struct;
	const uint8_t *end   = p + sz_struct;
	int depth     = -1;
	int in_chose  = 0;

	while (p + 4U <= end) {
		uint32_t token = be32_read(p);

		p += 4U;

		if (token == MK_FDT_BEGIN_NODE) {
			const char *nm = (const char *) p;

			depth++;
			if (depth == 1 &&
			    nm[0] == 'c' && nm[1] == 'h' && nm[2] == 'o' &&
			    nm[3] == 's' && nm[4] == 'e' && nm[5] == 'n' &&
			    nm[6] == '\0') {
				in_chose = 1;
			}
			while (p < end && *p != '\0') { p++; }
			if (p < end) { p++; }
			while (((uintptr_t) p & 3U) != 0U && p < end) { p++; }

		} else if (token == MK_FDT_END_NODE) {
			if (depth >= 0) { depth--; }
			if (depth < 1) { in_chose = 0; }

		} else if (token == MK_FDT_PROP) {
			uint32_t plen;
			uint32_t nameoff;
			uint32_t padded;
			const char *prop_name;

			if (p + 8U > end) { break; }
			plen = be32_read(p);
			nameoff = be32_read(p + 4U);
			padded = (plen + 3U) & ~3U;
			prop_name = (const char *) (fdt + off_strings + nameoff);
			if (in_chose && depth == 1 && mk_fdt_name_eq(prop_name, name)) {
				*val_off = (uint32_t) ((p + 8U) - fdt);
				*val_len = plen;
				return 1;
			}
			p += 8U + padded;

		} else if (token == MK_FDT_NOP) {
			/* nothing */
		} else {
			break;
		}
	}

	return 0;
}

/*
 * Populate the oplus_project DT node with hardware identity data
 * extracted from LK bootargs.  The node already exists in the DTB
 * (from the kernel DTS) but is empty -- LK normally fills it at
 * runtime.  Since MK replaces LK, we do it here.
 */
void mk_fdt_patch_oplus_project(uint8_t *fdt)
{
	const char *lk;
	const char *match;
	uint32_t project_no;
	uint32_t noff;
	uint32_t plen;
	uint8_t  val[4];
	uint32_t no_newcdt, no_nVersion, no_nProject, no_nDtsi;
	uint32_t no_nAudio, no_nRF, no_nPCB, no_eng, no_conf;

	lk = mk_stage0_mtk_usb_get_lk_bootargs();
	if (lk == 0) {
		uart_puts_all("[mk] fdt: oplus_project: no LK bootargs\r\n");
		return;
	}

	match = mk_fdt_find_bootarg(lk, "androidboot.prjname=", 20U);
	if (match == 0) {
		uart_puts_all("[mk] fdt: oplus_project: no prjname\r\n");
		return;
	}

	project_no = mk_fdt_parse_decimal_u32(match + 20U);
	if (project_no == 0U) {
		uart_puts_all("[mk] fdt: oplus_project: prjname=0\r\n");
		return;
	}

	noff = mk_fdt_node_end_off(fdt, "oplus_project");
	if (noff == 0U) {
		uart_puts_all("[mk] fdt: oplus_project node not found\r\n");
		return;
	}

	uart_puts_all("[mk] fdt: oplus_project end=0x");
	uart_puthex64_all((uint64_t) noff);
	uart_puts_all(" prj=0x");
	uart_puthex64_all((uint64_t) project_no);
	uart_puts_all("\r\n");

	/* Append property name strings to the string table. */
	no_newcdt   = mk_fdt_str_append(fdt, "newcdt");
	no_nVersion = mk_fdt_str_append(fdt, "nVersion");
	no_nProject = mk_fdt_str_append(fdt, "nProject");
	no_nDtsi    = mk_fdt_str_append(fdt, "nDtsi");
	no_nAudio   = mk_fdt_str_append(fdt, "nAudio");
	no_nRF      = mk_fdt_str_append(fdt, "nRF");
	no_nPCB     = mk_fdt_str_append(fdt, "nPCB");
	no_eng      = mk_fdt_str_append(fdt, "eng_version");
	no_conf     = mk_fdt_str_append(fdt, "is_confidential");

	/*
	 * Insert properties in reverse desired order.  Each insert at
	 * 'noff' pushes the previous ones right, so the final layout
	 * (reading forwards) matches insertion order reversed.
	 */
#define OP_INS_U32(nameoff, v) do {		\
	val[0] = (uint8_t) ((v) >> 24);	\
	val[1] = (uint8_t) ((v) >> 16);	\
	val[2] = (uint8_t) ((v) >> 8);		\
	val[3] = (uint8_t) (v);		\
	plen = mk_fdt_build_prop(s_prop_buf, (nameoff), val, 4U); \
	mk_fdt_struct_insert(fdt, noff, s_prop_buf, plen); \
} while (0)

	OP_INS_U32(no_conf,     0U);             /* is_confidential */
	OP_INS_U32(no_eng,      0U);             /* eng_version */
	OP_INS_U32(no_nPCB,     0U);             /* nPCB */
	OP_INS_U32(no_nRF,      0U);             /* nRF */
	OP_INS_U32(no_nAudio,   project_no);     /* nAudio */
	OP_INS_U32(no_nDtsi,    project_no);     /* nDtsi */
	OP_INS_U32(no_nProject, project_no);     /* nProject */
	OP_INS_U32(no_nVersion, 1U);             /* nVersion */
	OP_INS_U32(no_newcdt,   1U);             /* newcdt */

#undef OP_INS_U32

	uart_puts_all("[mk] fdt: oplus_project patched\r\n");
}
