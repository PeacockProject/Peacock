#include "mtk_i2c.h"

#define MTK_TOPCKGEN_BASE 0x10000000ULL
#define MTK_TOP_CLK_CFG_6_CLR 0x00a8U
#define MTK_TOP_CLK_CFG_UPDATE 0x0004U
#define MTK_TOP_I2C_SEL_CLR_MASK 0x00000087U
#define MTK_TOP_I2C_UPDATE_BIT (1U << 24)

#define MTK_INFRACFG_AO_BASE 0x10001000ULL
#define MTK_IFR2_CLR 0x0084U
#define MTK_IFR3_CLR 0x008cU
#define MTK_IFR_I2C_AP_BIT (1U << 11)
#define MTK_IFR_AP_DMA_BIT (1U << 18)

#define MTK_GPIO_BASE 0x10005000ULL
#define MTK_GPIO_MODE_BASE 0x0300U
#define MTK_GPIO_GROUP_STRIDE 0x0010U
#define MTK_I2C3_BASE 0x11009000ULL
#define MTK_I2C5_BASE 0x11016000ULL

#define MTK_I2C3_SCL_GPIO 50U
#define MTK_I2C3_SDA_GPIO 51U
#define MTK_I2C5_SCL_GPIO 48U
#define MTK_I2C5_SDA_GPIO 49U
#define MTK_I2C_PINMUX_MODE 1U

#define MTK_I2C_DATA_PORT 0x0000U
#define MTK_I2C_SLAVE_ADDR 0x0004U
#define MTK_I2C_INTR_MASK 0x0008U
#define MTK_I2C_INTR_STAT 0x000cU
#define MTK_I2C_CONTROL 0x0010U
#define MTK_I2C_TRANSFER_LEN 0x0014U
#define MTK_I2C_TRANSAC_LEN 0x0018U
#define MTK_I2C_DELAY_LEN 0x001cU
#define MTK_I2C_TIMING 0x0020U
#define MTK_I2C_START 0x0024U
#define MTK_I2C_EXT_CONF 0x0028U
#define MTK_I2C_LTIMING 0x002cU
#define MTK_I2C_HS 0x0030U
#define MTK_I2C_IO_CONFIG 0x0034U
#define MTK_I2C_MCU_INTR 0x0040U
#define MTK_I2C_FIFO_ADDR_CLR 0x0038U
#define MTK_I2C_CLOCK_DIV 0x0048U
#define MTK_I2C_SOFTRESET 0x0050U
#define MTK_I2C_IRQ_INFO 0x00e0U
#define MTK_I2C_DEBUGSTAT 0x00e4U
#define MTK_I2C_DEBUGCTRL 0x00e8U
#define MTK_I2C_FIFO_STAT 0x00f4U

#define MTK_I2C_SOFT_RST 0x0001U
#define MTK_I2C_FIFO_CLR 0x0001U
#define MTK_I2C_DELAY_LEN_VALUE 0x0002U
#define MTK_I2C_TIMING_VALUE 0x001aU
#define MTK_I2C_LTIMING_VALUE 0x001aU
#define MTK_I2C_HS_VALUE 0x0000U
#define MTK_I2C_IO_CONFIG_OPEN_DRAIN 0x0003U
#define MTK_I2C_CLOCK_DIV_VALUE 0x0404U
#define MTK_I2C_EXT_CONF_VALUE 0x1800U
#define MTK_I2C_START_TRANSAC 0x0001U
#define MTK_I2C_RS_MUL_TRIG (1U << 14)
#define MTK_I2C_MCU_INTR_EN 0x0001U
#define MTK_I2C_DEBUGCTRL_VALUE 0x0028U

#define MTK_I2C_ACKERR (1U << 1)
#define MTK_I2C_HS_NACKERR (1U << 2)
#define MTK_I2C_ARB_LOST (1U << 3)
#define MTK_I2C_RS_MULTI (1U << 4)
#define MTK_I2C_TIMEOUT (1U << 5)
#define MTK_I2C_DMAERR (1U << 6)
#define MTK_I2C_IBI (1U << 7)
#define MTK_I2C_BUS_ERR (1U << 8)
#define MTK_I2C_TRANSAC_COMP (1U << 0)
#define MTK_I2C_INTR_CLR_MASK (MTK_I2C_BUS_ERR | MTK_I2C_IBI | MTK_I2C_DMAERR | MTK_I2C_TIMEOUT | MTK_I2C_RS_MULTI | MTK_I2C_HS_NACKERR | MTK_I2C_ACKERR | MTK_I2C_ARB_LOST | MTK_I2C_TRANSAC_COMP)

#define MTK_I2C_CONTROL_CLK_EXT_EN (1U << 3)
#define MTK_I2C_CONTROL_ACKERR_DET_EN (1U << 5)
#define MTK_I2C_CONTROL_DMA_EN (1U << 2)
#define MTK_I2C_CONTROL_DMAACK_EN (1U << 8)
#define MTK_I2C_CONTROL_ASYNC_MODE (1U << 9)

static int g_last_i2c_error;
static uint32_t g_last_i2c_status;
static uint32_t g_last_i2c_debug0;
static uint32_t g_last_i2c_debug1;

static void reg_write32(uint64_t addr, uint32_t value)
{
	*(volatile uint32_t *) (uintptr_t) addr = value;
}

static uint16_t reg_read16(uint64_t addr)
{
	return *(volatile uint16_t *) (uintptr_t) addr;
}

static void reg_write16(uint64_t addr, uint16_t value)
{
	*(volatile uint16_t *) (uintptr_t) addr = value;
}

static void gpio_set_mode(uint32_t pin, uint32_t mode)
{
	uint32_t group;
	uint32_t shift;
	uint64_t addr;
	uint32_t value;

	group = pin / 8U;
	shift = (pin % 8U) * 4U;
	addr = MTK_GPIO_BASE + MTK_GPIO_MODE_BASE + ((uint64_t) group * MTK_GPIO_GROUP_STRIDE);
	value = *(volatile uint32_t *) (uintptr_t) addr;
	value &= ~(0xfU << shift);
	value |= (mode & 0xfU) << shift;
	*(volatile uint32_t *) (uintptr_t) addr = value;
}

static void topckgen_select_i2c_26m(void)
{
	/*
	 * CLK_TOP_I2C_SEL:
	 * bits [2:0] mux, bit [7] gate. Clear all => 26MHz source, ungated.
	 */
	reg_write32(MTK_TOPCKGEN_BASE + MTK_TOP_CLK_CFG_6_CLR, MTK_TOP_I2C_SEL_CLR_MASK);
	reg_write32(MTK_TOPCKGEN_BASE + MTK_TOP_CLK_CFG_UPDATE, MTK_TOP_I2C_UPDATE_BIT);
}

static void infracfg_enable_i2c3(void)
{
	/* Both main and DMA clocks are setclr gates on MT6765. */
	reg_write32(MTK_INFRACFG_AO_BASE + MTK_IFR2_CLR, MTK_IFR_I2C_AP_BIT);
	reg_write32(MTK_INFRACFG_AO_BASE + MTK_IFR3_CLR, MTK_IFR_AP_DMA_BIT);
}

static void i2c_prepare(uint64_t base)
{
	uint32_t scl_pin = MTK_I2C3_SCL_GPIO;
	uint32_t sda_pin = MTK_I2C3_SDA_GPIO;

	if (base == MTK_I2C5_BASE) {
		scl_pin = MTK_I2C5_SCL_GPIO;
		sda_pin = MTK_I2C5_SDA_GPIO;
	}

	topckgen_select_i2c_26m();
	infracfg_enable_i2c3();
	gpio_set_mode(scl_pin, MTK_I2C_PINMUX_MODE);
	gpio_set_mode(sda_pin, MTK_I2C_PINMUX_MODE);

	reg_write16(base + MTK_I2C_SOFTRESET, MTK_I2C_SOFT_RST);
	reg_write16(base + MTK_I2C_INTR_MASK, 0U);
	reg_write16(base + MTK_I2C_CONTROL,
		    (uint16_t) (MTK_I2C_CONTROL_CLK_EXT_EN |
				MTK_I2C_CONTROL_ACKERR_DET_EN));
	reg_write16(base + MTK_I2C_DELAY_LEN, MTK_I2C_DELAY_LEN_VALUE);
	reg_write16(base + MTK_I2C_TIMING, MTK_I2C_TIMING_VALUE);
	reg_write16(base + MTK_I2C_LTIMING, MTK_I2C_LTIMING_VALUE);
	reg_write16(base + MTK_I2C_HS, MTK_I2C_HS_VALUE);
	reg_write16(base + MTK_I2C_IO_CONFIG, MTK_I2C_IO_CONFIG_OPEN_DRAIN);
	reg_write16(base + MTK_I2C_MCU_INTR, MTK_I2C_MCU_INTR_EN);
	reg_write16(base + MTK_I2C_CLOCK_DIV, MTK_I2C_CLOCK_DIV_VALUE);
	reg_write16(base + MTK_I2C_DEBUGCTRL, MTK_I2C_DEBUGCTRL_VALUE);
	reg_write16(base + MTK_I2C_EXT_CONF, MTK_I2C_EXT_CONF_VALUE);
}

int mk_stage0_mtk_i2c_write_reg8(uint64_t base, uint8_t addr7, uint8_t reg, uint8_t value)
{
	uint32_t spins;
	uint16_t status;
	uint16_t irq_info;

	g_last_i2c_error = 0;
	g_last_i2c_status = 0U;
	g_last_i2c_debug0 = 0U;
	g_last_i2c_debug1 = 0U;

	if (base == 0U || base == 0xffffffffffffffffULL || addr7 == 0U) {
		g_last_i2c_error = -2;
		return -1;
	}

	i2c_prepare(base);

	reg_write16(base + MTK_I2C_SLAVE_ADDR, (uint16_t) ((uint16_t) addr7 << 1));
	reg_write16(base + MTK_I2C_INTR_STAT, MTK_I2C_INTR_CLR_MASK);
	reg_write16(base + MTK_I2C_FIFO_ADDR_CLR, MTK_I2C_FIFO_CLR);
	reg_write16(base + MTK_I2C_INTR_MASK, MTK_I2C_INTR_CLR_MASK);
	reg_write16(base + MTK_I2C_TRANSFER_LEN, 2U);
	reg_write16(base + MTK_I2C_TRANSAC_LEN, 1U);
	reg_write16(base + MTK_I2C_DATA_PORT, reg);
	reg_write16(base + MTK_I2C_DATA_PORT, value);
	reg_write16(base + MTK_I2C_START, MTK_I2C_START_TRANSAC);

	for (spins = 0; spins < 500000U; spins++) {
		status = reg_read16(base + MTK_I2C_INTR_STAT);
		irq_info = reg_read16(base + MTK_I2C_IRQ_INFO);
		if (status == 0U && irq_info != 0U) {
			status = irq_info;
		}
		if ((status & (uint16_t) (MTK_I2C_BUS_ERR | MTK_I2C_TIMEOUT |
					  MTK_I2C_HS_NACKERR | MTK_I2C_ACKERR |
					  MTK_I2C_ARB_LOST | MTK_I2C_DMAERR)) != 0U) {
			g_last_i2c_error = -3;
			g_last_i2c_status = (uint32_t) status;
			g_last_i2c_debug0 = (uint32_t) irq_info;
			g_last_i2c_debug1 = reg_read16(base + MTK_I2C_FIFO_STAT);
			reg_write16(base + MTK_I2C_INTR_MASK, 0U);
			reg_write16(base + MTK_I2C_INTR_STAT, MTK_I2C_INTR_CLR_MASK);
			return -1;
		}
		if ((status & (uint16_t) MTK_I2C_TRANSAC_COMP) != 0U) {
			reg_write16(base + MTK_I2C_INTR_MASK, 0U);
			reg_write16(base + MTK_I2C_INTR_STAT, MTK_I2C_INTR_CLR_MASK);
			return 0;
		}
		__asm__ volatile("" ::: "memory");
	}

	g_last_i2c_error = -4;
	g_last_i2c_status = (uint32_t) reg_read16(base + MTK_I2C_INTR_STAT);
	g_last_i2c_debug0 = reg_read16(base + MTK_I2C_IRQ_INFO);
	g_last_i2c_debug1 = reg_read16(base + MTK_I2C_FIFO_STAT);
	reg_write16(base + MTK_I2C_INTR_MASK, 0U);
	reg_write16(base + MTK_I2C_INTR_STAT, MTK_I2C_INTR_CLR_MASK);
	return -1;
}

int mk_stage0_mtk_i2c_last_error(void)
{
	return g_last_i2c_error;
}

uint32_t mk_stage0_mtk_i2c_last_status(void)
{
	return g_last_i2c_status;
}

uint32_t mk_stage0_mtk_i2c_last_debug0(void)
{
	return g_last_i2c_debug0;
}

uint32_t mk_stage0_mtk_i2c_last_debug1(void)
{
	return g_last_i2c_debug1;
}
