#ifndef MK_STAGE0_MTK_I2C_H
#define MK_STAGE0_MTK_I2C_H

#include <stdint.h>

int mk_stage0_mtk_i2c_write_reg8(uint64_t base, uint8_t addr7, uint8_t reg, uint8_t value);
int mk_stage0_mtk_i2c_last_error(void);
uint32_t mk_stage0_mtk_i2c_last_status(void);
uint32_t mk_stage0_mtk_i2c_last_debug0(void);
uint32_t mk_stage0_mtk_i2c_last_debug1(void);

#endif
