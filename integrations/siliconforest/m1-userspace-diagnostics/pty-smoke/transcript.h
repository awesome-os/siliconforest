#ifndef PTY_SMOKE_TRANSCRIPT_H
#define PTY_SMOKE_TRANSCRIPT_H

#include <stddef.h>

/* Finite /bin/sh -c script. It is not an interactive shell.
 * The script reads two PTY lines, prints their concatenation, then
 * /bin/uname -s, -m, and -r, then exits 0. It contains neither nonce half.
 */
const char *smoke_script(void);

/* Exactly 48 hex digits, no prefix and no trailing junk. */
int nonce_hex48(const char *text);

/* Two lines, half + newline + half + newline. Rejects a buffer that
 * already contains the joined nonce as contiguous bytes.
 */
int tx_halves(const char *nonce, char *dst, size_t dst_len, size_t *out_len);

int contains_span(const char *hay, size_t hay_len, const char *needle, size_t needle_len);

/* Four newline-terminated lines, adjacent, after stripping one trailing CR:
 * the joined nonce, Darwin, arm64, and one release token that contains a digit.
 * Surrounding lines are allowed. A blank or other line between them fails.
 */
int adjacent_uname(const char *buf, size_t len, const char *nonce);

#endif
