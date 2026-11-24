#!/bin/bash
# SPDX-License-Identifier: MIT
# Read-only: is the M1's lid open? (The built-in microphones of Apple silicon notebooks are
# disconnected in hardware while the lid is closed.) Asks the M1's own Linux for its lid
# switch state. Run through the bench queue:
#   BENCH_WHO=sound tools/bench.sh run integrations/siliconforest/m1-sound/aop/tools/linux_lid.sh
exec ssh -o HostKeyAlias=10.1.1.127 -o BatchMode=yes -o ConnectTimeout=15 chrisk@10.1.1.211 '
echo "== input devices with a switch"; awk "BEGIN{RS=\"\"} /SW=/ {print; print \"--\"}" /proc/bus/input/devices
echo "== lid switch state (EVIOCGSW bit 0: 1 = closed)"
sudo -n python3 - <<PY
import fcntl, glob, os, array
EVIOCGSW = lambda n: (2 << 30) | (n << 16) | (ord("E") << 8) | 0x1b
EVIOCGBIT = lambda ev, n: (2 << 30) | (n << 16) | (ord("E") << 8) | (0x20 + ev)
for path in sorted(glob.glob("/dev/input/event*")):
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
    except OSError as e:
        print(path, e); continue
    try:
        caps = array.array("B", [0] * 8); fcntl.ioctl(fd, EVIOCGBIT(5, 8), caps)
        if caps[0] & 1:
            sw = array.array("B", [0] * 8); fcntl.ioctl(fd, EVIOCGSW(8), sw)
            name = open("/sys/class/input/%s/device/name" % os.path.basename(path)).read().strip()
            print(path, name, "SW_LID =", sw[0] & 1, "(closed)" if sw[0] & 1 else "(open)")
    except OSError as e:
        print(path, e)
    os.close(fd)
PY
echo "== dmesg lid"; sudo -n dmesg | grep -iE "lid|macsmc-hid|smc.*input" | head -10
echo "== backlight"; cat /sys/class/backlight/*/actual_brightness 2>/dev/null; cat /sys/class/backlight/*/bl_power 2>/dev/null
echo "== aop syslog since boot"; sudo -n dmesg | grep -i "SFMDev\|aop_audio\|snd_soc_aop\|Unable" | tail -12
echo "== capture pcm status now"; cat /proc/asound/card0/pcm0c/sub0/status
'
