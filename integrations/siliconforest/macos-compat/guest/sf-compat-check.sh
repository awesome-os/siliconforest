#!/bin/sh
# SPDX-License-Identifier: MIT
# The programs the macOS-compatibility additions are for, run and checked on a SiliconForest
# system: in the QEMU rehearsal (qemu-compat.sh) and on the M1 over SSH
# (tools/sf_compat_push.sh check). Nothing here installs or changes the system: files are
# written under /private/tmp/sfcompat only, tmux runs on a socket of its own (-L sfcompat),
# HOME is a directory there.
#   COMPAT_STEP <name> rc=..: out=[..] err=[..]     one program
#   COMPAT_VERDICT ok|FAIL <what>                   one check
PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH
T=/usr/share/sf-test; W=/private/tmp/sfcompat; rm -rf $W; mkdir -p $W/home; cd $W
[ -x $T/sf-compat-probe ] || T=/private/tmp/sf-test
E="env -i PATH=/usr/local/bin:/opt/homebrew/bin:/opt/homebrew/sbin:/bin:/usr/bin:/sbin:/usr/sbin HOME=$W/home LANG=en_US.UTF-8 TERM=xterm-256color"
step() {  # <name> <seconds> <command...>
    name=$1; secs=$2; shift 2
    "$@" > $W/step.out 2> $W/step.err < /dev/null &
    pid=$!
    ( sleep "$secs"; echo "COMPAT_TIMEOUT $name"; kill -9 $pid ) > $W/step.to 2>&1 &
    w=$!
    wait $pid; rc=$?
    kill $w 2>/dev/null; wait $w 2>/dev/null
    printf 'COMPAT_STEP %s rc=%s: %s out=[%s] err=[%s]\n' "$name" "$rc" "$(cat $W/step.to 2>/dev/null)" \
        "$(head -c "${CUT:-400}" $W/step.out | tr '\n' '|')" "$(tail -c "${CUT:-400}" $W/step.err | tr '\n' '|')"
}
bad=0; ok() { if eval "$2"; then echo "COMPAT_VERDICT ok   $1"; else echo "COMPAT_VERDICT FAIL $1"; bad=$((bad + 1)); fi; }
CUT=1500 step probe 120 $T/sf-compat-probe
ok "the CoreServices text-encoding calls work (probe)" '[ "$rc" = 0 ] && grep -q "^SFCOMPAT ok" $W/step.out'
[ ! -x $T/sf-appkit-probe ] || { CUT=1500 step appkit-probe 300 $E $T/sf-appkit-probe; ok "AppKit loads without a display; NSPasteboard answers" '[ "$rc" = 0 ] && grep -q "^SFAPPKIT ok" $W/step.out'; }

edit() {  # <program> <file>: start it on a terminal (tmux), type a line, write, quit
    $E tmux -L sfcompat new-session -d -s ed -x 100 -y 30 "$1 $2 2> $W/ed.err; echo EXIT-STATUS=\$?; sleep 600"
    sleep "${ED_SECS:-45}"
    $E tmux -L sfcompat send-keys -t ed 'i' 'typed on a terminal in '"$1" Escape
    sleep 8
    $E tmux -L sfcompat capture-pane -p -t ed | sed 's/[[:space:]]*$//' | grep -v '^$' | head -n 6 | cut -c1-100 | sed "s/^/COMPAT_SCREEN $1 /"
    $E tmux -L sfcompat send-keys -t ed ':wq' Enter
    sleep "${ED_SECS:-45}"
    $E tmux -L sfcompat capture-pane -p -t ed | sed 's/[[:space:]]*$//' | grep -v '^$' | tail -n 3 | cut -c1-100 | sed "s/^/COMPAT_SCREEN_END $1 /"
    $E tmux -L sfcompat kill-server
    echo "COMPAT_ED $1 stderr=[$(tail -c 300 $W/ed.err | tr '\n' '|')] file=[$(head -c 200 "$2" 2>/dev/null | tr '\n' '|')]"
}
# ---- neovim
CUT=300 step nvim-version 300 $E nvim --version
ok "nvim starts" '[ "$rc" = 0 ] && grep -q "^NVIM v" $W/step.out'
step nvim-headless 300 $E nvim --headless -u NONE -c 'call setline(1, "written by nvim " . strftime("%Y"))' -c 'wq' $W/n1.txt
ok "nvim --headless writes a file" '[ "$rc" = 0 ] && grep -q "^written by nvim 20" $W/n1.txt'
edit nvim $W/n2.txt
ok "nvim on a terminal: typed text is in the file it wrote" 'grep -q "^typed on a terminal in nvim$" $W/n2.txt'
# ---- vim
CUT=300 step vim-version 300 $E vim --version
ok "vim starts" '[ "$rc" = 0 ] && grep -q "^VIM - Vi IMproved" $W/step.out'
step vim-ex 300 $E vim -u NONE -es -c 'call setline(1, "written by vim " . strftime("%Y"))' -c 'wq' $W/v1.txt
ok "vim -es writes a file" '[ "$rc" = 0 ] && grep -q "^written by vim 20" $W/v1.txt'
edit vim $W/v2.txt
ok "vim on a terminal: typed text is in the file it wrote" 'grep -q "^typed on a terminal in vim$" $W/v2.txt'
CUT=600 step vim-features 300 $E vim -u NONE -es -c 'redir! > '"$W/vf.txt"'' -c 'echo "perl=" . has("perl") . " clipboard=" . has("clipboard") . " lang=" . v:lang . " enc=" . &encoding' -c 'perl VIM::Msg("perl says " . (6*7) . " version " . $^V)' -c 'let @* = "to the pasteboard"' -c 'echo "star=[" . @* . "]"' -c 'redir END' -c 'q'
echo "COMPAT_VIM features: $(tr '\n' ' ' < $W/vf.txt 2>/dev/null)"
ok "vim: :perl runs Perl code in the system's libperl" 'grep -q "perl says 42 version v5.34" $W/vf.txt'
# ---- the system Perl
CUT=300 step perl-version 300 $E /usr/bin/perl -MConfig -e 'print "perl $^V on $^O, $Config{archname}, threads=", ($Config{useithreads} || "no"), "\n"'
ok "/usr/bin/perl is Perl 5.34.1 for darwin-thread-multi-2level" '[ "$rc" = 0 ] && grep -q "^perl v5.34.1 on darwin, darwin-thread-multi-2level, threads=define" $W/step.out'
CUT=300 step perl-modules 300 $E /usr/bin/perl -MPOSIX -MData::Dumper -MList::Util=sum -MDigest::SHA=sha256_hex -MEncode -MSocket -MTime::HiRes=time -Mthreads -MConfig -e 'my $t = threads->create(sub { sum(1..10) }); open(my $p, "-|", "echo piped") or die; my $l = <$p>; chomp $l; close $p; my $pid = fork(); if (!$pid) { exit 7 } waitpid($pid, 0); print "sum=", $t->join, " sha=", substr(sha256_hex("abc"), 0, 8), " floor=", POSIX::floor(2.7), " utf8=", length(Encode::encode("UTF-8", "\x{e9}")), " pipe=$l child=", $? >> 8, " info=", scalar(grep { $_ eq "INFO" } split(/ /, $Config{sig_name})), " hires=", (time() > 1e9 ? 1 : 0), "\n"'
ok "perl: XS modules load (POSIX, List::Util, Digest::SHA, Encode, Socket, Time::HiRes), threads, fork, pipe" '[ "$rc" = 0 ] && grep -q "^sum=55 sha=ba7816bf floor=2 utf8=2 pipe=piped child=7 info=1 hires=1" $W/step.out'
printf '#!/usr/bin/perl\nuse strict; use warnings;\nmy %%h = (a => 1, b => 2);\nprint join(",", map { "$_=$h{$_}" } sort keys %%h), "\\n";\n' > $W/s.pl; chmod 755 $W/s.pl
step perl-script 120 $E $W/s.pl
ok "a #!/usr/bin/perl script runs" '[ "$rc" = 0 ] && grep -q "^a=1,b=2$" $W/step.out'
# ---- /usr/lib/libexpat and libncurses through the programs that link them
if [ -x /opt/homebrew/bin/python3 ]; then
    CUT=300 step python-expat 300 $E python3 -c 'import pyexpat, plistlib, xml.etree.ElementTree as E; print(E.fromstring("<a b=\"7\"/>").get("b"), plistlib.loads(plistlib.dumps({"k": [1]}))["k"][0], pyexpat.EXPAT_VERSION)'
    ok "python: pyexpat, ElementTree and plistlib parse XML (/usr/lib/libexpat.1.dylib)" '[ "$rc" = 0 ] && grep -q "^7 1 expat_" $W/step.out'
    cat > $W/cu.py <<EOF
import curses, time
s = curses.initscr(); curses.start_color()
s.addstr(2, 3, "curses on SiliconForest", curses.A_BOLD); s.refresh(); time.sleep(${ED_SECS:-45} / 3 + 2)
h, w = s.getmaxyx(); curses.endwin()
open("$W/cu.txt", "w").write("%dx%d colors=%d\n" % (w, h, curses.COLORS))
EOF
    # (the root's terminfo database has xterm-256color only, so not tmux's own TERM)
    $E tmux -L sfcompat new-session -d -s cu -x 80 -y 24 "TERM=xterm-256color python3 $W/cu.py 2> $W/cu.err; sleep 600"
    sleep "${ED_SECS:-45}"; $E tmux -L sfcompat capture-pane -p -t cu | grep -v '^ *$' | head -n 3 | cut -c1-80 | sed 's/^/COMPAT_SCREEN curses /'
    sleep "${ED_SECS:-45}"; $E tmux -L sfcompat kill-server
    echo "COMPAT_CURSES file=[$(tr '\n' ' ' < $W/cu.txt 2>/dev/null)] stderr=[$(tail -c 300 $W/cu.err | tr '\n' '|')]"
    ok "python: the curses module draws on a terminal (/usr/lib/libncurses.5.4.dylib, the NCURSES60 names)" 'grep -q "^80x24 colors=256" $W/cu.txt'
fi
if [ -x /opt/homebrew/opt/git/libexec/git-core/git-http-push ]; then
    step git-http-push 120 $E /opt/homebrew/opt/git/libexec/git-core/git-http-push
    ok "git-http-push loads (it prints its usage)" 'grep -qi "usage: git http-push" $W/step.err $W/step.out'
fi
for p in sf-expat-probe sf-curses-probe sf-strxfrm-probe; do
    [ -x $T/$p ] || continue
    CUT=600 step $p 120 $E $T/$p
    ok "$p" '[ "$rc" = 0 ]'
done
echo "COMPAT_CHECK failed=$bad"
