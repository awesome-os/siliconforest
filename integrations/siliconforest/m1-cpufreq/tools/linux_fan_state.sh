#!/bin/bash
# SPDX-License-Identifier: MIT
# What the M1's own Linux sees of the fan, read-only (sysfs of macsmc_hwmon): speed,
# target, limits, the driver's fan_control parameter, load and uptime. One line per call;
# with a count and an interval, that many lines.
#   linux_fan_state.sh [label] [count, default 1] [seconds between, default 0]
# The mode key F0Md is not exposed by Linux: a fan that keeps a speed and target while the
# machine idles cool is in manual mode, one at 0 rpm / target 0 is the SMC's.
set -uo pipefail
LABEL=${1:-now}; COUNT=${2:-1}; GAP=${3:-0}
ssh -o HostKeyAlias=10.1.1.127 -o BatchMode=yes -o ConnectTimeout=10 chrisk@10.1.1.211 "
  h=\$(grep -l macsmc_hwmon /sys/class/hwmon/hwmon*/name | head -1 | xargs dirname)
  i=0
  while [ \$i -lt $COUNT ]; do
    [ \$i -gt 0 ] && sleep $GAP
    t=''
    for n in 1 2 3 4; do t=\"\$t \$(cat \$h/temp\${n}_label | tr ' ' '_')=\$(cat \$h/temp\${n}_input)\"; done
    echo \"LINUX_FAN label=$LABEL sample=\$i epoch=\$(date +%s) uptime_s=\$(cut -d. -f1 /proc/uptime) fan_rpm=\$(cat \$h/fan1_input) target_rpm=\$(cat \$h/fan1_target) min=\$(cat \$h/fan1_min) max=\$(cat \$h/fan1_max) fan_control_param=\$(cat /sys/module/macsmc_hwmon/parameters/fan_control 2>/dev/null || echo none) system_uW=\$(cat \$h/power1_input) load=\$(cut -d' ' -f1 /proc/loadavg)\$t\"
    i=\$((i+1))
  done
" 2>&1
