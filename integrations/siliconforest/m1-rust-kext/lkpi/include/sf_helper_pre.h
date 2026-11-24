/* SPDX-License-Identifier: GPL-2.0 */
/* Force-included when one rust/helpers file is compiled on its own: what Linux's helpers.c
 * sets up before including them all. Not inline: the Rust object calls them. */
#include <linux/compiler_types.h>
#define __rust_helper
