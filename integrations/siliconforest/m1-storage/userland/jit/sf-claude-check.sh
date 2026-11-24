# SPDX-License-Identifier: MIT
# The Claude Code CLI on a running SiliconForest (the M1's disk root), run over SSH:
#   tools/sf_userland_push.sh ssh sh -s < userland/jit/sf-claude-check.sh
# The vendor's file as it is (its SHA-256 is printed), with whatever /usr/lib/libSystem.B.dylib the
# system has: `--version`, `doctor`, `-p` without credentials, the first screen on a terminal (tmux)
# and the screen after one Enter; node's JIT; the test programs of this directory when they are in
# /private/tmp/jit; then the useless copy claude.sf of boot dr45 is removed. Nothing is signed in
# and nothing typed but that Enter. docs/roadmap/issues/SF-X-claude-code.md.
export PATH=/usr/local/bin:/opt/homebrew/bin:/opt/homebrew/sbin:/bin:/usr/bin:/sbin:/usr/sbin HOME=/var/root LANG=en_US.UTF-8 TERM=xterm-256color
cd /var/root; D=/usr/local/lib/claude-code/2.1.322
echo "CC libSystem $(sha256sum /usr/lib/libSystem.B.dylib | cut -c1-64) $(ls /usr/lib | grep 'libSystem.B.dylib.' | tr '\n' ' ')"
echo "CC claude -> $(readlink /usr/local/bin/claude) sha256 $(sha256sum $D/claude | cut -c1-64)"
env | grep -i -c 'anthropic\|api_key\|token' | sed 's/^/CC env lines naming a key or token: /'
run() { n=$1; shift; "$@" > /private/tmp/cc.out 2> /private/tmp/cc.err < /dev/null & p=$!; ( sleep 40; kill -9 $p 2>/dev/null ) & w=$!; wait $p; rc=$?; kill $w 2>/dev/null; echo "CC $n rc=$rc out=[$(head -c "${CUT:-300}" /private/tmp/cc.out | tr '\n' '|')] err=[$(tail -c 400 /private/tmp/cc.err | tr '\n' '|')]"; }
run version claude --version
CUT=1500 run doctor claude doctor
run print-no-credentials claude -p hello
tmux kill-server 2>/dev/null
tmux new-session -d -s c -x 100 -y 30 "claude 2> /private/tmp/tui.err; echo EXIT=\$?; sleep 120"
sleep 20; echo "CC tui first screen:"; tmux capture-pane -p -t c | sed 's/[[:space:]]*$//' | grep -v '^$' | head -n 24 | cut -c1-100 | sed 's/^/CC | /'
tmux send-keys -t c Enter; sleep 8; echo "CC tui after one Enter:"; tmux capture-pane -p -t c | sed 's/[[:space:]]*$//' | grep -v '^$' | head -n 24 | cut -c1-100 | sed 's/^/CC | /'
echo "CC tui stderr=[$(tail -c 300 /private/tmp/tui.err | tr '\n' '|')]"
tmux kill-server 2>/dev/null
run node-jit node -e 'let s=0; for (let i=0;i<3e7;i++) s+=i%7; console.log(s, process.version)'
for t in /private/tmp/jit/sf-jit-*; do [ -x "$t" ] && CUT=700 run "$(basename "$t")" "$t"; done
if [ "$(readlink /usr/local/bin/claude)" = ../lib/claude-code/2.1.322/claude ]; then rm -f $D/claude.sf; fi
ls -la $D | awk '{print "CC dir", $1, $5, $NF}'
f=$(ls -t /var/root/.claude/debug/* 2>/dev/null | head -n 1); [ -n "$f" ] && grep -i 'keychain\|cert' "$f" | tail -n 4 | cut -c1-220 | sed 's/^/CC log /'
sync; df -k / | tail -n 1 | sed 's/^/CC /'
