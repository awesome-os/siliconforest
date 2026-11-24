#!/bin/sh
# SPDX-License-Identifier: MIT
# What an installed SiliconForest needs for daily use besides the desktop: the SSH server and
# client, the account Homebrew runs as, the places Homebrew and a person's own programs live
# in. Runs inside the booted system (the files come from the RAM
# root image of a bench boot, or from a directory filled over SSH; the root is written through
# the fenced driver) or on a host against an unpacked root. Idempotent: a second run changes
# nothing that is already right, and never touches a host key or a line a person added.
#   sf-userland-install.sh <source directory> <root directory>
# The source directory holds
#   ssh/bin/{ssh,scp,sftp,ssh-keygen,ssh-add,ssh-agent,ssh-keyscan}  ssh/sbin/sshd
#   ssh/libexec/{sshd-session,sshd-auth,sftp-server}  ssh/etc/{moduli,ssh_config}
#                         upstream's OpenSSH for this system (any of them may be missing: a
#                         bench boot's RAM root carries the server's four, the rest follow)
#   sshd_config sf-sshd org.siliconforest.sshd.plist brew sf-environment shasum   (this directory's)
#   authorized_key.pub    ONE public key that may log in as root (added if it is not there)
#   chonk-terminal-darwin (optional) the terminal, replaced when it differs
# The big things (the system libraries macOS programs expect, /opt/homebrew, the Claude Code
# CLI) are not this script's: tools/sf_userland_push.sh sends them over SSH.
set -u
S=${1:?source directory}; R=${2:?root directory}; R=${R%/}
say() { echo "SF_USERLAND $*"; }
[ -x "$R/sbin/launchd" ] && [ -d "$R/System" ] && [ -d "$R/private" ] || { say "not a SiliconForest root: $R"; exit 2; }
for f in sshd_config sf-sshd org.siliconforest.sshd.plist brew sf-environment shasum; do [ -r "$S/$f" ] || { say "missing $S/$f"; exit 2; }; done
rc=0
put() {  # <source> <destination> <mode>: by way of a new name, so a running program's file is replaced, never rewritten
    if cmp -s "$S/$1" "$R/$2"; then chmod "$3" "$R/$2"; return 0; fi
    cp "$S/$1" "$R/$2.sf-new" && chmod "$3" "$R/$2.sf-new" && mv -f "$R/$2.sf-new" "$R/$2" || { say "FAILED $2"; rc=1; }
}
# /bin/sh is a launcher: it runs what /private/var/select/sh names, else /bin/bash, else /bin/zsh.
# /bin/bash is about to exist (Homebrew's): the system's own scripts must stay on the shell they run on now.
mkdir -p "$R/private/var/select"
[ -L "$R/private/var/select/sh" ] || ln -s /bin/zsh "$R/private/var/select/sh"
say "sh is $(readlink "$R/private/var/select/sh")"
mkdir -p "$R/etc/ssh" "$R/libexec" "$R/usr/libexec" "$R/usr/local/bin" "$R/var/empty" "$R/var/log" "$R/var/root" "$R/Users/sf" "$R/opt/homebrew"
chmod 755 "$R/var/empty"; chmod 700 "$R/var/root"
n=0
for f in bin/ssh bin/scp bin/sftp bin/ssh-keygen bin/ssh-add bin/ssh-agent bin/ssh-keyscan sbin/sshd libexec/sshd-session libexec/sshd-auth libexec/sftp-server; do
    [ -r "$S/ssh/$f" ] && { put "ssh/$f" "$f" 755; n=$((n + 1)); }
done
[ -r "$S/ssh/etc/moduli" ] && put ssh/etc/moduli etc/ssh/moduli 644
[ -r "$S/ssh/etc/ssh_config" ] && [ ! -e "$R/etc/ssh/ssh_config" ] && put ssh/etc/ssh_config etc/ssh/ssh_config 644
say "ssh programs put or already there: $n"
# The host key is made on the machine itself, here when this runs in the booted system (else by
# sf-sshd at its first start), and never leaves it. Its public half is printed: that is what a
# person compares before trusting the first connection.
if [ -z "$R" ] && [ -x /bin/ssh-keygen ]; then
    K=/etc/ssh/ssh_host_ed25519_key
    if [ ! -s "$K" ] || [ ! -s "$K.pub" ]; then
        rm -f "$K" "$K.pub" < /dev/null
        if /bin/ssh-keygen -q -t ed25519 -N '' -C siliconforest -f "$K" && chmod 600 "$K" && chmod 644 "$K.pub"; then say "host key made on this machine"; else say "FAILED host key"; rc=1; fi
    fi
    say "hostkey $(awk '{print $1, $2; exit}' "$K.pub" 2>/dev/null)"
    say "hostkey fingerprint $(/bin/ssh-keygen -l -f "$K.pub" 2>&1)"
fi
put sshd_config etc/ssh/sshd_config 644
put sf-sshd usr/libexec/sf-sshd 755
put org.siliconforest.sshd.plist System/Library/LaunchDaemons/org.siliconforest.sshd.plist 644
put brew usr/local/bin/brew 755
put sf-environment etc/sf-environment 644
put shasum usr/bin/shasum 755
[ -e "$R/usr/bin/sw_vers" ] || [ -L "$R/usr/bin/sw_vers" ] || [ ! -e "$R/bin/sw_vers" ] || ln -s /bin/sw_vers "$R/usr/bin/sw_vers"
[ -e "$R/bin/bash" ] || [ -L "$R/bin/bash" ] || ln -s /opt/homebrew/bin/bash "$R/bin/bash"
# two programs Homebrew keeps off the PATH under their usual names: GNU make is gmake there, and sqlite is
# "keg-only" (macOS has its own); the links dangle until the prefix is there
[ -e "$R/usr/local/bin/make" ] || [ -L "$R/usr/local/bin/make" ] || ln -s /opt/homebrew/bin/gmake "$R/usr/local/bin/make"
[ -e "$R/usr/local/bin/sqlite3" ] || [ -L "$R/usr/local/bin/sqlite3" ] || ln -s /opt/homebrew/opt/sqlite/bin/sqlite3 "$R/usr/local/bin/sqlite3"
chown 0:0 "$R/opt" "$R/usr/local" "$R/usr/local/bin" 2>/dev/null
# the account Homebrew runs as (no password, no login of its own), as the QEMU install has it
grep -q '^sf:' "$R/etc/passwd" || echo 'sf:*:501:20:SiliconForest User:/Users/sf:/bin/zsh' >> "$R/etc/passwd"
grep -q '^sf:' "$R/etc/master.passwd" || echo 'sf:*:501:20::0:0:SiliconForest User:/Users/sf:/bin/zsh' >> "$R/etc/master.passwd"
chown 501:20 "$R/Users/sf" "$R/opt/homebrew" || rc=1
# root's shell is zsh, as an account's on macOS: /bin/sh here is zsh in sh emulation, without line
# editing, and a terminal emulator starts the shell the user database names (login -pf root)
for f in etc/passwd etc/master.passwd; do
    if [ -e "$R/$f" ] && [ -x "$R/bin/zsh" ] && grep -q '^root:.*:/bin/sh$' "$R/$f"; then
        sed 's|^\(root:.*\):/bin/sh$|\1:/bin/zsh|' "$R/$f" > "$R/$f.sf-new" && chmod 644 "$R/$f.sf-new" && mv -f "$R/$f.sf-new" "$R/$f" || rc=1
    fi
done
# login shells read the environment file
for f in etc/profile etc/zshenv; do
    [ -e "$R/$f" ] && ! grep -q '/etc/sf-environment' "$R/$f" && echo '[ -r /etc/sf-environment ] && . /etc/sf-environment' >> "$R/$f"
done
# who may log in as root: public keys in root's own file; the one given here is added once
if [ -r "$S/authorized_key.pub" ]; then
    mkdir -p "$R/var/root/.ssh"; chmod 700 "$R/var/root/.ssh"
    A=$R/var/root/.ssh/authorized_keys
    key=$(awk 'NF >= 2 {print $2; exit}' "$S/authorized_key.pub")
    if [ -n "$key" ] && ! grep -q -F "$key" "$A" 2>/dev/null; then head -n 1 "$S/authorized_key.pub" >> "$A"; fi
    chmod 600 "$A"; chown 0:0 "$R/var/root" "$R/var/root/.ssh" "$A"
    say "authorized keys for root: $(grep -c . "$A")"
fi
if [ -r "$S/chonk-terminal-darwin" ]; then
    put chonk-terminal-darwin usr/bin/chonk-terminal-darwin 755
    say "terminal $(sha256sum "$R/usr/bin/chonk-terminal-darwin" | cut -c1-16)"
fi
# The desktop's stored choices (theme, display scale, wallpaper) are the person's own: reported, never changed.
# With nothing stored the desk wears the picture in /usr/share/backgrounds/siliconforest (the forest); it is
# the row "Siliconforest" of the desktop menu's Wallpaper list.
D=$R/private/var/db/desktop-state/chonkstep
say "desktop state left as it is: theme=$(cat "$D/theme" 2>/dev/null) scale=$(cat "$D/scale" 2>/dev/null) wallpaper=$(cat "$D/wallpaper" 2>/dev/null); forest picture $([ -r "$R/usr/share/backgrounds/siliconforest/siliconforest.png" ] && echo present || echo MISSING)"
say "done rc=$rc: $(cd "$R" && ls -l sbin/sshd libexec/sshd-session libexec/sshd-auth bin/ssh-keygen bin/ssh etc/ssh/sshd_config usr/libexec/sf-sshd System/Library/LaunchDaemons/org.siliconforest.sshd.plist usr/local/bin/brew 2>&1 | awk '{print $5 ":" $NF}' | tr '\n' ' ')"
exit $rc
