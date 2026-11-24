## Goal
Unmodified Homebrew arm64 macOS packages ("bottles": prebuilt Mach-O programs for macOS on Apple Silicon) run on SiliconForest. Everything here is tested in QEMU on the workstation (arm64 `virt`, TCG, the qemu-dev stream's image and runner); nothing was run on the M1.

Bottles are downloaded by the person running the tools, at run time, into `runs/homebrew/` (not committed, not baked into any image).

## Status (2026-11-11)
| Item | State |
|---|---|
| Static survey (48 formulae, 133 bottles, 1,403 Mach-O files) | done; rerun against each system root |
| Bottles poured on the host into a prefix disk, mounted read-write at `/opt/homebrew` in QEMU | works |
| Smoke tests executed in QEMU, today's image | 8 of 28 pass (run `r2`) |
| Smoke tests executed in QEMU, with the overlay of this ticket | 42 of 51 pass (runs `r8`, `r7`, `r9`, `r10`); the failures are the predicted ones |
| Static: main program startable, today's image → with the overlay | 13 of 48 → 41 of 48 |
| The real `brew` client in QEMU | **works for bottles** (run `b2`): `brew install tree` and `brew install jq` (with its dependency) downloaded, poured and ran, with five things supplied by hand (below) |
| Building formulae from source on the device | assessed only (below) |

"Executed" below always means a program ran in the QEMU guest and its output was checked. "Static" means read from Mach-O files on the host: it predicts what dyld accepts, not whether a program works.

## The answer in short
- Yes: QEMU is enough to develop this. A bottle is an ordinary arm64 Mach-O; the same binaries will run on the M1.
- The system needs no emulation layer for bottles. What was missing is ordinary: one broken function in libSystem, exports the arm64 libSystem left out, and the `/usr/lib` libraries that macOS ships and bottles therefore do not bring.
- Code signatures are not an obstacle: this kernel does not enforce them (below).

## Root cause that matters beyond Homebrew: `___chkstk_darwin`
In the lab's arm64 libSystem, `___chkstk_darwin` (the stack probe clang calls at the start of every function with more than a page of stack frame; size in x9, all registers preserved) is not a probe. `llvm-nm -n` shows it at the same address as `sandbox_check()`; the code there returns 0 in x0. A function with a large frame therefore loses its first argument.
- Seen as: jq (SIGSEGV in `yyparse`), GNU sed, GNU grep ("pattern passed as NULL"), perl (`Perl_sv_gets`), ncurses `infocmp` (sprintf to NULL). Backtraces from the guest are in `runs/homebrew/qemu/r2/results.json`.
- Source: `src/Libraries/libSystem/stub/pd_chkstk_darwin.s` defines `___chkstk_darwin` and `____chkstk_darwin` on one `ret`; libpthread's `pthread_asm.s` also defines `____chkstk_darwin` (the real probe). In the linked image the stub's `ret` is gone and its remaining label points at the next function. The linker step that drops it was not traced.
- Fix: on arm64 `___chkstk_darwin` is `b ____chkstk_darwin`. Confirmed by disassembly of the new build and by the five programs passing afterwards (run `r3`).
- Affects every arm64 program on SiliconForest with a frame over 4 KiB, including the lab's own. **Fold this hunk into the default libSystem.**

## How bottles are fetched and poured (verified against Homebrew's servers and source, commit `8679d30f`)
- Index: `https://formulae.brew.sh/api/formula.json` (8,728 formulae): version, `dependencies`, `uses_from_macos`, bottle blobs per tag, declarative `post_install_steps`.
- Blob: `https://ghcr.io/v2/homebrew/core/<name>/blobs/sha256:<digest>` with a bearer token from `https://ghcr.io/token?service=ghcr.io&scope=repository:homebrew/core/<name>:pull` (anonymous). Homebrew's own constant `Bearer QQ==` also works.
- Tags for Apple Silicon today: `arm64_golden_gate`, `arm64_tahoe` (macOS 26), `arm64_sequoia` (15), a few `arm64_sonoma`; `all` for bottles without machine code. Most formulae no longer have a Sonoma bottle.
- Prefix `/opt/homebrew`; a bottle unpacks to `Cellar/<name>/<version>/`. Placeholders `@@HOMEBREW_PREFIX@@`, `@@HOMEBREW_CELLAR@@` (also `_REPOSITORY`, `_LIBRARY`, `_PERL`, `_JAVA`) sit in Mach-O install names and in text files and are replaced at pour time (`keg_relocate.rb`). Of 133 bottles, 90 are `cellar :any` (need this), 31 are built for `/opt/homebrew/Cellar` only, 12 need nothing.
- Rewriting an install name changes the first code page, so the ad-hoc signature no longer matches; `brew` re-signs with `codesign --sign - --force`. `hb_macho.py` does both on the host: load commands are re-laid-out within the header padding and the page hashes of the CodeDirectory are recomputed in place.
- All 1,403 Mach-O files are ad-hoc signed (1,038 plain, 365 linker-signed). 951 use chained fixups, 452 the older dyld-info opcodes. Minimum OS: 26.0 for most, 11.0–13.5 for Rust and Go programs, one neovim file 26.6.2.
- Two files arrive with a signature that does not match their bytes (`gawk-5.4.1`, `zsh-5.9.2`, hard-link twins of relocated files). The pour re-signs them.

## What the system presents
| Fact | Value | Consequence |
|---|---|---|
| `SystemVersion.plist` / `sw_vers` | ProductName `PureDarwin`, ProductVersion `26.5`, build `25F74`; kernel Darwin 25.5.0 | brew selects `arm64_tahoe`. Keep 26.x: it matches the SDK version libSystem is built for (26.5) and is the only tag most formulae still have besides the next release. Claiming 14 or 15 would make brew build from source for most formulae. The cost of 26: bottles assume the macOS 26 symbol surface, which is what the survey measures |
| dyld | Apple's dyld of the macOS 11 generation (dyld2 + dyld3 sources) | loads chained fixups and LC_BUILD_VERSION 26 binaries; does not check a library's compatibility version (CoreFoundation, libc++, libobjc here say 0.0.0 and load); does not refuse a minimum OS above 26.5; prints "dyld cache load error" on every start (no shared cache; harmless) |
| Code signing | `amfi.kext` is a permissive stand-in | **Executed:** a copy of `tree` with one wrong page hash runs normally (run `r5`). Nothing is enforced; re-signing is done anyway so the files are valid on a system that does |
| libSystem | one `libSystem.B.dylib` (+ `libdyld`), built `CMAKE_BUILD_TYPE=Debug` | memcpy/memmove/memset are unoptimised C loops: correct, slow for everything |
| `/usr/lib` in the minimal image | libSystem, libc++, libobjc, CoreFoundation, Foundation, IOKit, libiconv, ICU | no libz, ncurses, libxml2, libcurl, libbz2, libedit, libffi, libexpat, libsqlite3, libutil |
| Files | no `/bin/bash`, no `/usr/bin/sw_vers` (it is `/bin/sw_vers`), no `/dev/fd`, no `/usr/share/locale`, no `su` | each one stops something (below) |

## Static survey
`hb_survey.py` walks each program's real library graph as dyld does and checks every non-weak import against the export tries of a system root. Reports: `runs/homebrew/survey/p0.md` (today's QEMU image), `p4.md` (with the overlay).

What stopped main programs on today's image, by formulae:
| Missing | Main programs stopped |
|---|---|
| `/usr/lib/libz.1.dylib` | 17 |
| CoreServices.framework | 13 (mostly through gettext's libintl) |
| libSystem `frexpl` | 9 |
| libSystem `getcontext` / `makecontext` / `setcontext` | 8 (OpenSSL's libcrypto imports them) |
| Foundation.framework at `Versions/C` | 5 |
| Security.framework, libncurses.5.4, libbz2.1.0, libedit.3 | 4 each |
| libSystem `preadv` / `pwritev` | 4 |
| libxml2.2, AppKit, libSystem `forkpty` | 3 each |
| about 110 further libSystem symbols | mostly llvm, gcc, rust, ruby, python |

After the overlay, 7 of 48 main programs are still stopped (static):
| Formula | Stopped by | Class |
|---|---|---|
| curl | Kerberos.framework, LDAP.framework | no source in the tree. The tree's own curl is in the overlay as `/usr/bin/curl` |
| imagemagick | `/usr/lib/libhvf.dylib` (through the freetype bottle) | Apple-proprietary font library, no open implementation: the freetype bottle cannot load. Needs a freetype built without it |
| llvm (`clang`) | `/usr/lib/libxcselect.dylib` | Xcode's developer-directory library; small API, could be written, pointless without an SDK (see source builds) |
| neovim | CoreServices `LocaleRefGetPartString` | tree's CoreServices lacks it (legacy Carbon locale call) |
| vim | AppKit at `Versions/C`, six CoreServices text-encoding calls, the system's `libperl` | frameworks: see next table; system Perl 5.34 is not something to ship, the bottle would need a rebuild |
| starship | AppKit | see next table |
| ffmpeg | AVFoundation, AudioToolbox, CoreMedia, CoreVideo, VideoToolbox, CoreImage, Metal, AppKit … | see next table |

### Frameworks, against the tree's own implementations
The tree has open reimplementations under `src/Frameworks/`. Framework-linked bottles are therefore not "impossible"; they depend on those frameworks being built for arm64 and exporting what the bottle imports. `hb_frameworks.py` (`runs/homebrew/survey/p4-frameworks.md`). "Named in source" is a lexical count of identifiers in the framework's source directory: **not** a build, an export list, or proof of function. No framework below was built or loaded by this stream.
| Framework | Tree source | Flake package | Symbols wanted | Named in source | Needed by (main programs) |
|---|---|---|---|---|---|
| AppKit | yes | `appkit-msm-arm64` | 68 | 67 | ffmpeg, starship, vim |
| CoreGraphics | yes | `coregraphics-arm64` | 45 | 45 | ffmpeg |
| AudioToolbox | yes | `audiotoolbox` (x86_64 only) | 28 | 28 | ffmpeg |
| CoreAudio | yes | none | 4 | 4 | ffmpeg |
| CoreMedia | yes | `app-frameworks` (x86_64 only) | 27 | 24 | ffmpeg |
| AVFoundation | yes | `app-frameworks` (x86_64 only) | 17 | 13 | ffmpeg |
| CoreVideo | yes | `corevideo` (x86_64 only) | 79 | 55 | ffmpeg |
| VideoToolbox | yes | `app-frameworks` (x86_64 only) | 59 | 18 | ffmpeg |
| OpenGL | yes | `openglFramework-arm64` | 22 | 7 | ffmpeg |
| Cocoa, QuartzCore, Carbon, ForceFeedback, GameController, ApplicationServices | yes | arm64 for Cocoa and QuartzCore, x86_64 only for the rest | few | most | ffmpeg and modules |
| CoreServices (present) | yes | `coreServices-arm64` | 12 missing | 0 | neovim, vim, python modules |
| CoreImage, Metal, Kerberos, LDAP, GLUT, OpenAL | no | none | 14, 10, 14, 38, 14, 16 | – | ffmpeg; curl, gnupg's dirmngr |
Building `appkit-msm-arm64` and the `app-frameworks` set for arm64 pulls in Mesa, glib, cairo and the window server (about 40 derivations not built on this workstation); that is the GUI stream's work and was not started here.

## What this ticket adds: the overlay
`integrations/puredarwin/homebrew/build-overlay.nix` builds one tree rooted at `/`; `hb_qemu.py --overlay` writes it onto a private copy of the qemu-dev system image. Contents and origin:
| Path | Origin |
|---|---|
| `usr/lib/libSystem.B.dylib` | `../libsystem-default/build-libsystem.nix` (the proposed default; `build-libsystem.nix` here imports it) |
| `usr/lib/libz.1`, `libcurl.4` | the tree's own dylib packages, unchanged |
| `usr/lib/libncurses.5.4`, `libpanel.5.4`, `libxml2.2`, `libexpat.1`, `libffi`, `libsqlite3` | `build-syslibs.nix`: the tree's arm64 static builds linked into dylibs under macOS's install names |
| `usr/lib/libbz2.1.0`, `libedit.3` | upstream bzip2 1.0.8 and libedit, cross-built (`build-syslibs.nix`, `build-libedit.nix`) |
| `usr/lib/libutil.dylib` | exports nothing (see stubs) |
| `usr/lib/libIOKitCF.dylib` | the tree's IOKit relinked with `kIOMainPortDefault` (`build-iokit.nix`) |
| `System/Library/Frameworks/{CoreServices,SystemConfiguration}.framework` | the tree's arm64 builds, binaries only |
| `System/Library/Frameworks/Security.framework` | the tree's + `patches/security-copy-certificate-chain.patch` |
| `Foundation.framework/Versions/C/Foundation` | link to `/usr/lib/libFoundation.dylib` |
| `usr/share/locale` | the tree's `locale-data.nix` (C.UTF-8, en_US.UTF-8) |
| `usr/bin/curl` | the tree's curl |
| `usr/lib/libhbcrash.dylib`, `usr/bin/hb-runas` | diagnostics of this stream, not for the default image |

### `../libsystem-default/patches/macos-abi.patch`
| Change | Detail |
|---|---|
| Stack probe | above |
| fenv for arm64 with the macOS ABI | `fenv_t` is 16 bytes (FPSR, FPCR); before, `fegetenv`/`fesetenv` moved 4 bytes of FPSR and the rounding mode was neither saved nor restored. Adds `feclearexcept`, `fegetexceptflag`, `feraiseexcept`, `fesetexceptflag`, `fetestexcept`, `fesetround`, `feholdexcept`, `feupdateenv`, `_FE_DFL_ENV` |
| long double entry points | 76 functions (`frexpl`, `modfl`, `csqrtl`, `nexttoward` …). On arm64 long double is double, so each calls its double counterpart |
| ucontext | Apple's arm64 `getcontext`/`setcontext`/`makecontext`/`swapcontext` were in the tree and not built; now built and exported |
| Exports | about 90 names that had implementations in the linked archives but were not exported, or not pulled in: `preadv`, `pwritev`, `forkpty`, `openpty`, `ctermid_r`, `getitimer`, `ether_*`, `getfsfile`, `funopen`, `regcomp_l`, `vfprintf_l`, `fgetattrlist`, `freadlink`, `proc_regionfilename`, `vm_region_recurse_64`, `task_swap_exception_ports`, `os_lock_*`, most `OSAtomic*`, `CC_MD5/SHA1/SHA384/SHA512_*`, `res_9_b64_*`, `bootstrap_look_up_per_user` … The link fails on a name without an implementation |
| libc sources added to the build | `tempnam`, `strtonum`, `ftime`, `getttyent` (`getttynam`), `sigcompat` (`sigblock`, `sigsetmask`) |
libSystem exports go from 2,367 to 2,549. Not exercised individually: each new export was linked, not tested; the smoke tests cover only what the tested programs call.

Left out, honestly missing: `gets` (its source needs libc-internal headers), `OSAtomicFifoEnqueue`/`Dequeue` (need the commpage preemption-free zone). Only llvm's and gcc's runtime libraries import them.

### Stubs (complete list)
| Symbol | Behaviour | Why acceptable |
|---|---|---|
| `_os_signpost_emit_with_name_impl` (libSystem) | does nothing | upstream's `os_signpost_enabled()` returns false; a signpost emitted when nothing collects is dropped by definition. node imports both |
| `libutil.dylib` | no exports | the surveyed bottles link it and import nothing; a program that imports from it fails at launch with dyld's message |
Everything else added is a real implementation. The tree's own libSystem contains further placeholders (for example the rest of `libsystem_trace/signpost.c`, parts of `stub/pd_libSystem_compat.c`); they were not audited.

## Executed results
Method: `hb_pour.py` pours on the host (the guest's ext4 driver writes about 0.2 MB/s under TCG) into a GPT + ext4 image; the guest mounts it read-write at `/opt/homebrew` (the storage stream's finding: ext4 is the writable filesystem); `hb_qemu.py` runs each test of `hb_tests.py` with a time limit and reruns a crashed test with `libhbcrash.dylib` inserted for a backtrace. Real-use tests check output (jq on JSON, `git init`/commit/log, `sqlite3` query, python importing ssl/sqlite3/ctypes/zlib, gpg symmetric round trip, tmux detached session, wget over HTTPS …); "version" tests only prove the program starts.

Before, on today's image (run `r2`, first 28 tests): 8 pass (tree, findutils, xz, less, fzf, ripgrep, zoxide, fish). With the libSystem patch and the first libraries (run `r3`, same 28): 22 pass.

Final state, overlay as described, base image with the qemu-dev stream's network bring-up. Runs: `r8` (the 45 tests of the main prefix in one boot), `r7`/`r9`/`r10` (compiler prefix). Per test, the last run that ran it:

| Test | Formula | Kind | Result | Run | Failure, or output tail |
|---|---|---|---|---|---|
| xz | xz | real | PASS | r8 | `hello-xz` |
| zstd | zstd | real | PASS | r8 | `hello-zstd` |
| sqlite | sqlite | real | PASS | r8 | `42\|3.54.1` |
| rust-version | rust | version | FAIL | r9 | `dyld: Symbol not found: _SecKeychainCopyDefault (Referenced from: /opt/homebrew/bin/cargo)` |
| rust-rlib | rust | real | PASS | r10 | `pdpowerd: no org.puredarwin.acpi events, nothing to watch libl.rlib` |
| gcc-version | gcc | version | PASS | r9 | `gcc-16 (Homebrew GCC 16.2.0) 16.2.0` |
| gcc-asm | gcc | real | PASS | r10 | `gcc rc=0 1` |
| llvm-version | llvm | version | FAIL | r7 | `dyld: Library not loaded: /usr/lib/libxcselect.dylib (Referenced from: /opt/homebrew/Cellar/llvm/23.1.5_1/lib/libclang-cpp.23.1.dylib)` |
| llvm-ir | llvm | real | FAIL | r7 | `dyld: Library not loaded: /usr/lib/libxcselect.dylib (Referenced from: /opt/homebrew/Cellar/llvm/23.1.5_1/lib/libclang-cpp.23.1.dylib)` |
| jq | jq | real | PASS | r8 | `"x" 3` |
| tree | tree | real | PASS | r8 | `t '-- a '-- b '-- f 3 directories, 1 file` |
| gnu-sed | gnu-sed | real | PASS | r8 | `he[ll]o` |
| grep | grep | real | PASS | r8 | `b1` |
| findutils | findutils | real | PASS | r8 | `x.txt y.txt` |
| make | make | real | PASS | r8 | `made-3` |
| less | less | real | PASS | r8 | `x y less 710 (PCRE2 regular expressions)` |
| fzf | fzf | real | PASS | r8 | `banana` |
| ripgrep | ripgrep | real | PASS | r8 | `t/d/f.txt:1:a needle here` |
| fd | fd | real | PASS | r8 | `t/d/f.txt` |
| zoxide | zoxide | real | PASS | r8 | `/tmp/hbzox` |
| eza | eza | real | PASS | r8 | `.rw-r--r-- 3 file` |
| bat | bat | real | PASS | r8 | `129 {"k": 1}` |
| lua | lua | real | PASS | r8 | `10 1024.0 003.1` |
| perl | perl | real | PASS | r8 | `55 3 5.44.0` |
| gawk | gawk | real | PASS | r8 | `b 3 1970` |
| coreutils | coreutils | real | PASS | r8 | `ba7816bf 1970-01-02 1+2+3+4 1001: 7 11 13 2` |
| bash | bash | real | PASS | r8 | `3 1024 v 5` |
| zsh | zsh | real | PASS | r8 | `ABC 42 5.9.2` |
| fish | fish | real | PASS | r8 | `pdpowerd: no org.puredarwin.acpi events, nothing to watch 42 ABC 4.9.3` |
| ncurses | ncurses | real | PASS | r8 | `urses/6.6/share/terminfo/./78/xterm-256color xterm-256color\|xterm with 256 colors, am, 256` |
| openssl@3 | openssl@3 | real | PASS | r8 | `in)= ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad ED25519 Private-Key:` |
| htop | htop | version | PASS | r8 | `htop 3.5.3-3.5.3` |
| nano | nano | version | PASS | r8 | `GNU nano, version 9.3` |
| tmux | tmux | real | PASS | r8 | `tmux 3.8 hb: 1 windows (created Thu Nov 12 06:15:43 2026)` |
| vim | vim | real | FAIL | r8 | `dyld: Library not loaded: /System/Library/Frameworks/AppKit.framework/Versions/C/AppKit (Referenced from: /opt/homebrew/bin/vim)` |
| neovim | neovim | real | FAIL | r8 | `dyld: Symbol not found: _LocaleRefGetPartString (Referenced from: /opt/homebrew/bin/nvim (which was built for Mac OS X 26.6))` |
| git | git | real | PASS | r8 | `2 1 file changed, 1 insertion(+)` |
| curl-https | curl | real | FAIL | r8 | `dyld: Library not loaded: /System/Library/Frameworks/LDAP.framework/Versions/A/LDAP (Referenced from: /opt/homebrew/opt/curl/bin/curl)` |
| wget-https | wget | real | PASS | r8 | `1` |
| gnupg | gnupg | real | PASS | r8 | `gpg: keybox '/tmp/hbt/gh/pubring.kbx' created secret-text` |
| nmap | nmap | real | PASS | r8 | `STATE SERVICE 80/tcp open http Nmap done: 1 IP address (1 host up) scanned in 3.08 seconds` |
| cmake | cmake | real | PASS | r8 | `x=42 h=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad` |
| gh | gh | version | PASS | r8 | `gh version 2.103.3 (2026-10-26) # bash completion V2 for gh -*- shell-script -*-` |
| starship | starship | real | FAIL | r8 | `dyld: Library not loaded: /System/Library/Frameworks/AppKit.framework/Versions/C/AppKit (Referenced from: /opt/homebrew/bin/starship)` |
| python | python@3.14 | real | PASS | r8 | `(3, 14) ba7816bf 42 3904355907 8 OpenSSL {"k": [1]}` |
| ruby | ruby | real | PASS | r8 | `6 {"a":1} ba7816bf 4.0.9` |
| node | node | real | PASS | r8 | `2,4,6 ba7816bf arm64 true` |
| imagemagick | imagemagick | real | FAIL | r8 | `dyld: Library not loaded: /usr/lib/libhvf.dylib (Referenced from: /opt/homebrew/opt/freetype/lib/libfreetype.6.dylib)` |
| ffmpeg | ffmpeg | real | FAIL | r8 | `dyld: Library not loaded: /System/Library/Frameworks/AVFoundation.framework/Versions/A/AVFoundation (Referenced from: /opt/homebrew/bin/ffmpeg)` |
| go-version | go | version | PASS | r8 | `go version go1.27.4 darwin/arm64` |
| go-run | go | real | PASS | r8 | `ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad` |

42 of 51 tests pass (37 real-use, 5 version-only among the passes).

Notes on these results:
- The six failures in `r8` and the two llvm ones are exactly the cases the static survey predicts (same dyld message). `rust-version` fails on `cargo` (`SecKeychainCopyDefault`, a keychain call the tree's Security lacks); `rustc` itself runs and compiled a library crate (`rust-rlib`).
- `go-run` compiles and runs a Go program in the guest; `gcc-asm` compiles C to arm64 assembly (no assembler or linker here, so no executable); `git` commits locally and `git ls-remote https://github.com/Homebrew/brew` answered (run `r6`); `wget` and the system `curl` fetch over HTTPS with certificate verification.
- Time limits: the workstation ran at load 40–60 during these runs. Two timeouts in earlier runs (`zstd` at 60 s, `node` at 300 s) passed when repeated with longer limits and nothing else changed.
- Go's TLS verification goes through `SecTrustEvaluateWithError`; whether the tree's Security can validate a real chain was not tested (`gh` was only started).


Known not covered: interactive use on a terminal (htop, nano, vim, less only started or driven non-interactively), anything long-running, threads under load, sockets beyond one HTTPS fetch.

## `brew` itself
**Executed (run `b2`, guest script `guest/brew-session.sh`):** the unmodified Homebrew client (commit `8679d30f`), staged on the prefix disk by `hb_pour.py --brew` with the portable-ruby 4.0.9 bottle unpacked where `brew vendor-install ruby` puts it, run as uid 1000:
| Step | Result |
|---|---|
| portable Ruby: `ruby -e`, then `-rjson -rdigest -ropenssl -retc` | runs; OpenSSL 4.0.3, `Etc.getpwuid` |
| `brew --version` | `Homebrew >=4.3.0 (shallow or no git repository)` |
| `brew config` | downloads the API data over HTTPS; reports `macOS: 26.5-arm64`, `Curl: 8.21.0 => /usr/bin/curl`, `Git: 2.56.1`, `CLT: N/A`, `Xcode: N/A`, `CPU: single-core 64-bit dunno` |
| `brew install tree` | manifest and bottle fetched from ghcr.io, poured, `tree --version` runs |
| `brew install jq` | resolves and pours the dependency `oniguruma`, relocates the install names itself, `jq -c .a` prints `[1,2]`. Read back from the disk image on the host: the install names are rewritten and the signature matches the bytes |
| `brew list --versions` | lists the bottles poured on the host and the ones it installed |

What had to be supplied, i.e. what SiliconForest lacks for brew:
| Need | Today | Done in the experiment | Proper fix |
|---|---|---|---|
| `/bin/bash` (`bin/brew` is `#!/bin/bash`) | absent | link to the bash bottle | ship bash in the base system (the tree has a recipe) |
| not root | the only user is root; no `su`, `sudo`, `login` | `hb-runas` + an `/etc/passwd` entry | a real user account and a way to switch |
| `/usr/bin/curl` with TLS and CA certificates | absent in the minimal image (`/etc/ssl/cert.pem` is there) | the tree's curl in the overlay | ship it |
| git | absent | the git bottle via `HOMEBREW_GIT_PATH` | ship `/usr/bin/git`, or let brew install it |
| `/usr/bin/sw_vers` | at `/bin/sw_vers` | link | install at the macOS path |
| `/dev/fd` | absent; `mount -t fdesc` is not supported by the guest's `mount` | **not fixed**: brew prints `/dev/fd/63: No such file or directory` twice per command (bash `<(…)` in `utils/os.sh`) and continues, because it reads the version from `SystemVersion.plist` first | mount fdesc at boot |
| Ruby | brew's own portable Ruby bottle (built for macOS 11) runs | staged on the host; `brew vendor-install ruby` itself was not run | nothing |
| sandbox | `Warning: Sandbox unavailable` | accepted | `sandbox-exec` is only used for builds and post-install |
| `codesign` | absent | nothing; jq's signature was valid after brew's own relocation (how brew re-signed without `/usr/bin/codesign` was not traced) | a re-signing tool at `/usr/bin/codesign` for the cases where brew does call it |
| Xcode Command Line Tools | `CLT: N/A` | not needed to pour bottles | only for source builds |

Not tried: `brew update` (needs the git checkout to be a full clone), `brew upgrade`, `brew doctor`, casks, services, formulae with `post_install`, anything that builds. A first attempt (run `b1`) stopped in `brew config` and was ended by hand after about 20 minutes without output; the same command completed in `b2`, where each step had a time limit. The cause of the first stall is not known. brew took minutes per command (Ruby start-up under emulation at host load 50); no timing is claimed.

Version to claim: keep `ProductVersion 26.x`. brew then reports `macOS: 26.5-arm64` and takes `arm64_tahoe` bottles, which is what was tested. ProductName `PureDarwin` did not matter to brew.


## Building formulae from source (assessment, nothing built)
- The llvm bottle's `clang` does not start: it links `/usr/lib/libxcselect.dylib`. The gcc bottle compiles C to assembly (see results) but uses the system assembler and linker of the Command Line Tools, which do not exist here.
- An SDK is needed (`MacOSX.sdk` headers and `.tbd` stubs). Apple's cannot be shipped; the tree has an open SDK under `nix/pkgs/toolchain/oss-sdk` that its own cross builds use.
- A linker: Homebrew has `lld` as a formula; the tree builds its own `ld` for the host only.
- brew's source builds also assume `/usr/bin/{make,patch,install_name_tool,codesign,xcrun}` and sandbox-exec.
- Realistic order: bottles first (this ticket), then a "Command Line Tools" equivalent as its own project.

## Default libSystem (issue 46 follow-up, 2026-11-11)
Recipe: `integrations/puredarwin/libsystem-default/build-libsystem.nix` = baseline `e7aa00a6` + the two patches of the previous default (`vm-page-size-symbol`, `posix-spawnattr-init-zero`) + `patches/macos-abi.patch` (the whole patch described above). Output: `/nix/store/wk4zidyvpi1xadhq6jabxxnidbp5a5fm-puredarwin-libsystem-default-0.1`. The Homebrew recipes import it. **No default was changed by this stream.**

The default to change is one line: in `artifacts/m1-desktop-010/apcie-port-candidate-003/mapping.json`, the entry with destination `/usr/lib/libSystem.B.dylib`, set `source` to `<that store path>/usr/lib/libSystem.B.dylib` and keep its `replaces_sha256`. `tools/pcie_assemble.py` (M1 RAM root) and `qemu-dev/qd_image.py` (QEMU images) both read that file. The many `build-*.nix` that link against `m1-userspace-diagnostics/build-libsystem-spawnattr.nix` can stay: the new library exports a superset.

Why the whole patch and not only the stack probe: no binary of the lab or of the base image imports any `fe*` function (checked with `llvm-nm -u` over the compositor, terminal, sysaudio, pd-netd tools, Mesa, every file of `/bin`, `/sbin`, `/usr/bin`, `/usr/sbin`, `/usr/libexec` and `/usr/lib` of the base image), so the fenv layout change cannot reach existing code; everything else is additional exports.

### Why the linker dropped the stub's `ret`
Reproduced outside the build with the tree's linker (Apple ld64, `puredarwin-native-ld`) and three small objects. `pd_chkstk_darwin.s` has two global labels on one instruction and no `.subsections_via_symbols`: ld64 makes `___chkstk_darwin` a zero-size atom and gives the `ret` to `____chkstk_darwin`. libpthread's archive member also defines `____chkstk_darwin`; ld64 only warns for an object-versus-archive duplicate (`ld: warning: duplicate symbol '____chkstk_darwin'`, present in the build log), keeps the archive's definition and discards the stub's atom with the `ret`. The zero-size `___chkstk_darwin` atom remains and is laid out at whatever comes next. In the reproduction the dangling label moves with the link order, and with the order reversed it is libpthread's own `_thread_chkstk_darwin` that dangles.

So the class is: a label that shares its address with a symbol that loses a duplicate-definition contest. Audit of the built library (`llvm-nm -n`):
- Exported text symbols sharing an address: old default 3 (`___chkstk_darwin`/`_sandbox_check`: the defect; `_madvise`/`_posix_madvise` and `_statfs`/`_statfs64`: intended aliases). New default: the two intended pairs only.
- Exported symbols sharing an address with a local symbol: 64, identical in both builds: the libunwind `unw_*`/`__unw_*` pairs, system-call wrappers and their `___name` stubs (`_accept`/`___accept`, `_close$NOCANCEL`/`___close_nocancel`, …), the `stat`/`stat64` family, `____chkstk_darwin`/`_thread_chkstk_darwin`. All are the same routine under two names.
- Not audited: local (non-exported) labels, data symbols.

The link still prints 74 `duplicate symbol` warnings (71 before the patch; the patch removes `____chkstk_darwin` and adds four `CC_MD5*`, defined in both `pd_cc_digest_bridge.c` and `CommonDigest.c`). They are C functions defined twice, mostly a hand-written copy in `stub/pd_libSystem_compat.c`, `dyld/pd_stubs.c` or `libsystem_kernel/_libc_funcptr.c` next to the real one in libc, libplatform, libpthread or libsystem_kernel: `memcpy`/`memmove`/`memset`/`bzero`/`strlen`/`strcmp`/`strchr`/`strstr`/`memchr`/`memcmp`, `OSAtomic*` (8), `OSSpinLock*`, `os_unfair_lock_lock/unlock`, `mk_timer_*`, `arc4random*`, `sigaddset`/`sigemptyset`/`sigfillset`, `sigwait`, `pthread_atfork`, `qos_class_self`, `strtol_l` family, `strtod_l`, `fprintf_l`, `snprintf_l`, `strxfrm`, `statvfs`, `backtrace_symbols*`, `mach_vm_region`, `task_threads`, `thread_suspend/resume`, `mach_task_self`, `environ`, `__cxa_atexit`, `__cleanup`, `_NSGetMachExecuteHeader`, `__stdoutp`/`__stderrp` (dyld's `glue.c` and libc's `findfp.c`). None dangles (C objects have no zero-size labels), but for each the linker chooses which body the system runs, silently. Which one wins was not determined per symbol. This deserves its own cleanup: make the duplicates errors.

### Exposure of the lab's own binaries
`libsystem-default/chkstk_audit.py` lists the call sites of `___chkstk_darwin` in a Mach-O and what each function does with x0 afterwards (validated on the bottles: libjq 4 of 4 exposed, gsed 3 of 3). Result for the lab: **no call sites at all.** None of these imports `___chkstk_darwin`: the compositor (build-009), the terminal (build-008), coreaudiod, pdtone, pdvolume, pcmplay and the three audio frameworks, pd-dhcp, pd-net-up, pd-netd, the four Mesa libraries, and all 147 Mach-O files examined including the base image's `/bin`, `/sbin`, `/usr/bin`, `/usr/sbin`, `/usr/libexec` (wifiutil, mdnsd, zsh, launchd …) and `/usr/lib`. The tree's cross toolchain does not emit stack probes and Rust uses its own. The lab's binaries were therefore never affected; the defect only hits code built by Apple's toolchain defaults (bottles, any third-party macOS binary).

The tree's own Python 3.13 also has no call to the probe, so its lost `print` output (QEMU stream) is **not** this defect. Not investigated further; the duplicate `__stdoutp` above is one thing to rule out.

### Proof
| Where | What | Result |
|---|---|---|
| QEMU | Homebrew smoke suite with the overlay rebuilt on the new default (`r11`, `r12`) | 39 of 45 and 3 of 6: the same tests pass and fail as before (`r8`, `r10`) |
| QEMU | throw-away desktop, full autopilot, new libSystem (`runs/qemu-dev/libsystem-default`, `libsystem-default2`) | **not a PASS**: `clipboard`, `scale`, `menuterm`, `limit` fail |
| QEMU | the same with the old default, run side by side (`libsystem-control`) | the same four phases fail. The failures are the QEMU loop's own at this host load, not the library's; a PASS of the full autopilot in QEMU was not obtained with either library |
| M1 | boot `ls02` (`libsystem-default/tools/ls_desktop_boot.sh`): default fast-boot collection `z66w83kk…`, `pd_quiet_wait=0 pd_rtc_wait=0`, `launcher-fast-quiet.patch`, slim stager, compositor build-009, terminal build-008, new libSystem staged for this boot only | `DARWIN_COMMAND_LINE_OBSERVED_LINUX_VERIFIED`, `linux_verified: True`. Every autopilot phase line has the verdict it has in `ds09` (both sessions `done result=ok`, no `FAILED`), `DSDESK_WIFI_JOIN_RC=0`, `DSDESK_NET_UP_RC=0`, `DSDESK_PING_RC=0 … joined=1`, the `PDSpeakers` and `PDTouchBar` lines as before. The RAM root image of the packet contains the new library byte for byte |
`ls01` never reached the bench: staging refused to overwrite the base RAM root's libSystem because `PCIE_EXTRA_FILES` drops the `replaces_sha256` field. `tools/ls_stager.py` (a `PCIE_STAGER` wrapper) restores it in the boot's own mapping. With the default changed in `mapping.json` this wrapper is not needed.

## Claude Code and Codex CLI (2026-11-12)
No sign-in was done and no credential of any kind is in the guest: the harness types scripts over the serial console, QEMU was started with an emptied host environment, and the guest reported 0 environment lines naming a key or token. Everything stops at the first screen.

### How they are distributed for macOS arm64 (from the vendors' pages and registries, fetched 2026-11-12)
| | Claude Code (Anthropic) | Codex CLI (OpenAI) |
|---|---|---|
| Documented installs | `curl -fsSL https://claude.ai/install.sh \| bash` (recommended), `brew install --cask claude-code`, npm `@anthropic-ai/claude-code` | `curl -fsSL https://chatgpt.com/codex/install.sh \| sh`, `npm install -g @openai/codex`, `brew install --cask codex`, GitHub release archives |
| Version examined | 2.1.322 (`downloads.claude.ai/claude-code-releases/2.1.322/darwin-arm64/claude`, checksum as in its manifest) | 0.167.2 (`codex-package-aarch64-apple-darwin.tar.gz`, checksum as in the Homebrew cask) |
| What it is | one 324 MB executable, a Bun-compiled JavaScript program; the npm package only selects this binary per platform (`engines: node >= 22` for the installer stub) | a Rust executable (271 MB) plus `codex-exec-server`, `codex-code-mode-host`, its own `rg`, and a voice helper with GStreamer libraries |
| Requires (vendor) | macOS 13.0+ | binary says minimum macOS 11.0 |
| Signature | Developer ID (`com.anthropic.claude-code`): not ad-hoc, so never rewritten by our tools; this kernel does not check it | Developer ID |
Anthropic's installer script was read before use: it downloads `latest`, the manifest and the binary from `downloads.claude.ai`, checks the SHA-256, and runs `claude install`; it needs bash and curl (or wget).

### Static survey (`hb_check.py`, against the default userland and against it plus the overlay)
- **Claude Code** links only `libSystem`, `libc++`, `libicucore`, `libresolv`. All 838 imports are exported by the stock image (old and new default libSystem alike); one weak import is absent (`posix_spawn_file_actions_addfchdir`). It does not call the stack probe. Nothing proprietary is linked.
- **Codex** (`codex`, `codex-exec-server`) links frameworks the image does not have, so dyld refuses it at launch:
| Needed | State here | Kind of work |
|---|---|---|
| `LocalAuthentication.framework` (linked, nothing imported) | absent; tree source `src/Frameworks/LocalAuthentication`, built for x86_64 only (`app-frameworks`) | build for arm64 |
| `CFNetwork.framework`: `CFNetworkCopyProxiesForURL`, PAC execution, 11 proxy constants | absent; tree source `src/Frameworks/CFNetwork/CFProxySupport.c`, x86_64 only | build for arm64 |
| `AppKit.framework/Versions/C` (4 pasteboard constants), `CoreGraphics.framework` (linked only) | absent from the default image; present in the Cocoa stream's arm64 root, where 1 constant is missing: `NSPasteboardURLReadingFileURLsOnlyKey` | Cocoa stream's root + one constant |
| Security: `kSecAttrKeyTypeAES`, `kSecMatchSubjectWholeString`, `kSecUseKeychain` (data, bound at launch) | declared in the tree's headers, not exported | small |
| Security: 19 SecureTransport calls (`SSLCreateContext`, `SSLHandshake`, …), `SecItemImport`, 3 keychain calls | header only in the tree | large; bound lazily, so only fatal if Codex takes that path (it carries rustls as well; not determined which it uses) |
| CoreServices: `log2`, `exp2` recorded against CoreServices; FSEvents (present) | the two math names are not re-exported by our CoreServices | small (re-export libSystem's), lazy |
`codex-code-mode-host` needs CFNetwork and the Security items only. Codex's bundled `rg` runs (executed).

### Executed (QEMU, `guest/cli-session.sh`, runs `c2`, `c3`; binaries placed on the prefix disk byte for byte as downloaded, run as uid 1000)
| | Claude Code | Codex |
|---|---|---|
| `--version` | `2.1.322 (Claude Code)` | dyld: `Library not loaded: …/LocalAuthentication.framework/Versions/A/LocalAuthentication` |
| `--help` | prints | – |
| First screen on a PTY (tmux 100x30, `TERM` set by tmux) | "Welcome to Claude Code v2.1.322 … Choose the text style", then after one Enter "Select login method: 1. Claude account with subscription / 2. Anthropic Console account / 3. 3rd-party platform". Stopped there | the dyld message |
| Rendering (screen read back from the disk image, since the serial console is 7-bit) | UTF-8 block and shade glyphs (U+2588, U+2584, U+2591, U+2592), `❯`, `·`; 256-colour SGR (`38;5;n`, `48;5;n`), bold | – |
| Input | Down moved the selection and the preview changed theme; Enter advanced | – |
| Resize 100x30 → 70x24 | layout redrawn at the new width | – |
| Network over TLS | `~/.claude/cache/changelog.md` (1.19 MB) was fetched; the log says "CA certs: Loaded 118 bundled root certificates"; one request (MCP registry) hit its own 5 s timeout | – |
| State written | `~/.claude.json`, `~/.claude/{sessions,projects,plugins,cache,debug,backups}`, `settings.json`; file watching on `settings.json` started without error | – |
| Non-interactive `claude -p hello` without credentials | **does not return** (ended by the 15-minute limit, twice). Its log stops 50 s in, after "API error (attempt 1/11): Could not resolve authentication method". On macOS this prints a "not logged in" message and exits. Cause not found | – |
| Installed by the vendor's script in the guest | yes, with one tool supplied. First try (`c4`): `shasum: command not found`, "Checksum verification failed" (on Darwin the script calls `shasum -a 256`; the image has only `sha256sum`). With a `/usr/bin/shasum` that runs `sha256sum` for that mode (`c5`): download, checksum, `claude install`: "Claude Code successfully installed! Version: 2.1.322, Location: ~/.local/bin/claude"; `~/.local/bin/claude --version` prints `2.1.322 (Claude Code)`. The whole run took 23 minutes under emulation | not attempted (cannot start) |
Not tested: Claude Code in the lab's own terminal on the desktop (tmux was the terminal here), anything after sign-in (the agent loop, the Bash tool, ripgrep search, sandboxing), `brew install --cask`.

### What they need that the system lacks
Checked in the guest (`command -v`): `security` no, `open` no, `pbcopy`/`pbpaste` no, `osascript` no, `sandbox-exec` no, `pgrep` no, `script` no, `stty` no, `tput` no, `locale` no, `defaults` no; `ps`, `rg` (bottle), `git` (bottle), `bash` (bottle), `zsh`, `sh`, `sw_vers`, `uname` yes. `/tmp` is `/private/tmp`; there is no `/var/folders` and `TMPDIR` is unset (Claude Code used `/tmp` without complaint).
| Gap | Who needs it | Effort |
|---|---|---|
| `/bin/bash`, a user account, `/usr/bin/curl`, UTF-8 locale data | installer script; both tools' shell tool | small: already on the fold-in list |
| `/usr/bin/shasum` | Anthropic's installer script (stops with "Checksum verification failed" without it) | small |
| `git`, `rg` in the default image | both (Claude Code brings its own ripgrep; Codex ships `rg`) | small |
| `/usr/bin/security` and a keychain | Claude Code stores the login in the macOS Keychain through `security`; when that fails it documents a fallback to `~/.claude/.credentials.json` (mode 0600). The log here shows "[keychain] readAsync failed". Codex: `cli_auth_credentials_store = "file"` keeps `~/.codex/auth.json` | none for a first login if the documented file fallback works here (untested: needs a sign-in); a real keychain is large |
| `/usr/bin/open` | browser hand-off at login (both) | small once a browser exists; until then see below |
| `pbcopy` | "press c to copy the login URL" | small (needs a clipboard service) |
| `sandbox-exec` (Seatbelt) | Codex confines commands with it on macOS; Claude Code's optional sandbox uses it | medium: the tree has a `Sandbox.kext` and a `libsandbox` that compiles SBPL profiles (it says some rule classes are recognised but not enforced), but no `sandbox-exec` command; whether the profiles these tools generate are accepted is untested. Until then Codex would have to run with its sandbox disabled |
| `pgrep`, `stty`, `tput`, `script`, `locale`, `defaults` | conveniences either tool may call | small each |
| Codex's frameworks (table above) | Codex cannot start | medium: two tree frameworks for arm64, three Security constants, one AppKit constant, and the Cocoa root in the image |
| `claude -p` not returning | scripts and CI use of Claude Code | unknown: to be debugged |

### Sign-in with no browser on the system (from the vendors' documentation; nothing was tried)
- **Claude Code:** the login screen prints a URL; the documentation says to open it in any browser (another machine works) and paste the code shown there at "Paste code here if prompted". Alternatives: `ANTHROPIC_API_KEY` in the environment, or an `apiKeyHelper` script. So a browser on SiliconForest is not required.
- **Codex:** default is a browser flow that returns to a local port, which needs a browser on the same machine. Documented for headless machines: `codex login --device-auth` (device code, beta, must be enabled in the account's settings), signing in with an API key, or copying `~/.codex/auth.json` from a machine that has a browser.

## What should be folded into the default images
1. The `___chkstk_darwin` hunk (all images, now).
2. The rest of `libsystem-homebrew.patch` (fenv ABI, long double, ucontext, exports).
3. `libz`, `libncurses`, `libxml2`, `libexpat`, `libffi`, `libsqlite3`, `libbz2`, `libedit`, `libcurl` in `/usr/lib`; `CoreServices`, `Security`, `SystemConfiguration` frameworks; `Foundation` `Versions/C`; `/usr/share/locale`; `kIOMainPortDefault`; `SecTrustCopyCertificateChain`.
4. For brew: `/bin/bash`, `/usr/bin/sw_vers`, `/dev/fd`, a non-root user and a way to become it, `/usr/bin/curl`, `/usr/bin/git` (or the bottle), `/usr/bin/codesign` (a re-signer; `hb_macho.py` shows the needed logic is small).
5. Build the userland optimised (libSystem is `-O0`).
6. For Claude Code and Codex: `/usr/bin/shasum`, `git` and `rg`, `pgrep`, `stty`, `tput`, `locale`; `/usr/bin/open` and `pbcopy`/`pbpaste` when there is something for them to talk to; `/usr/bin/security` (at least the generic-password subcommands over a file store) so credential storage does not depend on a fallback; `/dev/fd`; an 8-bit-clean console.
7. For Codex: `LocalAuthentication` and `CFNetwork` built for arm64, the three Security constants, `NSPasteboardURLReadingFileURLsOnlyKey` in AppKit, `log2`/`exp2` reachable through CoreServices.

## What goes onto the future disk root (12 November)
The M1 RAM root is not the place for any of this: it has about 15 MB free. The list is for the
root filesystem on disk (the storage stream's), and the QEMU full install already carries the
part marked "in the QEMU install".

| Item | Why | State |
|---|---|---|
| `/bin/bash` | `bin/brew` is `#!/bin/bash`; install scripts (Anthropic's, most others) | the tree has a recipe (`nix/pkgs/base/bash.nix`, built for x86_64 only: needs the arm64 triple and the lab's libSystem). Not in any image |
| A user account and a way to become it | brew refuses to run as root; CLI tools keep state per user | account `sf` (uid 501, `/Users/sf`) is in the QEMU install; there is no `login`, `su` or `sudo` (the Homebrew runs use this stream's `hb-runas`, a diagnostic) |
| `/usr/bin/curl` with TLS and the CA bundle | brew, installer scripts | upstream's full image has `/bin/curl`; **in the QEMU install** as a link at the macOS path. The minimal image has none (the overlay carries the tree's) |
| `/usr/bin/git` | brew (`HOMEBREW_GIT_PATH` otherwise), Claude Code, Codex | in upstream's full image |
| `/usr/bin/shasum` | Anthropic's installer stops with "Checksum verification failed" without it | **in the QEMU install**: `qemu-dev/guest/shasum`, a shell script over the userland's `sha1sum`/`sha256sum`/`sha512sum` (`-a 1\|256\|512`, `-c`) |
| `/usr/bin/sw_vers` | brew calls it by that path | the image has `/bin/sw_vers`; **in the QEMU install** as a link |
| `/dev/fd` | bash `<(…)`; brew prints an error twice per command without it | not there: the guest's `mount` refuses `-t fdesc`. Kernel/mount work, not a file to copy |
| `/usr/bin/open` | browser hand-off at sign-in | nothing to open a URL with yet; goes in with a browser (the Cocoa stream's Firefox) |
| `pbcopy`, `pbpaste` | "press c to copy the login URL" | need a clipboard service to talk to: the compositor's selection (Wayland `wl-copy`/`wl-paste` semantics behind the two names) |
| `pgrep` | both CLI tools may call it | the tree builds `pkill` for the base userland, not `pgrep`; toybox has an applet (`PGREP`) that upstream's configuration leaves off |
| `stty` | shell tools, installers | toybox has one (`STTY`, in its pending set), off in upstream's configuration |
| `tput` | scripts that colour their output | ncurses program; upstream's ncurses package installs the library and `ncursesw6-config` only |
| `locale` | scripts that test for UTF-8 | no source in the tree; a small program over `setlocale`/`nl_langinfo`. The locale *data* (`/usr/share/locale`) is in the overlay and **in the QEMU install** |
| An 8-bit-clean console | UTF-8 output of CLI tools on the serial console arrives 7-bit (seen in the Claude Code run) | tty/console setting, not a file; the lab's terminal on the desktop is not affected |
| The overlay's libraries and framework additions (`/usr/lib/lib{ncurses.5.4,panel.5.4,expat.1,ffi,sqlite3,xml2.2,bz2.1.0,edit.3,util}.dylib`, `Security` with `SecTrustCopyCertificateChain`, `Foundation` `Versions/C`) | what bottles link instead of bringing | **in the QEMU install** (`qemu_install.sh create --variant wayland`; `QD_EXTRAS=0` leaves them out) |

Two of the "symbol additions" changed with the tip: upstream's IOKit now defines
`kIOMainPortDefault` itself (`build-iokit.nix` is the tree's package at the tip and the relinked
one only with `SF_PD=pin`); `SecTrustCopyCertificateChain` is still this stream's patch.

## Framework gaps: the scanner, vim and neovim on the M1 (13 November, issues 45 and 47)
Everything here is in `integrations/puredarwin/macos-compat/` (recipes, patches, probes, tools) and
`integrations/puredarwin/homebrew/sf_gapscan.py`. "M1" below means the installed system on the
M1's disk, reached over SSH while it ran; nothing was booted for this.

### The scanner
`sf_gapscan.py --root <copy of a root> [--overlay <candidate fix>] --brew --system [--binary <path>] [--kegs <unpacked bottles>]`
reads Mach-O files on the workstation (3 s for the M1's prefix): every load command resolved as
dyld resolves it (absolute path, `@rpath`, `@loader_path`, dyld's fallback directories), every
import checked against the library its two-level ordinal binds it to, re-exports followed. It
reports the gap in the image itself and, per program in a formula's `bin/`, the gaps of its whole
load closure (the program does not start). Also: the interpreters scripts name. Static only.
Summaries (the raw output stays in `runs/sf-gaps/`): `homebrew/gapscan/`.

| Scan | Before | With the fixes below |
|---|---|---|
| The M1's own prefix: 71 formulae, 533 Mach-O files, 202 programs | 2 formulae with a program that does not start (vim, neovim) | 0 |
| The M1 root's own frameworks, libraries and programs | 0 of 128 programs | 0 of 129 (332 images with the new frameworks and Perl's extensions) |
| 138 unpacked bottles of the survey (not installed on the M1; static, not run) | 15 formulae | 10 (below; gone from the list: vim, neovim, starship, pinentry, libfido2) |
| Interpreters scripts name, missing from the root | `/usr/bin/perl` and `env perl` (27 scripts of git, vim, openssl, ncurses, jemalloc), `/usr/bin/python`, `/usr/bin/python3`, `/usr/bin/awk`, `/bin/csh`, `/usr/bin/make` | perl is there; the others are not |

What the first table missed and the programs then showed: a library that is present and exports
nothing counts as a symbol gap only for images that import from it (libexpat, found that way);
behaviour is never seen (strxfrm, below).

### What was wrong and what was done
| Gap | Who | Fix | Real or stub |
|---|---|---|---|
| CoreServices: `LocaleRefGetPartString`, `CreateTextEncoding`, `TECCreateConverter`, `TECConvertText`, `TECFlushText`, `TECDisposeConverter` | nvim (the first), vim (all six, called at every start) | `patches/coreservices-text-encoding-converter.patch` (`build-coreservices.nix`; the Homebrew overlay takes its CoreServices from there). Also `GetTextEncodingBase/Variant/Format`, `TECClearConverterContextInfo` | real: conversion between UTF-8/16/32 and any encoding CFString has, output in the canonical or HFS+ composed or decomposed form the target encoding's variant names (HFS+ leaves U+2000..2FFF, U+F900..FAFF, U+2F800..2FAFF alone), text that does not fit kept for the next call; a locale's language, script and region from its CoreFoundation identifier. Refused cleanly: an encoding CFString does not have (`kTextUnsupportedEncodingErr`), UTF-7, half a character at the end of the input (`kTECPartialCharErr`), a buffer too small for the locale string. 15 checks in `probe/sf-compat-probe.c` |
| `AppKit.framework/Versions/C` absent | vim (`NSPasteboard`, `NSSound`, `NSPasteboardTypeString`) | `build-appkit.nix`: the Cocoa stream's stack (SF-U), not a second AppKit: the part of `cocoa/build-desktop-root.nix` that dyld loads for a program linking AppKit or Cocoa, selected by the scanner's walk (`tools/sf_closure_copy.py`): the bundles AppKit, Cocoa, CoreData, CoreGraphics, CoreText, CoreVideo, IOSurface, Onyx2D, OpenGL, QuartzCore, Carbon and `libWindowServer`, `libGL`, `libGLU`, `libgallium`, `libLLVM`, `libdrm`, `libwayland-client/-cursor`, `libfreetype`, `libfontconfig` (101 MB, all new paths) | real, with one limit: in a program without a window the pasteboard is that process's own (the WindowServer library joins the compositor's selection only once a window made it connect, and Wayland gives the selection to a client with keyboard focus). `"*y` then `"*p` inside one vim works; another program does not see it. `+[NSSound soundNamed:]` of an unknown name is nil |
| `/System/Library/Perl/5.34/darwin-thread-multi-2level/CORE/libperl.dylib` absent (a hard load command of vim; not in the brief) | vim; every `#!/usr/bin/perl` script | `build-perl.nix`: Perl 5.34.1 from CPAN's source, built with perl-cross 1.3.7 in the nix sandbox (nothing is run on a target), threads and multiplicity, shared libperl with the install name macOS programs carry, `/usr/bin/perl`, `/System/Library/Perl/5.34` (1672 files, 51 MB). `perl/hints-darwin` is ours (type sizes asserted by `perl/sizes.c` with the cross compiler) | real. vim's `:perl` runs in it: Perl's own check of the interpreter layout a program was compiled with (`Perl_xs_handshake`) accepts vim. Not built: Apple's additions to its system Perl (its extra CPAN modules, dtrace probes). `%Config` names `cc`; there is no compiler on the system |
| `/usr/lib/libexpat.1.dylib` exported nothing | python `pyexpat` (so `xml.etree`, `plistlib`), `git-http-push` | `homebrew/build-syslibs.nix`: expat hides its API unless told a shared library is made; the tree builds it static only since the tip. Rebuilt visible; the recipe now fails when a library exports nothing | real (upstream expat) |
| `/usr/lib/libncurses.5.4.dylib` without the `name$NCURSES60` symbols | python `_curses`; cmake's `ccmake`, pinentry (static) | the same recipe: every function also under the name macOS's `<curses.h>` gives it (ld's `-alias_list`); the library is ncurses 6 with that ABI | real. Proven with python's curses on a terminal. The root's terminfo database has `xterm-256color` only (not `tmux-256color`, `screen`) |
| libSystem: `strxfrm()`/`wcsxfrm()` in `en_US.UTF-8` end the caller with SIGSEGV (`wcslen(NULL)`: the locale's collation table has no directives) | found by `/usr/bin/perl` (dies at start in a UTF-8 locale), the same in python's `locale.strxfrm` | `libsystem-default/patches/strxfrm-without-directives.patch`: such a locale collates as the C locale does. Also built and exported: `timingsafe_bcmp`, `ualarm` | real fix; collation in UTF-8 locales is by code point, not by language rules (the tree's locale data has no usable collation table) |

Not stubs anywhere above. `dyld: dyld cache load error: shared cache file open() failed` is not
printed by every program: dyld prints it only together with a real load failure (there is no
shared cache). Nothing to silence.

### Proof
`guest/sf-compat-check.sh` runs the programs and prints `COMPAT_VERDICT ok|FAIL`; the same file
is the QEMU guest's and the M1's check.

| Where | What | Result |
|---|---|---|
| QEMU, the M1's root image + `sys` part + the M1's `/opt/homebrew` (run k1) | CoreServices replaced in the running guest by `sf-lib-install.sh` | probe 15 of 15; `nvim --version`, `nvim --headless` writes a file, nvim on a tmux terminal: typed text is in the file |
| QEMU, a fresh boot of that root (k1-2) | nothing installed | the same |
| QEMU (k2) | the new files sent as the M1 gets them (tar stream, unpacked in the guest: 30 minutes there) | 1784 files hashed in the guest, 0 differ |
| QEMU (k3), new files put in by the host; libexpat and libncurses replaced in the guest | vim, python, git | `vim --version`, `vim -es` writes a file, vim on a terminal; `pyexpat`/ElementTree/plistlib; python curses draws at 80x24 with 256 colours; `git-http-push` loads. `/usr/bin/perl` and `:perl` fail in `en_US.UTF-8` (the strxfrm defect; they run under `LC_ALL=C`) |
| M1 | CoreServices replaced (guarded), then the 1784 new files + 12 links (sent in 9 s, every file hashed there before anything loaded it: equal) | probe 15 of 15; AppKit probe (pasteboard set and read back) in 27 ms; **nvim: starts, writes headless, edits on a terminal; vim: `--version` in 88 ms, `-es` writes a file, edits on a terminal, `:perl` prints "perl says 42 version v5.34.1"** |
| M1, the fixed libSystem tried through `DYLD_LIBRARY_PATH` from a scratch directory (not installed by that) | perl and python in `en_US.UTF-8` | `perl v5.34.1 in en_US.UTF-8 strxfrm=5`; python `locale.strxfrm` works (dies with the library in use) |
| QEMU (k3-2) | a fresh boot of k3's root (libexpat, libncurses replaced) | all 18 checks ok |
| QEMU (k7) | CoreServices, libexpat, libncurses and libSystem replaced in the running guest, in that order (the guest starts with the libSystem the M1 had) | each `SFLIB 3 installed`; all 18 checks ok, `QDDESK_PASS`; `e2fsck -fn` of the image afterwards clean |
| QEMU (k7-2) | a fresh boot of the root k7 left, nothing installed or injected | all 18 checks ok, `QDDESK_PASS` |
| M1, after that | libexpat, libncurses, then libSystem replaced (guarded: hash before load, the probe and node, claude, python3, bash, git, vim, nvim with the new file first) | **all 18 checks ok on the M1** (`COMPAT_CHECK failed=0`): the rows above plus `:perl` and `"*` in vim under `en_US.UTF-8` (`perl=1 clipboard=1 … perl says 42 version v5.34.1 star=[to the pasteboard]`), `/usr/bin/perl` with POSIX, List::Util, Digest::SHA, Encode, Socket, Time::HiRes, threads, fork and a pipe, a `#!/usr/bin/perl` script, python's `pyexpat`/ElementTree/plistlib and curses on a terminal, `git-http-push` |

Runs that taught something about the tools, not the system: k4, k5, k6 stopped in my own
`sf-lib-install.sh` (a program list with `sha256sum --version` and `tmux --version`, which never
end with 0; then a comparison that refused a program the new library repairs). Each stopped before
changing anything, as designed. The rule now: with the new library a program ends with 0 or as it
does with the library in use.

### On the M1's disk, and the way back
| Path | State | Back |
|---|---|---|
| `/System/Library/Frameworks/CoreServices.framework/Versions/A/CoreServices` | the new build (sha256 `4bf90de0…`) | `CoreServices.old` beside it is the file of before (hard link): `ln X.old X.back && mv -f X.back X` |
| 1784 files and 12 links under `/System/Library/Frameworks/{AppKit,Cocoa,CoreData,CoreGraphics,CoreText,CoreVideo,IOSurface,Onyx2D,OpenGL,QuartzCore,Carbon}.framework`, `/usr/lib/lib{WindowServer,GL.1,GLU.1,gallium-26.1.6,LLVM}.dylib` and five links, `/lib/lib{drm.2,fontconfig.1,freetype.6.20.6,wayland-client.0,wayland-cursor.0}.dylib`, `/usr/bin/perl` and Perl's tools, `/System/Library/Perl`, `/Library/Perl` | new paths; nothing that existed was touched (`/usr/bin/shasum` stayed the system's) | delete them: the list is `runs/sf-compat/stage/new.sha256` and `new.links` of the push (regenerate with `tools/sf_compat_stage.sh`) |
| `/usr/lib/libexpat.1.dylib` | the new build (`1d521e1a…`) | `libexpat.1.dylib.old` |
| `/usr/lib/libncurses.5.4.dylib` | the new build (`62f5e76b…`) | `libncurses.5.4.dylib.old` |
| `/usr/lib/libSystem.B.dylib` | the new build (`20e7a056…`, store path `mdnirx5x…`) | `libSystem.B.dylib.old2` is the file of before (`bebd667b…`, the xpc fix); `libSystem.B.dylib.old` is still the one before that (`09b83541…`) |
Scratch files left in `/private/tmp` there (`sfcompat/`, `sf-test/`, `sfls/`, `sfpc/`, `libhbcrash.dylib`): gone at the next boot or by hand.

### In the root build
- `homebrew/build-overlay.nix` (QEMU install, Homebrew runs, the disk root's `sys` part): CoreServices from `macos-compat/build-coreservices.nix`; `build-syslibs.nix` with the libexpat and libncurses repairs.
- `m1-storage/tools/sf_userland_push.sh stage`: the `sys` part also carries `macos-compat/build-compat-root.nix` (AppKit/Cocoa + Perl; `SF_COMPAT=0` leaves it out), so a newly installed disk root has them.
- `libsystem-default/build-libsystem.nix`: the strxfrm patch is in the lab's default libSystem recipe, and `artifacts/m1-desktop-010/apcie-port-candidate-003/mapping.json` (not in git) names the new store path since 13 November (copy of before: `mapping.json.before-strxfrm-fix-20261113`; `libsystem-default/DEFAULT.txt`). No RAM-root boot has used that mapping yet.
- For a system that runs: `macos-compat/tools/sf_compat_stage.sh` (builds, compares with a copy of the target's trees, stages only what the target lacks or must have replaced), `sf_compat_push.sh new|replace|check`, `sf_compat_qemu.sh <name> [second]` (the rehearsal; `SF_COMPAT_HOST_NEW=1` puts the new files in by the host, 12 minutes instead of 40).

### Remaining gaps (wide scan, `homebrew/gapscan/wide-after.md`; none of these formulae is installed on the M1)
| Gap | Programs stopped | What it needs | Estimate |
|---|---|---|---|
| Kerberos; for `sshd` also libSystem `login`, `logout`, `logwtmp`, `innetgr`, `libpam.2`, `libsandbox.1` | the openssh bottle's `ssh` and `sshd` (its other six programs resolve now, with `timingsafe_bcmp`). The lab's own OpenSSH is what the system uses | Kerberos (below); utmpx-backed login records (small), an OpenPAM port (medium) | 2 to 3 days without Kerberos |
| `Kerberos.framework` (39 GSS symbols), `LDAP.framework` (38) | curl bottle, gnupg's `dirmngr`, openssh | Heimdal's GSS-API and OpenLDAP's client library built as those frameworks; no source in the tree | a week each; the tree's own curl covers curl |
| Security: `SecKeychainCopyDefault`, `SecKeychainItemFreeContent` | rust's `cargo` | a keychain; a pair that answers "no default keychain" would let cargo start (not done: not run here) | 1 day with a QEMU proof |
| CoreServices: `LocaleStringToLangAndRegionCodes`, the Apple Event calls (`AEBuildAppleEvent`, `AESendMessage` …) | cmake's `cpack`; python's "Python Launcher.app" | the classic language/region code tables (small); Apple Events are a subsystem | 1 day; Apple Events: weeks |
| `libxcselect.dylib` (`xcselect_host_sdk_path`) | llvm's clang (27 programs) | one function; useful only with an SDK and a linker on the system | 1 day for the start; the SDK is the project |
| `libpcap.A.dylib` | `ldns-dpa` | libpcap over the kernel's BPF devices | 2 days |
| `libhvf.dylib` | imagemagick (through the freetype bottle) | Apple-proprietary, no open implementation | not possible; a freetype without it |
| AVFoundation, CoreMedia, VideoToolbox, CoreImage, Metal, ApplicationServices, CoreVideo's Metal calls | ffmpeg (20 programs) | the media frameworks for arm64 (SF-U, m1-avd) | weeks |
| `GLUT.framework`; `OpenGL.framework` by its unversioned path | webp's `vwebp` | freeglut over the WindowServer; a link | 3 days |
| The pasteboard of a windowless program reaching the desktop's clipboard | vim's `"*`, `pbcopy`/`pbpaste` | a data-control protocol in the compositor and in the WindowServer library | 3 to 5 days |
| A full terminfo database in `/usr/share/terminfo` | programs on the system ncurses under `TERM=tmux-256color`, `screen` | ncurses' database compiled for the root | half a day |
Known and not looked at in this job: sftp-server's SIGBUS, `dlopen()` in a child of `fork()`, the other `xpc_api_misuse` sites (SF-X).

## Open gaps, in order of formulae unblocked
| Gap | Unblocks | Kind |
|---|---|---|
| `$NCURSES60`-suffixed entry points in libncurses | python's curses module, ccmake | **done 13 November** (section above) |
| CoreServices: `LocaleRefGetPartString`, text-encoding converter calls | neovim, vim | **done 13 November** |
| AppKit at `Versions/C` | starship, vim | **done 13 November** (the Cocoa stream's stack on the disk; starship not run) |
| The media frameworks for arm64 | ffmpeg | GUI stream |
| Kerberos, LDAP frameworks | curl bottle, gnupg's dirmngr | no source; Heimdal/OpenLDAP shims or keep the tree's curl |
| libhvf | freetype bottle, imagemagick | proprietary; substitute is a freetype without it |
| libxcselect, SDK, linker | llvm's clang, source builds | separate project |

## Files
`integrations/puredarwin/homebrew/`: `hb_fetch.py` (index, closure, download), `hb_macho.py` (Mach-O reading, install-name rewrite, re-sign), `hb_survey.py`, `hb_frameworks.py`, `hb_pour.py`, `hb_tests.py`, `hb_qemu.py`, `hb_report.py`, `formulae-survey.txt`, `build-*.nix`, `patches/`, `guest/`.

Reproduce:
```sh
H=integrations/puredarwin/homebrew
python3 -B $H/hb_fetch.py --list $H/formulae-survey.txt             # about 1.2 GB into runs/homebrew/
python3 -B $H/hb_pour.py --name main jq git python@3.14 ...           # prefix image runs/homebrew/prefix/main.img
O=$(nix-build --extra-experimental-features 'nix-command flakes' --no-out-link $H/build-overlay.nix)
python3 -B $H/hb_qemu.py --prefix main --name run1 --overlay $O      # boots, tests, results.md
```
