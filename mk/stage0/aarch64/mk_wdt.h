#ifndef MK_WDT_H
#define MK_WDT_H

#include <stdint.h>

void setup_wdt(const void *fdt);
uint64_t mk_wdt_get_base(void);
void pet_wdt(void);
void mk_stage0_wdt_restore_for_linux(void);
void mk_stage0_wdt_handoff_ab_quiesce(void);
void mk_stage0_log_reset_watchdog_state(const char *tag);
void mk_stage0_log_retained_reset_provenance(const void *fdt);
void arm_recovery_wdt(void);
void arm_normal_wdt(void);
void trigger_recovery_wdt_reset(void);
void mk_stage0_fastboot_action_immediate(uint8_t action);

#endif /* MK_WDT_H */
