#ifndef MK_PMIC_H
#define MK_PMIC_H

#include <stdint.h>

int mk_pmic_pwrap_read16(uint32_t adr, uint16_t *rdata);
int mk_pmic_pwrap_write16(uint32_t adr, uint16_t wdata);
uint8_t mk_pmic_power_pressed(void);
uint8_t mk_pmic_homekey_pressed(void);
uint8_t mk_pmic_charger_connected(void);
uint8_t mk_pmic_boot_is_charger_only(const void *fdt);
void mk_pmic_power_off(void);

#endif /* MK_PMIC_H */
