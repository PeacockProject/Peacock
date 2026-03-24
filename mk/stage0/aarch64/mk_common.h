#ifndef MK_COMMON_H
#define MK_COMMON_H

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* MMIO access                                                         */
/* ------------------------------------------------------------------ */

static inline uint32_t mmio_read32(uint64_t addr)
{
	volatile uint32_t *p = (volatile uint32_t *) (uintptr_t) addr;
	return *p;
}

static inline void mmio_write32(uint64_t addr, uint32_t value)
{
	volatile uint32_t *p = (volatile uint32_t *) (uintptr_t) addr;
	*p = value;
	__asm__ volatile("dsb sy");
}

/* ------------------------------------------------------------------ */
/* Endian helpers                                                      */
/* ------------------------------------------------------------------ */

static inline uint32_t be32_read(const uint8_t *p)
{
	return ((uint32_t) p[0] << 24) |
	       ((uint32_t) p[1] << 16) |
	       ((uint32_t) p[2] << 8) |
	       (uint32_t) p[3];
}

static inline uint64_t be64_read(const uint8_t *p)
{
	uint64_t hi = be32_read(p);
	uint64_t lo = be32_read(p + 4);
	return (hi << 32) | lo;
}

static inline void be32_write(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t) (v >> 24);
	p[1] = (uint8_t) (v >> 16);
	p[2] = (uint8_t) (v >> 8);
	p[3] = (uint8_t) v;
}

static inline void be64_write(uint8_t *p, uint64_t v)
{
	be32_write(p, (uint32_t) (v >> 32));
	be32_write(p + 4U, (uint32_t) v);
}

static inline uint32_t le32_read(const uint8_t *p)
{
	return (uint32_t) p[0] | ((uint32_t) p[1] << 8) |
	       ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

static inline uint64_t le64_read(const uint8_t *p)
{
	return (uint64_t) le32_read(p) | ((uint64_t) le32_read(p + 4U) << 32);
}

/* ------------------------------------------------------------------ */
/* String helpers (no libc)                                            */
/* ------------------------------------------------------------------ */

static inline uint32_t mk_strlen(const char *s)
{
	uint32_t n = 0;
	while (s[n] != '\0') { n++; }
	return n;
}

static inline uint32_t mk_strnlen(const char *s, uint32_t max_len)
{
	uint32_t n = 0U;
	while (n < max_len && s[n] != '\0') { n++; }
	return n;
}

static inline int mk_str_starts(const char *s, const char *prefix)
{
	while (*prefix != '\0') {
		if (*s != *prefix) { return 0; }
		s++;
		prefix++;
	}
	return 1;
}

static inline int str_eq(const char *a, const char *b)
{
	uint32_t i = 0;
	if (a == 0 || b == 0) {
		return 0;
	}
	while (a[i] != '\0' && b[i] != '\0') {
		if (a[i] != b[i]) {
			return 0;
		}
		i++;
	}
	return a[i] == b[i];
}

static inline int str_contains(const char *s, const char *needle)
{
	uint32_t i;
	uint32_t nlen;

	if (s == 0 || needle == 0) {
		return 0;
	}
	nlen = mk_strlen(needle);
	if (nlen == 0) {
		return 1;
	}
	for (i = 0; s[i] != '\0'; i++) {
		uint32_t j = 0;
		while (needle[j] != '\0' && s[i + j] != '\0' && s[i + j] == needle[j]) {
			j++;
		}
		if (j == nlen) {
			return 1;
		}
	}
	return 0;
}

static inline int value_has_string(const uint8_t *buf, uint32_t len, const char *needle)
{
	uint32_t i = 0;
	while (i < len) {
		const char *s = (const char *) (buf + i);
		uint32_t l = 0;
		while (i + l < len && s[l] != '\0') {
			l++;
		}
		if (l > 0 && (str_eq(s, needle) || str_contains(s, needle))) {
			return 1;
		}
		i += l + 1;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* Memory helpers                                                      */
/* ------------------------------------------------------------------ */

static inline void mk_memcpy(uint8_t *dst, const uint8_t *src, uint32_t n)
{
	uint32_t i;
	for (i = 0U; i < n; i++) { dst[i] = src[i]; }
}

static inline void mk_memmove(uint8_t *dst, const uint8_t *src, uint32_t n)
{
	uint32_t i;
	if (dst <= src) {
		for (i = 0U; i < n; i++) { dst[i] = src[i]; }
	} else {
		for (i = n; i > 0U; i--) { dst[i - 1U] = src[i - 1U]; }
	}
}

static inline void mk_memset(uint8_t *dst, uint8_t val, uint32_t n)
{
	uint32_t i;
	for (i = 0U; i < n; i++) { dst[i] = val; }
}

/* ------------------------------------------------------------------ */
/* Alignment                                                           */
/* ------------------------------------------------------------------ */

static inline uint32_t align_up_u32(uint32_t value, uint32_t align)
{
	if (align == 0U) {
		return value;
	}
	return (value + align - 1U) & ~(align - 1U);
}

/* ------------------------------------------------------------------ */
/* Externally implemented functions                                    */
/* ------------------------------------------------------------------ */

void uart_puts_all(const char *s);
void uart_puthex64_all(uint64_t v);
void pet_wdt(void);
void clean_dcache_range(uintptr_t start, uint64_t len);
void delay_ms_calibrated(uint32_t ms);

/* Assembly-provided symbols */
extern char __mk_stack_top[];
extern char __mk_el1_vectors[];
extern char __mk_el1_trampoline[];
extern void __mk_chainload_raw(uint64_t fdt_pa, uint64_t entry);
extern uint64_t __mk_saved_el2_valid;
extern uint64_t __mk_saved_cptr_el2;
extern uint64_t __mk_saved_vbar_el2;
extern uint64_t __mk_saved_sctlr_el2;

#endif /* MK_COMMON_H */
