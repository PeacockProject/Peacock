#include <stdint.h>

__attribute__((section(".mk.note")))
static const char g_stage0_banner[] = "mk-stage0-aarch64";

__attribute__((noreturn))
void mk_stage0_main(uint64_t x0, uint64_t x1, uint64_t x2, uint64_t x3)
{
	volatile uint64_t keepalive = 0x4d4b535441474530ULL;
	(void) x0;
	(void) x1;
	(void) x2;
	(void) x3;
	(void) g_stage0_banner;

	for (;;) {
		keepalive ^= 0x0101010101010101ULL;
		__asm__ volatile("wfe");
	}
}
