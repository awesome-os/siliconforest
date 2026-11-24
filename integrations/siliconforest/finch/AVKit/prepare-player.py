#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Prepare our coherent bounded-player prefix and a fresh own movie test stage."""
import argparse,hashlib,json,shutil,subprocess
from pathlib import Path

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--package',type=Path,required=True);p.add_argument('--movie',type=Path,required=True);p.add_argument('--stage',type=Path,required=True)
 a=p.parse_args();here=Path(__file__).resolve().parent
 if a.stage.exists():raise ValueError('Fresh output required')
 a.stage.mkdir();shutil.copytree(a.package/'System',a.stage/'System',symlinks=True);shutil.copytree(a.package/'bin',a.stage/'bin',symlinks=True)
 (a.stage/'bin').chmod(0o755);(a.stage/'fixtures').mkdir();shutil.copy2(a.movie,a.stage/'fixtures/movie.mov');shutil.copy2(a.package/'providers.json',a.stage)
 subprocess.run(['python3',str(here.parent/'tools/portable-tests.py'),str(here.parents[1]/'homebrew/hb_macho.py'),str(a.stage/'bin'),str(a.stage/'binary-hashes.json')],check=True)
 # This first process validates all new provider/program bytes before any of
 # their images execute. Loaded readbacks can legitimately have PAC lowered.
 rows=[]
 for directory in ['System','bin']:
  for f in sorted((a.stage/directory).rglob('*')):
   if f.is_file() and not f.is_symlink():rows.append(dict(path=str(f.relative_to(a.stage)),sha256=hashlib.sha256(f.read_bytes()).hexdigest()))
 (a.stage/'cold-hashes.json').write_text(json.dumps(rows,indent=2)+'\n')
 verify=a.stage/'bin/cold-hashes';verify.write_text('''#!/usr/bin/env python3
from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parent.parent
for row in json.loads((root/'cold-hashes.json').read_text()):
 path=root/row['path']
 if not path.resolve().is_relative_to(root):raise ValueError('Hash path outside own stage')
 if hashlib.sha256(path.read_bytes()).hexdigest()!=row['sha256']:raise ValueError('Cold hash mismatch: '+row['path'])
print('AVKIT_COLD_HASHES_PASS')
''');verify.chmod(0o755)
 shutil.copy2(here.parent/'tools/compare-tests.py',a.stage)
 env={'DYLD_FRAMEWORK_PATH':'@ROOT@/System/Library/Frameworks','SF_METAL_ENABLE_REAL_DEVICE':'0','XDG_RUNTIME_DIR':'/private/var/run/gpu-desktop','WAYLAND_DISPLAY':'wayland-0'}
 manifest=[dict(name='avkit-cold-provider-hashes',argv=['bin/cold-hashes'],mode='exit',skip_lines=0,timeout=15),dict(name='avkit-value-test',argv=['bin/avkit-test'],mode='reference',skip_lines=0,timeout=20,env=env),dict(name='avkit-real-player',argv=['bin/avkit-player-probe','fixtures/movie.mov'],mode='exit',skip_lines=0,timeout=30,env=env)]
 (a.stage/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
 print(a.stage)
if __name__=='__main__':main()
