# Gap scan of the M1 disk root, 13 November 2026, before any fix

Made by `../sf_gapscan.py --root <copy of the M1 trees> --brew --system` over a copy of `/opt/homebrew`, `/usr/lib`, `/System`, `/lib`, `/bin`, `/sbin`, `/usr/bin`, `/usr/sbin`, `/usr/libexec`, `/libexec`, `/usr/local` read from the running system (71 formulae installed there).

## Homebrew prefix (/opt/homebrew/Cellar)

533 Mach-O images checked directly, 202 programs with their load closure. **2 owners (formulae) have at least one program that does not start; 2 programs in all.**

| # | Missing | Kind | Symbols | Formulae with a stopped program | Formulae with the gap in any image | Binding |
|---|---|---|---|---|---|---|
| 1 | CoreServices.framework | symbols absent | `_AEBuildAppleEvent`, `_AEDisposeDesc`, `_AEGetDescData`, `_AEGetParamDesc`, `_AESendMessage`, `_CreateTextEncoding`, `_LocaleRefGetPartString`, `_TECConvertText` … (11) | 2: neovim, vim | 3: neovim, python@3.14, vim | at load |
| 2 | AppKit.framework | library absent | `_NSApplicationMain`, `_NSPasteboardTypeString`, `_NSRunAlertPanel`, `_OBJC_CLASS_$_NSApplication`, `_OBJC_CLASS_$_NSDocument`, `_OBJC_CLASS_$_NSPasteboard`, `_OBJC_CLASS_$_NSSound`, `_OBJC_CLASS_$_NSWindowController` … (11) | 1: vim | 2: python@3.14, vim | at load |
| 3 | libperl.dylib | library absent | `_PL_thr_key`, `_PerlIOBase_pushed`, `_PerlIO_define_layer`, `_Perl_av_fetch`, `_Perl_av_len`, `_Perl_call_argv`, `_Perl_call_method`, `_Perl_call_pv` … (57) | 1: vim | 1: vim | at load |
| 4 | libexpat.1.dylib | symbols absent | `_XML_ErrorString`, `_XML_ExpatVersion`, `_XML_ExpatVersionInfo`, `_XML_ExternalEntityParserCreate`, `_XML_FreeContentModel`, `_XML_GetBase`, `_XML_GetBuffer`, `_XML_GetCurrentByteIndex` … (57) | 0:  | 2: git, python@3.14 | - |
| 5 | Carbon.framework | library absent | `_GetCurrentKeyModifiers` | 0:  | 1: python@3.14 | - |
| 6 | libncurses.5.4.dylib | symbols absent | `_assume_default_colors$NCURSES60`, `_color_content$NCURSES60`, `_copywin$NCURSES60`, `_delay_output$NCURSES60`, `_derwin$NCURSES60`, `_doupdate$NCURSES60`, `_endwin$NCURSES60`, `_flushinp$NCURSES60` … (68) | 0:  | 1: python@3.14 | - |

### Programs that do not start, by formula

| Formula | Programs | First gaps |
|---|---|---|
| neovim | 1: nvim | symbol CoreServices.framework:_LocaleRefGetPartString |
| vim | 1: vim | library AppKit.framework; library libperl.dylib; symbol CoreServices.framework:_CreateTextEncoding; symbol CoreServices.framework:_LocaleRefGetPartString … (8) |

### Interpreters that scripts in the prefix name (outside the prefix)

| Interpreter | In the root | Formulae | Scripts |
|---|---|---|---|
| `/usr/bin/env perl` | **no** | 5: git, jemalloc, ncurses, openssl@4, vim | 10 |
| `/usr/bin/env python` | **no** | 4: git, libevent, libpsl, python@3.14 | 7 |
| `/usr/bin/perl` | **no** | 2: git, vim | 17 |
| `/usr/bin/python` | **no** | 2: python@3.14, vim | 2 |
| `./python` | **no** | 1: python@3.14 | 1 |
| `/bin/csh` | **no** | 1: vim | 1 |
| `/usr/bin/awk` | **no** | 1: vim | 1 |
| `/usr/bin/env python3.8` | **no** | 1: python@3.14 | 2 |
| `/usr/bin/make` | **no** | 1: ncurses | 3 |
| `/usr/bin/python3` | **no** | 1: git | 1 |
| `/bin/sh` | yes | 18: bash, findutils, gawk, gettext, git, grep, icu4c@79, jemalloc, less, ncurses … | 172 |
| `/bin/bash` | yes | 3: ca-certificates, node, python@3.14 | 6 |
| `/usr/bin/env bash` | yes | 3: fzf, node, python@3.14 | 8 |
| `/usr/bin/env python3` | yes | 2: node, python@3.14 | 39 |
| `/usr/bin/env lua` | yes | 1: lpeg | 1 |
| `/usr/bin/env node` | yes | 1: node | 12 |
| `/usr/bin/env sh` | yes | 1: node | 2 |

## The root's own frameworks, libraries and programs

236 Mach-O images checked directly, 128 programs with their load closure. **0 owners (formulae) have at least one program that does not start; 0 programs in all.**

| # | Missing | Kind | Symbols | Formulae with a stopped program | Formulae with the gap in any image | Binding |
|---|---|---|---|---|---|---|

### Programs that do not start, by formula

| Formula | Programs | First gaps |
|---|---|---|
