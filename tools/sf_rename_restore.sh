#!/bin/sh
# SPDX-License-Identifier: MIT
# Undoes tools/sf_rename_switch.sh on an installed system, complete or interrupted: every file
# the switch put in place is taken out again (kept under new/), every file it replaced or
# removed is put back from old/. It needs no stage directory: the journal says what was done.
#
#   sf_rename_restore.sh [--dry-run] [--root DIR] [--name NAME]
#
# Reads ROOT/private/var/db/NAME/journal backwards (NAME: default sf-rename, as given to the switch). Afterwards that directory holds
#   new/<path>          the renamed files that were in place (state copies made by the switch
#                       are there too, with whatever the renamed system wrote into them)
#   journal.restored.N  the journal that was undone
# and the root is as it was before the switch, byte for byte, for every path of the manifest.
# The files that were replaced come back in one step, as the switch's core class went in: each
# old file is first given its name beside the path (PATH.sf-old, a hard link), then all of them
# are renamed over their paths by one process that starts no other program in between (the
# sf-rename-commit the switch kept beside the journal, else zsh's own mv, python3 or perl; plain
# mv when --root is not the running system). libSystem and
# libdyld must change together or nothing starts.
# The kernel is not this script's: `sf-bootbin-install.sh <PARTUUID> --rollback` from Linux.
set -u
NAME=sf-rename; DRY=0; ROOT=/
while [ $# -gt 0 ]; do
  case "$1" in
    --dry-run) DRY=1;;
    --root) ROOT=$2; shift;;
    --name) NAME=$2; shift;;
    -h|--help) sed -n '2,/^set -u/p' "$0" | sed '$d' | cut -c3-; exit 0;;
    *) echo "sf_rename_restore: unknown argument $1" >&2; exit 2;;
  esac
  shift
done
ROOT=${ROOT%/}
case "$NAME" in *[!A-Za-z0-9._-]*|"") echo "sf_rename_restore: --name: letters, digits, . _ - only" >&2; exit 2;; esac
DB=$ROOT/private/var/db/$NAME; OLD=$DB/old; NEW=$DB/new; J=$DB/journal
TAB=$(printf '\t')
say() { printf '%s\n' "$*"; }
die() { printf 'sf_rename_restore: %s\n' "$*" >&2; exit 1; }
[ -f "$J" ] || die "no journal at $J: nothing to restore"
TOOL=${SF_RENAME_TOOL:-$DB/sf-rename-commit}        # kept there by the switch
commit_method() {
  if [ -n "${SF_RENAME_COMMIT:-}" ]; then echo "$SF_RENAME_COMMIT"; return; fi   # for the tests: mv, tool, zsh, python3, perl
  if [ -n "$ROOT" ]; then echo mv; return; fi
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
COMMIT=$(commit_method)
say "replaced files come back together, renamed by $COMMIT"
[ "$COMMIT" != none ] || die "the running system offers no way to rename the files in one step (sf-rename-commit beside the journal, zsh/files, python3, perl); restore from Linux on the mounted partition. Nothing was changed"
# the journal's lines, last first (no tac on Darwin)
REV=$(mktemp "${TMPDIR:-/tmp}/sf-rename-rev.XXXXXX") || die "mktemp"
awk '{ l[NR] = $0 } END { for (i = NR; i > 0; i--) print l[i] }' "$J" > "$REV"
present() { [ -e "$1" ] || [ -L "$1" ]; }
n=0; bad=0
# ---- checks -----------------------------------------------------------------------------------
while IFS=$TAB read -r what path arg; do
  case "$what" in
    installed)
      [ "$arg" = replaced ] && present "$ROOT$path.sf-old" && { say "back $path (replaced; its old file waits beside it from an interrupted restore)"; n=$((n + 1)); continue; }
      [ "$arg" = replaced ] && ! present "$OLD$path" && present "$ROOT$path" && {
        # journalled, but the old file was not kept yet: the switch stopped right after the journal line,
        # and the new file only goes in once the old one is kept, so the path still holds the old file
        say "back $path (was journalled, not yet replaced: left as it is)"; continue; }
      if [ "$arg" = replaced ] && ! present "$OLD$path"; then say "BAD  the old $path is not in old/ and not at its path"; bad=$((bad + 1)); continue; fi
      say "back $path ($arg)";;
    removed)
      if present "$OLD$path"; then say "back $path (removed)"
      elif present "$ROOT$path"; then say "back $path (was journalled, not yet moved: left as it is)"
      else say "BAD  the old $path is not in old/"; bad=$((bad + 1)); fi;;
    migrated) say "take $path (the copy of $arg) to new/";;
    rmdir) say "back $path (a directory that was taken away)";;
    madedir) say "take $path away again (a directory the switch made), if nothing else is in it";;
    *) say "BAD  journal line: $what $path"; bad=$((bad + 1));;
  esac
  n=$((n + 1))
done < "$REV"
[ "$bad" = 0 ] || { rm -f "$REV"; die "$bad journal entries cannot be undone; nothing was changed"; }
if [ "$DRY" = 1 ]; then rm -f "$REV"; say "dry run: $n journal entries would be undone; nothing was changed"; exit 0; fi
# ---- the restore ------------------------------------------------------------------------------
aside() { # the file now at PATH goes to new/PATH
  present "$ROOT$1" || return 0
  mkdir -p "$NEW$(dirname "$1")" && { rm -rf "$NEW$1"; mv "$ROOT$1" "$NEW$1"; }
}
keep_new() { # the file now at PATH gets a second name, new/PATH; the path itself stays as it is
  mkdir -p "$NEW$(dirname "$1")" || return 1
  rm -rf "$NEW$1"
  if [ -L "$ROOT$1" ]; then cp -P "$ROOT$1" "$NEW$1"; else ln "$ROOT$1" "$NEW$1" 2>/dev/null || cp -p "$ROOT$1" "$NEW$1"; fi
}
: > "$DB/commit.restore" || die "cannot write $DB/commit.restore"
while IFS=$TAB read -r what path arg; do
  case "$what" in
    installed)
      rm -f "$ROOT$path.sf-new"
      if [ "$arg" = new ]; then aside "$path" || die "cannot take $path out"
      elif present "$ROOT$path.sf-old"; then      # an earlier restore stopped before its last step
        grep -q "^$ROOT$path.sf-old$TAB" "$DB/commit.restore" || printf '%s\t%s\n' "$ROOT$path.sf-old" "$ROOT$path" >> "$DB/commit.restore"
      elif present "$OLD$path"; then
        # the path is never left empty: the file in place is kept under new/, then the old one is
        # renamed over it (when both names are the same file - the switch stopped before the new
        # file went in - the second name in old/ is simply dropped)
        if [ "$ROOT$path" -ef "$OLD$path" ]; then rm -f "$OLD$path"
        else
          if present "$ROOT$path"; then keep_new "$path" || die "cannot keep the new $path"; fi
          rm -f "$ROOT$path.sf-old"
          mv "$OLD$path" "$ROOT$path.sf-old" || die "cannot bring the old $path beside its path (it is in old/)"
          printf '%s\t%s\n' "$ROOT$path.sf-old" "$ROOT$path" >> "$DB/commit.restore"
        fi
      fi;;
    removed)
      if present "$OLD$path"; then mkdir -p "$(dirname "$ROOT$path")" && mv "$OLD$path" "$ROOT$path" || die "cannot put $path back (it is in old/)"; fi;;
    migrated) aside "$path" || die "cannot take $path out";;
    rmdir) mkdir -p "$ROOT$path" && chmod 755 "$ROOT$path" || die "cannot make the directory $path again";;
    madedir) # only directories are left in it when the switch made it; anything else keeps it
      [ -d "$ROOT$path" ] && find "$ROOT$path" -depth -type d -exec rmdir {} \; 2>/dev/null;;
  esac
done < "$REV"
rm -f "$REV"
if [ -s "$DB/commit.restore" ]; then
  sync
  [ "${SF_RENAME_TEST_STOP:-0}" = 1 ] && die "stopped before the last step (test hook): the old files wait beside their paths"
  commit_renames "$DB/commit.restore" || die "the old files did not all go back (see $DB/commit.restore: FROM is still beside its path for those that did not; rename each FROM over its TO)"
fi
rm -f "$DB/commit.restore"
k=1; while [ -e "$J.restored.$k" ]; do k=$((k + 1)); done
mv "$J" "$J.restored.$k" || die "cannot rename the journal"
# old/ holds nothing any more: its empty directories go
find "$OLD" -depth -type d -exec rmdir {} \; 2>/dev/null
sync
say "restored: $n journal entries undone; the renamed files are in $NEW"
# the halt program must be the running kernel's: it finds the write fence by a sysctl name
if [ -z "$ROOT" ] && sysctl -n kern.sf_nvme.state > /dev/null 2>&1 && [ -x "$NEW/sbin/sf-halt" ]; then
  say "NOTE: the renamed kernel is running and /sbin/sf-halt is the old program again, which does not find this kernel's write fence."
  say "      Do not use reboot/halt now. Restart with: $NEW/sbin/sf-halt restart"
fi
