// SPDX-License-Identifier: MIT
// sf-trace: one eight-byte entry per traced method (its index in w17), the common entry that
// logs and enters the method with the return address replaced, and the return path.
    .text
    .p2align 2
    .globl _sftrace_thunks
_sftrace_thunks:
    .set idx, 0
    .rept 4096
    mov w17, #idx
    b Lcommon
    .set idx, idx + 1
    .endr
Lcommon:
    sub sp, sp, #0xa0
    stp x0, x1, [sp, #0x00]
    stp x2, x3, [sp, #0x10]
    stp x4, x5, [sp, #0x20]
    stp x6, x7, [sp, #0x30]
    stp x8, x17, [sp, #0x40]
    stp x29, x30, [sp, #0x50]
    stp d0, d1, [sp, #0x60]
    stp d2, d3, [sp, #0x70]
    stp d4, d5, [sp, #0x80]
    stp d6, d7, [sp, #0x90]
    mov x0, sp
    bl _sftrace_enter
    mov x17, x0
    ldp x0, x1, [sp, #0x00]
    ldp x2, x3, [sp, #0x10]
    ldp x4, x5, [sp, #0x20]
    ldp x6, x7, [sp, #0x30]
    ldr x8, [sp, #0x40]
    ldr x29, [sp, #0x50]
    ldp d0, d1, [sp, #0x60]
    ldp d2, d3, [sp, #0x70]
    ldp d4, d5, [sp, #0x80]
    ldp d6, d7, [sp, #0x90]
    add sp, sp, #0xa0
    adr x30, Lreturn
    br x17
Lreturn:
    sub sp, sp, #0x40
    stp x0, x1, [sp, #0x00]
    str x8, [sp, #0x10]
    stp d0, d1, [sp, #0x20]
    stp d2, d3, [sp, #0x30]
    mov x0, sp
    bl _sftrace_leave
    mov x30, x0
    ldp x0, x1, [sp, #0x00]
    ldr x8, [sp, #0x10]
    ldp d0, d1, [sp, #0x20]
    ldp d2, d3, [sp, #0x30]
    add sp, sp, #0x40
    ret
