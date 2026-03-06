#include "mtk_dsi.h"

#define MTK_DSI0_BASE 0x14014000ULL
#define MTK_MIPITX0_BASE 0x11c80000ULL
#define MTK_DSI_START 0x0000U
#define MTK_DSI_COM_CTRL 0x0010U
#define MTK_DSI_MODE_CTRL 0x0014U
#define MTK_DSI_TXRX_CTRL 0x0018U
#define MTK_DSI_PSCTRL 0x001cU
#define MTK_DSI_VSA_NL 0x0020U
#define MTK_DSI_VBP_NL 0x0024U
#define MTK_DSI_VFP_NL 0x0028U
#define MTK_DSI_VACT_NL 0x002cU
#define MTK_DSI_SIZE_CON 0x0038U
#define MTK_DSI_HSA_WC 0x0050U
#define MTK_DSI_HBP_WC 0x0054U
#define MTK_DSI_HFP_WC 0x0058U
#define MTK_DSI_INTSTA 0x000cU
#define MTK_DSI_CMDQ_SIZE 0x0060U
#define MTK_DSI_MEM_CONTI 0x0090U
#define MTK_DSI_PHY_LCCON 0x0104U
#define MTK_DSI_PHY_TIMECON0 0x0110U
#define MTK_DSI_PHY_TIMECON1 0x0114U
#define MTK_DSI_PHY_TIMECON2 0x0118U
#define MTK_DSI_PHY_TIMECON3 0x011cU
#define MTK_DSI_SHADOW_DEBUG 0x0190U
#define MTK_DSI_CMDQ_BASE 0x0200U

#define MTK_MIPITX_LANE_CON 0x000cU
#define MTK_MIPITX_PLL_PWR 0x0028U
#define MTK_MIPITX_PLL_CON0 0x002cU
#define MTK_MIPITX_PLL_CON1 0x0030U
#define MTK_MIPITX_PLL_CON4 0x003cU
#define MTK_MIPITX_PHY_SEL0 0x0040U
#define MTK_MIPITX_PHY_SEL1 0x0044U
#define MTK_MIPITX_PHY_SEL2 0x0048U
#define MTK_MIPITX_PHY_SEL3 0x004cU
#define MTK_MIPITX_SW_CTRL_CON4 0x0060U
#define MTK_MIPITX_APB_ASYNC_STA 0x0078U
#define MTK_MIPITX_D2_CKMODE_EN 0x0128U
#define MTK_MIPITX_D0_CKMODE_EN 0x0228U
#define MTK_MIPITX_CK_CKMODE_EN 0x0328U
#define MTK_MIPITX_D1_CKMODE_EN 0x0428U
#define MTK_MIPITX_D3_CKMODE_EN 0x0528U

#define MTK_DSI_INTSTA_BUSY (1U << 31)
#define MTK_DSI_COM_CTRL_DSI_RESET (1U << 0)
#define MTK_DSI_COM_CTRL_DPHY_RESET (1U << 2)
#define MTK_DSI_TXRX_HSTX_CKLP_EN (1U << 16)
#define MTK_DSI_PHY_LCCON_LC_HS_TX_EN (1U << 0)
#define MTK_DSI_SHADOW_FORCE_COMMIT (1U << 0)
#define MTK_DSI_SHADOW_BYPASS (1U << 1)
#define MTK_DSI_PACKED_PS_RGB888 2U
#define MTK_DSI_WMEM_CONTI 0x3cU
#define MTK_MIPITX_PLL_PWR_ON (1U << 0)
#define MTK_MIPITX_PLL_ISO_EN (1U << 1)
#define MTK_MIPITX_PLL_EN (1U << 4)
#define MTK_MIPITX_PLL_POSDIV_SHIFT 8U
#define MTK_MIPITX_PLL_POSDIV_MASK (0x7U << MTK_MIPITX_PLL_POSDIV_SHIFT)
#define MTK_MIPITX_SW_ANA_CK_EN (1U << 8)
#define MTK_DSI_DCS_SHORT_PACKET_ID_0 0x05U
#define MTK_DSI_DCS_SHORT_PACKET_ID_1 0x15U
#define MTK_DSI_DCS_LONG_PACKET_ID 0x39U
#define MTK_DSI_GENERIC_SHORT_PACKET_ID_1 0x13U
#define MTK_DSI_GENERIC_SHORT_PACKET_ID_2 0x23U
#define MTK_DSI_GENERIC_LONG_PACKET_ID 0x29U

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

static uint32_t pad_value_for_lane(uint32_t lane)
{
	switch (lane) {
	case 0U:
		return 2U; /* PAD_D0P_V */
	case 1U:
		return 6U; /* PAD_D1P_V */
	case 2U:
		return 0U; /* PAD_D2P_V */
	case 3U:
		return 8U; /* PAD_D3P_V */
	case 4U:
	case 5U:
	default:
		return 4U; /* PAD_CKP_V */
	}
}

static void apply_lane_swap(const mk_stage0_panel_t *panel)
{
	uint32_t l0;
	uint32_t l1;
	uint32_t l2;
	uint32_t l3;
	uint32_t ck;
	uint32_t rx;
	uint32_t p0;
	uint32_t p1;
	uint32_t p2;
	uint32_t p3;
	uint32_t pc;
	uint32_t prx;
	uint64_t ckmode_addr;
	uint32_t phy_sel0;
	uint32_t phy_sel1;
	uint32_t phy_sel2;
	uint32_t phy_sel3;

	if (panel == 0 || panel->lane_swap_enable == 0U) {
		reg_write32(MTK_MIPITX0_BASE + MTK_MIPITX_CK_CKMODE_EN, 1U);
		return;
	}

	l0 = panel->lane_swap[0];
	l1 = panel->lane_swap[1];
	l2 = panel->lane_swap[2];
	l3 = panel->lane_swap[3];
	ck = panel->lane_swap[4];
	rx = panel->lane_swap[5];

	p0 = pad_value_for_lane(l0);
	p1 = pad_value_for_lane(l1);
	p2 = pad_value_for_lane(l2);
	p3 = pad_value_for_lane(l3);
	pc = pad_value_for_lane(ck);
	prx = pad_value_for_lane(rx);

	switch (ck) {
	case 0U:
		ckmode_addr = MTK_MIPITX0_BASE + MTK_MIPITX_D0_CKMODE_EN;
		break;
	case 1U:
		ckmode_addr = MTK_MIPITX0_BASE + MTK_MIPITX_D1_CKMODE_EN;
		break;
	case 2U:
		ckmode_addr = MTK_MIPITX0_BASE + MTK_MIPITX_D2_CKMODE_EN;
		break;
	case 3U:
		ckmode_addr = MTK_MIPITX0_BASE + MTK_MIPITX_D3_CKMODE_EN;
		break;
	case 4U:
	default:
		ckmode_addr = MTK_MIPITX0_BASE + MTK_MIPITX_CK_CKMODE_EN;
		break;
	}
	reg_write32(ckmode_addr, 1U);

	phy_sel0 = (p1 << 28) |
		   (((pc + 1U) & 0xfU) << 24) |
		   ((pc & 0xfU) << 20) |
		   (((p0 + 1U) & 0xfU) << 16) |
		   ((p0 & 0xfU) << 12) |
		   (((p2 + 1U) & 0xfU) << 8) |
		   ((p2 & 0xfU) << 4);
	phy_sel1 = (((prx + 1U) & 0xfU) << 16) |
		   ((prx & 0xfU) << 12) |
		   (((p3 + 1U) & 0xfU) << 8) |
		   ((p3 & 0xfU) << 4);
	phy_sel2 = ((p2 & 0xfU) << 28) |
		   ((p1 & 0xfU) << 24) |
		   ((pc & 0xfU) << 16) |
		   ((p0 & 0xfU) << 8);
	phy_sel3 = p3 & 0xfU;

	reg_write32(MTK_MIPITX0_BASE + MTK_MIPITX_PHY_SEL0, phy_sel0);
	reg_write32(MTK_MIPITX0_BASE + MTK_MIPITX_PHY_SEL1, phy_sel1);
	reg_write32(MTK_MIPITX0_BASE + MTK_MIPITX_PHY_SEL2, phy_sel2);
	reg_write32(MTK_MIPITX0_BASE + MTK_MIPITX_PHY_SEL3, phy_sel3);
}

static int dsi_wait_not_busy(void)
{
	uint32_t spins;

	for (spins = 0; spins < 500000U; spins++) {
		if ((reg_read32(MTK_DSI0_BASE + MTK_DSI_INTSTA) & MTK_DSI_INTSTA_BUSY) == 0U) {
			return 0;
		}
		__asm__ volatile("" ::: "memory");
	}

	return -1;
}

static uint32_t lane_mask(uint32_t lanes)
{
	switch (lanes) {
	case 1U:
		return 0x1U;
	case 2U:
		return 0x3U;
	case 3U:
		return 0x7U;
	case 4U:
	default:
		return 0xfU;
	}
}

static uint32_t pack_psctrl(const mk_stage0_panel_t *panel)
{
	uint32_t ps_wc;

	ps_wc = panel->fb_width * 3U;
	return (ps_wc & 0x7fffU) | (MTK_DSI_PACKED_PS_RGB888 << 16);
}

static uint32_t pack_txrx_ctrl(const mk_stage0_panel_t *panel)
{
	uint32_t value;

	value = 0U;
	value |= (lane_mask(panel->dsi_lanes) & 0xfU) << 2;
	value |= MTK_DSI_TXRX_HSTX_CKLP_EN;
	return value;
}

static uint32_t pack_size_con(const mk_stage0_panel_t *panel)
{
	return ((panel->fb_height & 0x7fffU) << 16) |
	       (panel->fb_width & 0x7fffU);
}

static uint32_t calc_hsync_word_count(uint32_t pixels)
{
	uint32_t bytes;

	bytes = pixels * 3U;
	if (bytes > 10U) {
		return bytes - 10U;
	}

	return bytes;
}

static uint32_t calc_hfront_word_count(uint32_t pixels)
{
	uint32_t bytes;

	bytes = pixels * 3U;
	if (bytes > 12U) {
		return bytes - 12U;
	}

	return bytes;
}

static uint32_t calc_pcw(uint32_t data_rate, uint32_t pcw_ratio)
{
	uint32_t pcw;
	uint32_t rem;

	pcw = data_rate * pcw_ratio / 26U;
	rem = data_rate * pcw_ratio % 26U;

	return ((pcw & 0xffU) << 24) |
	       (((256U * rem / 26U) & 0xffU) << 16) |
	       (((256U * ((256U * rem) % 26U) / 26U) & 0xffU) << 8) |
	       ((256U * ((256U * ((256U * rem) % 26U)) % 26U) / 26U) & 0xffU);
}

static void select_pll_dividers(uint32_t data_rate, uint32_t *pcw_ratio, uint32_t *posdiv)
{
	if (data_rate >= 2000U) {
		*pcw_ratio = 1U;
		*posdiv = 0U;
	} else if (data_rate >= 1000U) {
		*pcw_ratio = 2U;
		*posdiv = 1U;
	} else if (data_rate >= 500U) {
		*pcw_ratio = 4U;
		*posdiv = 2U;
	} else if (data_rate > 250U) {
		*pcw_ratio = 8U;
		*posdiv = 3U;
	} else {
		*pcw_ratio = 16U;
		*posdiv = 4U;
	}
}

static uint32_t ns_to_cycle(uint32_t ns_scaled, uint32_t cycle_time)
{
	if (cycle_time == 0U) {
		return 0U;
	}

	return ns_scaled / cycle_time;
}

static void dsi_program_phy_timing(const mk_stage0_panel_t *panel)
{
	uint32_t temp_data_rate;
	uint32_t cycle_time;
	uint32_t ui;
	uint32_t hs_trail_n;
	uint32_t hs_trail;
	uint32_t hs_prpr;
	uint32_t hs_zero;
	uint32_t lpx;
	uint32_t ta_get;
	uint32_t ta_sure;
	uint32_t ta_go;
	uint32_t da_hs_exit;
	uint32_t clk_trail;
	uint32_t clk_zero;
	uint32_t clk_hs_prpr;
	uint32_t clk_hs_post;
	uint32_t clk_hs_exit;
	uint32_t timcon0;
	uint32_t timcon1;
	uint32_t timcon2;
	uint32_t timcon3;

	temp_data_rate = panel->dsi_pll_clock_cmd * 2U;
	if (temp_data_rate == 0U) {
		return;
	}

	ui = 1000U / temp_data_rate + 1U;
	cycle_time = 8000U / temp_data_rate + 1U;

	hs_trail_n = ns_to_cycle((((4U * ui) + 0x50U) * temp_data_rate), 0x1f40U) + 1U;
	hs_trail = hs_trail_n < 1U ? 1U : hs_trail_n;

	hs_prpr = ns_to_cycle((0x40U + 5U * ui), cycle_time) + 1U;
	if (hs_prpr < 1U) {
		hs_prpr = 1U;
	}

	hs_zero = ns_to_cycle((0xc8U + 10U * ui), cycle_time);
	if (hs_prpr < hs_zero) {
		hs_zero -= hs_prpr;
	}

	lpx = ns_to_cycle(temp_data_rate * 0x4bU, 0x1f40U) + 1U;
	if (lpx < 1U) {
		lpx = 1U;
	}

	ta_get = 5U * lpx;
	ta_sure = (3U * lpx) / 2U;
	ta_go = 4U * lpx;
	da_hs_exit = 2U * lpx;

	clk_trail = ns_to_cycle(0x64U * temp_data_rate, 0x1f40U) + 1U;
	if (clk_trail < 2U) {
		clk_trail = 2U;
	}

	clk_zero = ns_to_cycle(0x190U, cycle_time);
	clk_hs_prpr = ns_to_cycle(0x50U * temp_data_rate, 0x1f40U);
	if (clk_hs_prpr < 1U) {
		clk_hs_prpr = 1U;
	}
	clk_hs_post = ns_to_cycle((0x60U + 0x34U * ui), cycle_time);
	clk_hs_exit = 2U * lpx;

	timcon0 = (lpx & 0xffU) |
		  ((hs_prpr & 0xffU) << 8) |
		  ((hs_zero & 0xffU) << 16) |
		  ((hs_trail & 0xffU) << 24);
	timcon1 = (ta_go & 0xffU) |
		  ((ta_sure & 0xffU) << 8) |
		  ((ta_get & 0xffU) << 16) |
		  ((da_hs_exit & 0xffU) << 24);
	timcon2 = (0U & 0xffU) |
		  ((1U & 0xffU) << 8) |
		  ((clk_zero & 0xffU) << 16) |
		  ((clk_trail & 0xffU) << 24);
	timcon3 = (clk_hs_prpr & 0xffU) |
		  ((clk_hs_post & 0xffU) << 8) |
		  ((clk_hs_exit & 0xffU) << 16);

	reg_write32(MTK_DSI0_BASE + MTK_DSI_PHY_TIMECON0, timcon0);
	reg_write32(MTK_DSI0_BASE + MTK_DSI_PHY_TIMECON1, timcon1);
	reg_write32(MTK_DSI0_BASE + MTK_DSI_PHY_TIMECON2, timcon2);
	reg_write32(MTK_DSI0_BASE + MTK_DSI_PHY_TIMECON3, timcon3);
}

static int mipi_dphy_power_on(const mk_stage0_panel_t *panel)
{
	uint32_t data_rate;
	uint32_t pcw_ratio;
	uint32_t posdiv;
	uint32_t pll_con1;

	if (panel == 0) {
		return -1;
	}

	data_rate = panel->dsi_pll_clock_cmd * 2U;
	if (data_rate < 125U || data_rate > 2500U) {
		return -1;
	}

	apply_lane_swap(panel);
	reg_write32(MTK_MIPITX0_BASE + MTK_MIPITX_PLL_CON4, 0x00ff12e0U);
	reg_write32(MTK_MIPITX0_BASE + MTK_MIPITX_LANE_CON, 0x3fff0180U);
	for (volatile uint32_t spins = 0; spins < 50000U; spins++) {
		__asm__ volatile("" ::: "memory");
	}
	reg_write32(MTK_MIPITX0_BASE + MTK_MIPITX_LANE_CON, 0x3fff0080U);

	reg_rmw32(MTK_MIPITX0_BASE + MTK_MIPITX_PLL_PWR, 0U, MTK_MIPITX_PLL_PWR_ON);
	for (volatile uint32_t spins = 0; spins < 5000U; spins++) {
		__asm__ volatile("" ::: "memory");
	}
	reg_rmw32(MTK_MIPITX0_BASE + MTK_MIPITX_PLL_PWR, MTK_MIPITX_PLL_ISO_EN, 0U);

	select_pll_dividers(data_rate, &pcw_ratio, &posdiv);
	reg_write32(MTK_MIPITX0_BASE + MTK_MIPITX_PLL_CON0, calc_pcw(data_rate, pcw_ratio));

	pll_con1 = reg_read32(MTK_MIPITX0_BASE + MTK_MIPITX_PLL_CON1);
	pll_con1 &= ~MTK_MIPITX_PLL_POSDIV_MASK;
	pll_con1 |= (posdiv << MTK_MIPITX_PLL_POSDIV_SHIFT) & MTK_MIPITX_PLL_POSDIV_MASK;
	pll_con1 |= MTK_MIPITX_PLL_EN;
	reg_write32(MTK_MIPITX0_BASE + MTK_MIPITX_PLL_CON1, pll_con1);

	for (volatile uint32_t spins = 0; spins < 200000U; spins++) {
		__asm__ volatile("" ::: "memory");
	}
	reg_rmw32(MTK_MIPITX0_BASE + MTK_MIPITX_SW_CTRL_CON4, 0U, MTK_MIPITX_SW_ANA_CK_EN);

	dsi_program_phy_timing(panel);
	(void) reg_read32(MTK_MIPITX0_BASE + MTK_MIPITX_APB_ASYNC_STA);
	return 0;
}

int mk_stage0_mtk_dsi_host_init(const mk_stage0_panel_t *panel)
{
	if (panel == 0 || panel->fb_width == 0U || panel->fb_height == 0U) {
		return -1;
	}

	if (mipi_dphy_power_on(panel) != 0) {
		return -1;
	}

	reg_write32(MTK_DSI0_BASE + MTK_DSI_SHADOW_DEBUG,
		    MTK_DSI_SHADOW_FORCE_COMMIT | MTK_DSI_SHADOW_BYPASS);

	reg_write32(MTK_DSI0_BASE + MTK_DSI_COM_CTRL,
		    MTK_DSI_COM_CTRL_DSI_RESET | MTK_DSI_COM_CTRL_DPHY_RESET);
	reg_write32(MTK_DSI0_BASE + MTK_DSI_COM_CTRL, 0U);

	reg_write32(MTK_DSI0_BASE + MTK_DSI_MODE_CTRL, (uint32_t) MK_STAGE0_DSI_MODE_CMD);
	reg_write32(MTK_DSI0_BASE + MTK_DSI_TXRX_CTRL, pack_txrx_ctrl(panel));
	reg_write32(MTK_DSI0_BASE + MTK_DSI_PSCTRL, pack_psctrl(panel));
	reg_write32(MTK_DSI0_BASE + MTK_DSI_VSA_NL, panel->dsi_vsync_active);
	reg_write32(MTK_DSI0_BASE + MTK_DSI_VBP_NL, panel->dsi_vback_porch);
	reg_write32(MTK_DSI0_BASE + MTK_DSI_VFP_NL, panel->dsi_vfront_porch);
	reg_write32(MTK_DSI0_BASE + MTK_DSI_VACT_NL, panel->fb_height);
	reg_write32(MTK_DSI0_BASE + MTK_DSI_SIZE_CON, pack_size_con(panel));
	reg_write32(MTK_DSI0_BASE + MTK_DSI_HSA_WC,
		    calc_hsync_word_count(panel->dsi_hsync_active));
	reg_write32(MTK_DSI0_BASE + MTK_DSI_HBP_WC,
		    calc_hsync_word_count(panel->dsi_hback_porch));
	reg_write32(MTK_DSI0_BASE + MTK_DSI_HFP_WC,
		    calc_hfront_word_count(panel->dsi_hfront_porch));
	reg_write32(MTK_DSI0_BASE + MTK_DSI_MEM_CONTI, MTK_DSI_WMEM_CONTI);
	reg_write32(MTK_DSI0_BASE + MTK_DSI_PHY_LCCON, 0U);
	reg_write32(MTK_DSI0_BASE + MTK_DSI_PHY_LCCON, MTK_DSI_PHY_LCCON_LC_HS_TX_EN);
	reg_write32(MTK_DSI0_BASE + MTK_DSI_CMDQ_SIZE, 0U);
	reg_write32(MTK_DSI0_BASE + MTK_DSI_START, 0U);

	return dsi_wait_not_busy();
}

void mk_stage0_mtk_dsi_set_mode(mk_stage0_dsi_mode_t mode)
{
	reg_write32(MTK_DSI0_BASE + MTK_DSI_MODE_CTRL, (uint32_t) mode);
}

static int dsi_write_t0(uint8_t data_id, uint8_t data0, uint8_t data1)
{
	uint32_t t0;

	if (dsi_wait_not_busy() != 0) {
		return -1;
	}

	t0 = ((uint32_t) data1 << 24) |
	     ((uint32_t) data0 << 16) |
	     ((uint32_t) data_id << 8);

	reg_write32(MTK_DSI0_BASE + MTK_DSI_CMDQ_BASE, t0);
	reg_write32(MTK_DSI0_BASE + MTK_DSI_CMDQ_SIZE, 1U);
	reg_write32(MTK_DSI0_BASE + MTK_DSI_START, 0U);
	reg_write32(MTK_DSI0_BASE + MTK_DSI_START, 1U);

	return dsi_wait_not_busy();
}

int mk_stage0_mtk_dsi_write(uint8_t cmd, uint8_t count, const uint8_t *data)
{
	uint32_t data_id;
	uint32_t header;
	uint32_t payload_word;
	uint32_t byte_index;
	uint32_t payload_bytes;
	uint32_t payload_words;
	uint32_t total_words;
	uint32_t word_index;
	uint8_t byte_value;

	if (count != 0U && data == 0) {
		return -1;
	}

	if (count > 1U) {
		data_id = MTK_DSI_DCS_LONG_PACKET_ID;
	} else if (count == 1U) {
		return dsi_write_t0(MTK_DSI_DCS_SHORT_PACKET_ID_1, cmd, data[0]);
	} else {
		return dsi_write_t0(MTK_DSI_DCS_SHORT_PACKET_ID_0, cmd, 0U);
	}

	if (dsi_wait_not_busy() != 0) {
		return -1;
	}

	header = (uint32_t) (((uint32_t) (count + 1U) << 16) |
			     (data_id << 8) |
			     0x02U);
	reg_write32(MTK_DSI0_BASE + MTK_DSI_CMDQ_BASE, header);

	payload_bytes = (uint32_t) count + 1U;
	payload_words = (payload_bytes + 3U) / 4U;
	total_words = 1U + payload_words;

	for (word_index = 0; word_index < payload_words; word_index++) {
		payload_word = 0U;
		for (byte_index = 0; byte_index < 4U; byte_index++) {
			uint32_t global_index = word_index * 4U + byte_index;
			if (global_index >= payload_bytes) {
				break;
			}
			if (global_index == 0U) {
				byte_value = cmd;
			} else {
				byte_value = data[global_index - 1U];
			}
			payload_word |= (uint32_t) byte_value << (byte_index * 8U);
		}
		reg_write32(MTK_DSI0_BASE + MTK_DSI_CMDQ_BASE + ((uint64_t) (word_index + 1U) * 4ULL),
			    payload_word);
	}

	reg_write32(MTK_DSI0_BASE + MTK_DSI_CMDQ_SIZE, total_words);
	reg_write32(MTK_DSI0_BASE + MTK_DSI_START, 0U);
	reg_write32(MTK_DSI0_BASE + MTK_DSI_START, 1U);

	return dsi_wait_not_busy();
}

int mk_stage0_mtk_dsi_dcs_write0(uint8_t cmd)
{
	return mk_stage0_mtk_dsi_write(cmd, 0U, 0);
}

int mk_stage0_mtk_dsi_dcs_write1(uint8_t cmd, uint8_t value)
{
	return mk_stage0_mtk_dsi_write(cmd, 1U, &value);
}
