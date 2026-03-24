#include <stdint.h>
#include "mtk_panel.h"
#include "mtk_display.h"
#include "mtk_i2c.h"
#include "mtk_gpio.h"
#include "mtk_usb.h"
#include "mtk_storage.h"
#include "mk_boot.h"
#include "peacock_logo_asset.h"
#include "mk_common.h"
#include "mk_fdt.h"
#include "mk_wdt.h"
#include "mk_pmic.h"
#include "mk_ui.h"

/* MediaTek MSDC0 (eMMC) for writing a minimal BCB into para. */
#define MTK_MSDC0_BASE 0x11230000ULL
#define MSDC_CFG 0x00U
#define MSDC_INT 0x0cU
#define MSDC_FIFOCS 0x14U
#define MSDC_TXDATA 0x18U
#define MSDC_RXDATA 0x1cU
#define SDC_CFG 0x30U
#define SDC_CMD 0x34U
#define SDC_ARG 0x38U
#define SDC_STS 0x3cU
#define SDC_RESP0 0x40U
#define SDC_BLK_NUM 0x50U
#define MSDC_DMA_SA_HIGH 0x8cU
#define MSDC_DMA_SA 0x90U
#define MSDC_DMA_CTRL 0x98U
#define MSDC_DMA_CFG 0x9cU
#define MSDC_DMA_LEN 0xa8U
#define EMMC51_CFG0 0x204U

#define MSDC_CFG_RST (1U << 2)
#define MSDC_CFG_PIO (1U << 3)
#define MSDC_CFG_CKSTB (1U << 7)
#define MSDC_CFG_CKDIV_MASK (0xffU << 8)
/* bits [17:16] = CKMOD, bit [18] = HS400_CK_MODE, bit 21 and 25 = HS400 ext bits */
#define MSDC_CFG_CKMOD_MASK (0x7U << 16)
/* CKDIV=7, CKMOD=0: SDR divider mode, source_clock/16 (~12 MHz from 200 MHz) */
#define MSDC_CFG_CKDIV_SLOW (0x07U << 8)
/* CKDIV=1, CKMOD=0: SDR divider mode, source_clock/4 (~50 MHz from 200 MHz, HS52) */
#define MSDC_CFG_CKDIV_HS52 (0x01U << 8)
/* Mask to preserve only bits [7:0] (MODE, RST, PIO, CKSTB) when resetting clock */
#define MSDC_CFG_LOWER_MASK 0x000000FFU
#define MSDC_FIFOCS_RXCNT_MASK 0xffU
#define MSDC_FIFOCS_TXCNT_MASK (0xffU << 16)
#define MSDC_FIFOCS_CLR (1U << 31)
#define MSDC_INT_CMDRDY (1U << 8)
#define MSDC_INT_CMDTMO (1U << 9)
#define MSDC_INT_RSPCRCERR (1U << 10)
#define MSDC_INT_XFER_COMPL (1U << 12)
#define MSDC_INT_DATTMO (1U << 14)
#define MSDC_INT_DATCRCERR (1U << 15)
#define MSDC_INT_DATA_MASK (MSDC_INT_XFER_COMPL | MSDC_INT_DATTMO | MSDC_INT_DATCRCERR)
#define MSDC_INT_CMD_MASK (MSDC_INT_CMDRDY | MSDC_INT_CMDTMO | MSDC_INT_RSPCRCERR)
#define SDC_STS_SDCBUSY (1U << 0)
#define SDC_STS_CMDBUSY (1U << 1)
#define SDC_CFG_BUSWIDTH_MASK (0x3U << 16)
#define SDC_CFG_BUSWIDTH_8BIT (0x2U << 16)
#define MSDC_EMMC51_CFG_CMDQEN (0x1U << 0)
#define MSDC_DMA_SURR_ADDR_HIGH4BIT (0xfU << 0)
#define MSDC_DMA_CTRL_START (0x1U << 0)
#define MSDC_DMA_CTRL_STOP (0x1U << 1)
#define MSDC_DMA_CTRL_MODE (0x1U << 8)
#define MSDC_DMA_CTRL_LASTBUF (0x1U << 10)
#define MSDC_DMA_CTRL_BRUSTSZ (0x7U << 12)
#define MSDC_DMA_CFG_STS (0x1U << 0)
#define MSDC_BRUST_64B 0x6U

#define MMC_CMD17_READ_SINGLE_BLOCK 17U
#define MMC_CMD24_WRITE_BLOCK 24U
#define MMC_CMD25_WRITE_MULTIPLE_BLOCK 25U
#define MMC_CMD12_STOP_TRANSMISSION 12U
#define MMC_CMD6_SWITCH 6U
#define MMC_CMD8_SEND_EXT_CSD 8U
#define MMC_CMD13_SEND_STATUS 13U
#define MMC_CMD29_CLR_WRITE_PROT 29U

#define EXT_CSD_CMDQ_MODE_EN 15U
#define EXT_CSD_FLUSH_CACHE 32U
#define EXT_CSD_CACHE_CTRL 33U
#define EXT_CSD_USER_WP 171U
#define EXT_CSD_ERASE_GROUP_DEF 175U
#define EXT_CSD_PARTITION_CONFIG 179U
#define EXT_CSD_HC_WP_GRP_SIZE 221U
#define EXT_CSD_HC_ERASE_GRP_SIZE 224U
#define EXT_CSD_WP_GRP_SIZE 35U
#define EXT_CSD_BUS_WIDTH 183U
#define EXT_CSD_HS_TIMING 185U
#define EXT_CSD_GENERIC_CMD6_TIME 248U
#define EXT_CSD_CACHE_SIZE 249U

#define DEFAULT_CMD6_TIMEOUT_MS 500U

#define MMC_RSP_R1 1U
#define MMC_RSP_R1B 7U
#define R1_WP_VIOLATION (1U << 26)
#define MMC_RAWCMD_NODATA(opcode, resp) (((opcode) & 0x3fU) | (((resp) & 0x7U) << 7))
#define MMC_RAWCMD_READ(opcode, resp, blklen) \
	(((opcode) & 0x3fU) | (((resp) & 0x7U) << 7) | (((blklen) & 0xfffU) << 16) | (1U << 11))
#define MMC_RAWCMD_WRITE(opcode, resp, blklen) \
	(MMC_RAWCMD_READ((opcode), (resp), (blklen)) | (1U << 13))

#ifndef MK_DEVICE_PHOENIX_BOOTSTAGE
#define MK_DEVICE_PHOENIX_BOOTSTAGE ((const char *) 0)
#endif

#ifndef MK_DEVICE_PHOENIX_PRIMARY_PARTITION
#define MK_DEVICE_PHOENIX_PRIMARY_PARTITION ((const char *) 0)
#endif

#ifndef MK_DEVICE_PHOENIX_FALLBACK_PARTITION
#define MK_DEVICE_PHOENIX_FALLBACK_PARTITION ((const char *) 0)
#endif

#ifndef MK_DEVICE_PHOENIX_RECORD_MAGIC
#define MK_DEVICE_PHOENIX_RECORD_MAGIC ((const char *) 0)
#endif

#ifndef MK_DEVICE_PHOENIX_UFS_OFFSET
#define MK_DEVICE_PHOENIX_UFS_OFFSET 0ULL
#endif

#ifndef MK_DEVICE_PHOENIX_EMMC_OFFSET
#define MK_DEVICE_PHOENIX_EMMC_OFFSET 0ULL
#endif

#ifndef MK_DEVICE_BCB_PARA_LBA
#define MK_DEVICE_BCB_PARA_LBA 0ULL
#endif

#ifndef MK_DEVICE_BOOT_LABEL
#define MK_DEVICE_BOOT_LABEL ((const char *) 0)
#endif

#ifndef MK_DEVICE_ROOT_LABEL
#define MK_DEVICE_ROOT_LABEL ((const char *) 0)
#endif

#ifndef MK_DEVICE_LCM_BOOT_NAME
#define MK_DEVICE_LCM_BOOT_NAME ((const char *) 0)
#endif

#ifndef MK_DEVICE_HAS_FASTBOOT_USB
#define MK_DEVICE_HAS_FASTBOOT_USB 0
#endif

static uint64_t g_peacock_boot_lba;
static uint64_t g_peacock_boot_count;
static uint64_t g_peacock_root_lba;
static uint64_t g_peacock_root_count;
static uint8_t g_msdc_dma_sector_buf[512] __attribute__((aligned(64)));
static int g_peacock_boot_found;
static int g_peacock_root_found;
static uint8_t g_msdc_multi_write_disable;

/* LK MSDC register snapshot — taken before MK touches eMMC, restored at handoff
 * so the kernel's msdc driver finds the host in the same state LK left it. */
static uint32_t g_lk_msdc_cfg;
static uint32_t g_lk_sdc_cfg;
static uint32_t g_lk_emmc51_cfg0;
static int      g_lk_msdc_saved;

static uint8_t serial_char_allowed(char c)
{
	if (c >= '0' && c <= '9') {
		return 1U;
	}
	if (c >= 'A' && c <= 'Z') {
		return 1U;
	}
	if (c >= 'a' && c <= 'z') {
		return 1U;
	}
	if (c == '-' || c == '_' || c == '.') {
		return 1U;
	}
	return 0U;
}

static uint32_t copy_serial_token(const char *src, char *dst, uint32_t dst_cap)
{
	uint32_t n = 0;

	if (src == 0 || dst == 0 || dst_cap < 2U) {
		return 0;
	}
	while (src[n] != '\0' && src[n] != ' ' && n + 1U < dst_cap) {
		if (serial_char_allowed(src[n]) == 0U) {
			break;
		}
		dst[n] = src[n];
		n++;
	}
	dst[n] = '\0';
	return n;
}

static uint32_t parse_android_serial_from_bootargs(const char *bootargs, char *dst, uint32_t dst_cap)
{
	const char *key = "androidboot.serialno=";
	uint32_t key_len = mk_strlen(key);
	uint32_t i;

	if (bootargs == 0 || dst == 0 || dst_cap < 2U) {
		return 0;
	}
	for (i = 0; bootargs[i] != '\0'; i++) {
		uint32_t j = 0;

		if (i != 0 && bootargs[i - 1] != ' ') {
			continue;
		}
		while (j < key_len && bootargs[i + j] != '\0' && bootargs[i + j] == key[j]) {
			j++;
		}
		if (j == key_len) {
			return copy_serial_token(bootargs + i + key_len, dst, dst_cap);
		}
	}
	return 0;
}

static void uart_put_display_status(mk_stage0_display_status_t status)
{
	switch (status) {
	case MK_STAGE0_DISPLAY_NOT_NEEDED:
		uart_puts_all("already-inited");
		return;
	case MK_STAGE0_DISPLAY_READY:
		uart_puts_all("ready");
		return;
	case MK_STAGE0_DISPLAY_PENDING:
		uart_puts_all("pending");
		return;
	case MK_STAGE0_DISPLAY_UNSUPPORTED:
		uart_puts_all("unsupported");
		return;
	case MK_STAGE0_DISPLAY_BAD_STATE:
		uart_puts_all("bad-state");
		return;
	default:
		uart_puts_all("unknown");
		return;
	}
}

static void uart_put_display_fail_stage(mk_stage0_display_fail_stage_t stage)
{
	switch (stage) {
	case MK_STAGE0_DISPLAY_FAIL_NONE:
		uart_puts_all("none");
		return;
	case MK_STAGE0_DISPLAY_FAIL_HOST_INIT:
		uart_puts_all("host-init");
		return;
	case MK_STAGE0_DISPLAY_FAIL_INIT_TABLE:
		uart_puts_all("init-table");
		return;
	case MK_STAGE0_DISPLAY_FAIL_POST_BRIGHTNESS:
		uart_puts_all("post-brightness");
		return;
	case MK_STAGE0_DISPLAY_FAIL_BIAS_I2C:
		uart_puts_all("bias-i2c");
		return;
	default:
		uart_puts_all("unknown");
		return;
	}
}

static uint32_t resolve_device_serial_from_fdt(const void *fdt, char *dst, uint32_t dst_cap)
{
	const char *serial;
	const char *bootargs;
	uint32_t copied = 0;

	if (fdt == 0 || dst == 0 || dst_cap < 2U) {
		return 0;
	}
	dst[0] = '\0';

	serial = mk_fdt_find_chosen_string(fdt, "serial-number");
	if (serial != 0) {
		copied = copy_serial_token(serial, dst, dst_cap);
		if (copied != 0U) {
			return copied;
		}
	}

	bootargs = mk_fdt_find_chosen_string(fdt, "bootargs");
	copied = parse_android_serial_from_bootargs(bootargs, dst, dst_cap);
	return copied;
}

/* Button/keypad, menu rendering, OVL, and splash functions moved to mk_ui.c */


static uint32_t dcache_line_size(void)
{
	uint64_t ctr_el0;
	uint32_t words;

	__asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr_el0));
	words = 4U << (uint32_t) ((ctr_el0 >> 16) & 0xfU);
	if (words == 0) {
		return 64U;
	}
	return words;
}

void clean_dcache_range(uintptr_t start, uint64_t len)
{
	uint32_t line;
	uintptr_t p;
	uintptr_t end;

	if (len == 0) {
		return;
	}

	line = dcache_line_size();
	p = start & ~((uintptr_t) line - 1U);
	end = start + (uintptr_t) len;

	for (; p < end; p += line) {
		__asm__ volatile("dc cvac, %0" : : "r"(p) : "memory");
	}
	__asm__ volatile("dsb sy");
	__asm__ volatile("isb");
}


uint64_t read_cntfrq_el0(void)
{
	uint64_t v;

	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
	return v;
}

uint64_t read_cntpct_el0(void)
{
	uint64_t v;

	__asm__ volatile("mrs %0, cntpct_el0" : "=r"(v));
	return v;
}

void delay_ms_calibrated(uint32_t ms)
{
	uint64_t freq;
	uint64_t start;
	uint64_t target_ticks;

	if (ms == 0U) {
		return;
	}

	freq = read_cntfrq_el0();
	if (freq == 0U) {
		return;
	}

	start = read_cntpct_el0();
	target_ticks = (freq * (uint64_t) ms) / 1000ULL;

	while ((read_cntpct_el0() - start) < target_ticks) {
		pet_wdt();
		__asm__ volatile("");
	}
}

/* MTK uart (mt6577-compatible): 16550 register layout with reg-shift=2. */
#define MK_UART_THR_OFF 0x00U
#define MK_UART_LSR_OFF 0x14U
#define MK_UART_LSR_THRE 0x20U
#define MK_UART0_BASE 0x11002000ULL
#define MK_UART1_BASE 0x11003000ULL
#define MK_ENABLE_UART_LOG 1

static void uart_putc_one(uint64_t base, char c)
{
#if MK_ENABLE_UART_LOG
	uint32_t i;
	uint32_t lsr;

	for (i = 0; i < 100000U; i++) {
		lsr = mmio_read32(base + MK_UART_LSR_OFF);
		if ((lsr & MK_UART_LSR_THRE) != 0U) {
			break;
		}
	}

	/* Use 32-bit MMIO for MTK APB uart regs. */
	mmio_write32(base + MK_UART_THR_OFF, (uint32_t) (uint8_t) c);
#else
	(void) base;
	(void) c;
#endif
}

static void uart_putc_all(char c)
{
	uart_putc_one(MK_UART0_BASE, c);
}

void uart_puts_all(const char *s)
{
	uint32_t i = 0;

	if (s == 0) {
		return;
	}
	while (s[i] != '\0') {
		uart_putc_all(s[i]);
		i++;
	}
}

void uart_puthex64_all(uint64_t v)
{
	static const char hex[] = "0123456789abcdef";
	int i;

	for (i = 15; i >= 0; i--) {
		uint8_t n = (uint8_t) ((v >> ((uint32_t) i * 4U)) & 0x0fU);
		uart_putc_all(hex[n]);
	}
}

static int wait_for_mask_clear(uint64_t addr, uint32_t mask, uint32_t max_iters)
{
	uint32_t i;

	for (i = 0; i < max_iters; i++) {
		if ((mmio_read32(addr) & mask) == 0U) {
			return 1;
		}
	}
	return 0;
}

static int wait_for_mask_set(uint64_t addr, uint32_t mask, uint32_t max_iters)
{
	uint32_t i;

	for (i = 0; i < max_iters; i++) {
		if ((mmio_read32(addr) & mask) != 0U) {
			return 1;
		}
	}
	return 0;
}

static void msdc0_reset_host(void)
{
	uint64_t base = MTK_MSDC0_BASE;

	mmio_write32(base + MSDC_CFG, mmio_read32(base + MSDC_CFG) | MSDC_CFG_RST);
	(void) wait_for_mask_clear(base + MSDC_CFG, MSDC_CFG_RST, 200000U);
	mmio_write32(base + EMMC51_CFG0, mmio_read32(base + EMMC51_CFG0) & ~MSDC_EMMC51_CFG_CMDQEN);
	mmio_write32(base + MSDC_INT, 0xffffffffU);
	mmio_write32(base + MSDC_FIFOCS, MSDC_FIFOCS_CLR);
	(void) wait_for_mask_clear(base + MSDC_FIFOCS, MSDC_FIFOCS_CLR, 200000U);
}

void mk_stage0_msdc_snapshot_lk_state(void)
{
	uint64_t base = MTK_MSDC0_BASE;

	g_lk_msdc_cfg   = mmio_read32(base + MSDC_CFG);
	g_lk_sdc_cfg    = mmio_read32(base + SDC_CFG);
	g_lk_emmc51_cfg0 = mmio_read32(base + EMMC51_CFG0);
	g_lk_msdc_saved  = 1;

	uart_puts_all("[mk] msdc snapshot: cfg=0x");
	uart_puthex64_all((uint64_t) g_lk_msdc_cfg);
	uart_puts_all(" sdc_cfg=0x");
	uart_puthex64_all((uint64_t) g_lk_sdc_cfg);
	uart_puts_all(" emmc51=0x");
	uart_puthex64_all((uint64_t) g_lk_emmc51_cfg0);
	uart_puts_all("\r\n");
}

void mk_stage0_msdc_restore_for_linux(void)
{
	uint64_t base = MTK_MSDC0_BASE;

	uart_puts_all("[mk] msdc restore begin\r\n");
	msdc0_reset_host();

	if (g_lk_msdc_saved) {
		/*
		 * Restore the MSDC host registers to the state LK left them.
		 * The kernel msdc driver resumes from this state; if MK's
		 * eMMC operations changed clock dividers, bus width, or PIO
		 * mode the driver fails to reinitialize the card.
		 */
		mmio_write32(base + MSDC_CFG,   g_lk_msdc_cfg);
		mmio_write32(base + SDC_CFG,    g_lk_sdc_cfg);
		mmio_write32(base + EMMC51_CFG0, g_lk_emmc51_cfg0);

		/* Wait for clock stable after restoring MSDC_CFG. */
		{
			uint32_t i;
			for (i = 0U; i < 200000U; i++) {
				if ((mmio_read32(base + MSDC_CFG) & MSDC_CFG_CKSTB) != 0U) {
					break;
				}
			}
		}
	}

	uart_puts_all("[mk] msdc restore: cfg=0x");
	uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_CFG));
	uart_puts_all(" sdc_cfg=0x");
	uart_puthex64_all((uint64_t) mmio_read32(base + SDC_CFG));
	uart_puts_all(" emmc51=0x");
	uart_puthex64_all((uint64_t) mmio_read32(base + EMMC51_CFG0));
	uart_puts_all("\r\n");
}

static int msdc0_send_cmd_only(uint32_t opcode, uint32_t arg)
{
	uint64_t base = MTK_MSDC0_BASE;
	uint32_t rawcmd;

	if (!wait_for_mask_clear(base + SDC_STS, SDC_STS_SDCBUSY | SDC_STS_CMDBUSY, 200000U)) {
		msdc0_reset_host();
		if (!wait_for_mask_clear(base + SDC_STS, SDC_STS_SDCBUSY | SDC_STS_CMDBUSY, 200000U)) {
			return 0;
		}
	}

	mmio_write32(base + MSDC_INT, 0xffffffffU);
	mmio_write32(base + MSDC_FIFOCS, MSDC_FIFOCS_CLR);
	if (!wait_for_mask_clear(base + MSDC_FIFOCS, MSDC_FIFOCS_CLR, 200000U)) {
		msdc0_reset_host();
		return 0;
	}
	mmio_write32(base + EMMC51_CFG0, mmio_read32(base + EMMC51_CFG0) & ~MSDC_EMMC51_CFG_CMDQEN);

	mmio_write32(base + MSDC_CFG, mmio_read32(base + MSDC_CFG) | MSDC_CFG_PIO);
	mmio_write32(base + SDC_CFG,
		     (mmio_read32(base + SDC_CFG) & ~SDC_CFG_BUSWIDTH_MASK) | SDC_CFG_BUSWIDTH_8BIT);
	mmio_write32(base + SDC_BLK_NUM, 0U);
	mmio_write32(base + SDC_ARG, arg);

	rawcmd = MMC_RAWCMD_NODATA(opcode, MMC_RSP_R1);
	mmio_write32(base + SDC_CMD, rawcmd);

	if (!wait_for_mask_set(base + MSDC_INT, MSDC_INT_CMD_MASK, 400000U)) {
		uart_puts_all("[mk] msdc: cmd-only timeout op=0x");
		uart_puthex64_all((uint64_t) opcode);
		uart_puts_all(" int=0x");
		uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_INT));
		uart_puts_all("\r\n");
		msdc0_reset_host();
		return 0;
	}
	if ((mmio_read32(base + MSDC_INT) & (MSDC_INT_CMDTMO | MSDC_INT_RSPCRCERR)) != 0U) {
		uart_puts_all("[mk] msdc: cmd-only error op=0x");
		uart_puthex64_all((uint64_t) opcode);
		uart_puts_all(" int=0x");
		uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_INT));
		uart_puts_all("\r\n");
		mmio_write32(base + MSDC_INT, MSDC_INT_CMD_MASK);
		msdc0_reset_host();
		return 0;
	}
	mmio_write32(base + MSDC_INT, MSDC_INT_CMD_MASK);
	return 1;
}

/*
 * Like msdc0_send_cmd_only but with an explicit response type.
 * For resp_type == 0 (R0/no response): CMD0 sends no response from the card;
 * the MSDC fires CMDTMO as expected.  We treat that as success.
 * out_resp0 may be NULL if the response value is not needed.
 */
static int msdc0_send_cmd_raw(uint32_t opcode, uint32_t arg, uint32_t resp_type,
			      uint32_t *out_resp0)
{
	uint64_t base = MTK_MSDC0_BASE;
	uint32_t rawcmd;

	if (!wait_for_mask_clear(base + SDC_STS, SDC_STS_SDCBUSY | SDC_STS_CMDBUSY, 200000U)) {
		msdc0_reset_host();
		if (!wait_for_mask_clear(base + SDC_STS, SDC_STS_SDCBUSY | SDC_STS_CMDBUSY, 200000U)) {
			return 0;
		}
	}

	mmio_write32(base + MSDC_INT, 0xffffffffU);
	mmio_write32(base + MSDC_FIFOCS, MSDC_FIFOCS_CLR);
	if (!wait_for_mask_clear(base + MSDC_FIFOCS, MSDC_FIFOCS_CLR, 200000U)) {
		msdc0_reset_host();
		return 0;
	}
	mmio_write32(base + EMMC51_CFG0, mmio_read32(base + EMMC51_CFG0) & ~MSDC_EMMC51_CFG_CMDQEN);
	mmio_write32(base + MSDC_CFG, mmio_read32(base + MSDC_CFG) | MSDC_CFG_PIO);
	mmio_write32(base + SDC_CFG,
		     (mmio_read32(base + SDC_CFG) & ~SDC_CFG_BUSWIDTH_MASK) | SDC_CFG_BUSWIDTH_8BIT);
	mmio_write32(base + SDC_BLK_NUM, 0U);
	mmio_write32(base + SDC_ARG, arg);

	rawcmd = MMC_RAWCMD_NODATA(opcode, resp_type);
	mmio_write32(base + SDC_CMD, rawcmd);

	if (!wait_for_mask_set(base + MSDC_INT, MSDC_INT_CMD_MASK, 400000U)) {
		uart_puts_all("[mk] msdc: cmd-raw timeout op=0x");
		uart_puthex64_all((uint64_t) opcode);
		uart_puts_all("\r\n");
		msdc0_reset_host();
		return 0;
	}

	/* For R0: CMDTMO is expected (no response from card). */
	if (resp_type != 0U && (mmio_read32(base + MSDC_INT) & MSDC_INT_CMDTMO) != 0U) {
		uart_puts_all("[mk] msdc: cmd-raw tmo op=0x");
		uart_puthex64_all((uint64_t) opcode);
		uart_puts_all("\r\n");
		mmio_write32(base + MSDC_INT, MSDC_INT_CMD_MASK);
		msdc0_reset_host();
		return 0;
	}

	if (out_resp0 != 0) {
		*out_resp0 = mmio_read32(base + SDC_RESP0);
	}
	mmio_write32(base + MSDC_INT, MSDC_INT_CMD_MASK);
	return 1;
}

static int msdc0_wait_card_ready_timeout(uint32_t max_polls)
{
	uint64_t base = MTK_MSDC0_BASE;
	uint32_t rawcmd;
	uint32_t i;
	uint32_t last_r1 = 0U;
	uint32_t saw_wp = 0U;

	if (max_polls == 0U) {
		max_polls = 2000U;
	}

	for (i = 0U; i < max_polls; i++) {
		if (!wait_for_mask_clear(base + SDC_STS, SDC_STS_SDCBUSY | SDC_STS_CMDBUSY, 200000U)) {
			msdc0_reset_host();
			return 0;
		}

		mmio_write32(base + MSDC_INT, 0xffffffffU);
		mmio_write32(base + SDC_BLK_NUM, 0U);
		mmio_write32(base + SDC_ARG, (uint32_t) (1U << 16));

		rawcmd = MMC_RAWCMD_NODATA(MMC_CMD13_SEND_STATUS, MMC_RSP_R1);
		mmio_write32(base + SDC_CMD, rawcmd);

		if (!wait_for_mask_set(base + MSDC_INT, MSDC_INT_CMD_MASK, 400000U)) {
			uart_puts_all("[mk] msdc: status cmd timeout int=0x");
			uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_INT));
			uart_puts_all("\r\n");
			msdc0_reset_host();
			return 0;
		}
		if ((mmio_read32(base + MSDC_INT) & (MSDC_INT_CMDTMO | MSDC_INT_RSPCRCERR)) != 0U) {
			uart_puts_all("[mk] msdc: status cmd error int=0x");
			uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_INT));
			uart_puts_all("\r\n");
			mmio_write32(base + MSDC_INT, MSDC_INT_CMD_MASK);
			msdc0_reset_host();
			return 0;
		}
		mmio_write32(base + MSDC_INT, MSDC_INT_CMD_MASK);

		{
			uint32_t r1 = mmio_read32(base + SDC_RESP0);
			uint32_t ready = (r1 >> 8) & 1U;
			uint32_t state = (r1 >> 9) & 0xfU;
			uint32_t errors = r1 & 0xf9ffe008U;

			last_r1 = r1;
			if ((r1 & R1_WP_VIOLATION) != 0U) {
				saw_wp = 1U;
			}

			if (errors != 0U) {
				uart_puts_all("[mk] msdc: status error r1=0x");
				uart_puthex64_all((uint64_t) r1);
				uart_puts_all("\r\n");
				return 0;
			}
			if (ready != 0U && state == 4U) {
				if (saw_wp != 0U) {
					uart_puts_all("[mk] msdc: status ok-with-wp r1=0x");
					uart_puthex64_all((uint64_t) last_r1);
					uart_puts_all("\r\n");
				}
				return 1;
			}
		}

		pet_wdt();
	}

	if (saw_wp != 0U) {
		uart_puts_all("[mk] msdc: status wp-violation final r1=0x");
		uart_puthex64_all((uint64_t) last_r1);
		uart_puts_all("\r\n");
	}
	uart_puts_all("[mk] msdc: status ready timeout\r\n");
	return 0;
}

static int msdc0_wait_card_ready(void)
{
	return msdc0_wait_card_ready_timeout(2000U);
}

static uint32_t msdc0_extcsd_cmd6_timeout_ms(const uint8_t *extcsd)
{
	uint32_t timeout_ms = (uint32_t) extcsd[EXT_CSD_GENERIC_CMD6_TIME] * 10U;

	if (timeout_ms == 0U) {
		timeout_ms = DEFAULT_CMD6_TIMEOUT_MS;
	}

	return timeout_ms;
}

static int msdc0_switch_extcsd_byte(uint8_t index, uint8_t value, uint32_t max_polls)
{
	uint32_t arg = (3U << 24) | ((uint32_t) index << 16) | ((uint32_t) value << 8);

	uart_puts_all("[mk] msdc: cmd6 switch idx=0x");
	uart_puthex64_all((uint64_t) index);
	uart_puts_all(" val=0x");
	uart_puthex64_all((uint64_t) value);
	uart_puts_all(" polls=0x");
	uart_puthex64_all((uint64_t) max_polls);
	uart_puts_all("\r\n");

	if (!msdc0_send_cmd_only(MMC_CMD6_SWITCH, arg)) {
		uart_puts_all("[mk] msdc: cmd6 switch failed idx=0x");
		uart_puthex64_all((uint64_t) index);
		uart_puts_all(" val=0x");
		uart_puthex64_all((uint64_t) value);
		uart_puts_all("\r\n");
		return 0;
	}
	uart_puts_all("[mk] msdc: cmd6 sent\r\n");

	if (!wait_for_mask_clear(MTK_MSDC0_BASE + SDC_STS, SDC_STS_SDCBUSY | SDC_STS_CMDBUSY, 400000U)) {
		uart_puts_all("[mk] msdc: cmd6 busy clear failed idx=0x");
		uart_puthex64_all((uint64_t) index);
		uart_puts_all("\r\n");
		msdc0_reset_host();
		return 0;
	}
	uart_puts_all("[mk] msdc: cmd6 busy clear ok\r\n");

	if (!msdc0_wait_card_ready_timeout(max_polls)) {
		uart_puts_all("[mk] msdc: cmd6 ready timeout idx=0x");
		uart_puthex64_all((uint64_t) index);
		uart_puts_all("\r\n");
		return 0;
	}
	uart_puts_all("[mk] msdc: cmd6 ready ok\r\n");

	return 1;
}

static int msdc0_read_extcsd(uint8_t *out512)
{
	uint64_t base = MTK_MSDC0_BASE;
	uint32_t rawcmd;
	uint32_t i;

	if (out512 == 0) {
		return 0;
	}

	uart_puts_all("[mk] msdc: extcsd read begin\r\n");

	if (!wait_for_mask_clear(base + SDC_STS, SDC_STS_SDCBUSY | SDC_STS_CMDBUSY, 200000U)) {
		uart_puts_all("[mk] msdc: extcsd prebusy, resetting host\r\n");
		msdc0_reset_host();
		if (!wait_for_mask_clear(base + SDC_STS, SDC_STS_SDCBUSY | SDC_STS_CMDBUSY, 200000U)) {
			return 0;
		}
	}

	mmio_write32(base + MSDC_INT, 0xffffffffU);
	mmio_write32(base + MSDC_FIFOCS, MSDC_FIFOCS_CLR);
	if (!wait_for_mask_clear(base + MSDC_FIFOCS, MSDC_FIFOCS_CLR, 200000U)) {
		msdc0_reset_host();
		return 0;
	}
	mmio_write32(base + EMMC51_CFG0, mmio_read32(base + EMMC51_CFG0) & ~MSDC_EMMC51_CFG_CMDQEN);

	mmio_write32(base + MSDC_CFG, mmio_read32(base + MSDC_CFG) | MSDC_CFG_PIO);
	mmio_write32(base + SDC_CFG,
		     (mmio_read32(base + SDC_CFG) & ~SDC_CFG_BUSWIDTH_MASK) | SDC_CFG_BUSWIDTH_8BIT);
	mmio_write32(base + SDC_BLK_NUM, 1U);
	mmio_write32(base + SDC_ARG, 0U);

	rawcmd = MMC_RAWCMD_READ(MMC_CMD8_SEND_EXT_CSD, MMC_RSP_R1, 512U);
	mmio_write32(base + SDC_CMD, rawcmd);

	if (!wait_for_mask_set(base + MSDC_INT, MSDC_INT_CMD_MASK, 400000U)) {
		uart_puts_all("[mk] msdc: extcsd cmd timeout int=0x");
		uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_INT));
		uart_puts_all("\r\n");
		msdc0_reset_host();
		return 0;
	}
	if ((mmio_read32(base + MSDC_INT) & (MSDC_INT_CMDTMO | MSDC_INT_RSPCRCERR)) != 0U) {
		uart_puts_all("[mk] msdc: extcsd cmd error int=0x");
		uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_INT));
		uart_puts_all("\r\n");
		mmio_write32(base + MSDC_INT, MSDC_INT_CMD_MASK);
		msdc0_reset_host();
		return 0;
	}
	mmio_write32(base + MSDC_INT, MSDC_INT_CMD_MASK);
	uart_puts_all("[mk] msdc: extcsd cmd complete\r\n");

	for (i = 0; i < 128U; i++) {
		uint32_t words;
		uint32_t v;
		uint32_t spin = 0U;

		do {
			words = mmio_read32(base + MSDC_FIFOCS) & MSDC_FIFOCS_RXCNT_MASK;
			if (words != 0U) {
				break;
			}
			if ((mmio_read32(base + MSDC_INT) & (MSDC_INT_DATTMO | MSDC_INT_DATCRCERR)) != 0U) {
				uart_puts_all("[mk] msdc: extcsd data error int=0x");
				uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_INT));
				uart_puts_all("\r\n");
				mmio_write32(base + MSDC_INT, MSDC_INT_DATA_MASK);
				msdc0_reset_host();
				return 0;
			}
			if ((spin++ & 0x3fffU) == 0U) {
				pet_wdt();
			}
		} while (spin < 400000U);
		if (words == 0U) {
			uart_puts_all("[mk] msdc: extcsd rx timeout int=0x");
			uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_INT));
			uart_puts_all(" fifocs=0x");
			uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_FIFOCS));
			uart_puts_all("\r\n");
			msdc0_reset_host();
			return 0;
		}

		v = mmio_read32(base + MSDC_RXDATA);
		out512[i * 4U + 0U] = (uint8_t) (v & 0xffU);
		out512[i * 4U + 1U] = (uint8_t) ((v >> 8) & 0xffU);
		out512[i * 4U + 2U] = (uint8_t) ((v >> 16) & 0xffU);
		out512[i * 4U + 3U] = (uint8_t) ((v >> 24) & 0xffU);
	}

	if (!wait_for_mask_set(base + MSDC_INT, MSDC_INT_DATA_MASK, 800000U)) {
		uart_puts_all("[mk] msdc: extcsd data done timeout int=0x");
		uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_INT));
		uart_puts_all("\r\n");
		msdc0_reset_host();
		return 0;
	}
	if ((mmio_read32(base + MSDC_INT) & (MSDC_INT_DATTMO | MSDC_INT_DATCRCERR)) != 0U) {
		uart_puts_all("[mk] msdc: extcsd data done error int=0x");
		uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_INT));
		uart_puts_all("\r\n");
		mmio_write32(base + MSDC_INT, MSDC_INT_DATA_MASK);
		msdc0_reset_host();
		return 0;
	}
	mmio_write32(base + MSDC_INT, MSDC_INT_DATA_MASK);
	uart_puts_all("[mk] msdc: extcsd read complete\r\n");
	return 1;
}

static int msdc0_select_user_area(void)
{
	uint8_t extcsd[512];
	uint8_t partcfg;
	uint32_t cmd6_polls;

	if (!msdc0_read_extcsd(extcsd)) {
		uart_puts_all("[mk] msdc: extcsd read failed\r\n");
		return 0;
	}

	cmd6_polls = msdc0_extcsd_cmd6_timeout_ms(extcsd) * 4U;
	if (cmd6_polls < 2000U) {
		cmd6_polls = 2000U;
	}

	uart_puts_all("[mk] msdc: cmdq=0x");
	uart_puthex64_all((uint64_t) extcsd[EXT_CSD_CMDQ_MODE_EN]);
	uart_puts_all("\r\n");
	if ((extcsd[EXT_CSD_CMDQ_MODE_EN] & 0x1U) != 0U) {
		uart_puts_all("[mk] msdc: disabling cmdq\r\n");
		if (!msdc0_switch_extcsd_byte(EXT_CSD_CMDQ_MODE_EN, 0U, cmd6_polls)) {
			return 0;
		}
	}

	/* EXT_CSD[171] USER_WP: bit 0 = US_TEMP_WP_EN (entire user area temporarily WP'd),
	 * bit 2 = US_PWR_WP_EN (power-on WP mode for CMD28 groups).
	 * Oppo LK sets one of these before jumping to MK; CMD0 alone does not clear
	 * them on this Micron eMMC (Micron deviates from JEDEC on Power-On WP).
	 * Writing 0x00 is safe: OTP/DIS bits (6, 7) ignore writes of 0. */
	{
		uint8_t user_wp = extcsd[EXT_CSD_USER_WP];
		uart_puts_all("[mk] msdc: user_wp=0x");
		uart_puthex64_all((uint64_t) user_wp);
		uart_puts_all("\r\n");
		if ((user_wp & 0x05U) != 0U) {
			uart_puts_all("[mk] msdc: clearing user_wp\r\n");
			if (!msdc0_switch_extcsd_byte(EXT_CSD_USER_WP, 0x00U, cmd6_polls)) {
				uart_puts_all("[mk] msdc: user_wp clear failed (non-fatal)\r\n");
			}
		}
	}

	partcfg = extcsd[EXT_CSD_PARTITION_CONFIG];
	uart_puts_all("[mk] msdc: partcfg=0x");
	uart_puthex64_all((uint64_t) partcfg);
	uart_puts_all("\r\n");
	if ((partcfg & 0x7U) == 0U) {
		return 1;
	}

	partcfg &= (uint8_t) ~0x7U;
	return msdc0_switch_extcsd_byte(EXT_CSD_PARTITION_CONFIG, partcfg, cmd6_polls);
}

/*
 * Issue CMD0 (GO_IDLE_STATE) to clear all write protection — both Temporary
 * and Power-On WP — then re-initialize the eMMC back to Transfer state.
 *
 * Oppo LK applies Power-On WP via CMD28 (with USER_WP[US_PWR_WP_EN] set)
 * on the boot partition before jumping to MK.  CMD29 cannot clear Power-On
 * WP (it returns WP_VIOLATION).  Only CMD0 clears it.  Linux does this
 * automatically via mmc_power_cycle() → CMD0 in every MMC init.
 *
 * After CMD0 the card is in Idle state; the full identification sequence
 * CMD1 → CMD2 → CMD3 → CMD7 brings it back to Transfer state.
 */
static int msdc0_go_idle_reinit(void)
{
	uint64_t base = MTK_MSDC0_BASE;
	uint32_t ocr;
	uint32_t cfg;
	uint32_t i;

	/*
	 * Slow the MSDC clock before CMD0.  After CMD0 the card is in default
	 * speed mode (max 26 MHz SDR).  LK configured HS200 (~200 MHz), which
	 * causes MSDC_INT_DATCRCERR on any subsequent data transfer.
	 * CKMOD=0 (SDR divider), CKDIV=7 → source_clk/16 ≈ 12 MHz at 200 MHz.
	 * This speed is safe for CMD0 and the init sequence that follows.
	 * After bus width is restored we switch the card to HS52 (EXT_CSD[185]=1)
	 * and raise the host clock to ~50 MHz (CKDIV=1).  Without this restore,
	 * a 230 KB flash takes ~2 s at 12 MHz instead of ~100 ms at 50 MHz.
	 */
	cfg = mmio_read32(base + MSDC_CFG);
	uart_puts_all("[mk] msdc: go_idle: msdc_cfg=0x");
	uart_puthex64_all((uint64_t) cfg);
	uart_puts_all("\r\n");
	/* Clear ALL bits above [7:0] — this removes HS400 sampling-mode bits 25, 21, 18,
	 * [17:16] as well as the normal CKDIV/CKMOD fields.  Clearing only CKMOD+CKDIV
	 * left bits 21 and 25 set, which kept the HS400 data-path active even at 12 MHz
	 * and caused DAT CRC errors after CMD0. */
	cfg = (cfg & MSDC_CFG_LOWER_MASK) | MSDC_CFG_CKDIV_SLOW;
	mmio_write32(base + MSDC_CFG, cfg);
	for (i = 0U; i < 200000U; i++) {
		if ((mmio_read32(base + MSDC_CFG) & MSDC_CFG_CKSTB) != 0U) {
			break;
		}
	}
	uart_puts_all("[mk] msdc: go_idle: clock slowed cfg=0x");
	uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_CFG));
	uart_puts_all("\r\n");

	uart_puts_all("[mk] msdc: go_idle: CMD0 → clearing WP\r\n");

	/* CMD0: no response (R0).  Card → Idle state.  All WP cleared. */
	if (!msdc0_send_cmd_raw(0U, 0U, 0U, 0)) {
		uart_puts_all("[mk] msdc: go_idle: CMD0 send failed\r\n");
		return 0;
	}

	/* Let the card settle after CMD0 (74+ clocks per JESD84). */
	for (i = 0U; i < 10000U; i++) {
		(void) mmio_read32(MTK_MSDC0_BASE + MSDC_CFG);
	}

	uart_puts_all("[mk] msdc: go_idle: CMD1 poll\r\n");

	/*
	 * CMD1 (SEND_OP_COND): poll until OCR bit 31 (not-busy) is set.
	 * Arg = 0x40FF8080: sector addressing + all standard voltage ranges.
	 * Response type = R3 (no CRC on R3, MSDC treats it like R1).
	 */
	/*
	 * Poll CMD1 up to 50000 iterations.  JESD84 requires the card to
	 * assert OCR bit 31 within 1 s of the first CMD1.  At ~20 µs per
	 * iteration (12 MHz CMD line + host overhead) that is ~1 s of coverage.
	 * The previous 1000-iteration limit (~23 ms) was too short when the VCC
	 * cycle is calibrated to the correct 50 ms off + 50 ms on.
	 */
	ocr = 0U;
	for (i = 0U; i < 50000U; i++) {
		if (!msdc0_send_cmd_raw(1U, 0x40FF8080U, 3U, &ocr)) {
			uart_puts_all("[mk] msdc: go_idle: CMD1 failed\r\n");
			return 0;
		}
		if ((ocr & (1U << 31)) != 0U) {
			break;
		}
		pet_wdt();
	}
	if ((ocr & (1U << 31)) == 0U) {
		uart_puts_all("[mk] msdc: go_idle: CMD1 busy timeout\r\n");
		return 0;
	}
	uart_puts_all("[mk] msdc: go_idle: ocr=0x");
	uart_puthex64_all((uint64_t) ocr);
	uart_puts_all("\r\n");

	/* CMD2 (ALL_SEND_CID): R2 response, card → Identification state. */
	if (!msdc0_send_cmd_raw(2U, 0U, 2U, 0)) {
		uart_puts_all("[mk] msdc: go_idle: CMD2 failed\r\n");
		return 0;
	}

	/* CMD3 (SET_RELATIVE_ADDR): assign RCA=1, card → Standby state. */
	if (!msdc0_send_cmd_raw(3U, 0x00010000U, 1U, 0)) {
		uart_puts_all("[mk] msdc: go_idle: CMD3 failed\r\n");
		return 0;
	}

	/* CMD7 (SELECT_CARD): select RCA=1, card → Transfer state. */
	if (!msdc0_send_cmd_raw(7U, 0x00010000U, 7U, 0)) {
		uart_puts_all("[mk] msdc: go_idle: CMD7 failed\r\n");
		return 0;
	}

	uart_puts_all("[mk] msdc: go_idle: card in Transfer\r\n");

	/*
	 * Restore 8-bit bus width on the card side.
	 * CMD0 resets the card to 1-bit; the MSDC host is hardcoded 8-bit.
	 * EXT_CSD[183] BUS_WIDTH: value 2 = 8-bit.
	 */
	if (!msdc0_switch_extcsd_byte(EXT_CSD_BUS_WIDTH, 2U, 2000U)) {
		uart_puts_all("[mk] msdc: go_idle: bus-width restore failed\r\n");
		return 0;
	}

	/*
	 * Switch card to High Speed mode (HS_TIMING=1) then raise host clock
	 * to CKDIV=1 (~50 MHz).  Must set card side first, then host clock.
	 * HS52 does not require tuning (unlike HS200/HS400).
	 */
	if (!msdc0_switch_extcsd_byte(EXT_CSD_HS_TIMING, 0x01U, 2000U)) {
		uart_puts_all("[mk] msdc: go_idle: hs_timing switch failed (non-fatal)\r\n");
	} else {
		uint32_t hs_cfg = (mmio_read32(base + MSDC_CFG) & MSDC_CFG_LOWER_MASK) | MSDC_CFG_CKDIV_HS52;
		mmio_write32(base + MSDC_CFG, hs_cfg);
		for (i = 0U; i < 200000U; i++) {
			if ((mmio_read32(base + MSDC_CFG) & MSDC_CFG_CKSTB) != 0U) {
				break;
			}
		}
		uart_puts_all("[mk] msdc: go_idle: clock HS52 cfg=0x");
		uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_CFG));
		uart_puts_all("\r\n");
	}

	/* Restore CMDQ=off and PARTITION_CONFIG=user area. */
	if (!msdc0_select_user_area()) {
		uart_puts_all("[mk] msdc: go_idle: select user area failed\r\n");
		return 0;
	}

	/*
	 * Re-enable write cache.  CMD0 resets EXT_CSD to defaults; CACHE_CTRL[33]
	 * bit0 (CACHE_EN) defaults to 0 (disabled).  Without cache, the eMMC
	 * programs NAND inline per-sector during CMD25 (~4 ms/sector → ~2 s for
	 * 484 sectors).  With cache enabled, data is buffered in the card's DRAM
	 * and NAND programming is deferred to the FLUSH_CACHE command already
	 * issued at the end of every flash operation.
	 * EXT_CSD[33] CACHE_CTRL: bit 0 = CACHE_EN.
	 */
	if (!msdc0_switch_extcsd_byte(EXT_CSD_CACHE_CTRL, 0x01U, 2000U)) {
		uart_puts_all("[mk] msdc: go_idle: cache enable failed (non-fatal)\r\n");
	} else {
		uart_puts_all("[mk] msdc: go_idle: cache enabled\r\n");
	}

	uart_puts_all("[mk] msdc: go_idle: reinit complete\r\n");
	return 1;
}

static int msdc0_flush_cache(void)
{
	uint8_t extcsd[512];
	uint32_t cmd6_polls;

	uart_puts_all("[mk] msdc: flush cache begin\r\n");
	msdc0_reset_host();
	uart_puts_all("[mk] msdc: flush cache host reset\r\n");

	if (!msdc0_read_extcsd(extcsd)) {
		uart_puts_all("[mk] msdc: flush extcsd read failed\r\n");
		return 0;
	}
	uart_puts_all("[mk] msdc: flush extcsd read ok\r\n");

	cmd6_polls = 2000U;
	uart_puts_all("[mk] msdc: flush cache switch begin\r\n");

	if (!msdc0_switch_extcsd_byte(EXT_CSD_FLUSH_CACHE, 1U, cmd6_polls)) {
		uart_puts_all("[mk] msdc: flush cache switch failed\r\n");
		return 0;
	}

	uart_puts_all("[mk] msdc: flush cache done\r\n");
	return 1;
}

/*
 * Clear all write protection on the eMMC by issuing CMD0 (GO_IDLE_STATE)
 * and re-initializing the card.  CMD0 clears both Temporary and Power-On WP.
 *
 * The start_lba / sector_count parameters are kept for API compatibility
 * but are not used — CMD0 clears WP globally.
 */
static void msdc0_clr_write_prot_range(uint64_t start_lba, uint64_t sector_count)
{
	(void) start_lba;
	(void) sector_count;
	(void) msdc0_go_idle_reinit();
}

static int guid_zero_16(const uint8_t *p)
{
	uint32_t i;

	for (i = 0; i < 16U; i++) {
		if (p[i] != 0U) {
			return 0;
		}
	}
	return 1;
}

static int bytes_equal(const uint8_t *a, const uint8_t *b, uint32_t len)
{
	uint32_t i;

	for (i = 0; i < len; i++) {
		if (a[i] != b[i]) {
			return 0;
		}
	}
	return 1;
}

static int gpt_entry_name_equals(const uint8_t *entry, const char *label)
{
	uint32_t i;

	if (entry == 0 || label == 0 || label[0] == '\0') {
		return 0;
	}

	for (i = 0; i < 36U; i++) {
		uint8_t lo = entry[56U + (i * 2U)];
		uint8_t hi = entry[56U + (i * 2U) + 1U];
		char c = label[i];

		if (hi != 0U) {
			return 0;
		}
		if (c == '\0') {
			return lo == 0U;
		}
		if ((uint8_t) c != lo) {
			return 0;
		}
	}

	return label[36] == '\0';
}

static int msdc0_read_sector(uint64_t lba, uint8_t *out512)
{
	uint64_t base = MTK_MSDC0_BASE;
	uint32_t rawcmd;
	uint32_t i;

	if (out512 == 0 || (lba >> 32) != 0U) {
		return 0;
	}

	if (!wait_for_mask_clear(base + SDC_STS, SDC_STS_SDCBUSY | SDC_STS_CMDBUSY, 200000U)) {
		msdc0_reset_host();
		if (!wait_for_mask_clear(base + SDC_STS, SDC_STS_SDCBUSY | SDC_STS_CMDBUSY, 200000U)) {
			return 0;
		}
	}

	mmio_write32(base + MSDC_INT, 0xffffffffU);
	mmio_write32(base + MSDC_FIFOCS, MSDC_FIFOCS_CLR);
	if (!wait_for_mask_clear(base + MSDC_FIFOCS, MSDC_FIFOCS_CLR, 200000U)) {
		msdc0_reset_host();
		return 0;
	}

	mmio_write32(base + MSDC_CFG, mmio_read32(base + MSDC_CFG) | MSDC_CFG_PIO);
	mmio_write32(base + SDC_CFG,
		     (mmio_read32(base + SDC_CFG) & ~SDC_CFG_BUSWIDTH_MASK) | SDC_CFG_BUSWIDTH_8BIT);
	mmio_write32(base + SDC_BLK_NUM, 1U);
	mmio_write32(base + SDC_ARG, (uint32_t) lba);

	rawcmd = MMC_RAWCMD_READ(MMC_CMD17_READ_SINGLE_BLOCK, MMC_RSP_R1, 512U);
	mmio_write32(base + SDC_CMD, rawcmd);

	if (!wait_for_mask_set(base + MSDC_INT, MSDC_INT_CMD_MASK, 400000U)) {
		msdc0_reset_host();
		return 0;
	}
	if ((mmio_read32(base + MSDC_INT) & (MSDC_INT_CMDTMO | MSDC_INT_RSPCRCERR)) != 0U) {
		mmio_write32(base + MSDC_INT, MSDC_INT_CMD_MASK);
		msdc0_reset_host();
		return 0;
	}
	mmio_write32(base + MSDC_INT, MSDC_INT_CMD_MASK);

	for (i = 0; i < 128U; i++) {
		uint32_t words;
		uint32_t v;
		uint32_t spin = 0U;

		do {
			words = mmio_read32(base + MSDC_FIFOCS) & MSDC_FIFOCS_RXCNT_MASK;
			if (words != 0U) {
				break;
			}
			if ((mmio_read32(base + MSDC_INT) & (MSDC_INT_DATTMO | MSDC_INT_DATCRCERR)) != 0U) {
				mmio_write32(base + MSDC_INT, MSDC_INT_DATA_MASK);
				msdc0_reset_host();
				return 0;
			}
			if ((spin++ & 0x3fffU) == 0U) {
				pet_wdt();
			}
		} while (spin < 400000U);
		if (words == 0U) {
			msdc0_reset_host();
			return 0;
		}

		v = mmio_read32(base + MSDC_RXDATA);
		out512[i * 4U + 0U] = (uint8_t) (v & 0xffU);
		out512[i * 4U + 1U] = (uint8_t) ((v >> 8) & 0xffU);
		out512[i * 4U + 2U] = (uint8_t) ((v >> 16) & 0xffU);
		out512[i * 4U + 3U] = (uint8_t) ((v >> 24) & 0xffU);
	}

	if (!wait_for_mask_set(base + MSDC_INT, MSDC_INT_DATA_MASK, 800000U)) {
		msdc0_reset_host();
		return 0;
	}
	if ((mmio_read32(base + MSDC_INT) & (MSDC_INT_DATTMO | MSDC_INT_DATCRCERR)) != 0U) {
		mmio_write32(base + MSDC_INT, MSDC_INT_DATA_MASK);
		msdc0_reset_host();
		return 0;
	}
	mmio_write32(base + MSDC_INT, MSDC_INT_DATA_MASK);
	return 1;
}

static void msdc0_dma_off(void)
{
	uint64_t base = MTK_MSDC0_BASE;

	mmio_write32(base + MSDC_CFG, mmio_read32(base + MSDC_CFG) | MSDC_CFG_PIO);
}

static void msdc0_dma_on(void)
{
	uint64_t base = MTK_MSDC0_BASE;

	mmio_write32(base + MSDC_CFG, mmio_read32(base + MSDC_CFG) & ~MSDC_CFG_PIO);
}

static void msdc0_dma_stop(void)
{
	uint64_t base = MTK_MSDC0_BASE;
	uint32_t i;

	mmio_write32(base + MSDC_DMA_CTRL, mmio_read32(base + MSDC_DMA_CTRL) | MSDC_DMA_CTRL_STOP);
	for (i = 0U; i < 500000U; i++) {
		if ((mmio_read32(base + MSDC_DMA_CFG) & MSDC_DMA_CFG_STS) == 0U) {
			return;
		}
		if ((i & 0x3fffU) == 0U) {
			pet_wdt();
		}
	}
	uart_puts_all("[mk] msdc: dma stop timeout cfg=0x");
	uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_DMA_CFG));
	uart_puts_all("\r\n");
}

static void msdc0_dma_setup_basic(const void *buf, uint32_t len)
{
	uint64_t base = MTK_MSDC0_BASE;
	uint64_t addr = (uint64_t) (uintptr_t) buf;
	uint32_t ctrl;

	mmio_write32(base + MSDC_DMA_SA_HIGH, (uint32_t) ((addr >> 32) & 0xfU));
	mmio_write32(base + MSDC_DMA_SA, (uint32_t) addr);
	mmio_write32(base + MSDC_DMA_LEN, len);

	ctrl = mmio_read32(base + MSDC_DMA_CTRL);
	ctrl &= ~(MSDC_DMA_CTRL_MODE | MSDC_DMA_CTRL_LASTBUF | MSDC_DMA_CTRL_BRUSTSZ);
	ctrl |= MSDC_DMA_CTRL_LASTBUF | (MSDC_BRUST_64B << 12);
	mmio_write32(base + MSDC_DMA_CTRL, ctrl);
}

static void msdc0_dma_start(void)
{
	uint64_t base = MTK_MSDC0_BASE;

	mmio_write32(base + MSDC_DMA_CTRL, mmio_read32(base + MSDC_DMA_CTRL) | MSDC_DMA_CTRL_START);
}

static int msdc0_write_sector_dma(uint64_t lba, const uint8_t *in512)
{
	uint64_t base = MTK_MSDC0_BASE;
	uint32_t rawcmd;
	uint32_t r1;
	uint32_t i;

	if (in512 == 0 || (lba >> 32) != 0U) {
		return 0;
	}

	for (i = 0U; i < 512U; i++) {
		g_msdc_dma_sector_buf[i] = in512[i];
	}
	clean_dcache_range((uintptr_t) g_msdc_dma_sector_buf, 512U);
	pet_wdt();

	if (!wait_for_mask_clear(base + SDC_STS, SDC_STS_SDCBUSY | SDC_STS_CMDBUSY, 200000U)) {
		msdc0_reset_host();
		if (!wait_for_mask_clear(base + SDC_STS, SDC_STS_SDCBUSY | SDC_STS_CMDBUSY, 200000U)) {
			return 0;
		}
	}

	mmio_write32(base + MSDC_INT, 0xffffffffU);
	mmio_write32(base + MSDC_FIFOCS, MSDC_FIFOCS_CLR);
	if (!wait_for_mask_clear(base + MSDC_FIFOCS, MSDC_FIFOCS_CLR, 200000U)) {
		msdc0_reset_host();
		return 0;
	}
	mmio_write32(base + EMMC51_CFG0, mmio_read32(base + EMMC51_CFG0) & ~MSDC_EMMC51_CFG_CMDQEN);
	msdc0_dma_on();

	mmio_write32(base + SDC_CFG,
		     (mmio_read32(base + SDC_CFG) & ~SDC_CFG_BUSWIDTH_MASK) | SDC_CFG_BUSWIDTH_8BIT);
	mmio_write32(base + SDC_BLK_NUM, 1U);
	mmio_write32(base + SDC_ARG, (uint32_t) lba);

	rawcmd = MMC_RAWCMD_WRITE(MMC_CMD24_WRITE_BLOCK, MMC_RSP_R1, 512U);
	mmio_write32(base + SDC_CMD, rawcmd);

	if (!wait_for_mask_set(base + MSDC_INT, MSDC_INT_CMD_MASK, 400000U)) {
		uart_puts_all("[mk] msdc: dma write cmd timeout lba=0x");
		uart_puthex64_all(lba);
		uart_puts_all(" int=0x");
		uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_INT));
		uart_puts_all("\r\n");
		msdc0_dma_off();
		msdc0_reset_host();
		return 0;
	}
	if ((mmio_read32(base + MSDC_INT) & (MSDC_INT_CMDTMO | MSDC_INT_RSPCRCERR)) != 0U) {
		uart_puts_all("[mk] msdc: dma write cmd error lba=0x");
		uart_puthex64_all(lba);
		uart_puts_all(" int=0x");
		uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_INT));
		uart_puts_all("\r\n");
		mmio_write32(base + MSDC_INT, MSDC_INT_CMD_MASK);
		msdc0_dma_off();
		msdc0_reset_host();
		return 0;
	}
	mmio_write32(base + MSDC_INT, MSDC_INT_CMD_MASK);

	r1 = mmio_read32(base + SDC_RESP0);
	if ((r1 & R1_WP_VIOLATION) != 0U) {
		uart_puts_all("[mk] msdc: dma write cmd wp-violation lba=0x");
		uart_puthex64_all(lba);
		uart_puts_all(" r1=0x");
		uart_puthex64_all((uint64_t) r1);
		uart_puts_all("\r\n");
	}
	if ((r1 & 0xf9ffe008U) != 0U) {
		uart_puts_all("[mk] msdc: dma write cmd status error lba=0x");
		uart_puthex64_all(lba);
		uart_puts_all(" r1=0x");
		uart_puthex64_all((uint64_t) r1);
		uart_puts_all("\r\n");
		msdc0_dma_off();
		return 0;
	}

	msdc0_dma_setup_basic(g_msdc_dma_sector_buf, 512U);
	msdc0_dma_start();

	if (!wait_for_mask_set(base + MSDC_INT, MSDC_INT_DATA_MASK, 800000U)) {
		uart_puts_all("[mk] msdc: dma write data timeout lba=0x");
		uart_puthex64_all(lba);
		uart_puts_all(" int=0x");
		uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_INT));
		uart_puts_all(" cfg=0x");
		uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_DMA_CFG));
		uart_puts_all("\r\n");
		msdc0_dma_stop();
		msdc0_dma_off();
		msdc0_reset_host();
		return 0;
	}
	if ((mmio_read32(base + MSDC_INT) & (MSDC_INT_DATTMO | MSDC_INT_DATCRCERR)) != 0U) {
		uart_puts_all("[mk] msdc: dma write data error lba=0x");
		uart_puthex64_all(lba);
		uart_puts_all(" int=0x");
		uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_INT));
		uart_puts_all("\r\n");
		mmio_write32(base + MSDC_INT, MSDC_INT_DATA_MASK);
		msdc0_dma_stop();
		msdc0_dma_off();
		msdc0_reset_host();
		return 0;
	}
	mmio_write32(base + MSDC_INT, MSDC_INT_DATA_MASK);
	msdc0_dma_stop();
	msdc0_dma_off();

	if (!wait_for_mask_clear(base + SDC_STS, SDC_STS_SDCBUSY | SDC_STS_CMDBUSY, 800000U)) {
		uart_puts_all("[mk] msdc: dma write postbusy timeout lba=0x");
		uart_puthex64_all(lba);
		uart_puts_all(" sts=0x");
		uart_puthex64_all((uint64_t) mmio_read32(base + SDC_STS));
		uart_puts_all("\r\n");
		msdc0_reset_host();
		return 0;
	}
	if (!msdc0_wait_card_ready()) {
		uart_puts_all("[mk] msdc: dma write card-ready timeout lba=0x");
		uart_puthex64_all(lba);
		uart_puts_all("\r\n");
		return 0;
	}

	return 1;
}

static int msdc0_write_sector(uint64_t lba, const uint8_t *in512)
{
	uint64_t base = MTK_MSDC0_BASE;
	uint32_t rawcmd;
	uint32_t i;

	if (in512 == 0 || (lba >> 32) != 0U) {
		return 0;
	}
	pet_wdt();

	if (!wait_for_mask_clear(base + SDC_STS, SDC_STS_SDCBUSY | SDC_STS_CMDBUSY, 200000U)) {
		msdc0_reset_host();
		if (!wait_for_mask_clear(base + SDC_STS, SDC_STS_SDCBUSY | SDC_STS_CMDBUSY, 200000U)) {
			return 0;
		}
	}

	mmio_write32(base + MSDC_INT, 0xffffffffU);
	mmio_write32(base + MSDC_FIFOCS, MSDC_FIFOCS_CLR);
	if (!wait_for_mask_clear(base + MSDC_FIFOCS, MSDC_FIFOCS_CLR, 200000U)) {
		msdc0_reset_host();
		return 0;
	}

	mmio_write32(base + MSDC_CFG, mmio_read32(base + MSDC_CFG) | MSDC_CFG_PIO);
	mmio_write32(base + SDC_CFG,
		     (mmio_read32(base + SDC_CFG) & ~SDC_CFG_BUSWIDTH_MASK) | SDC_CFG_BUSWIDTH_8BIT);
	mmio_write32(base + SDC_BLK_NUM, 1U);
	mmio_write32(base + SDC_ARG, (uint32_t) lba);

	rawcmd = MMC_RAWCMD_WRITE(MMC_CMD24_WRITE_BLOCK, MMC_RSP_R1, 512U);
	mmio_write32(base + SDC_CMD, rawcmd);

	if (!wait_for_mask_set(base + MSDC_INT, MSDC_INT_CMD_MASK, 400000U)) {
		uart_puts_all("[mk] msdc: write cmd timeout lba=0x");
		uart_puthex64_all(lba);
		uart_puts_all(" int=0x");
		uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_INT));
		uart_puts_all("\r\n");
		msdc0_reset_host();
		return 0;
	}
	if ((mmio_read32(base + MSDC_INT) & (MSDC_INT_CMDTMO | MSDC_INT_RSPCRCERR)) != 0U) {
		uart_puts_all("[mk] msdc: write cmd error lba=0x");
		uart_puthex64_all(lba);
		uart_puts_all(" int=0x");
		uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_INT));
		uart_puts_all("\r\n");
		mmio_write32(base + MSDC_INT, MSDC_INT_CMD_MASK);
		msdc0_reset_host();
		return 0;
	}
	mmio_write32(base + MSDC_INT, MSDC_INT_CMD_MASK);

	{
		uint32_t r1 = mmio_read32(base + SDC_RESP0);

		if ((r1 & R1_WP_VIOLATION) != 0U) {
			uart_puts_all("[mk] msdc: write cmd wp-violation lba=0x");
			uart_puthex64_all(lba);
			uart_puts_all(" r1=0x");
			uart_puthex64_all((uint64_t) r1);
			uart_puts_all("\r\n");
		}
		if ((r1 & 0xf9ffe008U) != 0U) {
			uart_puts_all("[mk] msdc: write cmd status error lba=0x");
			uart_puthex64_all(lba);
			uart_puts_all(" r1=0x");
			uart_puthex64_all((uint64_t) r1);
			uart_puts_all("\r\n");
			return 0;
		}
	}

	{
		uint32_t spin = 0U;
		for (;;) {
			uint32_t txcnt = (mmio_read32(base + MSDC_FIFOCS) & MSDC_FIFOCS_TXCNT_MASK) >> 16;
			if (txcnt == 0U) {
				break;
			}
			if ((mmio_read32(base + MSDC_INT) & (MSDC_INT_DATTMO | MSDC_INT_DATCRCERR)) != 0U) {
				mmio_write32(base + MSDC_INT, MSDC_INT_DATA_MASK);
				msdc0_reset_host();
				return 0;
			}
			if ((spin++ & 0x3fffU) == 0U) {
				pet_wdt();
			}
			if (spin >= 400000U) {
				msdc0_reset_host();
				return 0;
			}
		}

		for (i = 0U; i < 128U; i++) {
			uint32_t v = (uint32_t) in512[i * 4U + 0U] |
				     ((uint32_t) in512[i * 4U + 1U] << 8) |
				     ((uint32_t) in512[i * 4U + 2U] << 16) |
				     ((uint32_t) in512[i * 4U + 3U] << 24);
			mmio_write32(base + MSDC_TXDATA, v);
		}
	}

	{
		uint32_t spin = 0U;
		for (;;) {
			uint32_t txcnt = (mmio_read32(base + MSDC_FIFOCS) & MSDC_FIFOCS_TXCNT_MASK) >> 16;
			if (txcnt == 0U) {
				break;
			}
			if ((mmio_read32(base + MSDC_INT) & (MSDC_INT_DATTMO | MSDC_INT_DATCRCERR)) != 0U) {
				mmio_write32(base + MSDC_INT, MSDC_INT_DATA_MASK);
				msdc0_reset_host();
				return 0;
			}
			if ((spin++ & 0x3fffU) == 0U) {
				pet_wdt();
			}
			if (spin >= 400000U) {
				uart_puts_all("[mk] msdc: write fifo drain timeout lba=0x");
				uart_puthex64_all(lba);
				uart_puts_all(" fifocs=0x");
				uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_FIFOCS));
				uart_puts_all("\r\n");
				msdc0_reset_host();
				return 0;
			}
		}
	}

	if (!wait_for_mask_set(base + MSDC_INT, MSDC_INT_DATA_MASK, 800000U)) {
		uart_puts_all("[mk] msdc: write data timeout lba=0x");
		uart_puthex64_all(lba);
		uart_puts_all(" int=0x");
		uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_INT));
		uart_puts_all("\r\n");
		msdc0_reset_host();
		return 0;
	}
	if ((mmio_read32(base + MSDC_INT) & (MSDC_INT_DATTMO | MSDC_INT_DATCRCERR)) != 0U) {
		uart_puts_all("[mk] msdc: write data error lba=0x");
		uart_puthex64_all(lba);
		uart_puts_all(" int=0x");
		uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_INT));
		uart_puts_all("\r\n");
		mmio_write32(base + MSDC_INT, MSDC_INT_DATA_MASK);
		msdc0_reset_host();
		return 0;
	}
	mmio_write32(base + MSDC_INT, MSDC_INT_DATA_MASK);

	if (!wait_for_mask_clear(base + SDC_STS, SDC_STS_SDCBUSY | SDC_STS_CMDBUSY, 800000U)) {
		uart_puts_all("[mk] msdc: write postbusy timeout lba=0x");
		uart_puthex64_all(lba);
		uart_puts_all(" sts=0x");
		uart_puthex64_all((uint64_t) mmio_read32(base + SDC_STS));
		uart_puts_all("\r\n");
		msdc0_reset_host();
		return 0;
	}
	if (!msdc0_wait_card_ready()) {
		uart_puts_all("[mk] msdc: write card-ready timeout lba=0x");
		uart_puthex64_all(lba);
		uart_puts_all("\r\n");
		return 0;
	}

	return 1;
}

static int msdc0_send_stop_transmission(void)
{
	uint64_t base = MTK_MSDC0_BASE;
	uint32_t rawcmd;

	if (!wait_for_mask_clear(base + SDC_STS, SDC_STS_SDCBUSY | SDC_STS_CMDBUSY, 400000U)) {
		uart_puts_all("[mk] msdc: stop prebusy timeout\r\n");
		msdc0_reset_host();
		return 0;
	}

	mmio_write32(base + MSDC_INT, 0xffffffffU);
	mmio_write32(base + SDC_BLK_NUM, 0U);
	mmio_write32(base + SDC_ARG, 0U);

	rawcmd = MMC_RAWCMD_NODATA(MMC_CMD12_STOP_TRANSMISSION, MMC_RSP_R1B) | (1U << 14);
	mmio_write32(base + SDC_CMD, rawcmd);

	if (!wait_for_mask_set(base + MSDC_INT, MSDC_INT_CMD_MASK, 800000U)) {
		uart_puts_all("[mk] msdc: stop cmd timeout int=0x");
		uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_INT));
		uart_puts_all("\r\n");
		msdc0_reset_host();
		return 0;
	}
	if ((mmio_read32(base + MSDC_INT) & (MSDC_INT_CMDTMO | MSDC_INT_RSPCRCERR)) != 0U) {
		uart_puts_all("[mk] msdc: stop cmd error int=0x");
		uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_INT));
		uart_puts_all("\r\n");
		mmio_write32(base + MSDC_INT, MSDC_INT_CMD_MASK);
		msdc0_reset_host();
		return 0;
	}
	mmio_write32(base + MSDC_INT, MSDC_INT_CMD_MASK);

	if (!wait_for_mask_clear(base + SDC_STS, SDC_STS_SDCBUSY | SDC_STS_CMDBUSY, 800000U)) {
		uart_puts_all("[mk] msdc: stop postbusy timeout\r\n");
		msdc0_reset_host();
		return 0;
	}

	return 1;
}

static int msdc0_write_sectors_multi(uint64_t lba, const uint8_t *in, uint32_t sector_count)
{
	uint64_t base = MTK_MSDC0_BASE;
	uint32_t rawcmd;
	uint32_t total_words;
	uint32_t w;
	uint32_t data_wait;

	if (in == 0 || sector_count < 2U || (lba >> 32) != 0U) {
		return 0;
	}

	pet_wdt();

	if (!wait_for_mask_clear(base + SDC_STS, SDC_STS_SDCBUSY | SDC_STS_CMDBUSY, 200000U)) {
		msdc0_reset_host();
		if (!wait_for_mask_clear(base + SDC_STS, SDC_STS_SDCBUSY | SDC_STS_CMDBUSY, 200000U)) {
			return 0;
		}
	}

	mmio_write32(base + MSDC_INT, 0xffffffffU);
	mmio_write32(base + MSDC_FIFOCS, MSDC_FIFOCS_CLR);
	if (!wait_for_mask_clear(base + MSDC_FIFOCS, MSDC_FIFOCS_CLR, 200000U)) {
		msdc0_reset_host();
		return 0;
	}

	mmio_write32(base + MSDC_CFG, mmio_read32(base + MSDC_CFG) | MSDC_CFG_PIO);
	mmio_write32(base + SDC_CFG,
		     (mmio_read32(base + SDC_CFG) & ~SDC_CFG_BUSWIDTH_MASK) | SDC_CFG_BUSWIDTH_8BIT);
	mmio_write32(base + SDC_BLK_NUM, sector_count);
	mmio_write32(base + SDC_ARG, (uint32_t) lba);

	rawcmd = ((MMC_CMD25_WRITE_MULTIPLE_BLOCK & 0x3fU) |
		  ((MMC_RSP_R1 & 0x7U) << 7) |
		  (512U << 16) |
		  (1U << 13) |
		  (2U << 11));
	mmio_write32(base + SDC_CMD, rawcmd);

	if (!wait_for_mask_set(base + MSDC_INT, MSDC_INT_CMD_MASK, 400000U)) {
		uart_puts_all("[mk] msdc: write-multi cmd timeout lba=0x");
		uart_puthex64_all(lba);
		uart_puts_all(" sectors=0x");
		uart_puthex64_all((uint64_t) sector_count);
		uart_puts_all(" int=0x");
		uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_INT));
		uart_puts_all("\r\n");
		msdc0_reset_host();
		return 0;
	}
	if ((mmio_read32(base + MSDC_INT) & (MSDC_INT_CMDTMO | MSDC_INT_RSPCRCERR)) != 0U) {
		uart_puts_all("[mk] msdc: write-multi cmd error lba=0x");
		uart_puthex64_all(lba);
		uart_puts_all(" sectors=0x");
		uart_puthex64_all((uint64_t) sector_count);
		uart_puts_all(" int=0x");
		uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_INT));
		uart_puts_all("\r\n");
		mmio_write32(base + MSDC_INT, MSDC_INT_CMD_MASK);
		msdc0_reset_host();
		return 0;
	}
	mmio_write32(base + MSDC_INT, MSDC_INT_CMD_MASK);

	total_words = sector_count * 128U;
	for (w = 0U; w < total_words; w++) {
		uint32_t txcnt;
		uint32_t spin = 0U;
		uint32_t v;
		uint32_t b = w * 4U;

		do {
			txcnt = (mmio_read32(base + MSDC_FIFOCS) & MSDC_FIFOCS_TXCNT_MASK) >> 16;
			if (txcnt < 128U) {
				break;
			}
			if ((mmio_read32(base + MSDC_INT) & (MSDC_INT_DATTMO | MSDC_INT_DATCRCERR)) != 0U) {
				uart_puts_all("[mk] msdc: write-multi fifo error int=0x");
				uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_INT));
				uart_puts_all("\r\n");
				mmio_write32(base + MSDC_INT, MSDC_INT_DATA_MASK);
				msdc0_reset_host();
				return 0;
			}
			if ((spin++ & 0x3fffU) == 0U) {
				pet_wdt();
			}
		} while (spin < 800000U);
		if (txcnt >= 128U) {
			uart_puts_all("[mk] msdc: write-multi fifo timeout\r\n");
			msdc0_reset_host();
			return 0;
		}

		v = (uint32_t) in[b + 0U] |
		    ((uint32_t) in[b + 1U] << 8) |
		    ((uint32_t) in[b + 2U] << 16) |
		    ((uint32_t) in[b + 3U] << 24);
		mmio_write32(base + MSDC_TXDATA, v);
	}

	data_wait = 800000U + (sector_count * 200000U);
	if (data_wait > 20000000U || data_wait < 800000U) {
		data_wait = 20000000U;
	}
	if (!wait_for_mask_set(base + MSDC_INT, MSDC_INT_DATA_MASK, data_wait)) {
		uart_puts_all("[mk] msdc: write-multi data timeout lba=0x");
		uart_puthex64_all(lba);
		uart_puts_all(" sectors=0x");
		uart_puthex64_all((uint64_t) sector_count);
		uart_puts_all(" int=0x");
		uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_INT));
		uart_puts_all("\r\n");
		msdc0_reset_host();
		return 0;
	}
	if ((mmio_read32(base + MSDC_INT) & (MSDC_INT_DATTMO | MSDC_INT_DATCRCERR)) != 0U) {
		uart_puts_all("[mk] msdc: write-multi data error lba=0x");
		uart_puthex64_all(lba);
		uart_puts_all(" sectors=0x");
		uart_puthex64_all((uint64_t) sector_count);
		uart_puts_all(" int=0x");
		uart_puthex64_all((uint64_t) mmio_read32(base + MSDC_INT));
		uart_puts_all("\r\n");
		mmio_write32(base + MSDC_INT, MSDC_INT_DATA_MASK);
		msdc0_reset_host();
		return 0;
	}
	mmio_write32(base + MSDC_INT, MSDC_INT_DATA_MASK);

	if (!msdc0_send_stop_transmission()) {
		return 0;
	}
	if (!msdc0_wait_card_ready()) {
		uart_puts_all("[mk] msdc: write-multi card-ready timeout lba=0x");
		uart_puthex64_all(lba);
		uart_puts_all("\r\n");
		return 0;
	}

	return 1;
}

static int stage0_gpt_find_relative(uint64_t base_lba,
				    const char *label,
				    uint64_t *out_start_lba,
				    uint64_t *out_lba_count)
{
	uint8_t header[512];
	uint8_t entry_sector[512];
	uint64_t entries_lba;
	uint32_t entry_count;
	uint32_t entry_size;
	uint32_t i;

	if (label == 0 || label[0] == '\0' || out_start_lba == 0 || out_lba_count == 0) {
		return 0;
	}
	if (!msdc0_read_sector(base_lba + 1U, header)) {
		return 0;
	}
	if (!bytes_equal(header, (const uint8_t *) "EFI PART", 8U)) {
		return 0;
	}

	entry_size = le32_read(header + 84U);
	entry_count = le32_read(header + 80U);
	entries_lba = base_lba + le64_read(header + 72U);

	if (entry_size < 128U || entry_size > 512U || entry_count == 0U || entry_count > 256U) {
		return 0;
	}

	for (i = 0; i < entry_count; i++) {
		uint32_t byte_off = i * entry_size;
		uint64_t lba = entries_lba + (uint64_t) (byte_off / 512U);
		uint32_t in_sector = byte_off % 512U;
		const uint8_t *entry;
		uint64_t first_lba;
		uint64_t last_lba;

		if (in_sector + entry_size > 512U) {
			return 0;
		}
		if (!msdc0_read_sector(lba, entry_sector)) {
			return 0;
		}
		entry = entry_sector + in_sector;
		if (guid_zero_16(entry)) {
			continue;
		}
		if (!gpt_entry_name_equals(entry, label)) {
			continue;
		}

		first_lba = le64_read(entry + 32U);
		last_lba = le64_read(entry + 40U);
		*out_start_lba = base_lba + first_lba;
		*out_lba_count = (last_lba >= first_lba) ? (last_lba - first_lba + 1U) : 0U;
		return 1;
	}

	return 0;
}

static int stage0_find_partition_any(const char *label, uint64_t *out_start_lba, uint64_t *out_lba_count)
{
	uint8_t header[512];
	uint8_t entry_sector[512];
	uint32_t entry_count;
	uint32_t entry_size;
	uint64_t entries_lba;
	uint32_t i;

	if (label == 0 || label[0] == '\0' || out_start_lba == 0 || out_lba_count == 0) {
		return 0;
	}
	if (stage0_gpt_find_relative(0U, label, out_start_lba, out_lba_count)) {
		return 1;
	}
	if (!msdc0_read_sector(1U, header)) {
		return 0;
	}
	if (!bytes_equal(header, (const uint8_t *) "EFI PART", 8U)) {
		return 0;
	}

	entry_size = le32_read(header + 84U);
	entry_count = le32_read(header + 80U);
	entries_lba = le64_read(header + 72U);
	if (entry_size < 128U || entry_size > 512U || entry_count == 0U || entry_count > 256U) {
		return 0;
	}

	for (i = 0; i < entry_count; i++) {
		uint32_t byte_off = i * entry_size;
		uint64_t lba = entries_lba + (uint64_t) (byte_off / 512U);
		uint32_t in_sector = byte_off % 512U;
		const uint8_t *entry;
		uint64_t first_lba;
		uint64_t last_lba;
		uint64_t child_base;
		uint64_t child_count;

		if (in_sector + entry_size > 512U) {
			break;
		}
		if (!msdc0_read_sector(lba, entry_sector)) {
			break;
		}
		entry = entry_sector + in_sector;
		if (guid_zero_16(entry)) {
			continue;
		}

		first_lba = le64_read(entry + 32U);
		last_lba = le64_read(entry + 40U);
		if (last_lba < first_lba) {
			continue;
		}
		child_base = first_lba;
		child_count = last_lba - first_lba + 1U;
		if (child_count <= 34U) {
			continue;
		}
		if (stage0_gpt_find_relative(child_base, label, out_start_lba, out_lba_count)) {
			return 1;
		}
	}

	return 0;
}

int mk_stage0_storage_prepare(void)
{
	return msdc0_select_user_area();
}

int mk_stage0_storage_find_partition(const char *label, uint64_t *out_start_lba, uint64_t *out_lba_count)
{
	return stage0_find_partition_any(label, out_start_lba, out_lba_count);
}

int mk_stage0_storage_find_partition_within(const char *container_label,
					    const char *label,
					    uint64_t *out_start_lba,
					    uint64_t *out_lba_count)
{
	uint64_t container_lba = 0U;
	uint64_t container_count = 0U;

	if (container_label == 0 || container_label[0] == '\0' ||
	    label == 0 || label[0] == '\0' ||
	    out_start_lba == 0 || out_lba_count == 0) {
		return 0;
	}
	if (!stage0_gpt_find_relative(0U, container_label, &container_lba, &container_count)) {
		return 0;
	}
	return stage0_gpt_find_relative(container_lba, label, out_start_lba, out_lba_count);
}

int mk_stage0_storage_read_sector(uint64_t lba, uint8_t *out512)
{
	return msdc0_read_sector(lba, out512);
}

int mk_stage0_storage_write_sector(uint64_t lba, const uint8_t *in512)
{
	return msdc0_write_sector_dma(lba, in512);
}

int mk_stage0_storage_write_sectors(uint64_t lba, const uint8_t *in, uint32_t sector_count)
{
	const uint32_t max_multi_chunk = 256U;
	uint32_t n = 0U;

	if (in == 0 || sector_count == 0U) {
		return 0;
	}

	while (n < sector_count) {
		uint32_t todo = sector_count - n;
		const uint8_t *chunk = in + ((uint64_t) n * 512U);
		uint64_t chunk_lba = lba + (uint64_t) n;
		uint32_t i;

		if (todo > max_multi_chunk) {
			todo = max_multi_chunk;
		}

		if (todo >= 2U && g_msdc_multi_write_disable == 0U) {
			if (!msdc0_write_sectors_multi(chunk_lba, chunk, todo)) {
				g_msdc_multi_write_disable = 1U;
				uart_puts_all("[mk] msdc: multi-write disabled, fallback single\r\n");
			}
		}

		if (g_msdc_multi_write_disable != 0U || todo < 2U) {
			for (i = 0U; i < todo; i++) {
				if (!msdc0_write_sector(chunk_lba + (uint64_t) i, chunk + ((uint64_t) i * 512U))) {
					return 0;
				}
			}
		}

		n += todo;
		if ((n & 0x3fU) == 0U) {
			pet_wdt();
		}
	}

	return 1;
}

int mk_stage0_storage_capacity_bytes(uint64_t *out_bytes)
{
	uint8_t extcsd[512];
	uint32_t sec_count;

	if (out_bytes == 0) {
		return 0;
	}
	if (!msdc0_select_user_area()) {
		return 0;
	}
	if (!msdc0_read_extcsd(extcsd)) {
		return 0;
	}

	sec_count = (uint32_t) extcsd[212U] |
		    ((uint32_t) extcsd[213U] << 8) |
		    ((uint32_t) extcsd[214U] << 16) |
		    ((uint32_t) extcsd[215U] << 24);
	if (sec_count == 0U) {
		return 0;
	}

	*out_bytes = (uint64_t) sec_count * 512ULL;
	return 1;
}

int mk_stage0_storage_flush(void)
{
	return msdc0_flush_cache();
}

void mk_stage0_storage_clr_write_prot_range(uint64_t start_lba, uint64_t sector_count)
{
	msdc0_clr_write_prot_range(start_lba, sector_count);
}

void mk_stage0_storage_pet_wdt(void)
{
	pet_wdt();
}

static void discover_peacock_partitions(void)
{
	uint8_t header[512];
	uint8_t entry_sector[512];
	uint32_t entry_count;
	uint32_t entry_size;
	uint64_t entries_lba;
	uint32_t i;
	uint64_t boot_lba;
	uint64_t boot_count;
	uint64_t root_lba;
	uint64_t root_count;

	g_peacock_boot_lba = 0U;
	g_peacock_boot_count = 0U;
	g_peacock_root_lba = 0U;
	g_peacock_root_count = 0U;
	g_peacock_boot_found = 0;
	g_peacock_root_found = 0;

	if (MK_DEVICE_BOOT_LABEL == 0 && MK_DEVICE_ROOT_LABEL == 0) {
		return;
	}
	if (!msdc0_select_user_area()) {
		uart_puts_all("[mk] gpt scan: failed to switch user area, trying current area\r\n");
	}
	if (!msdc0_read_sector(1U, header)) {
		uart_puts_all("[mk] gpt scan: primary header missing\r\n");
		uart_puts_all("[mk] gpt scan: lba1 read failed\r\n");
		return;
	}
	if (!bytes_equal(header, (const uint8_t *) "EFI PART", 8U)) {
		uart_puts_all("[mk] gpt scan: primary header missing\r\n");
		uart_puts_all("[mk] gpt scan: lba1[0..7]=0x");
		uart_puthex64_all(le64_read(header));
		uart_puts_all("\r\n");
		return;
	}

	entry_size = le32_read(header + 84U);
	entry_count = le32_read(header + 80U);
	entries_lba = le64_read(header + 72U);
	if (entry_size < 128U || entry_size > 512U || entry_count == 0U || entry_count > 256U) {
		uart_puts_all("[mk] gpt scan: unsupported header\r\n");
		return;
	}

	boot_lba = 0;
	boot_count = 0;
	root_lba = 0;
	root_count = 0;

	if (MK_DEVICE_ROOT_LABEL != 0 &&
	    stage0_gpt_find_relative(0U, MK_DEVICE_ROOT_LABEL, &root_lba, &root_count)) {
	}
	if (MK_DEVICE_BOOT_LABEL != 0 &&
	    stage0_gpt_find_relative(0U, MK_DEVICE_BOOT_LABEL, &boot_lba, &boot_count)) {
	}
	if ((MK_DEVICE_BOOT_LABEL == 0 || boot_lba != 0U) &&
	    (MK_DEVICE_ROOT_LABEL == 0 || root_lba != 0U)) {
		if (boot_lba != 0U) {
			g_peacock_boot_lba = boot_lba;
			g_peacock_boot_count = boot_count;
			g_peacock_boot_found = 1;
			uart_puts_all("[mk] boot label top-level ");
			uart_puts_all(MK_DEVICE_BOOT_LABEL);
			uart_puts_all(" lba=0x");
			uart_puthex64_all(boot_lba);
			uart_puts_all(" count=0x");
			uart_puthex64_all(boot_count);
			uart_puts_all("\r\n");
		}
		if (root_lba != 0U) {
			g_peacock_root_lba = root_lba;
			g_peacock_root_count = root_count;
			g_peacock_root_found = 1;
			uart_puts_all("[mk] root label top-level ");
			uart_puts_all(MK_DEVICE_ROOT_LABEL);
			uart_puts_all(" lba=0x");
			uart_puthex64_all(root_lba);
			uart_puts_all(" count=0x");
			uart_puthex64_all(root_count);
			uart_puts_all("\r\n");
		}
		return;
	}
	boot_lba = 0;
	boot_count = 0;
	root_lba = 0;
	root_count = 0;

	for (i = 0; i < entry_count; i++) {
		uint32_t byte_off = i * entry_size;
		uint64_t lba = entries_lba + (uint64_t) (byte_off / 512U);
		uint32_t in_sector = byte_off % 512U;
		const uint8_t *entry;
		uint64_t first_lba;
		uint64_t last_lba;
		uint64_t child_base;
		uint64_t child_count;
		uint64_t child_boot_lba;
		uint64_t child_boot_count;
		uint64_t child_root_lba;
		uint64_t child_root_count;

		if (in_sector + entry_size > 512U) {
			break;
		}
		if (!msdc0_read_sector(lba, entry_sector)) {
			break;
		}
		entry = entry_sector + in_sector;
		if (guid_zero_16(entry)) {
			continue;
		}

		first_lba = le64_read(entry + 32U);
		last_lba = le64_read(entry + 40U);
		if (last_lba < first_lba) {
			continue;
		}
		child_base = first_lba;
		child_count = last_lba - first_lba + 1U;
		if (child_count <= 34U) {
			continue;
		}

		child_boot_lba = 0U;
		child_boot_count = 0U;
		child_root_lba = 0U;
		child_root_count = 0U;

		if (MK_DEVICE_BOOT_LABEL != 0 &&
		    stage0_gpt_find_relative(child_base, MK_DEVICE_BOOT_LABEL,
					     &child_boot_lba, &child_boot_count)) {
		}
		if (MK_DEVICE_ROOT_LABEL != 0 &&
		    stage0_gpt_find_relative(child_base, MK_DEVICE_ROOT_LABEL,
					     &child_root_lba, &child_root_count)) {
		}
		if (!((MK_DEVICE_BOOT_LABEL == 0 || child_boot_lba != 0U) &&
		      (MK_DEVICE_ROOT_LABEL == 0 || child_root_lba != 0U))) {
			continue;
		}

		boot_lba = child_boot_lba;
		boot_count = child_boot_count;
		root_lba = child_root_lba;
		root_count = child_root_count;
		if (boot_lba != 0U) {
			g_peacock_boot_lba = boot_lba;
			g_peacock_boot_count = boot_count;
			g_peacock_boot_found = 1;
			uart_puts_all("[mk] boot label nested ");
			uart_puts_all(MK_DEVICE_BOOT_LABEL);
			uart_puts_all(" base=0x");
			uart_puthex64_all(child_base);
			uart_puts_all(" lba=0x");
			uart_puthex64_all(boot_lba);
			uart_puts_all(" count=0x");
			uart_puthex64_all(boot_count);
			uart_puts_all("\r\n");
		}
		if (root_lba != 0U) {
			g_peacock_root_lba = root_lba;
			g_peacock_root_count = root_count;
			g_peacock_root_found = 1;
			uart_puts_all("[mk] root label nested ");
			uart_puts_all(MK_DEVICE_ROOT_LABEL);
			uart_puts_all(" base=0x");
			uart_puthex64_all(child_base);
			uart_puts_all(" lba=0x");
			uart_puthex64_all(root_lba);
			uart_puts_all(" count=0x");
			uart_puthex64_all(root_count);
			uart_puts_all("\r\n");
		}
		return;
	}

	if (MK_DEVICE_BOOT_LABEL != 0 && boot_lba == 0U) {
		uart_puts_all("[mk] boot label missing ");
		uart_puts_all(MK_DEVICE_BOOT_LABEL);
		uart_puts_all("\r\n");
	}
	if (MK_DEVICE_ROOT_LABEL != 0 && root_lba == 0U) {
		uart_puts_all("[mk] root label missing ");
		uart_puts_all(MK_DEVICE_ROOT_LABEL);
		uart_puts_all("\r\n");
	}
}

static __attribute__((unused)) int peacock_boot_targets_missing(void)
{
	if (MK_DEVICE_BOOT_LABEL != 0 && g_peacock_boot_found == 0) {
		return 1;
	}
	if (MK_DEVICE_ROOT_LABEL != 0 && g_peacock_root_found == 0) {
		return 1;
	}
	return 0;
}

int mk_stage0_write_para_bcb(uint8_t set_recovery)
{
	uint32_t data[128];
	uint32_t i;
	uint32_t rawcmd;
	uint32_t txcnt;
	uint64_t base = MTK_MSDC0_BASE;

	if (MK_DEVICE_BCB_PARA_LBA == 0ULL) {
		uart_puts_all("[mk] no BCB para LBA configured\r\n");
		return 0;
	}
	for (i = 0; i < 128U; i++) {
		data[i] = 0U;
	}
	if (set_recovery != 0U) {
		data[0] = 0x746f6f62U; /* "boot" */
		data[1] = 0x6365722dU; /* "-rec" */
		data[2] = 0x7265766fU; /* "over" */
		data[3] = 0x00000079U; /* "y" */
	}

	if (!wait_for_mask_clear(base + SDC_STS, SDC_STS_SDCBUSY | SDC_STS_CMDBUSY, 200000U)) {
		msdc0_reset_host();
		if (!wait_for_mask_clear(base + SDC_STS, SDC_STS_SDCBUSY | SDC_STS_CMDBUSY, 200000U)) {
			uart_puts_all("[mk] BCB write: controller busy\r\n");
			return 0;
		}
	}

	mmio_write32(base + MSDC_INT, 0xffffffffU);
	mmio_write32(base + MSDC_FIFOCS, MSDC_FIFOCS_CLR);
	if (!wait_for_mask_clear(base + MSDC_FIFOCS, MSDC_FIFOCS_CLR, 200000U)) {
		uart_puts_all("[mk] BCB write: fifo clear timeout\r\n");
		return 0;
	}

	mmio_write32(base + MSDC_CFG, mmio_read32(base + MSDC_CFG) | MSDC_CFG_PIO);
	mmio_write32(base + SDC_CFG,
		   (mmio_read32(base + SDC_CFG) & ~SDC_CFG_BUSWIDTH_MASK) | SDC_CFG_BUSWIDTH_8BIT);
	mmio_write32(base + SDC_BLK_NUM, 1U);
	mmio_write32(base + SDC_ARG, (uint32_t) MK_DEVICE_BCB_PARA_LBA);

	rawcmd = MMC_RAWCMD_WRITE(MMC_CMD24_WRITE_BLOCK, MMC_RSP_R1, 512U);
	mmio_write32(base + SDC_CMD, rawcmd);

	if (!wait_for_mask_set(base + MSDC_INT, MSDC_INT_CMD_MASK, 400000U)) {
		uart_puts_all("[mk] BCB write: cmd timeout\r\n");
		return 0;
	}
	if ((mmio_read32(base + MSDC_INT) & (MSDC_INT_CMDTMO | MSDC_INT_RSPCRCERR)) != 0U) {
		uart_puts_all("[mk] BCB write: cmd error\r\n");
		mmio_write32(base + MSDC_INT, MSDC_INT_CMD_MASK);
		return 0;
	}
	mmio_write32(base + MSDC_INT, MSDC_INT_CMD_MASK);

	for (i = 0; i < 128U; i++) {
		do {
			txcnt = (mmio_read32(base + MSDC_FIFOCS) & MSDC_FIFOCS_TXCNT_MASK) >> 16;
		} while (txcnt >= 128U);
		mmio_write32(base + MSDC_TXDATA, data[i]);
	}

	if (!wait_for_mask_set(base + MSDC_INT, MSDC_INT_DATA_MASK, 800000U)) {
		uart_puts_all("[mk] BCB write: data timeout\r\n");
		return 0;
	}
	if ((mmio_read32(base + MSDC_INT) & (MSDC_INT_DATTMO | MSDC_INT_DATCRCERR)) != 0U) {
		uart_puts_all("[mk] BCB write: data error\r\n");
		mmio_write32(base + MSDC_INT, MSDC_INT_DATA_MASK);
		return 0;
	}
	mmio_write32(base + MSDC_INT, MSDC_INT_DATA_MASK);
	if (set_recovery != 0U) {
		uart_puts_all("[mk] BCB write: boot-recovery queued\r\n");
	} else {
		uart_puts_all("[mk] BCB write: normal boot queued\r\n");
	}
	return 1;
}

void mk_payload_main(uint64_t fdt_ptr)
{
	volatile uint64_t delay;
	simplefb_info_t info;
	const char *phx_stage;
	const char *phx_partition;
	const char *phx_partition_fallback;
	const char *phx_magic;
	const char *videolfb_lcmname;
	const mk_stage0_panel_t *panel;
	mk_stage0_display_ctx_t display_ctx;
	mk_stage0_display_status_t display_status;
	uint32_t fb_fallback_width;
	uint32_t fb_fallback_height;
	uint32_t fb_fallback_align;
	uint64_t phx_ufs_offset;
	uint64_t phx_emmc_offset;
	uint64_t videolfb_islcmfound;
	uint64_t videolfb_islcm_inited;
	char usb_serial[64];
	uint32_t usb_serial_len;
	info.addr = 0;
	info.size = 0;
	info.width = 0;
	info.height = 0;
	info.stride = 0;
	info.format = 0;
	phx_stage = 0;
	phx_partition = 0;
	phx_partition_fallback = 0;
	phx_magic = 0;
	videolfb_lcmname = 0;
	panel = 0;
	display_ctx.runtime_lcm_name = 0;
	display_ctx.videolfb_found = 0;
	display_ctx.videolfb_inited = 0;
	display_status = MK_STAGE0_DISPLAY_UNSUPPORTED;
	fb_fallback_width = 0;
	fb_fallback_height = 0;
	fb_fallback_align = 32U;
	phx_ufs_offset = 0;
	phx_emmc_offset = 0;
	videolfb_islcmfound = 0;
	videolfb_islcm_inited = 0;
	usb_serial[0] = '\0';
	usb_serial_len = 0;
	uart_puts_all("\r\n[mk] payload entry fdt=0x");
	uart_puthex64_all(fdt_ptr);
	uart_puts_all("\r\n");
	mk_stage0_msdc_snapshot_lk_state();
	mk_stage0_log_reset_watchdog_state("early");
	uart_puts_all("[mk] rr-mark-pre\r\n");
	mk_stage0_log_retained_reset_provenance((const void *) (uintptr_t) fdt_ptr);
	uart_puts_all("[mk] rr-mark-post\r\n");
	usb_serial_len = resolve_device_serial_from_fdt((const void *) (uintptr_t) fdt_ptr,
						      usb_serial, sizeof(usb_serial));
	if (usb_serial_len != 0U) {
		mk_stage0_mtk_usb_set_serial_ascii(usb_serial);
		uart_puts_all("[mk] usb serial=");
		uart_puts_all(usb_serial);
		uart_puts_all("\r\n");
	}
	{
		const char *lk_bootargs = mk_fdt_find_chosen_string(
			(const void *) (uintptr_t) fdt_ptr, "bootargs");
		if (lk_bootargs != 0) {
			mk_stage0_mtk_usb_set_lk_bootargs(lk_bootargs);
			uart_puts_all("[mk] lk bootargs stored\r\n");
		}
	}
	init_menu_buttons_from_fdt((const void *) (uintptr_t) fdt_ptr);
	setup_wdt((const void *) (uintptr_t) fdt_ptr);

	uart_puts_all("[mk] wdt_base=0x");
	uart_puthex64_all(mk_wdt_get_base());
	uart_puts_all("\r\n");
	mk_fdt_parse_simplefb((const void *) (uintptr_t) fdt_ptr, &info);
	uart_puts_all("[mk] fb_addr=0x");
	uart_puthex64_all(info.addr);
	uart_puts_all(" fb_size=0x");
	uart_puthex64_all(info.size);
	uart_puts_all("\r\n");
	if (info.addr == 0) {
		mk_fdt_parse_videolfb_from_chosen((const void *) (uintptr_t) fdt_ptr, &info);
		if (info.addr != 0) {
			uart_puts_all("[mk] fb fallback=atag,videolfb addr=0x");
			uart_puthex64_all(info.addr);
			uart_puts_all(" size=0x");
			uart_puthex64_all(info.size);
			uart_puts_all("\r\n");
		}
	}
	videolfb_lcmname =
		mk_fdt_find_chosen_string((const void *) (uintptr_t) fdt_ptr,
				      "atag,videolfb-lcmname");
	(void) mk_fdt_find_chosen_u64((const void *) (uintptr_t) fdt_ptr,
				 "atag,videolfb-islcmfound", &videolfb_islcmfound);
	(void) mk_fdt_find_chosen_u64((const void *) (uintptr_t) fdt_ptr,
				 "atag,videolfb-islcm_inited", &videolfb_islcm_inited);
	uart_puts_all("[mk] videolfb lcm=");
	uart_puts_all(videolfb_lcmname != 0 ? videolfb_lcmname : "(none)");
	uart_puts_all(" found=");
	uart_puthex64_all(videolfb_islcmfound);
	uart_puts_all(" inited=");
	uart_puthex64_all(videolfb_islcm_inited);
	uart_puts_all("\r\n");
	display_ctx.runtime_lcm_name = videolfb_lcmname;
	display_ctx.videolfb_found = videolfb_islcmfound;
	display_ctx.videolfb_inited = videolfb_islcm_inited;
	if (MK_DEVICE_LCM_BOOT_NAME != 0) {
		uart_puts_all("[mk] panel target=");
		uart_puts_all(MK_DEVICE_LCM_BOOT_NAME);
		uart_puts_all(" match=");
		uart_puts_all((videolfb_lcmname != 0 && str_eq(videolfb_lcmname, MK_DEVICE_LCM_BOOT_NAME)) ? "yes" : "no");
		uart_puts_all("\r\n");
	}
	panel = mk_stage0_panel_resolve(videolfb_lcmname, MK_DEVICE_LCM_BOOT_NAME);
	if (panel != 0) {
		fb_fallback_width = panel->fb_width;
		fb_fallback_height = panel->fb_height;
		fb_fallback_align = panel->fb_align;
		if (panel->runtime_fb_addr != 0U) {
			info.addr = panel->runtime_fb_addr;
			info.size = panel->runtime_fb_size;
			info.width = panel->fb_width;
			info.height = panel->fb_height;
			info.stride = panel->runtime_fb_stride;
			info.format = 0;
			uart_puts_all("[mk] fb override=device addr=0x");
			uart_puthex64_all(info.addr);
			uart_puts_all(" size=0x");
			uart_puthex64_all(info.size);
			uart_puts_all(" stride=0x");
			uart_puthex64_all(info.stride);
			uart_puts_all("\r\n");
		}
		uart_puts_all("[mk] panel profile=");
		uart_puts_all(mk_stage0_panel_name(panel));
		uart_puts_all(" fb=");
		uart_puthex64_all(fb_fallback_width);
		uart_puts_all("x");
		uart_puthex64_all(fb_fallback_height);
		uart_puts_all(" align=");
		uart_puthex64_all(fb_fallback_align);
		uart_puts_all(" lanes=");
		uart_puthex64_all(panel->dsi_lanes);
		uart_puts_all(" pkt=");
		uart_puthex64_all(panel->dsi_packet_size);
		uart_puts_all(" pll=");
		uart_puthex64_all(panel->dsi_pll_clock_cmd);
		uart_puts_all("/");
		uart_puthex64_all(panel->dsi_pll_clock_vdo);
		uart_puts_all(" mode=");
		uart_puts_all(panel->dsi_mode == MK_STAGE0_DSI_MODE_CMD ? "cmd" : "sync-pulse-vdo");
		uart_puts_all(" rst_ms=");
		uart_puthex64_all(panel->reset_delay0_ms);
		uart_puts_all("/");
		uart_puthex64_all(panel->reset_delay1_ms);
		uart_puts_all("/");
		uart_puthex64_all(panel->reset_delay2_ms);
		uart_puts_all("/");
		uart_puthex64_all(panel->reset_delay3_ms);
		uart_puts_all(" gpios=");
		uart_puthex64_all(panel->reset_gpio);
		uart_puts_all("/");
		uart_puthex64_all(panel->bias_enp_gpio);
		uart_puts_all("/");
		uart_puthex64_all(panel->bias_enn_gpio);
		uart_puts_all("\r\n");
		display_status = mk_stage0_display_prepare(panel, &display_ctx);
		uart_puts_all("[mk] display backend=");
		uart_puts_all(mk_stage0_panel_backend_name(panel) != 0 ? mk_stage0_panel_backend_name(panel) : "(none)");
		uart_puts_all(" status=");
		uart_put_display_status(display_status);
		uart_puts_all("\r\n");
		if (display_status == MK_STAGE0_DISPLAY_BAD_STATE) {
			uart_puts_all("[mk] display fail stage=");
			uart_put_display_fail_stage(mk_stage0_display_last_fail_stage());
			uart_puts_all(" idx=");
			uart_puthex64_all(mk_stage0_display_last_fail_index());
			uart_puts_all(" cmd=");
			uart_puthex64_all(mk_stage0_display_last_fail_cmd());
			uart_puts_all("\r\n");
			if (mk_stage0_display_last_fail_stage() == MK_STAGE0_DISPLAY_FAIL_BIAS_I2C) {
				uart_puts_all("[mk] bias-i2c err=");
				uart_puthex64_all((uint64_t) (uint32_t) mk_stage0_mtk_i2c_last_error());
				uart_puts_all(" status=");
				uart_puthex64_all((uint64_t) mk_stage0_mtk_i2c_last_status());
				uart_puts_all(" dbg0=");
				uart_puthex64_all((uint64_t) mk_stage0_mtk_i2c_last_debug0());
				uart_puts_all(" dbg1=");
				uart_puthex64_all((uint64_t) mk_stage0_mtk_i2c_last_debug1());
				uart_puts_all("\r\n");
			}
		}
	}

	phx_stage = mk_fdt_find_chosen_string((const void *) (uintptr_t) fdt_ptr,
					 "mk,phoenix-bootstage");
	phx_partition = mk_fdt_find_chosen_string((const void *) (uintptr_t) fdt_ptr,
					    "mk,phoenix-reserve-partition");
	phx_partition_fallback =
		mk_fdt_find_chosen_string((const void *) (uintptr_t) fdt_ptr,
				      "mk,phoenix-reserve-fallback-partition");
	phx_magic = mk_fdt_find_chosen_string((const void *) (uintptr_t) fdt_ptr,
					"mk,phoenix-record-magic");
	(void) mk_fdt_find_chosen_u64((const void *) (uintptr_t) fdt_ptr,
				 "mk,phoenix-reserve-offset-ufs", &phx_ufs_offset);
	(void) mk_fdt_find_chosen_u64((const void *) (uintptr_t) fdt_ptr,
				 "mk,phoenix-reserve-offset-emmc", &phx_emmc_offset);
	if (phx_stage == 0) {
		phx_stage = MK_DEVICE_PHOENIX_BOOTSTAGE;
	}
	if (phx_partition == 0) {
		phx_partition = MK_DEVICE_PHOENIX_PRIMARY_PARTITION;
	}
	if (phx_partition_fallback == 0) {
		phx_partition_fallback = MK_DEVICE_PHOENIX_FALLBACK_PARTITION;
	}
	if (phx_magic == 0) {
		phx_magic = MK_DEVICE_PHOENIX_RECORD_MAGIC;
	}
	if (phx_ufs_offset == 0) {
		phx_ufs_offset = MK_DEVICE_PHOENIX_UFS_OFFSET;
	}
	if (phx_emmc_offset == 0) {
		phx_emmc_offset = MK_DEVICE_PHOENIX_EMMC_OFFSET;
	}

	uart_puts_all("[mk] phx_stage=");
	uart_puts_all(phx_stage != 0 ? phx_stage : "(none)");
	uart_puts_all(" part=");
	uart_puts_all(phx_partition != 0 ? phx_partition : "(none)");
	uart_puts_all(" fallback=");
	uart_puts_all(phx_partition_fallback != 0 ? phx_partition_fallback : "(none)");
	uart_puts_all("\r\n");
	uart_puts_all("[mk] phx_magic=");
	uart_puts_all(phx_magic != 0 ? phx_magic : "(none)");
	uart_puts_all(" ufs_off=0x");
	uart_puthex64_all(phx_ufs_offset);
	uart_puts_all(" emmc_off=0x");
	uart_puthex64_all(phx_emmc_offset);
	uart_puts_all("\r\n");
	/* Keep the pre-draw path short, but do not leave visible work after draw. */
	for (delay = 0; delay < 2000000ULL; delay++) {
		if ((delay & 0x3fffULL) == 0) {
			pet_wdt();
		}
		__asm__ volatile("");
		}

		uart_puts_all("[mk] payload loop end\r\n");
		discover_peacock_partitions();
		if (!peacock_boot_targets_missing() && vol_down_held()) {
			uart_puts_all("[mk] peacock boot found, direct chainload by key request\r\n");
			mk_ui_boot_status("BOOTING");
			mk_boot_linux(fdt_ptr, g_peacock_boot_lba);
			uart_puts_all("[mk] direct linux handoff returned, entering menu fallback\r\n");
		}

	mk_ui_set_boot_fb(&info, fb_fallback_width, fb_fallback_height, fb_fallback_align);

	/* Offline charging: if booted by charger insertion only, enter charging loop */
	if (mk_pmic_boot_is_charger_only((const void *) (uintptr_t) fdt_ptr) != 0U) {
		uart_puts_all("[mk] charger-only boot detected\r\n");
		for (;;) {
			uint8_t charging_exit = enter_offline_charging(&info,
				fb_fallback_width, fb_fallback_height, fb_fallback_align);
			if (charging_exit == MK_CHARGING_EXIT_BOOT) {
				uart_puts_all("[mk] charging exit: boot\r\n");
				break;
			}
			if (charging_exit == MK_CHARGING_EXIT_MENU) {
				uart_puts_all("[mk] charging exit: menu\r\n");
				goto enter_menu;
			}
			/* MK_CHARGING_EXIT_OFF — charger removed */
			uart_puts_all("[mk] charging exit: power off\r\n");
			mk_pmic_power_off();
			/* Should not return, but if it does, spin */
		}
	}

enter_menu:
	draw_pattern(&info, fb_fallback_width, fb_fallback_height, fb_fallback_align);
	if (peacock_boot_targets_missing()) {
		uart_puts_all("[mk] peacock labels unresolved, entering menu fallback\r\n");
		enter_fastboot_fallback(&info, fb_fallback_width, fb_fallback_height, fb_fallback_align, 0U);
	} else {
		uart_puts_all("[mk] peacock boot found, entering menu by default\r\n");
		for (;;) {
			uint8_t action = enter_fastboot_fallback(&info, fb_fallback_width, fb_fallback_height,
							      fb_fallback_align, 1U);
			const uint8_t *staged_kernel;
			uint32_t staged_kernel_size;

			if (action == MK_FASTBOOT_ACTION_BOOT_STAGED_KERNEL) {
				staged_kernel = mk_stage0_mtk_usb_fastboot_download_buf();
				staged_kernel_size = mk_stage0_mtk_usb_fastboot_download_size();
				if (staged_kernel == 0 || staged_kernel_size == 0U) {
					uart_puts_all("[mk] staged kernel missing, re-entering menu\r\n");
					continue;
				}
				uart_puts_all("[mk] booting staged kernel bytes=0x");
				uart_puthex64_all((uint64_t) staged_kernel_size);
				uart_puts_all("\r\n");
				mk_ui_boot_status("BOOTING");
				mk_boot_linux_override_kernel(fdt_ptr, g_peacock_boot_lba,
							       staged_kernel, staged_kernel_size);
				uart_puts_all("[mk] staged kernel handoff returned, re-entering menu\r\n");
				continue;
			}
			if (action == MK_FASTBOOT_ACTION_CONTINUE) {
				uart_puts_all("[mk] continuing to linux handoff\r\n");
				mk_ui_boot_status("BOOTING");
				mk_boot_linux(fdt_ptr, g_peacock_boot_lba);
				uart_puts_all("[mk] linux handoff returned, re-entering menu\r\n");
				continue;
			}
			if (action == MK_FASTBOOT_ACTION_POWEROFF) {
				uart_puts_all("[mk] power off\r\n");
				mk_ui_boot_status("GOODBYE");
				delay_ms_calibrated(500U);
				mk_pmic_power_off();
			}
		}
	}
	arm_recovery_wdt();
	delay_ms_calibrated(1500U);
	trigger_recovery_wdt_reset();
}
