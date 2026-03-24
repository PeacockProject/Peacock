#include <stdint.h>
#include "mk_common.h"
#include "mk_pmic.h"
#include "mk_fdt.h"

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

/* Charger detection */
#define MT6357_CHR_TOP_CON0_ADDR 0xa88U
#define MT6357_RGS_CHRDET_SHIFT 4U

/* Power control */
#define MT6357_PPCCTL0_ADDR 0xa08U
#define MT6357_RG_PWRHOLD_SHIFT 0U
#define MT6357_STRUP_CON7_ADDR 0xa22U
#define MT6357_STRUP_PWROFF_SEQ_EN_SHIFT 0U
#define MT6357_STRUP_PWROFF_PREOFF_EN_SHIFT 1U

/* PONSTS bits */
#define MT6357_PONSTS_PWRKEY_BIT 0U
#define MT6357_PONSTS_CHRIN_BIT 2U

static int pwrap_wait_idle(void)
{
	uint32_t i;
	for (i = 0; i < 1000U; i++) {
		uint32_t val = mmio_read32(MTK_PMIC_WRAP_BASE + PWRAP_WACS2_RDATA);
		if ((val & PWRAP_WACS_FSM_MASK) == PWRAP_WACS_FSM_IDLE) {
			return 0;
		}
	}
	return -1;
}

static int pwrap_wait_vldclr(uint16_t *rdata)
{
	uint32_t i;
	for (i = 0; i < 1000U; i++) {
		uint32_t val = mmio_read32(MTK_PMIC_WRAP_BASE + PWRAP_WACS2_RDATA);
		if ((val & PWRAP_WACS_FSM_MASK) == PWRAP_WACS_FSM_WFVLDCLR) {
			*rdata = (uint16_t) (val & 0xffffU);
			return 0;
		}
	}
	return -1;
}

int mk_pmic_pwrap_write16(uint32_t adr, uint16_t wdata)
{
	if (mmio_read32(MTK_PMIC_WRAP_BASE + PWRAP_INIT_DONE2) == 0U) {
		return -1;
	}
	{
		uint32_t val = mmio_read32(MTK_PMIC_WRAP_BASE + PWRAP_WACS2_RDATA);
		if ((val & PWRAP_WACS_FSM_MASK) == PWRAP_WACS_FSM_WFVLDCLR) {
			mmio_write32(MTK_PMIC_WRAP_BASE + PWRAP_WACS2_VLDCLR, 1U);
		}
	}
	if (pwrap_wait_idle() != 0) {
		return -1;
	}

	mmio_write32(MTK_PMIC_WRAP_BASE + PWRAP_WACS2_CMD,
		     (1U << 31) | ((adr >> 1U) << 16) | wdata);
	return 0;
}

int mk_pmic_pwrap_read16(uint32_t adr, uint16_t *rdata)
{
	uint32_t val;

	if (rdata == 0) {
		return -1;
	}
	if (mmio_read32(MTK_PMIC_WRAP_BASE + PWRAP_INIT_DONE2) == 0U) {
		return -1;
	}
	val = mmio_read32(MTK_PMIC_WRAP_BASE + PWRAP_WACS2_RDATA);
	if ((val & PWRAP_WACS_FSM_MASK) == PWRAP_WACS_FSM_WFVLDCLR) {
		mmio_write32(MTK_PMIC_WRAP_BASE + PWRAP_WACS2_VLDCLR, 1U);
	}
	if (pwrap_wait_idle() != 0) {
		return -1;
	}

	mmio_write32(MTK_PMIC_WRAP_BASE + PWRAP_WACS2_CMD, ((adr >> 1U) << 16));
	if (pwrap_wait_vldclr(rdata) != 0) {
		return -1;
	}
	mmio_write32(MTK_PMIC_WRAP_BASE + PWRAP_WACS2_VLDCLR, 1U);
	return 0;
}

uint8_t mk_pmic_power_pressed(void)
{
	uint16_t topstatus = 0U;
	uint8_t pwr_deb;

	if (mk_pmic_pwrap_read16(MT6357_TOPSTATUS_ADDR, &topstatus) != 0) {
		return 0U;
	}

	pwr_deb = (uint8_t) ((topstatus >> MT6357_PWRKEY_DEB_SHIFT) & 0x1U);
	return pwr_deb == 0U ? 1U : 0U;
}

uint8_t mk_pmic_homekey_pressed(void)
{
	uint16_t topstatus = 0U;
	uint8_t home_deb;

	if (mk_pmic_pwrap_read16(MT6357_TOPSTATUS_ADDR, &topstatus) != 0) {
		return 0U;
	}

	home_deb = (uint8_t) ((topstatus >> MT6357_HOMEKEY_DEB_SHIFT) & 0x1U);
	return home_deb == 0U ? 1U : 0U;
}

uint8_t mk_pmic_charger_connected(void)
{
	uint16_t chr_top = 0U;

	if (mk_pmic_pwrap_read16(MT6357_CHR_TOP_CON0_ADDR, &chr_top) != 0) {
		return 0U;
	}
	return (uint8_t) ((chr_top >> MT6357_RGS_CHRDET_SHIFT) & 0x1U);
}

static uint8_t bootargs_has_value(const char *bootargs, const char *key, const char *val)
{
	uint32_t key_len = mk_strlen(key);
	uint32_t val_len = mk_strlen(val);
	uint32_t i;

	if (bootargs == 0 || key_len == 0 || val_len == 0) {
		return 0U;
	}
	for (i = 0; bootargs[i] != '\0'; i++) {
		uint32_t j = 0;

		if (i != 0 && bootargs[i - 1] != ' ') {
			continue;
		}
		while (j < key_len && bootargs[i + j] == key[j]) {
			j++;
		}
		if (j != key_len) {
			continue;
		}
		/* Match value */
		j = 0;
		while (j < val_len && bootargs[i + key_len + j] == val[j]) {
			j++;
		}
		if (j == val_len) {
			char next = bootargs[i + key_len + val_len];
			if (next == '\0' || next == ' ') {
				return 1U;
			}
		}
	}
	return 0U;
}

uint8_t mk_pmic_boot_is_charger_only(const void *fdt)
{
	const uint8_t *value = 0;
	uint32_t len = 0;
	const char *bootargs;

	/*
	 * Method 1: FDT /chosen "atag,boot" property
	 *   struct { u32 size, u32 tag, u32 bootmode, u32 boottype } (BE)
	 *   Boot mode 8 = KERNEL_POWER_OFF_CHARGING_BOOT
	 *   Boot mode 9 = LOW_POWER_OFF_CHARGING_BOOT
	 */
	if (mk_fdt_find_chosen_prop(fdt, "atag,boot", &value, &len) && len >= 12U) {
		uint32_t bootmode = be32_read(value + 8U);
		uart_puts_all("[mk] atag,boot bootmode=0x");
		uart_puthex64_all((uint64_t) bootmode);
		uart_puts_all("\r\n");
		if (bootmode == 8U || bootmode == 9U) {
			return 1U;
		}
	} else {
		uart_puts_all("[mk] atag,boot not found\r\n");
	}

	/*
	 * Method 2: bootargs "androidboot.mode=charger"
	 */
	bootargs = mk_fdt_find_chosen_string(fdt, "bootargs");
	if (bootargs != 0) {
		if (bootargs_has_value(bootargs, "androidboot.mode=", "charger")) {
			uart_puts_all("[mk] bootargs: androidboot.mode=charger\r\n");
			return 1U;
		}
	}

	/*
	 * Method 3: PONSTS fallback — charger insertion set, power key not set
	 */
	{
		uint16_t ponsts = 0U;
		if (mk_pmic_pwrap_read16(MT6357_PONSTS_ADDR, &ponsts) == 0) {
			uart_puts_all("[mk] ponsts=0x");
			uart_puthex64_all((uint64_t) ponsts);
			uart_puts_all("\r\n");
			if ((ponsts & (1U << MT6357_PONSTS_CHRIN_BIT)) != 0U &&
			    (ponsts & (1U << MT6357_PONSTS_PWRKEY_BIT)) == 0U) {
				uart_puts_all("[mk] ponsts: charger-only boot\r\n");
				return 1U;
			}
		}
	}

	return 0U;
}

void mk_pmic_power_off(void)
{
	uint16_t val = 0U;

	/* Clear PWRHOLD to allow power-off */
	if (mk_pmic_pwrap_read16(MT6357_PPCCTL0_ADDR, &val) == 0) {
		val &= (uint16_t) ~(1U << MT6357_RG_PWRHOLD_SHIFT);
		(void) mk_pmic_pwrap_write16(MT6357_PPCCTL0_ADDR, val);
	}

	/* Enable power-off sequence */
	if (mk_pmic_pwrap_read16(MT6357_STRUP_CON7_ADDR, &val) == 0) {
		val |= (uint16_t) (1U << MT6357_STRUP_PWROFF_SEQ_EN_SHIFT);
		val |= (uint16_t) (1U << MT6357_STRUP_PWROFF_PREOFF_EN_SHIFT);
		(void) mk_pmic_pwrap_write16(MT6357_STRUP_CON7_ADDR, val);
	}

	/* Spin — PMIC will cut power */
	for (;;) {
		__asm__ volatile("wfi");
	}
}
