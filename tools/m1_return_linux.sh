#!/bin/bash
# Bring the M1 back to verified Linux after chained boots (tools/pcie_iterate.sh PCIE_CHAIN=1)
# left it in the m1n1 proxy: one external reset, then a new Linux boot checked against the
# chain's baseline, by the bring-up controller's --return-only mode on the M5. With nothing
# pending it only checks that Linux is up. The bench keeper runs this by itself (tools/bench.py);
# by hand: tools/bench.sh run tools/m1_return_linux.sh
set -euo pipefail
ROOT=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)
W=/Users/chriskearney/darwin-lab-firstboot-007
SSH="ssh -o HostKeyAlias=10.1.1.93 -o BatchMode=yes"
if [ -z "${M5_HOST:-}" ]; then
  M5_HOST=10.1.1.70
  # the controller answers on the dock Ethernet (.127), or on Wi-Fi as .70 or .93, whichever it has
  for h in 10.1.1.55 10.1.1.93 10.1.1.70 10.1.1.127; do $SSH -o ConnectTimeout=2 chriskearney@$h true 2>/dev/null && M5_HOST=$h; done
fi
M5=chriskearney@$M5_HOST
tar -C "$ROOT/runs/m1-desktop-010/pcie-m2-host-001" --exclude=__pycache__ -cf - lab | $SSH $M5 "sudo -n sh -c 'rm -rf $W/lab-bench-return && mkdir -m 700 $W/lab-bench-return && tar -xf - -C $W/lab-bench-return --strip-components 1 && chown -R root:staff $W/lab-bench-return'"
OUT=$($SSH -o ServerAliveInterval=15 $M5 "sudo -n sh -c 'cd $W/lab-bench-return && exec caffeinate -dimsu $W/venv/bin/python -B tools/m1_bringup_cycle.py --execute --confirm-recovery J293_RECOVERY_READY --return-only --private-root $W/pcie-m2-cycles'" | tail -1) || true
echo "$OUT"
case "$OUT" in *'"linux_verified": true'*) exit 0;; esac
exit 1
