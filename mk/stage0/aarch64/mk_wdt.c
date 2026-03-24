#include <stdint.h>
#include "mk_common.h"
#include "mk_pmic.h"
#include "mk_wdt.h"

/* MediaTek TOPRGU watchdog registers. */
#define MTK_WDT_MODE 0x00U
#define MTK_WDT_MODE_EN (1U << 0)
#define MTK_WDT_MODE_EXRST_EN (1U << 2)
#define MTK_WDT_MODE_IRQ_EN (1U << 3)
#define MTK_WDT_MODE_AUTO_START (1U << 4)
#define MTK_WDT_MODE_DUAL_EN (1U << 6)
#define MTK_WDT_MODE_KEY 0x22000000U
#define MTK_WDT_LENGTH 0x04U
#define MTK_WDT_LENGTH_KEY 0x8U
#define MTK_WDT_RST 0x08U
#define MTK_WDT_RST_RELOAD 0x1971U
#define MTK_WDT_INTERVAL 0x10U
#define MTK_WDT_SWRST 0x14U
#define MTK_WDT_SWRST_KEY 0x1209U
#define MTK_WDT_NONRST2 0x24U
#define MTK_WDT_NONRST2_BOOTMODE_MASK 0x0fU
#define MTK_WDT_NONRST2_STAGE_MASK 0xe0000000U
#define MTK_WDT_NONRST2_STAGE_LK 0x40000000U
#define MTK_WDT_BYPASS_PWR_KEY (1U << 13)
#define MTK_WDT_REQ_MODE 0x30U
#define MTK_WDT_REQ_MODE_KEY 0x33000000U
#define MTK_WDT_REQ_MODE_RECOVERY_SEQ 0x33040002U
#define MTK_WDT_REQ_IRQ_EN 0x34U
#define MTK_WDT_REQ_IRQ_EN_KEY 0x44000000U
#define MTK_WDT_REQ_IRQ_EN_RECOVERY_MASK 0xfffbfffdU
#define MTK_WDT_REQ_IRQ_EN_RECOVERY_SEQ 0x44000000U
#define MTK_WDT_LATCH_CTL2 0x48U
#define MTK_WDT_LENGTH_VALUE(n) ((uint32_t) (n) << 11)
#define MTK_BOOTMODE_RECOVERY 2U

#define MT6357_PONSTS_ADDR 0x0cU
#define MT6357_POFFSTS_ADDR 0x0eU
#define MT6357_TOP_RST_STATUS_ADDR 0x152U
#define MT6357_TOPSTATUS_ADDR 0x24U
#define MTK_PMIC_WRAP_BASE 0x1000d000ULL
#define PWRAP_INIT_DONE2 0x0a0U

#define MBOOT_PARAMS_DEF_SRAM 1U
#define MBOOT_PARAMS_DEF_DRAM 2U
#define MBOOT_PARAMS_SIG 0x43474244U
#define MBOOT_MEMINFO_MAGIC1 0x61646472U
#define MBOOT_MEMINFO_MAGIC2 0x73697a65U

#define MK_FASTBOOT_ACTION_NONE 0U
#define MK_FASTBOOT_ACTION_REBOOT 1U
#define MK_FASTBOOT_ACTION_REBOOT_BOOTLOADER 2U
#define MK_FASTBOOT_ACTION_CONTINUE 3U
#define MK_FASTBOOT_ACTION_REBOOT_RECOVERY 4U
#define MK_FASTBOOT_ACTION_BOOT_STAGED_KERNEL 5U

static uint64_t g_wdt_base;
static int g_wdt_active;

typedef struct {
	uint8_t valid;
	uint32_t mode;
	uint32_t length;
	uint32_t interval;
	uint32_t nonrst2;
	uint32_t req_mode;
	uint32_t req_irq_en;
} mk_wdt_state_t;
static mk_wdt_state_t g_wdt_saved_state;

extern int mk_stage0_write_para_bcb(uint8_t set_recovery);

uint64_t mk_wdt_get_base(void)
{
	return g_wdt_base;
}

void setup_wdt(const void *fdt)
{
	uint64_t base = 0x10007000ULL;
	uint32_t reg;
	(void) fdt;

	if (base == 0) {
		return;
	}

	if (g_wdt_saved_state.valid == 0U) {
		g_wdt_saved_state.mode = mmio_read32(base + MTK_WDT_MODE);
		g_wdt_saved_state.length = mmio_read32(base + MTK_WDT_LENGTH);
		g_wdt_saved_state.interval = mmio_read32(base + MTK_WDT_INTERVAL);
		g_wdt_saved_state.nonrst2 = mmio_read32(base + MTK_WDT_NONRST2);
		g_wdt_saved_state.req_mode = mmio_read32(base + MTK_WDT_REQ_MODE);
		g_wdt_saved_state.req_irq_en = mmio_read32(base + MTK_WDT_REQ_IRQ_EN);
		g_wdt_saved_state.valid = 1U;
		uart_puts_all("[mk] wdt snapshot\r\n");
	}

	reg = mmio_read32(base + MTK_WDT_MODE);
	reg &= ~MTK_WDT_MODE_EN;
	reg |= MTK_WDT_MODE_KEY;
	mmio_write32(base + MTK_WDT_MODE, reg);
	mmio_write32(base + MTK_WDT_RST, MTK_WDT_RST_RELOAD);

	g_wdt_base = base;
	g_wdt_active = 1;
}

void pet_wdt(void)
{
	if (g_wdt_active != 0 && g_wdt_base != 0) {
		mmio_write32(g_wdt_base + MTK_WDT_RST, MTK_WDT_RST_RELOAD);
	}
}

void mk_stage0_wdt_restore_for_linux(void)
{
	uint32_t mode;

	if (g_wdt_base == 0U) {
		uart_puts_all("[mk] wdt restore: no base\r\n");
		return;
	}

	mode = mmio_read32(g_wdt_base + MTK_WDT_MODE);
	mode &= ~(MTK_WDT_MODE_EN |
		   MTK_WDT_MODE_EXRST_EN |
		   MTK_WDT_MODE_IRQ_EN |
		   MTK_WDT_MODE_AUTO_START |
		   MTK_WDT_MODE_DUAL_EN);
	mmio_write32(g_wdt_base + MTK_WDT_MODE,
		    (mode & 0x00ffffffU) | MTK_WDT_MODE_KEY);
	g_wdt_active = 0;
	uart_puts_all("[mk] wdt disable for linux: mode=0x");
	uart_puthex64_all(mmio_read32(g_wdt_base + MTK_WDT_MODE));
	uart_puts_all("\r\n");
}

void mk_stage0_wdt_handoff_ab_quiesce(void)
{
	uint32_t reg_mode;
	uint32_t reg_mode_post;
	uint32_t reg_req_mode;
	uint32_t reg_req_mode_post;
	uint32_t reg_req_irq;
	uint32_t reg_req_irq_post;

	if (g_wdt_base == 0U) {
		uart_puts_all("[mk] wdt-ab: no active base\r\n");
		return;
	}

	mk_stage0_log_reset_watchdog_state("wdt-ab-before");

	reg_mode = mmio_read32(g_wdt_base + MTK_WDT_MODE);
	uart_puts_all("[mk] wdt-ab: mode pre=0x");
	uart_puthex64_all((uint64_t) reg_mode);
	reg_mode &= ~(MTK_WDT_MODE_EN |
		      MTK_WDT_MODE_EXRST_EN |
		      MTK_WDT_MODE_IRQ_EN |
		      MTK_WDT_MODE_AUTO_START |
		      MTK_WDT_MODE_DUAL_EN);
	reg_mode |= MTK_WDT_MODE_KEY;
	uart_puts_all(" wr=0x");
	uart_puthex64_all((uint64_t) reg_mode);
	mmio_write32(g_wdt_base + MTK_WDT_MODE, reg_mode);
	reg_mode_post = mmio_read32(g_wdt_base + MTK_WDT_MODE);
	uart_puts_all(" post=0x");
	uart_puthex64_all((uint64_t) reg_mode_post);
	uart_puts_all("\r\n");

	reg_req_mode = mmio_read32(g_wdt_base + MTK_WDT_REQ_MODE);
	uart_puts_all("[mk] wdt-ab: req pre=0x");
	uart_puthex64_all((uint64_t) reg_req_mode);
	reg_req_mode &= ~(MTK_WDT_REQ_MODE_RECOVERY_SEQ & ~MTK_WDT_REQ_MODE_KEY);
	uart_puts_all(" wr=0x");
	uart_puthex64_all((uint64_t) (MTK_WDT_REQ_MODE_KEY | reg_req_mode));
	mmio_write32(g_wdt_base + MTK_WDT_REQ_MODE,
		      MTK_WDT_REQ_MODE_KEY | reg_req_mode);
	reg_req_mode_post = mmio_read32(g_wdt_base + MTK_WDT_REQ_MODE);
	uart_puts_all(" post=0x");
	uart_puthex64_all((uint64_t) reg_req_mode_post);
	uart_puts_all("\r\n");

	reg_req_irq = mmio_read32(g_wdt_base + MTK_WDT_REQ_IRQ_EN);
	uart_puts_all("[mk] wdt-ab: irq pre=0x");
	uart_puthex64_all((uint64_t) reg_req_irq);
	reg_req_irq = 0U;
	uart_puts_all(" wr=0x");
	uart_puthex64_all((uint64_t) MTK_WDT_REQ_IRQ_EN_KEY);
	mmio_write32(g_wdt_base + MTK_WDT_REQ_IRQ_EN,
		      MTK_WDT_REQ_IRQ_EN_KEY | reg_req_irq);
	reg_req_irq_post = mmio_read32(g_wdt_base + MTK_WDT_REQ_IRQ_EN);
	uart_puts_all(" post=0x");
	uart_puthex64_all((uint64_t) reg_req_irq_post);
	uart_puts_all("\r\n");

	mmio_write32(g_wdt_base + MTK_WDT_RST, MTK_WDT_RST_RELOAD);
	mk_stage0_log_reset_watchdog_state("wdt-ab-after");
}

void mk_stage0_log_reset_watchdog_state(const char *tag)
{
	uint64_t wdt_base = 0x10007000ULL;
	uint64_t wdt_mode;
	uint64_t wdt_len;
	uint64_t wdt_int;
	uint64_t wdt_nrst2;
	uint64_t wdt_req;
	uint64_t wdt_irq;
	uint64_t wdt_latch2;
	uint32_t pwrap_init;
	uint16_t ponsts = 0U;
	uint16_t poffsts = 0U;
	uint16_t top_rst = 0U;
	uint16_t topstatus = 0U;
	int have_pmic = 1;

	if (tag != 0 && tag[0] == 'e' && tag[1] == 'a' && tag[2] == 'r' && tag[3] == 'l' &&
	    tag[4] == 'y' && tag[5] == '\0') {
		uart_puts_all("[mk] rst early begin\r\n");
	}

	wdt_mode = mmio_read32(wdt_base + MTK_WDT_MODE);
	wdt_len = mmio_read32(wdt_base + MTK_WDT_LENGTH);
	wdt_int = mmio_read32(wdt_base + MTK_WDT_INTERVAL);
	wdt_nrst2 = mmio_read32(wdt_base + MTK_WDT_NONRST2);
	wdt_req = mmio_read32(wdt_base + MTK_WDT_REQ_MODE);
	wdt_irq = mmio_read32(wdt_base + MTK_WDT_REQ_IRQ_EN);
	wdt_latch2 = mmio_read32(wdt_base + MTK_WDT_LATCH_CTL2);

	pwrap_init = mmio_read32(MTK_PMIC_WRAP_BASE + PWRAP_INIT_DONE2);
	if (mk_pmic_pwrap_read16(MT6357_PONSTS_ADDR, &ponsts) != 0) {
		have_pmic = 0;
	}
	if (mk_pmic_pwrap_read16(MT6357_POFFSTS_ADDR, &poffsts) != 0) {
		have_pmic = 0;
	}
	if (mk_pmic_pwrap_read16(MT6357_TOP_RST_STATUS_ADDR, &top_rst) != 0) {
		have_pmic = 0;
	}
	if (mk_pmic_pwrap_read16(MT6357_TOPSTATUS_ADDR, &topstatus) != 0) {
		have_pmic = 0;
	}

	uart_puts_all("[mk] rst ");
	uart_puts_all(tag);
	uart_puts_all(": wdt_mode=0x");
	uart_puthex64_all(wdt_mode);
	uart_puts_all(" wdt_len=0x");
	uart_puthex64_all(wdt_len);
	uart_puts_all(" wdt_int=0x");
	uart_puthex64_all(wdt_int);
	uart_puts_all(" wdt_nrst2=0x");
	uart_puthex64_all(wdt_nrst2);
	uart_puts_all(" wdt_req=0x");
	uart_puthex64_all(wdt_req);
	uart_puts_all(" wdt_irq=0x");
	uart_puthex64_all(wdt_irq);
	uart_puts_all(" wdt_latch2=0x");
	uart_puthex64_all(wdt_latch2);
	uart_puts_all(" pwrap_init=0x");
	uart_puthex64_all((uint64_t) pwrap_init);
	if (have_pmic != 0) {
		uart_puts_all(" ponsts=0x");
		uart_puthex64_all((uint64_t) ponsts);
		uart_puts_all(" poffsts=0x");
		uart_puthex64_all((uint64_t) poffsts);
		uart_puts_all(" top_rst=0x");
		uart_puthex64_all((uint64_t) top_rst);
		uart_puts_all(" topstatus=0x");
		uart_puthex64_all((uint64_t) topstatus);
	} else {
		uart_puts_all(" pmic=unavailable");
	}
	uart_puts_all("\r\n");
	if (tag != 0 && tag[0] == 'e' && tag[1] == 'a' && tag[2] == 'r' && tag[3] == 'l' &&
	    tag[4] == 'y' && tag[5] == '\0') {
		uart_puts_all("[mk] rst early end\r\n");
	}
}

static void log_mboot_params_snapshot(uint32_t base, uint32_t size)
{
	uint32_t sig;
	uint32_t off_pl;
	uint32_t off_lpl;
	uint32_t sz_pl;
	uint32_t off_lk;
	uint32_t off_llk;
	uint32_t sz_lk;
	uint32_t sz_buffer;
	uint32_t off_linux;

	if (base == 0U || size < 48U) {
		uart_puts_all("[mk] rr: dbrb absent\r\n");
		return;
	}

	sig = mmio_read32((uint64_t) base + 0U);
	off_pl = mmio_read32((uint64_t) base + 4U);
	off_lpl = mmio_read32((uint64_t) base + 8U);
	sz_pl = mmio_read32((uint64_t) base + 12U);
	off_lk = mmio_read32((uint64_t) base + 16U);
	off_llk = mmio_read32((uint64_t) base + 20U);
	sz_lk = mmio_read32((uint64_t) base + 24U);
	sz_buffer = mmio_read32((uint64_t) base + 40U);
	off_linux = mmio_read32((uint64_t) base + 44U);

	uart_puts_all("[mk] rr: dbrb sig=0x");
	uart_puthex64_all(sig);
	uart_puts_all(" off_lpl=0x");
	uart_puthex64_all(off_lpl);
	uart_puts_all(" off_llk=0x");
	uart_puthex64_all(off_llk);
	uart_puts_all(" sz_buffer=0x");
	uart_puthex64_all(sz_buffer);
	uart_puts_all(" off_linux=0x");
	uart_puthex64_all(off_linux);
	uart_puts_all("\r\n");

	if (sig != MBOOT_PARAMS_SIG) {
		uart_puts_all("[mk] rr: dbrb bad sig\r\n");
		return;
	}

	if (off_lpl != 0U && off_lpl + 16U <= size) {
		uart_puts_all("[mk] rr: last-pl [0]=0x");
		uart_puthex64_all(mmio_read32((uint64_t) base + off_lpl + 0U));
		uart_puts_all(" [1]=0x");
		uart_puthex64_all(mmio_read32((uint64_t) base + off_lpl + 4U));
		uart_puts_all(" [2]=0x");
		uart_puthex64_all(mmio_read32((uint64_t) base + off_lpl + 8U));
		uart_puts_all(" [3]=0x");
		uart_puthex64_all(mmio_read32((uint64_t) base + off_lpl + 12U));
		uart_puts_all("\r\n");
	}

	if (off_llk != 0U && off_llk + 16U <= size) {
		uart_puts_all("[mk] rr: last-lk [0]=0x");
		uart_puthex64_all(mmio_read32((uint64_t) base + off_llk + 0U));
		uart_puts_all(" [1]=0x");
		uart_puthex64_all(mmio_read32((uint64_t) base + off_llk + 4U));
		uart_puts_all(" [2]=0x");
		uart_puthex64_all(mmio_read32((uint64_t) base + off_llk + 8U));
		uart_puts_all(" [3]=0x");
		uart_puthex64_all(mmio_read32((uint64_t) base + off_llk + 12U));
		uart_puts_all("\r\n");
	}

	(void) off_pl;
	(void) sz_pl;
	(void) off_lk;
	(void) sz_lk;
}

void mk_stage0_log_retained_reset_provenance(const void *fdt)
{
	const uint8_t *value = 0;
	uint32_t len = 0;
	uint32_t start;
	uint32_t size;
	uint32_t def_type;
	uint32_t offset;
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
	uint8_t chosen_stack[32] = {0};
	int found = 0;

	if (base == 0 || be32_read(base) != 0xd00dfeedU) {
		uart_puts_all("[mk] rr: no ram_console\r\n");
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

		if (token == 1U) { /* FDT_BEGIN_NODE */
			const char *node_name = (const char *) p;
			depth++;
			if (depth >= 32) { break; }
			chosen_stack[depth] = 0;
			if (depth == 1 && str_eq(node_name, "chosen")) {
				chosen_stack[depth] = 1;
			} else if (depth > 1 && chosen_stack[depth - 1] != 0) {
				chosen_stack[depth] = 1;
			}
			while (p < struct_end && *p != '\0') { p++; }
			if (p < struct_end) { p++; }
			while (((uintptr_t) p & 3U) != 0U && p < struct_end) { p++; }
			continue;
		}
		if (token == 2U) { if (depth >= 0) { depth--; } continue; }
		if (token == 4U) { continue; }
		if (token == 9U) { break; }
		if (token == 3U) { /* FDT_PROP */
			uint32_t plen;
			uint32_t nameoff;
			if (p + 8 > struct_end) { break; }
			plen = be32_read(p);
			nameoff = be32_read(p + 4);
			p += 8;
			if (depth >= 1 && chosen_stack[depth] != 0 &&
			    nameoff < size_strings && strings + nameoff < strings_end &&
			    str_eq((const char *)(strings + nameoff), "ram_console") &&
			    plen >= 16U) {
				value = p;
				len = plen;
				found = 1;
			}
			p += plen;
			while (((uintptr_t) p & 3U) != 0U && p < struct_end) { p++; }
		}
	}

	if (!found || len < 16U) {
		uart_puts_all("[mk] rr: no ram_console\r\n");
		return;
	}

	start = le32_read(value + 0U);
	size = le32_read(value + 4U);
	def_type = le32_read(value + 8U);
	offset = le32_read(value + 12U);

	uart_puts_all("[mk] rr: ram_console start=0x");
	uart_puthex64_all(start);
	uart_puts_all(" size=0x");
	uart_puthex64_all(size);
	uart_puts_all(" def=0x");
	uart_puthex64_all(def_type);
	uart_puts_all(" off=0x");
	uart_puthex64_all(offset);
	uart_puts_all("\r\n");

	if (offset > size) {
		uint32_t info_base = start + offset;
		uint32_t magic1 = mmio_read32((uint64_t) info_base + 0U);
		uint32_t mrdump_addr = mmio_read32((uint64_t) info_base + 20U);
		uint32_t mrdump_size = mmio_read32((uint64_t) info_base + 24U);
		uint32_t dram_addr = mmio_read32((uint64_t) info_base + 28U);
		uint32_t dram_size = mmio_read32((uint64_t) info_base + 32U);
		uint32_t mini_addr = mmio_read32((uint64_t) info_base + 44U);
		uint32_t mini_size = mmio_read32((uint64_t) info_base + 48U);
		uint32_t magic2 = mmio_read32((uint64_t) info_base + 52U);

		uart_puts_all("[mk] rr: meminfo magic1=0x");
		uart_puthex64_all(magic1);
		uart_puts_all(" magic2=0x");
		uart_puthex64_all(magic2);
		uart_puts_all(" mrdump=0x");
		uart_puthex64_all(mrdump_addr);
		uart_puts_all("+0x");
		uart_puthex64_all(mrdump_size);
		uart_puts_all(" dram=0x");
		uart_puthex64_all(dram_addr);
		uart_puts_all("+0x");
		uart_puthex64_all(dram_size);
		uart_puts_all(" mini=0x");
		uart_puthex64_all(mini_addr);
		uart_puts_all("+0x");
		uart_puthex64_all(mini_size);
		uart_puts_all("\r\n");

		if (magic1 != MBOOT_MEMINFO_MAGIC1 || magic2 != MBOOT_MEMINFO_MAGIC2) {
			uart_puts_all("[mk] rr: meminfo bad magic\r\n");
		}
	}

	if (def_type == MBOOT_PARAMS_DEF_SRAM || def_type == MBOOT_PARAMS_DEF_DRAM) {
		log_mboot_params_snapshot(start, size);
	}
}

void arm_recovery_wdt(void)
{
	uint32_t reg_mode;
	uint32_t reg_interval;
	uint32_t reg_norst2;
	uint32_t reg_req_mode;
	uint32_t reg_req_irq;

	if (g_wdt_base == 0) {
		uart_puts_all("[mk] recovery reboot unavailable (no WDT)\r\n");
		for (;;) {
			__asm__ volatile("wfe");
		}
	}

	uart_puts_all("[mk] reboot -> recovery via TOPRGU\r\n");
	(void) mk_stage0_write_para_bcb(1U);

	reg_norst2 = mmio_read32(g_wdt_base + MTK_WDT_NONRST2);
	reg_norst2 &= ~MTK_WDT_NONRST2_BOOTMODE_MASK;
	reg_norst2 |= MTK_BOOTMODE_RECOVERY;
	mmio_write32(g_wdt_base + MTK_WDT_NONRST2, reg_norst2);

	reg_norst2 = mmio_read32(g_wdt_base + MTK_WDT_NONRST2);
	reg_norst2 &= ~MTK_WDT_NONRST2_STAGE_MASK;
	reg_norst2 |= MTK_WDT_NONRST2_STAGE_LK;
	mmio_write32(g_wdt_base + MTK_WDT_NONRST2, reg_norst2);

	reg_mode = mmio_read32(g_wdt_base + MTK_WDT_MODE);
	reg_mode = (reg_mode & 0xffffffb0U) | MTK_WDT_MODE_KEY | MTK_WDT_MODE_AUTO_START;
	mmio_write32(g_wdt_base + MTK_WDT_MODE, reg_mode);

	reg_norst2 = mmio_read32(g_wdt_base + MTK_WDT_NONRST2);
	reg_norst2 |= MTK_WDT_BYPASS_PWR_KEY;
	mmio_write32(g_wdt_base + MTK_WDT_NONRST2, reg_norst2);

	reg_interval = mmio_read32(g_wdt_base + MTK_WDT_INTERVAL);
	reg_interval = (reg_interval & 0xfffffff8U) | 5U;
	mmio_write32(g_wdt_base + MTK_WDT_INTERVAL, reg_interval);

	mmio_write32(g_wdt_base + MTK_WDT_LENGTH,
		    MTK_WDT_LENGTH_VALUE(10U) | MTK_WDT_LENGTH_KEY);

	reg_req_mode = mmio_read32(g_wdt_base + MTK_WDT_REQ_MODE);
	reg_req_mode |= MTK_WDT_REQ_MODE_RECOVERY_SEQ;
	mmio_write32(g_wdt_base + MTK_WDT_REQ_MODE, reg_req_mode);

	reg_req_irq = mmio_read32(g_wdt_base + MTK_WDT_REQ_IRQ_EN);
	reg_req_irq = (reg_req_irq & MTK_WDT_REQ_IRQ_EN_RECOVERY_MASK) |
		       MTK_WDT_REQ_IRQ_EN_RECOVERY_SEQ;
	mmio_write32(g_wdt_base + MTK_WDT_REQ_IRQ_EN, reg_req_irq);

	reg_mode = mmio_read32(g_wdt_base + MTK_WDT_MODE);
	reg_mode = (reg_mode & 0xfffffffdU) | (MTK_WDT_MODE_KEY | 0x5dU);
	mmio_write32(g_wdt_base + MTK_WDT_MODE, reg_mode);

	reg_norst2 = mmio_read32(g_wdt_base + MTK_WDT_NONRST2);
	reg_norst2 |= MTK_WDT_BYPASS_PWR_KEY;
	mmio_write32(g_wdt_base + MTK_WDT_NONRST2, reg_norst2);

	uart_puts_all("[mk] toprgu mode=0x");
	uart_puthex64_all(mmio_read32(g_wdt_base + MTK_WDT_MODE));
	uart_puts_all(" nonrst2=0x");
	uart_puthex64_all(mmio_read32(g_wdt_base + MTK_WDT_NONRST2));
	uart_puts_all(" interval=0x");
	uart_puthex64_all(mmio_read32(g_wdt_base + MTK_WDT_INTERVAL));
	uart_puts_all(" req_mode=0x");
	uart_puthex64_all(mmio_read32(g_wdt_base + MTK_WDT_REQ_MODE));
	uart_puts_all(" req_irq=0x");
	uart_puthex64_all(mmio_read32(g_wdt_base + MTK_WDT_REQ_IRQ_EN));
	uart_puts_all("\r\n");
}

void arm_normal_wdt(void)
{
	uint32_t reg_norst2;

	if (g_wdt_base == 0) {
		uart_puts_all("[mk] reboot unavailable (no WDT)\r\n");
		for (;;) {
			__asm__ volatile("wfe");
		}
	}

	uart_puts_all("[mk] reboot -> normal via TOPRGU\r\n");
	(void) mk_stage0_write_para_bcb(0U);

	reg_norst2 = mmio_read32(g_wdt_base + MTK_WDT_NONRST2);
	reg_norst2 &= ~MTK_WDT_NONRST2_BOOTMODE_MASK;
	reg_norst2 &= ~MTK_WDT_NONRST2_STAGE_MASK;
	mmio_write32(g_wdt_base + MTK_WDT_NONRST2, reg_norst2);

	mmio_write32(g_wdt_base + MTK_WDT_RST, MTK_WDT_RST_RELOAD);
	mmio_write32(g_wdt_base + MTK_WDT_SWRST, MTK_WDT_SWRST_KEY);

	for (;;) {
		__asm__ volatile("");
	}
}

void trigger_recovery_wdt_reset(void)
{
	if (g_wdt_base == 0) {
		for (;;) {
			__asm__ volatile("wfe");
		}
	}

	mmio_write32(g_wdt_base + MTK_WDT_RST, MTK_WDT_RST_RELOAD);
	mmio_write32(g_wdt_base + MTK_WDT_SWRST, MTK_WDT_SWRST_KEY);

	for (;;) {
		__asm__ volatile("");
	}
}

void mk_stage0_fastboot_action_immediate(uint8_t action)
{
	if (action == MK_FASTBOOT_ACTION_NONE) {
		return;
	}

	uart_puts_all("[mk] fastboot immediate action: ");
	if (action == MK_FASTBOOT_ACTION_REBOOT_RECOVERY) {
		uart_puts_all("reboot-recovery\r\n");
		delay_ms_calibrated(30U);
		arm_recovery_wdt();
		/* no return */
	} else if (action == MK_FASTBOOT_ACTION_REBOOT_BOOTLOADER) {
		uart_puts_all("reboot-bootloader\r\n");
	} else if (action == MK_FASTBOOT_ACTION_BOOT_STAGED_KERNEL) {
		uart_puts_all("boot-staged-kernel\r\n");
		return;
	} else if (action == MK_FASTBOOT_ACTION_CONTINUE) {
		uart_puts_all("continue\r\n");
		return;
	} else {
		uart_puts_all("reboot\r\n");
	}

	delay_ms_calibrated(30U);
	arm_normal_wdt();
}
