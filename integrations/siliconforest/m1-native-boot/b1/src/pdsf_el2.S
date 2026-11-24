/* SPDX-License-Identifier: MIT */
/* The resident EL2 page (m1-native-boot/stub/el2_drop.S), assembled by b1/build.sh. */
        .section .rodata.pdsf_el2, "a"
        .balign 16
        .globl _pdsf_el2_start
        .globl _pdsf_el2_end
_pdsf_el2_start:
        .incbin "build/pdsf_el2.bin"
_pdsf_el2_end:
