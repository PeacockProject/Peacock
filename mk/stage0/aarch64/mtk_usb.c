#include <stdint.h>
#include "mtk_usb.h"
#include "mk_ext2.h"
#include "mtk_i2c.h"
#include "mtk_storage.h"

#define MTK_USB0_BASE 0x11200000ULL
/*
 * MT6765 downstream maps USB PHY through the 2nd "usb0" resource
 * (io_priv in kernel), which is 0x11CC0000 on this target.
 * 0x11210000 is the usb_sif node but does not back the PHY accesses used by
 * usb20 PHY programming sequence.
 */
#define MTK_USB_SIF_BASE 0x11cc0000ULL
#define MTK_I2C3_BASE 0x11009000ULL
#define MTK_I2C5_BASE 0x11016000ULL
#define MTK_TOPCKGEN_BASE 0x10000000ULL
#define MTK_INFRACFG_AO_BASE 0x10001000ULL
#define MTK_PMIC_WRAP_BASE 0x1000d000ULL
#define MTK_PERICFG_BASE 0x10003000ULL
#define MTK_DEVINFO_BASE 0x11c50000ULL
#define MTK_USB_L1INTS 0x00a0U
#define MTK_USB_L1INTM 0x00a4U

#define MTK_USB_TX_INT_STATUS (1U << 0)
#define MTK_USB_RX_INT_STATUS (1U << 1)
#define MTK_USB_USBCOM_INT_STATUS (1U << 2)
#define MTK_USB_DMA_INT_STATUS (1U << 3)
#define MTK_USB_QINT_STATUS (1U << 5)
#define MTK_USB_VBUS_STATUS (1U << 8)

#define MUSB_FADDR 0x00U
#define MUSB_POWER 0x01U
#define MUSB_INTRTX 0x02U
#define MUSB_INTRRX 0x04U
#define MUSB_INTRTXE 0x06U
#define MUSB_INTRRXE 0x08U
#define MUSB_INTRUSB 0x0aU
#define MUSB_INTRUSBE 0x0bU
#define MUSB_INDEX 0x0eU
#define MUSB_DEVCTL 0x60U
#define MUSB_TXFIFOSZ 0x62U
#define MUSB_RXFIFOSZ 0x63U
#define MUSB_TXFIFOADD 0x64U
#define MUSB_RXFIFOADD 0x66U
#define MUSB_ULPI_REG_DATA 0x74U
#define MUSB_CONFIGDATA 0x0fU
#define MUSB_HSDMA_INTR 0x200U

#define MUSB_POWER_SOFTCONN 0x40U
#define MUSB_POWER_ISOUPDATE 0x80U
#define MUSB_POWER_HSENAB 0x20U
#define MUSB_POWER_HSMODE 0x10U
#define MUSB_POWER_ENSUSPEND 0x01U

#define MUSB_INTR_RESET 0x04U
#define MUSB_INTR_CONNECT 0x10U
#define MUSB_INTR_SUSPEND 0x01U
#define MUSB_INTR_RESUME 0x02U
#define MUSB_INTR_DISCONNECT 0x20U

#define MUSB_DEVCTL_SESSION 0x01U

#define MUSB_CSR0 0x12U
#define MUSB_COUNT0 0x18U
#define MUSB_TXMAXP 0x10U
#define MUSB_RXMAXP 0x14U

#define MUSB_CSR0_TXPKTRDY 0x0002U
#define MUSB_CSR0_RXPKTRDY 0x0001U
#define MUSB_CSR0_P_SVDSETUPEND 0x0080U
#define MUSB_CSR0_P_SVDRXPKTRDY 0x0040U
#define MUSB_CSR0_P_SENDSTALL 0x0020U
#define MUSB_CSR0_P_SETUPEND 0x0010U
#define MUSB_CSR0_P_DATAEND 0x0008U
#define MUSB_CSR0_FLUSHFIFO 0x0100U

#define MUSB_FIFO_EP0 0x20U
#define MUSB_FIFO_EP1 0x24U

#define MUSB_TXCSR 0x12U
#define MUSB_RXCSR 0x16U
#define MUSB_RXCOUNT 0x18U

#define MUSB_TXCSR_TXPKTRDY 0x0001U
#define MUSB_TXCSR_FLUSHFIFO 0x0008U

#define MUSB_RXCSR_RXPKTRDY 0x0001U
#define MUSB_RXCSR_FLUSHFIFO 0x0010U

#define MTK_CLK_CFG_5 0x090U
#define MTK_CLK_CFG_5_SET 0x094U
#define MTK_CLK_CFG_5_CLR 0x098U
#define MTK_CLK_CFG_UPDATE 0x004U
#define MTK_CLK_CFG_5_USB_TOP_SEL_BIT 16U
#define MTK_CLK_CFG_5_USB_TOP_GATE_BIT 23U
#define MTK_CLK_CFG_UPDATE_USB_TOP_SEL_BIT 22U

#define MTK_IFR2_CLR 0x084U
#define MTK_IFR2_SET 0x080U
#define MTK_IFR2_STA 0x090U
#define MTK_IFR_ICUSB_BIT 8U
#define MTK_PERI_USB_SW_RST_BIT 29U

#define PWRAP_WACS2_EN 0x09cU
#define PWRAP_INIT_DONE2 0x0a0U
#define PWRAP_WACS2_CMD 0x0c20U
#define PWRAP_WACS2_RDATA 0x0c24U
#define PWRAP_WACS2_VLDCLR 0x0c28U

#define PWRAP_WACS_FSM_MASK (0x7U << 16)
#define PWRAP_WACS_FSM_IDLE (0x0U << 16)
#define PWRAP_WACS_FSM_WFVLDCLR (0x6U << 16)

#define MT6357_RG_LDO_VUSB33_EN_0_ADDR 0x199cU
#define MT6357_RG_LDO_VUSB33_EN_0_BIT 0U
#define MT6357_LDO_VUSB33_OP_EN_ADDR 0x199eU
#define MT6357_LDO_VUSB33_OP_CFG_ADDR 0x19a4U

/* MT6357 VEMC (eMMC VCC, 3.0 V). LDO_VEMC_CON0 = 0x1988, bit 0 = RG_LDO_VEMC_EN. */
#define MT6357_RG_LDO_VEMC_EN_ADDR 0x1988U
#define MT6357_RG_LDO_VEMC_EN_BIT  0U

#define SPARSE_MAGIC 0xed26ff3aU
#define CHUNK_TYPE_RAW 0xcac1U
#define CHUNK_TYPE_FILL 0xcac2U
#define CHUNK_TYPE_DONT 0xcac3U
#define CHUNK_TYPE_CRC32 0xcac4U

#define U2PHY_COM_BASE 0x800U
#define U3P_USBPHYACR0 0x000U
#define U3P_USBPHYACR1 0x004U
#define U3P_USBPHYACR5 0x014U
#define U3P_USBPHYACR6 0x018U
#define U3P_U2PHYACR4 0x020U
#define U3P_U2PHYDTM0 0x068U
#define U3P_U2PHYDTM1 0x06cU
#define U3P_U2PHYBC12C 0x080U

#define PA0_RG_USB20_INTR_EN (1U << 5)
#define PA5_RG_U2_HS_100U_U3_EN (1U << 11)
#define PA6_RG_U2_BC11_SW_EN (1U << 23)
#define PA6_RG_U2_OTG_VBUSCMP_EN (1U << 20)
#define PA6_RG_U2_SQTH_MASK 0x0fU
#define PA6_RG_U2_SQTH_VAL(x) ((uint32_t) ((x) & 0x0fU))
#define PA1_RG_USB20_HSTX_SRCTRL_MASK (0x7U << 12)
#define PA1_RG_USB20_HSTX_SRCTRL_VAL(x) ((uint32_t) (((x) & 0x7U) << 12))

#define MK_USB_PHY_DEVICE_VRT_REF 7U
#define MK_USB_PHY_DEVICE_TERM_REF 7U
#define MK_USB_PHY_DEVICE_REV6_REF 2U
#define MK_USB_PHY_INTR_CAL_EFUSE_WORD 107U
#define MK_USB_PHY_INTR_CAL_MASK 0x1fU
#define MK_USB_PHY_INTR_CAL_SHIFT 19U

#define P2C_RG_USB20_GPIO_CTL (1U << 9)
#define P2C_USB20_GPIO_MODE (1U << 8)
#define P2C_U2_GPIO_CTR_MSK (P2C_RG_USB20_GPIO_CTL | P2C_USB20_GPIO_MODE)

#define P2C_FORCE_UART_EN (1U << 26)
#define P2C_FORCE_SUSPENDM (1U << 18)
#define P2C_RG_DATAIN_MASK (0x0fU << 10)
#define P2C_RG_XCVRSEL_MASK (0x03U << 4)
#define P2C_RG_XCVRSEL_VAL(x) ((uint32_t) (((x) & 0x03U) << 4))
#define P2C_DTM0_PART_MASK ((1U << 23) | (1U << 21) | (1U << 20) | (1U << 19) | \
			    (1U << 17) | (1U << 7) | (1U << 6) | (1U << 2))

#define P2C_RG_UART_EN (1U << 16)
#define P2C_FORCE_IDDIG (1U << 9)
#define P2C_RG_VBUSVALID (1U << 5)
#define P2C_RG_SESSEND (1U << 4)
#define P2C_RG_AVALID (1U << 2)
#define P2C_RG_IDDIG (1U << 1)

#define P2C_RG_CHGDT_EN (1U << 0)

#define USB_DIR_IN 0x80U

#define USB_REQ_GET_STATUS 0x00U
#define USB_REQ_CLEAR_FEATURE 0x01U
#define USB_REQ_SET_FEATURE 0x03U
#define USB_REQ_SET_ADDRESS 0x05U
#define USB_REQ_GET_DESCRIPTOR 0x06U
#define USB_REQ_SET_DESCRIPTOR 0x07U
#define USB_REQ_GET_CONFIGURATION 0x08U
#define USB_REQ_SET_CONFIGURATION 0x09U
#define USB_REQ_GET_INTERFACE 0x0aU
#define USB_REQ_SET_INTERFACE 0x0bU

#define USB_DT_DEVICE 0x01U
#define USB_DT_CONFIG 0x02U
#define USB_DT_STRING 0x03U
#define USB_DT_DEVICE_QUALIFIER 0x06U
#define USB_DT_OTHER_SPEED_CONFIG 0x07U

#define USB_DESC_TYPE_FASTBOOT_CLASS 0xffU
#define USB_DESC_SUBCLASS_FASTBOOT 0x42U
#define USB_DESC_PROTOCOL_FASTBOOT 0x03U

#define USB_VID_GOOGLE 0x18d1U
#define USB_PID_FASTBOOT 0x4ee0U
#define MK_USB_STRING_ASCII_MAX 32U
#define MK_FASTBOOT_CMD_MAX 64U
#define MK_FASTBOOT_DOWNLOAD_MAX (64U * 1024U * 1024U)

/* SGM7220 Type-C controller (i2c5 @ 0x47) */
#define SGM7220_I2C_ADDR 0x47U
#define SGM7220_REG_MOD 0x08U
#define SGM7220_REG_INT 0x09U
#define SGM7220_REG_SET 0x0aU

#define SGM7220_MOD_CURRENT_MODE_ADVERTISE_SHIFT 6U
#define SGM7220_MOD_CURRENT_MODE_ADVERTISE_MASK (0x03U << SGM7220_MOD_CURRENT_MODE_ADVERTISE_SHIFT)

#define SGM7220_INT_SET_DISABLE_UFP_ACCESS 0x01U

#define SGM7220_SET_DISABLE_TERM 0x01U
#define SGM7220_SET_MODE_SELECT_SHIFT 4U
#define SGM7220_SET_MODE_SELECT_MASK (0x03U << SGM7220_SET_MODE_SELECT_SHIFT)
#define SGM7220_SET_MODE_SELECT_SNK 0x01U

typedef struct {
	uint8_t bm_request_type;
	uint8_t b_request;
	uint16_t w_value;
	uint16_t w_index;
	uint16_t w_length;
} usb_setup_packet_t;

typedef struct {
	uint8_t configured;
	uint8_t address;
	uint8_t pending_address;
	uint8_t address_pending;
	uint8_t started;
	uint32_t poll_count;
	uint32_t reset_count;
	uint8_t debug_once;
	uint8_t ep1_ready;
} usb_fastboot_state_t;

extern void uart_puts_all(const char *s);
extern void uart_puthex64_all(uint64_t v);

static uint64_t mk_rd_cntpct(void)
{
	uint64_t v;
	__asm__ volatile("mrs %0, cntpct_el0" : "=r"(v));
	return v;
}

static uint64_t mk_rd_cntfrq(void)
{
	uint64_t v;
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
	return v;
}

/* Calibrated delay using the ARM system counter (accurate regardless of CPU clock). */
static void mk_delay_ms(uint32_t ms)
{
	uint64_t freq = mk_rd_cntfrq();
	uint64_t start = mk_rd_cntpct();
	uint64_t ticks;

	if (freq == 0U) {
		return;
	}
	ticks = (freq * (uint64_t) ms) / 1000ULL;
	while ((mk_rd_cntpct() - start) < ticks) {
		__asm__ volatile("");
	}
}

/* Print elapsed ms since 'start_ct' using the system counter. */
static void mk_print_elapsed_ms(uint64_t start_ct, const char *label)
{
	uint64_t freq = mk_rd_cntfrq();
	uint64_t elapsed = mk_rd_cntpct() - start_ct;
	uint64_t ms;

	ms = (freq != 0U) ? ((elapsed * 1000ULL) / freq) : 0ULL;
	uart_puts_all("[mk] t+");
	uart_puthex64_all(ms);
	uart_puts_all("ms ");
	uart_puts_all(label);
	uart_puts_all("\r\n");
}

static usb_fastboot_state_t g_usb_state;
static uint64_t g_tcpc_i2c_base;
static uint8_t g_tcpc_addr;
static uint8_t g_tcpc_ready;
static struct {
	uint64_t i2c_base;
	uint8_t addr;
	uint8_t mod;
	uint8_t intr;
	uint8_t set;
	uint8_t valid;
} g_tcpc_saved;
static struct {
	uint16_t en0;
	uint16_t op_en;
	uint16_t op_cfg;
	uint8_t valid;
} g_vusb33_saved;
static struct {
	uint32_t clk_cfg_5;
	uint32_t ifr2_sta;
	uint8_t valid;
} g_usb_clock_saved;
static struct {
	uint32_t acr1;
	uint32_t acr5;
	uint32_t acr6;
	uint32_t acr3;
	uint32_t acr4;
	uint32_t dtm0;
	uint32_t dtm1;
	uint32_t bc12c;
	uint8_t valid;
} g_usb_phy_saved;
static uint8_t g_usb_serial_ascii[MK_USB_STRING_ASCII_MAX + 1U] = "GUCINNG6FAVW99OR";
static uint8_t g_usb_serial_ascii_len = 16U;
static char g_lk_bootargs[1024];
static uint32_t g_lk_bootargs_len = 0U;
static uint8_t g_usb_string_desc_buf[2U + (MK_USB_STRING_ASCII_MAX * 2U)];
static uint8_t g_fastboot_cmd_buf[MK_FASTBOOT_CMD_MAX + 1U];
static uint8_t g_fastboot_resp_buf[MK_FASTBOOT_CMD_MAX];
static const uint8_t g_fb_okay_version[] = "OKAY0.4";
static const uint8_t g_fb_okay_product[] = "OKAYMimKermel23";
static const uint8_t g_fb_okay_empty[] = "OKAY";
static const uint8_t g_fb_okay_raw[] = "OKAYraw";
static const uint8_t g_fb_okay_no[] = "OKAYno";
static const uint8_t g_fb_okay_zero[] = "OKAY0";
static uint8_t g_fastboot_sector_buf[512];
static uint8_t g_sparse_fill_buf[512U * 256U];
static uint8_t g_fastboot_download_buf[MK_FASTBOOT_DOWNLOAD_MAX];
static uint32_t g_fastboot_download_expected;
static uint32_t g_fastboot_download_received;
static uint32_t g_fastboot_download_staged_size;
static uint8_t g_fastboot_download_active;
static uint8_t g_fastboot_pending_action;
static uint8_t g_fastboot_fetch_trace_packets;
static uint32_t g_fastboot_fetch_trace_seq;

#define MTK_GPIO_BASE 0x10005000ULL
#define MTK_GPIO_DIR_BASE 0x0000U
#define MTK_GPIO_DO_BASE 0x0100U
#define MTK_GPIO_DI_BASE 0x0200U
#define MTK_GPIO_MODE_BASE 0x0300U
#define MTK_GPIO_GROUP_STRIDE 0x0010U

#define MK_USB_TCPC_SCL_GPIO 48U
#define MK_USB_TCPC_SDA_GPIO 49U

static inline uint32_t raw_read32(uint64_t addr)
{
	return *(volatile uint32_t *) (uintptr_t) addr;
}

static inline void raw_write32(uint64_t addr, uint32_t value)
{
	*(volatile uint32_t *) (uintptr_t) addr = value;
}

static void bb_delay(void)
{
	for (volatile uint32_t spin = 0; spin < 150U; spin++) {
		__asm__ volatile("" ::: "memory");
	}
}

static void bb_set_mode_gpio(uint32_t pin)
{
	uint32_t group = pin / 8U;
	uint32_t shift = (pin % 8U) * 4U;
	uint64_t addr = MTK_GPIO_BASE + MTK_GPIO_MODE_BASE + ((uint64_t) group * MTK_GPIO_GROUP_STRIDE);
	uint32_t val = raw_read32(addr);

	val &= ~(0xfU << shift);
	raw_write32(addr, val);
}

static void bb_set_mode_i2c(uint32_t pin)
{
	uint32_t group = pin / 8U;
	uint32_t shift = (pin % 8U) * 4U;
	uint64_t addr = MTK_GPIO_BASE + MTK_GPIO_MODE_BASE + ((uint64_t) group * MTK_GPIO_GROUP_STRIDE);
	uint32_t val = raw_read32(addr);

	val &= ~(0xfU << shift);
	val |= (1U << shift);
	raw_write32(addr, val);
}

static void bb_set_dir(uint32_t pin, uint32_t out)
{
	uint32_t group = pin / 32U;
	uint32_t bit = pin % 32U;
	uint64_t addr = MTK_GPIO_BASE + MTK_GPIO_DIR_BASE + ((uint64_t) group * MTK_GPIO_GROUP_STRIDE);
	uint32_t val = raw_read32(addr);

	if (out != 0U) {
		val |= (1U << bit);
	} else {
		val &= ~(1U << bit);
	}
	raw_write32(addr, val);
}

static void bb_set_do(uint32_t pin, uint32_t high)
{
	uint32_t group = pin / 32U;
	uint32_t bit = pin % 32U;
	uint64_t addr = MTK_GPIO_BASE + MTK_GPIO_DO_BASE + ((uint64_t) group * MTK_GPIO_GROUP_STRIDE);
	uint32_t val = raw_read32(addr);

	if (high != 0U) {
		val |= (1U << bit);
	} else {
		val &= ~(1U << bit);
	}
	raw_write32(addr, val);
}

static uint32_t bb_get_di(uint32_t pin)
{
	uint32_t group = pin / 32U;
	uint32_t bit = pin % 32U;
	uint64_t addr = MTK_GPIO_BASE + MTK_GPIO_DI_BASE + ((uint64_t) group * MTK_GPIO_GROUP_STRIDE);

	return (raw_read32(addr) >> bit) & 1U;
}

static void bb_release_line(uint32_t pin)
{
	/* Emulate open-drain high: input mode + output latch high. */
	bb_set_do(pin, 1U);
	bb_set_dir(pin, 0U);
}

static void bb_drive_low(uint32_t pin)
{
	bb_set_do(pin, 0U);
	bb_set_dir(pin, 1U);
}

static void bb_i2c_prepare(void)
{
	mk_stage0_mtk_i2c_snapshot_if_needed();
	bb_set_mode_gpio(MK_USB_TCPC_SCL_GPIO);
	bb_set_mode_gpio(MK_USB_TCPC_SDA_GPIO);
	bb_release_line(MK_USB_TCPC_SCL_GPIO);
	bb_release_line(MK_USB_TCPC_SDA_GPIO);
	bb_delay();
}

static void bb_i2c_start(void)
{
	bb_release_line(MK_USB_TCPC_SDA_GPIO);
	bb_release_line(MK_USB_TCPC_SCL_GPIO);
	bb_delay();
	bb_drive_low(MK_USB_TCPC_SDA_GPIO);
	bb_delay();
	bb_drive_low(MK_USB_TCPC_SCL_GPIO);
	bb_delay();
}

static void bb_i2c_stop(void)
{
	bb_drive_low(MK_USB_TCPC_SDA_GPIO);
	bb_delay();
	bb_release_line(MK_USB_TCPC_SCL_GPIO);
	bb_delay();
	bb_release_line(MK_USB_TCPC_SDA_GPIO);
	bb_delay();
}

static void bb_i2c_write_bit(uint32_t bit)
{
	if (bit != 0U) {
		bb_release_line(MK_USB_TCPC_SDA_GPIO);
	} else {
		bb_drive_low(MK_USB_TCPC_SDA_GPIO);
	}
	bb_delay();
	bb_release_line(MK_USB_TCPC_SCL_GPIO);
	bb_delay();
	bb_drive_low(MK_USB_TCPC_SCL_GPIO);
	bb_delay();
}

static int bb_i2c_write_byte(uint8_t byte)
{
	for (uint32_t i = 0; i < 8U; i++) {
		bb_i2c_write_bit((byte & 0x80U) != 0U);
		byte <<= 1;
	}

	/* ACK cycle */
	bb_release_line(MK_USB_TCPC_SDA_GPIO);
	bb_delay();
	bb_release_line(MK_USB_TCPC_SCL_GPIO);
	bb_delay();
	if (bb_get_di(MK_USB_TCPC_SDA_GPIO) != 0U) {
		bb_drive_low(MK_USB_TCPC_SCL_GPIO);
		bb_delay();
		return -1;
	}
	bb_drive_low(MK_USB_TCPC_SCL_GPIO);
	bb_delay();
	return 0;
}

static uint8_t bb_i2c_read_byte(uint32_t ack)
{
	uint8_t val = 0U;

	bb_release_line(MK_USB_TCPC_SDA_GPIO);
	for (uint32_t i = 0; i < 8U; i++) {
		val <<= 1;
		bb_release_line(MK_USB_TCPC_SCL_GPIO);
		bb_delay();
		val |= (uint8_t) (bb_get_di(MK_USB_TCPC_SDA_GPIO) & 1U);
		bb_drive_low(MK_USB_TCPC_SCL_GPIO);
		bb_delay();
	}

	/* ACK from master */
	if (ack != 0U) {
		bb_drive_low(MK_USB_TCPC_SDA_GPIO);
	} else {
		bb_release_line(MK_USB_TCPC_SDA_GPIO);
	}
	bb_delay();
	bb_release_line(MK_USB_TCPC_SCL_GPIO);
	bb_delay();
	bb_drive_low(MK_USB_TCPC_SCL_GPIO);
	bb_delay();
	bb_release_line(MK_USB_TCPC_SDA_GPIO);
	return val;
}

static int usb_sgm7220_write_reg8_bitbang(uint8_t addr7, uint8_t reg, uint8_t value)
{
	bb_i2c_prepare();
	bb_i2c_start();
	if (bb_i2c_write_byte((uint8_t) (addr7 << 1)) != 0) {
		bb_i2c_stop();
		return -1;
	}
	if (bb_i2c_write_byte(reg) != 0) {
		bb_i2c_stop();
		return -1;
	}
	if (bb_i2c_write_byte(value) != 0) {
		bb_i2c_stop();
		return -1;
	}
	bb_i2c_stop();
	return 0;
}

static int usb_sgm7220_read_reg8_bitbang(uint8_t addr7, uint8_t reg, uint8_t *out)
{
	bb_i2c_prepare();
	bb_i2c_start();
	if (bb_i2c_write_byte((uint8_t) (addr7 << 1)) != 0) {
		bb_i2c_stop();
		return -1;
	}
	if (bb_i2c_write_byte(reg) != 0) {
		bb_i2c_stop();
		return -1;
	}
	bb_i2c_start();
	if (bb_i2c_write_byte((uint8_t) ((addr7 << 1) | 1U)) != 0) {
		bb_i2c_stop();
		return -1;
	}
	*out = bb_i2c_read_byte(0U);
	bb_i2c_stop();
	return 0;
}

static void usb_sgm7220_dump_status(uint8_t addr7)
{
	uint8_t mod = 0U;
	uint8_t intr = 0U;
	uint8_t set = 0U;

	if (usb_sgm7220_read_reg8_bitbang(addr7, SGM7220_REG_MOD, &mod) != 0) {
		return;
	}
	if (usb_sgm7220_read_reg8_bitbang(addr7, SGM7220_REG_INT, &intr) != 0) {
		return;
	}
	if (usb_sgm7220_read_reg8_bitbang(addr7, SGM7220_REG_SET, &set) != 0) {
		return;
	}

	uart_puts_all("[mk] usb tcpc: mod=0x");
	uart_puthex64_all(mod);
	uart_puts_all(" int=0x");
	uart_puthex64_all(intr);
	uart_puts_all(" set=0x");
	uart_puthex64_all(set);
	uart_puts_all("\r\n");
}

static int usb_sgm7220_write_reg8(uint64_t i2c_base, uint8_t addr7, uint8_t reg, uint8_t value)
{
	int ret = mk_stage0_mtk_i2c_write_reg8(i2c_base, addr7, reg, value);

	if (ret == 0) {
		return 0;
	}
	if (usb_sgm7220_write_reg8_bitbang(addr7, reg, value) == 0) {
		uart_puts_all("[mk] usb tcpc: i2c bitbang fallback ok reg=0x");
		uart_puthex64_all(reg);
		uart_puts_all(" val=0x");
		uart_puthex64_all(value);
		uart_puts_all("\r\n");
		return 0;
	}
	return -1;
}

static int usb_sgm7220_read_reg8(uint64_t i2c_base, uint8_t addr7, uint8_t reg, uint8_t *out)
{
	(void) i2c_base;
	return usb_sgm7220_read_reg8_bitbang(addr7, reg, out);
}

static int usb_sgm7220_update_reg8(uint64_t i2c_base, uint8_t addr7,
				   uint8_t reg, uint8_t value, uint8_t mask)
{
	uint8_t oldv = 0U;
	uint8_t newv;

	if (usb_sgm7220_read_reg8(i2c_base, addr7, reg, &oldv) != 0) {
		return -1;
	}
	newv = (uint8_t) ((oldv & (uint8_t) ~mask) | (value & mask));
	return usb_sgm7220_write_reg8(i2c_base, addr7, reg, newv);
}

static int usb_try_force_typec_sink_sgm7220_on(uint64_t i2c_base, uint8_t addr7)
{
	int ret;
	uint8_t mode_val;

	if (g_tcpc_saved.valid == 0U) {
		if (usb_sgm7220_read_reg8(i2c_base, addr7, SGM7220_REG_MOD, &g_tcpc_saved.mod) == 0 &&
		    usb_sgm7220_read_reg8(i2c_base, addr7, SGM7220_REG_INT, &g_tcpc_saved.intr) == 0 &&
		    usb_sgm7220_read_reg8(i2c_base, addr7, SGM7220_REG_SET, &g_tcpc_saved.set) == 0) {
			g_tcpc_saved.i2c_base = i2c_base;
			g_tcpc_saved.addr = addr7;
			g_tcpc_saved.valid = 1U;
		}
	}

	/*
	 * Mirror kernel tcpc_sgm7220 flow:
	 * REG_SET init, disable-UFP-access policy bit, advertise 3A,
	 * then set CC mode to sink.
	 */
	ret = usb_sgm7220_write_reg8(i2c_base, addr7, SGM7220_REG_SET, 0x02U);
	if (ret != 0) {
		uart_puts_all("[mk] usb tcpc: init reg_set failed base=0x");
		uart_puthex64_all(i2c_base);
		uart_puts_all(" addr=0x");
		uart_puthex64_all(addr7);
		uart_puts_all(" err=0x");
		uart_puthex64_all((uint64_t) (uint32_t) mk_stage0_mtk_i2c_last_error());
		uart_puts_all(" st=0x");
		uart_puthex64_all((uint64_t) mk_stage0_mtk_i2c_last_status());
		uart_puts_all("\r\n");
		return -1;
	}
	ret = usb_sgm7220_update_reg8(i2c_base, addr7, SGM7220_REG_INT,
				      SGM7220_INT_SET_DISABLE_UFP_ACCESS,
				      SGM7220_INT_SET_DISABLE_UFP_ACCESS);
	if (ret != 0) {
		return -1;
	}
	ret = usb_sgm7220_update_reg8(i2c_base, addr7, SGM7220_REG_MOD,
				      (uint8_t) (0x02U << SGM7220_MOD_CURRENT_MODE_ADVERTISE_SHIFT),
				      SGM7220_MOD_CURRENT_MODE_ADVERTISE_MASK);
	if (ret != 0) {
		return -1;
	}
	ret = usb_sgm7220_update_reg8(i2c_base, addr7, SGM7220_REG_SET,
				      SGM7220_SET_DISABLE_TERM, SGM7220_SET_DISABLE_TERM);
	if (ret != 0) {
		return -1;
	}
	mode_val = (uint8_t) (SGM7220_SET_MODE_SELECT_SNK << SGM7220_SET_MODE_SELECT_SHIFT);
	ret = usb_sgm7220_update_reg8(i2c_base, addr7, SGM7220_REG_SET,
				      mode_val, SGM7220_SET_MODE_SELECT_MASK);
	if (ret != 0) {
		return -1;
	}
	ret = usb_sgm7220_update_reg8(i2c_base, addr7, SGM7220_REG_SET,
				      0x00U, SGM7220_SET_DISABLE_TERM);
	if (ret != 0) {
		return -1;
	}
	usb_sgm7220_dump_status(addr7);
	uart_puts_all("[mk] usb tcpc: sink configured base=0x");
	uart_puthex64_all(i2c_base);
	uart_puts_all(" addr=0x");
	uart_puthex64_all(addr7);
	uart_puts_all("\r\n");
	g_tcpc_i2c_base = i2c_base;
	g_tcpc_addr = addr7;
	g_tcpc_ready = 1U;
	return 0;
}

static void usb_try_force_typec_sink_sgm7220(void)
{
	g_tcpc_ready = 0U;

	/* Try known bus/address combos seen on this board family. */
	if (usb_try_force_typec_sink_sgm7220_on(MTK_I2C5_BASE, 0x47U) == 0)
		return;
	if (usb_try_force_typec_sink_sgm7220_on(MTK_I2C5_BASE, 0x60U) == 0)
		return;
	if (usb_try_force_typec_sink_sgm7220_on(MTK_I2C3_BASE, 0x47U) == 0)
		return;
	(void) usb_try_force_typec_sink_sgm7220_on(MTK_I2C3_BASE, 0x60U);
}

static void usb_wait_typec_attach_sgm7220(void)
{
	uint8_t reg_int = 0U;
	uint8_t reg_mod = 0U;
	uint32_t loops;

	if (g_tcpc_ready == 0U) {
		return;
	}

	for (loops = 0U; loops < 50U; loops++) {
		if (usb_sgm7220_read_reg8(g_tcpc_i2c_base, g_tcpc_addr, SGM7220_REG_INT, &reg_int) == 0 &&
		    usb_sgm7220_read_reg8(g_tcpc_i2c_base, g_tcpc_addr, SGM7220_REG_MOD, &reg_mod) == 0) {
			uint8_t attach = (uint8_t) ((reg_int >> 6U) & 0x03U);
			if (attach != 0U) {
				uart_puts_all("[mk] usb tcpc: attach=0x");
				uart_puthex64_all(attach);
				uart_puts_all(" int=0x");
				uart_puthex64_all(reg_int);
				uart_puts_all(" mod=0x");
				uart_puthex64_all(reg_mod);
				uart_puts_all("\r\n");
				return;
			}
		}

		/* Keep sink request alive while unattached. */
		if ((loops % 25U) == 0U) {
			(void) usb_try_force_typec_sink_sgm7220_on(g_tcpc_i2c_base, g_tcpc_addr);
		}

		for (volatile uint32_t spin = 0; spin < 120000U; spin++) {
			__asm__ volatile("");
		}
	}

	uart_puts_all("[mk] usb tcpc: attach timeout int=0x");
	uart_puthex64_all(reg_int);
	uart_puts_all(" mod=0x");
	uart_puthex64_all(reg_mod);
	uart_puts_all("\r\n");
}

static volatile uint8_t *usb_regs(void)
{
	return (volatile uint8_t *) (uintptr_t) MTK_USB0_BASE;
}

static inline uint8_t mmio_read8(volatile uint8_t *base, uint32_t off)
{
	return *(volatile uint8_t *) (base + off);
}

static inline uint16_t mmio_read16(volatile uint8_t *base, uint32_t off)
{
	return *(volatile uint16_t *) (volatile void *) (base + off);
}

static inline uint32_t mmio_read32(volatile uint8_t *base, uint32_t off)
{
	return *(volatile uint32_t *) (volatile void *) (base + off);
}

static inline void mmio_write8(volatile uint8_t *base, uint32_t off, uint8_t v)
{
	*(volatile uint8_t *) (base + off) = v;
}

static inline void mmio_write16(volatile uint8_t *base, uint32_t off, uint16_t v)
{
	*(volatile uint16_t *) (volatile void *) (base + off) = v;
}

static inline void mmio_write32(volatile uint8_t *base, uint32_t off, uint32_t v)
{
	*(volatile uint32_t *) (volatile void *) (base + off) = v;
}

static int pwrap_wait_idle(volatile uint8_t *base)
{
	for (uint32_t spin = 0; spin < 200000U; spin++) {
		uint32_t val = mmio_read32(base, PWRAP_WACS2_RDATA);
		if ((val & PWRAP_WACS_FSM_MASK) == PWRAP_WACS_FSM_IDLE) {
			return 0;
		}
	}
	return -1;
}

static int pwrap_wait_vldclr(volatile uint8_t *base, uint16_t *rdata)
{
	for (uint32_t spin = 0; spin < 200000U; spin++) {
		uint32_t val = mmio_read32(base, PWRAP_WACS2_RDATA);
		if ((val & PWRAP_WACS_FSM_MASK) == PWRAP_WACS_FSM_WFVLDCLR) {
			*rdata = (uint16_t) (val & 0xffffU);
			return 0;
		}
	}
	return -1;
}

static int pwrap_read16(uint32_t adr, uint16_t *rdata)
{
	volatile uint8_t *base = (volatile uint8_t *) (uintptr_t) MTK_PMIC_WRAP_BASE;
	uint32_t val;

	if (mmio_read32(base, PWRAP_INIT_DONE2) == 0U) {
		return -1;
	}
	val = mmio_read32(base, PWRAP_WACS2_RDATA);
	if ((val & PWRAP_WACS_FSM_MASK) == PWRAP_WACS_FSM_WFVLDCLR) {
		mmio_write32(base, PWRAP_WACS2_VLDCLR, 1U);
	}
	if (pwrap_wait_idle(base) != 0) {
		return -1;
	}

	mmio_write32(base, PWRAP_WACS2_CMD, ((adr >> 1U) << 16));
	if (pwrap_wait_vldclr(base, rdata) != 0) {
		return -1;
	}
	mmio_write32(base, PWRAP_WACS2_VLDCLR, 1U);
	return 0;
}

static int pwrap_write16(uint32_t adr, uint16_t wdata)
{
	volatile uint8_t *base = (volatile uint8_t *) (uintptr_t) MTK_PMIC_WRAP_BASE;
	uint32_t val;

	if (mmio_read32(base, PWRAP_INIT_DONE2) == 0U) {
		return -1;
	}
	val = mmio_read32(base, PWRAP_WACS2_RDATA);
	if ((val & PWRAP_WACS_FSM_MASK) == PWRAP_WACS_FSM_WFVLDCLR) {
		mmio_write32(base, PWRAP_WACS2_VLDCLR, 1U);
	}
	if (pwrap_wait_idle(base) != 0) {
		return -1;
	}

	mmio_write32(base, PWRAP_WACS2_CMD, (1U << 31) | ((adr >> 1U) << 16) | wdata);
	return 0;
}

static void usb_enable_vusb33(void)
{
	uint16_t regv;
	uint16_t op_en;
	uint16_t op_cfg;
	volatile uint8_t *base = (volatile uint8_t *) (uintptr_t) MTK_PMIC_WRAP_BASE;

	if (g_vusb33_saved.valid == 0U) {
		if (pwrap_read16(MT6357_RG_LDO_VUSB33_EN_0_ADDR, &g_vusb33_saved.en0) == 0 &&
		    pwrap_read16(MT6357_LDO_VUSB33_OP_EN_ADDR, &g_vusb33_saved.op_en) == 0 &&
		    pwrap_read16(MT6357_LDO_VUSB33_OP_CFG_ADDR, &g_vusb33_saved.op_cfg) == 0) {
			g_vusb33_saved.valid = 1U;
		}
	}

	if (pwrap_read16(MT6357_RG_LDO_VUSB33_EN_0_ADDR, &regv) != 0) {
		uart_puts_all("[mk] usb vusb33: pwrap read failed en=0x");
		uart_puthex64_all(mmio_read32(base, PWRAP_WACS2_EN));
		uart_puts_all(" init2=0x");
		uart_puthex64_all(mmio_read32(base, PWRAP_INIT_DONE2));
		uart_puts_all(" rdata=0x");
		uart_puthex64_all(mmio_read32(base, PWRAP_WACS2_RDATA));
		uart_puts_all("\r\n");
		return;
	}

	uart_puts_all("[mk] usb vusb33: en0=0x");
	uart_puthex64_all(regv);
	uart_puts_all("\r\n");

	regv |= (uint16_t) (1U << MT6357_RG_LDO_VUSB33_EN_0_BIT);
	if (pwrap_write16(MT6357_RG_LDO_VUSB33_EN_0_ADDR, regv) != 0) {
		uart_puts_all("[mk] usb vusb33: pwrap write failed\r\n");
		return;
	}

	if (pwrap_read16(MT6357_RG_LDO_VUSB33_EN_0_ADDR, &regv) == 0) {
		uart_puts_all("[mk] usb vusb33: en0-now=0x");
		uart_puthex64_all(regv);
		uart_puts_all("\r\n");
	}

	/*
	 * Match vendor regulator behavior closer: make sure VUSB33 op path
	 * is enabled in both OP_EN and OP_CFG.
	 */
	if (pwrap_read16(MT6357_LDO_VUSB33_OP_EN_ADDR, &op_en) == 0) {
		op_en |= 0x000fU;
		(void) pwrap_write16(MT6357_LDO_VUSB33_OP_EN_ADDR, op_en);
		if (pwrap_read16(MT6357_LDO_VUSB33_OP_EN_ADDR, &op_en) == 0) {
			uart_puts_all("[mk] usb vusb33: op_en=0x");
			uart_puthex64_all(op_en);
			uart_puts_all("\r\n");
		}
	}
	if (pwrap_read16(MT6357_LDO_VUSB33_OP_CFG_ADDR, &op_cfg) == 0) {
		op_cfg |= 0x000fU;
		(void) pwrap_write16(MT6357_LDO_VUSB33_OP_CFG_ADDR, op_cfg);
		if (pwrap_read16(MT6357_LDO_VUSB33_OP_CFG_ADDR, &op_cfg) == 0) {
			uart_puts_all("[mk] usb vusb33: op_cfg=0x");
			uart_puthex64_all(op_cfg);
			uart_puts_all("\r\n");
		}
	}
}

static void usb_restore_vusb33(void)
{
	if (g_vusb33_saved.valid == 0U) {
		return;
	}

	(void) pwrap_write16(MT6357_LDO_VUSB33_OP_EN_ADDR, g_vusb33_saved.op_en);
	(void) pwrap_write16(MT6357_LDO_VUSB33_OP_CFG_ADDR, g_vusb33_saved.op_cfg);
	(void) pwrap_write16(MT6357_RG_LDO_VUSB33_EN_0_ADDR, g_vusb33_saved.en0);
	uart_puts_all("[mk] usb vusb33: restored\r\n");
}

/*
 * Power-cycle the eMMC VCC (VEMC LDO) via MT6357 PMIC.
 *
 * Oppo LK enables Power-On Write Protection (CMD28 with US_PWR_WP_EN set) on
 * the boot partition before jumping to MK.  Micron eMMC clears Power-On WP
 * only on an actual VCC removal, not on CMD0.  Cycling VEMC is the equivalent
 * of what the Linux mmc_power_cycle() → regulator_disable/enable path does.
 *
 * After this call the caller must issue a full eMMC re-init sequence (CMD1,
 * CMD2, CMD3, CMD7, CMD6 bus-width, SELECT_PARTITION) — exactly what
 * msdc0_go_idle_reinit() already does.
 */
static void emmc_vcc_cycle(void)
{
	uint16_t regv;

	if (pwrap_read16(MT6357_RG_LDO_VEMC_EN_ADDR, &regv) != 0) {
		uart_puts_all("[mk] msdc: vemc read failed, skip vcc cycle\r\n");
		return;
	}
	uart_puts_all("[mk] msdc: vemc pre=0x");
	uart_puthex64_all((uint64_t) regv);
	uart_puts_all("\r\n");

	/* Disable VEMC — eMMC VCC off. */
	regv &= (uint16_t) ~(1U << MT6357_RG_LDO_VEMC_EN_BIT);
	if (pwrap_write16(MT6357_RG_LDO_VEMC_EN_ADDR, regv) != 0) {
		uart_puts_all("[mk] msdc: vemc disable failed\r\n");
		return;
	}
	uart_puts_all("[mk] msdc: vemc off\r\n");

	/* Hold VCC low for 50 ms.  The VEMC output capacitor (up to ~100 µF)
	 * needs ~25 ms to discharge through the eMMC load.  Previously uncalibrated
	 * spin loops ran for 300-600 ms at the effective CPU/cache speed; 50 ms is
	 * sufficient for full de-powering with a conservative margin. */
	mk_delay_ms(50U);

	/* Re-enable VEMC. */
	regv |= (uint16_t) (1U << MT6357_RG_LDO_VEMC_EN_BIT);
	if (pwrap_write16(MT6357_RG_LDO_VEMC_EN_ADDR, regv) != 0) {
		uart_puts_all("[mk] msdc: vemc enable failed\r\n");
		return;
	}
	uart_puts_all("[mk] msdc: vemc on\r\n");

	/* Wait 50 ms for VEMC to stabilize and the eMMC to complete its
	 * internal power-on reset.  JESD84 requires CMD1 be issued within 1 s
	 * of CMD0, but the card's power-on reset takes 10-50 ms in practice. */
	mk_delay_ms(50U);

	uart_puts_all("[mk] msdc: vcc cycle done\r\n");
}

static void usb_phy_init(void)
{
	volatile uint8_t *sif = (volatile uint8_t *) (uintptr_t) MTK_USB_SIF_BASE;
	uint32_t efuse_intr_cal;
	uint32_t intr_cal_tuned;
	uint32_t v;

#define USBPHY_READ32(off) mmio_read32(sif, U2PHY_COM_BASE + (off))
#define USBPHY_WRITE32(off, val) mmio_write32(sif, U2PHY_COM_BASE + (off), (val))
#define USBPHY_SET32(off, mask) do { \
	v = USBPHY_READ32(off); \
	v |= (uint32_t) (mask); \
	USBPHY_WRITE32(off, v); \
} while (0)
#define USBPHY_CLR32(off, mask) do { \
	v = USBPHY_READ32(off); \
	v &= (uint32_t) ~(mask); \
	USBPHY_WRITE32(off, v); \
} while (0)

	/* usb_phy_recover(): wait 50 usec before touching the PHY. */
	for (volatile uint32_t spin = 0; spin < 3000U; spin++) {
		__asm__ volatile("");
	}

	if (g_usb_phy_saved.valid == 0U) {
		g_usb_phy_saved.acr1 = USBPHY_READ32(U3P_USBPHYACR1);
		g_usb_phy_saved.acr5 = USBPHY_READ32(U3P_USBPHYACR5);
		g_usb_phy_saved.acr6 = USBPHY_READ32(U3P_USBPHYACR6);
		g_usb_phy_saved.acr3 = USBPHY_READ32(0x01cU);
		g_usb_phy_saved.acr4 = USBPHY_READ32(U3P_U2PHYACR4);
		g_usb_phy_saved.dtm0 = USBPHY_READ32(U3P_U2PHYDTM0);
		g_usb_phy_saved.dtm1 = USBPHY_READ32(U3P_U2PHYDTM1);
		g_usb_phy_saved.bc12c = USBPHY_READ32(U3P_U2PHYBC12C);
		g_usb_phy_saved.valid = 1U;
	}

	/* Match downstream usb_phy_recover() register sequencing. */
	USBPHY_CLR32(0x1cU, (1U << 12)); /* clear PUPD_BIST_EN */
	USBPHY_CLR32(0x68U, (1U << 26)); /* force_uart_en = 0 */
	USBPHY_CLR32(0x6cU, (1U << 16)); /* RG_UART_EN = 0 */
	USBPHY_CLR32(0x20U, (1U << 9) | (1U << 8)); /* gpio ctl/mode = 0 */
	USBPHY_CLR32(0x68U, (1U << 18)); /* force_suspendm = 0 */
	USBPHY_CLR32(0x68U, (1U << 6) | (1U << 7)); /* dp/dm pulldown = 0 */
	USBPHY_CLR32(0x68U, (0x3U << 4)); /* xcversel = 0 */
	USBPHY_CLR32(0x68U, (1U << 2)); /* termsel = 0 */
	USBPHY_CLR32(0x68U, (0xfU << 10)); /* datain = 0 */
	USBPHY_CLR32(0x68U, (1U << 20) | (1U << 21) | (1U << 19) |
			     (1U << 17) | (1U << 23)); /* clear force bits */
	USBPHY_CLR32(0x18U, (1U << 23)); /* BC11_SW_EN = 0 */
	USBPHY_SET32(0x18U, (1U << 20)); /* OTG_VBUSCMP_EN = 1 */
	USBPHY_CLR32(0x18U, (0xffU << 24)); /* PHY_REV[7:0] = 0x40 */
	USBPHY_SET32(0x18U, (0x40U << 24));
	USBPHY_CLR32(0x80U, (1U << 0)); /* RG_CHGDT_EN = 0 */

	/* usb_phy_recover(): wait 800 usec before forcing device mode. */
	for (volatile uint32_t spin = 0; spin < 50000U; spin++) {
		__asm__ volatile("");
	}

	/* set_usb_phy_mode(PHY_DEV_ACTIVE) */
	USBPHY_CLR32(0x6cU, (0x10U << 0));
	USBPHY_SET32(0x6cU, (0x2fU << 0));
	USBPHY_SET32(0x6cU, (0x3fU << 8));
	USBPHY_CLR32(0x04U, (0x7U << 12)); /* VRT_VREF_SEL */
	USBPHY_SET32(0x04U, (MK_USB_PHY_DEVICE_VRT_REF << 12));
	USBPHY_CLR32(0x04U, (0x7U << 8)); /* TERM_VREF_SEL */
	USBPHY_SET32(0x04U, (MK_USB_PHY_DEVICE_TERM_REF << 8));
	USBPHY_CLR32(0x18U, (0x3U << 30)); /* PHY_REV6 */
	USBPHY_SET32(0x18U, (MK_USB_PHY_DEVICE_REV6_REF << 30));

	/* hs_slew_rate_cal fallback + discth max like downstream path. */
	USBPHY_CLR32(0x14U, (0x7U << 12));
	USBPHY_SET32(0x14U, (0x4U << 12));
	USBPHY_SET32(0x18U, (0xfU << 4));

	/*
	 * Match downstream usb_phy_recover() efuse flow:
	 * read devinfo word #107, use low 5 bits as RG_USB20_INTR_CAL,
	 * then apply +2 margin.
	 */
	efuse_intr_cal = raw_read32(MTK_DEVINFO_BASE + (MK_USB_PHY_INTR_CAL_EFUSE_WORD * 4U));
	efuse_intr_cal &= MK_USB_PHY_INTR_CAL_MASK;
	if (efuse_intr_cal != 0U) {
		intr_cal_tuned = efuse_intr_cal + 2U;
		if (intr_cal_tuned > MK_USB_PHY_INTR_CAL_MASK) {
			intr_cal_tuned = MK_USB_PHY_INTR_CAL_MASK;
		}
		USBPHY_CLR32(0x04U, (MK_USB_PHY_INTR_CAL_MASK << MK_USB_PHY_INTR_CAL_SHIFT));
		USBPHY_SET32(0x04U, (intr_cal_tuned << MK_USB_PHY_INTR_CAL_SHIFT));
		uart_puts_all("[mk] usb phy intr_cal efuse=0x");
		uart_puthex64_all(efuse_intr_cal);
		uart_puts_all(" tuned=0x");
		uart_puthex64_all(intr_cal_tuned);
		uart_puts_all("\r\n");
	}

	/* Readback: verify writes stuck (all-zero = SIF clock not enabled). */
	uart_puts_all("[mk] usb phy acr0=0x");
	uart_puthex64_all(mmio_read32(sif, U2PHY_COM_BASE + U3P_USBPHYACR0));
	uart_puts_all(" acr1=0x");
	uart_puthex64_all(mmio_read32(sif, U2PHY_COM_BASE + U3P_USBPHYACR1));
	uart_puts_all(" acr5=0x");
	uart_puthex64_all(USBPHY_READ32(U3P_USBPHYACR5));
	uart_puts_all(" acr6=0x");
	uart_puthex64_all(USBPHY_READ32(U3P_USBPHYACR6));
	uart_puts_all("\r\n");
	uart_puts_all("[mk] usb phy dtm0=0x");
	uart_puthex64_all(USBPHY_READ32(U3P_U2PHYDTM0));
	uart_puts_all(" dtm1=0x");
	uart_puthex64_all(USBPHY_READ32(U3P_U2PHYDTM1));
	uart_puts_all(" acr3=0x");
	uart_puthex64_all(USBPHY_READ32(0x01cU));
	uart_puts_all(" acr4=0x");
	uart_puthex64_all(USBPHY_READ32(U3P_U2PHYACR4));
	uart_puts_all("\r\n");

#undef USBPHY_READ32
#undef USBPHY_WRITE32
#undef USBPHY_SET32
#undef USBPHY_CLR32
}

static void usb_phy_restore(void)
{
	volatile uint8_t *sif = (volatile uint8_t *) (uintptr_t) MTK_USB_SIF_BASE;

	if (g_usb_phy_saved.valid == 0U) {
		return;
	}

	mmio_write32(sif, U2PHY_COM_BASE + U3P_USBPHYACR1, g_usb_phy_saved.acr1);
	mmio_write32(sif, U2PHY_COM_BASE + U3P_USBPHYACR5, g_usb_phy_saved.acr5);
	mmio_write32(sif, U2PHY_COM_BASE + U3P_USBPHYACR6, g_usb_phy_saved.acr6);
	mmio_write32(sif, U2PHY_COM_BASE + 0x01cU, g_usb_phy_saved.acr3);
	mmio_write32(sif, U2PHY_COM_BASE + U3P_U2PHYACR4, g_usb_phy_saved.acr4);
	mmio_write32(sif, U2PHY_COM_BASE + U3P_U2PHYDTM0, g_usb_phy_saved.dtm0);
	mmio_write32(sif, U2PHY_COM_BASE + U3P_U2PHYDTM1, g_usb_phy_saved.dtm1);
	mmio_write32(sif, U2PHY_COM_BASE + U3P_U2PHYBC12C, g_usb_phy_saved.bc12c);
	uart_puts_all("[mk] usb phy: restored\r\n");
}

#define MUSB_SWRST 0x74U
#define MUSB_SWRST_SWRST (1U << 1)
#define MUSB_SWRST_DISUSBRESET (1U << 0)
#define MUSB_SWRST_FRC_VBUSVALID (1U << 2)


static void usb_core_reset(volatile uint8_t *base)
{
	uint16_t swrst;

	mmio_write8(base, MUSB_INTRUSBE, 0);
	mmio_write16(base, MUSB_INTRTXE, 0);
	mmio_write16(base, MUSB_INTRRXE, 0);
	mmio_write16(base, MUSB_INTRTX, 0xffffU);
	mmio_write16(base, MUSB_INTRRX, 0xffffU);
	mmio_write8(base, MUSB_INTRUSB, 0xffU);

	swrst = mmio_read16(base, MUSB_SWRST);
	/*
	 * Match downstream musb_platform_reset() behavior:
	 * write DISUSBRESET|SWRST and leave it programmed.
	 */
	swrst |= (uint16_t) (MUSB_SWRST_DISUSBRESET |
			     MUSB_SWRST_SWRST);
	mmio_write16(base, MUSB_SWRST, swrst);
	/* SWRST is self-clearing on working boots; allow it to settle. */
	for (uint32_t spin = 0; spin < 120000U; spin++) {
		if ((mmio_read16(base, MUSB_SWRST) & MUSB_SWRST_SWRST) == 0U) {
			break;
		}
		__asm__ volatile("");
	}
}

static void usb_dump_state(const char *tag)
{
	volatile uint8_t *base = usb_regs();

	uart_puts_all("[mk] usb ");
	uart_puts_all(tag);
	uart_puts_all(" devctl=0x");
	uart_puthex64_all(mmio_read8(base, MUSB_DEVCTL));
	uart_puts_all(" power=0x");
	uart_puthex64_all(mmio_read8(base, MUSB_POWER));
	uart_puts_all(" cfg=0x");
	uart_puthex64_all(mmio_read8(base, 0x10U + MUSB_CONFIGDATA));
	uart_puts_all(" l1ints=0x");
	uart_puthex64_all(mmio_read32(base, MTK_USB_L1INTS));
	uart_puts_all(" l1intm=0x");
	uart_puthex64_all(mmio_read32(base, MTK_USB_L1INTM));
	uart_puts_all(" intrusb=0x");
	uart_puthex64_all(mmio_read8(base, MUSB_INTRUSB));
	uart_puts_all(" intrtx=0x");
	uart_puthex64_all(mmio_read16(base, MUSB_INTRTX));
	uart_puts_all(" intrusbe=0x");
	uart_puthex64_all(mmio_read8(base, MUSB_INTRUSBE));
	uart_puts_all(" intrtxe=0x");
	uart_puthex64_all(mmio_read16(base, MUSB_INTRTXE));
	uart_puts_all("\r\n");
}

uint8_t mk_stage0_mtk_usb_fastboot_downloading(void)
{
	return g_fastboot_download_active;
}

uint8_t mk_stage0_mtk_usb_fastboot_take_action(void)
{
	uint8_t action = g_fastboot_pending_action;

	g_fastboot_pending_action = MK_FASTBOOT_ACTION_NONE;
	return action;
}

const uint8_t *mk_stage0_mtk_usb_fastboot_download_buf(void)
{
	return g_fastboot_download_buf;
}

uint32_t mk_stage0_mtk_usb_fastboot_download_size(void)
{
	return g_fastboot_download_staged_size;
}

__attribute__((weak)) void mk_stage0_fastboot_action_immediate(uint8_t action)
{
	(void) action;
}

static void usb_clock_init(void)
{
	volatile uint8_t *top = (volatile uint8_t *) (uintptr_t) MTK_TOPCKGEN_BASE;
	volatile uint8_t *infra = (volatile uint8_t *) (uintptr_t) MTK_INFRACFG_AO_BASE;
	uint32_t mask;

	if (g_usb_clock_saved.valid == 0U) {
		g_usb_clock_saved.clk_cfg_5 = mmio_read32(top, MTK_CLK_CFG_5);
		g_usb_clock_saved.ifr2_sta = mmio_read32(infra, MTK_IFR2_STA);
		g_usb_clock_saved.valid = 1U;
	}

	mask = (1U << MTK_CLK_CFG_5_USB_TOP_SEL_BIT) |
	       (1U << MTK_CLK_CFG_5_USB_TOP_GATE_BIT);
	mmio_write32(top, MTK_CLK_CFG_5_CLR, mask);
	mmio_write32(top, MTK_CLK_CFG_5_SET, (1U << MTK_CLK_CFG_5_USB_TOP_SEL_BIT));
	mmio_write32(top, MTK_CLK_CFG_UPDATE, (1U << MTK_CLK_CFG_UPDATE_USB_TOP_SEL_BIT));

	mmio_write32(infra, MTK_IFR2_CLR, (1U << MTK_IFR_ICUSB_BIT));
	(void) mmio_read32(infra, MTK_IFR2_STA);
}

static void usb_clock_quiesce(void)
{
	volatile uint8_t *top = (volatile uint8_t *) (uintptr_t) MTK_TOPCKGEN_BASE;
	volatile uint8_t *infra = (volatile uint8_t *) (uintptr_t) MTK_INFRACFG_AO_BASE;

	/*
	 * Undo the clock enables done for stage0 fastboot. Linux should see a
	 * passive USB block and bring the clocks back up on its own terms.
	 */
	mmio_write32(top, MTK_CLK_CFG_5_SET, (1U << MTK_CLK_CFG_5_USB_TOP_GATE_BIT));
	mmio_write32(top, MTK_CLK_CFG_UPDATE, (1U << MTK_CLK_CFG_UPDATE_USB_TOP_SEL_BIT));
	mmio_write32(infra, MTK_IFR2_SET, (1U << MTK_IFR_ICUSB_BIT));
	(void) mmio_read32(infra, MTK_IFR2_STA);
}

static void usb_clock_restore(void)
{
	volatile uint8_t *top = (volatile uint8_t *) (uintptr_t) MTK_TOPCKGEN_BASE;
	volatile uint8_t *infra = (volatile uint8_t *) (uintptr_t) MTK_INFRACFG_AO_BASE;
	uint32_t sel = (1U << MTK_CLK_CFG_5_USB_TOP_SEL_BIT);
	uint32_t gate = (1U << MTK_CLK_CFG_5_USB_TOP_GATE_BIT);
	uint32_t icusb = (1U << MTK_IFR_ICUSB_BIT);

	if (g_usb_clock_saved.valid == 0U) {
		usb_clock_quiesce();
		return;
	}

	if ((g_usb_clock_saved.clk_cfg_5 & sel) != 0U) {
		mmio_write32(top, MTK_CLK_CFG_5_SET, sel);
	} else {
		mmio_write32(top, MTK_CLK_CFG_5_CLR, sel);
	}
	if ((g_usb_clock_saved.clk_cfg_5 & gate) != 0U) {
		mmio_write32(top, MTK_CLK_CFG_5_SET, gate);
	} else {
		mmio_write32(top, MTK_CLK_CFG_5_CLR, gate);
	}
	mmio_write32(top, MTK_CLK_CFG_UPDATE, (1U << MTK_CLK_CFG_UPDATE_USB_TOP_SEL_BIT));

	if ((g_usb_clock_saved.ifr2_sta & icusb) != 0U) {
		mmio_write32(infra, MTK_IFR2_SET, icusb);
	} else {
		mmio_write32(infra, MTK_IFR2_CLR, icusb);
	}
	(void) mmio_read32(infra, MTK_IFR2_STA);
	uart_puts_all("[mk] usb clock: restored\r\n");
}

static void ep_select(volatile uint8_t *base, uint8_t ep)
{
	mmio_write8(base, MUSB_INDEX, ep);
}

static void ep0_flush(volatile uint8_t *base)
{
	ep_select(base, 0);
	mmio_write16(base, MUSB_CSR0, MUSB_CSR0_FLUSHFIFO);
}

static void ep0_write_fifo(volatile uint8_t *base, const uint8_t *buf, uint16_t len)
{
	uint16_t i;

	for (i = 0; i < len; i++) {
		mmio_write8(base, MUSB_FIFO_EP0, buf[i]);
	}
}

static void ep0_wait_txpktrdy_clear(volatile uint8_t *base, const char *tag)
{
	uint32_t wait = 150000U;

	ep_select(base, 0);
	while (((mmio_read16(base, MUSB_CSR0) & MUSB_CSR0_TXPKTRDY) != 0U) && (wait-- != 0U)) {
		__asm__ volatile("");
	}
	if (wait == 0U) {
		uart_puts_all("[mk] usb ep0 ");
		uart_puts_all(tag);
		uart_puts_all(" txpktrdy timeout csr0=0x");
		uart_puthex64_all(mmio_read16(base, MUSB_CSR0));
		uart_puts_all("\r\n");
	}
}

static void ep0_read_fifo(volatile uint8_t *base, uint8_t *buf, uint16_t len)
{
	uint16_t i;

	for (i = 0; i < len; i++) {
		buf[i] = mmio_read8(base, MUSB_FIFO_EP0);
	}
}

static void ep0_send_data(volatile uint8_t *base, const uint8_t *buf, uint16_t len)
{
	ep_select(base, 0);
	ep0_write_fifo(base, buf, len);
	/* For IN data, push packet and mark end of control data stage. */
	mmio_write16(base, MUSB_CSR0, MUSB_CSR0_TXPKTRDY | MUSB_CSR0_P_DATAEND);
	ep0_wait_txpktrdy_clear(base, "send");
}

static void ep0_ack_no_data(volatile uint8_t *base)
{
	ep_select(base, 0);
	mmio_write16(base, MUSB_CSR0, MUSB_CSR0_P_SVDRXPKTRDY | MUSB_CSR0_P_DATAEND);
	ep0_wait_txpktrdy_clear(base, "ack");
}

static void ep0_stall(volatile uint8_t *base)
{
	ep_select(base, 0);
	mmio_write16(base, MUSB_CSR0, MUSB_CSR0_P_SVDRXPKTRDY | MUSB_CSR0_P_SENDSTALL);
	uart_puts_all("[mk] usb ep0 stall\r\n");
}

static uint8_t str_starts_with_lit(const char *s, const char *prefix)
{
	uint32_t i = 0U;

	while (prefix[i] != '\0') {
		if (s[i] != prefix[i]) {
			return 0U;
		}
		i++;
	}
	return 1U;
}

static uint8_t str_eq_lit(const char *s, const char *lit)
{
	uint32_t i = 0U;

	while (lit[i] != '\0' && s[i] != '\0') {
		if (lit[i] != s[i]) {
			return 0U;
		}
		i++;
	}
	return (lit[i] == '\0' && s[i] == '\0') ? 1U : 0U;
}

static uint8_t str_contains_lit(const char *s, const char *needle)
{
	uint32_t i = 0U;
	uint32_t j;

	if (s == 0 || needle == 0 || needle[0] == '\0') {
		return 0U;
	}
	while (s[i] != '\0') {
		j = 0U;
		while (needle[j] != '\0' && s[i + j] != '\0' && s[i + j] == needle[j]) {
			j++;
		}
		if (needle[j] == '\0') {
			return 1U;
		}
		i++;
	}
	return 0U;
}

static uint8_t ascii_len_bounded_char(const char *s, uint8_t max_len)
{
	uint8_t n = 0U;

	if (s == 0) {
		return 0U;
	}
	while ((n < max_len) && (s[n] != '\0')) {
		n++;
	}
	return n;
}

static uint8_t u64_to_hex_ascii(uint64_t value, char *out, uint8_t out_cap)
{
	char rev[16];
	uint8_t rev_len = 0U;
	uint8_t i;

	if (out == 0 || out_cap < 2U) {
		return 0U;
	}
	if (value == 0U) {
		out[0] = '0';
		out[1] = '\0';
		return 1U;
	}
	while (value != 0U && rev_len < (uint8_t) sizeof(rev)) {
		uint8_t nib = (uint8_t) (value & 0x0fU);
		rev[rev_len++] = (char) ((nib < 10U) ? ('0' + nib) : ('a' + (nib - 10U)));
		value >>= 4U;
	}
	if ((uint8_t) (rev_len + 1U) > out_cap) {
		return 0U;
	}
	for (i = 0U; i < rev_len; i++) {
		out[i] = rev[rev_len - 1U - i];
	}
	out[rev_len] = '\0';
	return rev_len;
}

static uint8_t parse_u64_ascii(const char *s, uint64_t *out)
{
	uint64_t value = 0U;
	uint32_t i = 0U;
	uint32_t base = 10U;

	if (s == 0 || out == 0) {
		return 0U;
	}
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		base = 16U;
		i = 2U;
	}
	if (s[i] == '\0') {
		return 0U;
	}

	while (s[i] != '\0') {
		uint8_t c = (uint8_t) s[i];
		uint32_t digit;

		if (c >= (uint8_t) '0' && c <= (uint8_t) '9') {
			digit = (uint32_t) (c - (uint8_t) '0');
		} else if (base == 16U && c >= (uint8_t) 'a' && c <= (uint8_t) 'f') {
			digit = (uint32_t) (10U + c - (uint8_t) 'a');
		} else if (base == 16U && c >= (uint8_t) 'A' && c <= (uint8_t) 'F') {
			digit = (uint32_t) (10U + c - (uint8_t) 'A');
		} else {
			return 0U;
		}
		if (digit >= base) {
			return 0U;
		}
		if (value > ((UINT64_MAX - (uint64_t) digit) / (uint64_t) base)) {
			return 0U;
		}
		value = (value * (uint64_t) base) + (uint64_t) digit;
		i++;
	}

	*out = value;
	return 1U;
}

static uint8_t parse_u32_hex_ascii(const char *s, uint32_t *out)
{
	uint64_t value = 0U;
	uint32_t i = 0U;

	if (s == 0 || out == 0) {
		return 0U;
	}
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		i = 2U;
	}
	if (s[i] == '\0') {
		return 0U;
	}

	while (s[i] != '\0') {
		uint8_t c = (uint8_t) s[i];
		uint32_t digit;

		if (c >= (uint8_t) '0' && c <= (uint8_t) '9') {
			digit = (uint32_t) (c - (uint8_t) '0');
		} else if (c >= (uint8_t) 'a' && c <= (uint8_t) 'f') {
			digit = (uint32_t) (10U + c - (uint8_t) 'a');
		} else if (c >= (uint8_t) 'A' && c <= (uint8_t) 'F') {
			digit = (uint32_t) (10U + c - (uint8_t) 'A');
		} else {
			return 0U;
		}
		if (value > ((UINT32_MAX - (uint64_t) digit) >> 4U)) {
			return 0U;
		}
		value = (value << 4U) | (uint64_t) digit;
		i++;
	}

	*out = (uint32_t) value;
	return 1U;
}

static const uint8_t g_usb_device_desc[] = {
	18, USB_DT_DEVICE,
	0x00, 0x02,
	0xff, 0x42, 0x03,
	64,
	(uint8_t) (USB_VID_GOOGLE & 0xffU), (uint8_t) (USB_VID_GOOGLE >> 8),
	(uint8_t) (USB_PID_FASTBOOT & 0xffU), (uint8_t) (USB_PID_FASTBOOT >> 8),
	0x19, 0x04,
	1, 2, 3,
	1
};

static const uint8_t g_usb_qualifier_desc[] = {
	10, USB_DT_DEVICE_QUALIFIER,
	0x00, 0x02,
	0xff, 0x42, 0x03,
	64,
	1,
	0
};

static const uint8_t g_usb_config_desc_hs[] = {
	9, USB_DT_CONFIG,
	32, 0,
	1,
	1,
	0,
	0x80,
	50,
	9, 4,
	0, 0, 2,
	USB_DESC_TYPE_FASTBOOT_CLASS, USB_DESC_SUBCLASS_FASTBOOT, USB_DESC_PROTOCOL_FASTBOOT,
	0,
	7, 5,
	0x81, 0x02, 0x00, 0x02, 0,
	7, 5,
	0x01, 0x02, 0x00, 0x02, 0
};

static const uint8_t g_usb_config_desc_fs[] = {
	9, USB_DT_CONFIG,
	32, 0,
	1,
	1,
	0,
	0x80,
	50,
	9, 4,
	0, 0, 2,
	USB_DESC_TYPE_FASTBOOT_CLASS, USB_DESC_SUBCLASS_FASTBOOT, USB_DESC_PROTOCOL_FASTBOOT,
	0,
	7, 5,
	0x81, 0x02, 64, 0, 0,
	7, 5,
	0x01, 0x02, 64, 0, 0
};

static const uint8_t g_usb_other_speed_desc_hs[] = {
	9, USB_DT_OTHER_SPEED_CONFIG,
	32, 0,
	1,
	1,
	0,
	0x80,
	50,
	9, 4,
	0, 0, 2,
	USB_DESC_TYPE_FASTBOOT_CLASS, USB_DESC_SUBCLASS_FASTBOOT, USB_DESC_PROTOCOL_FASTBOOT,
	0,
	7, 5,
	0x81, 0x02, 0x00, 0x02, 0,
	7, 5,
	0x01, 0x02, 0x00, 0x02, 0
};

static const uint8_t g_usb_other_speed_desc_fs[] = {
	9, USB_DT_OTHER_SPEED_CONFIG,
	32, 0,
	1,
	1,
	0,
	0x80,
	50,
	9, 4,
	0, 0, 2,
	USB_DESC_TYPE_FASTBOOT_CLASS, USB_DESC_SUBCLASS_FASTBOOT, USB_DESC_PROTOCOL_FASTBOOT,
	0,
	7, 5,
	0x81, 0x02, 64, 0, 0,
	7, 5,
	0x01, 0x02, 64, 0, 0
};

static const uint8_t g_usb_string_lang[] = {4, USB_DT_STRING, 0x09, 0x04};
static const uint8_t g_usb_mfr_ascii[] = "OPPO";
static const uint8_t g_usb_prod_ascii[] = "MinKernel";

static uint8_t ascii_len_bounded(const uint8_t *s, uint8_t max_len)
{
	uint8_t n = 0U;

	if (s == 0) {
		return 0U;
	}
	while ((n < max_len) && (s[n] != 0U)) {
		n++;
	}
	return n;
}

static uint8_t ascii_char_allowed(uint8_t c)
{
	if (c >= (uint8_t) '0' && c <= (uint8_t) '9') {
		return 1U;
	}
	if (c >= (uint8_t) 'A' && c <= (uint8_t) 'Z') {
		return 1U;
	}
	if (c >= (uint8_t) 'a' && c <= (uint8_t) 'z') {
		return 1U;
	}
	if (c == (uint8_t) '-' || c == (uint8_t) '_' || c == (uint8_t) '.') {
		return 1U;
	}
	return 0U;
}

static void build_string_desc_ascii(const uint8_t *ascii, uint8_t ascii_len,
				      const uint8_t **out_desc, uint16_t *out_len)
{
	uint8_t i;

	if (ascii_len > MK_USB_STRING_ASCII_MAX) {
		ascii_len = MK_USB_STRING_ASCII_MAX;
	}
	g_usb_string_desc_buf[0] = (uint8_t) (2U + ((uint16_t) ascii_len * 2U));
	g_usb_string_desc_buf[1] = USB_DT_STRING;
	for (i = 0U; i < ascii_len; i++) {
		g_usb_string_desc_buf[2U + (i * 2U)] = ascii[i];
		g_usb_string_desc_buf[3U + (i * 2U)] = 0U;
	}
	*out_desc = g_usb_string_desc_buf;
	*out_len = g_usb_string_desc_buf[0];
}

void mk_stage0_mtk_usb_set_serial_ascii(const char *serial)
{
	uint8_t i;
	uint8_t out_len = 0U;

	if (serial == 0) {
		return;
	}
	for (i = 0U; (i < MK_USB_STRING_ASCII_MAX) && (serial[i] != '\0'); i++) {
		uint8_t c = (uint8_t) serial[i];

		if (ascii_char_allowed(c) == 0U) {
			break;
		}
		g_usb_serial_ascii[out_len++] = c;
	}
	if (out_len == 0U) {
		return;
	}
	g_usb_serial_ascii[out_len] = 0U;
	g_usb_serial_ascii_len = out_len;
}

void mk_stage0_mtk_usb_set_lk_bootargs(const char *bootargs)
{
	uint32_t i;

	if (bootargs == 0) {
		return;
	}
	for (i = 0U; i < (sizeof(g_lk_bootargs) - 1U) && bootargs[i] != '\0'; i++) {
		g_lk_bootargs[i] = bootargs[i];
	}
	g_lk_bootargs[i] = '\0';
	g_lk_bootargs_len = i;
}

const char *mk_stage0_mtk_usb_get_lk_bootargs(void)
{
	return (g_lk_bootargs_len != 0U) ? g_lk_bootargs : 0;
}

static void handle_get_descriptor(volatile uint8_t *base, uint16_t value, uint16_t length)
{
	const uint8_t *desc = 0;
	uint16_t desc_len = 0;
	uint8_t type = (uint8_t) (value >> 8);
	uint8_t index = (uint8_t) value;
	uint8_t hs_mode = (mmio_read8(base, MUSB_POWER) & MUSB_POWER_HSMODE) != 0U;

	switch (type) {
	case USB_DT_DEVICE:
		desc = g_usb_device_desc;
		desc_len = sizeof(g_usb_device_desc);
		break;
	case USB_DT_CONFIG:
		desc = hs_mode ? g_usb_config_desc_hs : g_usb_config_desc_fs;
		desc_len = hs_mode ? sizeof(g_usb_config_desc_hs) : sizeof(g_usb_config_desc_fs);
		break;
	case USB_DT_OTHER_SPEED_CONFIG:
		desc = hs_mode ? g_usb_other_speed_desc_fs : g_usb_other_speed_desc_hs;
		desc_len = hs_mode ? sizeof(g_usb_other_speed_desc_fs) : sizeof(g_usb_other_speed_desc_hs);
		break;
	case USB_DT_STRING:
		if (index == 0U) {
			desc = g_usb_string_lang;
			desc_len = sizeof(g_usb_string_lang);
		} else if (index == 1U) {
			build_string_desc_ascii(g_usb_mfr_ascii,
					      ascii_len_bounded(g_usb_mfr_ascii, MK_USB_STRING_ASCII_MAX),
					      &desc, &desc_len);
		} else if (index == 2U) {
			build_string_desc_ascii(g_usb_prod_ascii,
					      ascii_len_bounded(g_usb_prod_ascii, MK_USB_STRING_ASCII_MAX),
					      &desc, &desc_len);
		} else if (index == 3U) {
			build_string_desc_ascii(g_usb_serial_ascii, g_usb_serial_ascii_len, &desc, &desc_len);
		}
		break;
	case USB_DT_DEVICE_QUALIFIER:
		desc = g_usb_qualifier_desc;
		desc_len = sizeof(g_usb_qualifier_desc);
		break;
	default:
		break;
	}

	if (desc == 0) {
		uart_puts_all("[mk] usb desc miss type=0x");
		uart_puthex64_all(type);
		uart_puts_all(" idx=0x");
		uart_puthex64_all(index);
		uart_puts_all("\r\n");
		ep0_stall(base);
		return;
	}
	if (length < desc_len) {
		desc_len = length;
	}
	uart_puts_all("[mk] usb desc type=0x");
	uart_puthex64_all(type);
	uart_puts_all(" idx=0x");
	uart_puthex64_all(index);
	uart_puts_all(" len=0x");
	uart_puthex64_all(desc_len);
	if (type == USB_DT_STRING) {
		uart_puts_all(" bytes=");
		uart_puthex64_all((desc_len > 0U) ? desc[0] : 0U);
		uart_puts_all(",");
		uart_puthex64_all((desc_len > 1U) ? desc[1] : 0U);
		uart_puts_all(",");
		uart_puthex64_all((desc_len > 2U) ? desc[2] : 0U);
		uart_puts_all(",");
		uart_puthex64_all((desc_len > 3U) ? desc[3] : 0U);
	}
	uart_puts_all("\r\n");
	ep0_send_data(base, desc, desc_len);
}

static void handle_setup_packet(volatile uint8_t *base, const usb_setup_packet_t *pkt)
{
	uint8_t tmp[2];

	uart_puts_all("[mk] usb setup bm=0x");
	uart_puthex64_all(pkt->bm_request_type);
	uart_puts_all(" req=0x");
	uart_puthex64_all(pkt->b_request);
	uart_puts_all(" wValue=0x");
	uart_puthex64_all(pkt->w_value);
	uart_puts_all(" wIndex=0x");
	uart_puthex64_all(pkt->w_index);
	uart_puts_all(" wLen=0x");
	uart_puthex64_all(pkt->w_length);
	uart_puts_all("\r\n");

	switch (pkt->b_request) {
	case USB_REQ_GET_DESCRIPTOR:
		if ((pkt->bm_request_type & 0x80U) == 0U) {
			ep0_stall(base);
			return;
		}
		handle_get_descriptor(base, pkt->w_value, pkt->w_length);
		return;
	case USB_REQ_SET_ADDRESS:
		g_usb_state.pending_address = (uint8_t) (pkt->w_value & 0x7fU);
		g_usb_state.address_pending = 1U;
		uart_puts_all("[mk] usb setaddr pending=0x");
		uart_puthex64_all(g_usb_state.pending_address);
		uart_puts_all("\r\n");
		ep0_ack_no_data(base);
		return;
	case USB_REQ_GET_CONFIGURATION:
		if ((pkt->bm_request_type & 0x80U) == 0U) {
			ep0_stall(base);
			return;
		}
		tmp[0] = g_usb_state.configured;
		ep0_send_data(base, tmp, 1);
		return;
	case USB_REQ_SET_CONFIGURATION:
		g_usb_state.configured = (uint8_t) pkt->w_value;
		ep0_ack_no_data(base);
		return;
	case USB_REQ_GET_STATUS:
		if ((pkt->bm_request_type & 0x80U) == 0U) {
			ep0_stall(base);
			return;
		}
		tmp[0] = 0;
		tmp[1] = 0;
		ep0_send_data(base, tmp, 2);
		return;
	case USB_REQ_GET_INTERFACE:
		if ((pkt->bm_request_type & 0x80U) == 0U) {
			ep0_stall(base);
			return;
		}
		tmp[0] = 0;
		ep0_send_data(base, tmp, 1);
		return;
	case USB_REQ_SET_INTERFACE:
		ep0_ack_no_data(base);
		return;
	default:
		break;
	}

	ep0_stall(base);
}

static void ep1_write_fifo(volatile uint8_t *base, const uint8_t *buf, uint16_t len)
{
	uint16_t i;

	for (i = 0; i < len; i++) {
		mmio_write8(base, MUSB_FIFO_EP1, buf[i]);
	}
}

static void ep1_read_fifo(volatile uint8_t *base, uint8_t *buf, uint16_t len)
{
	uint16_t i;

	for (i = 0; i < len; i++) {
		buf[i] = mmio_read8(base, MUSB_FIFO_EP1);
	}
}

static void ep1_read_fifo_fast(volatile uint8_t *base, uint8_t *buf, uint16_t len)
{
	uint16_t i = 0U;

	while ((uint16_t) (len - i) >= 4U) {
		uint32_t v = mmio_read32(base, MUSB_FIFO_EP1);
		buf[i + 0U] = (uint8_t) (v & 0xffU);
		buf[i + 1U] = (uint8_t) ((v >> 8) & 0xffU);
		buf[i + 2U] = (uint8_t) ((v >> 16) & 0xffU);
		buf[i + 3U] = (uint8_t) ((v >> 24) & 0xffU);
		i = (uint16_t) (i + 4U);
	}
	while (i < len) {
		buf[i++] = mmio_read8(base, MUSB_FIFO_EP1);
	}
}

static void ep1_discard_fifo_fast(volatile uint8_t *base, uint16_t len)
{
	while (len >= 4U) {
		(void) mmio_read32(base, MUSB_FIFO_EP1);
		len = (uint16_t) (len - 4U);
	}
	while (len != 0U) {
		(void) mmio_read8(base, MUSB_FIFO_EP1);
		len--;
	}
}

static int ep1_wait_tx_ready(volatile uint8_t *base)
{
	uint32_t wait = 150000U;
	uint32_t iter = 0U;

	ep_select(base, 1U);
	while (((mmio_read16(base, MUSB_TXCSR) & MUSB_TXCSR_TXPKTRDY) != 0U) && (wait-- != 0U)) {
		if ((iter++ & 0x3ffU) == 0U) {
			mk_stage0_storage_pet_wdt();
		}
		__asm__ volatile("");
	}
	if (wait == 0U) {
		uart_puts_all("[mk] usb ep1 tx timeout csr=0x");
		uart_puthex64_all(mmio_read16(base, MUSB_TXCSR));
		uart_puts_all("\r\n");
		return -1;
	}
	return 0;
}

static void fastboot_send_raw(volatile uint8_t *base, const uint8_t *buf, uint16_t len)
{
	uint8_t trace = g_fastboot_fetch_trace_packets;
	uint32_t seq = g_fastboot_fetch_trace_seq;

	if (buf == 0 || len == 0U) {
		return;
	}
	if (trace != 0U) {
		uart_puts_all("[mk] raw seq=0x");
		uart_puthex64_all(seq);
		uart_puts_all(" enter len=0x");
		uart_puthex64_all(len);
		uart_puts_all("\r\n");
	}
	if (ep1_wait_tx_ready(base) != 0) {
		uart_puts_all("[mk] fastboot raw drop: ep1 not ready\r\n");
		return;
	}
	if (trace != 0U) {
		uart_puts_all("[mk] raw seq=0x");
		uart_puthex64_all(seq);
		uart_puts_all(" txready\r\n");
	}
	ep_select(base, 1U);
	ep1_write_fifo(base, buf, len);
	if (trace != 0U) {
		uart_puts_all("[mk] raw seq=0x");
		uart_puthex64_all(seq);
		uart_puts_all(" fifo\r\n");
	}
	mmio_write16(base, MUSB_TXCSR, MUSB_TXCSR_TXPKTRDY);
	if (trace != 0U) {
		uart_puts_all("[mk] raw seq=0x");
		uart_puthex64_all(seq);
		uart_puts_all(" txpktrdy\r\n");
		g_fastboot_fetch_trace_seq = seq + 1U;
		g_fastboot_fetch_trace_packets = (uint8_t) (trace - 1U);
	}
}

static void fastboot_send_response(volatile uint8_t *base, const char *status4, const char *msg)
{
	uint16_t pos = 0U;
	uint8_t i;

	if (status4 == 0) {
		return;
	}
	for (i = 0U; i < 4U && status4[i] != '\0'; i++) {
		g_fastboot_resp_buf[pos++] = (uint8_t) status4[i];
	}
	if (msg != 0) {
		uint8_t n = ascii_len_bounded_char(msg, (uint8_t) (sizeof(g_fastboot_resp_buf) - pos));
		for (i = 0U; i < n; i++) {
			g_fastboot_resp_buf[pos++] = (uint8_t) msg[i];
		}
	}
	uart_puts_all("[mk] fastboot resp len=0x");
	uart_puthex64_all(pos);
	uart_puts_all("\r\n");
	if (ep1_wait_tx_ready(base) != 0) {
		uart_puts_all("[mk] fastboot resp drop: ep1 not ready\r\n");
		return;
	}
	ep_select(base, 1U);
	ep1_write_fifo(base, g_fastboot_resp_buf, pos);
	mmio_write16(base, MUSB_TXCSR, MUSB_TXCSR_TXPKTRDY);
	uart_puts_all("[mk] fastboot resp queued csr=0x");
	uart_puthex64_all(mmio_read16(base, MUSB_TXCSR));
	uart_puts_all("\r\n");
}

static uint8_t hex_lower_nibble(uint8_t v)
{
	v &= 0x0fU;
	return (uint8_t) ((v < 10U) ? ((uint8_t) '0' + v) : ((uint8_t) 'a' + (v - 10U)));
}

static void fastboot_send_okay_u32_hex(volatile uint8_t *base, uint32_t value)
{
	uint8_t rsp[14];
	uint32_t shift;
	uint32_t i = 0U;

	rsp[i++] = (uint8_t) 'O';
	rsp[i++] = (uint8_t) 'K';
	rsp[i++] = (uint8_t) 'A';
	rsp[i++] = (uint8_t) 'Y';
	rsp[i++] = (uint8_t) '0';
	rsp[i++] = (uint8_t) 'x';
	for (shift = 28U; shift <= 28U; shift -= 4U) {
		rsp[i++] = hex_lower_nibble((uint8_t) ((value >> shift) & 0x0fU));
		if (shift == 0U) {
			break;
		}
	}
	fastboot_send_raw(base, rsp, (uint16_t) i);
}

static void fastboot_send_data_header(volatile uint8_t *base, uint32_t data_len)
{
	uint8_t hdr[12];
	uint32_t shift;
	uint32_t i = 0U;

	hdr[i++] = (uint8_t) 'D';
	hdr[i++] = (uint8_t) 'A';
	hdr[i++] = (uint8_t) 'T';
	hdr[i++] = (uint8_t) 'A';
	for (shift = 28U; shift <= 28U; shift -= 4U) {
		hdr[i++] = hex_lower_nibble((uint8_t) ((data_len >> shift) & 0x0fU));
		if (shift == 0U) {
			break;
		}
	}
	fastboot_send_raw(base, hdr, 12U);
}

static void fastboot_send_bulk(volatile uint8_t *base, const uint8_t *buf, uint32_t len)
{
	uint32_t sent = 0U;

	while (sent < len) {
		uint16_t chunk = (uint16_t) (len - sent);

		if (chunk > 512U) {
			chunk = 512U;
		}
		fastboot_send_raw(base, buf + sent, chunk);
		sent += chunk;
	}
}

static void fastboot_fetch_progress_pump(volatile uint8_t *base)
{
	uint32_t spins;

	for (spins = 0U; spins < 4U; spins++) {
		(void) base;
		mk_stage0_storage_pet_wdt();
		mk_stage0_mtk_usb_fastboot_poll();
	}
}

static void fastboot_fail_msg(volatile uint8_t *base, const char *msg)
{
	fastboot_send_response(base, "FAIL", msg);
}

static uint16_t rd_le16(const uint8_t *p)
{
	return (uint16_t) ((uint16_t) p[0] | ((uint16_t) p[1] << 8));
}

static uint32_t rd_le32(const uint8_t *p)
{
	return (uint32_t) p[0] |
	    ((uint32_t) p[1] << 8) |
	    ((uint32_t) p[2] << 16) |
	    ((uint32_t) p[3] << 24);
}

static void wr_le32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t) (v & 0xffU);
	p[1] = (uint8_t) ((v >> 8) & 0xffU);
	p[2] = (uint8_t) ((v >> 16) & 0xffU);
	p[3] = (uint8_t) ((v >> 24) & 0xffU);
}

static int fastboot_flash_sparse(volatile uint8_t *base,
	uint64_t part_lba, uint64_t part_sector_count)
{
	const uint8_t *buf = g_fastboot_download_buf;
	uint32_t buf_sz = g_fastboot_download_staged_size;
	uint16_t file_hdr_sz;
	uint16_t chunk_hdr_sz;
	uint32_t blk_sz;
	uint32_t total_blks;
	uint32_t total_chunks;
	const uint8_t *p;
	uint64_t cur_lba = part_lba;
	uint32_t ci;

	if (buf_sz < 28U) {
		fastboot_fail_msg(base, "sparse: short header");
		return 0;
	}
	file_hdr_sz = rd_le16(buf + 8U);
	chunk_hdr_sz = rd_le16(buf + 10U);
	blk_sz = rd_le32(buf + 12U);
	total_blks = rd_le32(buf + 16U);
	total_chunks = rd_le32(buf + 20U);
	if (file_hdr_sz < 28U || file_hdr_sz > buf_sz ||
	    chunk_hdr_sz < 12U || blk_sz == 0U || (blk_sz % 512U) != 0U) {
		fastboot_fail_msg(base, "sparse: bad header");
		return 0;
	}
	if (((uint64_t) total_blks * (uint64_t) blk_sz) >
	    (part_sector_count * 512U)) {
		fastboot_fail_msg(base, "image too large");
		return 0;
	}

	p = buf + file_hdr_sz;
	for (ci = 0U; ci < total_chunks; ci++) {
		uint16_t ctype;
		uint32_t chunk_sz;
		uint32_t total_sz;
		uint64_t out_sectors;
		const uint8_t *data;
		uint32_t ctype_u32;

		if (((uint32_t) (p - buf) + (uint32_t) chunk_hdr_sz) > buf_sz) {
			fastboot_fail_msg(base, "sparse: truncated");
			return 0;
		}
		ctype = rd_le16(p);
		chunk_sz = rd_le32(p + 4U);
		total_sz = rd_le32(p + 8U);
		if (total_sz < chunk_hdr_sz ||
		    ((uint32_t) (p - buf) + total_sz) > buf_sz) {
			fastboot_fail_msg(base, "sparse: bad chunk");
			return 0;
		}
		data = p + chunk_hdr_sz;
		out_sectors = ((uint64_t) chunk_sz * (uint64_t) blk_sz) / 512U;
		ctype_u32 = (uint32_t) ctype;
		uart_puts_all("[mk] sparse chunk idx=0x");
		uart_puthex64_all(ci);
		uart_puts_all(" type=0x");
		uart_puthex64_all(ctype_u32);
		uart_puts_all(" sectors=0x");
		uart_puthex64_all(out_sectors);
		uart_puts_all("\r\n");
		if ((cur_lba + out_sectors) > (part_lba + part_sector_count)) {
			fastboot_fail_msg(base, "sparse: overflows partition");
			return 0;
		}

		if (ctype == CHUNK_TYPE_RAW) {
			uint32_t data_sz = total_sz - chunk_hdr_sz;
			uint32_t sector_count = (uint32_t) out_sectors;

			if (data_sz != ((uint32_t) chunk_sz * blk_sz)) {
				fastboot_fail_msg(base, "sparse: bad raw size");
				return 0;
			}
			mk_stage0_storage_pet_wdt();
			if (sector_count != 0U &&
			    mk_stage0_storage_write_sectors(cur_lba, data, sector_count) == 0) {
				fastboot_fail_msg(base, "sparse: write failed");
				return 0;
			}
			cur_lba += out_sectors;
		} else if (ctype == CHUNK_TYPE_FILL) {
			uint32_t fill;
			uint32_t i;
			uint64_t s;

			if ((total_sz - chunk_hdr_sz) != 4U) {
				fastboot_fail_msg(base, "sparse: bad fill");
				return 0;
			}
			fill = rd_le32(data);
			uart_puts_all("[mk] sparse FILL val=0x");
			uart_puthex64_all(fill);
			uart_puts_all(" sectors=0x");
			uart_puthex64_all(out_sectors);
			uart_puts_all("\r\n");

			if (fill == 0U) {
				/*
				 * Optimization: skip zero-fill chunks. The sparse format
				 * asks us to materialize zeros, but for this userdata image
				 * the large zero-fill regions are unallocated ext4 space.
				 * RAW chunks still carry allocated metadata and real data.
				 */
				cur_lba += out_sectors;
			} else {
				for (i = 0U; i < ((512U * 256U) / 4U); i++) {
					wr_le32(g_sparse_fill_buf + (i * 4U), fill);
				}
				for (s = 0U; s < out_sectors;) {
					uint32_t batch = (uint32_t) (out_sectors - s);

					if (batch > 256U) {
						batch = 256U;
					}
					mk_stage0_storage_pet_wdt();
					if (mk_stage0_storage_write_sectors(cur_lba + s,
						g_sparse_fill_buf, batch) == 0) {
						fastboot_fail_msg(base, "sparse: fill write failed");
						return 0;
					}
					s += (uint64_t) batch;
				}
				cur_lba += out_sectors;
			}
		} else if (ctype == CHUNK_TYPE_DONT) {
			if (total_sz != chunk_hdr_sz) {
				fastboot_fail_msg(base, "sparse: bad dontcare");
				return 0;
			}
			cur_lba += out_sectors;
		} else if (ctype == CHUNK_TYPE_CRC32) {
			if ((total_sz - chunk_hdr_sz) != 4U) {
				fastboot_fail_msg(base, "sparse: bad crc32");
				return 0;
			}
		} else {
			fastboot_fail_msg(base, "sparse: unknown chunk");
			return 0;
		}

		p += total_sz;
	}

	return 1;
}

static void fastboot_maybe_apply_action_now(volatile uint8_t *base, uint8_t action)
{
	uint32_t wait = 300000U;

	if (action == MK_FASTBOOT_ACTION_NONE) {
		return;
	}

	/*
	 * Give EP1 IN some time to drain so host receives the OKAY before reset.
	 * If it does not drain in time, still continue with reboot.
	 */
	ep_select(base, 1U);
	while (((mmio_read16(base, MUSB_TXCSR) & MUSB_TXCSR_TXPKTRDY) != 0U) && (wait-- != 0U)) {
		__asm__ volatile("");
	}
	mk_stage0_fastboot_action_immediate(action);
}

static void fastboot_handle_fetch_command(volatile uint8_t *base, const char *arg)
{
	char label[MK_FASTBOOT_CMD_MAX + 1U];
	char numbuf[32];
	uint32_t label_len = 0U;
	uint64_t part_lba = 0U;
	uint64_t part_count = 0U;
	uint64_t part_size_bytes;
	uint64_t offset = 0U;
	uint64_t size = 0U;
	uint64_t remain;
	uint64_t lba;
	uint32_t in_sector;
	uint32_t send_chunk;
	uint32_t part_chunk_index = 0U;
	const char *p = arg;
	uint32_t i;

	if (arg == 0 || arg[0] == '\0') {
		fastboot_fail_msg(base, "missing partition");
		return;
	}

	while (p[label_len] != '\0' && p[label_len] != ':') {
		label_len++;
	}
	if (label_len == 0U || label_len > MK_FASTBOOT_CMD_MAX) {
		fastboot_fail_msg(base, "bad partition");
		return;
	}
	for (i = 0U; i < label_len; i++) {
		label[i] = p[i];
	}
	label[label_len] = '\0';
	p += label_len;

	if (*p == ':') {
		uint32_t n = 0U;

		p++;
		while (p[n] != '\0' && p[n] != ':') {
			if (n >= sizeof(numbuf) - 1U) {
				fastboot_fail_msg(base, "bad offset");
				return;
			}
			numbuf[n] = p[n];
			n++;
		}
		numbuf[n] = '\0';
		if (parse_u64_ascii(numbuf, &offset) == 0U) {
			fastboot_fail_msg(base, "bad offset");
			return;
		}
		p += n;
		if (*p == ':') {
			n = 0U;
			p++;
			while (p[n] != '\0') {
				if (n >= sizeof(numbuf) - 1U) {
					fastboot_fail_msg(base, "bad size");
					return;
				}
				numbuf[n] = p[n];
				n++;
			}
			numbuf[n] = '\0';
			if (parse_u64_ascii(numbuf, &size) == 0U) {
				fastboot_fail_msg(base, "bad size");
				return;
			}
			p += n;
		}
	}
	if (*p != '\0') {
		fastboot_fail_msg(base, "bad fetch args");
		return;
	}

	if (str_eq_lit(label, "peacock-zImage") != 0U ||
	    str_eq_lit(label, "peacock-initramfs") != 0U) {
		mk_ext2_t ext2;
		uint64_t boot_lba = 0U;
		uint64_t boot_count = 0U;
		uint64_t file_bytes;
		uint64_t send_size;
		uint64_t sent_total = 0U;
		uint64_t next_progress = 1024U * 1024U;
		const char *path = (str_eq_lit(label, "peacock-zImage") != 0U)
			? "/zImage" : "/initramfs.cpio.gz";
		int file_size;
		const uint8_t *src;
		uint32_t chunk_index = 0U;

		if (mk_stage0_storage_prepare() == 0) {
			fastboot_fail_msg(base, "storage unavailable");
			return;
		}
		if (!mk_stage0_storage_find_partition_within("userdata", "boot", &boot_lba, &boot_count)) {
			fastboot_fail_msg(base, "peacock boot not found");
			return;
		}
		if (!mk_ext2_open(boot_lba, &ext2)) {
			fastboot_fail_msg(base, "peacock boot fs bad");
			return;
		}
		file_size = mk_ext2_load_file(&ext2, path, g_fastboot_download_buf, MK_FASTBOOT_DOWNLOAD_MAX);
		if (file_size < 0) {
			fastboot_fail_msg(base, "peacock file missing");
			return;
		}
		file_bytes = (uint64_t) file_size;
		if (offset > file_bytes) {
			fastboot_fail_msg(base, "offset too large");
			return;
		}
		remain = file_bytes - offset;
		send_size = (size == 0U) ? remain : size;
		if (send_size == 0U) {
			fastboot_fail_msg(base, "empty fetch");
			return;
		}
		if (file_bytes > 0xffffffffU) {
			fastboot_fail_msg(base, "fetch >4g unsupported");
			return;
		}
		if (send_size > remain) {
			fastboot_fail_msg(base, "size too large");
			return;
		}
		src = g_fastboot_download_buf + (uint32_t) offset;

		uart_puts_all("[mk] fastboot fetch file=");
		uart_puts_all(label);
		uart_puts_all(" offset=0x");
		uart_puthex64_all(offset);
		uart_puts_all(" size=0x");
		uart_puthex64_all(send_size);
		uart_puts_all("\r\n");

		uart_puts_all("[mk] fetch: before DATA\r\n");
		fastboot_send_data_header(base, (uint32_t) send_size);
		uart_puts_all("[mk] fetch: after DATA\r\n");
		g_fastboot_fetch_trace_packets = 4U;
		g_fastboot_fetch_trace_seq = 0U;
		while (send_size != 0U) {
			send_chunk = (uint32_t) send_size;
			if (send_chunk > (4U * 1024U)) {
				send_chunk = 4U * 1024U;
			}
			if (sent_total == 0U) {
				uint32_t first_packet = send_chunk;

				if (first_packet > 512U) {
					first_packet = 512U;
				}
				uart_puts_all("[mk] fetch: before first bulk size=0x");
				uart_puthex64_all(send_chunk);
				uart_puts_all("\r\n");
				uart_puts_all("[mk] fetch: before first raw size=0x");
				uart_puthex64_all(first_packet);
				uart_puts_all("\r\n");
				fastboot_send_raw(base, src, (uint16_t) first_packet);
				uart_puts_all("[mk] fetch: after first raw\r\n");
				src += first_packet;
				send_size -= (uint64_t) first_packet;
				sent_total += (uint64_t) first_packet;
				send_chunk -= first_packet;
				if (send_chunk == 0U) {
					uart_puts_all("[mk] fetch: first bulk done\r\n");
					fastboot_fetch_progress_pump(base);
					continue;
				}
			}
			uart_puts_all("[mk] fetch: chunk begin idx=0x");
			uart_puthex64_all(chunk_index);
			uart_puts_all(" size=0x");
			uart_puthex64_all(send_chunk);
			uart_puts_all("\r\n");
			fastboot_send_bulk(base, src, send_chunk);
			sent_total += (uint64_t) send_chunk;
			if (sent_total <= (uint64_t) (4U * 1024U)) {
				uart_puts_all("[mk] fetch: first bulk done\r\n");
			}
			uart_puts_all("[mk] fetch: chunk done idx=0x");
			uart_puthex64_all(chunk_index);
			uart_puts_all("\r\n");
			fastboot_fetch_progress_pump(base);
			if (sent_total >= next_progress) {
				uart_puts_all("[mk] fetch: sent=0x");
				uart_puthex64_all(sent_total);
				uart_puts_all("\r\n");
				next_progress += 1024U * 1024U;
			}
			src += send_chunk;
			send_size -= (uint64_t) send_chunk;
			chunk_index++;
		}
		uart_puts_all("[mk] fetch: before OKAY\r\n");
		fastboot_send_response(base, "OKAY", "");
		return;
	}

	if (mk_stage0_storage_prepare() == 0) {
		fastboot_fail_msg(base, "storage unavailable");
		return;
	}
	if (mk_stage0_storage_find_partition(label, &part_lba, &part_count) == 0) {
		fastboot_fail_msg(base, "partition not found");
		return;
	}
	if (part_count > (UINT64_MAX / 512U)) {
		fastboot_fail_msg(base, "partition too large");
		return;
	}
	part_size_bytes = part_count * 512U;
	if (offset > part_size_bytes) {
		fastboot_fail_msg(base, "offset too large");
		return;
	}
	remain = part_size_bytes - offset;
	if (size == 0U) {
		size = remain;
	}
	if (size > remain) {
		fastboot_fail_msg(base, "size too large");
		return;
	}
	if (size == 0U) {
		fastboot_fail_msg(base, "empty fetch");
		return;
	}
	if (size > 0xffffffffU) {
		fastboot_fail_msg(base, "fetch >4g unsupported");
		return;
	}

	uart_puts_all("[mk] fastboot fetch part=");
	uart_puts_all(label);
	uart_puts_all(" offset=0x");
	uart_puthex64_all(offset);
	uart_puts_all(" size=0x");
	uart_puthex64_all(size);
	uart_puts_all("\r\n");

	fastboot_send_data_header(base, (uint32_t) size);
	g_fastboot_fetch_trace_packets = 4U;
	g_fastboot_fetch_trace_seq = 0U;

	lba = part_lba + (offset >> 9);
	in_sector = (uint32_t) (offset & 0x1ffU);
	while (size != 0U) {
		uint32_t chunk;
		uint32_t first_packet;

		mk_stage0_storage_pet_wdt();
		if (mk_stage0_storage_read_sector(lba, g_fastboot_sector_buf) == 0) {
			fastboot_fail_msg(base, "read failed");
			return;
		}
		chunk = 512U - in_sector;
		if ((uint64_t) chunk > size) {
			chunk = (uint32_t) size;
		}
		if (offset == 0U && in_sector == 0U && lba == part_lba) {
			first_packet = chunk;
			if (first_packet > 512U) {
				first_packet = 512U;
			}
			uart_puts_all("[mk] fetch part: before first raw size=0x");
			uart_puthex64_all(first_packet);
			uart_puts_all("\r\n");
			fastboot_send_raw(base, &g_fastboot_sector_buf[in_sector], (uint16_t) first_packet);
			uart_puts_all("[mk] fetch part: after first raw\r\n");
			if (chunk > first_packet) {
				uart_puts_all("[mk] fetch part: chunk begin idx=0x");
				uart_puthex64_all(part_chunk_index);
				uart_puts_all(" size=0x");
				uart_puthex64_all(chunk - first_packet);
				uart_puts_all("\r\n");
				fastboot_send_bulk(base, &g_fastboot_sector_buf[in_sector + first_packet],
						  chunk - first_packet);
				uart_puts_all("[mk] fetch part: chunk done idx=0x");
				uart_puthex64_all(part_chunk_index);
				uart_puts_all("\r\n");
				fastboot_fetch_progress_pump(base);
			}
		} else {
			uart_puts_all("[mk] fetch part: chunk begin idx=0x");
			uart_puthex64_all(part_chunk_index);
			uart_puts_all(" size=0x");
			uart_puthex64_all(chunk);
			uart_puts_all("\r\n");
			fastboot_send_bulk(base, &g_fastboot_sector_buf[in_sector], chunk);
			uart_puts_all("[mk] fetch part: chunk done idx=0x");
			uart_puthex64_all(part_chunk_index);
			uart_puts_all("\r\n");
			fastboot_fetch_progress_pump(base);
		}
		size -= (uint64_t) chunk;
		lba++;
		in_sector = 0U;
		part_chunk_index++;
	}

	fastboot_send_response(base, "OKAY", "");
}

static void fastboot_handle_download_command(volatile uint8_t *base, const char *arg)
{
	uint32_t bytes = 0U;

	if (parse_u32_hex_ascii(arg, &bytes) == 0U) {
		fastboot_fail_msg(base, "bad download size");
		return;
	}
	if (bytes > MK_FASTBOOT_DOWNLOAD_MAX) {
		fastboot_fail_msg(base, "data too large");
		return;
	}

	g_fastboot_download_expected = bytes;
	g_fastboot_download_received = 0U;
	g_fastboot_download_active = 1U;
	g_fastboot_download_staged_size = 0U;

	uart_puts_all("[mk] fastboot download bytes=0x");
	uart_puthex64_all(bytes);
	uart_puts_all("\r\n");

	fastboot_send_data_header(base, bytes);
	if (bytes == 0U) {
		g_fastboot_download_active = 0U;
		g_fastboot_download_staged_size = 0U;
		fastboot_send_response(base, "OKAY", "");
	}
}

static void fastboot_handle_flash_command(volatile uint8_t *base, const char *label)
{
	uint64_t part_lba = 0U;
	uint64_t part_count = 0U;
	uint64_t part_bytes = 0U;
	uint32_t magic_le = 0U;
	uint32_t remaining;
	uint64_t lba;
	const uint8_t *src;

	if (label == 0 || label[0] == '\0') {
		fastboot_fail_msg(base, "missing partition");
		return;
	}
	if (g_fastboot_download_staged_size == 0U) {
		fastboot_fail_msg(base, "no image downloaded");
		return;
	}
	if (mk_stage0_storage_prepare() == 0) {
		fastboot_fail_msg(base, "storage unavailable");
		return;
	}
	if (mk_stage0_storage_find_partition(label, &part_lba, &part_count) == 0) {
		fastboot_fail_msg(base, "partition not found");
		return;
	}
	if (part_count > (UINT64_MAX / 512U)) {
		fastboot_fail_msg(base, "partition too large");
		return;
	}
	part_bytes = part_count * 512U;
	magic_le = rd_le32(g_fastboot_download_buf);
	if (magic_le != SPARSE_MAGIC &&
	    (uint64_t) g_fastboot_download_staged_size > part_bytes) {
		fastboot_fail_msg(base, "image too large");
		return;
	}

	uart_puts_all("[mk] fastboot flash part=");
	uart_puts_all(label);
	uart_puts_all(" lba=0x");
	uart_puthex64_all(part_lba);
	uart_puts_all(" count=0x");
	uart_puthex64_all(part_count);
	uart_puts_all(" bytes=0x");
	uart_puthex64_all(g_fastboot_download_staged_size);
	uart_puts_all("\r\n");

	/* Power-cycle eMMC VCC to clear Micron Power-On Write Protection, then
	 * re-initialize the eMMC.  msdc0_go_idle_reinit() (called inside
	 * mk_stage0_storage_clr_write_prot_range) handles the post-VCC CMD1
	 * polling and card bring-up. */
	{
		uint64_t t0 = mk_rd_cntpct();
		emmc_vcc_cycle();
		mk_print_elapsed_ms(t0, "vcc_cycle");
		mk_stage0_storage_clr_write_prot_range(part_lba,
		    (uint64_t) g_fastboot_download_staged_size / 512U + 1U);
		mk_print_elapsed_ms(t0, "reinit");
	}

	if (magic_le == SPARSE_MAGIC) {
		uart_puts_all("[mk] fastboot flash sparse\r\n");
		if (fastboot_flash_sparse(base, part_lba, part_count) == 0) {
			return;
		}
	} else {
		remaining = g_fastboot_download_staged_size;
		src = g_fastboot_download_buf;
		lba = part_lba;

		uart_puts_all("[mk] fastboot flash lba=0x");
		uart_puthex64_all(lba);
		uart_puts_all("\r\n");

		if (remaining >= 512U) {
			uint64_t tw = mk_rd_cntpct();
			uint32_t full_sectors = remaining / 512U;
			mk_stage0_storage_pet_wdt();
			if (mk_stage0_storage_write_sectors(lba, src, full_sectors) == 0) {
				fastboot_fail_msg(base, "write failed");
				return;
			}
			mk_print_elapsed_ms(tw, "write");
			lba += (uint64_t) full_sectors;
			src += (uint64_t) full_sectors * 512U;
			remaining -= full_sectors * 512U;
		}

		if (remaining != 0U) {
			uint32_t i;
			for (i = 0U; i < 512U; i++) {
				g_fastboot_sector_buf[i] = 0U;
			}
			for (i = 0U; i < remaining; i++) {
				g_fastboot_sector_buf[i] = src[i];
			}
			mk_stage0_storage_pet_wdt();
			if (mk_stage0_storage_write_sector(lba, g_fastboot_sector_buf) == 0) {
				fastboot_fail_msg(base, "write failed");
				return;
			}
		}
	}
	uart_puts_all("[mk] fastboot flash write complete\r\n");
	uart_puts_all("[mk] fastboot flash flush begin\r\n");
	{
		uint64_t tf = mk_rd_cntpct();
		if (mk_stage0_storage_flush() == 0) {
			uart_puts_all("[mk] fastboot flash flush failed\r\n");
			fastboot_fail_msg(base, "flush failed");
			return;
		}
		mk_print_elapsed_ms(tf, "flush");
	}
	uart_puts_all("[mk] fastboot flash flush complete\r\n");

	g_fastboot_download_staged_size = 0U;
	fastboot_send_response(base, "OKAY", "");
}

static void fastboot_handle_command(volatile uint8_t *base, const char *cmd)
{
	const char *arg;
	if (cmd == 0) {
		return;
	}

	uart_puts_all("[mk] fastboot cmd=");
	uart_puts_all(cmd);
	uart_puts_all("\r\n");

	if (str_starts_with_lit(cmd, "getvar:") != 0U) {
		arg = cmd + 7;
		if (str_eq_lit(arg, "version") != 0U) {
			uart_puts_all("[mk] fastboot getvar version\r\n");
			uart_puts_all("[mk] fastboot send begin version\r\n");
			fastboot_send_raw(base, g_fb_okay_version, sizeof(g_fb_okay_version) - 1U);
			uart_puts_all("[mk] fastboot send done version\r\n");
		} else if (str_eq_lit(arg, "product") != 0U) {
			uart_puts_all("[mk] fastboot getvar product\r\n");
			uart_puts_all("[mk] fastboot send begin product\r\n");
			fastboot_send_raw(base, g_fb_okay_product, sizeof(g_fb_okay_product) - 1U);
			uart_puts_all("[mk] fastboot send done product\r\n");
		} else if (str_eq_lit(arg, "serialno") != 0U) {
			uart_puts_all("[mk] fastboot getvar serialno\r\n");
			uart_puts_all("[mk] fastboot send begin serial\r\n");
			fastboot_send_response(base, "OKAY", (const char *) g_usb_serial_ascii);
			uart_puts_all("[mk] fastboot send done serial\r\n");
		} else if (str_eq_lit(arg, "max-download-size") != 0U) {
			uart_puts_all("[mk] fastboot getvar max-download-size\r\n");
			fastboot_send_okay_u32_hex(base, MK_FASTBOOT_DOWNLOAD_MAX);
		} else if (str_eq_lit(arg, "max-fetch-size") != 0U) {
			uint64_t storage_bytes = 0U;
			uint32_t max_fetch = 0xffffffffU;

			uart_puts_all("[mk] fastboot getvar max-fetch-size\r\n");
			if (mk_stage0_storage_capacity_bytes(&storage_bytes) != 0) {
				max_fetch = (storage_bytes > 0xffffffffULL) ? 0xffffffffU : (uint32_t) storage_bytes;
			}
			uart_puts_all("[mk] fastboot max-fetch-size=0x");
			uart_puthex64_all(max_fetch);
			uart_puts_all("\r\n");
			fastboot_send_okay_u32_hex(base, max_fetch);
		} else if (str_starts_with_lit(arg, "partition-size:") != 0U) {
			const char *label = arg + 15;
			uint64_t part_lba = 0U;
			uint64_t part_count = 0U;
			uint64_t part_bytes = 0U;
			char size_ascii[24];

			if (label[0] == '\0') {
				fastboot_send_response(base, "OKAY", "");
				return;
			}
			if (str_eq_lit(label, "peacock-zImage") != 0U ||
			    str_eq_lit(label, "peacock-initramfs") != 0U) {
				mk_ext2_t ext2;
				uint64_t boot_lba = 0U;
				uint64_t boot_count = 0U;
				const char *path = (str_eq_lit(label, "peacock-zImage") != 0U)
					? "/zImage" : "/initramfs.cpio.gz";
				int file_size;

				if (mk_stage0_storage_prepare() != 0 &&
				    mk_stage0_storage_find_partition_within("userdata", "boot", &boot_lba, &boot_count) != 0 &&
				    mk_ext2_open(boot_lba, &ext2) != 0) {
					file_size = mk_ext2_load_file(&ext2, path, g_fastboot_download_buf, MK_FASTBOOT_DOWNLOAD_MAX);
					if (file_size > 0) {
						part_bytes = (uint64_t) file_size;
					}
				}
			} else
			if (mk_stage0_storage_prepare() != 0 &&
			    mk_stage0_storage_find_partition(label, &part_lba, &part_count) != 0 &&
			    part_count <= (UINT64_MAX / 512U)) {
				part_bytes = part_count * 512U;
			}
			if (u64_to_hex_ascii(part_bytes, size_ascii, (uint8_t) sizeof(size_ascii)) == 0U) {
				fastboot_send_response(base, "OKAY", "");
			} else {
				fastboot_send_response(base, "OKAY", size_ascii);
			}
		} else if (str_starts_with_lit(arg, "partition-type:") != 0U) {
			uart_puts_all("[mk] fastboot getvar partition-type\r\n");
			uart_puts_all("[mk] fastboot send begin parttype\r\n");
			fastboot_send_raw(base, g_fb_okay_raw, sizeof(g_fb_okay_raw) - 1U);
			uart_puts_all("[mk] fastboot send done parttype\r\n");
		} else if (str_starts_with_lit(arg, "has-slot:") != 0U) {
			uart_puts_all("[mk] fastboot getvar has-slot\r\n");
			fastboot_send_raw(base, g_fb_okay_no, sizeof(g_fb_okay_no) - 1U);
		} else if (str_starts_with_lit(arg, "is-logical:") != 0U) {
			uart_puts_all("[mk] fastboot getvar is-logical\r\n");
			fastboot_send_raw(base, g_fb_okay_no, sizeof(g_fb_okay_no) - 1U);
		} else if (str_eq_lit(arg, "slot-count") != 0U) {
			uart_puts_all("[mk] fastboot getvar slot-count\r\n");
			fastboot_send_raw(base, g_fb_okay_zero, sizeof(g_fb_okay_zero) - 1U);
		} else if (str_eq_lit(arg, "is-userspace") != 0U) {
			uart_puts_all("[mk] fastboot getvar is-userspace\r\n");
			fastboot_send_raw(base, g_fb_okay_no, sizeof(g_fb_okay_no) - 1U);
		} else {
			uart_puts_all("[mk] fastboot getvar unknown\r\n");
			uart_puts_all("[mk] fastboot send begin unknown\r\n");
			fastboot_send_raw(base, g_fb_okay_empty, sizeof(g_fb_okay_empty) - 1U);
			uart_puts_all("[mk] fastboot send done unknown\r\n");
		}
		return;
	}

	if (str_starts_with_lit(cmd, "fetch:") != 0U) {
		fastboot_handle_fetch_command(base, cmd + 6);
		return;
	}

	if (str_starts_with_lit(cmd, "download:") != 0U) {
		fastboot_handle_download_command(base, cmd + 9);
		return;
	}
	if (str_starts_with_lit(cmd, "flash:") != 0U) {
		fastboot_handle_flash_command(base, cmd + 6);
		return;
	}
	if (str_starts_with_lit(cmd, "erase:") != 0U) {
		fastboot_send_response(base, "FAIL", "erase unsupported");
		return;
	}
	if (str_starts_with_lit(cmd, "reboot:") != 0U) {
		const char *target = cmd + 7;

		fastboot_send_response(base, "OKAY", "");
		if (str_eq_lit(target, "recovery") != 0U) {
			g_fastboot_pending_action = MK_FASTBOOT_ACTION_REBOOT_RECOVERY;
		} else if (str_eq_lit(target, "bootloader") != 0U) {
			g_fastboot_pending_action = MK_FASTBOOT_ACTION_REBOOT_BOOTLOADER;
		} else {
			g_fastboot_pending_action = MK_FASTBOOT_ACTION_REBOOT;
		}
		fastboot_maybe_apply_action_now(base, g_fastboot_pending_action);
		return;
	}
	if (str_starts_with_lit(cmd, "reboot ") != 0U) {
		const char *target = cmd + 7;

		fastboot_send_response(base, "OKAY", "");
		if (str_eq_lit(target, "recovery") != 0U) {
			g_fastboot_pending_action = MK_FASTBOOT_ACTION_REBOOT_RECOVERY;
		} else if (str_eq_lit(target, "bootloader") != 0U) {
			g_fastboot_pending_action = MK_FASTBOOT_ACTION_REBOOT_BOOTLOADER;
		} else {
			g_fastboot_pending_action = MK_FASTBOOT_ACTION_REBOOT;
		}
		fastboot_maybe_apply_action_now(base, g_fastboot_pending_action);
		return;
	}
	if (str_eq_lit(cmd, "reboot-recovery") != 0U) {
		fastboot_send_response(base, "OKAY", "");
		g_fastboot_pending_action = MK_FASTBOOT_ACTION_REBOOT_RECOVERY;
		fastboot_maybe_apply_action_now(base, g_fastboot_pending_action);
		return;
	}
	if (str_eq_lit(cmd, "reboot") != 0U) {
		fastboot_send_response(base, "OKAY", "");
		g_fastboot_pending_action = MK_FASTBOOT_ACTION_REBOOT;
		fastboot_maybe_apply_action_now(base, g_fastboot_pending_action);
		return;
	}
	if (str_eq_lit(cmd, "reboot-bootloader") != 0U) {
		fastboot_send_response(base, "OKAY", "");
		g_fastboot_pending_action = MK_FASTBOOT_ACTION_REBOOT_BOOTLOADER;
		fastboot_maybe_apply_action_now(base, g_fastboot_pending_action);
		return;
	}
	if (str_eq_lit(cmd, "continue") != 0U) {
		fastboot_send_response(base, "OKAY", "");
		g_fastboot_pending_action = MK_FASTBOOT_ACTION_CONTINUE;
		return;
	}
	if (str_eq_lit(cmd, "oem dump-bootargs") != 0U) {
		uart_puts_all("[mk] fastboot oem dump-bootargs\r\n");
		if (g_lk_bootargs_len != 0U) {
			uint32_t off = 0U;
			char chunk[57];

			while (off < g_lk_bootargs_len) {
				uint32_t i;

				for (i = 0U; i < 56U && g_lk_bootargs[off + i] != '\0'; i++) {
					chunk[i] = g_lk_bootargs[off + i];
				}
				chunk[i] = '\0';
				fastboot_send_response(base, "INFO", chunk);
				off += i;
			}
		}
		fastboot_send_response(base, "OKAY", "");
		return;
	}
	if (str_eq_lit(cmd, "oem boot-kernel") != 0U ||
	    str_eq_lit(cmd, "oem boot-kernel zImage") != 0U) {
		if (g_fastboot_download_staged_size == 0U) {
			fastboot_send_response(base, "FAIL", "no staged kernel");
			return;
		}
		uart_puts_all("[mk] fastboot oem boot-kernel bytes=0x");
		uart_puthex64_all(g_fastboot_download_staged_size);
		uart_puts_all("\r\n");
		fastboot_send_response(base, "OKAY", "");
		g_fastboot_pending_action = MK_FASTBOOT_ACTION_BOOT_STAGED_KERNEL;
		return;
	}
	if (str_starts_with_lit(cmd, "reboot") != 0U) {
		/* Be tolerant of host command variants: pick action by keyword. */
		fastboot_send_response(base, "OKAY", "");
		if (str_contains_lit(cmd, "recovery") != 0U) {
			g_fastboot_pending_action = MK_FASTBOOT_ACTION_REBOOT_RECOVERY;
		} else if (str_contains_lit(cmd, "bootloader") != 0U) {
			g_fastboot_pending_action = MK_FASTBOOT_ACTION_REBOOT_BOOTLOADER;
		} else {
			g_fastboot_pending_action = MK_FASTBOOT_ACTION_REBOOT;
		}
		fastboot_maybe_apply_action_now(base, g_fastboot_pending_action);
		return;
	}

	fastboot_send_response(base, "FAIL", "unknown command");
}

static void usb_fastboot_ep1_init(volatile uint8_t *base)
{
	uint16_t maxp = ((mmio_read8(base, MUSB_POWER) & MUSB_POWER_HSMODE) != 0U) ? 512U : 64U;
	uint16_t txfifo_add = 8U;
	uint16_t txfifo_sz_code = (maxp == 512U) ? 6U : 3U;
	uint16_t tx_units = (uint16_t) (maxp / 8U);
	uint16_t rxfifo_add = (uint16_t) (txfifo_add + tx_units);

	ep_select(base, 1U);
	mmio_write16(base, MUSB_TXMAXP, maxp);
	mmio_write16(base, MUSB_RXMAXP, maxp);
	mmio_write8(base, MUSB_TXFIFOSZ, (uint8_t) txfifo_sz_code);
	mmio_write8(base, MUSB_RXFIFOSZ, (uint8_t) txfifo_sz_code);
	mmio_write16(base, MUSB_TXFIFOADD, txfifo_add);
	mmio_write16(base, MUSB_RXFIFOADD, rxfifo_add);

	/* Flush stale data after reset/config changes. */
	mmio_write16(base, MUSB_TXCSR, MUSB_TXCSR_FLUSHFIFO);
	mmio_write16(base, MUSB_TXCSR, MUSB_TXCSR_FLUSHFIFO);
	mmio_write16(base, MUSB_TXCSR, 0U);
	mmio_write16(base, MUSB_RXCSR, MUSB_RXCSR_FLUSHFIFO);
	mmio_write16(base, MUSB_RXCSR, MUSB_RXCSR_FLUSHFIFO);
	mmio_write16(base, MUSB_RXCSR, 0U);

	mmio_write16(base, MUSB_INTRTXE, 0x0003U); /* EP0 + EP1 IN */
	mmio_write16(base, MUSB_INTRRXE, 0x0002U); /* EP1 OUT */
	g_usb_state.ep1_ready = 1U;

	uart_puts_all("[mk] fastboot ep1 maxp=0x");
	uart_puthex64_all(maxp);
	uart_puts_all(" txadd=0x");
	uart_puthex64_all(txfifo_add);
	uart_puts_all(" rxadd=0x");
	uart_puthex64_all(rxfifo_add);
	uart_puts_all("\r\n");
}

int mk_stage0_mtk_usb_fastboot_init(void)
{
	volatile uint8_t *base = usb_regs();
	uint8_t intrusbe;
	uint8_t power;
	uint8_t ulpi;

	g_usb_state.configured = 0;
	g_usb_state.address = 0;
	g_usb_state.pending_address = 0;
	g_usb_state.address_pending = 0;
	g_usb_state.poll_count = 0;
	g_usb_state.reset_count = 0;
	g_usb_state.debug_once = 0;
	g_usb_state.ep1_ready = 0;
	g_fastboot_download_expected = 0U;
	g_fastboot_download_received = 0U;
	g_fastboot_download_staged_size = 0U;
	g_fastboot_download_active = 0U;
	g_fastboot_pending_action = MK_FASTBOOT_ACTION_NONE;

	/* Preboot CC/USB policy cannot be trusted in stage0; configure it here. */
	usb_try_force_typec_sink_sgm7220();
	usb_wait_typec_attach_sgm7220();
	usb_enable_vusb33();
	usb_clock_init();
	for (volatile uint32_t spin = 0; spin < 600000U; spin++) {
		__asm__ volatile("");
	}
	usb_phy_init();
	usb_core_reset(base);

	/* Match downstream MTK mask in active TWRP runtime (0x2f). */
	mmio_write32(base, MTK_USB_L1INTM,
		     MTK_USB_TX_INT_STATUS |
		     MTK_USB_RX_INT_STATUS |
		     MTK_USB_USBCOM_INT_STATUS |
		     MTK_USB_DMA_INT_STATUS |
		     MTK_USB_QINT_STATUS);
	mmio_write32(base, MUSB_HSDMA_INTR, 0x00ff00ffU);
	mmio_write16(base, MUSB_INTRTXE, 0x0001U);
	mmio_write16(base, MUSB_INTRRXE, 0x0000U);
	/* Device-mode bus interrupts seen in working TWRP runtime: 0x27. */
	intrusbe = (uint8_t) (MUSB_INTR_SUSPEND |
			      MUSB_INTR_RESUME |
			      MUSB_INTR_RESET |
			      MUSB_INTR_DISCONNECT);
	mmio_write8(base, MUSB_INTRUSBE, intrusbe);

	mmio_write8(base, MUSB_FADDR, 0);
	mmio_write8(base, MUSB_DEVCTL,
		    (uint8_t) (mmio_read8(base, MUSB_DEVCTL) | MUSB_DEVCTL_SESSION));
	/* Explicit EP0 maxpacket to 64 for fastboot descriptors. */
	ep_select(base, 0);
	mmio_write16(base, MUSB_TXMAXP, 64U);
	ep0_flush(base);
	/* Mirror musb_start(): ignore babble noise in ULPI path. */
	ulpi = mmio_read8(base, MUSB_ULPI_REG_DATA);
	ulpi |= 0x80U;
	ulpi &= (uint8_t) ~0x40U;
	mmio_write8(base, MUSB_ULPI_REG_DATA, ulpi);

	power = (uint8_t) (MUSB_POWER_ENSUSPEND | MUSB_POWER_HSENAB);
	mmio_write8(base, MUSB_POWER, power);
	for (volatile uint32_t spin = 0; spin < 50000U; spin++) {
		__asm__ volatile("");
	}
	power = (uint8_t) (MUSB_POWER_ENSUSPEND |
			   MUSB_POWER_HSENAB |
			   MUSB_POWER_SOFTCONN);
	mmio_write8(base, MUSB_POWER, power);

	/*
	 * Keep endpoint/FIFO layout untouched in stage0 fallback.
	 * Downstream stack initializes these later; touching them here has
	 * caused control transfer regressions on this target.
	 */

	g_usb_state.started = 1U;
	usb_dump_state("init");
	return 0;
}

static void usb_restore_typec_sink_policy(void)
{
	if (g_tcpc_saved.valid == 0U) {
		return;
	}

	(void) usb_sgm7220_write_reg8(g_tcpc_saved.i2c_base, g_tcpc_saved.addr,
				      SGM7220_REG_MOD, g_tcpc_saved.mod);
	(void) usb_sgm7220_write_reg8(g_tcpc_saved.i2c_base, g_tcpc_saved.addr,
				      SGM7220_REG_INT, g_tcpc_saved.intr);
	(void) usb_sgm7220_write_reg8(g_tcpc_saved.i2c_base, g_tcpc_saved.addr,
				      SGM7220_REG_SET, g_tcpc_saved.set);
	/* Bitbang leaves GPIOs 48/49 in GPIO mode; restore I2C peripheral mux
	 * so the kernel's I2C5 controller can drive the bus. */
	bb_set_mode_i2c(MK_USB_TCPC_SCL_GPIO);
	bb_set_mode_i2c(MK_USB_TCPC_SDA_GPIO);
	uart_puts_all("[mk] usb tcpc: restored\r\n");
}

static void usb_fastboot_transport_stop(void)
{
	volatile uint8_t *base;
	uint8_t power;
	uint8_t devctl;

	if (g_usb_state.started == 0U) {
		return;
	}

	base = usb_regs();

	mmio_write16(base, MUSB_INTRTXE, 0U);
	mmio_write16(base, MUSB_INTRRXE, 0U);
	mmio_write8(base, MUSB_INTRUSBE, 0U);
	mmio_write16(base, MUSB_INTRTX, 0xffffU);
	mmio_write16(base, MUSB_INTRRX, 0xffffU);
	mmio_write8(base, MUSB_INTRUSB, 0xffU);
	mmio_write32(base, MTK_USB_L1INTM, 0U);
	mmio_write32(base, MTK_USB_L1INTS, 0xffffffffU);
	mmio_write32(base, MUSB_HSDMA_INTR, 0x00ff00ffU);

	power = mmio_read8(base, MUSB_POWER);
	power &= (uint8_t) ~(MUSB_POWER_SOFTCONN | MUSB_POWER_HSENAB | MUSB_POWER_ENSUSPEND);
	mmio_write8(base, MUSB_POWER, power);

	devctl = mmio_read8(base, MUSB_DEVCTL);
	devctl &= (uint8_t) ~MUSB_DEVCTL_SESSION;
	mmio_write8(base, MUSB_DEVCTL, devctl);

	usb_core_reset(base);
}

static void usb_platform_restore_saved_state(void)
{
	if (g_usb_phy_saved.valid != 0U) {
		usb_phy_restore();
	} else {
		uart_puts_all("[mk] usb phy: no snapshot\r\n");
	}

	if (g_tcpc_saved.valid != 0U) {
		usb_restore_typec_sink_policy();
	} else {
		uart_puts_all("[mk] usb tcpc: no snapshot\r\n");
	}

	if (g_vusb33_saved.valid != 0U) {
		usb_restore_vusb33();
	} else {
		uart_puts_all("[mk] usb vusb33: no snapshot\r\n");
	}

	if (g_usb_clock_saved.valid != 0U) {
		usb_clock_restore();
	} else {
		uart_puts_all("[mk] usb clock: no snapshot, passive gate\r\n");
		usb_clock_quiesce();
	}
}

static void usb_fastboot_state_clear(void)
{
	g_usb_state.started = 0U;
	g_usb_state.configured = 0U;
	g_usb_state.address = 0U;
	g_usb_state.pending_address = 0U;
	g_usb_state.address_pending = 0U;
	g_usb_state.ep1_ready = 0U;
	g_fastboot_download_expected = 0U;
	g_fastboot_download_received = 0U;
	g_fastboot_download_staged_size = 0U;
	g_fastboot_download_active = 0U;
	g_fastboot_pending_action = MK_FASTBOOT_ACTION_NONE;
}

void mk_stage0_mtk_usb_fastboot_quiesce(void)
{
	if (g_usb_state.started == 0U) {
		return;
	}

	usb_fastboot_transport_stop();
	usb_platform_restore_saved_state();
	usb_fastboot_state_clear();
}

void mk_stage0_mtk_usb_platform_restore_for_linux(void)
{
	uart_puts_all("[mk] usb platform restore begin\r\n");
	if (g_usb_state.started != 0U) {
		usb_fastboot_transport_stop();
	} else {
		uart_puts_all("[mk] usb fastboot: not-started\r\n");
	}
	usb_platform_restore_saved_state();
	usb_fastboot_state_clear();
	uart_puts_all("[mk] usb platform restore done\r\n");
}

void mk_stage0_mtk_usb_fastboot_poll(void)
{
	volatile uint8_t *base = usb_regs();
	uint32_t l1;
	uint8_t intrusbe;
	uint8_t int_usb;
	uint16_t intrtxe;
	uint16_t int_tx;
	uint16_t intrrxe;
	uint16_t int_rx;
	uint16_t csr0;

	if (g_usb_state.started == 0U) {
		return;
	}

	if (g_usb_state.configured == 0U) {
		g_usb_state.poll_count++;
		if (g_usb_state.debug_once == 0U) {
			usb_dump_state("wait");
			g_usb_state.debug_once = 1U;
		}
	}

	l1 = mmio_read32(base, MTK_USB_L1INTS) & mmio_read32(base, MTK_USB_L1INTM);
	(void) l1;

	intrusbe = mmio_read8(base, MUSB_INTRUSBE);
	int_usb = (uint8_t) (mmio_read8(base, MUSB_INTRUSB) & intrusbe);
	if (int_usb != 0U) {
		mmio_write8(base, MUSB_INTRUSB, int_usb);
	}
	intrtxe = mmio_read16(base, MUSB_INTRTXE);
	int_tx = (uint16_t) (mmio_read16(base, MUSB_INTRTX) & intrtxe);
	if (int_tx != 0U) {
		mmio_write16(base, MUSB_INTRTX, int_tx);
	}
	intrrxe = mmio_read16(base, MUSB_INTRRXE);
	int_rx = (uint16_t) (mmio_read16(base, MUSB_INTRRX) & intrrxe);
	if (int_rx != 0U) {
		mmio_write16(base, MUSB_INTRRX, int_rx);
	}

	if ((int_usb & MUSB_INTR_RESET) != 0U) {
		uint8_t power;

		g_usb_state.reset_count++;
		g_usb_state.configured = 0;
		g_usb_state.address = 0;
		g_usb_state.pending_address = 0;
		g_usb_state.address_pending = 0;
		g_fastboot_download_expected = 0U;
		g_fastboot_download_received = 0U;
		g_fastboot_download_staged_size = 0U;
		g_fastboot_download_active = 0U;
		g_fastboot_pending_action = MK_FASTBOOT_ACTION_NONE;

		/* Re-arm device-mode interrupt/mode state after bus reset. */
		mmio_write16(base, MUSB_INTRRXE, 0x0000U);
		mmio_write16(base, MUSB_INTRTXE, 0x0001U);
		g_usb_state.ep1_ready = 0U;
		mmio_write8(base, MUSB_INTRUSBE,
			    (uint8_t) (MUSB_INTR_SUSPEND |
				       MUSB_INTR_RESUME |
				       MUSB_INTR_RESET |
				       MUSB_INTR_DISCONNECT));
		mmio_write8(base, MUSB_DEVCTL,
			    (uint8_t) (mmio_read8(base, MUSB_DEVCTL) |
				       MUSB_DEVCTL_SESSION));
		power = (uint8_t) (MUSB_POWER_ENSUSPEND |
				   MUSB_POWER_HSENAB |
				   MUSB_POWER_SOFTCONN);
		mmio_write8(base, MUSB_POWER, power);
		mmio_write8(base, MUSB_FADDR, 0);
		ep0_flush(base);
		usb_dump_state("reset");
	}
	if ((int_usb & MUSB_INTR_DISCONNECT) != 0U) {
		uart_puts_all("[mk] usb disconnect\r\n");
	}

	ep_select(base, 0);
	csr0 = mmio_read16(base, MUSB_CSR0);
	if ((csr0 & MUSB_CSR0_P_SETUPEND) != 0U) {
		mmio_write16(base, MUSB_CSR0, MUSB_CSR0_P_SVDSETUPEND);
		csr0 = mmio_read16(base, MUSB_CSR0);
	}

	if ((csr0 & MUSB_CSR0_RXPKTRDY) != 0U) {
		uint8_t raw[8];
		usb_setup_packet_t pkt;
		uint16_t count = mmio_read16(base, MUSB_COUNT0) & 0x7fU;

		if (count != 8U) {
			uart_puts_all("[mk] usb ep0 bad setup count=0x");
			uart_puthex64_all(count);
			uart_puts_all(" csr0=0x");
			uart_puthex64_all(csr0);
			uart_puts_all("\r\n");
			ep0_stall(base);
			return;
		}
		ep0_read_fifo(base, raw, 8U);
		pkt.bm_request_type = raw[0];
		pkt.b_request = raw[1];
		pkt.w_value = (uint16_t) raw[2] | ((uint16_t) raw[3] << 8);
		pkt.w_index = (uint16_t) raw[4] | ((uint16_t) raw[5] << 8);
		pkt.w_length = (uint16_t) raw[6] | ((uint16_t) raw[7] << 8);

		/*
		 * Mirror downstream EP0 flow:
		 * on IN data requests, first clear RXPKTRDY (SVDRXPKTRDY)
		 * and wait for it to latch before loading FIFO.
		 */
		if ((pkt.w_length != 0U) && ((pkt.bm_request_type & USB_DIR_IN) != 0U)) {
			uint32_t wait = 50000U;
			mmio_write16(base, MUSB_CSR0, MUSB_CSR0_P_SVDRXPKTRDY);
			while (((mmio_read16(base, MUSB_CSR0) & MUSB_CSR0_RXPKTRDY) != 0U) &&
			       (wait-- != 0U)) {
				__asm__ volatile("");
			}
			if (wait == 0U) {
				uart_puts_all("[mk] usb setup: svdrxpkt timeout\r\n");
			}
		}

		handle_setup_packet(base, &pkt);
		usb_dump_state("setup");
		return;
	}

	if ((int_tx & 0x0001U) != 0U) {
		uart_puts_all("[mk] usb ep0 txint csr0=0x");
		uart_puthex64_all(csr0);
		uart_puts_all(" cnt0=0x");
		uart_puthex64_all(mmio_read16(base, MUSB_COUNT0) & 0x7fU);
		uart_puts_all("\r\n");
	}

	if (g_usb_state.configured != 0U && g_usb_state.ep1_ready == 0U) {
		usb_fastboot_ep1_init(base);
	}

	if (g_usb_state.ep1_ready != 0U) {
		uint16_t rxcsr;
		uint16_t rxcount;
		uint32_t ep1_loops = 0U;

		ep_select(base, 1U);
		rxcsr = mmio_read16(base, MUSB_RXCSR);
		while (((rxcsr & MUSB_RXCSR_RXPKTRDY) != 0U) && (ep1_loops < 64U)) {
			rxcount = (uint16_t) (mmio_read16(base, MUSB_RXCOUNT) & 0x1fffU);
			if (g_fastboot_download_active != 0U) {
				uint32_t remaining = g_fastboot_download_expected - g_fastboot_download_received;
				uint16_t consume = rxcount;

				if ((uint32_t) consume > remaining) {
					consume = (uint16_t) remaining;
				}
				if (consume != 0U) {
					ep1_read_fifo_fast(base,
							   g_fastboot_download_buf + g_fastboot_download_received,
							   consume);
				}
				if (rxcount > consume) {
					ep1_discard_fifo_fast(base, (uint16_t) (rxcount - consume));
				}
				mmio_write16(base, MUSB_RXCSR, 0U);

				g_fastboot_download_received += consume;
				if (consume != rxcount || g_fastboot_download_received > g_fastboot_download_expected) {
					g_fastboot_download_active = 0U;
					g_fastboot_download_expected = 0U;
					g_fastboot_download_received = 0U;
					g_fastboot_download_staged_size = 0U;
					fastboot_fail_msg(base, "download overflow");
				} else if (g_fastboot_download_received == g_fastboot_download_expected) {
					g_fastboot_download_active = 0U;
					g_fastboot_download_staged_size = g_fastboot_download_expected;
					uart_puts_all("[mk] fastboot download complete bytes=0x");
					uart_puthex64_all(g_fastboot_download_staged_size);
					uart_puts_all("\r\n");
					fastboot_send_response(base, "OKAY", "");
				}
			} else {
				uart_puts_all("[mk] fastboot ep1 rx count=0x");
				uart_puthex64_all(rxcount);
				uart_puts_all(" csr=0x");
				uart_puthex64_all(rxcsr);
				uart_puts_all("\r\n");
				if (rxcount > MK_FASTBOOT_CMD_MAX) {
					rxcount = MK_FASTBOOT_CMD_MAX;
				}
				ep1_read_fifo(base, g_fastboot_cmd_buf, rxcount);
				g_fastboot_cmd_buf[rxcount] = 0U;
				/*
				 * For EP1 OUT, clear RXPKTRDY by writing a clean RXCSR value.
				 * Avoid read-modify-write of implementation-defined status bits.
				 */
				mmio_write16(base, MUSB_RXCSR, 0U);
				fastboot_handle_command(base, (const char *) g_fastboot_cmd_buf);
			}
			rxcsr = mmio_read16(base, MUSB_RXCSR);
			ep1_loops++;
		}
	}

	/*
	 * Commit deferred SET_ADDRESS once status stage has drained.
	 */
	if ((g_usb_state.address_pending != 0U) &&
	    ((csr0 & MUSB_CSR0_TXPKTRDY) == 0U) &&
	    ((csr0 & MUSB_CSR0_RXPKTRDY) == 0U)) {
		g_usb_state.address = g_usb_state.pending_address;
		mmio_write8(base, MUSB_FADDR, g_usb_state.address);
		g_usb_state.address_pending = 0U;
		uart_puts_all("[mk] usb setaddr commit=0x");
		uart_puthex64_all(g_usb_state.address);
		uart_puts_all("\r\n");
	}

	if (g_usb_state.configured != 0U && g_usb_state.ep1_ready == 0U) {
		usb_fastboot_ep1_init(base);
	}
}
