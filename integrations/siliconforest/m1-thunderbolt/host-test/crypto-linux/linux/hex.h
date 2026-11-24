/* SPDX-License-Identifier: MIT */
#include <ctype.h>
static inline int hex_to_bin(unsigned char c) { return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1; }
