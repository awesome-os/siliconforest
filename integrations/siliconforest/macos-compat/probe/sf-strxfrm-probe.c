// SPDX-License-Identifier: MIT
// Probe for libSystem's strxfrm()/wcsxfrm() in a UTF-8 locale and for timingsafe_bcmp
// (../../libsystem-default/patches/strxfrm-without-directives.patch). A libSystem without
// the patch does not export timingsafe_bcmp, so dyld refuses this program; before the
// patch strxfrm() in en_US.UTF-8 ended the caller with SIGSEGV.
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

extern int timingsafe_bcmp(const void *, const void *, size_t);

int main(void)
{
    char out[64] = "";
    wchar_t wout[64] = L"";
    const char *l = setlocale(LC_ALL, "en_US.UTF-8");
    size_t n = strxfrm(out, "caf\xc3\xa9", sizeof(out));
    size_t small = strxfrm(out, "caf\xc3\xa9", 3);
    size_t w = wcsxfrm(wout, L"abc", 64);
    int order = strcoll("a", "b");
    int same = timingsafe_bcmp("abc", "abc", 3), differ = timingsafe_bcmp("abc", "abd", 3);
    int ok = l && n > 0 && small == n && strlen(out) == 2 && w > 0 && order < 0 && same == 0 && differ != 0;
    printf("SFSTRXFRM %s locale=%s strxfrm=%zu (3-byte buffer: returns %zu, wrote %zu) wcsxfrm=%zu strcoll(a,b)=%d timingsafe_bcmp=%d,%d\n",
           ok ? "ok" : "FAIL", l ? l : "(refused)", n, small, strlen(out), w, order, same, differ);
    return ok ? 0 : 1;
}
