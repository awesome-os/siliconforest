# SPDX-License-Identifier: MIT
# Guest side of the `brew` client experiment (hb_qemu.py --extra-script): the real Homebrew
# client from the prefix disk, run as a normal user, pouring bottles it downloads itself.
# What this sets up, and why, is the list of things SiliconForest lacks for brew today:
#   /bin/bash            brew is a bash script; the image has none. Linked to the bash bottle.
#   /usr/bin/sw_vers     brew calls it by that path; the image has /bin/sw_vers.
#   /dev/fd              bash's <(...) needs it; tried with the kernel's fdesc filesystem.
#   a user account       brew refuses to run as root. uid 1000 "brew", home /tmp/brewhome.
#   HOMEBREW_GIT_PATH    no /usr/bin/git in the image; the git bottle.
#   /usr/bin/curl        from the overlay (the tree's curl).
[ -e /bin/bash ] || ln -s /opt/homebrew/bin/bash /bin/bash
[ -e /usr/bin/sw_vers ] || ln -s /bin/sw_vers /usr/bin/sw_vers
[ -e /dev/fd/0 ] || echo "BREW fdesc mount: $(mount -t fdesc -o union stdin /dev 2>&1; echo rc=$?)"
echo "BREW /dev/fd: $(ls /dev/fd 2>&1 | head -5 | tr '\n' ' ')"
grep -q '^brew:' /etc/passwd || echo 'brew:*:1000:1000:Homebrew:/tmp/brewhome:/bin/sh' >> /etc/passwd
grep -q '^brew:' /etc/master.passwd || echo 'brew:*:1000:1000::0:0:Homebrew:/tmp/brewhome:/bin/sh' >> /etc/master.passwd
grep -q '^brew:' /etc/group || echo 'brew:*:1000:brew' >> /etc/group
mkdir -p /tmp/brewhome && chown 1000:1000 /tmp/brewhome
cat > /tmp/brew-user.sh <<'EOS'
export HOME=/tmp/brewhome USER=brew LOGNAME=brew TERM=xterm-256color LC_ALL=en_US.UTF-8
export PATH=/opt/homebrew/bin:/opt/homebrew/sbin:/bin:/sbin:/usr/bin:/usr/sbin
export HOMEBREW_NO_AUTO_UPDATE=1 HOMEBREW_NO_ANALYTICS=1 HOMEBREW_NO_ENV_HINTS=1 HOMEBREW_NO_INSTALL_CLEANUP=1
export HOMEBREW_GIT_PATH=/opt/homebrew/bin/git
R=/opt/homebrew/Library/Homebrew/vendor/portable-ruby/current/bin/ruby
# step NAME SECONDS COMMAND...: output tail, exit status, and a time limit
step() {
  name=$1; secs=$2; shift 2
  echo "BREW_STEP $name"
  "$@" > $HOME/step.out 2>&1 &
  pid=$!
  ( sleep $secs; echo "BREW_TIMEOUT $name"; kill -9 $pid ) &
  w=$!
  wait $pid; rc=$?
  kill $w 2>/dev/null
  tail -${TAILN:-25} $HOME/step.out; echo "BREW_RC $name $rc"
}
id
step ruby-hello 120 $R -e 'puts RUBY_VERSION, RUBY_PLATFORM'
step ruby-stdlib 240 $R -rjson -rdigest -ropenssl -retc -e 'puts OpenSSL::OPENSSL_VERSION, Etc.getpwuid.name, Digest::SHA256.hexdigest("abc")[0,8]'
step version 120 brew --version
TAILN=45 step config 900 brew config
TAILN=40 step install-tree 1500 brew install tree
step run-tree 60 /opt/homebrew/bin/tree --version
TAILN=40 step install-jq 1500 brew install jq
step run-jq 60 sh -c 'echo "{\"a\":[1,2]}" | /opt/homebrew/bin/jq -c .a'
step list 600 brew list --versions
EOS
hb-runas 1000 1000 /bin/sh /tmp/brew-user.sh
