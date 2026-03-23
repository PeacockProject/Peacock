#include <stdint.h>
#include "mtk_panel.h"
#include "mtk_display.h"
#include "mtk_i2c.h"
#include "mtk_gpio.h"
#include "mtk_usb.h"
#include "mtk_storage.h"
#include "mk_boot.h"
#include "peacock_logo_asset.h"

#define FDT_MAGIC 0xd00dfeedU
#define FDT_BEGIN_NODE 1U
#define FDT_END_NODE 2U
#define FDT_PROP 3U
#define FDT_NOP 4U
#define FDT_END 9U
#define MAX_FDT_DEPTH 32
#define KEY_VOLUMEUP 115U
#define KEY_VOLUMEDOWN 114U

#define KP_BASE 0x10010000ULL
#define KP_MEM1 0x0004U
#define KP_MEM2 0x0008U
#define KP_MEM3 0x000cU
#define KP_MEM4 0x0010U
#define KP_MEM5 0x0014U

#define MTK_PMIC_WRAP_BASE 0x1000d000ULL
#define PWRAP_INIT_DONE2 0x0a0U
#define PWRAP_WACS2_CMD 0x0c20U
#define PWRAP_WACS2_RDATA 0x0c24U
#define PWRAP_WACS2_VLDCLR 0x0c28U
#define PWRAP_WACS_FSM_MASK (0x7U << 16)
#define PWRAP_WACS_FSM_IDLE (0x0U << 16)
#define PWRAP_WACS_FSM_WFVLDCLR (0x6U << 16)

#define MT6357_TOPSTATUS_ADDR 0x24U
#define MT6357_PONSTS_ADDR 0x0cU
#define MT6357_POFFSTS_ADDR 0x0eU
#define MT6357_TOP_RST_STATUS_ADDR 0x152U
#define MT6357_PWRKEY_DEB_SHIFT 1U
#define MT6357_HOMEKEY_DEB_SHIFT 3U

typedef struct {
	uint64_t addr;
	uint64_t size;
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	const char *format;
} simplefb_info_t;

/* MediaTek TOPRGU watchdog (from upstream mtk_wdt driver). */
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

/* MediaTek OVL0 overlay plane carrying inherited warning text. */
#define MTK_DISP_OVL0_BASE 0x1400b000ULL
#define MTK_DISP_OVL0_2L_BASE 0x1400c000ULL
#define MTK_DISP_OVL_TRIG 0x010U
#define MTK_DISP_OVL_SRC_CON 0x02cU
#define MTK_DISP_OVL_L0_CON 0x030U
#define MTK_DISP_OVL_L0_SRC_SIZE 0x038U
#define MTK_DISP_OVL_L0_OFFSET 0x03cU
#define MTK_DISP_OVL_L0_PITCH 0x044U
#define MTK_DISP_OVL_L0_ADDR 0x0f40U
#define MTK_DISP_OVL_L0_CLEAR 0x25cU
#define MTK_DISP_OVL_L3_CON 0x090U
#define MTK_DISP_OVL_L3_SRC_SIZE 0x098U
#define MTK_DISP_OVL_L3_OFFSET 0x09cU
#define MTK_DISP_OVL_L3_ADDR 0x0fa0U
#define MTK_DISP_OVL_L3_PITCH 0x0a4U
#define MTK_DISP_OVL_RDMA3_CTRL 0x120U
#define MTK_DISP_OVL_L3_CLEAR 0x268U
#define MTK_DISP_OVL_DATAPATH_EXT_CON 0x324U
#define MTK_DISP_OVL_EL2_CON 0x370U
#define MTK_DISP_OVL_EL2_SRC_SIZE 0x378U
#define MTK_DISP_OVL_EL2_OFFSET 0x37cU
#define MTK_DISP_OVL_EL2_ADDR 0x0fb8U
#define MTK_DISP_OVL_EL2_PITCH 0x384U
#define MTK_DISP_OVL_EL2_CLEAR 0x398U

#define MTK_OPPO_A16_WARNBUF_ADDR 0x41438fc0ULL
#define MTK_OPPO_A16_WARNBUF_WIDTH 720U
#define MTK_OPPO_A16_WARNBUF_HEIGHT 101U
#define MTK_OPPO_A16_WARNBUF_PITCH 1440U
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
} mk_stage0_wdt_state_t;
static mk_stage0_wdt_state_t g_wdt_saved_state;
static uint64_t g_peacock_boot_lba;
static uint64_t g_peacock_boot_count;
static uint64_t g_peacock_root_lba;
static uint64_t g_peacock_root_count;
static uint8_t g_msdc_dma_sector_buf[512] __attribute__((aligned(64)));
static int g_peacock_boot_found;
static int g_peacock_root_found;
static uint8_t g_msdc_multi_write_disable;

typedef struct {
	uint32_t vol_up_gpio;
	uint32_t vol_down_gpio;
	uint32_t vol_up_hwcode;
	uint32_t vol_down_hwcode;
	uint8_t last_up_raw;
	uint8_t last_down_raw;
	uint8_t last_power_raw;
	uint8_t stable_up_pressed;
	uint8_t stable_down_pressed;
	uint8_t stable_power_pressed;
	uint8_t up_stable_count;
	uint8_t down_stable_count;
	uint8_t power_stable_count;
	uint8_t has_any;
} menu_button_state_t;

static menu_button_state_t g_menu_buttons = {
	.vol_up_gpio = MK_STAGE0_GPIO_NONE,
	.vol_down_gpio = MK_STAGE0_GPIO_NONE,
	.vol_up_hwcode = 0xffffffffU,
	.vol_down_hwcode = 0xffffffffU,
	.last_up_raw = 0U,
	.last_down_raw = 0U,
	.last_power_raw = 0U,
	.stable_up_pressed = 0U,
	.stable_down_pressed = 0U,
	.stable_power_pressed = 0U,
	.up_stable_count = 0U,
	.down_stable_count = 0U,
	.power_stable_count = 0U,
	.has_any = 0U,
};

void uart_puts_all(const char *s);
void uart_puthex64_all(uint64_t v);
static void arm_recovery_wdt(void);
static void trigger_recovery_wdt_reset(void);
static void arm_normal_wdt(void);
static uint32_t align_up_u32(uint32_t value, uint32_t align);
static void clean_dcache_range(uintptr_t start, uint64_t len);

static uint32_t be32_read(const uint8_t *p)
{
	return ((uint32_t) p[0] << 24) |
	       ((uint32_t) p[1] << 16) |
	       ((uint32_t) p[2] << 8) |
	       (uint32_t) p[3];
}

static uint64_t be64_read(const uint8_t *p)
{
	uint64_t hi = be32_read(p);
	uint64_t lo = be32_read(p + 4);
	return (hi << 32) | lo;
}

static uint32_t str_len(const char *s)
{
	uint32_t n = 0;
	while (s != 0 && s[n] != '\0') {
		n++;
	}
	return n;
}

static int str_eq(const char *a, const char *b)
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
	uint32_t key_len = str_len(key);
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

static int str_contains(const char *s, const char *needle)
{
	uint32_t i;
	uint32_t nlen;

	if (s == 0 || needle == 0) {
		return 0;
	}
	nlen = str_len(needle);
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

static int value_has_string(const uint8_t *buf, uint32_t len, const char *needle)
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

static uint32_t reg_read32_local(uint64_t addr)
{
	return *(volatile uint32_t *)(uintptr_t) addr;
}

static void reg_write32_local(uint64_t addr, uint32_t value)
{
	*(volatile uint32_t *)(uintptr_t) addr = value;
}

typedef struct {
	uint8_t valid;
	uint32_t ovl0_src_con;
	uint32_t ovl0_l0_con;
	uint32_t ovl0_l0_src_size;
	uint32_t ovl0_l0_offset;
	uint32_t ovl0_l0_pitch;
	uint32_t ovl0_l0_addr;
	uint32_t ovl0_l3_con;
	uint32_t ovl0_l3_src_size;
	uint32_t ovl0_l3_offset;
	uint32_t ovl0_l3_addr;
	uint32_t ovl0_l3_pitch;
	uint32_t ovl0_datapath_ext_con;
	uint32_t ovl0_el2_con;
	uint32_t ovl0_el2_src_size;
	uint32_t ovl0_el2_offset;
	uint32_t ovl0_el2_addr;
	uint32_t ovl0_el2_pitch;
	uint32_t ovl02_src_con;
	uint32_t ovl02_l0_con;
	uint32_t ovl02_l0_src_size;
	uint32_t ovl02_l0_offset;
	uint32_t ovl02_l0_pitch;
	uint32_t ovl02_l0_addr;
} mk_stage0_display_ovl_state_t;

static mk_stage0_display_ovl_state_t g_display_ovl_state;

static void snapshot_display_ovl_state_once(void)
{
	mk_stage0_display_ovl_state_t *s = &g_display_ovl_state;

	if (s->valid != 0U) {
		return;
	}

	s->ovl0_src_con = reg_read32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_SRC_CON);
	s->ovl0_l0_con = reg_read32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L0_CON);
	s->ovl0_l0_src_size = reg_read32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L0_SRC_SIZE);
	s->ovl0_l0_offset = reg_read32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L0_OFFSET);
	s->ovl0_l0_pitch = reg_read32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L0_PITCH);
	s->ovl0_l0_addr = reg_read32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L0_ADDR);
	s->ovl0_l3_con = reg_read32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_CON);
	s->ovl0_l3_src_size = reg_read32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_SRC_SIZE);
	s->ovl0_l3_offset = reg_read32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_OFFSET);
	s->ovl0_l3_addr = reg_read32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_ADDR);
	s->ovl0_l3_pitch = reg_read32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_PITCH);
	s->ovl0_datapath_ext_con = reg_read32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_DATAPATH_EXT_CON);
	s->ovl0_el2_con = reg_read32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_EL2_CON);
	s->ovl0_el2_src_size = reg_read32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_EL2_SRC_SIZE);
	s->ovl0_el2_offset = reg_read32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_EL2_OFFSET);
	s->ovl0_el2_addr = reg_read32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_EL2_ADDR);
	s->ovl0_el2_pitch = reg_read32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_EL2_PITCH);
	s->ovl02_src_con = reg_read32_local(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_SRC_CON);
	s->ovl02_l0_con = reg_read32_local(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_CON);
	s->ovl02_l0_src_size = reg_read32_local(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_SRC_SIZE);
	s->ovl02_l0_offset = reg_read32_local(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_OFFSET);
	s->ovl02_l0_pitch = reg_read32_local(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_PITCH);
	s->ovl02_l0_addr = reg_read32_local(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_ADDR);
	s->valid = 1U;
	uart_puts_all("[mk] display ovl: snapshot\r\n");
}

void mk_stage0_display_restore_for_linux(void)
{
	mk_stage0_display_ovl_state_t *s = &g_display_ovl_state;

	if (s->valid == 0U) {
		uart_puts_all("[mk] display ovl: no snapshot\r\n");
		return;
	}

	reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_SRC_CON, s->ovl0_src_con);
	reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L0_CON, s->ovl0_l0_con);
	reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L0_SRC_SIZE, s->ovl0_l0_src_size);
	reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L0_OFFSET, s->ovl0_l0_offset);
	reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L0_PITCH, s->ovl0_l0_pitch);
	reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L0_ADDR, s->ovl0_l0_addr);
	reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_CON, s->ovl0_l3_con);
	reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_SRC_SIZE, s->ovl0_l3_src_size);
	reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_OFFSET, s->ovl0_l3_offset);
	reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_ADDR, s->ovl0_l3_addr);
	reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_PITCH, s->ovl0_l3_pitch);
	reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_DATAPATH_EXT_CON,
			 s->ovl0_datapath_ext_con);
	reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_EL2_CON, s->ovl0_el2_con);
	reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_EL2_SRC_SIZE, s->ovl0_el2_src_size);
	reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_EL2_OFFSET, s->ovl0_el2_offset);
	reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_EL2_ADDR, s->ovl0_el2_addr);
	reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_EL2_PITCH, s->ovl0_el2_pitch);
	reg_write32_local(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_SRC_CON, s->ovl02_src_con);
	reg_write32_local(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_CON, s->ovl02_l0_con);
	reg_write32_local(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_SRC_SIZE, s->ovl02_l0_src_size);
	reg_write32_local(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_OFFSET, s->ovl02_l0_offset);
	reg_write32_local(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_PITCH, s->ovl02_l0_pitch);
	reg_write32_local(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_ADDR, s->ovl02_l0_addr);
	reg_write32_local(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_TRIG, 1U);
	reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_TRIG, 1U);
	uart_puts_all("[mk] display ovl: restored\r\n");
}

static int menu_pwrap_wait_idle(void)
{
	uint32_t i;
	for (i = 0; i < 1000U; i++) {
		uint32_t val = reg_read32_local(MTK_PMIC_WRAP_BASE + PWRAP_WACS2_RDATA);
		if ((val & PWRAP_WACS_FSM_MASK) == PWRAP_WACS_FSM_IDLE) {
			return 0;
		}
	}
	return -1;
}

static int menu_pwrap_wait_vldclr(uint16_t *rdata)
{
	uint32_t i;
	for (i = 0; i < 1000U; i++) {
		uint32_t val = reg_read32_local(MTK_PMIC_WRAP_BASE + PWRAP_WACS2_RDATA);
		if ((val & PWRAP_WACS_FSM_MASK) == PWRAP_WACS_FSM_WFVLDCLR) {
			*rdata = (uint16_t) (val & 0xffffU);
			return 0;
		}
	}
	return -1;
}

static int menu_pwrap_read16(uint32_t adr, uint16_t *rdata)
{
	uint32_t val;

	if (rdata == 0) {
		return -1;
	}
	if (reg_read32_local(MTK_PMIC_WRAP_BASE + PWRAP_INIT_DONE2) == 0U) {
		return -1;
	}
	val = reg_read32_local(MTK_PMIC_WRAP_BASE + PWRAP_WACS2_RDATA);
	if ((val & PWRAP_WACS_FSM_MASK) == PWRAP_WACS_FSM_WFVLDCLR) {
		reg_write32_local(MTK_PMIC_WRAP_BASE + PWRAP_WACS2_VLDCLR, 1U);
	}
	if (menu_pwrap_wait_idle() != 0) {
		return -1;
	}

	reg_write32_local(MTK_PMIC_WRAP_BASE + PWRAP_WACS2_CMD, ((adr >> 1U) << 16));
	if (menu_pwrap_wait_vldclr(rdata) != 0) {
		return -1;
	}
	reg_write32_local(MTK_PMIC_WRAP_BASE + PWRAP_WACS2_VLDCLR, 1U);
	return 0;
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

	wdt_mode = reg_read32_local(wdt_base + MTK_WDT_MODE);
	wdt_len = reg_read32_local(wdt_base + MTK_WDT_LENGTH);
	wdt_int = reg_read32_local(wdt_base + MTK_WDT_INTERVAL);
	wdt_nrst2 = reg_read32_local(wdt_base + MTK_WDT_NONRST2);
	wdt_req = reg_read32_local(wdt_base + MTK_WDT_REQ_MODE);
	wdt_irq = reg_read32_local(wdt_base + MTK_WDT_REQ_IRQ_EN);
	wdt_latch2 = reg_read32_local(wdt_base + MTK_WDT_LATCH_CTL2);

	pwrap_init = reg_read32_local(MTK_PMIC_WRAP_BASE + PWRAP_INIT_DONE2);
	if (menu_pwrap_read16(MT6357_PONSTS_ADDR, &ponsts) != 0) {
		have_pmic = 0;
	}
	if (menu_pwrap_read16(MT6357_POFFSTS_ADDR, &poffsts) != 0) {
		have_pmic = 0;
	}
	if (menu_pwrap_read16(MT6357_TOP_RST_STATUS_ADDR, &top_rst) != 0) {
		have_pmic = 0;
	}
	if (menu_pwrap_read16(MT6357_TOPSTATUS_ADDR, &topstatus) != 0) {
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

	reg_mode = reg_read32_local(g_wdt_base + MTK_WDT_MODE);
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
	reg_write32_local(g_wdt_base + MTK_WDT_MODE, reg_mode);
	reg_mode_post = reg_read32_local(g_wdt_base + MTK_WDT_MODE);
	uart_puts_all(" post=0x");
	uart_puthex64_all((uint64_t) reg_mode_post);
	uart_puts_all("\r\n");

	reg_req_mode = reg_read32_local(g_wdt_base + MTK_WDT_REQ_MODE);
	uart_puts_all("[mk] wdt-ab: req pre=0x");
	uart_puthex64_all((uint64_t) reg_req_mode);
	reg_req_mode &= ~(MTK_WDT_REQ_MODE_RECOVERY_SEQ & ~MTK_WDT_REQ_MODE_KEY);
	uart_puts_all(" wr=0x");
	uart_puthex64_all((uint64_t) (MTK_WDT_REQ_MODE_KEY | reg_req_mode));
	reg_write32_local(g_wdt_base + MTK_WDT_REQ_MODE,
			  MTK_WDT_REQ_MODE_KEY | reg_req_mode);
	reg_req_mode_post = reg_read32_local(g_wdt_base + MTK_WDT_REQ_MODE);
	uart_puts_all(" post=0x");
	uart_puthex64_all((uint64_t) reg_req_mode_post);
	uart_puts_all("\r\n");

	reg_req_irq = reg_read32_local(g_wdt_base + MTK_WDT_REQ_IRQ_EN);
	uart_puts_all("[mk] wdt-ab: irq pre=0x");
	uart_puthex64_all((uint64_t) reg_req_irq);
	reg_req_irq = 0U;
	uart_puts_all(" wr=0x");
	uart_puthex64_all((uint64_t) MTK_WDT_REQ_IRQ_EN_KEY);
	reg_write32_local(g_wdt_base + MTK_WDT_REQ_IRQ_EN,
			  MTK_WDT_REQ_IRQ_EN_KEY | reg_req_irq);
	reg_req_irq_post = reg_read32_local(g_wdt_base + MTK_WDT_REQ_IRQ_EN);
	uart_puts_all(" post=0x");
	uart_puthex64_all((uint64_t) reg_req_irq_post);
	uart_puts_all("\r\n");

	reg_write32_local(g_wdt_base + MTK_WDT_RST, MTK_WDT_RST_RELOAD);
	mk_stage0_log_reset_watchdog_state("wdt-ab-after");
}

static uint8_t menu_power_pressed_from_pmic(void)
{
	uint16_t topstatus = 0U;
	uint8_t pwr_deb;

	if (menu_pwrap_read16(MT6357_TOPSTATUS_ADDR, &topstatus) != 0) {
		return 0U;
	}

	pwr_deb = (uint8_t) ((topstatus >> MT6357_PWRKEY_DEB_SHIFT) & 0x1U);
	return pwr_deb == 0U ? 1U : 0U;
}

static uint8_t menu_up_pressed_from_pmic_homekey(void)
{
	uint16_t topstatus = 0U;
	uint8_t home_deb;

	if (menu_pwrap_read16(MT6357_TOPSTATUS_ADDR, &topstatus) != 0) {
		return 0U;
	}

	home_deb = (uint8_t) ((topstatus >> MT6357_HOMEKEY_DEB_SHIFT) & 0x1U);
	return home_deb == 0U ? 1U : 0U;
}

static int fdt_find_chosen_prop(const void *fdt, const char *prop_name,
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
	uint8_t chosen_stack[MAX_FDT_DEPTH] = {0};
	int depth = -1;

	if (base == 0 || prop_name == 0 || out_value == 0 || out_len == 0) {
		return 0;
	}
	if (be32_read(base) != FDT_MAGIC) {
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

		if (token == FDT_BEGIN_NODE) {
			const char *node_name = (const char *) p;

			depth++;
			if (depth >= MAX_FDT_DEPTH) {
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

		if (token == FDT_END_NODE) {
			if (depth >= 0) {
				depth--;
			}
			continue;
		}

		if (token == FDT_NOP) {
			continue;
		}

		if (token == FDT_END) {
			break;
		}

		if (token == FDT_PROP) {
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

static const char *fdt_find_chosen_string(const void *fdt, const char *prop_name)
{
	const uint8_t *value = 0;
	uint32_t len = 0;

	if (!fdt_find_chosen_prop(fdt, prop_name, &value, &len) || len == 0) {
		return 0;
	}
	return (const char *) value;
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

	serial = fdt_find_chosen_string(fdt, "serial-number");
	if (serial != 0) {
		copied = copy_serial_token(serial, dst, dst_cap);
		if (copied != 0U) {
			return copied;
		}
	}

	bootargs = fdt_find_chosen_string(fdt, "bootargs");
	copied = parse_android_serial_from_bootargs(bootargs, dst, dst_cap);
	return copied;
}

static int fdt_find_chosen_u64(const void *fdt, const char *prop_name, uint64_t *out_value)
{
	const uint8_t *value = 0;
	uint32_t len = 0;

	if (out_value == 0) {
		return 0;
	}
	if (!fdt_find_chosen_prop(fdt, prop_name, &value, &len)) {
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

#define MBOOT_PARAMS_DEF_SRAM 1U
#define MBOOT_PARAMS_DEF_DRAM 2U
#define MBOOT_PARAMS_SIG 0x43474244U /* DBRR */
#define MBOOT_MEMINFO_MAGIC1 0x61646472U /* "addr" */
#define MBOOT_MEMINFO_MAGIC2 0x73697a65U /* "size" */

static uint32_t le32_read_bytes(const uint8_t *p)
{
	if (p == 0) {
		return 0U;
	}

	return (uint32_t) p[0]
	     | ((uint32_t) p[1] << 8)
	     | ((uint32_t) p[2] << 16)
	     | ((uint32_t) p[3] << 24);
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

	sig = reg_read32_local((uint64_t) base + 0U);
	off_pl = reg_read32_local((uint64_t) base + 4U);
	off_lpl = reg_read32_local((uint64_t) base + 8U);
	sz_pl = reg_read32_local((uint64_t) base + 12U);
	off_lk = reg_read32_local((uint64_t) base + 16U);
	off_llk = reg_read32_local((uint64_t) base + 20U);
	sz_lk = reg_read32_local((uint64_t) base + 24U);
	sz_buffer = reg_read32_local((uint64_t) base + 40U);
	off_linux = reg_read32_local((uint64_t) base + 44U);

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
		uart_puthex64_all(reg_read32_local((uint64_t) base + off_lpl + 0U));
		uart_puts_all(" [1]=0x");
		uart_puthex64_all(reg_read32_local((uint64_t) base + off_lpl + 4U));
		uart_puts_all(" [2]=0x");
		uart_puthex64_all(reg_read32_local((uint64_t) base + off_lpl + 8U));
		uart_puts_all(" [3]=0x");
		uart_puthex64_all(reg_read32_local((uint64_t) base + off_lpl + 12U));
		uart_puts_all("\r\n");
	}

	if (off_llk != 0U && off_llk + 16U <= size) {
		uart_puts_all("[mk] rr: last-lk [0]=0x");
		uart_puthex64_all(reg_read32_local((uint64_t) base + off_llk + 0U));
		uart_puts_all(" [1]=0x");
		uart_puthex64_all(reg_read32_local((uint64_t) base + off_llk + 4U));
		uart_puts_all(" [2]=0x");
		uart_puthex64_all(reg_read32_local((uint64_t) base + off_llk + 8U));
		uart_puts_all(" [3]=0x");
		uart_puthex64_all(reg_read32_local((uint64_t) base + off_llk + 12U));
		uart_puts_all("\r\n");
	}

	(void) off_pl;
	(void) sz_pl;
	(void) off_lk;
	(void) sz_lk;
}

static void mk_stage0_log_retained_reset_provenance(const void *fdt)
{
	const uint8_t *value = 0;
	uint32_t len = 0;
	uint32_t start;
	uint32_t size;
	uint32_t def_type;
	uint32_t offset;

	if (!fdt_find_chosen_prop(fdt, "ram_console", &value, &len) || len < 16U) {
		uart_puts_all("[mk] rr: no ram_console\r\n");
		return;
	}

	start = le32_read_bytes(value + 0U);
	size = le32_read_bytes(value + 4U);
	def_type = le32_read_bytes(value + 8U);
	offset = le32_read_bytes(value + 12U);

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
		uint32_t magic1 = reg_read32_local((uint64_t) info_base + 0U);
		uint32_t mrdump_addr = reg_read32_local((uint64_t) info_base + 20U);
		uint32_t mrdump_size = reg_read32_local((uint64_t) info_base + 24U);
		uint32_t dram_addr = reg_read32_local((uint64_t) info_base + 28U);
		uint32_t dram_size = reg_read32_local((uint64_t) info_base + 32U);
		uint32_t mini_addr = reg_read32_local((uint64_t) info_base + 44U);
		uint32_t mini_size = reg_read32_local((uint64_t) info_base + 48U);
		uint32_t magic2 = reg_read32_local((uint64_t) info_base + 52U);

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

static int fdt_find_compatible_prop(const void *fdt, const char *needle,
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
	uint8_t match_stack[MAX_FDT_DEPTH] = {0};
	int depth = -1;

	if (base == 0 || needle == 0 || prop_name == 0 || out_value == 0 || out_len == 0) {
		return 0;
	}
	if (be32_read(base) != FDT_MAGIC) {
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

		if (token == FDT_BEGIN_NODE) {
			depth++;
			if (depth >= MAX_FDT_DEPTH) {
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
		if (token == FDT_END_NODE) {
			if (depth >= 0) {
				depth--;
			}
			continue;
		}
		if (token == FDT_NOP) {
			continue;
		}
		if (token == FDT_END) {
			break;
		}
		if (token != FDT_PROP) {
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

static uint32_t parse_gpio_pin_from_prop(const uint8_t *value, uint32_t len, uint32_t fallback)
{
	if (value == 0 || len < 8U) {
		return fallback;
	}
	return be32_read(value + 4);
}

static void keypad_raw_read(uint16_t state[5])
{
	state[0] = (uint16_t) reg_read32_local(KP_BASE + KP_MEM1);
	state[1] = (uint16_t) reg_read32_local(KP_BASE + KP_MEM2);
	state[2] = (uint16_t) reg_read32_local(KP_BASE + KP_MEM3);
	state[3] = (uint16_t) reg_read32_local(KP_BASE + KP_MEM4);
	state[4] = (uint16_t) reg_read32_local(KP_BASE + KP_MEM5);
}

static uint8_t keypad_hwcode_pressed(uint32_t hwcode)
{
	uint16_t state[5];
	uint32_t idx;
	uint32_t bit;

	if (hwcode == 0xffffffffU || hwcode >= 72U) {
		return 0U;
	}

	keypad_raw_read(state);
	idx = hwcode / 16U;
	bit = hwcode % 16U;
	if (idx >= 5U) {
		return 0U;
	}
	return ((state[idx] & (uint16_t) (1U << bit)) == 0U) ? 1U : 0U;
}

static void init_menu_buttons_from_fdt(const void *fdt)
{
	const uint8_t *value = 0;
	uint32_t len = 0;
	uint32_t map_num = 0U;
	uint32_t i;

	g_menu_buttons.vol_up_gpio = MK_STAGE0_GPIO_NONE;
	g_menu_buttons.vol_down_gpio = MK_STAGE0_GPIO_NONE;
	g_menu_buttons.vol_up_hwcode = 0xffffffffU;
	g_menu_buttons.vol_down_hwcode = 0xffffffffU;
	g_menu_buttons.last_up_raw = 0U;
	g_menu_buttons.last_down_raw = 0U;
	g_menu_buttons.last_power_raw = 0U;
	g_menu_buttons.stable_up_pressed = 0U;
	g_menu_buttons.stable_down_pressed = 0U;
	g_menu_buttons.stable_power_pressed = 0U;
	g_menu_buttons.up_stable_count = 0U;
	g_menu_buttons.down_stable_count = 0U;
	g_menu_buttons.power_stable_count = 0U;
	g_menu_buttons.has_any = 0U;

	if (fdt_find_compatible_prop(fdt, "mediatek,kp", "keypad,volume-up", &value, &len)) {
		g_menu_buttons.vol_up_gpio = parse_gpio_pin_from_prop(value, len, MK_STAGE0_GPIO_NONE);
	}
	if (fdt_find_compatible_prop(fdt, "mediatek,kp", "keypad,volume-down", &value, &len)) {
		g_menu_buttons.vol_down_gpio = parse_gpio_pin_from_prop(value, len, MK_STAGE0_GPIO_NONE);
	}

	if (fdt_find_compatible_prop(fdt, "mediatek,kp", "mediatek,kpd-hw-map-num", &value, &len) &&
	    len >= 4U) {
		map_num = be32_read(value);
		if (map_num > 72U) {
			map_num = 72U;
		}
	}
	if (map_num != 0U &&
	    fdt_find_compatible_prop(fdt, "mediatek,kp", "mediatek,kpd-hw-init-map", &value, &len) &&
	    len >= (map_num * 4U)) {
		for (i = 0; i < map_num; i++) {
			uint32_t keycode = be32_read(value + (i * 4U));
			if (keycode == KEY_VOLUMEUP && g_menu_buttons.vol_up_hwcode == 0xffffffffU) {
				g_menu_buttons.vol_up_hwcode = i;
			}
			if (keycode == KEY_VOLUMEDOWN && g_menu_buttons.vol_down_hwcode == 0xffffffffU) {
				g_menu_buttons.vol_down_hwcode = i;
			}
		}
	}

	if (g_menu_buttons.vol_up_gpio != MK_STAGE0_GPIO_NONE ||
	    g_menu_buttons.vol_down_gpio != MK_STAGE0_GPIO_NONE ||
	    g_menu_buttons.vol_up_hwcode != 0xffffffffU ||
	    g_menu_buttons.vol_down_hwcode != 0xffffffffU) {
		g_menu_buttons.has_any = 1U;
	}
	if (reg_read32_local(MTK_PMIC_WRAP_BASE + PWRAP_INIT_DONE2) != 0U) {
		g_menu_buttons.has_any = 1U;
	}

	if (g_menu_buttons.has_any != 0U) {
		uart_puts_all("[mk] menu keys up.gpio=0x");
		uart_puthex64_all(g_menu_buttons.vol_up_gpio);
		uart_puts_all(" down.gpio=0x");
		uart_puthex64_all(g_menu_buttons.vol_down_gpio);
		uart_puts_all(" up.hw=0x");
		uart_puthex64_all(g_menu_buttons.vol_up_hwcode);
		uart_puts_all(" down.hw=0x");
		uart_puthex64_all(g_menu_buttons.vol_down_hwcode);
		uart_puts_all(" pwrap.init=0x");
		uart_puthex64_all(reg_read32_local(MTK_PMIC_WRAP_BASE + PWRAP_INIT_DONE2));
		uart_puts_all("\r\n");
	}
}

static uint8_t menu_button_is_pressed_gpio(uint32_t pin)
{
	if (pin == MK_STAGE0_GPIO_NONE) {
		return 0U;
	}
	return mk_stage0_mtk_gpio_read(pin) == 0U ? 1U : 0U;
}

static __attribute__((unused)) uint8_t vol_down_held(void)
{
	if (g_menu_buttons.vol_down_hwcode != 0xffffffffU) {
		return keypad_hwcode_pressed(g_menu_buttons.vol_down_hwcode);
	}
	if (g_menu_buttons.vol_down_gpio != MK_STAGE0_GPIO_NONE) {
		return menu_button_is_pressed_gpio(g_menu_buttons.vol_down_gpio);
	}
	return 0U;
}

static uint8_t menu_update_stable_signal(uint8_t raw, uint8_t *last_raw,
					 uint8_t *stable, uint8_t *stable_count)
{
	uint8_t edge = 0U;

	if (raw == *last_raw) {
		if (*stable_count < 5U) {
			*stable_count = (uint8_t) (*stable_count + 1U);
		}
	} else {
		*last_raw = raw;
		*stable_count = 0U;
	}

	if (*stable_count >= 3U && *stable != raw) {
		*stable = raw;
		if (raw != 0U) {
			edge = 1U;
		}
	}

	return edge;
}

static void menu_buttons_poll_edges(uint8_t *up_pressed_edge, uint8_t *down_pressed_edge,
				    uint8_t *select_pressed_edge)
{
	uint8_t up_raw = 0U;
	uint8_t down_raw = 0U;
	uint8_t power_raw = 0U;
	uint8_t home_up_raw = 0U;

	if (up_pressed_edge == 0 || down_pressed_edge == 0 || select_pressed_edge == 0) {
		return;
	}
	*up_pressed_edge = 0U;
	*down_pressed_edge = 0U;
	*select_pressed_edge = 0U;

	if (g_menu_buttons.has_any == 0U) {
		return;
	}

	/* Prefer keypad HW map over GPIO to avoid floating GPIO-only reads. */
	if (g_menu_buttons.vol_up_hwcode != 0xffffffffU) {
		up_raw = keypad_hwcode_pressed(g_menu_buttons.vol_up_hwcode);
	} else if (g_menu_buttons.vol_up_gpio != MK_STAGE0_GPIO_NONE) {
		up_raw = menu_button_is_pressed_gpio(g_menu_buttons.vol_up_gpio);
	} else {
		home_up_raw = menu_up_pressed_from_pmic_homekey();
		up_raw = home_up_raw;
	}

	if (g_menu_buttons.vol_down_hwcode != 0xffffffffU) {
		down_raw = keypad_hwcode_pressed(g_menu_buttons.vol_down_hwcode);
	} else if (g_menu_buttons.vol_down_gpio != MK_STAGE0_GPIO_NONE) {
		down_raw = menu_button_is_pressed_gpio(g_menu_buttons.vol_down_gpio);
	}

	power_raw = menu_power_pressed_from_pmic();

	*up_pressed_edge = menu_update_stable_signal(up_raw, &g_menu_buttons.last_up_raw,
						     &g_menu_buttons.stable_up_pressed,
						     &g_menu_buttons.up_stable_count);
	*down_pressed_edge = menu_update_stable_signal(down_raw, &g_menu_buttons.last_down_raw,
						       &g_menu_buttons.stable_down_pressed,
						       &g_menu_buttons.down_stable_count);
	*select_pressed_edge = menu_update_stable_signal(power_raw, &g_menu_buttons.last_power_raw,
							 &g_menu_buttons.stable_power_pressed,
							 &g_menu_buttons.power_stable_count);

	(void) home_up_raw;
}

static uint32_t menu_bpp_from_format(const char *fmt)
{
	if (fmt != 0 && str_eq(fmt, "r5g6b5")) {
		return 2U;
	}
	return 4U;
}

static uint32_t menu_resolve_fb(const simplefb_info_t *info, uint32_t fallback_width,
				uint32_t fallback_height, uint32_t fallback_align,
				volatile uint8_t **out_fb, uint32_t *out_w,
				uint32_t *out_h, uint32_t *out_stride)
{
	uint32_t w;
	uint32_t h;
	uint32_t stride;

	if (info == 0 || out_fb == 0 || out_w == 0 || out_h == 0 || out_stride == 0) {
		return 0U;
	}
	if (info->addr == 0U || menu_bpp_from_format(info->format) != 4U) {
		return 0U;
	}

	w = info->width;
	h = info->height;
	stride = info->stride;
	if ((w == 0U || h == 0U) && fallback_width != 0U && fallback_height != 0U) {
		w = fallback_width;
		h = fallback_height;
		if (stride == 0U) {
			stride = align_up_u32(w, fallback_align) * 4U;
		}
	}
	if (w == 0U || h == 0U || stride == 0U) {
		return 0U;
	}

	*out_fb = (volatile uint8_t *) (uintptr_t) info->addr;
	*out_w = w;
	*out_h = h;
	*out_stride = stride;
	return 1U;
}

static __attribute__((unused)) void menu_fill_rect32(volatile uint8_t *fb, uint32_t stride,
			     uint32_t fb_w, uint32_t fb_h,
			     uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t argb)
{
	uint32_t iy;
	uint32_t ix;
	uint32_t x_end;
	uint32_t y_end;

	if (fb == 0 || w == 0U || h == 0U || x >= fb_w || y >= fb_h) {
		return;
	}

	x_end = x + w;
	y_end = y + h;
	if (x_end > fb_w) {
		x_end = fb_w;
	}
	if (y_end > fb_h) {
		y_end = fb_h;
	}

	for (iy = y; iy < y_end; iy++) {
		volatile uint32_t *line = (volatile uint32_t *) (fb + ((uint64_t) iy * stride));
		for (ix = x; ix < x_end; ix++) {
			line[ix] = argb;
		}
	}
}

static uint8_t menu_glyph_row_5x7(char c, uint32_t row)
{
	static const uint8_t font5x7[128][7] = {
		['A'] = {0x0eU, 0x11U, 0x11U, 0x1fU, 0x11U, 0x11U, 0x11U},
		['B'] = {0x1eU, 0x11U, 0x11U, 0x1eU, 0x11U, 0x11U, 0x1eU},
		['C'] = {0x0eU, 0x11U, 0x10U, 0x10U, 0x10U, 0x11U, 0x0eU},
		['D'] = {0x1eU, 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x1eU},
		['E'] = {0x1fU, 0x10U, 0x10U, 0x1eU, 0x10U, 0x10U, 0x1fU},
		['F'] = {0x1fU, 0x10U, 0x10U, 0x1eU, 0x10U, 0x10U, 0x10U},
		['G'] = {0x0fU, 0x10U, 0x10U, 0x13U, 0x11U, 0x11U, 0x0fU},
		['H'] = {0x11U, 0x11U, 0x11U, 0x1fU, 0x11U, 0x11U, 0x11U},
		['I'] = {0x1fU, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U, 0x1fU},
		['J'] = {0x01U, 0x01U, 0x01U, 0x01U, 0x11U, 0x11U, 0x0eU},
		['K'] = {0x11U, 0x12U, 0x14U, 0x18U, 0x14U, 0x12U, 0x11U},
		['L'] = {0x10U, 0x10U, 0x10U, 0x10U, 0x10U, 0x10U, 0x1fU},
		['M'] = {0x11U, 0x1bU, 0x15U, 0x15U, 0x11U, 0x11U, 0x11U},
		['N'] = {0x11U, 0x11U, 0x19U, 0x15U, 0x13U, 0x11U, 0x11U},
		['O'] = {0x0eU, 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x0eU},
		['P'] = {0x1eU, 0x11U, 0x11U, 0x1eU, 0x10U, 0x10U, 0x10U},
		['Q'] = {0x0eU, 0x11U, 0x11U, 0x11U, 0x15U, 0x12U, 0x0dU},
		['R'] = {0x1eU, 0x11U, 0x11U, 0x1eU, 0x14U, 0x12U, 0x11U},
		['S'] = {0x0fU, 0x10U, 0x10U, 0x0eU, 0x01U, 0x01U, 0x1eU},
		['T'] = {0x1fU, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U},
		['U'] = {0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x0eU},
		['V'] = {0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x0aU, 0x04U},
		['W'] = {0x11U, 0x11U, 0x11U, 0x15U, 0x15U, 0x15U, 0x0aU},
		['X'] = {0x11U, 0x11U, 0x0aU, 0x04U, 0x0aU, 0x11U, 0x11U},
		['Y'] = {0x11U, 0x11U, 0x0aU, 0x04U, 0x04U, 0x04U, 0x04U},
		['Z'] = {0x1fU, 0x01U, 0x02U, 0x04U, 0x08U, 0x10U, 0x1fU},
		['0'] = {0x0eU, 0x11U, 0x13U, 0x15U, 0x19U, 0x11U, 0x0eU},
		['1'] = {0x04U, 0x0cU, 0x04U, 0x04U, 0x04U, 0x04U, 0x0eU},
		['2'] = {0x0eU, 0x11U, 0x01U, 0x02U, 0x04U, 0x08U, 0x1fU},
		['3'] = {0x1eU, 0x01U, 0x01U, 0x0eU, 0x01U, 0x01U, 0x1eU},
		['4'] = {0x02U, 0x06U, 0x0aU, 0x12U, 0x1fU, 0x02U, 0x02U},
		['5'] = {0x1fU, 0x10U, 0x10U, 0x1eU, 0x01U, 0x01U, 0x1eU},
		['6'] = {0x0eU, 0x10U, 0x10U, 0x1eU, 0x11U, 0x11U, 0x0eU},
		['7'] = {0x1fU, 0x01U, 0x02U, 0x04U, 0x08U, 0x08U, 0x08U},
		['8'] = {0x0eU, 0x11U, 0x11U, 0x0eU, 0x11U, 0x11U, 0x0eU},
		['9'] = {0x0eU, 0x11U, 0x11U, 0x0fU, 0x01U, 0x01U, 0x0eU},
		['-'] = {0x00U, 0x00U, 0x00U, 0x0eU, 0x00U, 0x00U, 0x00U},
		[':'] = {0x00U, 0x04U, 0x00U, 0x00U, 0x04U, 0x00U, 0x00U},
		['>'] = {0x00U, 0x10U, 0x08U, 0x04U, 0x08U, 0x10U, 0x00U},
	};
	uint8_t uc = (uint8_t) c;

	if (row >= 7U) {
		return 0U;
	}
	if (uc >= 'a' && uc <= 'z') {
		uc = (uint8_t) (uc - 'a' + 'A');
	}
	if (uc >= 128U) {
		return 0U;
	}
	return font5x7[uc][row];
}

static __attribute__((unused)) void menu_draw_text_5x7(volatile uint8_t *fb, uint32_t stride,
			       uint32_t fb_w, uint32_t fb_h, uint32_t x, uint32_t y,
			       uint32_t scale, uint32_t argb, const char *text)
{
	uint32_t i;
	uint32_t row;
	uint32_t col;
	uint32_t sx;
	uint32_t sy;
	uint64_t write_count = 0U;
	char first = '\0';
	uint8_t first_bits = 0U;
	uint8_t first_logged = 0U;

	if (fb == 0 || text == 0 || scale == 0U) {
		return;
	}

	for (i = 0; text[i] != '\0'; i++) {
		uint32_t char_x = x + i * (6U * scale);
		char c = text[i];
		if (c >= 'a' && c <= 'z') {
			c = (char) (c - 'a' + 'A');
		}
		for (row = 0; row < 7U; row++) {
			uint8_t bits = menu_glyph_row_5x7(c, row);
			if (first_logged == 0U && c != ' ' && row == 0U) {
				first = c;
				first_bits = bits;
				first_logged = 1U;
			}
			for (col = 0; col < 5U; col++) {
				if ((bits & (1U << (4U - col))) == 0U) {
					continue;
				}
				for (sy = 0; sy < scale; sy++) {
					uint32_t py = y + row * scale + sy;
					volatile uint32_t *line;
					if (py >= fb_h) {
						continue;
					}
					line = (volatile uint32_t *) (fb + ((uint64_t) py * stride));
					for (sx = 0; sx < scale; sx++) {
						uint32_t px = char_x + col * scale + sx;
						if (px < fb_w) {
							line[px] = argb;
							write_count++;
						}
					}
				}
			}
		}
	}
	uart_puts_all("[mk] menu text dbg first=0x");
	uart_puthex64_all((uint64_t) (uint8_t) first);
	uart_puts_all(" bits0=0x");
	uart_puthex64_all(first_bits);
	uart_puts_all(" writes=0x");
	uart_puthex64_all(write_count);
	uart_puts_all("\r\n");
}

static void delay_ms_calibrated(uint32_t ms);

static __attribute__((unused)) void menu_draw_dbg_step(const char *tag)
{
	uart_puts_all("[mk] menu draw step=");
	uart_puts_all(tag);
	uart_puts_all("\r\n");
	delay_ms_calibrated(3U);
}

static __attribute__((unused)) uint32_t menu_u32_to_dec(char *dst, uint32_t cap, uint32_t value)
{
	char tmp[16];
	uint32_t n = 0U;
	uint32_t i;

	if (dst == 0 || cap < 2U) {
		return 0U;
	}
	if (value == 0U) {
		dst[0] = '0';
		dst[1] = '\0';
		return 1U;
	}

	while (value != 0U && n < (uint32_t) sizeof(tmp)) {
		tmp[n++] = (char) ('0' + (value % 10U));
		value /= 10U;
	}
	if (n + 1U > cap) {
		n = cap - 1U;
	}
	for (i = 0; i < n; i++) {
		dst[i] = tmp[n - 1U - i];
	}
	dst[n] = '\0';
	return n;
}

static uint32_t fastboot_menu_item_count(uint8_t continue_available);
static const char *fastboot_menu_label(uint8_t continue_available, uint32_t menu_index);
static const char *fastboot_menu_row_text(uint8_t continue_available, uint32_t menu_index);
static uint8_t fastboot_menu_select_action(uint8_t continue_available, uint32_t menu_index);

static __attribute__((unused)) void render_fastboot_menu_overlay(const simplefb_info_t *info,
					 uint32_t fallback_width, uint32_t fallback_height,
					 uint32_t fallback_align, uint32_t menu_index, uint32_t secs_left,
					 uint8_t continue_available)
{
	volatile uint8_t *fb = 0;
	volatile uint8_t *fb_page1 = 0;
	uint32_t w = 0U;
	uint32_t h = 0U;
	uint32_t stride = 0U;
	uint32_t x0 = 20U;
	uint32_t y0 = 0U;
	uint32_t box_w = 680U;
	uint32_t box_h = (continue_available != 0U) ? 304U : 250U;
	uint32_t bg = 0xf0181818U;
	uint32_t accent = 0xff2e8b57U;
	uint32_t row_sel = 0xff2e8b57U;
	uint32_t row_unsel = 0xff303030U;
	uint32_t fg = 0xffffffffU;
	uint32_t fg_sel = 0xff081808U;
	uint32_t item_count = fastboot_menu_item_count(continue_available);
	uint32_t row_y[3] = {44U, 98U, 152U};
	uint32_t help_y = (continue_available != 0U) ? 220U : 166U;
	uint64_t ovl_addr = 0U;
	uint64_t ovl0_l0_addr = 0U;
	uint64_t ovl0_l3_addr = 0U;
	uint64_t ovl0_el2_addr = 0U;
	uint64_t ovl0_l0_pitch = 0U;
	uint64_t ovl02_l0_pitch = 0U;
	uint64_t ovl0_src = 0U;
	uint64_t ovl02_src = 0U;
	uint64_t fb_base = 0U;
	uint64_t fb_limit = 0U;
	uint64_t sample_before = 0U;
	uint64_t sample_after = 0U;
	uint64_t page_bytes = 0U;
	uint64_t flush_len;
	uintptr_t flush_start;
	char secs_buf[16];
	char countdown[40];
	const char *help_text = "DOWN NEXT  UP PREV  PWR SELECT";
	uint32_t secs_len;
	uint32_t i;

	if (menu_resolve_fb(info, fallback_width, fallback_height, fallback_align, &fb, &w, &h, &stride) == 0U) {
		uart_puts_all("[mk] menu draw skip: fb unresolved\r\n");
		return;
	}

	uart_puts_all("[mk] menu draw px begin fb=0x");
	uart_puthex64_all((uint64_t) (uintptr_t) fb);
	uart_puts_all(" stride=0x");
	uart_puthex64_all(stride);
	uart_puts_all(" wh=0x");
	uart_puthex64_all(w);
	uart_puts_all("x");
	uart_puthex64_all(h);
	uart_puts_all("\r\n");

	if (w == 0U || h == 0U || stride < 4U || w <= x0 || h <= y0) {
		uart_puts_all("[mk] menu draw skip: bad geometry\r\n");
		return;
	}
	if (box_w > (w - x0)) {
		box_w = w - x0;
	}
	if (box_h > (h - y0)) {
		box_h = h - y0;
	}
	/* Keep the menu docked near the bottom edge. */
	if (h > box_h + 24U) {
		y0 = h - box_h - 24U;
	} else {
		y0 = 0U;
	}
	if (box_w < 80U || box_h < 120U) {
		uart_puts_all("[mk] menu draw skip: box too small\r\n");
		return;
	}
	page_bytes = (uint64_t) stride * h;
	fb_base = (uint64_t) (uintptr_t) fb;
	fb_limit = fb_base + ((info != 0 && info->size != 0U) ? info->size : page_bytes);
	snapshot_display_ovl_state_once();
	ovl0_src = reg_read32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_SRC_CON);
	ovl02_src = reg_read32_local(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_SRC_CON);
	ovl_addr = reg_read32_local(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_ADDR);
	ovl0_l0_addr = reg_read32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L0_ADDR);
	ovl0_l3_addr = reg_read32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_ADDR);
	ovl0_el2_addr = reg_read32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_EL2_ADDR);
	ovl0_l0_pitch = reg_read32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L0_PITCH);
	ovl02_l0_pitch = reg_read32_local(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_PITCH);
	uart_puts_all("[mk] menu draw ovl.l0=0x");
	uart_puthex64_all(ovl_addr);
	uart_puts_all(" fb_base=0x");
	uart_puthex64_all(fb_base);
	uart_puts_all(" fb_limit=0x");
	uart_puthex64_all(fb_limit);
	uart_puts_all(" page_bytes=0x");
	uart_puthex64_all(page_bytes);
	uart_puts_all("\r\n");
	uart_puts_all("[mk] menu draw ovl src0=0x");
	uart_puthex64_all(ovl0_src);
	uart_puts_all(" src2l=0x");
	uart_puthex64_all(ovl02_src);
	uart_puts_all(" l0=0x");
	uart_puthex64_all(ovl0_l0_addr);
	uart_puts_all(" l3=0x");
	uart_puthex64_all(ovl0_l3_addr);
	uart_puts_all(" el2=0x");
	uart_puthex64_all(ovl0_el2_addr);
	uart_puts_all(" p0=0x");
	uart_puthex64_all(ovl0_l0_pitch);
	uart_puts_all(" p2l=0x");
	uart_puthex64_all(ovl02_l0_pitch);
	uart_puts_all("\r\n");
	delay_ms_calibrated(3U);
	if ((ovl0_src & 0x0eU) != 0U) {
		reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_SRC_CON, 0x1U);
		reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_CON, 0U);
		reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_SRC_SIZE, 0U);
		reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_OFFSET, 0U);
		reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_ADDR, 0U);
		reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_PITCH, 0U);
		reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_CLEAR, 1U);
		reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_TRIG, 1U);
		uart_puts_all("[mk] menu draw: forced ovl0 src=l0-only\r\n");
		uart_puts_all("[mk] menu draw: ovl0 src now=0x");
		uart_puthex64_all(reg_read32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_SRC_CON));
		uart_puts_all("\r\n");
		delay_ms_calibrated(3U);
	}
	if ((ovl0_l0_pitch & 0xffffU) >= (uint64_t) (w * 4U) &&
	    (ovl0_l0_pitch & 0xffffU) <= (uint64_t) (w * 6U)) {
		stride = (uint32_t) (ovl0_l0_pitch & 0xffffU);
		uart_puts_all("[mk] menu draw: using ovl0 pitch stride=0x");
		uart_puthex64_all(stride);
		uart_puts_all("\r\n");
		delay_ms_calibrated(3U);
	} else if ((ovl02_l0_pitch & 0xffffU) >= (uint64_t) (w * 4U) &&
		   (ovl02_l0_pitch & 0xffffU) <= (uint64_t) (w * 6U)) {
		stride = (uint32_t) (ovl02_l0_pitch & 0xffffU);
		uart_puts_all("[mk] menu draw: using ovl0_2l pitch stride=0x");
		uart_puthex64_all(stride);
		uart_puts_all("\r\n");
		delay_ms_calibrated(3U);
	}
	if (ovl_addr >= fb_base && (ovl_addr + (uint64_t) stride) <= fb_limit) {
		fb = (volatile uint8_t *) (uintptr_t) ovl_addr;
		uart_puts_all("[mk] menu draw using active l0 fb=0x");
		uart_puthex64_all((uint64_t) (uintptr_t) fb);
		uart_puts_all("\r\n");
		delay_ms_calibrated(3U);
	}
	page_bytes = (uint64_t) stride * h;
	flush_start = (uintptr_t) (fb + (uint64_t) y0 * stride);
	flush_len = (uint64_t) box_h * stride;

	menu_draw_dbg_step("p0-fill-bg");
	menu_fill_rect32(fb, stride, w, h, x0, y0, box_w, box_h, bg);
	menu_draw_dbg_step("p0-fill-header");
	menu_fill_rect32(fb, stride, w, h, x0 + 4U, y0 + 4U, box_w - 8U, 26U, accent);
	for (i = 0U; i < item_count; i++) {
		uart_puts_all("[mk] menu draw row fill idx=0x");
		uart_puthex64_all(i);
		uart_puts_all("\r\n");
		menu_fill_rect32(fb, stride, w, h, x0 + 12U, y0 + row_y[i], box_w - 24U, 48U,
				(menu_index == i) ? row_sel : row_unsel);
	}
	menu_draw_dbg_step("p0-text-title");
	sample_before = ((volatile uint32_t *) (fb + (uint64_t) (y0 + 10U) * stride))[x0 + 12U];
	uart_puts_all("[mk] menu text sample before=0x");
	uart_puthex64_all(sample_before);
	uart_puts_all("\r\n");
	delay_ms_calibrated(1U);
	menu_draw_text_5x7(fb, stride, w, h, x0 + 12U, y0 + 10U, 2U, fg, "FASTBOOT MENU");
	sample_after = ((volatile uint32_t *) (fb + (uint64_t) (y0 + 10U) * stride))[x0 + 12U];
	uart_puts_all("[mk] menu text sample after=0x");
	uart_puthex64_all(sample_after);
	uart_puts_all("\r\n");
	delay_ms_calibrated(1U);
	clean_dcache_range(flush_start, flush_len);
	menu_draw_dbg_step("p0-text-title-flush");
	for (i = 0U; i < item_count; i++) {
		uart_puts_all("[mk] menu draw row text idx=0x");
		uart_puthex64_all(i);
		uart_puts_all("\r\n");
		menu_draw_text_5x7(fb, stride, w, h, x0 + 24U, y0 + row_y[i] + 14U, 2U,
				  (menu_index == i) ? fg_sel : fg,
				  fastboot_menu_row_text(continue_available, i));
		clean_dcache_range(flush_start, flush_len);
	}
	menu_draw_dbg_step("p0-text-help");
	menu_draw_text_5x7(fb, stride, w, h, x0 + 12U, y0 + help_y, 2U, fg, help_text);
	clean_dcache_range(flush_start, flush_len);
	menu_draw_dbg_step("p0-text-help-flush");

	/* Temporarily skip countdown drawing while isolating menu render crash. */
	secs_len = menu_u32_to_dec(secs_buf, sizeof(secs_buf), secs_left);
	for (i = 0U; i < sizeof(countdown); i++) {
		countdown[i] = '\0';
	}
	(void) secs_len;
	(void) secs_buf;
	(void) countdown;
	menu_draw_dbg_step("p0-countdown-skip");

	menu_draw_dbg_step("p0-flush");
	clean_dcache_range(flush_start, flush_len);

	uart_puts_all("[mk] menu draw mirror-check size=0x");
	uart_puthex64_all((info != 0) ? info->size : 0U);
	uart_puts_all(" need=0x");
	uart_puthex64_all(page_bytes * 2U);
	uart_puts_all("\r\n");
	delay_ms_calibrated(3U);
	if (info != 0 && info->size >= (page_bytes * 2U)) {
		fb_page1 = fb + page_bytes;
		menu_draw_dbg_step("p1-fill-bg");
		menu_fill_rect32(fb_page1, stride, w, h, x0, y0, box_w, box_h, bg);
		menu_draw_dbg_step("p1-fill-header");
		menu_fill_rect32(fb_page1, stride, w, h, x0 + 4U, y0 + 4U, box_w - 8U, 26U, accent);
		for (i = 0U; i < item_count; i++) {
			menu_fill_rect32(fb_page1, stride, w, h, x0 + 12U, y0 + row_y[i], box_w - 24U, 48U,
					(menu_index == i) ? row_sel : row_unsel);
		}
		menu_draw_dbg_step("p1-text-title");
		menu_draw_text_5x7(fb_page1, stride, w, h, x0 + 12U, y0 + 10U, 2U, fg, "FASTBOOT MENU");
		for (i = 0U; i < item_count; i++) {
			menu_draw_text_5x7(fb_page1, stride, w, h, x0 + 24U, y0 + row_y[i] + 14U, 2U,
					  (menu_index == i) ? fg_sel : fg,
					  fastboot_menu_row_text(continue_available, i));
		}
		menu_draw_dbg_step("p1-text-help");
		menu_draw_text_5x7(fb_page1, stride, w, h, x0 + 12U, y0 + help_y, 2U, fg, help_text);
		menu_draw_dbg_step("p1-countdown-skip");
		flush_start = (uintptr_t) (fb_page1 + (uint64_t) y0 * stride);
		menu_draw_dbg_step("p1-flush");
		clean_dcache_range(flush_start, flush_len);
		uart_puts_all("[mk] menu draw mirrored page1\r\n");
		delay_ms_calibrated(3U);
	}
	uart_puts_all("[mk] menu draw px end\r\n");
	delay_ms_calibrated(3U);
}

static uint32_t bpp_from_format(const char *fmt)
{
	if (fmt == 0) {
		return 4;
	}
	if (str_eq(fmt, "r5g6b5")) {
		return 2;
	}
	return 4;
}

static uint32_t align_up_u32(uint32_t value, uint32_t align)
{
	if (align == 0U) {
		return value;
	}
	return (value + align - 1U) & ~(align - 1U);
}

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

static void clean_dcache_range(uintptr_t start, uint64_t len)
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

static uint32_t mmio_read32(uint64_t addr);
static void mmio_write32(uint64_t addr, uint32_t value);
void pet_wdt(void);
void uart_puts_all(const char *s);
void uart_puthex64_all(uint64_t v);

static uint64_t read_cntfrq_el0(void)
{
	uint64_t v;

	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
	return v;
}

static uint64_t read_cntpct_el0(void)
{
	uint64_t v;

	__asm__ volatile("mrs %0, cntpct_el0" : "=r"(v));
	return v;
}

static void delay_ms_calibrated(uint32_t ms)
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

static uint8_t glyph_row_for_char(char c, uint32_t row)
{
	static const uint8_t glyph_M[7] = {0x11U, 0x1bU, 0x15U, 0x15U, 0x11U, 0x11U, 0x11U};
	static const uint8_t glyph_i[7] = {0x04U, 0x00U, 0x0cU, 0x04U, 0x04U, 0x04U, 0x0eU};
	static const uint8_t glyph_n[7] = {0x00U, 0x00U, 0x1aU, 0x15U, 0x11U, 0x11U, 0x11U};
	static const uint8_t glyph_K[7] = {0x11U, 0x12U, 0x14U, 0x18U, 0x14U, 0x12U, 0x11U};
	static const uint8_t glyph_e[7] = {0x00U, 0x00U, 0x0eU, 0x11U, 0x1fU, 0x10U, 0x0fU};
	static const uint8_t glyph_r[7] = {0x00U, 0x00U, 0x16U, 0x19U, 0x10U, 0x10U, 0x10U};
	static const uint8_t glyph_l[7] = {0x0cU, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U, 0x0eU};
	const uint8_t *glyph;

	if (row >= 7U) {
		return 0U;
	}

	switch (c) {
	case 'M':
		glyph = glyph_M;
		break;
	case 'i':
		glyph = glyph_i;
		break;
	case 'n':
		glyph = glyph_n;
		break;
	case 'K':
		glyph = glyph_K;
		break;
	case 'e':
		glyph = glyph_e;
		break;
	case 'r':
		glyph = glyph_r;
		break;
	case 'l':
		glyph = glyph_l;
		break;
	default:
		return 0U;
	}

	return glyph[row];
}

static void draw_label_32(volatile uint8_t *fb, uint32_t stride, uint32_t w, uint32_t h)
{
	static const char text[] = "MinKernel";
	const uint32_t glyph_w = 5U;
	const uint32_t glyph_h = 7U;
	const uint32_t scale = 4U;
	const uint32_t spacing = 4U;
	const uint32_t text_w =
		((sizeof(text) - 1U) * glyph_w * scale) +
		((sizeof(text) - 2U) * spacing);
	uint32_t base_x = 0U;
	uint32_t base_y = 56U;
	uint32_t i;
	uint32_t row;
	uint32_t col;
	uint32_t sy;
	uint32_t sx;

	if (fb == 0 || stride == 0U || w == 0U || h == 0U) {
		return;
	}

	if (w > text_w) {
		base_x = (w - text_w) / 2U;
	}
	if (base_y + (glyph_h * scale) >= h) {
		return;
	}

	for (i = 0; i < (sizeof(text) - 1U); i++) {
		uint32_t char_x = base_x + (i * ((glyph_w * scale) + spacing));
		for (row = 0; row < glyph_h; row++) {
			uint8_t bits = glyph_row_for_char(text[i], row);
			for (col = 0; col < glyph_w; col++) {
				if ((bits & (1U << (glyph_w - 1U - col))) == 0U) {
					continue;
				}
				for (sy = 0; sy < scale; sy++) {
					volatile uint32_t *line32 =
						(volatile uint32_t *) (fb + ((uint64_t) (base_y + (row * scale) + sy) * stride));
					for (sx = 0; sx < scale; sx++) {
						line32[char_x + (col * scale) + sx] = 0xffffffffU;
					}
				}
			}
		}
	}
}

static void render_logo_page_rgba(volatile uint8_t *fb, uint32_t stride, uint32_t w, uint32_t h)
{
	uint32_t logo_w = MK_STAGE0_PEACOCK_LOGO_WIDTH;
	uint32_t logo_h = MK_STAGE0_PEACOCK_LOGO_HEIGHT;
	uint32_t logo_x = 0U;
	uint32_t logo_y = 0U;
	uint32_t stride_px = stride / 4U;
	uint32_t x;
	uint32_t y;

	if (fb == 0 || stride == 0U || w == 0U || h == 0U) {
		return;
	}

	if (logo_w > w) {
		logo_w = w;
	}
	if (logo_h > h) {
		logo_h = h;
	}
	if (w > logo_w) {
		logo_x = (w - logo_w) / 2U;
	}
	if (h > logo_h) {
		logo_y = (h - logo_h) / 2U;
	}

	for (y = 0; y < h; y++) {
		volatile uint32_t *line32 =
			(volatile uint32_t *) (fb + ((uint64_t) y * stride));
		for (x = 0; x < stride_px; x++) {
			line32[x] = 0xff000000U;
		}
	}

	draw_label_32(fb, stride, w, h);

	for (y = 0; y < logo_h; y++) {
		volatile uint32_t *line32 =
			(volatile uint32_t *) (fb + ((uint64_t) (logo_y + y) * stride));
		const uint8_t *src =
			&g_peacock_logo_index[(uint64_t) y * (uint64_t) MK_STAGE0_PEACOCK_LOGO_WIDTH];
		for (x = 0; x < logo_w; x++) {
			line32[logo_x + x] = g_peacock_logo_palette[src[x]];
		}
	}
}

static __attribute__((unused)) void try_direct_link_flip_and_disable_strip(const simplefb_info_t *info,
									   uint32_t fallback_width,
									   uint32_t fallback_height,
									   uint32_t fallback_align)
{
	uint32_t bpp;
	uint32_t w;
	uint32_t h;
	uint32_t stride;
	uint64_t page0_addr;
	uint32_t src_con;
	uint32_t src_con_2l;

	if (info == 0 || info->addr == 0U) {
		return;
	}

	bpp = bpp_from_format(info->format);
	w = info->width;
	h = info->height;
	stride = info->stride;
	if ((w == 0U || h == 0U) && fallback_width != 0U && fallback_height != 0U) {
		w = fallback_width;
		h = fallback_height;
		if (stride == 0U) {
			stride = align_up_u32(w, fallback_align) * 4U;
		}
	}
	if (bpp != 4U || w == 0U || h == 0U || stride == 0U) {
		return;
	}

	page0_addr = info->addr;
	snapshot_display_ovl_state_once();

	src_con = reg_read32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_SRC_CON);
	src_con &= ~0x8U;
	reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_SRC_CON, src_con);
	reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_CON, 0U);
	reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_SRC_SIZE, 0U);
	reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_OFFSET, 0U);
	reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_ADDR, 0U);
	reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_PITCH, 0U);
	reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_L3_CLEAR, 1U);

	src_con_2l = reg_read32_local(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_SRC_CON);
	src_con_2l |= 0x1U;
	reg_write32_local(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_SRC_CON, src_con_2l);
	reg_write32_local(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_ADDR, (uint32_t) page0_addr);
	reg_write32_local(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_OFFSET, 0U);
	reg_write32_local(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_SRC_SIZE,
			 (h << 16) | (w & 0xfffU));
	reg_write32_local(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_L0_CLEAR, 1U);
	reg_write32_local(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_TRIG, 1U);
	reg_write32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_TRIG, 1U);

	uart_puts_all("[mk] relatch page0=0x");
	uart_puthex64_all(page0_addr);
	uart_puts_all(" ovl0.src=0x");
	uart_puthex64_all(reg_read32_local(MTK_DISP_OVL0_BASE + MTK_DISP_OVL_SRC_CON));
	uart_puts_all(" ovl0_2l.src=0x");
	uart_puthex64_all(reg_read32_local(MTK_DISP_OVL0_2L_BASE + MTK_DISP_OVL_SRC_CON));
	uart_puts_all("\r\n");
}

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

static __attribute__((unused)) int fdt_root_has_compatible(const void *fdt, const char *needle)
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

	if (base == 0 || needle == 0 || be32_read(base) != FDT_MAGIC) {
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

		if (token == FDT_BEGIN_NODE) {
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
		if (token == FDT_END_NODE) {
			if (depth >= 0) {
				depth--;
			}
			continue;
		}
		if (token == FDT_NOP) {
			continue;
		}
		if (token == FDT_END) {
			break;
		}
		if (token != FDT_PROP) {
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

static __attribute__((unused)) int fdt_find_compatible_reg(const void *fdt, const char *needle,
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
	uint8_t match_stack[MAX_FDT_DEPTH] = {0};
	int depth = -1;

	if (base == 0 || needle == 0 || out_base == 0 || be32_read(base) != FDT_MAGIC) {
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

		if (token == FDT_BEGIN_NODE) {
			depth++;
			if (depth >= MAX_FDT_DEPTH) {
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
		if (token == FDT_END_NODE) {
			if (depth >= 0) {
				depth--;
			}
			continue;
		}
		if (token == FDT_NOP) {
			continue;
		}
		if (token == FDT_END) {
			break;
		}
		if (token != FDT_PROP) {
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

static uint32_t mmio_read32(uint64_t addr)
{
	volatile uint32_t *p = (volatile uint32_t *) (uintptr_t) addr;
	return *p;
}

static void mmio_write32(uint64_t addr, uint32_t value)
{
	volatile uint32_t *p = (volatile uint32_t *) (uintptr_t) addr;
	*p = value;
	__asm__ volatile("dsb sy");
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

static void setup_wdt(const void *fdt)
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

	/* Disable the watchdog entirely before kernel handoff.
	 * The kernel's mtk_wdt driver will re-enable it when it probes.
	 * This prevents WDT reboot if slow driver probes (e.g. I2C timeouts)
	 * delay the kernel's WDT takeover.
	 */
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

void mk_stage0_msdc_restore_for_linux(void)
{
	uint64_t base = MTK_MSDC0_BASE;

	uart_puts_all("[mk] msdc restore begin\r\n");
	msdc0_reset_host();
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

static uint32_t le32_read(const uint8_t *p)
{
	return (uint32_t) p[0] |
	       ((uint32_t) p[1] << 8) |
	       ((uint32_t) p[2] << 16) |
	       ((uint32_t) p[3] << 24);
}

static uint64_t le64_read(const uint8_t *p)
{
	return (uint64_t) le32_read(p) | ((uint64_t) le32_read(p + 4) << 32);
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

static uint32_t fastboot_menu_item_count(uint8_t continue_available)
{
	return (continue_available != 0U) ? 3U : 2U;
}

static const char *fastboot_menu_label(uint8_t continue_available, uint32_t menu_index)
{
	if (continue_available != 0U) {
		if (menu_index == 0U) {
			return "continue-boot";
		}
		if (menu_index == 1U) {
			return "stay-fastboot";
		}
		return "reboot-recovery";
	}

	if (menu_index == 0U) {
		return "stay-fastboot";
	}
	return "reboot-recovery";
}

static const char *fastboot_menu_row_text(uint8_t continue_available, uint32_t menu_index)
{
	if (continue_available != 0U) {
		if (menu_index == 0U) {
			return "CONTINUE BOOT";
		}
		if (menu_index == 1U) {
			return "STAY FASTBOOT";
		}
		return "REBOOT RECOVERY";
	}

	if (menu_index == 0U) {
		return "STAY FASTBOOT";
	}
	return "REBOOT RECOVERY";
}

static uint8_t fastboot_menu_select_action(uint8_t continue_available, uint32_t menu_index)
{
	if (continue_available != 0U) {
		if (menu_index == 0U) {
			return MK_FASTBOOT_ACTION_CONTINUE;
		}
		if (menu_index == 2U) {
			return MK_FASTBOOT_ACTION_REBOOT_RECOVERY;
		}
		return MK_FASTBOOT_ACTION_NONE;
	}

	if (menu_index == 1U) {
		return MK_FASTBOOT_ACTION_REBOOT_RECOVERY;
	}
	return MK_FASTBOOT_ACTION_NONE;
}

static uint8_t handle_fastboot_menu_input(uint32_t *menu_index, uint64_t *last_event_ticks,
					  uint64_t now_ticks, uint64_t freq,
					  uint8_t *menu_dirty, uint8_t continue_available)
{
	uint8_t up_edge = 0U;
	uint8_t down_edge = 0U;
	uint8_t select_edge = 0U;
	static uint8_t up_latched = 0U;
	static uint8_t down_latched = 0U;
	static uint8_t select_latched = 0U;
	uint8_t up_pressed;
	uint8_t down_pressed;
	uint8_t select_pressed;
	uint8_t action;
	uint64_t debounce_ticks = (freq != 0U) ? (freq / 20ULL) : 0ULL;
	uint32_t item_count = fastboot_menu_item_count(continue_available);

	if (menu_dirty != 0) {
		*menu_dirty = 0U;
	}
	if (menu_index == 0 || last_event_ticks == 0 || g_menu_buttons.has_any == 0U || item_count == 0U) {
		return 0U;
	}

	menu_buttons_poll_edges(&up_edge, &down_edge, &select_edge);
	(void) up_edge;
	(void) down_edge;
	(void) select_edge;

	up_pressed = g_menu_buttons.stable_up_pressed;
	down_pressed = g_menu_buttons.stable_down_pressed;
	select_pressed = g_menu_buttons.stable_power_pressed;

	if (up_pressed == 0U) {
		up_latched = 0U;
	}
	if (down_pressed == 0U) {
		down_latched = 0U;
	}
	if (select_pressed == 0U) {
		select_latched = 0U;
	}

	if ((up_pressed == 0U || up_latched != 0U) &&
	    (down_pressed == 0U || down_latched != 0U) &&
	    (select_pressed == 0U || select_latched != 0U)) {
		return 0U;
	}

	if (debounce_ticks != 0U && (now_ticks - *last_event_ticks) < debounce_ticks) {
		return 0U;
	}
	*last_event_ticks = now_ticks;

	if (up_pressed != 0U && up_latched == 0U) {
		up_latched = 1U;
		*menu_index = (*menu_index == 0U) ? (item_count - 1U) : (*menu_index - 1U);
		if (menu_dirty != 0) {
			*menu_dirty = 1U;
		}
		if (g_menu_buttons.vol_up_gpio == MK_STAGE0_GPIO_NONE &&
		    g_menu_buttons.vol_up_hwcode == 0xffffffffU) {
			uart_puts_all("[mk] menu input: homekey-as-up\r\n");
		} else {
			uart_puts_all("[mk] menu input: volume-up\r\n");
		}
		uart_puts_all("[mk] menu select=");
		uart_puts_all(fastboot_menu_label(continue_available, *menu_index));
		uart_puts_all("\r\n");
	}

	if (down_pressed != 0U && down_latched == 0U) {
		down_latched = 1U;
		*menu_index = (*menu_index + 1U) % item_count;
		if (menu_dirty != 0) {
			*menu_dirty = 1U;
		}
		uart_puts_all("[mk] menu select=");
		uart_puts_all(fastboot_menu_label(continue_available, *menu_index));
		uart_puts_all("\r\n");
	}

	if (select_pressed != 0U && select_latched == 0U) {
		select_latched = 1U;
		if (menu_dirty != 0) {
			*menu_dirty = 1U;
		}
		uart_puts_all("[mk] menu input: power\r\n");
		action = fastboot_menu_select_action(continue_available, *menu_index);
		if (action == MK_FASTBOOT_ACTION_REBOOT_RECOVERY) {
			uart_puts_all("[mk] menu action: reboot recovery\r\n");
			return action;
		}
		if (action == MK_FASTBOOT_ACTION_CONTINUE) {
			uart_puts_all("[mk] menu action: continue boot\r\n");
			return action;
		}
		uart_puts_all("[mk] menu action: stay fastboot\r\n");
	}

	return 0U;
}

static uint32_t fastboot_timeout_secs_left(uint64_t start_ticks, uint64_t now_ticks,
					   uint64_t timeout_ticks, uint64_t freq)
{
	uint64_t elapsed;
	uint64_t remaining;

	if (timeout_ticks == 0U || freq == 0U) {
		return 0U;
	}
	if (now_ticks <= start_ticks) {
		return (uint32_t) ((timeout_ticks + freq - 1ULL) / freq);
	}
	elapsed = now_ticks - start_ticks;
	if (elapsed >= timeout_ticks) {
		return 0U;
	}
	remaining = timeout_ticks - elapsed;
	return (uint32_t) ((remaining + freq - 1ULL) / freq);
}

static __attribute__((unused)) uint8_t enter_fastboot_fallback(const simplefb_info_t *info,
							       uint32_t fallback_width,
							       uint32_t fallback_height,
							       uint32_t fallback_align,
							       uint8_t continue_available)
{
	uint32_t heartbeat = 0U;
	uint32_t menu_index = 0U;
	uint32_t secs_left = 0U;
	uint32_t spin;
	uint64_t now_ticks;
	uint64_t menu_last_event_ticks = 0U;
	uint64_t next_ui_ticks = 0U;
	uint64_t ui_interval_ticks;
	uint64_t start_ticks;
	uint64_t freq;
	uint64_t timeout_ticks;
	uint8_t menu_dirty = 0U;
	uint8_t draw_pending = 0U;
	uint8_t fb_action = MK_FASTBOOT_ACTION_NONE;

	uart_puts_all("[mk] fastboot fallback: holding\r\n");
	if (g_menu_buttons.has_any != 0U) {
		uart_puts_all("[mk] menu controls: vol-down=next vol-up=prev power=select\r\n");
		uart_puts_all("[mk] menu select=");
		uart_puts_all(fastboot_menu_label(continue_available, menu_index));
		uart_puts_all("\r\n");
		menu_dirty = 1U;
		draw_pending = 1U;
	}
	freq = read_cntfrq_el0();
	start_ticks = read_cntpct_el0();
	timeout_ticks = 0ULL;
	uart_puts_all("[mk] fastboot fallback: auto-reboot disabled\r\n");
	ui_interval_ticks = (freq != 0U) ? (freq / 100ULL) : 1ULL;
	if (ui_interval_ticks == 0U) {
		ui_interval_ticks = 1ULL;
	}
	next_ui_ticks = start_ticks;
	if (MK_DEVICE_HAS_FASTBOOT_USB != 0) {
		if (mk_stage0_mtk_usb_fastboot_init() == 0) {
			uart_puts_all("[mk] fastboot fallback: usb ep0 online\r\n");
				for (;;) {
					now_ticks = read_cntpct_el0();
					mk_stage0_mtk_usb_fastboot_poll();
					fb_action = mk_stage0_mtk_usb_fastboot_take_action();
					if (fb_action != MK_FASTBOOT_ACTION_NONE) {
						uart_puts_all("[mk] fastboot action: ");
						if (fb_action == MK_FASTBOOT_ACTION_REBOOT_RECOVERY) {
							uart_puts_all("reboot-recovery\r\n");
						} else if (fb_action == MK_FASTBOOT_ACTION_REBOOT_BOOTLOADER) {
							uart_puts_all("reboot-bootloader\r\n");
						} else if (fb_action == MK_FASTBOOT_ACTION_BOOT_STAGED_KERNEL) {
							uart_puts_all("boot-staged-kernel\r\n");
							if (continue_available != 0U) {
								return MK_FASTBOOT_ACTION_BOOT_STAGED_KERNEL;
							}
							uart_puts_all("[mk] boot-staged-kernel ignored: peacock boot unavailable\r\n");
							continue;
						} else if (fb_action == MK_FASTBOOT_ACTION_CONTINUE) {
							uart_puts_all("continue\r\n");
							if (continue_available != 0U) {
								return MK_FASTBOOT_ACTION_CONTINUE;
							}
							uart_puts_all("[mk] continue ignored: peacock boot unavailable\r\n");
							continue;
						} else {
							uart_puts_all("reboot\r\n");
						}
						delay_ms_calibrated(30U);
						if (fb_action == MK_FASTBOOT_ACTION_REBOOT_RECOVERY) {
							arm_recovery_wdt();
						}
						arm_normal_wdt();
					}
					if (timeout_ticks != 0U && (now_ticks - start_ticks) >= timeout_ticks) {
						uart_puts_all("[mk] fastboot fallback: timeout, rebooting recovery\r\n");
						arm_recovery_wdt();
					delay_ms_calibrated(50U);
					trigger_recovery_wdt_reset();
				}
				if (mk_stage0_mtk_usb_fastboot_downloading() != 0U) {
					for (spin = 0; spin < 64U; spin++) {
						__asm__ volatile("");
					}
					pet_wdt();
					continue;
				}
				if (now_ticks >= next_ui_ticks) {
					fb_action = handle_fastboot_menu_input(&menu_index, &menu_last_event_ticks,
									      now_ticks, freq, &menu_dirty,
									      continue_available);
					if (fb_action == MK_FASTBOOT_ACTION_REBOOT_RECOVERY) {
						arm_recovery_wdt();
						delay_ms_calibrated(50U);
						trigger_recovery_wdt_reset();
					}
					if (fb_action == MK_FASTBOOT_ACTION_CONTINUE) {
						return MK_FASTBOOT_ACTION_CONTINUE;
					}
					if (menu_dirty != 0U && g_menu_buttons.has_any != 0U) {
						draw_pending = 1U;
					}
					if (draw_pending != 0U) {
						secs_left = fastboot_timeout_secs_left(start_ticks, now_ticks,
									      timeout_ticks, freq);
						uart_puts_all("[mk] menu draw call\r\n");
						render_fastboot_menu_overlay(info, fallback_width, fallback_height,
									 fallback_align, menu_index, secs_left,
									 continue_available);
						uart_puts_all("[mk] menu draw done\r\n");
						draw_pending = 0U;
						menu_dirty = 0U;
					}
					next_ui_ticks = now_ticks + ui_interval_ticks;
				}
				for (spin = 0; spin < 2048U; spin++) {
					__asm__ volatile("");
				}
				pet_wdt();
			}
		}
		uart_puts_all("[mk] fastboot fallback: usb init failed\r\n");
	}

	for (;;) {
		now_ticks = read_cntpct_el0();
		fb_action = mk_stage0_mtk_usb_fastboot_take_action();
		if (fb_action != MK_FASTBOOT_ACTION_NONE) {
			uart_puts_all("[mk] fastboot action: ");
			if (fb_action == MK_FASTBOOT_ACTION_REBOOT_RECOVERY) {
				uart_puts_all("reboot-recovery\r\n");
			} else if (fb_action == MK_FASTBOOT_ACTION_BOOT_STAGED_KERNEL) {
				uart_puts_all("boot-staged-kernel\r\n");
				if (continue_available != 0U) {
					return MK_FASTBOOT_ACTION_BOOT_STAGED_KERNEL;
				}
				uart_puts_all("[mk] boot-staged-kernel ignored: peacock boot unavailable\r\n");
				continue;
			} else if (fb_action == MK_FASTBOOT_ACTION_CONTINUE) {
				uart_puts_all("continue\r\n");
				if (continue_available != 0U) {
					return MK_FASTBOOT_ACTION_CONTINUE;
				}
				uart_puts_all("[mk] continue ignored: peacock boot unavailable\r\n");
				continue;
			} else {
				uart_puts_all("reboot\r\n");
			}
			delay_ms_calibrated(30U);
			if (fb_action == MK_FASTBOOT_ACTION_REBOOT_RECOVERY) {
				arm_recovery_wdt();
			}
			arm_normal_wdt();
		}
		if (timeout_ticks != 0U && (now_ticks - start_ticks) >= timeout_ticks) {
			uart_puts_all("[mk] fastboot fallback: timeout, rebooting recovery\r\n");
			arm_recovery_wdt();
			delay_ms_calibrated(50U);
			trigger_recovery_wdt_reset();
		}
		if (now_ticks >= next_ui_ticks) {
			fb_action = handle_fastboot_menu_input(&menu_index, &menu_last_event_ticks,
						       now_ticks, freq, &menu_dirty,
						       continue_available);
			if (fb_action == MK_FASTBOOT_ACTION_REBOOT_RECOVERY) {
				arm_recovery_wdt();
				delay_ms_calibrated(50U);
				trigger_recovery_wdt_reset();
			}
			if (fb_action == MK_FASTBOOT_ACTION_CONTINUE) {
				return MK_FASTBOOT_ACTION_CONTINUE;
			}
			if (menu_dirty != 0U && g_menu_buttons.has_any != 0U) {
				draw_pending = 1U;
			}
			if (draw_pending != 0U) {
				secs_left = fastboot_timeout_secs_left(start_ticks, now_ticks, timeout_ticks, freq);
				uart_puts_all("[mk] menu draw call\r\n");
				render_fastboot_menu_overlay(info, fallback_width, fallback_height,
							 fallback_align, menu_index, secs_left,
							 continue_available);
				uart_puts_all("[mk] menu draw done\r\n");
				draw_pending = 0U;
				menu_dirty = 0U;
			}
			next_ui_ticks = now_ticks + ui_interval_ticks;
		}
		for (spin = 0; spin < 400000U; spin++) {
			if ((spin & 0x3fffU) == 0U) {
				pet_wdt();
			}
			__asm__ volatile("");
		}
		if ((heartbeat++ & 0x0fU) == 0U) {
			uart_puts_all("[mk] fastboot fallback: alive\r\n");
		}
	}
}

static int write_para_bcb(uint8_t set_recovery)
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

static void trigger_recovery_wdt_reset(void)
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

static void arm_recovery_wdt(void)
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
	(void) write_para_bcb(1U);

	/*
	 * Generic MTK reboot-mode uses the low nibble in NONRST2. Keep that set
	 * as a fallback, then follow the vendor LK reset sequence.
	 */
	reg_norst2 = mmio_read32(g_wdt_base + MTK_WDT_NONRST2);
	reg_norst2 &= ~MTK_WDT_NONRST2_BOOTMODE_MASK;
	reg_norst2 |= MTK_BOOTMODE_RECOVERY;
	mmio_write32(g_wdt_base + MTK_WDT_NONRST2, reg_norst2);

	/*
	 * Match vendor MTK LK (`FUN_000574bc`) rather than the generic Linux
	 * reboot-mode driver. That sequence touches TOPRGU + 0x10/0x24/0x30/0x34
	 * before asserting SWRST.
	 */
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

	mmio_write32(g_wdt_base + MTK_WDT_LENGTH, MTK_WDT_LENGTH_VALUE(10U) | MTK_WDT_LENGTH_KEY);

	reg_req_mode = mmio_read32(g_wdt_base + MTK_WDT_REQ_MODE);
	reg_req_mode |= MTK_WDT_REQ_MODE_RECOVERY_SEQ;
	mmio_write32(g_wdt_base + MTK_WDT_REQ_MODE, reg_req_mode);

	reg_req_irq = mmio_read32(g_wdt_base + MTK_WDT_REQ_IRQ_EN);
	reg_req_irq = (reg_req_irq & MTK_WDT_REQ_IRQ_EN_RECOVERY_MASK) | MTK_WDT_REQ_IRQ_EN_RECOVERY_SEQ;
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

static void arm_normal_wdt(void)
{
	uint32_t reg_norst2;

	if (g_wdt_base == 0) {
		uart_puts_all("[mk] reboot unavailable (no WDT)\r\n");
		for (;;) {
			__asm__ volatile("wfe");
		}
	}

	uart_puts_all("[mk] reboot -> normal via TOPRGU\r\n");
	(void) write_para_bcb(0U);

	/*
	 * Clear any persisted bootmode/stage bits so the next boot follows
	 * the normal path.
	 */
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

static void parse_simplefb_from_fdt(const void *fdt, simplefb_info_t *info)
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
	uint8_t simple_stack[MAX_FDT_DEPTH] = {0};
	int depth = -1;

	if (base == 0 || info == 0) {
		return;
	}
	if (be32_read(base) != FDT_MAGIC) {
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

		if (token == FDT_BEGIN_NODE) {
			depth++;
			if (depth >= MAX_FDT_DEPTH) {
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

		if (token == FDT_END_NODE) {
			if (depth >= 0) {
				depth--;
			}
			continue;
		}

		if (token == FDT_NOP) {
			continue;
		}

		if (token == FDT_END) {
			break;
		}

		if (token == FDT_PROP) {
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

static void parse_videolfb_from_chosen(const void *fdt, simplefb_info_t *info)
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

	(void) fdt_find_chosen_u64(fdt, "atag,videolfb-fb_base_h", &fb_hi);
	(void) fdt_find_chosen_u64(fdt, "atag,videolfb-fb_base_l", &fb_lo);
	(void) fdt_find_chosen_u64(fdt, "atag,videolfb-vramSize", &fb_size);

	if (fb_lo == 0 || fb_size == 0) {
		return;
	}

	info->addr = (fb_hi << 32) | (fb_lo & 0xffffffffULL);
	info->size = fb_size;
}

static void draw_pattern(const simplefb_info_t *info,
			 uint32_t fallback_width,
			 uint32_t fallback_height,
			 uint32_t fallback_align)
{
	volatile uint8_t *fb;
	volatile uint32_t *fb32;
	uint64_t frame_bytes;
	uint64_t pages_to_paint;
	uint64_t page;
	uint32_t bpp;
	uint32_t x;
	uint32_t y;
	uint32_t w;
	uint32_t h;
	uint32_t stride;

	if (info == 0 || info->addr == 0) {
		return;
	}

	fb = (volatile uint8_t *) (uintptr_t) info->addr;
	fb32 = (volatile uint32_t *) (uintptr_t) info->addr;
	bpp = bpp_from_format(info->format);
	w = info->width;
	h = info->height;
	stride = info->stride;
	if ((w == 0U || h == 0U) && fallback_width != 0U && fallback_height != 0U) {
		w = fallback_width;
		h = fallback_height;
		if (stride == 0U) {
			stride = align_up_u32(w, fallback_align) * 4U;
		}
	}
	if (w == 0 || h == 0) {
		uint64_t i;
		uint64_t words = info->size / 4U;
		if (words == 0U) {
			return;
		}
		for (i = 0; i < words; i++) {
			fb32[i] = 0xffffffffU;
		}
		clean_dcache_range((uintptr_t) fb32, words * 4U);
		return;
	}
	if (stride == 0) {
		stride = w * bpp;
	}
	frame_bytes = (uint64_t) stride * (uint64_t) h;
	pages_to_paint = 1U;
	if (frame_bytes != 0U && info->size >= frame_bytes) {
		pages_to_paint = info->size / frame_bytes;
		if (pages_to_paint == 0U) {
			pages_to_paint = 1U;
		}
		if (pages_to_paint > 6U) {
			pages_to_paint = 6U;
		}
	}
	if (bpp == 4U && fallback_width != 0U && fallback_height != 0U &&
	    w == fallback_width && h == fallback_height) {
		render_logo_page_rgba(fb, stride, w, h);
		clean_dcache_range((uintptr_t) fb, frame_bytes);
		return;
	}

	for (page = 0; page < pages_to_paint; page++) {
		volatile uint8_t *page_fb = fb + (page * frame_bytes);
		for (y = 0; y < h; y++) {
			volatile uint8_t *line = page_fb + (uint64_t) y * (uint64_t) stride;
			for (x = 0; x < w; x++) {
				if (bpp == 2) {
					uint16_t c = (x < 8 || y < 8 || x > w - 9 || y > h - 9) ? 0xffffU : 0x07e0U;
					volatile uint16_t *p16 = (volatile uint16_t *) (line + x * 2U);
					*p16 = c;
				} else {
					volatile uint8_t *p32 = line + x * 4U;
					uint8_t r = (x < 8 || y < 8 || x > w - 9 || y > h - 9) ? 0xffU : (uint8_t) (x & 0xffU);
					uint8_t g = (uint8_t) (y & 0xffU);
					uint8_t b = 0x40U;
					/* B,G,R,A write works for common x8r8g8b8/a8r8g8b8 paths. */
					p32[0] = b;
					p32[1] = g;
					p32[2] = r;
					p32[3] = 0xffU;
				}
			}
		}
	}

	clean_dcache_range((uintptr_t) fb, frame_bytes * pages_to_paint);
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
		const char *lk_bootargs = fdt_find_chosen_string(
			(const void *) (uintptr_t) fdt_ptr, "bootargs");
		if (lk_bootargs != 0) {
			mk_stage0_mtk_usb_set_lk_bootargs(lk_bootargs);
			uart_puts_all("[mk] lk bootargs stored\r\n");
		}
	}
	init_menu_buttons_from_fdt((const void *) (uintptr_t) fdt_ptr);
	g_wdt_base = 0;
	g_wdt_active = 0;
	setup_wdt((const void *) (uintptr_t) fdt_ptr);

	uart_puts_all("[mk] wdt_base=0x");
	uart_puthex64_all(g_wdt_base);
	uart_puts_all("\r\n");
	parse_simplefb_from_fdt((const void *) (uintptr_t) fdt_ptr, &info);
	uart_puts_all("[mk] fb_addr=0x");
	uart_puthex64_all(info.addr);
	uart_puts_all(" fb_size=0x");
	uart_puthex64_all(info.size);
	uart_puts_all("\r\n");
	if (info.addr == 0) {
		parse_videolfb_from_chosen((const void *) (uintptr_t) fdt_ptr, &info);
		if (info.addr != 0) {
			uart_puts_all("[mk] fb fallback=atag,videolfb addr=0x");
			uart_puthex64_all(info.addr);
			uart_puts_all(" size=0x");
			uart_puthex64_all(info.size);
			uart_puts_all("\r\n");
		}
	}
	videolfb_lcmname =
		fdt_find_chosen_string((const void *) (uintptr_t) fdt_ptr,
				      "atag,videolfb-lcmname");
	(void) fdt_find_chosen_u64((const void *) (uintptr_t) fdt_ptr,
				 "atag,videolfb-islcmfound", &videolfb_islcmfound);
	(void) fdt_find_chosen_u64((const void *) (uintptr_t) fdt_ptr,
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

	phx_stage = fdt_find_chosen_string((const void *) (uintptr_t) fdt_ptr,
					 "mk,phoenix-bootstage");
	phx_partition = fdt_find_chosen_string((const void *) (uintptr_t) fdt_ptr,
					    "mk,phoenix-reserve-partition");
	phx_partition_fallback =
		fdt_find_chosen_string((const void *) (uintptr_t) fdt_ptr,
				      "mk,phoenix-reserve-fallback-partition");
	phx_magic = fdt_find_chosen_string((const void *) (uintptr_t) fdt_ptr,
					"mk,phoenix-record-magic");
	(void) fdt_find_chosen_u64((const void *) (uintptr_t) fdt_ptr,
				 "mk,phoenix-reserve-offset-ufs", &phx_ufs_offset);
	(void) fdt_find_chosen_u64((const void *) (uintptr_t) fdt_ptr,
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
			mk_boot_linux(fdt_ptr, g_peacock_boot_lba);
			uart_puts_all("[mk] direct linux handoff returned, entering menu fallback\r\n");
		}
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
				mk_boot_linux_override_kernel(fdt_ptr, g_peacock_boot_lba,
							       staged_kernel, staged_kernel_size);
				uart_puts_all("[mk] staged kernel handoff returned, re-entering menu\r\n");
				continue;
			}
			if (action == MK_FASTBOOT_ACTION_CONTINUE) {
				uart_puts_all("[mk] continuing to linux handoff\r\n");
				mk_boot_linux(fdt_ptr, g_peacock_boot_lba);
				uart_puts_all("[mk] linux handoff returned, re-entering menu\r\n");
				continue;
			}
		}
	}
	arm_recovery_wdt();
	delay_ms_calibrated(1500U);
	trigger_recovery_wdt_reset();
}
