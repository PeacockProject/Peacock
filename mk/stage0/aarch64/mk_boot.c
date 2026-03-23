/*
 * MK stage0: boot Linux from the Peacock boot partition via extlinux.
 *
 * Implements:
 *   1. extlinux.conf parser
 *   2. RFC 1951 deflate decompressor + RFC 1952 gzip wrapper
 *   3. FDT /chosen patcher (bootargs + initrd)
 *   4. D-cache flush + MMU disable + arm64 kernel jump
 */

#include <stdint.h>
#include "mk_boot.h"
#include "mk_ext2.h"
#include "mtk_i2c.h"
#include "mtk_usb.h"
#include "mk_zlib_gzip.h"

/* ------------------------------------------------------------------ */
/* UART (declared in kernel_payload_main.c)                            */
/* ------------------------------------------------------------------ */

void uart_puts_all(const char *s);
void uart_puthex64_all(uint64_t v);
void pet_wdt(void);
void mk_stage0_log_reset_watchdog_state(const char *tag);
extern char __mk_stack_top[];
extern char __mk_el1_vectors[];
extern char __mk_el1_trampoline[];
extern void __mk_chainload_raw(uint64_t fdt_pa, uint64_t entry);
extern uint64_t __mk_saved_el2_valid;
extern uint64_t __mk_saved_cptr_el2;
extern uint64_t __mk_saved_vbar_el2;
extern uint64_t __mk_saved_sctlr_el2;

#define MK_UART0_BASE_ADDR          0x11002000ULL
#define MK_UART_RBR_THR_DLL_OFF     0x00U
#define MK_UART_IER_DLH_OFF         0x04U
#define MK_UART_IIR_FCR_OFF         0x08U
#define MK_UART_LSR_OFF             0x14U
#define MK_UART_ESCAPE_EN_OFF       (0x11U << 2)
#define MK_UART_SLEEP_EN_OFF        (0x12U << 2)
#define MK_UART_DMA_EN_OFF          (0x13U << 2)

#define MK_UART_LSR_THRE            0x20U
#define MK_UART_LSR_TEMT            0x40U

#define MK_UART_FCR_FIFO_EN         0x01U
#define MK_UART_FCR_CLEAR_RX        0x02U
#define MK_UART_FCR_CLEAR_TX        0x04U

/* ------------------------------------------------------------------ */
/* Memory layout for loading                                           */
/* ------------------------------------------------------------------ */

/*
 * LK + MK payload occupy roughly 0x40000000–0x47FFFFFF.
 * Load areas are placed safely above that.
 *
 * MK_GZIP_STAGE_ADDR: raw Image.gz loaded here before decompression
 * MK_KERNEL_BASE:     2MB-aligned DRAM base for kernel placement
 *                     kernel lands at MK_KERNEL_BASE + text_offset
 * MK_INITRD_ADDR:     initramfs load address
 * MK_FDT_COPY_ADDR:   copy of LK FDT, patched for Peacock boot
 */
#define MK_GZIP_STAGE_ADDR    0x4C000000UL
#define MK_KERNEL_BASE        0x49000000UL
#define MK_INITRD_ADDR        0x50000000UL
#define MK_FDT_COPY_ADDR      0x54000000UL
#define MK_FDT_MAX_SIZE       (2U * 1024U * 1024U)
#define MK_GZIP_MAX_SIZE      (20U * 1024U * 1024U)
#define MK_INITRD_MAX_SIZE    (10U * 1024U * 1024U)
#define MK_KERNEL_MAX_SIZE    (64U * 1024U * 1024U)

#define MK_SCTLR_EL1_INIT             0x30500800ULL
#define MK_CPTR_EL2_DEFAULT           0x33ffULL
#define MK_HCR_EL2_RW                 (1ULL << 31)
#define MK_HCR_EL2_API                (1ULL << 41)
#define MK_HCR_EL2_APK                (1ULL << 40)
#define MK_HCR_HOST_NVHE_FLAGS        (MK_HCR_EL2_RW | MK_HCR_EL2_API | MK_HCR_EL2_APK)
#define MK_SPSR_EL2_EL1H_MASKED       0x3c5ULL
#define MK_ICC_SRE_EL2_SRE            (1ULL << 0)
#define MK_ICC_SRE_EL2_ENABLE         (1ULL << 3)

/* ------------------------------------------------------------------ */
/* FDT tokens and header offsets                                       */
/* ------------------------------------------------------------------ */

#define FDT_MAGIC_VAL      0xD00DFEEDUL
#define FDT_BEGIN_NODE     1U
#define FDT_END_NODE       2U
#define FDT_PROP           3U
#define FDT_NOP            4U
#define FDT_END            9U

#define FDT_OFF_TOTALSIZE  4U
#define FDT_OFF_STRUCT     8U
#define FDT_OFF_STRINGS    12U
#define FDT_OFF_SIZE_STR   32U
#define FDT_OFF_SIZE_STRUCT 36U

/* ------------------------------------------------------------------ */
/* Byte helpers (no libc)                                              */
/* ------------------------------------------------------------------ */

static uint32_t mk_strlen(const char *s)
{
	uint32_t n = 0U;
	while (s[n] != '\0') { n++; }
	return n;
}

static uint32_t mk_strnlen(const char *s, uint32_t max_len)
{
	uint32_t n = 0U;
	while (n < max_len && s[n] != '\0') { n++; }
	return n;
}

static int mk_str_starts(const char *s, const char *prefix)
{
	while (*prefix != '\0') {
		if (*s != *prefix) { return 0; }
		s++;
		prefix++;
	}
	return 1;
}

static void mk_memcpy(uint8_t *dst, const uint8_t *src, uint32_t n)
{
	uint32_t i;
	for (i = 0U; i < n; i++) { dst[i] = src[i]; }
}

static void mk_memmove(uint8_t *dst, const uint8_t *src, uint32_t n)
{
	uint32_t i;
	if (dst <= src) {
		for (i = 0U; i < n; i++) { dst[i] = src[i]; }
	} else {
		for (i = n; i > 0U; i--) { dst[i - 1U] = src[i - 1U]; }
	}
}

static void mk_memset(uint8_t *dst, uint8_t val, uint32_t n)
{
	uint32_t i;
	for (i = 0U; i < n; i++) { dst[i] = val; }
}

static uint32_t be32r(const uint8_t *p)
{
	return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) |
	       ((uint32_t) p[2] << 8)  |  (uint32_t) p[3];
}

static void be32w(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t) (v >> 24);
	p[1] = (uint8_t) (v >> 16);
	p[2] = (uint8_t) (v >> 8);
	p[3] = (uint8_t) v;
}

static void be64w(uint8_t *p, uint64_t v)
{
	be32w(p, (uint32_t) (v >> 32));
	be32w(p + 4U, (uint32_t) v);
}

static uint32_t le32r(const uint8_t *p)
{
	return (uint32_t) p[0] | ((uint32_t) p[1] << 8) |
	       ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

static uint32_t mk_mmio_read32(uint64_t addr)
{
	return *(volatile uint32_t *) (uintptr_t) addr;
}

static void mk_mmio_write32(uint64_t addr, uint32_t value)
{
	*(volatile uint32_t *) (uintptr_t) addr = value;
}

static uint64_t le64r(const uint8_t *p)
{
	return (uint64_t) le32r(p) | ((uint64_t) le32r(p + 4U) << 32);
}

static void trace_first16(const char *tag, const uint8_t *p)
{
	uart_puts_all(tag);
	uart_puts_all(" [0..7]=0x");
	uart_puthex64_all(le64r(p));
	uart_puts_all(" [8..15]=0x");
	uart_puthex64_all(le64r(p + 8U));
	uart_puts_all("\r\n");
}

/* ------------------------------------------------------------------ */
/* extlinux.conf parser                                                */
/* ------------------------------------------------------------------ */

typedef struct {
	char kernel[256];
	char initrd[256];
	char append[512];
} extlinux_entry_t;

static const char *skip_ws(const char *p)
{
	while (*p == ' ' || *p == '\t') { p++; }
	return p;
}

static void copy_value(const char *src, char *dst, uint32_t dst_max)
{
	uint32_t i = 0U;

	src = skip_ws(src);
	while (*src != '\0' && *src != '\n' && *src != '\r' &&
	       i < dst_max - 1U) {
		dst[i++] = *src++;
	}
	while (i > 0U && (dst[i - 1U] == ' ' || dst[i - 1U] == '\t')) {
		i--;
	}
	dst[i] = '\0';
}

static int parse_extlinux(const char *buf, uint32_t len,
			  extlinux_entry_t *out)
{
	const char *p   = buf;
	const char *end = buf + len;
	int in_label    = 0;

	out->kernel[0] = '\0';
	out->initrd[0] = '\0';
	out->append[0] = '\0';

	while (p < end) {
		const char *line_start = p;
		const char *line_end;
		const char *lp;

		while (p < end && *p != '\n') { p++; }
		line_end = p;
		if (p < end) { p++; }

		lp = skip_ws(line_start);

		if (*lp == '#' || lp >= line_end) {
			continue;
		}

		if (mk_str_starts(lp, "label ") || mk_str_starts(lp, "label\t")) {
			in_label = 1;
			continue;
		}

		if (!in_label) {
			continue;
		}

		if (mk_str_starts(lp, "linux ") || mk_str_starts(lp, "linux\t") ||
		    mk_str_starts(lp, "kernel ") || mk_str_starts(lp, "kernel\t")) {
			const char *v = lp;

			while (*v != ' ' && *v != '\t' && *v != '\0' && v < line_end) {
				v++;
			}
			copy_value(v, out->kernel, sizeof(out->kernel));
		} else if (mk_str_starts(lp, "initrd ") ||
			   mk_str_starts(lp, "initrd\t")) {
			copy_value(lp + 6U, out->initrd, sizeof(out->initrd));
		} else if (mk_str_starts(lp, "append ") ||
			   mk_str_starts(lp, "append\t")) {
			copy_value(lp + 7U, out->append, sizeof(out->append));
		}
	}

	return (out->kernel[0] != '\0') ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Inflate (RFC 1951) + gzip (RFC 1952) decompressor                  */
/* ------------------------------------------------------------------ */

typedef struct {
	const uint8_t *src;
	uint32_t       src_len;
	uint32_t       src_pos;
	uint32_t       bits;
	uint32_t       bit_count;
	uint8_t       *dst;
	uint32_t       dst_max;
	uint32_t       dst_pos;
	uint32_t       next_progress_kib;
	int            error;
} inflate_ctx_t;

/*
 * Keep the inflate state off the early stack. The current failure window is
 * in compiler-generated stack setup for the local inflate_ctx_t inside
 * gunzip(), so use a static, explicitly aligned context for validation.
 */
static inflate_ctx_t s_gunzip_ic __attribute__((aligned(16)));

static void inflate_report_progress(inflate_ctx_t *ic)
{
	uint32_t kib = ic->dst_pos >> 10;
	const uint32_t step_kib = 256U;

	if (kib < ic->next_progress_kib) { return; }

	uart_puts_all("[mk] inflate: ");
	uart_puthex64_all((uint64_t) kib);
	uart_puts_all(" KiB out src=0x");
	uart_puthex64_all((uint64_t) ic->src_pos);
	uart_puts_all("\r\n");
	ic->next_progress_kib += step_kib;
}

static void inflate_trace_state(const char *tag, inflate_ctx_t *ic);

static uint32_t br_read(inflate_ctx_t *ic, uint32_t n)
{
	uint32_t v;

	while (ic->bit_count < n) {
		if (ic->src_pos >= ic->src_len) {
			ic->error = 1;
			inflate_trace_state("src overrun", ic);
			return 0U;
		}
		ic->bits |= (uint32_t) ic->src[ic->src_pos++] << ic->bit_count;
		ic->bit_count += 8U;
	}
	v = ic->bits & ((1U << n) - 1U);
	ic->bits >>= n;
	ic->bit_count -= n;
	return v;
}

static void br_byte_align(inflate_ctx_t *ic)
{
	uint32_t skip = ic->bit_count & 7U;

	ic->bits     >>= skip;
	ic->bit_count -= skip;
}

static void ic_emit(inflate_ctx_t *ic, uint8_t b)
{
	if (ic->dst_pos >= ic->dst_max) { ic->error = 1; return; }
	ic->dst[ic->dst_pos++] = b;
	inflate_report_progress(ic);
}

static void ic_back_ref(inflate_ctx_t *ic, uint32_t dist, uint32_t len)
{
	uint32_t i;

	if (dist == 0U || dist > ic->dst_pos) { ic->error = 1; return; }
	for (i = 0U; i < len; i++) {
		if ((i & 0xFFU) == 0U) { pet_wdt(); }
		uint8_t b = ic->dst[ic->dst_pos - dist];
		ic_emit(ic, b);
	}
}

/* Canonical Huffman table. */
typedef struct {
	uint16_t count[16];
	uint16_t sym[288];
} huff_t;

static uint8_t s_inflate_fixed_ll[288] __attribute__((aligned(16)));
static uint8_t s_inflate_fixed_dl[30] __attribute__((aligned(16)));
static huff_t s_inflate_fixed_lt __attribute__((aligned(16)));
static huff_t s_inflate_fixed_dt __attribute__((aligned(16)));
static uint8_t s_inflate_dynamic_cl_lengths[19] __attribute__((aligned(16)));
static huff_t s_inflate_dynamic_cl_tree __attribute__((aligned(16)));
static uint8_t s_inflate_dynamic_lengths[320] __attribute__((aligned(16)));
static huff_t s_inflate_dynamic_lt __attribute__((aligned(16)));
static huff_t s_inflate_dynamic_dt __attribute__((aligned(16)));

static int inflate_is_suspect_dynamic(uint32_t hlit, uint32_t hdist,
				       uint32_t hclen)
{
	return hlit == 0x105U && hdist == 0x1eU && hclen == 0x12U;
}

static void inflate_dump_cl_lengths(const uint8_t *cl_lengths)
{
	uint32_t i;

	for (i = 0U; i < 19U; i++) {
		uart_puts_all("[mk] inflate: suspect cl[0x");
		uart_puthex64_all((uint64_t) i);
		uart_puts_all("]=0x");
		uart_puthex64_all((uint64_t) cl_lengths[i]);
		uart_puts_all("\r\n");
	}
}

static void inflate_trace_suspect_iter(uint32_t iter_count, uint32_t filled_before,
					 int sym, uint32_t rep,
					 uint32_t filled_after,
					 inflate_ctx_t *ic)
{
	uart_puts_all("[mk] inflate: suspect iter=0x");
	uart_puthex64_all((uint64_t) iter_count);
	uart_puts_all(" before=0x");
	uart_puthex64_all((uint64_t) filled_before);
	uart_puts_all(" sym=0x");
	uart_puthex64_all((uint64_t) (uint32_t) sym);
	uart_puts_all(" rep=0x");
	uart_puthex64_all((uint64_t) rep);
	uart_puts_all(" after=0x");
	uart_puthex64_all((uint64_t) filled_after);
	uart_puts_all(" src=0x");
	uart_puthex64_all((uint64_t) ic->src_pos);
	uart_puts_all(" bits=0x");
	uart_puthex64_all((uint64_t) ic->bit_count);
	uart_puts_all("\r\n");
}

static void inflate_trace_suspect_entry(uint32_t filled, inflate_ctx_t *ic)
{
	uart_puts_all("[mk] inflate: suspect entry filled=0x");
	uart_puthex64_all((uint64_t) filled);
	uart_puts_all(" src=0x");
	uart_puthex64_all((uint64_t) ic->src_pos);
	uart_puts_all(" bits=0x");
	uart_puthex64_all((uint64_t) ic->bit_count);
	uart_puts_all("\r\n");
}

static void inflate_trace_suspect_decoded(uint32_t filled, int sym,
					       inflate_ctx_t *ic)
{
	uart_puts_all("[mk] inflate: suspect decoded filled=0x");
	uart_puthex64_all((uint64_t) filled);
	uart_puts_all(" sym=0x");
	uart_puthex64_all((uint64_t) (uint32_t) sym);
	uart_puts_all(" src=0x");
	uart_puthex64_all((uint64_t) ic->src_pos);
	uart_puts_all(" bits=0x");
	uart_puthex64_all((uint64_t) ic->bit_count);
	uart_puts_all("\r\n");
}

static void inflate_trace_state(const char *tag, inflate_ctx_t *ic)
{
	uart_puts_all("[mk] inflate: ");
	uart_puts_all(tag);
	uart_puts_all(" src=0x");
	uart_puthex64_all((uint64_t) ic->src_pos);
	uart_puts_all(" dst=0x");
	uart_puthex64_all((uint64_t) ic->dst_pos);
	uart_puts_all(" bits=0x");
	uart_puthex64_all((uint64_t) ic->bit_count);
	uart_puts_all(" err=0x");
	uart_puthex64_all((uint64_t) ic->error);
	uart_puts_all("\r\n");
}

static void huff_build(huff_t *t, const uint8_t *lengths, uint32_t n,
		       uint32_t sym_max)
{
	uint32_t i;
	uint32_t offs[16];

	for (i = 0U; i < 16U; i++) { t->count[i] = 0U; }
	for (i = 0U; i < n; i++) {
		if (lengths[i] != 0U && lengths[i] <= 15U) {
			t->count[lengths[i]]++;
		}
	}
	offs[0] = 0U;
	for (i = 1U; i < 16U; i++) {
		offs[i] = offs[i - 1U] + t->count[i - 1U];
	}
	for (i = 0U; i < n; i++) {
		uint8_t l = lengths[i];

		if (l != 0U && l <= 15U && offs[l] < sym_max) {
			t->sym[offs[l]++] = (uint16_t) i;
		}
	}
}

static int huff_validate(const huff_t *t, const char *tag, int strict_incomplete)
{
	int left = 1;
	uint32_t len;
	uint32_t used = 0U;

	for (len = 1U; len < 16U; len++) {
		left = (left << 1) - (int) t->count[len];
		used += (uint32_t) t->count[len];
		if (left < 0) {
			uart_puts_all("[mk] inflate: ");
			uart_puts_all(tag);
			uart_puts_all(" oversubscribed\r\n");
			return -1;
		}
	}

	if (used == 0U) {
		uart_puts_all("[mk] inflate: ");
		uart_puts_all(tag);
		uart_puts_all(" empty\r\n");
		return -1;
	}

	if (left > 0) {
		uart_puts_all("[mk] inflate: ");
		uart_puts_all(tag);
		uart_puts_all(" incomplete left=0x");
		uart_puthex64_all((uint64_t) (uint32_t) left);
		uart_puts_all("\r\n");
		if (strict_incomplete) { return -1; }
	}

	return 0;
}

/* Decode one symbol using canonical Huffman (LSB-first bit stream). */
static int huff_decode(inflate_ctx_t *ic, const huff_t *t)
{
	int cur  = 0;
	int base = 0;
	int offs = 0;
	int len;

	for (len = 1; len <= 15; len++) {
		cur = (cur << 1) | (int) br_read(ic, 1U);
		if (ic->error) { return -1; }
		base <<= 1;
		if ((int) t->count[len] > 0) {
			if (cur - base < (int) t->count[len]) {
				return (int) t->sym[offs + (cur - base)];
			}
			offs += (int) t->count[len];
			base += (int) t->count[len];
		}
	}
	ic->error = 1;
	inflate_trace_state("huff decode invalid", ic);
	return -1;
}

/* RFC 1951 length and distance tables. */
static const uint8_t len_extra[29] = {
	0,0,0,0,0,0,0,0, 1,1,1,1, 2,2,2,2, 3,3,3,3, 4,4,4,4, 5,5,5,5, 0
};
static const uint16_t len_base[29] = {
	3,4,5,6,7,8,9,10, 11,13,15,17, 19,23,27,31,
	35,43,51,59, 67,83,99,115, 131,163,195,227, 258
};
static const uint8_t dist_extra[30] = {
	0,0,0,0, 1,1, 2,2, 3,3, 4,4, 5,5, 6,6, 7,7,
	8,8, 9,9, 10,10, 11,11, 12,12, 13,13
};
static const uint32_t dist_base[30] = {
	1,2,3,4, 5,7, 9,13, 17,25, 33,49, 65,97, 129,193,
	257,385, 513,769, 1025,1537, 2049,3073, 4097,6145,
	8193,12289, 16385,24577
};

static void inflate_huff_block(inflate_ctx_t *ic, const huff_t *lt,
				const huff_t *dt)
{
	while (!ic->error) {
		pet_wdt();
		int sym = huff_decode(ic, lt);

		if (sym < 0) {
			inflate_trace_state("literal decode failed", ic);
			return;
		}
		if (sym < 256) {
			ic_emit(ic, (uint8_t) sym);
		} else if (sym == 256) {
			return;
		} else if (sym <= 285) {
			uint32_t li  = (uint32_t) sym - 257U;
			uint32_t len = (uint32_t) len_base[li] + br_read(ic, len_extra[li]);
			int ds       = huff_decode(ic, dt);

			if (ds < 0 || ds >= 30) {
				ic->error = 1;
				inflate_trace_state("distance decode failed", ic);
				return;
			}
			{
				uint32_t di   = (uint32_t) ds;
				uint32_t dist = dist_base[di] + br_read(ic, dist_extra[di]);

				ic_back_ref(ic, dist, len);
			}
		} else {
			ic->error = 1;
		}
	}
}

static void inflate_fixed(inflate_ctx_t *ic)
{
	uint8_t *ll = s_inflate_fixed_ll;
	uint8_t *dl = s_inflate_fixed_dl;
	huff_t *lt = &s_inflate_fixed_lt;
	huff_t *dt = &s_inflate_fixed_dt;
	uint32_t i;

	for (i = 0U;   i <= 143U; i++) { ll[i] = 8U; }
	for (i = 144U; i <= 255U; i++) { ll[i] = 9U; }
	for (i = 256U; i <= 279U; i++) { ll[i] = 7U; }
	for (i = 280U; i <= 287U; i++) { ll[i] = 8U; }
	for (i = 0U; i < 30U; i++) { dl[i] = 5U; }

	huff_build(lt, ll, 288U, 288U);
	huff_build(dt, dl, 30U, 30U);
	inflate_huff_block(ic, lt, dt);
}

static void inflate_dynamic(inflate_ctx_t *ic)
{
	uint32_t hlit  = br_read(ic, 5U) + 257U;
	uint32_t hdist = br_read(ic, 5U) + 1U;
	uint32_t hclen = br_read(ic, 4U) + 4U;
	uint8_t  *cl_lengths = s_inflate_dynamic_cl_lengths;
	huff_t   *cl_tree = &s_inflate_dynamic_cl_tree;
	uint8_t  *lengths = s_inflate_dynamic_lengths;
	uint32_t total = hlit + hdist;
	uint32_t i;
	uint32_t filled;
	uint32_t iter_count = 0U;
	int suspect = inflate_is_suspect_dynamic(hlit, hdist, hclen);
	static const uint8_t cl_order[19] = {
		16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
	};

	uart_puts_all("[mk] inflate: dynamic enter\r\n");
	uart_puts_all("[mk] inflate: dynamic hdr hlit=0x");
	uart_puthex64_all((uint64_t) hlit);
	uart_puts_all(" hdist=0x");
	uart_puthex64_all((uint64_t) hdist);
	uart_puts_all(" hclen=0x");
	uart_puthex64_all((uint64_t) hclen);
	uart_puts_all("\r\n");

	if (ic->error) { return; }
	if (hlit > 286U || hdist > 30U || total > 320U) {
		ic->error = 1;
		return;
	}

	for (i = 0U; i < 19U; i++) { cl_lengths[i] = 0U; }
	for (i = 0U; i < hclen; i++) {
		cl_lengths[cl_order[i]] = (uint8_t) br_read(ic, 3U);
	}
	if (suspect) { inflate_dump_cl_lengths(cl_lengths); }
	huff_build(cl_tree, cl_lengths, 19U, 19U);
	if (huff_validate(cl_tree, "dynamic cl tree", 0) != 0) {
		ic->error = 1;
		inflate_trace_state("dynamic cl tree invalid", ic);
		return;
	}
	uart_puts_all("[mk] inflate: dynamic cl tree built\r\n");

	for (i = 0U; i < 320U; i++) { lengths[i] = 0U; }
	filled = 0U;

	while (filled < total && !ic->error) {
		uint32_t filled_before = filled;
		uint32_t rep = 0U;

		if (++iter_count > 1024U) {
			ic->error = 1;
			uart_puts_all("[mk] inflate: dynamic loop guard filled=0x");
			uart_puthex64_all((uint64_t) filled);
			uart_puts_all(" total=0x");
			uart_puthex64_all((uint64_t) total);
			uart_puts_all("\r\n");
			inflate_trace_state("dynamic loop guard", ic);
			return;
		}
		pet_wdt();
		if (suspect && filled_before >= 0x100U) {
			inflate_trace_suspect_entry(filled_before, ic);
		}
		int sym = huff_decode(ic, cl_tree);

		if (sym < 0) {
			uart_puts_all("[mk] inflate: dynamic code decode failed filled=0x");
			uart_puthex64_all((uint64_t) filled);
			uart_puts_all(" total=0x");
			uart_puthex64_all((uint64_t) total);
			uart_puts_all("\r\n");
			inflate_trace_state("dynamic code decode failed", ic);
			return;
		}
		if (suspect && filled_before >= 0x100U) {
			inflate_trace_suspect_decoded(filled_before, sym, ic);
		}
		if (sym <= 15) {
			lengths[filled++] = (uint8_t) sym;
		} else if (sym == 16) {
			rep = br_read(ic, 2U) + 3U;
			uint8_t  last = (filled > 0U) ? lengths[filled - 1U] : 0U;

			while (rep-- > 0U && filled < total) {
				lengths[filled++] = last;
			}
			rep = filled - filled_before;
		} else if (sym == 17) {
			rep = br_read(ic, 3U) + 3U;

			while (rep-- > 0U && filled < total) { lengths[filled++] = 0U; }
			rep = filled - filled_before;
		} else if (sym == 18) {
			int suspect_terminal = suspect && filled_before >= 0x100U;
			if (suspect) {
				uart_puts_all("[mk] inflate: suspect rep18 pre src=0x");
				uart_puthex64_all((uint64_t) ic->src_pos);
				uart_puts_all(" bits=0x");
				uart_puthex64_all((uint64_t) ic->bit_count);
				uart_puts_all("\r\n");
			}
			rep = br_read(ic, 7U);
			if (suspect) {
				uart_puts_all("[mk] inflate: suspect rep18 extra=0x");
				uart_puthex64_all((uint64_t) rep);
				uart_puts_all(" src=0x");
				uart_puthex64_all((uint64_t) ic->src_pos);
				uart_puts_all(" bits=0x");
				uart_puthex64_all((uint64_t) ic->bit_count);
				uart_puts_all("\r\n");
			}
			rep += 11U;

			while (rep > 0U && filled < total) {
				if (suspect_terminal) {
					uart_puts_all("[mk] inflate: suspect rep18 step pre filled=0x");
					uart_puthex64_all((uint64_t) filled);
					uart_puts_all(" rep=0x");
					uart_puthex64_all((uint64_t) rep);
					uart_puts_all("\r\n");
				}
				lengths[filled++] = 0U;
				rep--;
				if (suspect_terminal) {
					uart_puts_all("[mk] inflate: suspect rep18 step post filled=0x");
					uart_puthex64_all((uint64_t) filled);
					uart_puts_all(" rep=0x");
					uart_puthex64_all((uint64_t) rep);
					uart_puts_all("\r\n");
				}
			}
			if (suspect) {
				uart_puts_all("[mk] inflate: suspect rep18 filldone after=0x");
				uart_puthex64_all((uint64_t) filled);
				uart_puts_all(" src=0x");
				uart_puthex64_all((uint64_t) ic->src_pos);
				uart_puts_all(" bits=0x");
				uart_puthex64_all((uint64_t) ic->bit_count);
				uart_puts_all("\r\n");
			}
			rep = filled - filled_before;
		} else {
			ic->error = 1;
			uart_puts_all("[mk] inflate: dynamic invalid symbol=0x");
			uart_puthex64_all((uint64_t) (uint32_t) sym);
			uart_puts_all("\r\n");
			inflate_trace_state("dynamic invalid symbol", ic);
		}
		if (suspect) {
			inflate_trace_suspect_iter(iter_count, filled_before, sym, rep,
						      filled, ic);
		}
	}
	uart_puts_all("[mk] inflate: dynamic lengths filled\r\n");

	{
		huff_t *lt = &s_inflate_dynamic_lt;
		huff_t *dt = &s_inflate_dynamic_dt;

		huff_build(lt, lengths,        hlit,  288U);
		huff_build(dt, lengths + hlit, hdist, 30U);
		uart_puts_all("[mk] inflate: dynamic lt/dt built\r\n");
		inflate_huff_block(ic, lt, dt);
	}
}

static void inflate_store(inflate_ctx_t *ic)
{
	uint32_t len;
	uint32_t nlen;
	uint32_t i;

	br_byte_align(ic);
	len  = br_read(ic, 16U);
	nlen = br_read(ic, 16U);
	if ((len ^ nlen) != 0xFFFFU) { ic->error = 1; return; }

	for (i = 0U; i < len; i++) {
		ic_emit(ic, (uint8_t) br_read(ic, 8U));
	}
}

static int do_inflate(inflate_ctx_t *ic)
{
	int is_final;

	uart_puts_all("[mk] inflate: do_inflate start\r\n");

	do {
		pet_wdt();
		uint32_t bfinal = br_read(ic, 1U);
		uint32_t btype  = br_read(ic, 2U);

		is_final = (int) bfinal;
		if (ic->error) { return -1; }

		uart_puts_all("[mk] inflate: block bfinal=0x");
		uart_puthex64_all((uint64_t) bfinal);
		uart_puts_all(" btype=0x");
		uart_puthex64_all((uint64_t) btype);
		uart_puts_all("\r\n");

		switch (btype) {
		case 0U: inflate_store(ic);   break;
		case 1U: inflate_fixed(ic);   break;
		case 2U: inflate_dynamic(ic); break;
		default: ic->error = 1;       break;
		}

		if (ic->error) { return -1; }
	} while (!is_final);

	return (int) ic->dst_pos;
}

static int gunzip(const uint8_t *src, uint32_t src_len,
		  uint8_t *dst, uint32_t dst_max)
{
	uint32_t pos = 0U;
	uint8_t flags;
	inflate_ctx_t *ic = &s_gunzip_ic;

	uart_puts_all("[mk] gunzip: enter\r\n");

	if (src_len < 18U ||
	    src[0] != 0x1FU || src[1] != 0x8BU || src[2] != 0x08U) {
		return -1;
	}

	uart_puts_all("[mk] gunzip: hdr ok\r\n");
	flags = src[3];
	uart_puts_all("[mk] gunzip: flags=0x");
	uart_puthex64_all((uint64_t) flags);
	uart_puts_all("\r\n");
	pos   = 10U;

	if ((flags & 0x04U) != 0U) {  /* FEXTRA */
		if (pos + 2U > src_len) { return -1; }
		pos += 2U + ((uint32_t) src[pos] | ((uint32_t) src[pos + 1U] << 8));
	}
	if ((flags & 0x08U) != 0U) {  /* FNAME */
		while (pos < src_len && src[pos] != 0U) { pos++; }
		pos++;
	}
	if ((flags & 0x10U) != 0U) {  /* FCOMMENT */
		while (pos < src_len && src[pos] != 0U) { pos++; }
		pos++;
	}
	if ((flags & 0x02U) != 0U) { pos += 2U; }  /* FHCRC */

	if (pos >= src_len) { return -1; }
	uart_puts_all("[mk] gunzip: payload pos=0x");
	uart_puthex64_all((uint64_t) pos);
	uart_puts_all("\r\n");

	ic->src       = src + pos;
	ic->src_len   = src_len - pos;
	ic->src_pos   = 0U;
	ic->bits      = 0U;
	ic->bit_count = 0U;
	ic->dst       = dst;
	ic->dst_max   = dst_max;
	ic->dst_pos   = 0U;
	ic->next_progress_kib = 256U;
	ic->error     = 0;

	uart_puts_all("[mk] gunzip: before do_inflate\r\n");
	return do_inflate(ic);
}

/* ------------------------------------------------------------------ */
/* FDT /chosen patcher                                                 */
/* ------------------------------------------------------------------ */

static uint32_t fdt_hdr32(const uint8_t *fdt, uint32_t off)
{
	return be32r(fdt + off);
}

static void fdt_hdr32w(uint8_t *fdt, uint32_t off, uint32_t v)
{
	be32w(fdt + off, v);
}

/*
 * Append a null-terminated string to the strings section.
 * Returns its nameoff within the strings section.
 */
static uint32_t fdt_str_append(uint8_t *fdt, const char *s)
{
	uint32_t off_str   = fdt_hdr32(fdt, FDT_OFF_STRINGS);
	uint32_t sz_str    = fdt_hdr32(fdt, FDT_OFF_SIZE_STR);
	uint32_t totalsize = fdt_hdr32(fdt, FDT_OFF_TOTALSIZE);
	uint32_t nameoff   = sz_str;
	uint32_t slen      = mk_strlen(s) + 1U;
	uint32_t i;

	for (i = 0U; i < slen; i++) {
		fdt[off_str + sz_str + i] = (uint8_t) s[i];
	}

	fdt_hdr32w(fdt, FDT_OFF_SIZE_STR, sz_str + slen);
	fdt_hdr32w(fdt, FDT_OFF_TOTALSIZE, totalsize + slen);
	return nameoff;
}

/*
 * Insert 'len' bytes into the struct section at struct-relative byte
 * 'struct_off'. Shifts the entire remainder of the FDT (including the
 * strings section) to make room.
 */
static void fdt_struct_insert(uint8_t *fdt, uint32_t struct_off,
			      const uint8_t *data, uint32_t len)
{
	uint32_t off_struct  = fdt_hdr32(fdt, FDT_OFF_STRUCT);
	uint32_t sz_struct   = fdt_hdr32(fdt, FDT_OFF_SIZE_STRUCT);
	uint32_t off_strings = fdt_hdr32(fdt, FDT_OFF_STRINGS);
	uint32_t totalsize   = fdt_hdr32(fdt, FDT_OFF_TOTALSIZE);
	uint8_t *insert_at   = fdt + off_struct + struct_off;
	/* Move everything from insert point to end of FDT (includes strings). */
	uint32_t bytes_to_move = totalsize - off_struct - struct_off;
	uint32_t rem;

	uart_puts_all("[mk] fdt: insert struct_off=0x");
	uart_puthex64_all((uint64_t) struct_off);
	uart_puts_all(" len=0x");
	uart_puthex64_all((uint64_t) len);
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
		insert_at[len + rem] = insert_at[rem];
		if ((rem & 0xFFFFU) == 0U) {
			pet_wdt();
			uart_puts_all("[mk] fdt: insert rem=0x");
			uart_puthex64_all((uint64_t) rem);
			uart_puts_all("\r\n");
		}
	}
	uart_puts_all("[mk] fdt: insert memmove done\r\n");
	mk_memcpy(insert_at, data, len);
	uart_puts_all("[mk] fdt: insert memcpy done\r\n");

	fdt_hdr32w(fdt, FDT_OFF_SIZE_STRUCT, sz_struct + len);
	/* Strings section moves right if it lives after the struct. */
	if (off_strings > off_struct + struct_off) {
		fdt_hdr32w(fdt, FDT_OFF_STRINGS, off_strings + len);
	}
	fdt_hdr32w(fdt, FDT_OFF_TOTALSIZE, totalsize + len);
	uart_puts_all("[mk] fdt: insert hdr done\r\n");
}

/*
 * Build a FDT_PROP record into buf and return its byte length.
 * buf must be at least 12 + ((val_len + 3) & ~3) bytes.
 */
static uint32_t fdt_build_prop(uint8_t *buf, uint32_t nameoff,
				const uint8_t *value, uint32_t val_len)
{
	uint32_t padded = (val_len + 3U) & ~3U;

	be32w(buf,       FDT_PROP);
	be32w(buf + 4U,  val_len);
	be32w(buf + 8U,  nameoff);
	mk_memcpy(buf + 12U, value, val_len);
	mk_memset(buf + 12U + val_len, 0U, padded - val_len);

	return 12U + padded;
}

/*
 * Find the struct-section byte offset of the FDT_END_NODE that closes
 * /chosen (depth 1). We insert new properties just before this offset.
 * Returns 0 on failure.
 */
static uint32_t fdt_chosen_end_off(const uint8_t *fdt)
{
	uint32_t off_struct = fdt_hdr32(fdt, FDT_OFF_STRUCT);
	uint32_t sz_struct  = fdt_hdr32(fdt, FDT_OFF_SIZE_STRUCT);
	const uint8_t *p    = fdt + off_struct;
	const uint8_t *end  = p + sz_struct;
	int depth    = -1;
	int in_chose = 0;

	while (p + 4U <= end) {
		uint32_t token   = be32r(p);

		p += 4U;

		if (token == FDT_BEGIN_NODE) {
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

		} else if (token == FDT_END_NODE) {
			uint32_t cur_off = (uint32_t) ((p - 4U) - (fdt + off_struct));
			if (in_chose && depth == 1) {
				/* Found it: insert BEFORE this token. */
				return cur_off;
			}
			if (depth >= 0) { depth--; }
			if (depth < 1)  { in_chose = 0; }

		} else if (token == FDT_PROP) {
			uint32_t val_len;
			uint32_t padded;

			if (p + 8U > end) { break; }
			val_len = be32r(p);
			padded  = (val_len + 3U) & ~3U;
			p += 8U + padded;

		} else if (token == FDT_NOP) {
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
static uint32_t fdt_node_end_off(const uint8_t *fdt, const char *nodename)
{
	uint32_t off_struct = fdt_hdr32(fdt, FDT_OFF_STRUCT);
	uint32_t sz_struct  = fdt_hdr32(fdt, FDT_OFF_SIZE_STRUCT);
	const uint8_t *base = fdt + off_struct;
	const uint8_t *p    = base;
	const uint8_t *end  = p + sz_struct;
	int depth        = -1;
	int target_depth = -1;

	while (p + 4U <= end) {
		uint32_t token = be32r(p);

		p += 4U;

		if (token == FDT_BEGIN_NODE) {
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

		} else if (token == FDT_END_NODE) {
			if (target_depth >= 0 && depth == target_depth) {
				return (uint32_t) ((p - 4U) - base);
			}
			if (depth >= 0) { depth--; }

		} else if (token == FDT_PROP) {
			uint32_t val_len;
			uint32_t padded;

			if (p + 8U > end) { break; }
			val_len = be32r(p);
			padded  = (val_len + 3U) & ~3U;
			p += 8U + padded;

		} else if (token == FDT_NOP) {
			/* nothing */
		} else {
			break;
		}
	}

	return 0U;
}

static int fdt_name_eq(const char *a, const char *b)
{
	while (*a != '\0' && *b != '\0') {
		if (*a != *b) { return 0; }
		a++;
		b++;
	}
	return (*a == '\0' && *b == '\0') ? 1 : 0;
}

static int fdt_chosen_find_prop(const uint8_t *fdt, const char *name,
				uint32_t *val_off, uint32_t *val_len)
{
	uint32_t off_struct  = fdt_hdr32(fdt, FDT_OFF_STRUCT);
	uint32_t off_strings = fdt_hdr32(fdt, FDT_OFF_STRINGS);
	uint32_t sz_struct   = fdt_hdr32(fdt, FDT_OFF_SIZE_STRUCT);
	const uint8_t *p     = fdt + off_struct;
	const uint8_t *end   = p + sz_struct;
	int depth     = -1;
	int in_chose  = 0;

	while (p + 4U <= end) {
		uint32_t token = be32r(p);

		p += 4U;

		if (token == FDT_BEGIN_NODE) {
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

		} else if (token == FDT_END_NODE) {
			if (depth >= 0) { depth--; }
			if (depth < 1) { in_chose = 0; }

		} else if (token == FDT_PROP) {
			uint32_t plen;
			uint32_t nameoff;
			uint32_t padded;
			const char *prop_name;

			if (p + 8U > end) { break; }
			plen = be32r(p);
			nameoff = be32r(p + 4U);
			padded = (plen + 3U) & ~3U;
			prop_name = (const char *) (fdt + off_strings + nameoff);
			if (in_chose && depth == 1 && fdt_name_eq(prop_name, name)) {
				*val_off = (uint32_t) ((p + 8U) - fdt);
				*val_len = plen;
				return 1;
			}
			p += 8U + padded;

		} else if (token == FDT_NOP) {
			/* nothing */
		} else {
			break;
		}
	}

	return 0;
}

/*
 * Large scratch buffer for FDT property records.
 * Placed in BSS so it doesn't consume stack.
 */
static uint8_t s_prop_buf[2100];
static char    s_bootargs_copy[2048];
static char    s_bootargs_effective[2048] __attribute__((unused));

static const char s_diag_bootargs[] =
	"console=tty0 "
	"console=ttyS0,921600n1 "
	"earlycon=uart8250,mmio32,0x11002000,921600n1 "
	"keep_bootcon "
	"loglevel=4 "
	"vmalloc=400M "
	"page_owner=on "
	"swiotlb=noforce "
	"androidboot.hardware=mt6765 "
	"maxcpus=8 "
	"loop.max_part=7 "
	"firmware_class.path=/vendor/firmware "
	"bootopt=64S3,32N2,64N2 "
	"buildvariant=user "
	"disable_uart=0 "
	"force_uart=1 "
	"mtk_printk_ctrl.disable_uart=0 "
	"printk.force_uart=1";

/*
 * Search bootargs for key=value token starting with prefix.
 * Returns pointer to start of the match, or 0 if not found.
 */
static const char *
find_bootarg(const char *bootargs, const char *prefix, uint32_t prefix_len)
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

/*
 * Append specific key=value tokens from the LK bootargs to dst.
 * Only passes through keys critical for hardware detection.
 */
static uint32_t
append_lk_passthrough(char *dst, uint32_t pos, uint32_t dst_max)
{
	(void) dst_max;
	/* Passthrough temporarily disabled to isolate boot crash. */

	uart_puts_all("[mk] lk passthrough: final pos=0x");
	uart_puthex64_all((uint64_t) pos);
	uart_puts_all("\r\n");

	dst[pos] = '\0';
	return pos;
}

static void __attribute__((unused))
build_effective_bootargs(char *dst, uint32_t dst_max, const char *base)
{
	uint32_t i;
	uint32_t pos;

	if (dst_max == 0U) {
		return;
	}

	pos = 0U;
	dst[0] = '\0';

	if (base != 0 && base[0] != '\0') {
		uart_puts_all("[mk] bootargs: base from extlinux len=0x");
		uart_puthex64_all((uint64_t) mk_strlen(base));
		uart_puts_all("\r\n");
		for (i = 0U; base[i] != '\0' && pos + 1U < dst_max; ++i) {
			dst[pos++] = base[i];
		}
	}

	/*
	 * If the extlinux append line was empty, copy the full LK bootargs
	 * as base.  LK bootargs carry essential kernel parameters like
	 * vmalloc=, maxcpus=, swiotlb=, console=, etc.  Without them the
	 * kernel crashes early (corrupt timers, RCU stalls).
	 */
	if (pos == 0U) {
		const char *lk = mk_stage0_mtk_usb_get_lk_bootargs();

		uart_puts_all("[mk] bootargs: LK fallback\r\n");
		if (lk != 0) {
			for (i = 0U; lk[i] != '\0' && pos + 1U < dst_max;
			     ++i) {
				dst[pos++] = lk[i];
			}
			uart_puts_all("[mk] bootargs: LK base len=0x");
			uart_puthex64_all((uint64_t) pos);
			uart_puts_all("\r\n");
		}
	}

	while (pos > 0U && dst[pos - 1U] == ' ') {
		--pos;
	}

	if (pos > 0U && pos + 1U < dst_max) {
		dst[pos++] = ' ';
	}

	for (i = 0U; s_diag_bootargs[i] != '\0' && pos + 1U < dst_max; ++i) {
		dst[pos++] = s_diag_bootargs[i];
	}

	dst[pos] = '\0';

	pos = append_lk_passthrough(dst, pos, dst_max);

	uart_puts_all("[mk] bootargs: built len=0x");
	uart_puthex64_all((uint64_t) pos);
	uart_puts_all("\r\n");
}

static uint32_t parse_decimal_u32(const char *s)
{
	uint32_t val = 0U;

	while (*s >= '0' && *s <= '9') {
		val = val * 10U + (uint32_t) (*s - '0');
		s++;
	}
	return val;
}

/*
 * Populate the oplus_project DT node with hardware identity data
 * extracted from LK bootargs.  The node already exists in the DTB
 * (from the kernel DTS) but is empty — LK normally fills it at
 * runtime.  Since MK replaces LK, we do it here.
 */
static void
fdt_patch_oplus_project(uint8_t *fdt)
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

	match = find_bootarg(lk, "androidboot.prjname=", 20U);
	if (match == 0) {
		uart_puts_all("[mk] fdt: oplus_project: no prjname\r\n");
		return;
	}

	project_no = parse_decimal_u32(match + 20U);
	if (project_no == 0U) {
		uart_puts_all("[mk] fdt: oplus_project: prjname=0\r\n");
		return;
	}

	noff = fdt_node_end_off(fdt, "oplus_project");
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
	no_newcdt   = fdt_str_append(fdt, "newcdt");
	no_nVersion = fdt_str_append(fdt, "nVersion");
	no_nProject = fdt_str_append(fdt, "nProject");
	no_nDtsi    = fdt_str_append(fdt, "nDtsi");
	no_nAudio   = fdt_str_append(fdt, "nAudio");
	no_nRF      = fdt_str_append(fdt, "nRF");
	no_nPCB     = fdt_str_append(fdt, "nPCB");
	no_eng      = fdt_str_append(fdt, "eng_version");
	no_conf     = fdt_str_append(fdt, "is_confidential");

	/*
	 * Insert properties in reverse desired order.  Each insert at
	 * 'noff' pushes the previous ones right, so the final layout
	 * (reading forwards) matches insertion order reversed.
	 */
#define OP_INS_U32(nameoff, v) do {		\
	be32w(val, (v));			\
	plen = fdt_build_prop(s_prop_buf, (nameoff), val, 4U); \
	fdt_struct_insert(fdt, noff, s_prop_buf, plen); \
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

/*
 * Copy src_fdt to dst, then add /chosen properties for the Linux boot:
 *   bootargs       = append line from extlinux.conf
 *   linux,initrd-start / linux,initrd-end
 *
 * Returns 1 on success, 0 on failure.
 * dst must have MK_FDT_MAX_SIZE bytes available.
 */
static __attribute__((unused)) int
fdt_patch(const uint8_t *src_fdt, uint8_t *dst,
	  const char *bootargs,
	  uint64_t initrd_start, uint64_t initrd_end)
{
	uint32_t magic;
	uint32_t totalsize;
	uint32_t coff;
	uint32_t no_ba;
	uint32_t no_is;
	uint32_t no_ie;
	uint8_t  val[8];
	uint32_t plen;
	uint32_t ba_len;
	uint32_t prop_off;
	uint32_t prop_len;
	uint32_t i;
	uint32_t padded;
	int have_is;
	int have_ie;

	uart_puts_all("[mk] fdt: enter src=0x");
	uart_puthex64_all((uint64_t) (uintptr_t) src_fdt);
	uart_puts_all(" dst=0x");
	uart_puthex64_all((uint64_t) (uintptr_t) dst);
	uart_puts_all("\r\n");

	magic = be32r(src_fdt);
	uart_puts_all("[mk] fdt: magic=0x");
	uart_puthex64_all((uint64_t) magic);
	uart_puts_all("\r\n");
	if (magic != FDT_MAGIC_VAL) { return 0; }

	totalsize = be32r(src_fdt + FDT_OFF_TOTALSIZE);
	uart_puts_all("[mk] fdt: totalsize=0x");
	uart_puthex64_all((uint64_t) totalsize);
	uart_puts_all("\r\n");
	if (totalsize + 1024U > MK_FDT_MAX_SIZE) { return 0; }

	mk_memcpy(dst, src_fdt, totalsize);
	mk_memset(dst + totalsize, 0U, 1024U);
	uart_puts_all("[mk] fdt: copy done\r\n");

	coff = fdt_chosen_end_off(dst);
	uart_puts_all("[mk] fdt: chosen end off=0x");
	uart_puthex64_all((uint64_t) coff);
	uart_puts_all("\r\n");
	if (coff == 0U) { return 0; }

	/*
	 * NOP out the existing bootargs property so the kernel doesn't
	 * find it before our newly inserted one.
	 * fdt_chosen_find_prop returns val_off pointing 8 bytes past
	 * the FDT_PROP token, so the prop record starts at val_off - 12.
	 * Total record: 4(token) + 4(len) + 4(nameoff) + padded(value).
	 */
	if (fdt_chosen_find_prop(dst, "bootargs", &prop_off, &prop_len)) {
		uint32_t old_start = prop_off - 12U;
		uint32_t old_padded = (prop_len + 3U) & ~3U;
		uint32_t old_total = 12U + old_padded;
		uint32_t nop_i;

		uart_puts_all("[mk] fdt: NOP old bootargs off=0x");
		uart_puthex64_all((uint64_t) old_start);
		uart_puts_all(" len=0x");
		uart_puthex64_all((uint64_t) old_total);
		uart_puts_all("\r\n");
		for (nop_i = 0U; nop_i < old_total; nop_i += 4U) {
			be32w(dst + old_start + nop_i, FDT_NOP);
		}
	}

	have_ie = fdt_chosen_find_prop(dst, "linux,initrd-end", &prop_off, &prop_len);
	if (have_ie && prop_len == 8U) {
		be64w(dst + prop_off, initrd_end);
		uart_puts_all("[mk] fdt: overwrote initrd-end64\r\n");
	} else if (have_ie && prop_len == 4U) {
		/* 32-bit is fine — of_read_number handles len/4 cells.
		 * All our initrd addresses are below 4GB. */
		be32w(dst + prop_off, (uint32_t) initrd_end);
		uart_puts_all("[mk] fdt: overwrote initrd-end32\r\n");
	} else {
		have_ie = 0;
	}
	have_is = fdt_chosen_find_prop(dst, "linux,initrd-start", &prop_off, &prop_len);
	if (have_is && prop_len == 8U) {
		be64w(dst + prop_off, initrd_start);
		uart_puts_all("[mk] fdt: overwrote initrd-start64\r\n");
	} else if (have_is && prop_len == 4U) {
		be32w(dst + prop_off, (uint32_t) initrd_start);
		uart_puts_all("[mk] fdt: overwrote initrd-start32\r\n");
	} else {
		have_is = 0;
	}

	/*
	 * Append property name strings BEFORE inserting struct records.
	 * fdt_struct_insert will shift the strings section rightward,
	 * so the nameoffs remain valid.
	 */
	no_ba = fdt_str_append(dst, "bootargs");
	no_is = fdt_str_append(dst, "linux,initrd-start");
	no_ie = fdt_str_append(dst, "linux,initrd-end");
	uart_puts_all("[mk] fdt: nameoff bootargs=0x");
	uart_puthex64_all((uint64_t) no_ba);
	uart_puts_all(" initrd-start=0x");
	uart_puthex64_all((uint64_t) no_is);
	uart_puts_all(" initrd-end=0x");
	uart_puthex64_all((uint64_t) no_ie);
	uart_puts_all("\r\n");

	ba_len = mk_strnlen(bootargs, sizeof(s_bootargs_copy) - 1U);
	for (i = 0U; i < ba_len; i++) {
		s_bootargs_copy[i] = bootargs[i];
	}
	s_bootargs_copy[ba_len] = '\0';
	uart_puts_all("[mk] fdt: bootargs ptr=0x");
	uart_puthex64_all((uint64_t) (uintptr_t) bootargs);
	uart_puts_all(" len=0x");
	uart_puthex64_all((uint64_t) ba_len);
	uart_puts_all("\r\n");

	/*
	 * Insert properties at 'coff' in reverse desired order.
	 * Each insert pushes the previous content right, so the final
	 * order (reading left-to-right in the struct section) will be:
	 *   bootargs → initrd-start → initrd-end → FDT_END_NODE
	 */

	if (!have_ie) {
		/* arm64 expects 64-bit initrd address cells in /chosen. */
		be64w(val, initrd_end);
		plen = fdt_build_prop(s_prop_buf, no_ie, val, 8U);
		uart_puts_all("[mk] fdt: build initrd-end plen=0x");
		uart_puthex64_all((uint64_t) plen);
		uart_puts_all("\r\n");
		fdt_struct_insert(dst, coff, s_prop_buf, plen);
		uart_puts_all("[mk] fdt: inserted initrd-end\r\n");
	}

	/* 2. initrd-start (inserted at same coff, goes before initrd-end). */
	if (!have_is) {
		be64w(val, initrd_start);
		plen = fdt_build_prop(s_prop_buf, no_is, val, 8U);
		uart_puts_all("[mk] fdt: build initrd-start plen=0x");
		uart_puthex64_all((uint64_t) plen);
		uart_puts_all("\r\n");
		fdt_struct_insert(dst, coff, s_prop_buf, plen);
		uart_puts_all("[mk] fdt: inserted initrd-start\r\n");
	}

	/* 3. bootargs (inserted at same coff, goes before initrd-start). */
	ba_len = mk_strlen(s_bootargs_copy) + 1U;
	if (ba_len > sizeof(s_prop_buf) - 12U - 3U) {
		ba_len = sizeof(s_prop_buf) - 12U - 4U;
	}
	uart_puts_all("[mk] fdt: build bootargs begin len=0x");
	uart_puthex64_all((uint64_t) ba_len);
	uart_puts_all("\r\n");
	padded = (ba_len + 3U) & ~3U;
	be32w(s_prop_buf, FDT_PROP);
	uart_puts_all("[mk] fdt: build bootargs hdr0\r\n");
	be32w(s_prop_buf + 4U, ba_len);
	uart_puts_all("[mk] fdt: build bootargs hdr1\r\n");
	be32w(s_prop_buf + 8U, no_ba);
	uart_puts_all("[mk] fdt: build bootargs hdr2\r\n");
	for (i = 0U; i < ba_len; i++) {
		s_prop_buf[12U + i] = (uint8_t) s_bootargs_copy[i];
	}
	uart_puts_all("[mk] fdt: build bootargs copy done\r\n");
	while (i < padded) {
		s_prop_buf[12U + i] = 0U;
		i++;
	}
	uart_puts_all("[mk] fdt: build bootargs pad done\r\n");
	plen = 12U + padded;
	uart_puts_all("[mk] fdt: build bootargs plen=0x");
	uart_puthex64_all((uint64_t) plen);
	uart_puts_all(" ba_len=0x");
	uart_puthex64_all((uint64_t) ba_len);
	uart_puts_all("\r\n");
	fdt_struct_insert(dst, coff, s_prop_buf, plen);
	uart_puts_all("[mk] fdt: inserted bootargs\r\n");

	(void) no_is;
	(void) no_ie;
	uart_puts_all("[mk] fdt: chosen ok\r\n");

	/* Populate /odm/oplus_project with LK hardware identity data. */
	fdt_patch_oplus_project(dst);

	uart_puts_all("[mk] fdt: patch ok\r\n");

	return 1;
}

/* ------------------------------------------------------------------ */
/* D-cache flush + MMU disable + arm64 kernel jump                    */
/* ------------------------------------------------------------------ */

static void flush_dcache_range(uint64_t start, uint64_t end_addr)
{
	uint64_t ctr;
	uint64_t line_size;
	uint64_t addr;

	__asm__ volatile("mrs %0, CTR_EL0" : "=r"(ctr));
	line_size = 4UL << ((ctr >> 16) & 0xFUL);
	start    &= ~(line_size - 1UL);

	for (addr = start; addr < end_addr; addr += line_size) {
		__asm__ volatile("dc civac, %0" :: "r"(addr) : "memory");
	}
	__asm__ volatile("dsb sy" ::: "memory");
	__asm__ volatile("isb"    ::: "memory");
}

#define MK_HANDOFF_KEEP_MMU_DIAG 1U

static void handoff_dump_reg(const char *name, uint64_t value)
{
	uart_puts_all("[mk] handoff: ");
	uart_puts_all(name);
	uart_puts_all("=0x");
	uart_puthex64_all(value);
	uart_puts_all("\r\n");
}

static uint64_t handoff_entry_branch_target(uint64_t entry, uint32_t word1)
{
	int64_t imm26 = (int64_t) (word1 & 0x03ffffffU);

	if ((imm26 & 0x02000000LL) != 0LL) {
		imm26 |= ~0x03ffffffLL;
	}

	return (uint64_t) ((int64_t) entry + 4LL + (imm26 << 2));
}

static void handoff_dump_entry_bytes(uint64_t entry)
{
	const uint8_t *p = (const uint8_t *) (uintptr_t) entry;
	uint32_t word1 = le32r(p + 4U);
	uint64_t branch_target = handoff_entry_branch_target(entry, word1);
	const uint8_t *bt = (const uint8_t *) (uintptr_t) branch_target;

	trace_first16("[mk] handoff: live entry [0..15]", p);
	trace_first16("[mk] handoff: live entry [16..31]", p + 16U);
	handoff_dump_reg("entry_word0", (uint64_t) le32r(p));
	handoff_dump_reg("entry_word1", (uint64_t) word1);
	handoff_dump_reg("entry_branch_target", branch_target);
	trace_first16("[mk] handoff: live target [0..15]", bt);
	trace_first16("[mk] handoff: live target [16..31]", bt + 16U);
}

static void mk_uart_quiesce_for_linux(void)
{
	uint64_t timeout;
	uint32_t lsr;

	for (timeout = 0U; timeout < 1000000U; timeout++) {
		lsr = mk_mmio_read32(MK_UART0_BASE_ADDR + MK_UART_LSR_OFF);
		if ((lsr & (MK_UART_LSR_THRE | MK_UART_LSR_TEMT)) ==
		    (MK_UART_LSR_THRE | MK_UART_LSR_TEMT)) {
			break;
		}
		if ((timeout & 0x3ffU) == 0U) {
			pet_wdt();
		}
	}

	/* Stop interrupts and vendor side channels before Linux probes UART0. */
	mk_mmio_write32(MK_UART0_BASE_ADDR + MK_UART_IER_DLH_OFF, 0U);
	mk_mmio_write32(MK_UART0_BASE_ADDR + MK_UART_DMA_EN_OFF, 0U);
	mk_mmio_write32(MK_UART0_BASE_ADDR + MK_UART_SLEEP_EN_OFF, 0U);
	mk_mmio_write32(MK_UART0_BASE_ADDR + MK_UART_ESCAPE_EN_OFF, 0U);
	mk_mmio_write32(MK_UART0_BASE_ADDR + MK_UART_IIR_FCR_OFF,
			MK_UART_FCR_FIFO_EN | MK_UART_FCR_CLEAR_RX | MK_UART_FCR_CLEAR_TX);
	__asm__ volatile("dsb sy" ::: "memory");
	__asm__ volatile("isb" ::: "memory");
}

static void mk_restore_el2_state_for_linux(void)
{
	if (__mk_saved_el2_valid == 0U) {
		uart_puts_all("[mk] handoff: el2 restore skipped\r\n");
		return;
	}

	__asm__ volatile("msr cptr_el2, %0" :: "r"(__mk_saved_cptr_el2) : "memory");
	__asm__ volatile("msr vbar_el2, %0" :: "r"(__mk_saved_vbar_el2) : "memory");
	__asm__ volatile("msr sctlr_el2, %0" :: "r"(__mk_saved_sctlr_el2) : "memory");
	__asm__ volatile("isb" ::: "memory");

	uart_puts_all("[mk] handoff: el2 restore done cptr=0x");
	uart_puthex64_all(__mk_saved_cptr_el2);
	uart_puts_all(" vbar=0x");
	uart_puthex64_all(__mk_saved_vbar_el2);
	uart_puts_all("\r\n");
}

static void __attribute__((noreturn))
jump_to_kernel(uint64_t entry, uint64_t fdt_pa)
{
	uint64_t current_el;
	uint64_t sctlr = 0U;
	uint64_t hcr = 0U;

	__asm__ volatile("mrs %0, CurrentEL" : "=r"(current_el));
	handoff_dump_reg("entry", entry);
	handoff_dump_reg("fdt", fdt_pa);
	handoff_dump_reg("CurrentEL_raw", current_el);
	current_el = (current_el >> 2U) & 0x3U;
	handoff_dump_reg("CurrentEL", current_el);

	if (current_el == 2U) {
		uart_puts_all("[mk] linux handoff: EL2 direct\r\n");
		__asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr));
		__asm__ volatile("mrs %0, hcr_el2" : "=r"(hcr));
		handoff_dump_reg("SCTLR_EL2_pre", sctlr);
		handoff_dump_reg("HCR_EL2_pre", hcr);
	} else if (current_el == 1U) {
		uart_puts_all("[mk] linux handoff: EL1 direct\r\n");
		__asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
		handoff_dump_reg("SCTLR_EL1_pre", sctlr);
	} else {
		uart_puts_all("[mk] linux handoff: unsupported EL=0x");
		uart_puthex64_all(current_el);
		uart_puts_all("\r\n");
		for (;;) { }
	}

	uart_puts_all("[mk] handoff: before dsb/isb\r\n");
	__asm__ volatile("dsb sy" ::: "memory");
	__asm__ volatile("isb" ::: "memory");
	uart_puts_all("[mk] handoff: ic iallu begin\r\n");
	__asm__ volatile("ic iallu" ::: "memory");
	__asm__ volatile("dsb nsh" ::: "memory");
	__asm__ volatile("isb" ::: "memory");
	uart_puts_all("[mk] handoff: ic iallu done\r\n");
	uart_puts_all("[mk] handoff: before branch\r\n");
	handoff_dump_entry_bytes(entry);
	uart_puts_all("[mk] handoff: wdt restore\r\n");
	mk_stage0_wdt_restore_for_linux();
	uart_puts_all("[mk] handoff: msdc restore\r\n");
	mk_stage0_msdc_restore_for_linux();
	uart_puts_all("[mk] handoff: display restore\r\n");
	mk_stage0_display_restore_for_linux();
	uart_puts_all("[mk] handoff: usb restore\r\n");
	mk_stage0_mtk_usb_platform_restore_for_linux();
	uart_puts_all("[mk] handoff: i2c restore\r\n");
	mk_stage0_mtk_i2c_restore_for_linux();
	uart_puts_all("[mk] handoff: el2 restore\r\n");
	mk_restore_el2_state_for_linux();
	uart_puts_all("[mk] handoff: uart0 quiesce\r\n");
	mk_uart_quiesce_for_linux();
	uart_puts_all("[mk] handoff: daif mask\r\n");
	__asm__ volatile("msr daifset, #0xf" ::: "memory");
	__asm__ volatile("isb" ::: "memory");
	uart_puts_all("[mk] handoff: mmu off branch\r\n");

	/*
	 * Chainload with the arm64 boot protocol:
	 *   x0 = FDT, x1 = entry, MMU+D-cache off at the current EL.
	 * The kernel's own head.S will drop from EL2 to EL1 when needed.
	 */
	__mk_chainload_raw(fdt_pa, entry);

	__builtin_unreachable();
}

/* ------------------------------------------------------------------ */
/* arm64 Image header constants                                        */
/* ------------------------------------------------------------------ */

#define ARM64_IMAGE_MAGIC     0x644D5241U  /* "ARM\x64" LE */
#define ARM64_HDR_TEXT_OFFSET 8U           /* LE u64 */
#define ARM64_HDR_IMAGE_SIZE  16U          /* LE u64 */
#define ARM64_HDR_MAGIC_OFF   56U          /* LE u32 */

/* ------------------------------------------------------------------ */
/* Static buffers (avoid stack pressure)                               */
/* ------------------------------------------------------------------ */

static mk_ext2_t     s_ext2;
static char          s_conf_buf[4096];
static extlinux_entry_t s_entry;

/* ------------------------------------------------------------------ */
/* Main boot entry                                                     */
/* ------------------------------------------------------------------ */

static void mk_boot_linux_common(uint64_t fdt_ptr, uint64_t boot_lba,
				 const uint8_t *kernel_override,
				 uint32_t kernel_override_size)
{
	uint8_t *gz_stage    = (uint8_t *) (uintptr_t) MK_GZIP_STAGE_ADDR;
	uint8_t *kernel_base = (uint8_t *) (uintptr_t) MK_KERNEL_BASE;
	uint8_t *initrd_buf  = (uint8_t *) (uintptr_t) MK_INITRD_ADDR;
	const uint8_t *kernel_src;
	uint32_t kernel_src_size;
	int      gz_size = 0;
	int      decomp_size;
	int      initrd_size;
	int      conf_size;
	uint64_t text_offset;
	uint64_t img_size;
	uint32_t img_magic;
	uint8_t *kernel_entry;
	uint64_t kernel_end;
	uint64_t kernel_flush_end;
	uint64_t initrd_start;
	uint64_t initrd_end;
	uint64_t handoff_fdt = fdt_ptr;

	uart_puts_all("[mk] boot: start\r\n");

	if (!mk_ext2_open(boot_lba, &s_ext2)) {
		uart_puts_all("[mk] boot: ext2 open failed\r\n");
		return;
	}

	conf_size = mk_ext2_load_file(&s_ext2, "extlinux/extlinux.conf",
				      (uint8_t *) s_conf_buf,
				      sizeof(s_conf_buf) - 1U);
	if (conf_size <= 0) {
		uart_puts_all("[mk] boot: extlinux.conf not found\r\n");
		return;
	}
	s_conf_buf[conf_size] = '\0';

	if (!parse_extlinux(s_conf_buf, mk_strlen(s_conf_buf), &s_entry)) {
		uart_puts_all("[mk] boot: extlinux parse failed\r\n");
		return;
	}

	uart_puts_all("[mk] boot: kernel=");
	if (kernel_override != 0U) {
		uart_puts_all("(staged override)");
	} else {
		uart_puts_all(s_entry.kernel);
	}
	uart_puts_all("\r\n");

	if (kernel_override != 0U) {
		if (kernel_override_size == 0U) {
			uart_puts_all("[mk] boot: staged kernel empty\r\n");
			return;
		}
		uart_puts_all("[mk] boot: staged kernel size=0x");
		uart_puthex64_all((uint64_t) kernel_override_size);
		uart_puts_all("\r\n");
		/*
		 * g_fastboot_download_buf lives at ~0x491B6000, above
		 * kernel_base (0x49000000).  The gzip decompressor writes
		 * output upward from kernel_base; after ~1.7 MB of output it
		 * reaches the source buffer and starts corrupting un-consumed
		 * compressed data, silently producing a garbage kernel image.
		 * Copy the staged payload to gz_stage (0x4C000000) first —
		 * the same address the normal eMMC path uses — so source and
		 * destination regions are fully disjoint.
		 */
		if (kernel_override_size > MK_GZIP_MAX_SIZE) {
			uart_puts_all("[mk] boot: staged kernel too large for gz_stage\r\n");
			return;
		}
		uart_puts_all("[mk] boot: staged copy to gz_stage\r\n");
		mk_memcpy(gz_stage, kernel_override, kernel_override_size);
		kernel_src = gz_stage;
		kernel_src_size = kernel_override_size;
	} else {
		/* Load kernel (Image.gz) into gzip staging area. */
		gz_size = mk_ext2_load_file(&s_ext2, s_entry.kernel,
					    gz_stage, MK_GZIP_MAX_SIZE);
		if (gz_size <= 0) {
			uart_puts_all("[mk] boot: kernel load failed\r\n");
			return;
		}
		kernel_src = gz_stage;
		kernel_src_size = (uint32_t) gz_size;
	}

	uart_puts_all("[mk] boot: kernel gz_size=0x");
	uart_puthex64_all((uint64_t) kernel_src_size);
	uart_puts_all("\r\n");
	uart_puts_all("[mk] boot: kernel_base=0x");
	uart_puthex64_all(MK_KERNEL_BASE);
	uart_puts_all("\r\n");

	uart_puts_all("[mk] boot: dst probe start\r\n");
	kernel_base[0] = 0xABU;
	if (kernel_base[0] != 0xABU) {
		uart_puts_all("[mk] boot: dst probe FAIL\r\n");
		return;
	}
	uart_puts_all("[mk] boot: dst probe ok\r\n");

	uart_puts_all("[mk] boot: src probe start\r\n");
	uart_puts_all("[mk] boot: src[0]=0x");
	uart_puthex64_all((uint64_t) kernel_src[0]);
	uart_puts_all("\r\n");
	uart_puts_all("[mk] boot: src[1]=0x");
	uart_puthex64_all((uint64_t) kernel_src[1]);
	uart_puts_all("\r\n");
	uart_puts_all("[mk] boot: src[2]=0x");
	uart_puthex64_all((uint64_t) kernel_src[2]);
	uart_puts_all("\r\n");
	uart_puts_all("[mk] boot: src[3]=0x");
	uart_puthex64_all((uint64_t) kernel_src[3]);
	uart_puts_all("\r\n");
	uart_puts_all("[mk] boot: src[9]=0x");
	uart_puthex64_all((uint64_t) kernel_src[9]);
	uart_puts_all("\r\n");
	uart_puts_all("[mk] boot: src[10]=0x");
	uart_puthex64_all((uint64_t) kernel_src[10]);
	uart_puts_all("\r\n");
	uart_puts_all("[mk] boot: src probe ok\r\n");

	/* Decompress into kernel_base. */
	uart_puts_all("[mk] boot: decompress start\r\n");
	if (mk_zlib_is_gzip_package(kernel_src, kernel_src_size)) {
		int z_rc;
		uint32_t z_consumed = 0U;
		uint32_t z_out = 0U;

		uart_puts_all("[mk] boot: gzip backend=lk2nd-zlib\r\n");
		z_rc = mk_zlib_decompress_gzip(kernel_src, kernel_src_size,
					       kernel_base, MK_KERNEL_MAX_SIZE,
					       &z_consumed, &z_out);
		uart_puts_all("[mk] boot: zlib rc=0x");
		uart_puthex64_all((uint64_t) (uint32_t) z_rc);
		uart_puts_all(" consumed=0x");
		uart_puthex64_all((uint64_t) z_consumed);
		uart_puts_all(" out=0x");
		uart_puthex64_all((uint64_t) z_out);
		uart_puts_all("\r\n");
		trace_first16("[mk] boot: zlib first16", kernel_base);
		uart_puts_all("[mk] boot: zlib bounds out<=max=");
		uart_puthex64_all((uint64_t) (z_out <= MK_KERNEL_MAX_SIZE));
		uart_puts_all(" consumed<=gz=");
		uart_puthex64_all((uint64_t) (z_consumed <= kernel_src_size));
		uart_puts_all("\r\n");
		if (z_rc == 0) {
			decomp_size = (int) z_out;
			uart_puts_all("[mk] boot: zlib consumed=0x");
			uart_puthex64_all((uint64_t) z_consumed);
			uart_puts_all(" out=0x");
			uart_puthex64_all((uint64_t) z_out);
			uart_puts_all("\r\n");
		} else {
			uart_puts_all("[mk] boot: zlib backend failed, fallback old\r\n");
			decomp_size = gunzip(kernel_src, kernel_src_size,
					     kernel_base, MK_KERNEL_MAX_SIZE);
		}
	} else {
		img_magic = le32r(kernel_src + ARM64_HDR_MAGIC_OFF);
		if (img_magic == ARM64_IMAGE_MAGIC) {
			if (kernel_src_size > MK_KERNEL_MAX_SIZE) {
				uart_puts_all("[mk] boot: raw Image too large\r\n");
				return;
			}
			uart_puts_all("[mk] boot: kernel format=raw Image\r\n");
			mk_memcpy(kernel_base, kernel_src, kernel_src_size);
			decomp_size = (int) kernel_src_size;
		} else {
			uart_puts_all("[mk] boot: gzip backend=legacy\r\n");
			decomp_size = gunzip(kernel_src, kernel_src_size,
					     kernel_base, MK_KERNEL_MAX_SIZE);
		}
	}
	if (decomp_size <= 0) {
		uart_puts_all("[mk] boot: decompression failed\r\n");
		return;
	}

	uart_puts_all("[mk] boot: decompressed=0x");
	uart_puthex64_all((uint64_t) (uint32_t) decomp_size);
	uart_puts_all("\r\n");
	trace_first16("[mk] boot: decompressed first16", kernel_base);

	/* Parse arm64 Image header. */
	uart_puts_all("[mk] boot: image header begin\r\n");
	img_magic = le32r(kernel_base + ARM64_HDR_MAGIC_OFF);
	uart_puts_all("[mk] boot: image magic=0x");
	uart_puthex64_all((uint64_t) img_magic);
	uart_puts_all("\r\n");
	if (img_magic != ARM64_IMAGE_MAGIC) {
		uart_puts_all("[mk] boot: not arm64 Image\r\n");
		return;
	}

	uart_puts_all("[mk] boot: image hdr fields begin\r\n");
	text_offset = le64r(kernel_base + ARM64_HDR_TEXT_OFFSET);
	img_size    = le64r(kernel_base + ARM64_HDR_IMAGE_SIZE);

	/* Fall back to standard 0x80000 for old kernels (image_size == 0). */
	if (img_size == 0U) {
		text_offset = 0x80000UL;
	}
	/* Bogus offset guard (e.g. U-Boot headers). */
	if (text_offset > 2U * 1024U * 1024U) {
		text_offset = 0UL;
	}

	uart_puts_all("[mk] boot: text_offset=0x");
	uart_puthex64_all(text_offset);
	uart_puts_all("\r\n");

	/*
	 * arm64 boot protocol: kernel Image must be placed at a 2MB-aligned
	 * base + text_offset. Our base is MK_KERNEL_BASE.
	 * Move the decompressed image from offset 0 to offset text_offset.
	 */
	if (text_offset != 0UL) {
		mk_memmove(kernel_base + text_offset, kernel_base,
			   (uint32_t) decomp_size);
	}
	/* Keep entry consistent with the relocated Image placement. */
	kernel_entry = kernel_base + text_offset;
	kernel_end   = (uint64_t) (uintptr_t) kernel_entry +
		       (uint64_t) (uint32_t) decomp_size;
	kernel_flush_end = (img_size >= (uint64_t) (uint32_t) decomp_size) ?
			   ((uint64_t) (uintptr_t) kernel_entry + img_size) :
			   kernel_end;

	/* Load initramfs. */
	uart_puts_all("[mk] boot: initrd=");
	uart_puts_all(s_entry.initrd);
	uart_puts_all("\r\n");

	initrd_size = mk_ext2_load_file(&s_ext2, s_entry.initrd,
					initrd_buf, MK_INITRD_MAX_SIZE);
	if (initrd_size <= 0) {
		uart_puts_all("[mk] boot: initramfs load failed\r\n");
		return;
	}

	initrd_start = MK_INITRD_ADDR;
	initrd_end   = MK_INITRD_ADDR + (uint64_t) (uint32_t) initrd_size;

	uart_puts_all("[mk] boot: initrd_end=0x");
	uart_puthex64_all(initrd_end);
	uart_puts_all("\r\n");

	build_effective_bootargs(s_bootargs_effective,
				(uint32_t) sizeof(s_bootargs_effective),
				s_entry.append);
	uart_puts_all("[mk] boot: fdt patch minimal\r\n");
	if (!fdt_patch((const uint8_t *) (uintptr_t) fdt_ptr,
		       (uint8_t *) (uintptr_t) MK_FDT_COPY_ADDR,
		       s_bootargs_effective,
		       initrd_start,
		       initrd_end)) {
		uart_puts_all("[mk] boot: fdt patch failed\r\n");
		return;
	}
	handoff_fdt = MK_FDT_COPY_ADDR;

	uart_puts_all("[mk] boot: entry=0x");
	uart_puthex64_all((uint64_t) (uintptr_t) kernel_entry);
	uart_puts_all(" fdt=0x");
	uart_puthex64_all(handoff_fdt);
	uart_puts_all("\r\n");
	uart_puts_all("[mk] boot: kernel_entry=0x");
	uart_puthex64_all((uint64_t) (uintptr_t) kernel_entry);
	uart_puts_all("\r\n");
	uart_puts_all("[mk] boot: kernel_out_size=0x");
	uart_puthex64_all((uint64_t) (uint32_t) decomp_size);
	uart_puts_all("\r\n");
	uart_puts_all("[mk] boot: kernel_end=0x");
	uart_puthex64_all(kernel_end);
	uart_puts_all("\r\n");
	uart_puts_all("[mk] boot: kernel_flush_end=0x");
	uart_puthex64_all(kernel_flush_end);
	uart_puts_all("\r\n");
	if (kernel_end <= MK_GZIP_STAGE_ADDR) {
		uart_puts_all("[mk] boot: gap_to_gz_stage=0x");
		uart_puthex64_all(MK_GZIP_STAGE_ADDR - kernel_end);
		uart_puts_all("\r\n");
	} else {
		uart_puts_all("[mk] boot: overlap_gz_stage=0x");
		uart_puthex64_all(kernel_end - MK_GZIP_STAGE_ADDR);
		uart_puts_all("\r\n");
	}

	/* Flush D-cache for all regions kernel will access. */
	flush_dcache_range((uint64_t) (uintptr_t) kernel_entry,
			   kernel_flush_end);
	flush_dcache_range(initrd_start, initrd_end);
	flush_dcache_range(MK_FDT_COPY_ADDR,
			   MK_FDT_COPY_ADDR + (uint64_t) MK_FDT_MAX_SIZE);
	mk_stage0_log_reset_watchdog_state("pre-handoff");

	jump_to_kernel((uint64_t) (uintptr_t) kernel_entry, handoff_fdt);
}

void mk_boot_linux(uint64_t fdt_ptr, uint64_t boot_lba)
{
	mk_boot_linux_common(fdt_ptr, boot_lba, 0, 0U);
}

void mk_boot_linux_override_kernel(uint64_t fdt_ptr, uint64_t boot_lba,
				   const uint8_t *kernel_buf,
				   uint32_t kernel_size)
{
	mk_boot_linux_common(fdt_ptr, boot_lba, kernel_buf, kernel_size);
}
