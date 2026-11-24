/* SPDX-License-Identifier: MIT */
/*
 * SiliconForest native boot: the page that stays at EL2 when XNU runs on the M1 without
 * the m1n1 hypervisor.
 *
 * m1n1 vectors here at EL2 with its MMU off (P_VECTOR; src/main.c, next_stage). The
 * open-source XNU has no EL2 entry code (pexpert H13.h defines HAS_EL2, nothing uses it), and
 * under the hypervisor it has always run at EL1. So this page configures EL2 the way m1n1's
 * hypervisor does for its guest, minus everything that makes it a hypervisor (no stage 2, no
 * traps, no FIQ routing to EL2), and enters XNU at EL1h. Register values are the ones of
 * m1n1 src/hv/hv.c (hv_init, hv_init_secondary) and proxyclient/m1n1/hv/__init__.py (init,
 * start), Copyright The Asahi Linux Contributors, MIT.
 *
 * Afterwards the page only reports: any exception that reaches EL2, and any exception at EL1
 * before XNU has installed its own vectors, is printed on the UART (one line, hexadecimal)
 * and the core stops. Nothing is emulated.
 *
 * Layout (16 KiB, position independent, never mapped: EL2 runs with its MMU off):
 *   0x0000  reset entry of a secondary core (its RVBAR points at this page)
 *   0x0004  boot entry: x0 = XNU entry, x1 = boot-args, both physical
 *   0x0008  hold entry of a secondary core that m1n1 started (P_SMP_CALL): x0 = address of
 *           the core's reset vector register, x1 = the value it holds now, x2/x3 = address
 *           and value of the acknowledgement
 *   0x0800  EL2 vectors
 *   0x1000  EL1 vectors, used until XNU sets VBAR_EL1
 *   0x3f00  parameters, written by the launcher
 *   0x3f40  one acknowledgement word per held core
 */

#define HCR_API         (1 << 41)
#define HCR_APK         (1 << 40)
#define HCR_E2H         (1 << 34)
#define HCR_RW          (1 << 31)

#define SCTLR_EL12      s3_5_c1_c0_0
#define VBAR_EL12       s3_5_c12_c0_0
#define ACTLR_EL12_APL  s3_6_c15_c14_6          /* SYS_IMP_APL_ACTLR_EL12, cores before Blizzard */
#define VM_TMR_FIQ_ENA  s3_5_c15_c1_3
#define SPRR_CONFIG     s3_6_c15_c1_0
#define GXF_CONFIG      s3_6_c15_c1_2
#define APVMKEYLO       s3_6_c15_c14_4
#define APVMKEYHI       s3_6_c15_c14_5
#define APSTS_EL12      s3_6_c15_c14_7
#define AMX_CTL_EL2     s3_4_c15_c4_7
#define CYC_OVRD        s3_5_c15_c5_0

#define FLAG_SPRR_GXF   1       /* enable SPRR and GXF as hv.start() does */
#define FLAG_AMX        2       /* AMX for EL1 as hv.init() does */

#define UTRSTAT         0x10
#define UTXH            0x20

        .text
        .globl _start
_start:
        b       secondary_reset                 /* 0x0000 */
        b       boot_entry                      /* 0x0004 */
        b       hold_entry                      /* 0x0008 */

/* x0: character. Clobbers x9-x11. A UART that never empties is given up on. */
putc:
        adr     x9, param_uart
        ldr     x9, [x9]
        cbz     x9, 2f
        mov     x10, #0x400000
1:      ldr     w11, [x9, #UTRSTAT]
        tbnz    w11, #1, 3f
        subs    x10, x10, #1
        b.ne    1b
        ret
3:      str     w0, [x9, #UTXH]
2:      ret

/* x0: value, printed as 16 hexadecimal digits. Clobbers x9-x14. */
puthex:
        mov     x12, x0
        mov     x13, #60
        mov     x14, x30
1:      lsr     x0, x12, x13
        and     x0, x0, #0xf
        cmp     x0, #10
        add     x0, x0, #'0'
        b.lo    2f
        add     x0, x0, #('a' - '0' - 10)
2:      bl      putc
        subs    x13, x13, #4
        b.pl    1b
        ret     x14

/* x0: address of a NUL-terminated string. Clobbers x9-x12, x15. */
puts:
        mov     x15, x30
        mov     x12, x0
1:      ldrb    w0, [x12], #1
        cbz     w0, 2f
        bl      putc
        b       1b
2:      ret     x15

/*
 * EL2 as the hypervisor leaves it for a guest, without the hypervisor. Clobbers x0-x3, x16.
 */
el2_setup:
        mov     x16, x30
        adr     x0, el2_vectors
        msr     vbar_el2, x0
        isb

        /* hv_init(): API, APK, E2H (forced on this core), RW. Not VM, TEA, AMO; not FMO:
         * timer, IPI and PMC FIQs are taken at EL1. TGE clear: EL1 exists. */
        mov     x0, #(HCR_RW)
        movk    x0, #((HCR_API | HCR_APK | HCR_E2H) >> 32), lsl #32
        msr     hcr_el2, x0
        isb
        msr     hacr_el2, xzr                   /* no implementation-defined traps */
        msr     mdcr_el2, xzr                   /* no debug traps */
        mrs     x0, cptr_el2                    /* E2H layout: FPEN, no FP/SIMD trap */
        orr     x0, x0, #(3 << 20)
        msr     cptr_el2, x0
        mov     x0, #((1 << 11) | (1 << 10))    /* CNTHCTL_EL1PTEN | CNTHCTL_EL1PCTEN */
        msr     cnthctl_el2, x0
        msr     cntvoff_el2, xzr
        msr     cnthp_ctl_el2, xzr              /* m1n1's own timers stay off */
        msr     cnthv_ctl_el2, xzr
        mov     x0, #3                          /* the EL1 timers raise FIQs */
        msr     VM_TMR_FIQ_ENA, x0
        mrs     x0, midr_el1
        msr     vpidr_el2, x0
        mrs     x0, mpidr_el1
        msr     vmpidr_el2, x0
        msr     vttbr_el2, xzr

        /* EL1 starts with its MMU and caches off, as XNU's boot protocol has it. */
        mrs     x0, SCTLR_EL12
        mov     x1, #((1 << 0) | (1 << 2) | (1 << 12))
        bic     x0, x0, x1
        bic     x0, x0, #(1 << 19)              /* WXN */
        msr     SCTLR_EL12, x0
        mrs     x0, ACTLR_EL12_APL              /* init(): EnMDSB */
        orr     x0, x0, #(1 << 12)
        msr     ACTLR_EL12_APL, x0

        /* init(): the keys EL1's pointer authentication uses under EL2 */
        ldr     x0, =0x4E7672476F6E6147
        msr     APVMKEYLO, x0
        ldr     x0, =0x697665596F755570
        msr     APVMKEYHI, x0
        mov     x0, #1
        msr     APSTS_EL12, x0

        /* hv_init(): WFI mode back to its default */
        mrs     x0, CYC_OVRD
        bic     x0, x0, #(3 << 24)
        msr     CYC_OVRD, x0

        adr     x2, param_flags
        ldr     x2, [x2]
        tbz     x2, #1, 1f
        mrs     x0, AMX_CTL_EL2
        orr     x0, x0, #(1 << 62)
        msr     AMX_CTL_EL2, x0
1:      tbz     x2, #0, 2f
        mov     x0, #1
        msr     SPRR_CONFIG, x0                 /* start(): "Enabling SPRR" */
        isb
        msr     GXF_CONFIG, x0                  /* start(): "Enabling GXF"; last, see hv_write_hcr() */
2:      isb
        ret     x16

/* Enter XNU at x19 with x0 = x20, at EL1h, interrupts masked. */
enter_el1:
        adr     x0, el1_vectors
        msr     VBAR_EL12, x0
        mov     x0, #0x3c5                      /* DAIF masked, EL1h */
        msr     spsr_el2, x0
        msr     elr_el2, x19
        msr     sp_el0, xzr
        msr     sp_el1, xzr
        mov     x0, x20
        mov     x1, xzr
        mov     x2, xzr
        mov     x3, xzr
        isb
        eret

boot_entry:
        mov     x19, x0
        mov     x20, x1
        adr     x0, msg_boot
        bl      puts
        bl      el2_setup
        adr     x0, msg_el2
        bl      puts
        mov     x0, x19
        bl      puthex
        /* what memory holds at the entry, as this core reads it with its MMU off */
        adr     x0, msg_code
        bl      puts
        ldr     x0, [x19]
        bl      puthex
        mov     x0, #' '
        bl      putc
        ldr     x0, [x19, #8]
        bl      puthex
        adr     x0, msg_nl
        bl      puts
        b       enter_el1

secondary_reset:
        msr     daifset, #0xf
        bl      el2_setup
        adr     x19, param_secondary_entry
        ldr     x19, [x19]
        mov     x20, xzr
        cbnz    x19, enter_el1
        adr     x0, msg_nosec
        bl      puts
1:      wfe
        b       1b

/*
 * A secondary core, started and initialised by m1n1 (smp_start_secondaries: chicken bits,
 * as for Linux) and sent here from m1n1's own idle loop, which it never returns to. It
 * waits until the kernel writes its reset vector register (SFAppleBootCPUPMGR, sfcpuheld=1),
 * then enters that vector at EL1, as m1n1's hypervisor did on the kernel's write to the
 * CPU-start block.
 */
hold_entry:
        msr     daifset, #0xf
        mov     x21, x0
        mov     x22, x1
        str     x3, [x2]                        /* while m1n1's MMU and caches are still on */
        dsb     sy
        mrs     x4, sctlr_el2
        mov     x5, #((1 << 0) | (1 << 2) | (1 << 12))
        bic     x4, x4, x5
        msr     sctlr_el2, x4
        isb
        ldr     x23, =0x00000ffffffff000        /* RVBAR_ADDR as SFAppleBootCPUPMGR has it */
        and     x22, x22, x23
1:      ldr     x4, [x21]
        and     x4, x4, x23
        cmp     x4, x22
        b.ne    2f
        wfe
        b       1b
2:      mov     x19, x4
        mov     x20, xzr
        bl      el2_setup
        b       enter_el1

/* x17: vector number, x18: 2 for EL2, 1 for EL1. Never returns. */
report:
        adr     x0, msg_exc
        bl      puts
        add     x0, x18, #'0'
        bl      putc
        adr     x0, msg_v
        bl      puts
        mov     x0, x17
        bl      puthex
        adr     x0, msg_esr
        bl      puts
        cmp     x18, #2
        b.ne    1f
        mrs     x1, esr_el2
        mrs     x2, elr_el2
        mrs     x3, far_el2
        mrs     x4, spsr_el2
        b       2f
1:      mrs     x1, esr_el1
        mrs     x2, elr_el1
        mrs     x3, far_el1
        mrs     x4, spsr_el1
2:      mov     x5, x30                         /* the interrupted code's link register */
        mov     x0, x1
        bl      puthex
        adr     x0, msg_elr
        bl      puts
        mov     x0, x2
        bl      puthex
        adr     x0, msg_far
        bl      puts
        mov     x0, x3
        bl      puthex
        adr     x0, msg_spsr
        bl      puts
        mov     x0, x4
        bl      puthex
        adr     x0, msg_cpu
        bl      puts
        mrs     x0, mpidr_el1
        bl      puthex
        adr     x0, msg_nl
        bl      puts
3:      wfe
        b       3b

msg_boot:       .asciz "\r\nPDNB stub at EL2\r\n"
msg_el2:        .asciz "PDNB EL2 configured, entering EL1 at "
msg_code:       .asciz " code "
msg_nosec:      .asciz "\r\nPDNB secondary reset without an entry\r\n"
msg_exc:        .asciz "\r\nPDNB EXCEPTION at EL"
msg_v:          .asciz " vector "
msg_esr:        .asciz " esr "
msg_elr:        .asciz " elr "
msg_far:        .asciz " far "
msg_spsr:       .asciz " spsr "
msg_cpu:        .asciz " mpidr "
msg_nl:         .asciz "\r\n"
        .balign 8
        .ltorg

        .macro  vectors level
        .set    n, 0
        .rept   16
        .balign 0x80
        mov     x17, #n
        mov     x18, #\level
        b       report
        .set    n, n + 1
        .endr
        .endm

        .balign 0x800
el2_vectors:                                    /* 0x0800 */
        vectors 2

        .balign 0x800
el1_vectors:                                    /* 0x1000 */
        vectors 1

        .org    0x3f00
param_magic:            .quad 0x31424e4450       /* "PDNB1": the page keeps the bytes it booted with */
param_uart:             .quad 0                 /* physical base of uart0, 0: silent */
param_flags:            .quad 0
param_secondary_entry:  .quad 0                 /* XNU's reset vector, physical; 0: none */
        .org    0x3f40
hold_acks:              .space 64
        .org    0x4000
