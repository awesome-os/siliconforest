# SPDX-License-Identifier: MIT
# Upstream's OpenSSH for arm64 (nix/pkgs/base/openssh.nix at the tip: OpenSSL and zlib linked
# in, no PAM, no Kerberos; programs in /bin, /sbin and /libexec, configuration in /etc/ssh).
# Homebrew's openssh bottle cannot be loaded here: ssh links Kerberos.framework, sshd libpam
# and libsandbox. userland/sf-userland-install.sh takes the programs, not upstream's
# sshd_config (password logins for root) nor its launchd job.
((import ../../rebase/pd.nix).pd).packages.x86_64-linux.openssh-arm64
