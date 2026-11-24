# Gap scan of the M1 disk root, 13 November 2026, with the fixes of this job

The same copy with what `../../macos-compat/tools/sf_compat_stage.sh` stages laid over it (`--overlay`): CoreServices, libexpat, libncurses and libSystem as rebuilt, AppKit/Cocoa and the system Perl as new files.

## Homebrew prefix (/opt/homebrew/Cellar)

533 Mach-O images checked directly, 202 programs with their load closure. **0 owners (formulae) have at least one program that does not start; 0 programs in all.**

| # | Missing | Kind | Symbols | Formulae with a stopped program | Formulae with the gap in any image | Binding |
|---|---|---|---|---|---|---|
| 1 | CoreServices.framework | symbols absent | `_AEBuildAppleEvent`, `_AEDisposeDesc`, `_AEGetDescData`, `_AEGetParamDesc`, `_AESendMessage` | 0:  | 1: python@3.14 | - |

### Programs that do not start, by formula

| Formula | Programs | First gaps |
|---|---|---|

### Interpreters that scripts in the prefix name (outside the prefix)

| Interpreter | In the root | Formulae | Scripts |
|---|---|---|---|
| `/usr/bin/env python` | **no** | 4: git, libevent, libpsl, python@3.14 | 7 |
| `/usr/bin/python` | **no** | 2: python@3.14, vim | 2 |
| `./python` | **no** | 1: python@3.14 | 1 |
| `/bin/csh` | **no** | 1: vim | 1 |
| `/usr/bin/awk` | **no** | 1: vim | 1 |
| `/usr/bin/env python3.8` | **no** | 1: python@3.14 | 2 |
| `/usr/bin/make` | **no** | 1: ncurses | 3 |
| `/usr/bin/python3` | **no** | 1: git | 1 |
| `/bin/sh` | yes | 18: bash, findutils, gawk, gettext, git, grep, icu4c@79, jemalloc, less, ncurses … | 172 |
| `/usr/bin/env perl` | yes | 5: git, jemalloc, ncurses, openssl@4, vim | 10 |
| `/bin/bash` | yes | 3: ca-certificates, node, python@3.14 | 6 |
| `/usr/bin/env bash` | yes | 3: fzf, node, python@3.14 | 8 |
| `/usr/bin/env python3` | yes | 2: node, python@3.14 | 39 |
| `/usr/bin/perl` | yes | 2: git, vim | 17 |
| `/usr/bin/env lua` | yes | 1: lpeg | 1 |
| `/usr/bin/env node` | yes | 1: node | 12 |
| `/usr/bin/env sh` | yes | 1: node | 2 |

## The root's own frameworks, libraries and programs

332 Mach-O images checked directly, 129 programs with their load closure. **0 owners (formulae) have at least one program that does not start; 0 programs in all.**

| # | Missing | Kind | Symbols | Formulae with a stopped program | Formulae with the gap in any image | Binding |
|---|---|---|---|---|---|---|

### Programs that do not start, by formula

| Formula | Programs | First gaps |
|---|---|---|
