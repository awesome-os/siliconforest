#!/bin/sh
# SPDX-License-Identifier: MIT
# FXNET: power-save sweep on the M1 (needs the SFBwfm of kernel 47+ and /private/var/db/sffxnet/bin/sf-wifipm).
# For each mode: set it, delayed-TCP-echo curve (M1 -> lab through ssh -R) and lab -> M1 ping at 1 s.
D=$(cd "$(dirname "$0")" && pwd)
SSH="ssh -o UserKnownHostsFile=$HOME/.ssh/known_hosts_m1_siliconforest -o HostKeyAlias=m1-siliconforest -o BatchMode=yes root@10.1.1.211"
run() {
  tag=$1; shift
  $SSH "/private/var/db/sffxnet/bin/sf-wifipm $*" 2>&1 | sed "s/^/$tag /"
  sleep 2
  timeout 220 $SSH -o ExitOnForwardFailure=yes -R 47812:127.0.0.1:47812 "/opt/homebrew/bin/python3 -I - probe 47812 $tag 0,20,100,150,250,400,700,1000,1500,2500 12 0.3" < $D/tcpdelay.py 2>&1
  echo "$tag lab->M1 ping 1s: $(ping -c 25 -i 1 10.1.1.211 | python3 $D/pstats.py)"
  echo "$tag M1->gw ping 0.2s: $($SSH 'ping -c 40 -i 0.2 10.1.1.1' | python3 $D/pstats.py)"
  echo "$tag lab->M1 ping 0.2s: $(ping -c 40 -i 0.2 10.1.1.211 | python3 $D/pstats.py)"
}
run PM2-200 fast 200
run PM2-2000 fast 2000
run CAM off
run PM2-2000b fast 2000
