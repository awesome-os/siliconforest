"""Assemble the PCIe enumeration packet (M2). Offline; no transport or hardware."""
from pathlib import Path
import datetime,hashlib,json,shutil,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT/'artifacts/m1-desktop-010/apcie-port-candidate-003'
NAME=sys.argv[1] if len(sys.argv)>1 else 'pcie-enum-candidate-001'
OUT=ROOT/'artifacts/m1-desktop-010'/NAME
import os
# PCIE_LAB: a private copy of the launcher tree for this boot (tools/pcie_iterate.sh makes one,
# so nothing shared is written while other boots are queued or running).
LAB=Path(os.environ['PCIE_LAB']).resolve() if os.environ.get('PCIE_LAB') else ROOT/'runs/m1-desktop-010/pcie-m2-host-001/lab'
WIFI=os.environ.get('PCIE_WIFI')=='1'
KERNEL=(ROOT/('runs/m1-desktop-010/bwfm-kc-build-001/result/kernel' if WIFI else 'runs/m1-desktop-010/apcie-host-kc-build-001/result/kernel')).resolve()
# PCIE_KC=<collection directory>: that collection instead of the shared build link, which
# another boot being prepared at the same time may repoint
if os.environ.get('PCIE_KC'): KERNEL=(Path(os.environ['PCIE_KC'])/'kernel').resolve(strict=True)
SUPERVISOR=ROOT/('integrations/siliconforest/m1-bwfm/supervisor/darwin-wifi-test' if WIFI else 'integrations/siliconforest/m1-apcie-host/supervisor/darwin-pcie-enum-test')
STAGER=ROOT/'integrations/siliconforest/chonkstep-darwin/stage_ramdisk.py'
# PCIE_STAGER=<script>: another stager with the same arguments and record (the slim RAM root:
# integrations/siliconforest/m1-boot-speed/tools/stage_ramdisk_slim.py). Default: the one above.
if os.environ.get('PCIE_STAGER'): STAGER=Path(os.environ['PCIE_STAGER']).resolve(strict=True)
# The RAM root's base image and the base mapping. Since 12 November 2026 both are the tip's
# (docs/roadmap/issues/SF-V-rebase.md): the root of upstream's arm64 minimal image at the tip
# (integrations/siliconforest/rebase/tools/rb_ramdisk_base.sh) and a mapping whose libSystem is the
# tip's build of integrations/siliconforest/libsystem-default. PCIE_PIN=1: the pin's image and the
# mapping as it was before the switch, for booting a collection of the pinned revision.
PIN=os.environ.get('PCIE_PIN')=='1'
ORIGINAL=ROOT/('artifacts/m1-bringup-009/ramdisk.img' if PIN else 'artifacts/rebase/ramdisk-tip.img')
MAPPING=BASE/('mapping.json.before-tip-20261112' if PIN else 'mapping.json')
SRC=ROOT/'integrations/siliconforest/m1-apcie-host'
_sha={}
def sha(p):
 # One read per file state: the RAM disk (480 MiB) is hashed for the record and for the manifest.
 p=Path(p);k=(str(p),p.stat().st_mtime_ns,p.stat().st_size)
 if k not in _sha:_sha[k]=hashlib.sha256(p.read_bytes()).hexdigest()
 return _sha[k]
load=lambda p:json.loads(Path(p).read_text())
def save(p,d):
 with Path(p).open('x')as f:json.dump(d,f,indent=2);f.write('\n')
# PCIE_ASSEMBLE_STAGE: 'all' (default), 'build' (packet only) or 'check' (the offline load check of
# an assembled packet only); tools/pcie_iterate.sh runs the check alongside the delivery.
STAGE=os.environ.get('PCIE_ASSEMBLE_STAGE','all')
def check():
 code='''import sys,json\nfrom pathlib import Path\nfrom types import SimpleNamespace\nlab,p=map(Path,sys.argv[1:]);sys.path[:0]=[str(lab),str(lab/'integrations/siliconforest/m1-bringup')]\nimport host_progress as h,normal_image as n\nreport,contents=n.verify_manifest(p/'manifest.json');i=SimpleNamespace(report=report,contents=contents)\nassert h.apcie_port_observer_enabled(i) is True;h.apcie_port_runtime(i,120,240)\nargs=h.guest_bootargs(i);assert args.endswith(' sfapciebridge=1'),args\nprint(json.dumps({'bootargs':args,'members':len(report['kernel']['members'])}))\n'''
 x=subprocess.run([sys.executable,'-B','-c',code,str(LAB),str(OUT)],capture_output=True,text=True,timeout=120,cwd=LAB)
 print(x.stdout[-600:],x.stderr[-3000:]);x.check_returncode()
if STAGE=='check':
 check();print(OUT);sys.exit(0)
OUT.mkdir(mode=0o700)
JOIN=os.environ.get('PCIE_WIFI_JOIN')=='1'
if JOIN:
 env=dict(l.strip().split('=',1) for l in (ROOT/'private/wifi.env').read_text().splitlines() if '=' in l)
 assert "'" not in env['WIFI_SSID']+env['WIFI_PSK']
 filled=OUT/'darwin-wifi-join-test.private'
 filled.write_text(Path(os.environ.get('PCIE_GUEST_TEMPLATE') or ROOT/'integrations/siliconforest/m1-bwfm/supervisor/darwin-wifi-join-test.in').read_text().replace('@SSID@',env['WIFI_SSID']).replace('@PSK@',env['WIFI_PSK']).replace('@TPUT_HOST@',os.environ.get('PCIE_TPUT_HOST','')))
 filled.chmod(0o700)
 SUPERVISOR=filled
mapping=[]
for e in load(MAPPING)['files']:
 e=dict(e)
 if e['destination']=='/usr/bin/darwin-desktop-test':e['source']=str(SUPERVISOR)
 mapping.append(e)
if JOIN:
 # sf-dhcp, sf-net-up, sf-netd and its launchd job, all from the sf-netd build tools/staged.sh
 # names (the tip's; with PCIE_PIN=1 or SF_STAGED=pin the pinned revision's, as before the rebase)
 N=Path(subprocess.run([str(ROOT/'tools/staged.sh'),'netd'],env=dict(os.environ,**({'SF_STAGED':'pin'} if PIN else {})),stdout=subprocess.PIPE,text=True,check=True).stdout.strip())
 assert (N/'usr/sbin/sf-netd').is_file(),N
 for rel,exe in (('usr/bin/sf-dhcp',True),('usr/bin/sf-net-up',True),('usr/sbin/sf-netd',True),('System/Library/LaunchDaemons/org.siliconforest.sf-netd.plist',False)):
  mapping.append({'source':str(N/rel),'destination':'/'+rel,'executable':exe})
if WIFI:
 # This M1's own firmware, private and never committed.
 for f in sorted((ROOT/'private/firmware/brcm').iterdir()):
  mapping.append({'source':str(f),'destination':'/Library/Firmware/brcm/'+f.name,'executable':False})
# Extra or replacement files: PCIE_EXTRA_FILES="source=/destination;source=/destination"
for item in filter(None,os.environ.get('PCIE_EXTRA_FILES','').split(';')):
 src,dst=item.split('=',1);src=str(Path(src).resolve());assert Path(src).is_file(),src
 mapping=[e for e in mapping if e['destination']!=dst]+[{'source':src,'destination':dst,'executable':True}]
save(OUT/'mapping.json',{'files':mapping})
with(OUT/'stage.log').open('x')as f:subprocess.run([sys.executable,'-B',str(STAGER),'--base',str(ORIGINAL),'--mapping',str(OUT/'mapping.json'),'--output',str(OUT/'ramdisk.img')],stdout=f,stderr=subprocess.STDOUT,check=True)
ram=load(OUT/'ramdisk.record.json');assert ram['e2fsck_exit_code']==0
shutil.copyfile(KERNEL,OUT/'kernel.macho')
text=(BASE/'source.patch').read_text()
if WIFI:
 B=ROOT/'integrations/siliconforest/m1-bwfm'
 for q in sorted(B.iterdir()):
  if q.is_file():text+='\n# COMPONENT SFBwfm (ISC bwfm port; GPL-2.0 as a whole with the DART pool)\n# FILE '+str(q.relative_to(ROOT))+' sha256='+sha(q)+'\n'+q.read_text(errors='replace')+'\n'
for n in ['CMakeLists.txt','Info.plist','NOTICE','SFApcieT8103.cpp','SFApcieT8103.h','dart/sf_dart_pool.cpp','dart/sf_dart_pool.h','build-kext.nix','build-kc.nix','supervisor/darwin-pcie-enum-test']:
 p=SRC/n;text+='\n# COMPONENT PCIe host bridge (GPL-2.0)\n# FILE '+str(p.relative_to(ROOT))+' sha256='+sha(p)+'\n'+p.read_text()+'\n'
(OUT/'source.patch').write_text(text)
record=load(BASE/'build-record.json')
record.update(recorded_at=datetime.datetime.now(datetime.timezone.utc).isoformat(),source_patch_sha256=sha(OUT/'source.patch'),output_path=str(KERNEL.parent),kernel_sha256=sha(KERNEL),
 hardware_gate='PCIE_WIFI_TRACK_NO_ADMISSION_REQUIRED',desktop_test_mode='PCIE_HOST_BRIDGE_ENUMERATION',
 source_patch_scope='native50 source envelope plus the GPL-2.0 SFApcieT8103Bridge host bridge; launcher brings the link up from the proxy',
 not_claimed=['MSI','DART/DMA','BCM4378 firmware or networking'])
record['host_input_sources']={k:sha(LAB/k) for k in record['host_input_sources']}
record['ramdisk']={'sha256':sha(OUT/'ramdisk.img'),'payload_record_sha256':sha(OUT/'ramdisk.record.json'),'guest_script_sha256':sha(SUPERVISOR),'scope':'candidate-003 payloads with the supervisor replaced'}
save(OUT/'build-record.json',record)
manifest=load(BASE/'manifest.json')
manifest['artifacts']={k:{'path':n,'sha256':sha(OUT/n),'bytes':(OUT/n).stat().st_size}for k,n in [('kernel','kernel.macho'),('source_patch','source.patch'),('build_record','build-record.json'),('ramdisk','ramdisk.img')]}
save(OUT/'manifest.json',manifest)
if STAGE!='build':check()
print(OUT)
