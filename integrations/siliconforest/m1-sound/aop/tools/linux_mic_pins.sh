#!/bin/bash
# SPDX-License-Identifier: MIT
# Nothing is recorded and no setting is changed: the M1's own Linux is asked whether its
# microphones' PDM data lines move while the capture device is open. The capture device is
# opened (which makes the Linux driver switch the microphones on, as the permitted
# `arecord --dump-hw-params` does) and held for about two seconds WITHOUT setting hardware
# parameters or starting a stream: no sample is transferred. Meanwhile the AOP GPIO block's
# pin registers are read through the pinctrl driver's debugfs register dump, about 60
# times: pins 30/31 (PDM clocks) and 36/37 (PDM data), bit 0 is the pin's level.
# RESULT: INCONCLUSIVE, do not rely on it. That register dump is the pinctrl driver's
# cache from the time it probed, so it shows the same words whether the microphones are
# on or off (run once, November 11: all pins unchanged while the device was open). The
# question it was meant to answer (do the data lines move under Linux?) is still open and
# needs either a real recording by a person or a read of the live registers.
#   BENCH_WHO=sound tools/bench.sh run integrations/siliconforest/m1-sound/aop/tools/linux_mic_pins.sh
exec ssh -o HostKeyAlias=10.1.1.127 -o BatchMode=yes -o ConnectTimeout=15 chrisk@10.1.1.211 '
sudo -n python3 - <<PY
import os, time, collections
REG = "/sys/kernel/debug/regmap/24a820000.pinctrl/registers"
NUB = "/sys/kernel/debug/regmap/23d1f0000.pinctrl/registers"
PINS = [0, 1, 22, 26, 30, 31, 36, 37, 38]
def sample(path=REG):
    out = {}
    for line in open(path):
        a, v = line.split(":")
        out[int(a, 16) // 4] = int(v, 16)
    return out
def show(tag, n):
    seen = collections.defaultdict(collections.Counter)
    for _ in range(n):
        s = sample()
        for p in PINS:
            seen[p][s[p]] += 1
    print(tag, " ".join("pin%d:%s" % (p, ",".join("%x*%d" % kv for kv in sorted(seen[p].items()))) for p in PINS))
    print(tag, "nub pin0 %x" % sample(NUB)[0])
show("closed  ", 20)
dev = [d for d in os.listdir("/dev/snd") if d.startswith("pcmC0D0c")]
print("device", dev, open("/proc/asound/card0/id").read().strip())
t = time.time()
fd = os.open("/dev/snd/" + dev[0], os.O_RDONLY | os.O_NONBLOCK)
print("opened in %.3f s; status:" % (time.time() - t), open("/proc/asound/card0/pcm0c/sub0/status").read().split()[:4])
time.sleep(0.3)
show("open    ", 60)
os.close(fd)
time.sleep(0.5)
show("closed 2", 20)
print("status:", open("/proc/asound/card0/pcm0c/sub0/status").read().strip())
PY
sudo -n dmesg | grep -iE "aop|SFMDev|snd_soc" | tail -6
'
