/* SPDX-License-Identifier: MIT
 * Instructions from proxyclient/tools/chainload.py at exact121.
 * The sole host-patched field is the 8-byte entry literal at offset 0x60.
 */
.text
.globl _start
_start:
    mov x7, x2
    mov x8, x3
1:
    ldp x4, x5, [x1], #16
    stp x4, x5, [x2]
    add x2, x2, #16
    subs x3, x3, #16
    b.ne 1b
    mov x2, x7
    mov x3, x8
2:
    dc cvac, x2
    add x2, x2, #64
    subs x3, x3, #64
    b.ne 2b
    dsb sy
    mov x2, x7
    mov x3, x8
3:
    ic ivau, x2
    add x2, x2, #64
    subs x3, x3, #64
    b.ne 3b
    dsb sy
    isb
    ldr x1, =0x1122334455667788
    br x1
.pool
