#include "mtk_gpio.h"
#include "mtk_panel.h"

#define MTK_GPIO_BASE 0x10005000ULL
#define MTK_GPIO_DIR_BASE 0x0000U
#define MTK_GPIO_DO_BASE 0x0100U
#define MTK_GPIO_DI_BASE 0x0200U
#define MTK_GPIO_MODE_BASE 0x0300U
#define MTK_GPIO_GROUP_STRIDE 0x0010U

static uint32_t reg_read32(uint64_t addr)
{
	return *(volatile uint32_t *) (uintptr_t) addr;
}

static void reg_write32(uint64_t addr, uint32_t value)
{
	*(volatile uint32_t *) (uintptr_t) addr = value;
}

static void gpio_set_mode_gpio(uint32_t pin)
{
	uint32_t group;
	uint32_t shift;
	uint64_t addr;
	uint32_t value;

	if (pin == MK_STAGE0_GPIO_NONE) {
		return;
	}

	group = pin / 8U;
	shift = (pin % 8U) * 4U;
	addr = MTK_GPIO_BASE + MTK_GPIO_MODE_BASE + ((uint64_t) group * MTK_GPIO_GROUP_STRIDE);
	value = reg_read32(addr);
	value &= ~(0xfU << shift);
	reg_write32(addr, value);
}

static void gpio_set_dir_out(uint32_t pin)
{
	uint32_t group;
	uint32_t bit;
	uint64_t addr;
	uint32_t value;

	if (pin == MK_STAGE0_GPIO_NONE) {
		return;
	}

	group = pin / 32U;
	bit = pin % 32U;
	addr = MTK_GPIO_BASE + MTK_GPIO_DIR_BASE + ((uint64_t) group * MTK_GPIO_GROUP_STRIDE);
	value = reg_read32(addr);
	value |= (1U << bit);
	reg_write32(addr, value);
}

static void gpio_set_output(uint32_t pin, uint32_t high)
{
	uint32_t group;
	uint32_t bit;
	uint64_t addr;
	uint32_t value;

	if (pin == MK_STAGE0_GPIO_NONE) {
		return;
	}

	group = pin / 32U;
	bit = pin % 32U;
	addr = MTK_GPIO_BASE + MTK_GPIO_DO_BASE + ((uint64_t) group * MTK_GPIO_GROUP_STRIDE);
	value = reg_read32(addr);
	if (high != 0U) {
		value |= (1U << bit);
	} else {
		value &= ~(1U << bit);
	}
	reg_write32(addr, value);
}

static uint32_t gpio_get_input(uint32_t pin)
{
	uint32_t group;
	uint32_t bit;
	uint64_t addr;
	uint32_t value;

	if (pin == MK_STAGE0_GPIO_NONE) {
		return 0U;
	}

	group = pin / 32U;
	bit = pin % 32U;
	addr = MTK_GPIO_BASE + MTK_GPIO_DI_BASE + ((uint64_t) group * MTK_GPIO_GROUP_STRIDE);
	value = reg_read32(addr);
	return (value >> bit) & 1U;
}

void mk_stage0_mtk_gpio_write(uint32_t pin, uint32_t high)
{
	gpio_set_mode_gpio(pin);
	gpio_set_dir_out(pin);
	gpio_set_output(pin, high);
}

uint32_t mk_stage0_mtk_gpio_read(uint32_t pin)
{
	return gpio_get_input(pin);
}

void mk_stage0_mtk_delay_ms(uint32_t ms)
{
	volatile uint32_t outer;
	volatile uint32_t inner;

	for (outer = 0; outer < ms; outer++) {
		for (inner = 0; inner < 18000U; inner++) {
			__asm__ volatile("" ::: "memory");
		}
	}
}

void mk_stage0_mtk_delay_us(uint32_t us)
{
	volatile uint32_t outer;
	volatile uint32_t inner;

	for (outer = 0; outer < us; outer++) {
		for (inner = 0; inner < 18U; inner++) {
			__asm__ volatile("" ::: "memory");
		}
	}
}
