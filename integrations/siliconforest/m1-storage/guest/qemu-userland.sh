#!/bin/sh
# SPDX-License-Identifier: MIT
# QEMU rehearsal of an installed system's userland (tools/dr_qemu_userland.sh): the M1's root
# image as the read-write root, userland/sf-userland-install.sh from /usr/share/sf-userland as
# a bench boot's RAM root would carry it, the `sys` part unpacked from the stream
# tools/sf_userland_push.sh sends, /opt/homebrew from a second disk (poured on the host: this
# machine's ext4 writes far too slowly under emulation to unpack it here), then the SSH server
# and client over the loopback and every installed program once. The login key of this
# rehearsal is made here and exists only in this throw-away image.
PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH
echo QDDESK_BEGIN
phase=$(cat /etc/dr-phase 2>/dev/null); q() { [ "$phase" != quick ]; }   # quick: the install, the SSH server and a handful of programs
step() {  # <name> <seconds> <command...>: output tail, exit status, a time limit
    name=$1; secs=$2; shift 2
    "$@" > /private/tmp/step.out 2>&1 < /dev/null &
    pid=$!
    ( sleep "$secs"; echo "UL_TIMEOUT $name"; kill -9 $pid ) > /private/tmp/step.to 2>&1 &
    w=$!
    wait $pid; rc=$?
    kill $w 2>/dev/null; wait $w 2>/dev/null
    printf 'UL_STEP %s rc=%s: %s %s\n' "$name" "$rc" "$(cat /private/tmp/step.to 2>/dev/null)" "$(tail -n "${TAILN:-3}" /private/tmp/step.out | tr '\n' '|' | cut -c1-"${CUT:-230}")"
}
mount 2>&1 | grep ' on / ' | sed 's/^/UL_MOUNT /'
sh /usr/share/sf-userland/sf-userland-install.sh /usr/share/sf-userland / 2>&1 | sed 's/^/UL_INSTALL1 /'
sh /usr/share/sf-userland/sf-userland-install.sh /usr/share/sf-userland / 2>&1 | grep 'hostkey\|done\|wallpaper\|authorized' | sed 's/^/UL_INSTALL2 /'
ls -l /bin/sh /bin/bash /private/var/select/sh 2>&1 | awk '{print "UL_SH", $1, $(NF-2), $(NF-1), $NF}'
tail -n 2 /etc/passwd | sed 's/^/UL_PASSWD /'; tail -n 1 /etc/profile /etc/zshenv | sed 's/^/UL_PROFILE /'
ls -ld /var/root /var/root/.ssh /var/root/.ssh/authorized_keys /var/empty /Users/sf /opt/homebrew 2>&1 | awk '{print "UL_LS", $1, $3, $4, $NF}'
# ---- the sys part, as the push unpacks it
if [ -r /usr/share/sf-test/sys.tar.gz ]; then   # (quick: the host put the files there; this machine writes about 0.2 MB/s)
    t0=$(date +%s); gunzip < /usr/share/sf-test/sys.tar.gz | tar -x -p -C / -f - 2>&1 | tail -n 5; echo "UL_SYS tar rc=$? seconds=$(( $(date +%s) - t0 ))"
fi
n=0; bad=0; while read -r want f; do n=$((n + 1)); [ "$(sha256sum "/$f" | cut -c1-64)" = "$want" ] || { bad=$((bad + 1)); echo "UL_SYS differs: $f"; }; done < /usr/share/sf-test/sys.sha256
echo "UL_SYS files=$n different=$bad"
ls -l /usr/bin/curl /bin/ssh /System/Library/Frameworks/Security.framework/Security /System/Library/Frameworks/Foundation.framework/Versions/C/Foundation 2>&1 | awk '{print "UL_SYS", $1, $3, $4, $5, $(NF-2), $(NF-1), $NF}'
# ---- /opt/homebrew
mounted=no
for d in /dev/disk0s1 /dev/disk1s1 /dev/disk2s1 /dev/disk3s1; do
    [ -b "$d" ] || continue
    if mount -t ext4 "$d" /opt/homebrew 2>/dev/null; then
        if [ -d /opt/homebrew/Cellar ]; then mounted=$d; break; fi
        umount /opt/homebrew
    fi
done
echo "UL_PREFIX disk=$mounted $(ls -ld /opt/homebrew | awk '{print $1, $3, $4}') cellar=$(ls /opt/homebrew/Cellar 2>/dev/null | wc -l)"
# ---- the network of this machine (single-user: nothing configured it), then the loopback
sf-dhcp --up en0 > /dev/null 2>&1; sf-net-up en0 > /dev/null 2>&1; echo "UL_NET up rc=$?"
ping -c 1 127.0.0.1 > /dev/null 2>&1; echo "UL_NET loopback ping rc=$?"
# ---- the SSH server as launchd would run it, and the client against it
( sh /usr/libexec/sf-sshd > /var/log/sshd.log 2>&1 & )
sleep 8; head -n 4 /var/log/sshd.log | sed 's/^/UL_SSHD /'
step ssh-version 30 ssh -V
ssh-keygen -q -t ed25519 -N '' -C rehearsal -f /private/tmp/rehearsal-key; cat /private/tmp/rehearsal-key.pub >> /var/root/.ssh/authorized_keys
printf '127.0.0.1 %s\n' "$(awk '{print $1, $2}' /etc/ssh/ssh_host_ed25519_key.pub)" > /private/tmp/known
O="-o UserKnownHostsFile=/private/tmp/known -o GlobalKnownHostsFile=/dev/null -o StrictHostKeyChecking=yes -o BatchMode=yes -o ConnectTimeout=20"
TAILN=6 CUT=600 step ssh-login 120 ssh $O -i /private/tmp/rehearsal-key root@127.0.0.1 'uname -a; id; echo "PATH=$PATH LANG=$LANG HOME=$HOME"; brew --version'
step ssh-no-key 60 ssh $O -o PubkeyAuthentication=no -o PreferredAuthentications=password,keyboard-interactive root@127.0.0.1 true
step ssh-other-user 60 ssh $O -i /private/tmp/rehearsal-key sf@127.0.0.1 true
printf '127.0.0.1 ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGZha2VrZXlmYWtla2V5ZmFrZWtleWZha2VrZXlmYWtl\n' > /private/tmp/known-wrong
step ssh-wrong-hostkey 60 ssh -o UserKnownHostsFile=/private/tmp/known-wrong -o GlobalKnownHostsFile=/dev/null -o StrictHostKeyChecking=yes -o BatchMode=yes -i /private/tmp/rehearsal-key root@127.0.0.1 true
echo rehearsal > /private/tmp/scp-src; step scp 120 scp $O -i /private/tmp/rehearsal-key /private/tmp/scp-src root@127.0.0.1:/private/tmp/scp-dst; cmp -s /private/tmp/scp-src /private/tmp/scp-dst; echo "UL_SCP equal rc=$?"
step scp-legacy 120 scp -O $O -i /private/tmp/rehearsal-key /private/tmp/scp-src root@127.0.0.1:/private/tmp/scp-dst2; cmp -s /private/tmp/scp-src /private/tmp/scp-dst2; echo "UL_SCP legacy protocol equal rc=$?"
TAILN=12 CUT=900 step sftp-server-alone 20 sh -c '/libexec/sftp-server -h; echo rc=$?; printf "" | /libexec/sftp-server -e; echo rc=$?'
printf 'ls /etc/ssh\n' > /private/tmp/sftp-batch; TAILN=14 CUT=1400 step sftp 120 sftp -v $O -i /private/tmp/rehearsal-key -b /private/tmp/sftp-batch root@127.0.0.1
tail -n 16 /var/log/sshd.log | cut -c1-200 | sed 's/^/UL_SSHDLOG /'
# ---- every installed program once, with the terminal's environment
E="env -i PATH=/usr/local/bin:/opt/homebrew/bin:/opt/homebrew/sbin:/bin:/usr/bin:/sbin:/usr/sbin HOME=/var/root LANG=en_US.UTF-8 TERM=xterm-256color"
cd /private/tmp
step brew-version 300 $E brew --version
q && step brew-list 600 $E sh -c 'brew list --versions | wc -l'
step bash 60 $E bash -c 'echo ${BASH_VERSION} $((6*7))'
step sh-is 30 $E sh -c 'echo ${ZSH_VERSION:-not-zsh} ${BASH_VERSION:-not-bash}'
step git 240 $E sh -c 'rm -rf g && mkdir g && cd g && git init -q && echo a > f && git add f && git -c user.name=t -c user.email=t@example.invalid commit -q -m one && git log --oneline | wc -l && git --version'
step curl 60 $E curl --version
q && step curl-https 120 $E sh -c 'curl -sS -o /private/tmp/c.out -w "%{http_code} %{size_download}" https://formulae.brew.sh/api/formula/tree.json'
q && step less 30 $E less --version
q && step nano 30 $E nano --version
q && step grep 30 $E sh -c 'printf "a1\nb2\n" | ggrep -o "b[0-9]"'
q && step gsed 30 $E sh -c 'echo hello | gsed -E "s/(ll)/[\1]/"'
q && step gawk 30 $E sh -c 'echo "1 2" | gawk "{print \$1+\$2}"'
q && step coreutils 30 $E sh -c 'gls -d / && gdate -u -d @86400 +%F && printf abc | gsha256sum | cut -c1-8'
q && step find 30 $E sh -c 'gfind /etc/ssh -name sshd_config'
step tmux 90 $E sh -c 'tmux -V && tmux new-session -d -s t "sleep 20" && tmux list-sessions | cut -c1-20; tmux kill-server'
step python 300 $E python3 -c 'import ssl, sqlite3, json, hashlib, sys; print(sys.version_info[:2], ssl.OPENSSL_VERSION.split()[0], hashlib.sha256(b"abc").hexdigest()[:8])'
step node 600 $E node -e 'console.log([1,2,3].map(x=>x*2).join(), process.arch, require("crypto").createHash("sha256").update("abc").digest("hex").slice(0,8))'
q && step npm 600 $E npm --version
q && step jq 30 $E sh -c 'echo "{\"a\":[1,2]}" | jq -c .a'
q && step rg 30 $E sh -c 'echo needle > n.txt && rg -n needle n.txt'
q && step tree 30 $E tree --version
q && step htop 30 $E htop --version
q && step wget 30 $E wget --version
q && step fzf 30 $E sh -c 'printf "apple\nbanana\n" | fzf -f nan'
q && step bat 60 $E bat --version
q && step fd 30 $E fd --version
q && step make 30 $E sh -c 'gmake --version | head -n 1; make --version | head -n 1'
q && step xz 30 $E sh -c 'echo hello-xz | xz | xz -d'
q && step zstd 60 $E sh -c 'echo hello-zstd | zstd -q | zstd -dq'
q && step sqlite 60 $E sqlite3 :memory: 'select 6*7, sqlite_version();'
q && step gh 120 $E gh --version
step shasum 30 $E sh -c 'printf abc | shasum -a 256 | cut -c1-8'
step claude-version 600 $E claude --version
if [ "$phase" = tui ]; then
    # the first screen on a real PTY (tmux is the terminal), nothing typed into it but one Enter
    $E tmux new-session -d -s c -x 100 -y 30 "claude; echo EXIT-STATUS=\$?; sleep 600"
    sleep 150; $E tmux capture-pane -p -t c | sed 's/[[:space:]]*$//' | grep -v '^$' | head -n 30 | cut -c1-110 | sed -n l | sed 's/^/UL_CLAUDE_SCREEN1 /'
    $E tmux send-keys -t c Enter; sleep 60; $E tmux capture-pane -p -t c | sed 's/[[:space:]]*$//' | grep -v '^$' | head -n 30 | cut -c1-110 | sed -n l | sed 's/^/UL_CLAUDE_SCREEN2 /'
    $E tmux kill-server
fi
pkill sshd 2>/dev/null
sync; umount /opt/homebrew 2>/dev/null
echo QDDESK_PASS
echo QDDESK_END
