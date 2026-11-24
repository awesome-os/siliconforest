#!/bin/bash
# SPDX-License-Identifier: MIT
# The big parts of an installed SiliconForest's userland, built here and sent to the RUNNING
# system over SSH (the disk root is written by SiliconForest itself, through the fenced
# driver; nothing is written to its partition from Linux). The server and the key that may
# log in were put there by a bench boot (dr_desk_boot.sh DR_INSTALL=1 DR_USERLAND=1).
#
#   sf_userland_push.sh stage              build runs/sf-userland/stage/{sys,brew,claude} and their file lists
#   sf_userland_push.sh known <console.bin>  write the known-hosts file from the public host key the
#                                          install boot printed (SF_USERLAND hostkey ...)
#   sf_userland_push.sh ssh [command...]   a command (or a shell) on the running system
#   sf_userland_push.sh push <part>        send one part (gtar first, then sys, brew, claude, apps) as a tar stream, unpack it at /,
#                                          then compare the SHA-256 of every file there with the list made here
#   sf_userland_push.sh verify <part>      the comparison alone
#   sf_userland_push.sh libsystem <new libSystem.B.dylib> <sf-xpc-probe>
#                                          replace /usr/lib/libSystem.B.dylib (a build of ../../libsystem-default/build-libsystem.nix):
#                                          the file is sent beside it, and userland/sf-libsystem-install.sh checks its SHA-256
#                                          there, proves that programs run with it (the probe of userland/jit), keeps the file
#                                          in use as libSystem.B.dylib.old and only then renames. The one file in use this tool replaces.
#
#   sys     what macOS programs expect of a system and this root lacks (../../homebrew/build-overlay.nix:
#           libraries in /usr/lib, three frameworks, /usr/bin/curl, locale data; ../../macos-compat/
#           build-compat-root.nix: AppKit and Cocoa, the system Perl 5.34), the SSH client programs,
#           sftp-server. Only paths the root does not have yet: nothing in use is replaced.
#   brew    /opt/homebrew: the Homebrew client with its Ruby and the bottles of SF_FORMULAE, poured and
#           relocated on this workstation (../../homebrew/hb_pour.py), owned by the account `sf` (501:20)
#   claude  the Claude Code CLI as its vendor ships it (runs/homebrew/cli/claude-bin, fetched by the
#           Homebrew stream from downloads.claude.ai and checked against its manifest), at
#           /usr/local/lib/claude-code/<version>/claude with /usr/local/bin/claude pointing at it
#   gpu     the graphics of the default install (../../m1-vulkan/system.nix: GL Mesa b7b03c5 in
#           /usr/local/lib/mesa-asahi, the Vulkan loader, Honeykrisp, /etc/vulkan/icd.d, vulkaninfo and
#           vkcube): `push gpu` runs ../../m1-vulkan/tools/sf-gpu-install.sh push + install (files there
#           and different are kept as <name>.old-<tag>; `sf-gpu-install.sh revert <tag>` undoes it).
#           Running programs keep the GL they loaded: relaunch the compositor afterwards (gui.lock).
#   apps    what macOS applications need of the system beyond `sys` (../../terminal-app/
#           build-macos-apps-root.nix; docs/roadmap/issues/SF-Y-terminal-app.md): the application stack
#           with its patches, the Swift runtime, the framework wrappers, /usr/bin/open and /usr/bin/login.
#           Unlike the other parts it replaces files the root has, so it goes on by the guarded
#           procedures of ../../terminal-app/tools/sf_terminal.sh, in this order: `install new` (paths the
#           root lacks), `install core` (libSystem, libFoundation, the CoreFoundation and Foundation
#           framework paths, dyld: each proven on the target before it takes the name, the file in use
#           kept), `install direct` (the rest, existing files kept as .old-sfy or .pd). The five files
#           every program loads are only sent when their SHA-256 is in
#           ../../terminal-app/core-rehearsed.sha256, the record of the QEMU rehearsals that passed
#           (sf_terminal.sh qemu <name>, then qemu <name> second). `stage` does not build this part:
#           `push apps` builds it (sf_terminal.sh install-stage) when runs/sf-terminal/install is not there.
#           SF_USERLAND_DRY=1 push apps: check the hashes and print what would be run, send nothing.
# Nothing here is committed: runs/ holds bottles and binaries.
# The address is the one the M1 gets from the router whichever system runs (SF_HOST); the
# host key is SiliconForest's own, kept in a file of its own (SF_KNOWN), never in ~/.ssh/known_hosts.
set -euo pipefail
ROOT=$(cd "$(dirname "$(readlink -f "$0")")/../../../.." && pwd); cd "$ROOT"
export PATH=$HOME/.nix-profile/bin:$PATH
H=integrations/siliconforest/homebrew; W=runs/sf-userland; ST=$W/stage
SF_HOST=${SF_HOST:-10.1.1.211}; SF_KNOWN=${SF_KNOWN:-$HOME/.ssh/known_hosts_m1_siliconforest}
SF_FORMULAE=${SF_FORMULAE:-bash git less nano grep gnu-sed gawk gnu-tar coreutils findutils tmux python@3.14 node jq ripgrep tree htop wget fzf bat fd make xz zstd sqlite gh}
BASE=${SF_ROOT_IMAGE:-artifacts/m1-desktop-010/pcie-enum-candidate-sw01/ramdisk.img}
SSH=(ssh -o UserKnownHostsFile="$SF_KNOWN" -o GlobalKnownHostsFile=/dev/null -o StrictHostKeyChecking=yes -o HostKeyAlias=m1-siliconforest
     -o PasswordAuthentication=no -o BatchMode=yes -o ConnectTimeout=6 -o ServerAliveInterval=10 "root@$SF_HOST")
list() {  # <stage directory>: "sha256  path" of every regular file, "target<TAB>path" of every link
  (cd "$1" && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 -r sha256sum | sed 's|  \./|  |') > "$1.sha256"   # "sha256  path"
  (cd "$1" && find . -type l -printf '%l\t%P\n' | LC_ALL=C sort) > "$1.links"
  echo "$1: $(grep -c . "$1.sha256") files, $(grep -c . "$1.links" || true) links, $(du -sm "$1" | cut -f1) MB"
}
case "${1:?stage|known|ssh|push|verify|libsystem}" in
push|verify) if [ "${2:-}" = gpu ]; then G=integrations/siliconforest/m1-vulkan/tools/sf-gpu-install.sh
    [ "$1" = push ] && { "$G" push && "$G" install "${SF_GPU_TAG:-b7b03c5}"; } || "$G" status; exit $?; fi;;&
libsystem)
  F=${2:?the new libSystem.B.dylib}; PR=${3:?sf-xpc-probe (userland/jit/build-jit-test.nix)}; I=integrations/siliconforest/m1-storage/userland/sf-libsystem-install.sh
  [ "$(head -c 4 "$F" | od -An -tx1 | tr -d ' ')" = cffaedfe ] || { echo "$F is not a 64-bit Mach-O" >&2; exit 2; }
  S=$(sha256sum "$F" | cut -c1-64)
  "${SSH[@]}" 'cat > /private/tmp/sf-xpc-probe && chmod 755 /private/tmp/sf-xpc-probe' < "$PR"
  "${SSH[@]}" 'cat > /usr/lib/libSystem.B.dylib.sf-new && sync' < "$F"
  "${SSH[@]}" "sh -s /usr/lib/libSystem.B.dylib.sf-new $S /private/tmp/sf-xpc-probe" < "$I";;
stage)
  mkdir -p "$ST"; chmod -R u+w "$ST" 2>/dev/null || true; rm -rf "$ST/sys" "$ST/claude"
  # ---- gtar: GNU tar from its bottle, the one file, as fetched (it links libSystem and libiconv only).
  # The system's own tar (toybox 0.8.13) gives up after some eighty files of one archive ("bad symlink";
  # boot dr39: 81 of 21,770 files of the brew part arrived); two entries it can do.
  python3 -B $H/hb_fetch.py gnu-tar > "$W/fetch-gtar.log" 2>&1 || { tail -3 "$W/fetch-gtar.log" >&2; exit 2; }
  rm -rf "$ST/gtar"; mkdir -p "$ST/gtar/usr/local/libexec"
  cp "$(ls -d runs/homebrew/kegs/arm64_tahoe/gnu-tar/*/bin/gtar | tail -1)" "$ST/gtar/usr/local/libexec/sf-gtar"; chmod 755 "$ST/gtar/usr/local/libexec/sf-gtar"
  list "$ST/gtar"
  # ---- sys
  O=$(cd $H && nix-build --extra-experimental-features 'nix-command flakes' --no-out-link build-overlay.nix | tail -1)
  S=$(integrations/siliconforest/m1-boot-speed/tools/bs_nix_path.py m1-storage/userland build-openssh.nix)
  [ -x "$O/usr/bin/curl" ] && [ -x "$S/bin/ssh" ] || { echo "no overlay or no OpenSSH" >&2; exit 2; }
  mkdir -p "$ST/sys"; cp -a "$O/." "$ST/sys/"; chmod -R u+w "$ST/sys"
  # AppKit/Cocoa and the system Perl 5.34 (../../macos-compat/build-compat-root.nix; SF_COMPAT=0 leaves them out)
  if [ "${SF_COMPAT:-1}" = 1 ]; then
    CR=$(cd integrations/siliconforest/macos-compat && nix-build --extra-experimental-features 'nix-command flakes' --no-out-link build-compat-root.nix | tail -1)
    [ -d "$CR/System/Library/Frameworks/AppKit.framework" ] || { echo "no compat root" >&2; exit 2; }
    cp -a "$CR/." "$ST/sys/"; chmod -R u+w "$ST/sys"
  fi
  rm -f "$ST/sys/usr/lib/libhbcrash.dylib"                      # a diagnostic of the Homebrew stream
  mkdir -p "$ST/sys/bin" "$ST/sys/libexec" "$ST/sys/etc/ssh"
  for f in ssh scp sftp ssh-add ssh-agent ssh-keyscan; do cp "$S/bin/$f" "$ST/sys/bin/$f"; done
  cp "$S/libexec/sftp-server" "$ST/sys/libexec/"; cp "$S/etc/ssh/moduli" "$S/etc/ssh/ssh_config" "$ST/sys/etc/ssh/"
  # only paths the root does not have: a file that is there is left alone (and named when it differs)
  (cd "$ST/sys" && find . \( -type f -o -type l \) -printf '%P\n') | while read -r f; do
    if debugfs -R "stat /$f" "$BASE" 2>/dev/null | grep -q '^Inode:'; then
      if [ -f "$ST/sys/$f" ] && [ ! -L "$ST/sys/$f" ] && [ "$(debugfs -R "cat /$f" "$BASE" 2>/dev/null | sha256sum)" = "$(sha256sum < "$ST/sys/$f")" ]; then echo "sys: the root has /$f, identical"
      else echo "sys: the root has /$f, DIFFERENT: the root's is kept"; fi
      rm -f "$ST/sys/$f"
    fi
  done
  chmod -R u+w,go-w "$ST/sys"; list "$ST/sys"
  # ---- brew
  python3 -B $H/hb_fetch.py $SF_FORMULAE portable-ruby > "$W/fetch.log" 2>&1 || { tail -3 "$W/fetch.log" >&2; exit 2; }
  python3 -B $H/hb_pour.py --name sf-m1 --no-image --brew runs/homebrew/brew-src $SF_FORMULAE > "$W/pour.json" || { tail -5 "$W/pour.json" >&2; exit 2; }
  rm -rf "$ST/brew"; mkdir -p "$ST/brew/opt"; mv runs/homebrew/prefix/sf-m1 "$ST/brew/opt/homebrew"
  rm -rf "$ST/brew/opt/homebrew/.git"
  # python3 and pip3 by those names (the bottle links only the versioned ones; brew's own post-install is not run here)
  for n in python3 pip3; do
    for d in "$ST"/brew/opt/homebrew/opt/python@3.*/libexec/bin; do
      [ -e "$d/$n" ] && [ ! -e "$ST/brew/opt/homebrew/bin/$n" ] && ln -s "../opt/$(basename "$(dirname "$(dirname "$d")")")/libexec/bin/$n" "$ST/brew/opt/homebrew/bin/$n"
    done
  done
  list "$ST/brew"
  # ---- claude
  C=runs/homebrew/cli/claude-bin; B=$(find "$C" -type f -name claude | head -1); [ -n "$B" ] || { echo "no Claude Code binary under $C" >&2; exit 2; }
  V=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' runs/homebrew/cli/claude-manifest.json)
  WANT=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["platforms"]["darwin-arm64"]["checksum"])' runs/homebrew/cli/claude-manifest.json)
  [ "$(sha256sum "$B" | cut -d' ' -f1)" = "$WANT" ] || { echo "the Claude Code binary is not the one its manifest names" >&2; exit 2; }
  mkdir -p "$ST/claude/usr/local/lib/claude-code/$V" "$ST/claude/usr/local/bin"
  cp --reflink=auto "$B" "$ST/claude/usr/local/lib/claude-code/$V/claude"; chmod 755 "$ST/claude/usr/local/lib/claude-code/$V/claude"
  ln -s "../lib/claude-code/$V/claude" "$ST/claude/usr/local/bin/claude"
  list "$ST/claude";;
known)
  CON=${2:?console.bin of the install boot}
  KEY=$(strings -n 8 "$CON" | sed -n 's/.*SF_USERLAND hostkey \(ssh-ed25519 [A-Za-z0-9+/=]*\).*/\1/p' | tail -1)
  [ -n "$KEY" ] || { echo "no host key line in $CON" >&2; exit 2; }
  umask 077; printf 'm1-siliconforest %s\n' "$KEY" > "$SF_KNOWN"
  echo "$SF_KNOWN: $(ssh-keygen -l -f "$SF_KNOWN" | cut -d' ' -f1,2,4)";;
ssh) shift; exec "${SSH[@]}" "$@";;
push|verify)
  P=${2:?gtar|sys|brew|claude|apps}
  if [ "$P" = apps ]; then
    TT=integrations/siliconforest/terminal-app/tools/sf_terminal.sh; IN=runs/sf-terminal/install
    REH=integrations/siliconforest/terminal-app/core-rehearsed.sha256
    [ -s "$IN/core/replace.list" ] && [ -s "$IN/direct/manifest" ] || "$TT" install-stage
    # the files every program loads: only as the files a rehearsal passed with
    bad=0
    while read -r path sum _; do
      case "$path" in /usr/lib/libSystem.B.dylib|/usr/lib/libFoundation.dylib|*/CoreFoundation.framework/Versions/A/CoreFoundation|*/Foundation.framework/Versions/C/Foundation|*/IOKit.framework/Versions/A/IOKit)
        if grep -q "^$sum  $path " "$REH" 2>/dev/null; then echo "SFPUSH apps core $path $(echo "$sum" | cut -c1-12): rehearsed ($(grep "^$sum  $path " "$REH" | cut -d' ' -f4- | head -1))"
        else echo "SFPUSH apps core $path $(echo "$sum" | cut -c1-12): NOT in $REH"; bad=1; fi;;
      esac
    done < "$IN/core/replace.list"
    sum=$(cat "$IN/core/dyld.sha256")
    if grep -q "^$sum  /usr/lib/dyld " "$REH" 2>/dev/null; then echo "SFPUSH apps core /usr/lib/dyld $(echo "$sum" | cut -c1-12): rehearsed"
    else echo "SFPUSH apps core /usr/lib/dyld $(echo "$sum" | cut -c1-12): NOT in $REH"; bad=1; fi
    [ "$bad" = 0 ] || { echo "SFPUSH apps: a file every program loads has not passed a rehearsal (sf_terminal.sh qemu <name>, qemu <name> second); nothing sent" >&2; exit 1; }
    if [ "$1" = push ]; then
      if [ -n "${SF_USERLAND_DRY:-}" ]; then
        echo "SFPUSH apps (dry): would run $TT install new, install core, install direct: $(grep -c . "$IN/direct/manifest") files, $(grep -c . "$IN/direct/links") links, $(grep -c . "$IN/core/replace.list") guarded replacements and dyld"; exit 0
      fi
      "$TT" install new && "$TT" install core && "$TT" install direct || { echo "SFPUSH apps: an install step failed (see above)" >&2; exit 1; }
    fi
    [ -z "${SF_USERLAND_DRY:-}" ] || exit 0
    # every file of the part there, hashed there, compared here (a wrapped library <name>.pd is the root's own)
    grep -v '\.pd$' "$IN/direct/manifest" | sed 's/^\([0-9a-f]*\) \(.*\)$/\1  \2/' > "$W/apps.sha256"
    while read -r path sum _; do
      case "$path" in /usr/lib/libSystem.B.dylib|/usr/lib/libFoundation.dylib|*/CoreFoundation.framework/Versions/A/CoreFoundation|*/Foundation.framework/Versions/C/Foundation|*/IOKit.framework/Versions/A/IOKit) echo "$sum  ${path#/}" >> "$W/apps.sha256";; esac
    done < "$IN/core/replace.list"
    echo "$(cat "$IN/core/dyld.sha256")  usr/lib/dyld" >> "$W/apps.sha256"
    cut -c67- "$W/apps.sha256" | tr '\n' '\0' | "${SSH[@]}" 'cd / && xargs -0 sha256sum 2>&1' > "$W/apps.remote.sha256" || true
    if diff "$W/apps.sha256" "$W/apps.remote.sha256" > "$W/apps.diff"; then echo "SFPUSH apps verified: $(grep -c . "$W/apps.sha256") files equal on the target"
    else
      echo "SFPUSH apps DIFFERS: $(grep -c '^>' "$W/apps.diff") files"
      # each file that differs: how much. (A file the kernel maps executable can read back with a few
      # instructions changed: upstream's kernel rewrites pointer-authentication instructions in the pages
      # it maps, and on the M1 that showed through read(2) for libSystem: 4 words, xpaclri as nop.)
      grep '^<' "$W/apps.diff" | cut -c69- | while read -r f; do
        "${SSH[@]}" "cat '/$f'" < /dev/null > "$W/apps.there" 2>/dev/null || true
        here=$IN/direct/files/$f; case "$f" in usr/lib/dyld) here=$IN/core/dyld.new;; usr/lib/libSystem.B.dylib|usr/lib/libFoundation.dylib) here=$IN/core/replace/$(basename "$f").new;; */CoreFoundation|*/Foundation|*/IOKit) [ -e "$here" ] || here=$IN/core/replace/$(basename "$f").new;; esac
        if [ "$(stat -c %s "$W/apps.there")" = "$(stat -c %s "$here")" ]; then echo "SFPUSH apps differs: /$f in $(cmp -l "$W/apps.there" "$here" | wc -l) bytes of $(stat -c %s "$here")"
        else echo "SFPUSH apps differs: /$f is $(stat -c %s "$W/apps.there") bytes there, $(stat -c %s "$here") here"; fi
      done
      rm -f "$W/apps.there"; exit 1
    fi
    exit 0
  fi
  D=$ST/$P; [ -d "$D" ] && [ -s "$D.sha256" ] || { echo "no $D: run stage" >&2; exit 2; }
  if [ "$1" = push ]; then
    OWN=(--owner=0 --group=0); [ "$P" != brew ] || OWN=(--owner=501 --group=20)
    t0=$(date +%s)
    # Members: for brew the prefix itself; for the others only files and links, so that no directory of the
    # system (/, /usr, /opt ...) takes its owner or mode from the archive. GNU tar on the target when the gtar
    # part is there and runs, else the system's tar.
    if [ "$P" = brew ]; then printf 'opt/homebrew\0' > "$W/$P.members"; REC=()
    else (cd "$D" && find . -mindepth 1 \( -type f -o -type l \) -printf '%P\0') > "$W/$P.members"; REC=(--no-recursion); fi
    tar -C "$D" "${OWN[@]}" --numeric-owner --format=gnu "${REC[@]}" --null -T "$W/$P.members" -cf - | gzip -1 | "${SSH[@]}" '
      X=/usr/local/libexec/sf-gtar
      if [ -x $X ] && $X --version > /dev/null 2>&1; then echo "SFPUSH_TAR $($X --version | head -n 1)"; gunzip | $X -x -p --numeric-owner -C / -f - 2>&1 | tail -n 5
      else echo "SFPUSH_TAR the system tar"; gunzip | tar -x -p -C / -f - 2>&1 | tail -n 5; fi; sync'
    echo "SFPUSH $P sent in $(( $(date +%s) - t0 )) s"
  fi
  # every file there, hashed there, compared here
  cut -c67- "$D.sha256" | tr '\n' '\0' | "${SSH[@]}" 'cd / && xargs -0 sha256sum 2>&1' > "$W/$P.remote.sha256" || true
  if diff "$D.sha256" "$W/$P.remote.sha256" > "$W/$P.diff"; then echo "SFPUSH $P verified: $(grep -c . "$D.sha256") files equal on the target"
  else echo "SFPUSH $P DIFFERS: $(grep -c '^[<>]' "$W/$P.diff") lines, e.g. $(head -3 "$W/$P.diff" | cut -c1-160 | tr '\n' ' ')"; exit 1; fi
  # every link's target, read there in one go (a loop of 26,000 readlink calls took a whole session in dr41)
  cut -f2 "$D.links" | tr '\n' '\0' | "${SSH[@]}" 'cd / && xargs -0 readlink 2>&1' > "$W/$P.remote.links" || true
  if diff <(cut -f1 "$D.links") "$W/$P.remote.links" > "$W/$P.linkdiff"; then echo "SFPUSH $P links: $(grep -c . "$D.links" || true) equal"
  else echo "SFPUSH $P LINKS DIFFER: $(grep -c '^[<>]' "$W/$P.linkdiff") lines, e.g. $(head -3 "$W/$P.linkdiff" | cut -c1-120 | tr '\n' ' ')"; exit 1; fi;;
*) echo "stage|known|ssh|push|verify" >&2; exit 2;;
esac
