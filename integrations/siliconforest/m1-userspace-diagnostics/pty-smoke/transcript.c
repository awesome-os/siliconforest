#include "transcript.h"

#include <string.h>

const char *smoke_script(void)
{
	return "read a || exit 2; read b || exit 2; "
		"printf '%s%s\\n' \"$a\" \"$b\"; "
		"/bin/uname -s; /bin/uname -m; /bin/uname -r; exit 0";
}

int nonce_hex48(const char *text)
{
	size_t i;

	if (text == NULL) {
		return 0;
	}
	for (i = 0; i < 48; i++) {
		char c = text[i];
		int hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
			(c >= 'A' && c <= 'F');
		if (!hex) {
			return 0;
		}
	}
	return text[48] == '\0';
}

int contains_span(const char *hay, size_t hay_len, const char *needle, size_t needle_len)
{
	size_t i;

	if (needle_len == 0 || hay == NULL || needle == NULL || hay_len < needle_len) {
		return 0;
	}
	for (i = 0; i + needle_len <= hay_len; i++) {
		if (memcmp(hay + i, needle, needle_len) == 0) {
			return 1;
		}
	}
	return 0;
}

int tx_halves(const char *nonce, char *dst, size_t dst_len, size_t *out_len)
{
	if (!nonce_hex48(nonce) || dst == NULL || dst_len < 50) {
		return 0;
	}
	memcpy(dst, nonce, 24);
	dst[24] = '\n';
	memcpy(dst + 25, nonce + 24, 24);
	dst[49] = '\n';
	if (contains_span(dst, 50, nonce, 48)) {
		return 0;
	}
	if (out_len != NULL) {
		*out_len = 50;
	}
	return 1;
}

static int release_token(const char *line, size_t n)
{
	size_t i;
	int digit = 0;

	if (n == 0 || n > 64) {
		return 0;
	}
	for (i = 0; i < n; i++) {
		char c = line[i];
		int ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
			(c >= 'A' && c <= 'Z') || c == '.' || c == '_' || c == '+' || c == '-';
		if (!ok) {
			return 0;
		}
		if (c >= '0' && c <= '9') {
			digit = 1;
		}
	}
	return digit;
}

static int line_eq(const char *line, size_t n, const char *lit)
{
	size_t want = strlen(lit);
	return n == want && memcmp(line, lit, want) == 0;
}

int adjacent_uname(const char *buf, size_t len, const char *nonce)
{
	size_t starts[4];
	size_t lens[4];
	size_t count = 0;
	size_t i = 0;

	if (buf == NULL || !nonce_hex48(nonce)) {
		return 0;
	}
	while (i < len) {
		size_t start = i;
		size_t n;
		while (i < len && buf[i] != '\n') {
			i++;
		}
		if (i >= len) {
			break;
		}
		n = i - start;
		i++;
		if (n > 0 && buf[start + n - 1] == '\r') {
			n--;
		}
		if (count < 4) {
			starts[count] = start;
			lens[count] = n;
			count++;
		} else {
			starts[0] = starts[1];
			lens[0] = lens[1];
			starts[1] = starts[2];
			lens[1] = lens[2];
			starts[2] = starts[3];
			lens[2] = lens[3];
			starts[3] = start;
			lens[3] = n;
		}
		if (count == 4 &&
			line_eq(buf + starts[0], lens[0], nonce) &&
			line_eq(buf + starts[1], lens[1], "Darwin") &&
			line_eq(buf + starts[2], lens[2], "arm64") &&
			release_token(buf + starts[3], lens[3])) {
			return 1;
		}
	}
	return 0;
}
