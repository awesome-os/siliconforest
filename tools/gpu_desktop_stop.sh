#!/bin/bash
# End a running tools/gpu_desktop.sh session early. Interrupts the hypervisor worker on the M5;
# the bench controller then resets the M1 and verifies it is back in Linux (about a minute).
set -euo pipefail
M5=chriskearney@${M5_HOST:-10.1.1.70}; W=/Users/chriskearney/darwin-lab-firstboot-007
ssh -o HostKeyAlias=10.1.1.93 -o BatchMode=yes "$M5" "sudo -n sh -c 'cd $W/pcie-m2-cycles && d=\$(ls -td cycle-*/001 | head -1) && p=\$(python3 -c \"import json;print(json.load(open(\\\"\$d/guest-pid.json\\\"))[\\\"pid\\\"])\") && ps -p \$p >/dev/null && kill -INT \$p && echo interrupted worker \$p in \$d || echo no running session found'"
