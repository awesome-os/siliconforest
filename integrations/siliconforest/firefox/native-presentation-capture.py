# SPDX-License-Identifier: MIT
"""Capture the owned native GPU fixture after admitting its unique frame."""
import json,os,re,shutil,subprocess,sys,time
from pathlib import Path
root=Path(__file__).resolve().parent
phase,marker=sys.argv[1:]
assert phase in ['green','yellow','resized','adopted'] and re.fullmatch(r'SF-FX GPU native \d+',marker)
inject=Path('/private/var/run/gpu-desktop/inject');name='sffx-gpu-'+marker.split()[-1]+'-'+phase
def request(lines):
 if (inject/'request').exists() or (inject/'request.part').exists():raise RuntimeError('Other inject request pending')
 (inject/'done').unlink(missing_ok=True)
 with (inject/'request.part').open('x') as f:f.write(lines+'\n')
 os.replace(inject/'request.part',inject/'request')
 until=time.monotonic()+6
 while not (inject/'done').exists() and time.monotonic()<until:time.sleep(.05)
 status=(inject/'done').read_text();assert re.search(r'errors=0\b',status),status
request('dump '+name)
dump=(inject/(name+'.txt')).read_text();(root/(phase+'-windows.txt')).write_text(dump)
lines=[line for line in dump.splitlines() if marker in line and 'mapped=true' in line]
assert len(lines)==1,lines
line=lines[0];m=re.search(r'content=Rect \{ pos: Point \{ x: (-?\d+), y: (-?\d+) \}, size: Size \{ w: (\d+), h: (\d+) \}',line);assert m,line
metadata=dict(marker=marker,content=list(map(int,m.groups())),external='external=true' in line,window_line=line)

x,y,w,h=metadata['content'];px,py=x+w//2,y+h//2
assert 0<=px<=2559 and 0<=py<=1599,metadata
request('abs %d %d\nwait 500\ncapture %s-renderer' % (round(px*65535/2559),round(py*65535/1599),name))
metadata['synthetic_pointer_wake_within_owned_content']=True
(root/(phase+'-capture.json')).write_text(json.dumps(metadata,indent=2)+'\n')
shutil.copy2(inject/(name+'-renderer.ppm'),root/(phase+'-renderer.ppm'))
renderer_log=Path('/var/log/desktop-session.log').read_text(errors='replace')
renderer_lines=[line for line in renderer_log.splitlines() if 'WMINJECT capture=' in line and name+'-renderer.ppm' in line]
(root/(phase+'-renderer-source.log')).write_text('\n'.join(renderer_lines)+'\n')
assert renderer_lines and 'source=renderer' in renderer_lines[-1],renderer_lines
r=subprocess.run(['/usr/bin/chonkstep-darwin-desktop','--screencapture',str(root/(phase+'.png'))],capture_output=True,text=True,timeout=10)
(root/(phase+'-capture.log')).write_text(r.stdout+r.stderr);r.check_returncode()
