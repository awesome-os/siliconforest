/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Force-included right after sf_lkpi_pre.h, for bindgen runs only (never when compiling C).
 *
 * Haiku's KernelExport.h has `typedef struct { ... } seqlock;` and LinuxKPI has
 * `struct seqlock`. C keeps a typedef name and a struct tag apart, Rust has one namespace,
 * so bindgen would emit two types called `seqlock`. Give Haiku's a different name here; its
 * header guard keeps the later, ordinary inclusion from redefining it.
 */
#define seqlock haiku_seqlock
#include <KernelExport.h>
#undef seqlock
