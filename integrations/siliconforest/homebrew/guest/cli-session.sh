# SPDX-License-Identifier: MIT
# Guest side of the Claude Code / Codex CLI experiment (hb_qemu.py --extra-script).
# The vendor binaries are on the prefix disk under /opt/homebrew/.sf/ exactly as downloaded
# on the host (hb_pour.py --extra); nothing of the host's environment or home directory is
# in the guest. NO sign-in happens here: no account, key or token exists in the guest, and
# the script stops at each tool's first screen.
# Phases (CLI_PHASES, default "start tui"):
#   start   --version, --help, a non-interactive call without credentials
#   tui     each tool in a tmux pane (a real PTY): first screen, a key, a resize, screen dumps
#   install the vendor's installer script, as a user would run it (slow: the guest's ext4
#           driver writes about 0.2 MB/s under emulation and the binary is 324 MB)
PHASES="${CLI_PHASES:-start tui}"
[ -e /bin/bash ] || ln -s /opt/homebrew/bin/bash /bin/bash
[ -e /usr/bin/sw_vers ] || ln -s /bin/sw_vers /usr/bin/sw_vers
# macOS has /usr/bin/shasum (a Perl script); Anthropic's installer calls `shasum -a 256` on
# Darwin. The image has sha256sum only. This is that one mode, by the image's own tool.
if [ ! -e /usr/bin/shasum ]; then
  printf '%s\n' '#!/bin/sh' 'if [ "$1" = -a ] && [ "$2" = 256 ]; then shift 2; exec /bin/sha256sum "$@"; fi' 'echo "shasum: only -a 256 is provided here" >&2; exit 2' > /usr/bin/shasum
  chmod 755 /usr/bin/shasum
fi
grep -q '^user:' /etc/passwd || echo 'user:*:1000:1000:User:/opt/homebrew/home:/bin/sh' >> /etc/passwd
grep -q '^user:' /etc/master.passwd || echo 'user:*:1000:1000::0:0:User:/opt/homebrew/home:/bin/sh' >> /etc/master.passwd
grep -q '^user:' /etc/group || echo 'user:*:1000:user' >> /etc/group
mkdir -p /opt/homebrew/home && chown 1000:1000 /opt/homebrew/home
cat > /tmp/cli-user.sh <<'EOS'
export HOME=/opt/homebrew/home USER=user LOGNAME=user SHELL=/bin/bash TERM=xterm-256color LC_ALL=en_US.UTF-8
export PATH=$HOME/.local/bin:/opt/homebrew/bin:/opt/homebrew/sbin:/bin:/sbin:/usr/bin:/usr/sbin
V=/opt/homebrew/.sf
PHASES="$CLI_PHASES"
env | grep -i -c 'anthropic\|openai\|api_key\|token' | sed 's/^/CLI env lines naming a key or token: /'
step() {
  name=$1; secs=$2; shift 2
  echo "CLI_STEP $name"
  "$@" > $HOME/step.out 2>&1 < /dev/null &
  pid=$!
  ( sleep $secs; echo "CLI_TIMEOUT $name"; kill -9 $pid ) &
  w=$!
  wait $pid; rc=$?
  kill $w 2>/dev/null
  head -c 3000 $HOME/step.out | tail -${TAILN:-30}; echo; echo "CLI_RC $name $rc"
}
# One tool on a PTY: tmux is the terminal (TERM=tmux-256color inside, 100x30).
tui() {
  name=$1; secs=$2; shift 2
  echo "CLI_STEP tui-$name"
  tmux kill-server 2>/dev/null
  tmux new-session -d -s t -x 100 -y 30 "$*; echo; echo EXIT-STATUS=\$?; sleep 600"
  sleep $secs
  # The serial console is 7-bit: the screens also go to files (UTF-8 and attributes intact),
  # which the host reads from the disk image afterwards.
  mkdir -p $HOME/screens; tmux capture-pane -p -e -t t > $HOME/screens/$name-first.txt
  echo "CLI_SCREEN $name first"; tmux capture-pane -p -t t | sed 's/[[:space:]]*$//' | grep -v '^$' | head -45
  echo "CLI_SCREEN $name first-with-attributes (escape sequences as tmux reproduces them; first 12 lines)"
  tmux capture-pane -p -e -t t | grep -v '^$' | head -12 | sed -n l | cut -c1-300
  tmux send-keys -t t Down; sleep 8
  echo "CLI_SCREEN $name after-Down"; tmux capture-pane -p -t t | sed 's/[[:space:]]*$//' | grep -v '^$' | head -30
  tmux resize-window -t t -x 70 -y 24; sleep 10
  echo "CLI_SCREEN $name after-resize-70x24"; tmux capture-pane -p -t t | sed 's/[[:space:]]*$//' | grep -v '^$' | head -30
  tmux capture-pane -p -e -t t > $HOME/screens/$name-resized.txt
  if [ "${TUI_ENTER:-0}" = 1 ]; then
    # One Enter: accept the highlighted text style. The next screen is the sign-in choice; nothing is chosen there.
    tmux resize-window -t t -x 100 -y 30; sleep 5; tmux send-keys -t t Enter; sleep ${TUI_ENTER_WAIT:-60}
    tmux capture-pane -p -e -t t > $HOME/screens/$name-after-enter.txt
    echo "CLI_SCREEN $name after-Enter"; tmux capture-pane -p -t t | sed 's/[[:space:]]*$//' | grep -v '^$' | head -40
  fi
  echo "CLI_PANE $name: $(tmux display-message -p -t t '#{pane_width}x#{pane_height} dead=#{pane_dead} cmd=#{pane_current_command}')"
  tmux send-keys -t t C-c; sleep 3; tmux send-keys -t t C-c; sleep 5
  echo "CLI_SCREEN $name after-ctrl-c"; tmux capture-pane -p -t t | sed 's/[[:space:]]*$//' | grep -v '^$' | tail -6
  tmux kill-server 2>/dev/null
  echo "CLI_RC tui-$name 0"
}
case " $PHASES " in *" start "*)
  step claude-version 300 $V/claude --version
  TAILN=14 step claude-help 300 $V/claude --help
  TAILN=40 step claude-print-unauthenticated 900 $V/claude --debug -p hello
  echo "CLI helpers: $(for c in security open pbcopy pbpaste osascript sandbox-exec ps pgrep rg git bash zsh sh script stty tput locale defaults sw_vers uname; do if command -v $c >/dev/null 2>&1; then echo "$c=yes"; else echo "$c=NO"; fi; done | tr '\n' ' ')"
  echo "CLI tmpdir: TMPDIR=${TMPDIR:-unset} $(ls -ld /tmp /private/tmp /var/folders 2>&1 | tr '\n' ' ')"
  step codex-version 120 $V/codex/bin/codex --version
  step codex-rg 60 $V/codex/codex-path/rg --version
;; esac
case " $PHASES " in *" tui "*)
  mkdir -p $HOME/proj && cd $HOME/proj && git init -q . 2>/dev/null
  TUI_ENTER=1 tui claude 150 $V/claude
  ls -la $HOME/.claude 2>/dev/null | head -12; ls $HOME/.claude.json 2>/dev/null
  tui codex 40 $V/codex/bin/codex
;; esac
case " $PHASES " in *" install "*)
  TAILN=25 step claude-install-script 9000 bash $V/claude-install.sh
  ls -la $HOME/.local/bin 2>/dev/null
  step claude-installed-version 300 $HOME/.local/bin/claude --version
;; esac
EOS
CLI_PHASES="$PHASES" hb-runas 1000 1000 /bin/sh /tmp/cli-user.sh
