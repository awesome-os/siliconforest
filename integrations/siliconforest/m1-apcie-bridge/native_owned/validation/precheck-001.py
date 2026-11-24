from pathlib import Path
import hashlib,json,os,runpy,subprocess
root=Path('/home/chrisk/src/darwin-lab');leaf=root/'integrations/puredarwin/m1-apcie-bridge/native_owned'
out=leaf/'validation/target-precheck-001';out.mkdir(exist_ok=False)
h=runpy.run_path(str(root/'integrations/puredarwin/m1-apcie-bridge/root-build/compile-native-owned-001.py'),run_name='precheck_inputs_only')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
names=['owned.c','owned.h','owned_types.h','native_owned.cpp','native_owned.h','lifecycle_bridge.h']
before={n:sha(leaf/n) for n in names}
record={'status':'MUTABLE_SINGLE_OBJECT_PRECHECK_ONLY','not_admission':True,'source_sha256':before,'results':[]}
common=['-Wall','-Wextra','-Werror','-Wpedantic','-Wframe-larger-than=1024','-fstack-usage','-fno-stack-protector','-fno-common','-DKERNEL','-DKERNEL_PRIVATE','-DTARGET_OS_OSX','-isystem',str(h['KH']/'Headers'),'-isystem',str(h['KH']/'PrivateHeaders'),'-isystem',str(root/'sources/puredarwin-baseline/src/Libraries/AvailabilityVersions/include')]
for opt in ['O0','O2']:
 for name in ['owned.c','native_owned.cpp']:
  cpp=name.endswith('.cpp');tag=Path(name).stem+'-'+opt
  cc=h['TC']/('arm64-apple-darwin20.4-clang++' if cpp else 'arm64-apple-darwin20.4-clang')
  flags=['-std=c++17','-fno-exceptions','-fno-rtti','-fapple-kext'] if cpp else ['-std=c11','-ffreestanding','-fno-builtin']
  cmd=[str(cc),*flags,*common,'-'+opt,'-MD','-MF',str(out/(tag+'.d')),'-c',str(leaf/name),'-o',str(out/(tag+'.o'))]
  (out/(tag+'-command.json')).write_text(json.dumps(cmd,indent=2)+'\n')
  r=subprocess.run(cmd,env=dict(os.environ,DARWIN_SDK_ROOT=str(h['SDK'])),capture_output=True)
  (out/(tag+'.log')).write_bytes(r.stdout+r.stderr)
  entry={'tag':tag,'returncode':r.returncode,'log_sha256':sha(out/(tag+'.log'))}
  for suffix in ['.o','.su']:
   p=out/(tag+suffix)
   if p.exists():entry[suffix+'_sha256']=sha(p)
  record['results'].append(entry)
record['source_unchanged']=before=={n:sha(leaf/n) for n in names}
(out/'result.json').write_text(json.dumps(record,indent=2)+'\n')
print(json.dumps(record,indent=2))
