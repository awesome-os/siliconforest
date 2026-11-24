#!/bin/bash
# SPDX-License-Identifier: MIT
# Read-only: what the M1's own Linux shows about the AOP's and the NUB's GPIO blocks and
# the power domains, for comparison with the pin and PMGR words SFAopAudio logs. Only
# debugfs and sysfs files are read.
# CAVEAT (found by using it): the regmap "registers" files of the pinctrl driver show its
# register CACHE, filled when the driver probed (before Linux started the AOP), not the
# pins' current words: they tell what the boot loader left, nothing about later changes.
# Run through the bench queue:
#   BENCH_WHO=sound tools/bench.sh run integrations/siliconforest/m1-sound/aop/tools/linux_pins.sh
exec ssh -o HostKeyAlias=10.1.1.127 -o BatchMode=yes -o ConnectTimeout=15 chrisk@10.1.1.211 '
sudo -n sh -c "
for c in 24a820000.pinctrl 23d1f0000.pinctrl; do
  d=/sys/kernel/debug/pinctrl/\$c
  echo == \$c; ls \$d 2>&1 | tr \"\\n\" \" \"; echo
  echo -- pinmux-pins; head -60 \$d/pinmux-pins 2>&1
  echo -- pinconf-pins; head -50 \$d/pinconf-pins 2>&1
done
echo == gpio; grep -A30 -E \"24a820000|23d1f0000\" /sys/kernel/debug/gpio 2>&1 | head -60
echo == genpd; grep -iE \"aop|pdm|leap|audio|sio|mca|nub\" /sys/kernel/debug/pm_genpd/pm_genpd_summary 2>&1 | head -40
echo == regmap; ls /sys/kernel/debug/regmap/ 2>&1 | grep -iE \"24a82|23d1f|pinctrl\" | head
for r in /sys/kernel/debug/regmap/*24a820000* /sys/kernel/debug/regmap/*23d1f0000*; do [ -e \$r/registers ] && { echo -- \$r; head -70 \$r/registers; }; done
echo == wireplumber sources; 
"
wpctl status 2>/dev/null | sed -n "/Audio/,/Video/p" | head -40
'
