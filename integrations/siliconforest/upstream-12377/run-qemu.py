#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Headless disposable PCIe/xHCI/storage rehearsal; never contacts the M1."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess,time

here=Path(__file__).resolve().parent;repo=here.parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',required=True,type=Path)
parser.add_argument('--image-recipe',type=Path,default=here/'build-qemu-image.nix')
parser.add_argument('--guest-script',type=Path,default=here/'qemu-guest.sh')
a=parser.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
nix=str(Path.home()/'.nix-profile/bin/nix-build')
def run(argv,name,timeout=1200,env=None):
    with (out/name).open('wb') as f:
        result=subprocess.run(argv,cwd=repo,stdout=f,stderr=subprocess.STDOUT,timeout=timeout,env=env)
    if result.returncode:raise RuntimeError(name+' rc='+str(result.returncode))
def build(file,name,extra=()):
    run([nix,'--extra-experimental-features','nix-command flakes',str(file),'--no-out-link',*extra],name,1800)
    paths=[Path(s) for s in (out/name).read_text().splitlines() if s.startswith('/nix/store/') and Path(s).is_dir()]
    assert paths,name
    return paths[-1]
image=build(a.image_recipe.resolve(),'image-build.log')
io=build(here.parent/'m1-ext4-eof/build-io-test.nix','io-build.log')
usb=build(here/'build-usb-read.nix','usb-build.log')
utility=build(here/'build-ext4util.nix','utility-build.log')
frozen_utility=build(here/'build-ext4util.nix','frozen-utility-build.log',('--arg','fixed','false'))
ioreg=build(here/'build-ioreg.nix','ioreg-build.log')
fs=out/'scratch.ext4';disk=out/'scratch.img';usb_disk=out/'usb-fixture.img'
with fs.open('wb') as f:f.truncate(64<<20)
run(['mke2fs','-q','-t','ext4','-b','4096','-I','256','-L','upstreamscratch','-O','metadata_csum,64bit,metadata_csum_seed,^orphan_file',str(fs)],'mkfs.log')
run(['python3',str(here.parent/'m1-storage/tools/dr_qemu_disk.py'),str(fs),str(disk)],'disk.log')
with usb_disk.open('wb') as f:
    f.write(b'SF_UPSTREAM_USB_FIXTURE_v1'.ljust(4096,b'\0'))
    for start in range(4096,8<<20,4096):f.write(bytes((i*37+(i>>8)*19+13)&255 for i in range(start,start+4096)))
env=os.environ.copy();env['QD_BASE_IMAGE']=str(next(image.glob('*.img')))
system=repo/'runs/qemu-dev/upstream-12377'/(out.name+'.img')
system.parent.mkdir(parents=True,exist_ok=True)
assert not system.exists(),'Do not overwrite another run image'
run(['python3',str(here.parent/'qemu-dev/qd_image.py'),'--output',str(system),'--kernel','debug','--bootargs',' -s sfqemu_eof=1 sf_upstream_rehearsal=1',
    '--file',str(a.guest_script.resolve())+'=/usr/bin/upstream-guest',
    '--file',str(io/'bin/eof-io')+'=/usr/bin/eof-io',
    '--file',str(usb/'bin/usb-read')+'=/usr/bin/usb-read',
    '--file',str(ioreg/'usr/sbin/ioreg')+'=/usr/bin/ioreg',
    '--file',str(utility/'System/Library/Filesystems/ext4.fs/Contents/Resources/ext4.util')+'=/usr/bin/upstream-ext4.util',
    '--file',str(frozen_utility/'System/Library/Filesystems/ext4.fs/Contents/Resources/ext4.util')+'=/usr/bin/frozen-ext4.util'], 'stage.log',env=env)
run(['python3',str(here/'qemu-topology.py'),'--image',str(system),'--out',str(out/'guest'),
    '--command','sh /usr/bin/upstream-guest','--up','QDDESK_END','--shot-every','0','--timeout','600',
    '--qemu-arg=-drive','--qemu-arg=if=none,id=data,file='+str(disk)+',format=raw',
    '--qemu-arg=-device','--qemu-arg=virtio-blk-pci,drive=data,logical_block_size=4096,physical_block_size=4096',
    '--qemu-arg=-drive','--qemu-arg=if=none,id=usbdata,file='+str(usb_disk)+',format=raw,readonly=on',
    '--qemu-arg=-device','--qemu-arg=usb-storage,drive=usbdata,bus=xhci.0'], 'run.log',timeout=720)
serial=(out/'guest/serial.log').read_bytes()
assert b'USB_READ PASS bytes=8388608 chunk=524288' in serial
assert b'EOF_IO PASS cases=28 resize_steps=7' in serial
assert b'UPSTREAM_QEMU io=0 unmount=0 usb=0 utility=253 frozen_utility=254' in serial and b'QDDESK_PASS' in serial
assert b'RavynXHCIPort' in serial and b'IOPCI2PCIBridge' in serial
layout=re.search(r'first=(\d+) blocks=(\d+)',(out/'disk.log').read_text());assert layout
first,blocks=map(int,layout.groups());after=out/'after.ext4'
with disk.open('rb') as f:f.seek(first*4096);after.write_bytes(f.read(blocks*4096))
run(['e2fsck','-fn','-E','no_optimize_extents',str(after)],'fsck.log')
rows=re.findall(rb'EOF_IO FILE ([mw]-\d+|resized) size=(\d+)',serial);assert len(rows)==29
proof=[]
for rawname,rawsize in rows:
    name=rawname.decode();size=int(rawsize);dest=out/(name+'.bin')
    run(['debugfs','-R','dump /'+name+' '+str(dest),str(after)],name+'-dump.log')
    data=dest.read_bytes();assert data==bytes((i*37+(i>>8)*19+13)&255 for i in range(size))
    proof.append({'name':name,'bytes':size,'sha256':hashlib.sha256(data).hexdigest()})
result={'status':'PASS','image':str(image),'usb_bytes':8<<20,'usb_chunk':512<<10,'eof_cases':28,'resize_steps':7,'fsck_rc':0,'files':proof,
    'limits':'Dedicated emulated root-port/xHCI/media only. No M1, dock, DMA/DART, physical HID or DP proof.'}
(out/'summary.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
