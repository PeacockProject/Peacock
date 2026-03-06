.section .text.boot, "ax"
.global _start
.type _start, %function
.extern __mk_stack_top

/*
 * Linux arm64 Image header (must begin at offset 0):
 *  0x00 code0
 *  0x04 code1
 *  0x08 text_offset
 *  0x10 image_size
 *  0x18 flags
 *  0x20 res2
 *  0x28 res3
 *  0x30 res4
 *  0x38 magic ("ARM\x64")
 *  0x3c res5
 */
_start:
	/* Keep Linux/EFI-style "MZ" marker in first instruction word. */
	.inst 0x91005a4d
	b .Lentry
	/* text_offset */
	.quad 0x0000000000080000
	/* image_size */
	.quad _image_end - _start
	/* flags */
	.quad 0
	/* res2, res3, res4 */
	.quad 0
	.quad 0
	.quad 0
	/* magic: "ARM\x64" little-endian */
	.word 0x644d5241
	.word 0

.Lentry:
	/* Save FDT pointer (x0 from ATF) before EL2 setup clobbers it. */
	mov x19, x0

	/* Probe UART0 (0x11002000) before touching stack or C code.
	 * If 'M' appears on serial, mk entry is reached and UART base is correct. */
	movz x10, #0x1100, lsl #16
	movk x10, #0x2000		/* x10 = 0x11002000 */
	movz x12, #0x86a0
	movk x12, #1, lsl #16		/* x12 = 0x186a0 = 100000 */
.Lprobe_poll:
	ldr w11, [x10, #0x14]		/* LSR */
	tbnz w11, #5, .Lprobe_send	/* THRE set */
	subs x12, x12, #1
	b.ne .Lprobe_poll
.Lprobe_send:
	mov w11, #0x4d			/* 'M' */
	str w11, [x10]			/* THR */

	/* EL2 safety: disable FP/SIMD traps and install safe exception vectors
	 * before touching the stack or entering C code.
	 * CPTR_EL2 = 0x33ff: RES1 bits [9:0] set, TFP (bit 10) = 0.
	 * Without this, compiler-generated FP/SIMD instructions trap to the
	 * (invalid) EL2 handler -> system reset. */
	mov x0, #0x33ff
	msr cptr_el2, x0
	adr x0, .Lel2_vectors
	msr vbar_el2, x0
	isb

	/* Restore FDT pointer into x0 before entering C. */
	mov x0, x19

	adrp x9, __mk_stack_top
	add x9, x9, :lo12:__mk_stack_top
	mov sp, x9
	bl mk_payload_main
1:
	wfe
	b 1b

.section .text.boot.alias, "ax"
.global _start_alias
.type _start_alias, %function

/* Some MTK boot paths jump to kernel+text_offset directly. */
_start_alias:
	mov x0, #0x33ff
	msr cptr_el2, x0
	adr x0, .Lel2_vectors
	msr vbar_el2, x0
	isb
	adrp x9, __mk_stack_top
	add x9, x9, :lo12:__mk_stack_top
	mov sp, x9
	bl mk_payload_main
2:
	wfe
	b 2b

/*
 * Minimal EL2 exception vector table.
 * ARM64 requires 2048-byte (0x800) alignment.
 * 16 entries x 0x80 bytes each = 2048 bytes total.
 * Each handler spins with wfe — safe hang instead of unknown-address crash.
 *
 * Groups (4 entries each):
 *   VBAR+0x000: current EL, SP0   (Sync/IRQ/FIQ/SError)
 *   VBAR+0x200: current EL, SPx   (Sync/IRQ/FIQ/SError)  <- most likely
 *   VBAR+0x400: lower EL, AArch64 (Sync/IRQ/FIQ/SError)
 *   VBAR+0x600: lower EL, AArch32 (Sync/IRQ/FIQ/SError)
 */
.section .text.boot, "ax"
.balign 0x800
.Lel2_vectors:
.Lel2_v0:
	wfe
	b .Lel2_v0
	.balign 0x80
.Lel2_v1:
	wfe
	b .Lel2_v1
	.balign 0x80
.Lel2_v2:
	wfe
	b .Lel2_v2
	.balign 0x80
.Lel2_v3:
	wfe
	b .Lel2_v3
	.balign 0x80
.Lel2_v4:
	wfe
	b .Lel2_v4
	.balign 0x80
.Lel2_v5:
	wfe
	b .Lel2_v5
	.balign 0x80
.Lel2_v6:
	wfe
	b .Lel2_v6
	.balign 0x80
.Lel2_v7:
	wfe
	b .Lel2_v7
	.balign 0x80
.Lel2_v8:
	wfe
	b .Lel2_v8
	.balign 0x80
.Lel2_v9:
	wfe
	b .Lel2_v9
	.balign 0x80
.Lel2_v10:
	wfe
	b .Lel2_v10
	.balign 0x80
.Lel2_v11:
	wfe
	b .Lel2_v11
	.balign 0x80
.Lel2_v12:
	wfe
	b .Lel2_v12
	.balign 0x80
.Lel2_v13:
	wfe
	b .Lel2_v13
	.balign 0x80
.Lel2_v14:
	wfe
	b .Lel2_v14
	.balign 0x80
.Lel2_v15:
	wfe
	b .Lel2_v15
	.balign 0x80
