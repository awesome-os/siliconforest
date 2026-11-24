/*
 * Darwin Lab T8103 first-entry experiment. Included only by start.s in the
 * explicitly selected DEBUG candidate. This is not the XNU VM/IOKit console.
 * Entry ABI: __start, EL1, stage-1 MMU off, x0 = normal XNU boot_args pointer.
 * x0 is preserved and is not dereferenced. No stack or writable globals used.
 * The loader must validate T8103 and ADT uart0 reg0 == 0x235200000 and provide
 * a working, already configured Samsung UART (m1n1 VUART for the first route).
 * UART constants: pinned m1n1 src/soc.h and src/uart_regs.h; see CONTRACT.md.
 */
#if !defined(ARM64_BOARD_CONFIG_T8103) || !DEBUG
#error "M1 first entry requires the native T8103 DEBUG configuration"
#endif

	.align 2
	.globl EXT(darwin_lab_m1_first_entry)
LEXT(darwin_lab_m1_first_entry)
	/* Mask exceptions before validation, including the wrong-EL park path. */
	msr DAIFSet, #0xf
	/* x19 is a diagnostic state, not an authorization or success token. */
	mov x19, #0
	mrs x9, CurrentEL
	cmp x9, #4
	b.ne Ldl_m1_bad_el
	mrs x9, SCTLR_EL1
	tbnz x9, #0, Ldl_m1_bad_mmu
	/* Preserve exception state at a local spin vector on a synchronous fault. */
	adrp x9, EXT(LowExceptionVectorBase)@page
	add x9, x9, EXT(LowExceptionVectorBase)@pageoff
	msr VBAR_EL1, x9
	isb
	/* Tonga/M1 Icestorm or Firestorm. This cannot identify a Mac model. */
	mrs x9, MIDR_EL1
	ubfx x10, x9, #24, #8
	cmp x10, #0x61
	b.ne Ldl_m1_bad_cpu
	ubfx x10, x9, #4, #12
	cmp x10, #0x22
	b.eq Ldl_m1_cpu_ok
	cmp x10, #0x23
	b.ne Ldl_m1_bad_cpu
Ldl_m1_cpu_ok:
	mov x19, #1
	movz x1, #0x3520, lsl #16
	movk x1, #2, lsl #32 /* x1 = 0x235200000 */
	adr x2, Ldl_m1_message
Ldl_m1_next:
	ldrb w3, [x2], #1
	cbz w3, Ldl_m1_drain
	mov x4, #0x100000 /* bounded polling, no timer/IRQ dependencies */
Ldl_m1_tx_wait:
	ldr w5, [x1, #0x10] /* UTRSTAT: TXBE */
	tbnz w5, #1, Ldl_m1_tx
	subs x4, x4, #1
	b.ne Ldl_m1_tx_wait
	b Ldl_m1_uart_timeout
Ldl_m1_tx:
	str w3, [x1, #0x20] /* UTXH, same access width as pinned m1n1 */
	dsb sy
	mov x19, #2
	b Ldl_m1_next
Ldl_m1_drain:
	mov x4, #0x100000
Ldl_m1_drain_wait:
	ldr w5, [x1, #0x10] /* UTRSTAT: transmitter empty */
	tbnz w5, #2, Ldl_m1_drained
	subs x4, x4, #1
	b.ne Ldl_m1_drain_wait
	b Ldl_m1_uart_timeout
Ldl_m1_drained:
	mov x19, #3
	b Ldl_m1_park
Ldl_m1_bad_el:
	mov x19, #0xe1
	b Ldl_m1_park
Ldl_m1_bad_mmu:
	mov x19, #0xe2
	b Ldl_m1_park
Ldl_m1_bad_cpu:
	mov x19, #0xe3
	b Ldl_m1_park
Ldl_m1_uart_timeout:
	mov x19, #0xe4
Ldl_m1_park:
	/* No fallthrough, return, WFI, secondary CPU startup or interrupt enable. */
	b Ldl_m1_park
Ldl_m1_message:
	.asciz "HVLOG: DARWIN_LAB_M1_EARLY_CONSOLE checkpoint=entry-before-vm next=park kernel_init=not_started\r\n"
	.align 2

/* Public source omits these Apple cache helpers. They are NOT implemented
 * here. For this parked-entry-only image, any accidental use fails closed
 * forever instead of reporting successful cache maintenance. These labels
 * are outside the diagnostic path; no normal-boot support is asserted. */
	.globl EXT(CleanPoC_DcacheRegion_Force_nopreempt)
	.globl EXT(CleanPoC_DcacheRegion_Force_nopreempt_nohid)
	.globl EXT(enable_dc_mva_ops)
	.globl EXT(disable_dc_mva_ops)
	.globl EXT(darwin_lab_m1_unsupported_cache)
LEXT(CleanPoC_DcacheRegion_Force_nopreempt)
LEXT(CleanPoC_DcacheRegion_Force_nopreempt_nohid)
LEXT(enable_dc_mva_ops)
LEXT(disable_dc_mva_ops)
LEXT(darwin_lab_m1_unsupported_cache)
	msr DAIFSet, #0xf
	mov x19, #0xef
Ldl_m1_unsupported_cache_park:
	b Ldl_m1_unsupported_cache_park
