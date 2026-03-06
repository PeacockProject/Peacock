#include "mtk_pwm.h"

#define MTK_TOPCKGEN_BASE 0x10000000ULL
#define MTK_TOP_CLK_CFG_4_CLR 0x0088U
#define MTK_TOP_CLK_CFG_UPDATE 0x0004U
#define MTK_TOP_DISP_PWM_SEL_CLR_MASK 0x83000000U
#define MTK_TOP_DISP_PWM_UPDATE_BIT (1U << 19)

#define MTK_INFRACFG_AO_BASE 0x10001000ULL
#define MTK_IFR4_CLR 0x00a8U
#define MTK_IFR_DISP_PWM_BIT (1U << 2)

#define MTK_GPIO_BASE 0x10005000ULL
#define MTK_GPIO_MODE_BASE 0x0300U
#define MTK_GPIO_GROUP_STRIDE 0x0010U
#define MTK_PWM0_GPIO 90U
#define MTK_PWM0_MODE 2U

#define MTK_DISP_PWM0_BASE 0x1100e000ULL
#define MTK_DISP_PWM_EN_OFF 0x00U
#define MTK_DISP_PWM_COMMIT_OFF 0x0cU
#define MTK_DISP_PWM_CON_0_OFF 0x18U
#define MTK_DISP_PWM_CON_1_OFF 0x1cU
#define MTK_DISP_PWM_DEBUG_OFF 0x80U

static uint32_t reg_read32(uint64_t addr)
{
	return *(volatile uint32_t *) (uintptr_t) addr;
}

static void reg_write32(uint64_t addr, uint32_t value)
{
	*(volatile uint32_t *) (uintptr_t) addr = value;
}

static void reg_rmw32(uint64_t addr, uint32_t clear_mask, uint32_t set_mask)
{
	uint32_t value;

	value = reg_read32(addr);
	value &= ~clear_mask;
	value |= set_mask;
	reg_write32(addr, value);
}

static void topckgen_select_disp_pwm_26m(void)
{
	/*
	 * CLK_TOP_DISP_PWM_SEL:
	 * bits [25:24] mux, bit [31] gate. Clear all three => 26MHz source, ungated.
	 */
	reg_write32(MTK_TOPCKGEN_BASE + MTK_TOP_CLK_CFG_4_CLR, MTK_TOP_DISP_PWM_SEL_CLR_MASK);
	reg_write32(MTK_TOPCKGEN_BASE + MTK_TOP_CLK_CFG_UPDATE, MTK_TOP_DISP_PWM_UPDATE_BIT);
}

static void infracfg_enable_disp_pwm(void)
{
	/* GATE_IFR4 uses setclr ops; write CLR to ungate CLK_IFR_DISP_PWM. */
	reg_write32(MTK_INFRACFG_AO_BASE + MTK_IFR4_CLR, MTK_IFR_DISP_PWM_BIT);
}

static void gpio90_set_pwm0_mode(void)
{
	uint32_t group;
	uint32_t shift;
	uint64_t addr;
	uint32_t value;

	group = MTK_PWM0_GPIO / 8U;
	shift = (MTK_PWM0_GPIO % 8U) * 4U;
	addr = MTK_GPIO_BASE + MTK_GPIO_MODE_BASE + ((uint64_t) group * MTK_GPIO_GROUP_STRIDE);
	value = reg_read32(addr);
	value &= ~(0xfU << shift);
	value |= (MTK_PWM0_MODE & 0xfU) << shift;
	reg_write32(addr, value);
}

void mk_stage0_mtk_pwm0_enable(uint32_t level_1024, uint32_t pwm_div)
{
	if (level_1024 == 0U) {
		level_1024 = 1U;
	}
	if (level_1024 > 1023U) {
		level_1024 = 1023U;
	}
	if (pwm_div > 0x3ffU) {
		pwm_div = 0x3ffU;
	}

	topckgen_select_disp_pwm_26m();
	infracfg_enable_disp_pwm();
	gpio90_set_pwm0_mode();

	/* 1024-step period in low bits, requested duty in high bits. */
	reg_rmw32(MTK_DISP_PWM0_BASE + MTK_DISP_PWM_CON_0_OFF, 0x03ff0000U, pwm_div << 16);
	reg_rmw32(MTK_DISP_PWM0_BASE + MTK_DISP_PWM_CON_1_OFF, 0x000003ffU, 1023U);
	reg_rmw32(MTK_DISP_PWM0_BASE + MTK_DISP_PWM_CON_1_OFF, 0x1fff0000U, level_1024 << 16);

	/* Vendor test path toggles DEBUG[1:0] to 3 before commit. */
	reg_rmw32(MTK_DISP_PWM0_BASE + MTK_DISP_PWM_DEBUG_OFF, 0x3U, 0x3U);
	reg_rmw32(MTK_DISP_PWM0_BASE + MTK_DISP_PWM_EN_OFF, 0x1U, 0x1U);
	reg_write32(MTK_DISP_PWM0_BASE + MTK_DISP_PWM_COMMIT_OFF, 1U);
	reg_write32(MTK_DISP_PWM0_BASE + MTK_DISP_PWM_COMMIT_OFF, 0U);
}
