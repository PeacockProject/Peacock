#ifndef MK_STAGE0_MTK_GPIO_H
#define MK_STAGE0_MTK_GPIO_H

#include <stdint.h>

void mk_stage0_mtk_gpio_write(uint32_t pin, uint32_t high);
uint32_t mk_stage0_mtk_gpio_read(uint32_t pin);
void mk_stage0_mtk_delay_ms(uint32_t ms);
void mk_stage0_mtk_delay_us(uint32_t us);

#endif
