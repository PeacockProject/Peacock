.section .text.boot, "ax"
.global _start
.type _start, %function
.global __mk_el1_vectors
.global __mk_el1_trampoline
.global __mk_chainload_raw
.global __mk_saved_el2_valid
.global __mk_saved_cptr_el2
.global __mk_saved_vbar_el2
.global __mk_saved_sctlr_el2
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

	mrs x20, CurrentEL
	lsr x21, x20, #2
	cmp x21, #2
	b.ne .Lentry_skip_el2_save

	mrs x22, cptr_el2
	adrp x23, __mk_saved_cptr_el2
	add x23, x23, :lo12:__mk_saved_cptr_el2
	str x22, [x23]

	mrs x22, vbar_el2
	adrp x23, __mk_saved_vbar_el2
	add x23, x23, :lo12:__mk_saved_vbar_el2
	str x22, [x23]

	mrs x22, sctlr_el2
	adrp x23, __mk_saved_sctlr_el2
	add x23, x23, :lo12:__mk_saved_sctlr_el2
	str x22, [x23]

	mov x22, #1
	adrp x23, __mk_saved_el2_valid
	add x23, x23, :lo12:__mk_saved_el2_valid
	str x22, [x23]
.Lentry_skip_el2_save:

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
	mrs x1, sctlr_el2
	bic x1, x1, #(1 << 1)		/* A: clear alignment fault enable */
	bic x1, x1, #(1 << 3)		/* SA: clear SP alignment check */
	msr sctlr_el2, x1
	isb

	/* Restore FDT pointer into x0 before entering C. */
	mov x0, x19

	adrp x9, __mk_stack_top
	add x9, x9, :lo12:__mk_stack_top
	mov sp, x9
	msr daifset, #0xf
	bl mk_payload_main
1:
	wfe
	b 1b

.section .text.boot.alias, "ax"
.global _start_alias
.type _start_alias, %function

/* Some MTK boot paths jump to kernel+text_offset directly. */
_start_alias:
	mrs x20, CurrentEL
	lsr x21, x20, #2
	cmp x21, #2
	b.ne .Lentry_alias_skip_el2_save

	mrs x22, cptr_el2
	adrp x23, __mk_saved_cptr_el2
	add x23, x23, :lo12:__mk_saved_cptr_el2
	str x22, [x23]

	mrs x22, vbar_el2
	adrp x23, __mk_saved_vbar_el2
	add x23, x23, :lo12:__mk_saved_vbar_el2
	str x22, [x23]

	mrs x22, sctlr_el2
	adrp x23, __mk_saved_sctlr_el2
	add x23, x23, :lo12:__mk_saved_sctlr_el2
	str x22, [x23]

	mov x22, #1
	adrp x23, __mk_saved_el2_valid
	add x23, x23, :lo12:__mk_saved_el2_valid
	str x22, [x23]
.Lentry_alias_skip_el2_save:
	mov x0, #0x33ff
	msr cptr_el2, x0
	adr x0, .Lel2_vectors
	msr vbar_el2, x0
	isb
	mrs x1, sctlr_el2
	bic x1, x1, #(1 << 1)		/* A: clear alignment fault enable */
	bic x1, x1, #(1 << 3)		/* SA: clear SP alignment check */
	msr sctlr_el2, x1
	isb
	adrp x9, __mk_stack_top
	add x9, x9, :lo12:__mk_stack_top
	mov sp, x9
	msr daifset, #0xf
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
	b .Lexc_curr_sp0_sync
	.balign 0x80
.Lel2_v1:
	b .Lexc_curr_sp0_irq
	.balign 0x80
.Lel2_v2:
	b .Lexc_curr_sp0_fiq
	.balign 0x80
.Lel2_v3:
	b .Lexc_curr_sp0_serr
	.balign 0x80
.Lel2_v4:
	b .Lexc_curr_spx_sync
	.balign 0x80
.Lel2_v5:
	b .Lexc_curr_spx_irq
	.balign 0x80
.Lel2_v6:
	b .Lexc_curr_spx_fiq
	.balign 0x80
.Lel2_v7:
	b .Lexc_curr_spx_serr
	.balign 0x80
.Lel2_v8:
	b .Lexc_lower64_sync
	.balign 0x80
.Lel2_v9:
	b .Lexc_lower64_irq
	.balign 0x80
.Lel2_v10:
	b .Lexc_lower64_fiq
	.balign 0x80
.Lel2_v11:
	b .Lexc_lower64_serr
	.balign 0x80
.Lel2_v12:
	b .Lexc_lower32_sync
	.balign 0x80
.Lel2_v13:
	b .Lexc_lower32_irq
	.balign 0x80
.Lel2_v14:
	b .Lexc_lower32_fiq
	.balign 0x80
.Lel2_v15:
	b .Lexc_lower32_serr
	.balign 0x80

.Luart_wait_thre:
	movz x10, #0x1100, lsl #16
	movk x10, #0x2000
	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Luart_wait_thre_poll:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Luart_wait_thre_ok
	subs x12, x12, #1
	b.ne .Luart_wait_thre_poll
.Luart_wait_thre_ok:
	ret

.Luart_putc:
	bl .Luart_wait_thre
	movz x10, #0x1100, lsl #16
	movk x10, #0x2000
	str w0, [x10]
	ret

.Luart_puts:
	ldrb w1, [x0]
	cbz w1, .Luart_puts_done
.Luart_puts_loop:
	mov w0, w1
	bl .Luart_putc
	add x0, x0, #1
	ldrb w1, [x0]
	cbnz w1, .Luart_puts_loop
.Luart_puts_done:
	ret

.Luart_puthex64:
	mov x2, x0
	mov x3, #60
.Luart_puthex64_loop:
	lsr x1, x2, x3
	and x1, x1, #0xf
	cmp x1, #10
	b.lo .Luart_hex_digit
	add x1, x1, #('a' - 10)
	b .Luart_hex_emit
.Luart_hex_digit:
	add x1, x1, #'0'
.Luart_hex_emit:
	mov w0, w1
	bl .Luart_putc
	subs x3, x3, #4
	b.pl .Luart_puthex64_loop
	ret

.Ldump_el2_common:
	adr x0, .Lmsg_esr
	bl .Luart_puts
	mrs x0, esr_el2
	bl .Luart_puthex64
	adr x0, .Lmsg_crlf
	bl .Luart_puts

	adr x0, .Lmsg_elr
	bl .Luart_puts
	mrs x0, elr_el2
	bl .Luart_puthex64
	adr x0, .Lmsg_crlf
	bl .Luart_puts

	adr x0, .Lmsg_far
	bl .Luart_puts
	mrs x0, far_el2
	bl .Luart_puthex64
	adr x0, .Lmsg_crlf
	bl .Luart_puts

	adr x0, .Lmsg_spsr
	bl .Luart_puts
	mrs x0, spsr_el2
	bl .Luart_puthex64
	adr x0, .Lmsg_crlf
	bl .Luart_puts

	adr x0, .Lmsg_hcr
	bl .Luart_puts
	mrs x0, hcr_el2
	bl .Luart_puthex64
	adr x0, .Lmsg_crlf
	bl .Luart_puts

	adr x0, .Lmsg_sctlr
	bl .Luart_puts
	mrs x0, sctlr_el2
	bl .Luart_puthex64
	adr x0, .Lmsg_crlf
	bl .Luart_puts

	adr x0, .Lmsg_vbar
	bl .Luart_puts
	mrs x0, vbar_el2
	bl .Luart_puthex64
	adr x0, .Lmsg_crlf
	bl .Luart_puts
	b .Lexc_hang

.Lexc_curr_sp0_sync:
	adr x0, .Lmsg_exc_curr_sp0_sync
	bl .Luart_puts
	b .Ldump_el2_common
.Lexc_curr_sp0_irq:
	adr x0, .Lmsg_exc_curr_sp0_irq
	bl .Luart_puts
	b .Ldump_el2_common
.Lexc_curr_sp0_fiq:
	adr x0, .Lmsg_exc_curr_sp0_fiq
	bl .Luart_puts
	b .Ldump_el2_common
.Lexc_curr_sp0_serr:
	adr x0, .Lmsg_exc_curr_sp0_serr
	bl .Luart_puts
	b .Ldump_el2_common

.Lexc_curr_spx_sync:
	adr x0, .Lmsg_exc_curr_spx_sync
	bl .Luart_puts
	b .Ldump_el2_common
.Lexc_curr_spx_irq:
	adr x0, .Lmsg_exc_curr_spx_irq
	bl .Luart_puts
	b .Ldump_el2_common
.Lexc_curr_spx_fiq:
	adr x0, .Lmsg_exc_curr_spx_fiq
	bl .Luart_puts
	b .Ldump_el2_common
.Lexc_curr_spx_serr:
	adr x0, .Lmsg_exc_curr_spx_serr
	bl .Luart_puts
	b .Ldump_el2_common

.Lexc_lower64_sync:
	adr x0, .Lmsg_exc_lower64_sync
	bl .Luart_puts
	b .Ldump_el2_common
.Lexc_lower64_irq:
	adr x0, .Lmsg_exc_lower64_irq
	bl .Luart_puts
	b .Ldump_el2_common
.Lexc_lower64_fiq:
	adr x0, .Lmsg_exc_lower64_fiq
	bl .Luart_puts
	b .Ldump_el2_common
.Lexc_lower64_serr:
	adr x0, .Lmsg_exc_lower64_serr
	bl .Luart_puts
	b .Ldump_el2_common

.Lexc_lower32_sync:
	adr x0, .Lmsg_exc_lower32_sync
	bl .Luart_puts
	b .Ldump_el2_common
.Lexc_lower32_irq:
	adr x0, .Lmsg_exc_lower32_irq
	bl .Luart_puts
	b .Ldump_el2_common
.Lexc_lower32_fiq:
	adr x0, .Lmsg_exc_lower32_fiq
	bl .Luart_puts
	b .Ldump_el2_common
.Lexc_lower32_serr:
	adr x0, .Lmsg_exc_lower32_serr
	bl .Luart_puts
	b .Ldump_el2_common

.Lexc_hang:
	wfe
	b .Lexc_hang

.section .text.boot, "ax"
.macro RAW_UART_EMIT_REG reg
.Lraw_emit_reg_wait_\@:
	ldr w12, [x10, #0x14]
	tbz w12, #5, .Lraw_emit_reg_wait_\@
	str \reg, [x10]
.endm

.macro RAW_UART_EMIT_HEX64 reg
	mov x22, \reg
	mov x23, #60
.Lraw_hex_loop_\@:
	lsr x24, x22, x23
	and x24, x24, #0xf
	cmp x24, #10
	b.lo .Lraw_hex_digit_\@
	add x24, x24, #('a' - 10)
	b .Lraw_hex_emit_\@
.Lraw_hex_digit_\@:
	add x24, x24, #'0'
.Lraw_hex_emit_\@:
	mov w11, w24
	RAW_UART_EMIT_REG w11
	subs x23, x23, #4
	b.pl .Lraw_hex_loop_\@
.endm

.balign 0x800
__mk_el1_vectors:
.Lel1_v0:
	b .Lexc1_curr_sp0_sync
	.balign 0x80
.Lel1_v1:
	b .Lexc1_curr_sp0_irq
	.balign 0x80
.Lel1_v2:
	b .Lexc1_curr_sp0_fiq
	.balign 0x80
.Lel1_v3:
	b .Lexc1_curr_sp0_serr
	.balign 0x80
.Lel1_v4:
	movz x10, #0x1100, lsl #16
	movk x10, #0x2000
	mov w11, #'V'
1:	ldr w12, [x10, #0x14]
	tbz w12, #5, 1b
	str w11, [x10]
	mov w11, #'0'
2:	ldr w12, [x10, #0x14]
	tbz w12, #5, 2b
	str w11, [x10]
	mov w11, #0x0d
3:	ldr w12, [x10, #0x14]
	tbz w12, #5, 3b
	str w11, [x10]
	mov w11, #0x0a
4:	ldr w12, [x10, #0x14]
	tbz w12, #5, 4b
	str w11, [x10]
	b .Lexc1_curr_spx_sync
	.balign 0x80
.Lel1_v5:
	b .Lexc1_curr_spx_irq
	.balign 0x80
.Lel1_v6:
	b .Lexc1_curr_spx_fiq
	.balign 0x80
.Lel1_v7:
	b .Lexc1_curr_spx_serr
	.balign 0x80
.Lel1_v8:
	b .Lexc1_lower64_sync
	.balign 0x80
.Lel1_v9:
	b .Lexc1_lower64_irq
	.balign 0x80
.Lel1_v10:
	b .Lexc1_lower64_fiq
	.balign 0x80
.Lel1_v11:
	b .Lexc1_lower64_serr
	.balign 0x80
.Lel1_v12:
	b .Lexc1_lower32_sync
	.balign 0x80
.Lel1_v13:
	b .Lexc1_lower32_irq
	.balign 0x80
.Lel1_v14:
	b .Lexc1_lower32_fiq
	.balign 0x80
.Lel1_v15:
	b .Lexc1_lower32_serr
	.balign 0x80

.Ldump_el1_common:
	adr x0, .Lmsg1_esr
	bl .Luart_puts
	mrs x0, esr_el1
	bl .Luart_puthex64
	adr x0, .Lmsg_crlf
	bl .Luart_puts

	adr x0, .Lmsg1_elr
	bl .Luart_puts
	mrs x0, elr_el1
	bl .Luart_puthex64
	adr x0, .Lmsg_crlf
	bl .Luart_puts

	adr x0, .Lmsg1_far
	bl .Luart_puts
	mrs x0, far_el1
	bl .Luart_puthex64
	adr x0, .Lmsg_crlf
	bl .Luart_puts

	adr x0, .Lmsg1_spsr
	bl .Luart_puts
	mrs x0, spsr_el1
	bl .Luart_puthex64
	adr x0, .Lmsg_crlf
	bl .Luart_puts

	adr x0, .Lmsg1_sctlr
	bl .Luart_puts
	mrs x0, sctlr_el1
	bl .Luart_puthex64
	adr x0, .Lmsg_crlf
	bl .Luart_puts

	adr x0, .Lmsg1_vbar
	bl .Luart_puts
	mrs x0, vbar_el1
	bl .Luart_puthex64
	adr x0, .Lmsg_crlf
	bl .Luart_puts
	b .Lexc1_hang

.Lexc1_curr_sp0_sync:
	adr x0, .Lmsg1_exc_curr_sp0_sync
	bl .Luart_puts
	b .Ldump_el1_common
.Lexc1_curr_sp0_irq:
	adr x0, .Lmsg1_exc_curr_sp0_irq
	bl .Luart_puts
	b .Ldump_el1_common
.Lexc1_curr_sp0_fiq:
	adr x0, .Lmsg1_exc_curr_sp0_fiq
	bl .Luart_puts
	b .Ldump_el1_common
.Lexc1_curr_sp0_serr:
	adr x0, .Lmsg1_exc_curr_sp0_serr
	bl .Luart_puts
	b .Ldump_el1_common

.Lexc1_curr_spx_sync:
	movz x10, #0x1100, lsl #16
	movk x10, #0x2000
	mov w11, #'V'
1:	ldr w12, [x10, #0x14]
	tbz w12, #5, 1b
	str w11, [x10]
	mov w11, #'1'
2:	ldr w12, [x10, #0x14]
	tbz w12, #5, 2b
	str w11, [x10]
	mov w11, #0x0d
3:	ldr w12, [x10, #0x14]
	tbz w12, #5, 3b
	str w11, [x10]
	mov w11, #0x0a
4:	ldr w12, [x10, #0x14]
	tbz w12, #5, 4b
	str w11, [x10]

	mov x20, x30
	mov x21, x29
	mov x25, sp
	mov x26, x16
	mov x27, x17

	mrs x16, esr_el1
	mrs x17, elr_el1
	mrs x18, far_el1
	mrs x19, spsr_el1

	mov w11, #'E'
	RAW_UART_EMIT_REG w11
	mov w11, #':'
	RAW_UART_EMIT_REG w11
	RAW_UART_EMIT_HEX64 x16
	mov w11, #0x0d
	RAW_UART_EMIT_REG w11
	mov w11, #0x0a
	RAW_UART_EMIT_REG w11

	mov w11, #'L'
	RAW_UART_EMIT_REG w11
	mov w11, #':'
	RAW_UART_EMIT_REG w11
	RAW_UART_EMIT_HEX64 x17
	mov w11, #0x0d
	RAW_UART_EMIT_REG w11
	mov w11, #0x0a
	RAW_UART_EMIT_REG w11

	mov w11, #'F'
	RAW_UART_EMIT_REG w11
	mov w11, #':'
	RAW_UART_EMIT_REG w11
	RAW_UART_EMIT_HEX64 x18
	mov w11, #0x0d
	RAW_UART_EMIT_REG w11
	mov w11, #0x0a
	RAW_UART_EMIT_REG w11

	mov w11, #'S'
	RAW_UART_EMIT_REG w11
	mov w11, #':'
	RAW_UART_EMIT_REG w11
	RAW_UART_EMIT_HEX64 x19
	mov w11, #0x0d
	RAW_UART_EMIT_REG w11
	mov w11, #0x0a
	RAW_UART_EMIT_REG w11

	mov w11, #'R'
	RAW_UART_EMIT_REG w11
	mov w11, #':'
	RAW_UART_EMIT_REG w11
	RAW_UART_EMIT_HEX64 x20
	mov w11, #0x0d
	RAW_UART_EMIT_REG w11
	mov w11, #0x0a
	RAW_UART_EMIT_REG w11

	mov w11, #'P'
	RAW_UART_EMIT_REG w11
	mov w11, #':'
	RAW_UART_EMIT_REG w11
	RAW_UART_EMIT_HEX64 x21
	mov w11, #0x0d
	RAW_UART_EMIT_REG w11
	mov w11, #0x0a
	RAW_UART_EMIT_REG w11

	mov w11, #'T'
	RAW_UART_EMIT_REG w11
	mov w11, #':'
	RAW_UART_EMIT_REG w11
	RAW_UART_EMIT_HEX64 x25
	mov w11, #0x0d
	RAW_UART_EMIT_REG w11
	mov w11, #0x0a
	RAW_UART_EMIT_REG w11

	mov w11, #'A'
	RAW_UART_EMIT_REG w11
	mov w11, #':'
	RAW_UART_EMIT_REG w11
	RAW_UART_EMIT_HEX64 x26
	mov w11, #0x0d
	RAW_UART_EMIT_REG w11
	mov w11, #0x0a
	RAW_UART_EMIT_REG w11

	mov w11, #'B'
	RAW_UART_EMIT_REG w11
	mov w11, #':'
	RAW_UART_EMIT_REG w11
	RAW_UART_EMIT_HEX64 x27
	mov w11, #0x0d
	RAW_UART_EMIT_REG w11
	mov w11, #0x0a
	RAW_UART_EMIT_REG w11
	b .Lexc1_hang
.Lexc1_curr_spx_irq:
	adr x0, .Lmsg1_exc_curr_spx_irq
	bl .Luart_puts
	b .Ldump_el1_common
.Lexc1_curr_spx_fiq:
	adr x0, .Lmsg1_exc_curr_spx_fiq
	bl .Luart_puts
	b .Ldump_el1_common
.Lexc1_curr_spx_serr:
	adr x0, .Lmsg1_exc_curr_spx_serr
	bl .Luart_puts
	b .Ldump_el1_common

.Lexc1_lower64_sync:
	adr x0, .Lmsg1_exc_lower64_sync
	bl .Luart_puts
	b .Ldump_el1_common
.Lexc1_lower64_irq:
	adr x0, .Lmsg1_exc_lower64_irq
	bl .Luart_puts
	b .Ldump_el1_common
.Lexc1_lower64_fiq:
	adr x0, .Lmsg1_exc_lower64_fiq
	bl .Luart_puts
	b .Ldump_el1_common
.Lexc1_lower64_serr:
	adr x0, .Lmsg1_exc_lower64_serr
	bl .Luart_puts
	b .Ldump_el1_common

.Lexc1_lower32_sync:
	adr x0, .Lmsg1_exc_lower32_sync
	bl .Luart_puts
	b .Ldump_el1_common
.Lexc1_lower32_irq:
	adr x0, .Lmsg1_exc_lower32_irq
	bl .Luart_puts
	b .Ldump_el1_common
.Lexc1_lower32_fiq:
	adr x0, .Lmsg1_exc_lower32_fiq
	bl .Luart_puts
	b .Ldump_el1_common
.Lexc1_lower32_serr:
	adr x0, .Lmsg1_exc_lower32_serr
	bl .Luart_puts
	b .Ldump_el1_common

.Lexc1_hang:
	wfe
	b .Lexc1_hang

.section .text.boot, "ax"
.balign 16
.type __mk_el1_trampoline, %function
__mk_el1_trampoline:
	movz x10, #0x1100, lsl #16
	movk x10, #0x2000

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_0:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_0
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_0
.Lel1_probe_emit_0:
	mov w11, #'E'
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_1:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_1
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_1
.Lel1_probe_emit_1:
	mov w11, #'1'
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_2:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_2
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_2
.Lel1_probe_emit_2:
	mov w11, #0x0d
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_3:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_3
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_3
.Lel1_probe_emit_3:
	mov w11, #0x0a
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_4:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_4
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_4
.Lel1_probe_emit_4:
	mov w11, #'E'
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_5:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_5
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_5
.Lel1_probe_emit_5:
	mov w11, #'2'
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_6:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_6
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_6
.Lel1_probe_emit_6:
	mov w11, #0x0d
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_7:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_7
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_7
.Lel1_probe_emit_7:
	mov w11, #0x0a
	str w11, [x10]

	msr daifset, #0xf

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_8:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_8
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_8
.Lel1_probe_emit_8:
	mov w11, #'D'
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_9:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_9
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_9
.Lel1_probe_emit_9:
	mov w11, #'0'
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_10:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_10
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_10
.Lel1_probe_emit_10:
	mov w11, #0x0d
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_11:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_11
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_11
.Lel1_probe_emit_11:
	mov w11, #0x0a
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_p0_0:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_p0_0
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_p0_0
.Lel1_probe_emit_p0_0:
	mov w11, #'P'
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_p0_1:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_p0_1
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_p0_1
.Lel1_probe_emit_p0_1:
	mov w11, #'0'
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_p0_2:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_p0_2
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_p0_2
.Lel1_probe_emit_p0_2:
	mov w11, #0x0d
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_p0_3:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_p0_3
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_p0_3
.Lel1_probe_emit_p0_3:
	mov w11, #0x0a
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_q0_0:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_q0_0
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_q0_0
.Lel1_probe_emit_q0_0:
	mov w11, #'Q'
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_q0_1:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_q0_1
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_q0_1
.Lel1_probe_emit_q0_1:
	mov w11, #'0'
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_q0_2:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_q0_2
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_q0_2
.Lel1_probe_emit_q0_2:
	mov w11, #0x0d
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_q0_3:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_q0_3
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_q0_3
.Lel1_probe_emit_q0_3:
	mov w11, #0x0a
	str w11, [x10]

	mrs x16, CurrentEL

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_q1_0:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_q1_0
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_q1_0
.Lel1_probe_emit_q1_0:
	mov w11, #'Q'
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_q1_1:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_q1_1
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_q1_1
.Lel1_probe_emit_q1_1:
	mov w11, #'1'
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_q1_2:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_q1_2
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_q1_2
.Lel1_probe_emit_q1_2:
	mov w11, #0x0d
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_q1_3:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_q1_3
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_q1_3
.Lel1_probe_emit_q1_3:
	mov w11, #0x0a
	str w11, [x10]

	lsr x17, x16, #2
	and x17, x17, #0xf

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_q2_0:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_q2_0
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_q2_0
.Lel1_probe_emit_q2_0:
	mov w11, #'Q'
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_q2_1:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_q2_1
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_q2_1
.Lel1_probe_emit_q2_1:
	mov w11, #'2'
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_q2_2:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_q2_2
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_q2_2
.Lel1_probe_emit_q2_2:
	mov w11, #0x0d
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_q2_3:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_q2_3
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_q2_3
.Lel1_probe_emit_q2_3:
	mov w11, #0x0a
	str w11, [x10]

	cmp x17, #10
	b.lo .Lel1_probe_digit_q3
	add x17, x17, #('a' - 10)
	b .Lel1_probe_emit_nibble_q3
.Lel1_probe_digit_q3:
	add x17, x17, #'0'
.Lel1_probe_emit_nibble_q3:
	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_q3_nibble:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_q3_nibble
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_q3_nibble
.Lel1_probe_emit_q3_nibble:
	mov w11, w17
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_q3_0:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_q3_0
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_q3_0
.Lel1_probe_emit_q3_0:
	mov w11, #'Q'
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_q3_1:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_q3_1
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_q3_1
.Lel1_probe_emit_q3_1:
	mov w11, #'3'
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_q3_2:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_q3_2
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_q3_2
.Lel1_probe_emit_q3_2:
	mov w11, #0x0d
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_q3_3:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_q3_3
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_q3_3
.Lel1_probe_emit_q3_3:
	mov w11, #0x0a
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_r0_0:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_r0_0
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_r0_0
.Lel1_probe_emit_r0_0:
	mov w11, #'R'
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_r0_1:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_r0_1
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_r0_1
.Lel1_probe_emit_r0_1:
	mov w11, #'0'
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_r0_2:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_r0_2
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_r0_2
.Lel1_probe_emit_r0_2:
	mov w11, #0x0d
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_r0_3:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_r0_3
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_r0_3
.Lel1_probe_emit_r0_3:
	mov w11, #0x0a
	str w11, [x10]

	mrs x13, vbar_el1

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_r1_0:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_r1_0
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_r1_0
.Lel1_probe_emit_r1_0:
	mov w11, #'R'
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_r1_1:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_r1_1
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_r1_1
.Lel1_probe_emit_r1_1:
	mov w11, #'1'
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_r1_2:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_r1_2
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_r1_2
.Lel1_probe_emit_r1_2:
	mov w11, #0x0d
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_r1_3:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_r1_3
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_r1_3
.Lel1_probe_emit_r1_3:
	mov w11, #0x0a
	str w11, [x10]

	lsr x17, x13, #12
	and x17, x17, #0xf
	lsr x18, x13, #8
	and x18, x18, #0xf

	cmp x17, #10
	b.lo .Lel1_probe_digit_r2_hi
	add x17, x17, #('a' - 10)
	b .Lel1_probe_emit_nibble_r2_hi
.Lel1_probe_digit_r2_hi:
	add x17, x17, #'0'
.Lel1_probe_emit_nibble_r2_hi:
	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_r2_hi:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_r2_hi
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_r2_hi
.Lel1_probe_emit_r2_hi:
	mov w11, w17
	str w11, [x10]

	cmp x18, #10
	b.lo .Lel1_probe_digit_r2_lo
	add x18, x18, #('a' - 10)
	b .Lel1_probe_emit_nibble_r2_lo
.Lel1_probe_digit_r2_lo:
	add x18, x18, #'0'
.Lel1_probe_emit_nibble_r2_lo:
	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_r2_lo:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_r2_lo
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_r2_lo
.Lel1_probe_emit_r2_lo:
	mov w11, w18
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_r2_0:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_r2_0
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_r2_0
.Lel1_probe_emit_r2_0:
	mov w11, #'R'
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_r2_1:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_r2_1
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_r2_1
.Lel1_probe_emit_r2_1:
	mov w11, #'2'
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_r2_2:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_r2_2
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_r2_2
.Lel1_probe_emit_r2_2:
	mov w11, #0x0d
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_r2_3:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_r2_3
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_r2_3
.Lel1_probe_emit_r2_3:
	mov w11, #0x0a
	str w11, [x10]

	add x15, x13, #0x200

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_s0_0:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_s0_0
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_s0_0
.Lel1_probe_emit_s0_0:
	mov w11, #'S'
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_s0_1:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_s0_1
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_s0_1
.Lel1_probe_emit_s0_1:
	mov w11, #'0'
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_s0_2:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_s0_2
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_s0_2
.Lel1_probe_emit_s0_2:
	mov w11, #0x0d
	str w11, [x10]

	movz x12, #0x86a0
	movk x12, #1, lsl #16
.Lel1_probe_wait_s0_3:
	ldr w11, [x10, #0x14]
	tbnz w11, #5, .Lel1_probe_emit_s0_3
	subs x12, x12, #1
	b.ne .Lel1_probe_wait_s0_3
.Lel1_probe_emit_s0_3:
	mov w11, #0x0a
	str w11, [x10]

	mov x0, x21
	mov x1, xzr
	mov x2, xzr
	mov x3, xzr
	br x20
	b .Lexc1_hang

.balign 16
.type __mk_chainload_raw, %function
__mk_chainload_raw:
	/* x0 = fdt_pa, x1 = entry.  ARM64 boot protocol: x0=FDT, x1-x3=0. */
	mov x4, x1		/* save entry in x4 */
	mov x1, xzr
	mov x2, xzr
	mov x3, xzr
	msr daifset, #0xf
	isb
	mrs x5, CurrentEL
	lsr x5, x5, #2
	cmp x5, #2
	b.ne .Lchainload_el1

	dsb sy
	mrs x5, sctlr_el2
	mov x6, #0x1005
	bic x5, x5, x6
	msr sctlr_el2, x5
	isb
	br x4

.Lchainload_el1:
	cmp x5, #1
	b.ne .Lchainload_hang
	dsb sy
	mrs x5, sctlr_el1
	mov x6, #0x1005
	bic x5, x5, x6
	msr sctlr_el1, x5
	isb
	br x4

.Lchainload_hang:
	wfe
	b .Lchainload_hang

.section .rodata, "a"
.Lmsg_crlf:
	.asciz "\r\n"
.Lmsg_probe_currel:
	.asciz "[mk] el1 probe: CurrentEL=0x"
.Lmsg_probe_vbar:
	.asciz "[mk] el1 probe: VBAR_EL1=0x"
.Lmsg_probe_expect:
	.asciz "[mk] el1 probe: expect vectors=0x"
.Lmsg_probe_slot:
	.asciz "[mk] el1 probe: branch slot=0x"
.Lmsg_exc_curr_sp0_sync:
	.asciz "[mk] exc: curr-sp0-sync\r\n"
.Lmsg_exc_curr_sp0_irq:
	.asciz "[mk] exc: curr-sp0-irq\r\n"
.Lmsg_exc_curr_sp0_fiq:
	.asciz "[mk] exc: curr-sp0-fiq\r\n"
.Lmsg_exc_curr_sp0_serr:
	.asciz "[mk] exc: curr-sp0-serror\r\n"
.Lmsg_exc_curr_spx_sync:
	.asciz "[mk] exc: curr-spx-sync\r\n"
.Lmsg_exc_curr_spx_irq:
	.asciz "[mk] exc: curr-spx-irq\r\n"
.Lmsg_exc_curr_spx_fiq:
	.asciz "[mk] exc: curr-spx-fiq\r\n"
.Lmsg_exc_curr_spx_serr:
	.asciz "[mk] exc: curr-spx-serror\r\n"
.Lmsg_exc_lower64_sync:
	.asciz "[mk] exc: lower64-sync\r\n"
.Lmsg_exc_lower64_irq:
	.asciz "[mk] exc: lower64-irq\r\n"
.Lmsg_exc_lower64_fiq:
	.asciz "[mk] exc: lower64-fiq\r\n"
.Lmsg_exc_lower64_serr:
	.asciz "[mk] exc: lower64-serror\r\n"
.Lmsg_exc_lower32_sync:
	.asciz "[mk] exc: lower32-sync\r\n"
.Lmsg_exc_lower32_irq:
	.asciz "[mk] exc: lower32-irq\r\n"
.Lmsg_exc_lower32_fiq:
	.asciz "[mk] exc: lower32-fiq\r\n"
.Lmsg_exc_lower32_serr:
	.asciz "[mk] exc: lower32-serror\r\n"
.Lmsg_esr:
	.asciz "[mk] exc: ESR_EL2=0x"
.Lmsg_elr:
	.asciz "[mk] exc: ELR_EL2=0x"
.Lmsg_far:
	.asciz "[mk] exc: FAR_EL2=0x"
.Lmsg_spsr:
	.asciz "[mk] exc: SPSR_EL2=0x"
.Lmsg_hcr:
	.asciz "[mk] exc: HCR_EL2=0x"
.Lmsg_sctlr:
	.asciz "[mk] exc: SCTLR_EL2=0x"
.Lmsg_vbar:
	.asciz "[mk] exc: VBAR_EL2=0x"
.Lmsg1_exc_curr_sp0_sync:
	.asciz "[mk] exc1: curr-sp0-sync\r\n"
.Lmsg1_exc_curr_sp0_irq:
	.asciz "[mk] exc1: curr-sp0-irq\r\n"
.Lmsg1_exc_curr_sp0_fiq:
	.asciz "[mk] exc1: curr-sp0-fiq\r\n"
.Lmsg1_exc_curr_sp0_serr:
	.asciz "[mk] exc1: curr-sp0-serror\r\n"
.Lmsg1_exc_curr_spx_sync:
	.asciz "[mk] exc1: curr-spx-sync\r\n"
.Lmsg1_exc_curr_spx_irq:
	.asciz "[mk] exc1: curr-spx-irq\r\n"
.Lmsg1_exc_curr_spx_fiq:
	.asciz "[mk] exc1: curr-spx-fiq\r\n"
.Lmsg1_exc_curr_spx_serr:
	.asciz "[mk] exc1: curr-spx-serror\r\n"
.Lmsg1_exc_lower64_sync:
	.asciz "[mk] exc1: lower64-sync\r\n"
.Lmsg1_exc_lower64_irq:
	.asciz "[mk] exc1: lower64-irq\r\n"
.Lmsg1_exc_lower64_fiq:
	.asciz "[mk] exc1: lower64-fiq\r\n"
.Lmsg1_exc_lower64_serr:
	.asciz "[mk] exc1: lower64-serror\r\n"
.Lmsg1_exc_lower32_sync:
	.asciz "[mk] exc1: lower32-sync\r\n"
.Lmsg1_exc_lower32_irq:
	.asciz "[mk] exc1: lower32-irq\r\n"
.Lmsg1_exc_lower32_fiq:
	.asciz "[mk] exc1: lower32-fiq\r\n"
.Lmsg1_exc_lower32_serr:
	.asciz "[mk] exc1: lower32-serror\r\n"
.Lmsg1_esr:
	.asciz "[mk] exc1: ESR_EL1=0x"
.Lmsg1_elr:
	.asciz "[mk] exc1: ELR_EL1=0x"
.Lmsg1_far:
	.asciz "[mk] exc1: FAR_EL1=0x"
.Lmsg1_spsr:
	.asciz "[mk] exc1: SPSR_EL1=0x"
.Lmsg1_sctlr:
	.asciz "[mk] exc1: SCTLR_EL1=0x"
.Lmsg1_vbar:
	.asciz "[mk] exc1: VBAR_EL1=0x"
.Lmsg_el1_tramp_enter:
	.asciz "[mk] el1 tramp enter\r\n"
.Lmsg_el1_tramp_branch:
	.asciz "[mk] el1 tramp branch\r\n"

.section .bss, "aw", %nobits
.balign 8
__mk_saved_el2_valid:
	.quad 0
__mk_saved_cptr_el2:
	.quad 0
__mk_saved_vbar_el2:
	.quad 0
__mk_saved_sctlr_el2:
	.quad 0
