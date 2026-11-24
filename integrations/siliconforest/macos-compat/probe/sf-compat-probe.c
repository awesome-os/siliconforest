// SPDX-License-Identifier: MIT
// Probe for the CoreServices text-encoding calls Homebrew's vim and neovim need
// (../patches/coreservices-text-encoding-converter.patch). Prints one line per check and
// "SFCOMPAT ok" only if every one holds; with a CoreServices that lacks the calls dyld
// refuses the program (that is how sf-lib-install.sh tells the old library from the new).
#include <CoreServices/CoreServices.h>
#include <stdio.h>
#include <string.h>

static int bad;

static void hex(const UInt8 *p, ByteCount n, char *out)
{
    for (ByteCount i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", p[i]);
    out[2 * n] = 0;
}

static void check(const char *what, int ok, const char *detail)
{
    printf("SFCOMPAT %s %s %s\n", ok ? "ok  " : "FAIL", what, detail);
    if (!ok) bad++;
}

static void convert(const char *what, TextEncoding in, TextEncoding out, const UInt8 *src, ByteCount n, const char *want)
{
    TECObjectRef c = NULL;
    UInt8 buf[64];
    char got[160] = "", line[256];
    ByteCount used = 0, made = 0, more = 0;
    OSStatus s = TECCreateConverter(&c, in, out);
    OSStatus t = s == noErr ? TECConvertText(c, src, n, &used, buf, sizeof(buf), &made) : 0;
    OSStatus f = s == noErr ? TECFlushText(c, buf + made, sizeof(buf) - made, &more) : 0;

    if (s == noErr) hex(buf, made + more, got);
    snprintf(line, sizeof(line), "create=%d convert=%d flush=%d in=%lu out=%s want=%s", (int)s, (int)t, (int)f, (unsigned long)used, got, want);
    check(what, s == noErr && t == noErr && f == noErr && used == n && !strcmp(got, want), line);
    if (c) TECDisposeConverter(c);
}

int main(void)
{
    char buf[64] = "?", line[256];
    TextEncoding u8 = CreateTextEncoding(kTextEncodingUnicodeDefault, kTextEncodingDefaultVariant, kUnicodeUTF8Format);
    TextEncoding u8hfsc = CreateTextEncoding(kTextEncodingUnicodeDefault, kUnicodeHFSPlusCompVariant, kUnicodeUTF8Format);
    TextEncoding u8hfsd = CreateTextEncoding(kTextEncodingUnicodeDefault, kUnicodeHFSPlusDecompVariant, kUnicodeUTF8Format);
    TextEncoding u8c = CreateTextEncoding(kTextEncodingUnicodeDefault, kUnicodeCanonicalCompVariant, kUnicodeUTF8Format);
    TextEncoding u8d = CreateTextEncoding(kTextEncodingUnicodeDefault, kUnicodeCanonicalDecompVariant, kUnicodeUTF8Format);
    TextEncoding u16 = CreateTextEncoding(kTextEncodingUnicodeDefault, kTextEncodingDefaultVariant, kUnicode16BitFormat);

    snprintf(line, sizeof(line), "utf8=%#x (CFString's is %#x)", (unsigned)u8, (unsigned)kCFStringEncodingUTF8);
    check("CreateTextEncoding packs as CFStringEncoding does", u8 == kCFStringEncodingUTF8 && u16 == kCFStringEncodingUTF16, line);

    OSStatus s = LocaleRefGetPartString(NULL, kLocaleLanguageMask | kLocaleLanguageVariantMask | kLocaleRegionMask | kLocaleRegionVariantMask,
                                        sizeof(buf) - 10, buf);
    snprintf(line, sizeof(line), "status=%d string=[%s]", (int)s, buf);
    check("LocaleRefGetPartString(current locale)", s == noErr, line);
    LocaleRef de = NULL;
    s = LocaleRefFromLocaleString("de_Latn_CH", &de);
    OSStatus s2 = LocaleRefGetPartString(de, kLocaleLanguageMask | kLocaleRegionMask, sizeof(buf), buf);
    snprintf(line, sizeof(line), "status=%d,%d string=[%s]", (int)s, (int)s2, buf);
    check("LocaleRefGetPartString(de_Latn_CH, language|region) = de_CH", s == noErr && s2 == noErr && !strcmp(buf, "de_CH"), line);
    s2 = LocaleRefGetPartString(de, kLocaleAllPartsMask, 4, buf);
    snprintf(line, sizeof(line), "status=%d", (int)s2);
    check("a buffer too small is refused, not overrun", s2 == kLocalesBufferTooSmallErr, line);

    static const UInt8 nfd[] = { 'c', 'a', 'f', 'e', 0xcc, 0x81 }, nfc[] = { 'c', 'a', 'f', 0xc3, 0xa9 };
    static const UInt8 kelvin[] = { 0xe2, 0x84, 0xaa };      // U+212A KELVIN SIGN: canonically K, in HFS+'s exempt range
    static const UniChar w[] = { 'e', 0x0301 };
    convert("UTF-8 -> UTF-8 HFS+ composed", u8, u8hfsc, nfd, sizeof(nfd), "636166c3a9");
    convert("UTF-8 -> UTF-8 HFS+ decomposed", u8, u8hfsd, nfc, sizeof(nfc), "63616665cc81");
    convert("UTF-8 -> UTF-8 canonically composed", u8, u8c, nfd, sizeof(nfd), "636166c3a9");
    convert("UTF-8 -> UTF-8 canonically decomposed", u8, u8d, nfc, sizeof(nfc), "63616665cc81");
    convert("UTF-8 -> UTF-8, no normal form asked for", u8, u8, nfd, sizeof(nfd), "63616665cc81");
    convert("HFS+ decomposition leaves U+212A alone", u8, u8hfsd, kelvin, sizeof(kelvin), "e284aa");
    convert("canonical decomposition turns U+212A into K", u8, u8d, kelvin, sizeof(kelvin), "4b");
    convert("UTF-16 -> UTF-8 canonically composed", u16, u8c, (const UInt8 *)w, sizeof(w), "c3a9");
    convert("UTF-8 -> UTF-16", u8, u16, (const UInt8 *)"A", 1, "4100");

    TECObjectRef c = (TECObjectRef)1;
    s = TECCreateConverter(&c, CreateTextEncoding(0x7777, 0, 0), u8);
    snprintf(line, sizeof(line), "status=%d converter=%p", (int)s, (void *)c);
    check("an encoding nobody has is refused", s == kTextUnsupportedEncodingErr && c == NULL, line);

    UInt8 small[2], rest[8];
    ByteCount used = 0, made = 0, more = 0;
    s = TECCreateConverter(&c, u8, u8);
    OSStatus t = TECConvertText(c, (const UInt8 *)"hello", 5, &used, small, sizeof(small), &made);
    OSStatus f = TECFlushText(c, rest, sizeof(rest), &more);
    snprintf(line, sizeof(line), "convert=%d in=%lu out=%lu flush=%d out=%lu", (int)t, (unsigned long)used, (unsigned long)made, (int)f, (unsigned long)more);
    check("an output buffer too small: the rest comes with the flush", s == noErr && t == kTECOutputBufferFullStatus && used == 5 && made == 2 &&
          f == noErr && more == 3 && !memcmp(small, "he", 2) && !memcmp(rest, "llo", 3), line);
    t = TECConvertText(c, (const UInt8 *)"a\xc3", 2, &used, rest, sizeof(rest), &made);
    snprintf(line, sizeof(line), "convert=%d in=%lu out=%lu", (int)t, (unsigned long)used, (unsigned long)made);
    check("half a character at the end is reported and not consumed", t == kTECPartialCharErr && used == 1 && made == 1, line);
    TECDisposeConverter(c);

    if (bad) {
        printf("SFCOMPAT failed=%d\n", bad);
        return 1;
    }
    printf("SFCOMPAT ok\n");
    return 0;
}
