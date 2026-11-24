#!/bin/sh
# SPDX-License-Identifier: MIT
# Every program the userland push installed, run once on the running system, with the
# environment the desktop's terminal gives its shell. Sent over SSH:
#   tools/sf_userland_push.sh ssh sh -s < userland/sf-userland-check.sh
# One line per program: UL_STEP <name> rc=<exit status>: <last lines of its output>.
step() {  # <name> <seconds> <command...>
    name=$1; secs=$2; shift 2
    "$@" > /private/tmp/step.out 2>&1 < /dev/null &
    pid=$!
    ( sleep "$secs"; echo "UL_TIMEOUT $name"; kill -9 $pid ) > /private/tmp/step.to 2>&1 &
    w=$!
    wait $pid; rc=$?
    kill $w 2>/dev/null; wait $w 2>/dev/null
    printf 'UL_STEP %s rc=%s: %s %s\n' "$name" "$rc" "$(cat /private/tmp/step.to 2>/dev/null)" "$(tail -n "${TAILN:-3}" /private/tmp/step.out | tr '\n' '|' | cut -c1-"${CUT:-230}")"
}
E="env -i PATH=/usr/local/bin:/opt/homebrew/bin:/opt/homebrew/sbin:/bin:/usr/bin:/sbin:/usr/sbin HOME=/var/root LANG=en_US.UTF-8 TERM=xterm-256color"
cd /private/tmp
step brew-version 300 $E brew --version
step brew-list 600 $E sh -c 'brew list --versions | wc -l'
step bash 60 $E bash -c 'echo ${BASH_VERSION} $((6*7))'
step sh-is 30 $E sh -c 'echo ${ZSH_VERSION:-not-zsh} ${BASH_VERSION:-not-bash}'
step git 240 $E sh -c 'rm -rf g && mkdir g && cd g && git init -q && echo a > f && git add f && git -c user.name=t -c user.email=t@example.invalid commit -q -m one && git log --oneline | wc -l && git --version'
step curl 60 $E curl --version
step curl-https 120 $E sh -c 'curl -sS -o /private/tmp/c.out -w "%{http_code} %{size_download}" https://formulae.brew.sh/api/formula/tree.json'
step less 30 $E less --version
step nano 30 $E nano --version
step grep 30 $E sh -c 'printf "a1\nb2\n" | ggrep -o "b[0-9]"'
step gsed 30 $E sh -c 'echo hello | gsed -E "s/(ll)/[\1]/"'
step gawk 30 $E sh -c 'echo "1 2" | gawk "{print \$1+\$2}"'
step coreutils 30 $E sh -c 'gls -d / && gdate -u -d @86400 +%F && printf abc | gsha256sum | cut -c1-8'
step find 30 $E sh -c 'gfind /etc/ssh -name sshd_config'
step tmux 90 $E sh -c 'tmux -V && tmux new-session -d -s t "sleep 20" && tmux list-sessions | cut -c1-20; tmux kill-server'
step python 300 $E python3 -c 'import ssl, sqlite3, json, hashlib, sys; print(sys.version_info[:2], ssl.OPENSSL_VERSION.split()[0], hashlib.sha256(b"abc").hexdigest()[:8])'
step node 600 $E node -e 'console.log([1,2,3].map(x=>x*2).join(), process.arch, require("crypto").createHash("sha256").update("abc").digest("hex").slice(0,8))'
step npm 600 $E npm --version
step jq 30 $E sh -c 'echo "{\"a\":[1,2]}" | jq -c .a'
step rg 30 $E sh -c 'echo needle > n.txt && rg -n needle n.txt'
step tree 30 $E tree --version
step htop 30 $E htop --version
step wget 30 $E wget --version
step fzf 30 $E sh -c 'printf "apple\nbanana\n" | fzf -f nan'
step bat 60 $E bat --version
step fd 30 $E fd --version
step make 30 $E sh -c 'gmake --version | head -n 1; make --version | head -n 1'
step xz 30 $E sh -c 'echo hello-xz | xz | xz -d'
step zstd 60 $E sh -c 'echo hello-zstd | zstd -q | zstd -dq'
step sqlite 60 $E sqlite3 :memory: 'select 6*7, sqlite_version();'
step gh 120 $E gh --version
step shasum 30 $E sh -c 'printf abc | shasum -a 256 | cut -c1-8'
step claude-version 600 $E claude --version
step ssh-client 20 $E ssh -V
step npm-ls 60 $E npm ls -g --depth=0
step brew-config 120 $E brew config
TAILN=6 step brew-install 240 $E sh -c 'brew install lua 2>&1 | tail -n 4; lua -v'
df -k / | tail -n 1 | awk '{print "UL_DF kbytes=" $2, "used=" $3, "free=" $4}'
rm -f /private/tmp/step.out /private/tmp/step.to
