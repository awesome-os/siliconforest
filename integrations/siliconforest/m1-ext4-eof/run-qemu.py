#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Isolated QEMU scratch ext4 negative/positive controls; never contacts an M1."""
import argparse,hashlib,json,os,re,subprocess,time
from pathlib import Path
repo=Path(__file__).resolve().parents[3];here=Path(__file__).resolve().parent
nix=str(Path.home()/'.nix-profile/bin/nix-build')
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--phase',choices=['frozen','fixed','both'],default='both');p.add_argument('--fragmented',action='store_true',help='seed alternating free blocks on the disposable scratch filesystem');a=p.parse_args()
w=a.output.resolve();w.mkdir(parents=True,exist_ok=False)
def command(argv,name,timeout=900,env=None,allowed=(0,)):
 with (w/name).open('wb') as f:r=subprocess.run(argv,cwd=repo,stdout=f,stderr=subprocess.STDOUT,timeout=timeout,env=env)
 if r.returncode not in allowed:raise RuntimeError(name+' rc='+str(r.returncode))
 return r.returncode

def build(file,name,extra=()):
 command([nix,'--extra-experimental-features','nix-command flakes',str(file),'--no-out-link',*extra],name,timeout=1800)
 paths=[l for l in (w/name).read_text().splitlines() if l.startswith('/nix/store/') and Path(l).is_dir()]
 if not paths:raise RuntimeError('No built output in '+name)
 return Path(paths[-1])
tools=build(here/'build-io-test.nix','io-test-build.txt')
summary=[]
for phase in (['frozen','fixed'] if a.phase=='both' else [a.phase]):
 image=build(here/'build-qemu-image.nix',phase+'-image-build.txt',('--arg','fixed','true' if phase=='fixed' else 'false'))
 images=list(image.glob('*.img'));assert len(images)==1,images
 fs=w/(phase+'-fs.img');disk=w/(phase+'-disk.img')
 with fs.open('wb') as f:f.truncate(64<<20)
 command(['mke2fs','-q','-t','ext4','-b','4096','-I','256','-L','eofscratch','-O','metadata_csum,64bit,metadata_csum_seed,^orphan_file',str(fs)],phase+'-mkfs.txt')
 if a.fragmented:
  # Linux debugfs writes ONLY our fresh regular image, never a mounted/device FS.
  # Reserve unwritten blocks, then punch alternating holes to force extent nodes.
  commands=w/(phase+'-fragment-commands.txt')
  commands.write_text('write /dev/null /reserved\nfallocate /reserved 0 14000\n'+''.join('punch /reserved %d %d\n'%(n,n) for n in range(1,14000,2)))
  command(['debugfs','-w','-f',str(commands),str(fs)],phase+'-fragment.txt',timeout=120)
  command(['e2fsck','-fn',str(fs)],phase+'-fragment-precheck.txt')
 command(['python3',str(repo/'integrations/siliconforest/m1-storage/tools/dr_qemu_disk.py'),str(fs),str(disk)],phase+'-disk.txt')
 layout=re.search(r'first=(\d+) blocks=(\d+)',(w/(phase+'-disk.txt')).read_text());assert layout
 first,blocks=map(int,layout.groups());assert first==4102
 name='eof-'+phase+'-'+str(time.time_ns());env=os.environ.copy();env.update(QD_BASE_IMAGE=str(images[0]),DR_QEMU_SINGLE='1',DR_QEMU_KERNEL='debug',DR_QEMU_BOOTARGS=' -s sfqemu_eof=1',DR_QEMU_KEEP='1',DR_QEMU_FILES=str(tools/'bin/eof-io')+'=/usr/bin/eof-io')
 command([str(repo/'integrations/siliconforest/m1-storage/tools/dr_qemu.sh'),name,str(here/'qemu-guest.sh'),str(disk),'rw','600'],phase+'-run.txt',timeout=720,env=env)
 run=repo/'runs/m1-storage/qemu-dr'/name;serial=(run/'serial.log').read_bytes();(w/(phase+'-serial.log')).write_bytes(serial)
 assert b'EOF_IO ENV pagesize=16384 fsblock=4096' in serial
 assert b'EOF_IO PASS cases=28 resize_steps=7' in serial and b'EOF_GUEST result=0 unmount=0' in serial and b'QDDESK_PASS' in serial
 after=w/(phase+'-after.img')
 with disk.open('rb') as f:f.seek(first*4096);data=f.read(blocks*4096)
 assert len(data)==blocks*4096;after.write_bytes(data)
 rc=command(['e2fsck','-fn','-E','no_optimize_extents',str(after)],phase+'-fsck.txt',allowed=(0,4))
 fsck=(w/(phase+'-fsck.txt')).read_text()
 if phase=='frozen':
  # Identify THIS file: contiguous tails yield i_size errors; fragmented tails
  # yield individual out-of-bounds extents. Unrelated RC4 is not sufficient.
  command(['debugfs','-R','stat /m-52208',str(after)],phase+'-counterexample-stat.txt')
  stat=(w/(phase+'-counterexample-stat.txt')).read_text()
  ino=re.search(r'Inode:\s*(\d+)',stat);assert ino and re.search(r'Size:\s*52208\b',stat),stat
  mismatch=re.search(r'Inode '+ino[1]+r', i_size is 52208, should be 65536\.',fsck)
  extent=re.search(r'Inode '+ino[1]+r', end of extent exceeds allowed value\s*\(logical block (13|14|15),',fsck)
  assert rc==4 and (mismatch or extent),fsck
 else:
  assert rc==0,fsck
 rows=re.findall(rb'EOF_IO FILE ([mw]-\d+|resized) size=(\d+)',serial);assert len(rows)==29
 file_proof=[];metadata_cases=[]
 for rawname,rawsize in rows:
  name=rawname.decode();size=int(rawsize);dest=w/(phase+'-'+name+'.bin')
  command(['debugfs','-R','dump /'+name+' '+str(dest),str(after)],phase+'-'+name+'-dump.txt')
  content=dest.read_bytes();expected=bytes((i*37+(i>>8)*19+13)&255 for i in range(size));assert content==expected,(phase,name,len(content),size)
  if a.fragmented:
   stat_name=phase+'-'+name+'-stat.txt'
   command(['debugfs','-R','stat /'+name,str(after)],stat_name)
   stat=(w/stat_name).read_text();count=re.search(r'Blockcount:\s*(\d+)',stat);assert count,stat
   if '(ETB' in stat and int(count[1])>((size+4095)//4096)*8:metadata_cases.append(name)
  file_proof.append({'name':name,'bytes':size,'sha256':hashlib.sha256(content).hexdigest()})
 if a.fragmented:assert metadata_cases,'Fixture did not exercise accounted extent metadata'
 record={'phase':phase,'guest_and_clean_unmount':True,'e2fsck_fn_rc':rc,'files_exact':file_proof,'fragmented':a.fragmented,'accounted_extent_metadata_cases':metadata_cases,'qemu_run':str(run),'limits':'Isolated VM filesystem behavior only; no M1 runtime or power-loss proof'};summary.append(record)
 (w/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(phase,'PASS','fsck',rc,'files',len(rows),flush=True)
