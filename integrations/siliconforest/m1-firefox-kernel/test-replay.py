#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Replay real recordings through the staged core, with hosted OS cache hooks."""
from pathlib import Path
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

here=Path(__file__).resolve().parent
repo=here.parents[2]
source=Path(sys.argv[1]).resolve()/'src/Kernel/Extensions/SFAppleAVD'
records=[Path(p).resolve() for p in sys.argv[2:]]
if not records or any(not p.is_file() for p in records):
    raise SystemExit('usage: test-replay.py prepared-tree recording.pdaj [...] (all inputs required)')
linux=repo/'sources/gpu-plan/linux-aurora'
assert subprocess.check_output(['git','-C',str(linux),'rev-parse','HEAD'],text=True).strip()=='6a08a08e57558083f552733a5276d71b1a4d8ff6'
files={**{'kernel/linux/'+f:'drivers/media/platform/apple/avd/'+f for f in ['avd.h','avd-inst.h','avd-regs.h','avd-hw.c','avd-h264.c','avd-hevc.c','avd-vp9.c']},
       **{'kernel/linux/'+f:'drivers/media/v4l2-core/'+f for f in ['v4l2-h264.c','v4l2-vp9.c']},
       **{'kernel/shim/media/'+f:'include/media/'+f for f in ['v4l2-h264.h','v4l2-vp9.h']},
       'include/sfavd/v4l2-controls.h':'include/uapi/linux/v4l2-controls.h'}
for own,upstream in files.items():
    assert (source/own).read_bytes()==subprocess.check_output(['git','-C',str(linux),'show','HEAD:'+upstream]),own
print('AVD_FROZEN_REPLAY all12 Linux identity files PASS',flush=True)
cc=os.environ.get('CC','clang')
flags=['-std=gnu11','-O1','-g','-Wall','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-fno-sanitize-recover=all']
with tempfile.TemporaryDirectory(prefix='sf-avd-replay-') as tmp:
    tmp=Path(tmp);avd=tmp/'avd';shutil.copytree(source,avd)
    shutil.copytree(here.parent/'m1-avd/host-test',avd/'host-test')
    (avd/'user').mkdir();shutil.copy2(here.parent/'m1-avd/user/sfavd_rec.h',avd/'user/sfavd_rec.h')
    # This hosted hook records API ordering only, never claims a physical CMO.
    hook=tmp/'sync.c';hook.write_text('''#include "sf_avd_os.h"
#include <assert.h>
int sf_avd_os_dma_sync(void *cookie, enum sf_avd_dma_phase phase) {
 assert(cookie && phase>=SF_AVD_DMA_INITIAL && phase<=SF_AVD_DMA_PUBLISH_CPU); return 0;
}
''')
    objects=[]
    for name in ['sf_avd_core.c','linux/avd-hw.c','linux/avd-h264.c','linux/avd-hevc.c','linux/avd-vp9.c','linux/v4l2-h264.c','linux/v4l2-vp9.c']:
        obj=tmp/(Path(name).stem+'.o');objects.append(str(obj))
        subprocess.run([cc,*flags,'-I'+str(avd/'kernel'),'-I'+str(avd/'kernel/shim'),'-I'+str(avd/'kernel/linux'),'-c',str(avd/'kernel'/name),'-o',str(obj)],check=True)
    binary=tmp/'replay'
    subprocess.run([cc,*flags,'-I'+str(avd/'kernel'),'-I'+str(avd/'include'),str(avd/'host-test/avd_host.c'),str(avd/'host-test/host_os.c'),str(hook),*objects,'-o',str(binary)],check=True)
    for i,record in enumerate(records):
        a,b=tmp/(str(i)+'.a'),tmp/(str(i)+'.b')
        output=subprocess.check_output([str(binary),str(record),str(a)],stderr=subprocess.PIPE,text=True)
        subprocess.run([str(binary),str(record),str(b)],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,check=True)
        assert a.read_bytes()==b.read_bytes(),record
        print('AVD_FROZEN_REPLAY PASS '+record.name+' '+output.strip()+' sha256='+hashlib.sha256(a.read_bytes()).hexdigest(),flush=True)
print('AVD_FROZEN_REPLAY PASS '+str(len(records))+' recordings (host only; no output-pixel/DMA proof)',flush=True)
