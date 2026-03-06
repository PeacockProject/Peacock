.section .text.boot, "ax"
.global _start
.type _start, %function

_start:
	/* x0-x3 typically carry bootloader arguments. */
	bl mk_stage0_main
1:
	wfe
	b 1b
