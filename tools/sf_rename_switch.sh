#!/bin/sh
# SPDX-License-Identifier: MIT
# The SiliconForest rename on an installed system: puts the renamed user space of a staged
# directory in place, keeping every file it replaces or removes, so that sf_rename_restore.sh
# can put the system back exactly. docs/design/siliconforest-naming-m1-install.md.
#
#   sf_rename_switch.sh [--dry-run] [--root DIR] [--name NAME] [--classes "C ..."] [--allow-mismatch] [--fail-after N] STAGE
#
#   STAGE     the staged directory (tools/sf_rename_stage.py): manifest.tsv and root/
#   --root    the system's root (default /). A mounted SiliconForest partition or a mock root
#             works the same; nothing outside DIR is read or written
#   --name    the directory under ROOT/private/var/db that keeps the old files and the journal
#             (default sf-rename); a separate stage (the audio pre-test) uses its own name
#   --dry-run checks everything and prints what would be done; changes nothing
#   --classes the classes to do in this run, in the order given (default "state direct launchd core").
#             A later run with the same manifest does the rest: e.g. everything but core on the
#             running system, core from Linux on the mounted partition
#   --allow-mismatch  go on when a live file is neither the expected old file nor the new one
#             (it is still kept); without it such a file stops the switch before any change
#   --fail-after N    test hook: stop as if interrupted after N changes
#
# manifest.tsv, one row per line, tab separated, '#' starts a comment:
#   install  PATH  OLD  NEW  MODE  CLASS  NOTE   the file root/PATH of the stage goes to PATH
#   link     PATH  OLD  TARGET  -  CLASS  NOTE   PATH becomes a symbolic link to TARGET
#   remove   PATH  OLD  -    -     CLASS  NOTE   PATH is taken away (kept in old/)
#   migrate  PATH  OLD  -    MODE  CLASS  NOTE   state keyed by an old name: NOTE is "from=<old path>";
#                                                PATH is made as a copy of it when PATH does not exist.
#                                                The old file stays where it is (the old system needs it).
#                                                "from=<old path>;sed=<expression>": the copy goes through
#                                                sed (names inside an XML property list, hashes inside a
#                                                receipt); a binary property list is carried over as it is
#   sweep    PATH  -    -    -     CLASS  NOTE   every regular file whose path begins with PATH (backups an
#                                                earlier install left beside a library: AppKit.pre-t13 ...) is taken
#                                                away like a `remove` row; the dry run lists what it finds
#   rmdir    PATH  -    -    -     CLASS  NOTE   the directory PATH is taken away when the class's other rows have
#                                                left it empty (a bundle under its old name); one that is not empty stays
#   keep     PATH  ...                           accounted for, not touched (printed by --dry-run)
# OLD is the SHA-256 the live file is expected to have, "absent", or "unknown" (not in the lab's
# records: any content is accepted and kept). NEW is the SHA-256 of the staged file.
# CLASS orders the work: state, direct, launchd, then core (the files every program loads) last.
# Class kernel is never in a run unless --classes names it: files that must not be in place while
# the old kernel runs (sf-halt: it finds the write fence by a sysctl name; under the other kernel
# it would restart the machine without sealing the disk). It is done from Linux on the mounted
# partition in the same sitting as the kernel install.
#
# On the running system the hash of a file in use is not the hash of the file on the disk: the
# kernel rewrites pointer-authentication instructions in the page cache of an executed signed
# file and read(2) returns those bytes (m1-storage/userland/sf-libsystem-install.sh). Run there,
# the check shows DIFF for libraries in use; the dry run tells which, --allow-mismatch goes on
# (the old file is kept through a hard link, the same bytes on the disk). Against the partition
# mounted from Linux the hashes are the disk's and must match. New files are hashed before
# anything can have loaded them.
#
# Kept under ROOT/private/var/db/sf-rename/: old/<path> (every file replaced or removed),
# journal (one line per change, written before the change), switched (the manifest used).
# A file that is replaced is never absent from its path: the old file gets a second name in
# old/ (a hard link), then the new file is renamed over the path in one step. That matters for
# the core class (dyld, libSystem ...): every command this script starts loads them.
# A switch that was interrupted is undone with sf_rename_restore.sh, or simply run again with
# the same manifest (rows already done are skipped).
#
# The core class goes in as one step. libSystem and /usr/lib/system/libdyld.dylib name each
# other's entry points (sf_libdyld_libkernel_init ...): a program started while one is new and
# the other old does not load. So every core file is first put beside its target and checked
# (PATH.sf-new), and then all of them are renamed into place by ONE process that starts no other
# program in between: STAGE/sf-rename-commit (m1-storage/userland/sf-rename-commit.c; kept beside
# the journal for the restore), else zsh's own mv (zsh/files, which SiliconForest's zsh is built
# without), else python3 or perl. A root that is not the running system (--root DIR: the partition mounted from Linux, a
# mock root) needs none of that and takes plain mv. The dry run says which way it would take;
# when the running system offers none, the core class is refused before anything is changed.
# The kernel is not this script's: it is installed from Linux with install/linux/sf-bootbin-install.sh.
set -u
NAME=sf-rename; DRY=0; ROOT=/; ALLOW=0; FAILAFTER=0; STAGE=; CLASSES='state direct launchd core'
while [ $# -gt 0 ]; do
  case "$1" in
    --dry-run) DRY=1;;
    --root) ROOT=$2; shift;;
    --name) NAME=$2; shift;;
    --allow-mismatch) ALLOW=1;;
    --classes) CLASSES=$2; shift;;
    --fail-after) FAILAFTER=$2; shift;;
    -h|--help) sed -n '2,/^set -u/p' "$0" | sed '$d' | cut -c3-; exit 0;;
    -*) echo "sf_rename_switch: unknown option $1" >&2; exit 2;;
    *) STAGE=$1;;
  esac
  shift
done
[ -n "$STAGE" ] && [ -f "$STAGE/manifest.tsv" ] && [ -d "$STAGE/root" ] || { echo "sf_rename_switch: STAGE needs manifest.tsv and root/" >&2; exit 2; }
ROOT=${ROOT%/}
[ -d "$ROOT/" ] || { echo "sf_rename_switch: no root $ROOT/" >&2; exit 2; }
case "$NAME" in *[!A-Za-z0-9._-]*|"") echo "sf_rename_switch: --name: letters, digits, . _ - only" >&2; exit 2;; esac
DB=$ROOT/private/var/db/$NAME; OLD=$DB/old; J=$DB/journal
TAB=$(printf '\t')
if command -v sha256sum >/dev/null 2>&1; then sha() { sha256sum "$1" | cut -c1-64; }
elif command -v shasum >/dev/null 2>&1; then sha() { shasum -a 256 "$1" | cut -c1-64; }
else sha() { openssl dgst -sha256 "$1" | sed 's/.*= *//'; }; fi
say() { printf '%s\n' "$*"; }
die() { printf 'sf_rename_switch: %s\n' "$*" >&2; exit 1; }
rows() { # the manifest's rows of one class, in file order; its rmdir rows after the others, the deepest first
  grep -v '^#' "$STAGE/manifest.tsv" | grep -v '^$' | while IFS=$TAB read -r act path old new mode class note; do
    [ "$class" = "$1" ] && [ "$act" != rmdir ] && printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$act" "$path" "$old" "$new" "$mode" "$class" "$note"
  done
  grep -v '^#' "$STAGE/manifest.tsv" | grep -v '^$' | while IFS=$TAB read -r act path old new mode class note; do
    [ "$class" = "$1" ] && [ "$act" = rmdir ] && printf '%s\t%s\n' "${#path}" "$path"
  done | sort -rn | while IFS=$TAB read -r len path; do printf 'rmdir\t%s\t-\t-\t-\t%s\t-\n' "$path" "$1"; done
}
swept() { # the regular files a sweep row means: PATH followed by anything, not the switch's own temporary names
  for f in "$ROOT$1"*; do
    [ -f "$f" ] && [ ! -L "$f" ] || continue
    case "$f" in *.sf-new|*.sf-old) continue;; esac
    printf '%s\n' "${f#"$ROOT"}"
  done
}
# what is at a live path: absent, link:<target>, dir, or the file's SHA-256
state_of() {
  if [ -L "$1" ]; then printf 'link:%s' "$(readlink "$1")"
  elif [ -d "$1" ]; then printf 'dir'
  elif [ -e "$1" ]; then sha "$1"
  else printf 'absent'; fi
}

# how the core class's renames are done: one process, nothing started in between (see the head)
TOOL=${SF_RENAME_TOOL:-$STAGE/sf-rename-commit}     # m1-storage/userland/sf-rename-commit.c, beside the scripts in the stage
commit_method() {
  if [ -n "${SF_RENAME_COMMIT:-}" ]; then echo "$SF_RENAME_COMMIT"; return; fi   # for the tests: mv, tool, zsh, python3, perl
  if [ -n "$ROOT" ]; then echo mv; return; fi           # not the running system
  if [ -x "$TOOL" ] && "$TOOL" --check 2>/dev/null; then echo tool; return; fi
  if [ -n "${ZSH_VERSION:-}" ] && (zmodload -F zsh/files b:zf_mv) 2>/dev/null; then echo zsh; return; fi
  if command -v python3 >/dev/null 2>&1 && python3 -c 'import os' 2>/dev/null; then echo python3; return; fi
  if command -v perl >/dev/null 2>&1 && perl -e 1 2>/dev/null; then echo perl; return; fi
  echo none
}
commit_renames() { # $1: a file of "FROM<tab>TO" lines; every FROM is renamed over its TO
  case "$COMMIT" in
    mv) while IFS=$TAB read -r from to; do mv -f "$from" "$to" || return 1; done < "$1";;
    tool) "$TOOL" "$1";;
    zsh) zmodload -F zsh/files b:zf_mv || return 1
         while IFS=$TAB read -r from to; do zf_mv -f "$from" "$to" || return 1; done < "$1";;
    python3) python3 -c 'import os, sys
for line in open(sys.argv[1]):
    a, b = line.rstrip("\n").split("\t")
    os.rename(a, b)' "$1";;
    perl) perl -e 'open(F, "<", $ARGV[0]) or die; while (<F>) { chomp; my ($a, $b) = split /\t/; rename($a, $b) or die "$a: $!"; }' "$1";;
    *) return 1;;
  esac
}
# ---- 1. checks (nothing is changed) ---------------------------------------------------------
if [ -e "$J" ] && ! cmp -s "$STAGE/manifest.tsv" "$DB/switched"; then
  die "a journal of a switch with another manifest exists at $J. Run sf_rename_restore.sh first"
fi
for c in $CLASSES; do case "$c" in state|direct|launchd|core|kernel) ;; *) die "--classes: unknown class $c";; esac; done
inrun() { case " $CLASSES " in *" $1 "*) return 0;; esac; return 1; }
COMMIT=mv
if inrun kernel && [ -z "$ROOT" ] && [ "${SF_RENAME_KERNEL_LIVE:-0}" != 1 ]; then
  die "class kernel on the running system: its files belong to the other kernel (see the head of this script). Do it from Linux on the mounted partition (--root). Nothing was changed"
fi
if inrun core; then
  COMMIT=$(commit_method)
  say "core class: its files go in together, renamed by $COMMIT"
  [ "$COMMIT" != none ] || die "the running system offers no way to rename the core files in one step (STAGE/sf-rename-commit, zsh/files, python3, perl); do the core class from Linux on the mounted partition. Nothing was changed"
fi
bad=0; nwork=0; nskip=0; nunknown=0
CHECK=$(mktemp "${TMPDIR:-/tmp}/sf-rename-check.XXXXXX") || die "mktemp"
grep -v '^#' "$STAGE/manifest.tsv" | grep -v '^$' | while IFS=$TAB read -r act path old new mode class note; do
  live=$ROOT$path
  case "$path" in /*) ;; *) say "BAD  row without an absolute path: $act $path"; echo bad >> "$CHECK"; continue;; esac
  case "$path" in */../*|*/..) say "BAD  path with ..: $path"; echo bad >> "$CHECK"; continue;; esac
  case "$class" in state|direct|launchd|core|kernel|none) ;; *) say "BAD  unknown class '$class': $path"; echo bad >> "$CHECK"; continue;; esac
  is=$(state_of "$live")
  [ "$act" = keep ] || inrun "$class" || { say "later $path (class $class is not in this run)"; continue; }
  case "$act" in
    install)
      [ -f "$STAGE/root$path" ] || { say "BAD  staged file missing: $path"; echo bad >> "$CHECK"; continue; }
      got=$(sha "$STAGE/root$path")
      [ "$got" = "$new" ] || { say "BAD  staged file is $got, manifest says $new: $path"; echo bad >> "$CHECK"; continue; }
      if [ "$is" = "$new" ]; then say "same $path (already the new file)"; echo skip >> "$CHECK"
      elif [ "$is" = dir ]; then say "BAD  a directory is in the way: $path"; echo bad >> "$CHECK"
      elif [ "$old" = unknown ]; then say "inst $path (live: $is; no recorded old hash)"; echo work >> "$CHECK"; echo unknown >> "$CHECK"
      elif [ "$is" = "$old" ]; then say "inst $path"; echo work >> "$CHECK"
      else say "DIFF $path: live is $is, expected $old"; echo mismatch >> "$CHECK"; echo work >> "$CHECK"; fi;;
    link)
      if [ "$is" = "link:$new" ]; then say "same $path -> $new"; echo skip >> "$CHECK"
      elif [ "$is" = dir ]; then say "BAD  a directory is in the way: $path"; echo bad >> "$CHECK"
      elif [ "$old" = unknown ] || [ "$is" = "$old" ]; then say "link $path -> $new (live: $is)"; echo work >> "$CHECK"
      else say "DIFF $path: live is $is, expected $old"; echo mismatch >> "$CHECK"; echo work >> "$CHECK"; fi;;
    remove)
      if [ "$is" = absent ]; then say "same $path (already absent)"; echo skip >> "$CHECK"
      elif [ "$is" = dir ]; then say "BAD  remove of a directory is not supported: $path"; echo bad >> "$CHECK"
      elif [ "$old" = unknown ] || [ "$is" = "$old" ]; then say "rm   $path (kept in old/)"; echo work >> "$CHECK"
      else say "DIFF $path: live is $is, expected $old"; echo mismatch >> "$CHECK"; echo work >> "$CHECK"; fi;;
    migrate)
      from=${note#from=}; from=${from%%;sed=*}
      case "$from" in /*) ;; *) say "BAD  migrate without from=<path>: $path"; echo bad >> "$CHECK"; continue;; esac
      if [ "$is" != absent ]; then say "same $path (exists; $from is left alone)"; echo skip >> "$CHECK"
      elif [ -f "$ROOT$from" ]; then say "copy $from -> $path"; echo work >> "$CHECK"
      else say "none $path: nothing at $from to carry over"; echo skip >> "$CHECK"; fi;;
    sweep)
      n=0; for f in $(swept "$path"); do say "rm   $f (swept; kept in old/)"; echo work >> "$CHECK"; n=$((n + 1)); done
      [ $n = 0 ] && { say "same $path* (nothing there)"; echo skip >> "$CHECK"; };;
    rmdir)
      if [ "$is" = absent ]; then say "same $path (already absent)"; echo skip >> "$CHECK"
      elif [ "$is" = dir ]; then say "rmd  $path (when this run has emptied it)"; echo work >> "$CHECK"
      else say "BAD  rmdir of something that is not a directory: $path"; echo bad >> "$CHECK"; fi;;
    keep) say "keep $path ($note)";;
    *) say "BAD  unknown action '$act': $path"; echo bad >> "$CHECK";;
  esac
done
nwork=$(grep -c '^work$' "$CHECK"); nskip=$(grep -c '^skip$' "$CHECK"); bad=$(grep -c '^bad$' "$CHECK")
nmis=$(grep -c '^mismatch$' "$CHECK"); nunknown=$(grep -c '^unknown$' "$CHECK"); rm -f "$CHECK"
say "checked: $nwork to do, $nskip already as wanted, $nunknown without a recorded old hash, $nmis different from the record, $bad bad rows"
[ "$bad" = 0 ] || die "the manifest or the stage is not usable; nothing was changed"
if [ "$nmis" != 0 ] && [ "$ALLOW" = 0 ]; then
  [ "$DRY" = 1 ] && { say "dry run: a real run would stop here (live files differ from the record; --allow-mismatch goes on and keeps them)"; exit 3; }
  die "live files differ from the record (DIFF lines above); nothing was changed. --allow-mismatch goes on and keeps them"
fi
if [ "$DRY" = 1 ]; then say "dry run: nothing was changed"; exit 0; fi

# ---- 2. the switch ---------------------------------------------------------------------------
mkdir -p "$OLD" || die "cannot make $OLD"
cp "$STAGE/manifest.tsv" "$DB/switched" || die "cannot write $DB/switched"
if [ -f "$TOOL" ]; then cp "$TOOL" "$DB/sf-rename-commit" && chmod 755 "$DB/sf-rename-commit" || die "cannot keep the rename tool for the restore"; fi
[ -e "$J" ] || : > "$J" || die "cannot write $J"
count=0
step() { # called before each change; the test hook stops here
  if [ "$FAILAFTER" != 0 ] && [ "$count" -ge "$FAILAFTER" ]; then sync; die "stopped after $count changes (--fail-after)"; fi
  count=$((count + 1))
}
put_away() { # moves the live PATH to old/PATH (its directory made first)
  mkdir -p "$OLD$(dirname "$1")" && mv "$ROOT$1" "$OLD$1"
}
keep_old() { # gives the live file PATH a second name, old/PATH; the path itself stays as it is
  mkdir -p "$OLD$(dirname "$1")" || return 1
  [ -e "$OLD$1" ] && return 0          # kept by an earlier, interrupted run: that one is the old file
  if [ -L "$ROOT$1" ]; then cp -P "$ROOT$1" "$OLD$1"; else ln "$ROOT$1" "$OLD$1" 2>/dev/null || cp -p "$ROOT$1" "$OLD$1"; fi
}
made_dir() { # journals the topmost directory of PATH's chain that does not exist yet (the restore takes it away again)
  d=$1; top=
  while [ -n "$d" ] && [ "$d" != / ] && [ ! -d "$ROOT$d" ]; do top=$d; d=$(dirname "$d"); done
  [ -z "$top" ] || printf 'madedir\t%s\t-\n' "$top" >> "$J"
}
do_class() {
  rows "$1" > "$DB/rows.$1"
  rm -f "$DB/commit.core"
  while IFS=$TAB read -r act path old new mode class note; do
    live=$ROOT$path; is=$(state_of "$live")
    case "$act" in
      install)
        [ "$is" = "$new" ] && continue
        step
        made_dir "$(dirname "$path")"
        mkdir -p "$(dirname "$live")" || die "mkdir for $path"
        cp "$STAGE/root$path" "$live.sf-new" || die "copy of $path"
        [ "$(sha "$live.sf-new")" = "$new" ] || { rm -f "$live.sf-new"; die "the copy of $path does not have the manifest's hash; stopped"; }
        chmod "$mode" "$live.sf-new" || die "chmod $path"
        [ "$(id -u)" = 0 ] && chown 0:0 "$live.sf-new"
        if [ "$is" = absent ]; then printf 'installed\t%s\tnew\n' "$path" >> "$J"
        else printf 'installed\t%s\treplaced\n' "$path" >> "$J"; keep_old "$path" || die "cannot keep the old $path"; fi
        if [ "$1" = core ]; then printf '%s\t%s\n' "$live.sf-new" "$live" >> "$DB/commit.core"
        else mv -f "$live.sf-new" "$live" || die "cannot put $path in place (its old file is in old/ as well)"; fi;;
      link)
        [ "$is" = "link:$new" ] && continue
        step
        mkdir -p "$(dirname "$live")" || die "mkdir for $path"
        if [ "$is" = absent ]; then printf 'installed\t%s\tnew\n' "$path" >> "$J"
        else printf 'installed\t%s\treplaced\n' "$path" >> "$J"; put_away "$path" || die "cannot keep the old $path"; fi
        ln -s "$new" "$live" || die "cannot link $path";;
      remove)
        [ "$is" = absent ] && continue
        step
        printf 'removed\t%s\t-\n' "$path" >> "$J"
        put_away "$path" || die "cannot keep the old $path";;
      sweep)
        for f in $(swept "$path"); do
          step
          printf 'removed\t%s\t-\n' "$f" >> "$J"
          put_away "$f" || die "cannot keep the old $f"
        done;;
      rmdir)
        [ "$is" = dir ] || continue
        if [ -z "$(ls -A "$live" 2>/dev/null)" ]; then
          step
          printf 'rmdir\t%s\t-\n' "$path" >> "$J"
          rmdir "$live" || die "cannot take the directory $path away"
        else say "left $path (not empty)"; fi;;
      migrate)
        from=${note#from=}; expr=; case "$from" in *";sed="*) expr=${from#*;sed=}; from=${from%%;sed=*};; esac
        [ "$is" = absent ] && [ -f "$ROOT$from" ] || continue
        step
        mkdir -p "$(dirname "$live")" || die "mkdir for $path"
        printf 'migrated\t%s\t%s\n' "$path" "$from" >> "$J"
        cp -p "$ROOT$from" "$live" || die "cannot copy $from to $path"
        # names inside the file: only in a text file (a binary property list is carried over as it is)
        if [ -n "$expr" ] && ! head -c 6 "$live" | grep -q 'bplist'; then
          sed "$expr" "$ROOT$from" > "$live" || die "cannot rewrite $path"
        fi;;
    esac
  done < "$DB/rows.$1"
  rm -f "$DB/rows.$1"
  if [ -s "$DB/commit.core" ]; then
    sync
    commit_renames "$DB/commit.core" || die "the core files did not all go in (see $DB/commit.core: FROM is still there for those that did not). Run sf_rename_restore.sh, or this switch again"
  fi
  rm -f "$DB/commit.core"
}
for c in $CLASSES; do do_class "$c"; done
sync
say "switched: $count changes; old files in $OLD; undo with sf_rename_restore.sh --root ${ROOT:-/} --name $NAME"
# the halt program must be the running kernel's: it finds the write fence by a sysctl name
if [ -z "$ROOT" ] && sysctl -n kern.pd_nvme.state > /dev/null 2>&1; then
  if [ -e "$OLD/sbin/sf-halt" ]; then
    say "NOTE: the old kernel is running and /sbin/sf-halt is the renamed program, which does not find this kernel's write fence."
    say "      Do not use reboot/halt now. Restart with: $OLD/sbin/sf-halt restart"
  else say "note: the old kernel is running; /sbin/sf-halt is still its own program (class kernel not done): reboot as usual"; fi
fi
