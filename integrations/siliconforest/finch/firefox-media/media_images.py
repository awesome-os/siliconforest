# SPDX-License-Identifier: MIT
"""Post-measurement dyld observations for already admitted own media processes."""
from pathlib import Path
import re
import subprocess


def inspect(processes,directory,frameworks=None):
    rows=[]
    for process in processes:
        if process['type'] not in ['browser','gpu','rdd']:
            continue
        pid=int(process['pid']);start=process.get('os_start_identity')
        current=subprocess.run(['ps','-p',str(pid),'-o','lstart='],capture_output=True,text=True,check=True).stdout.strip()
        if start is None or current!=start.strip():
            raise RuntimeError('Media image target no longer has its owned identity')
        path=directory/('media-images-'+str(pid)+'.txt')
        sample=subprocess.run(['/private/var/db/sffx/probes-v3/sf-sample',str(pid),'.05','10','-file',str(path)],
                              capture_output=True,text=True,timeout=10)
        text=path.read_text() if path.exists() else ''
        images=[];selected={}
        for line in text.partition('\nBinary Images:\n')[2].splitlines():
            match=re.search(r'\s(CoreVideo|VideoToolbox(?:\.pd)?|AudioToolbox(?:\.output-sf)?|CoreAudio|AudioUnit)\s+<[^>]+>\s+(.+)$',line)
            if match:
                images.append(line.strip());selected.setdefault(match[1],[]).append(match[2])
        checks={}
        if frameworks:
            for name in ['CoreVideo','VideoToolbox','AudioToolbox']:
                if (frameworks/(name+'.framework')).is_dir():
                    paths=selected.get(name,[])
                    checks[name]=len(paths)==1 and paths[0].startswith(str(frameworks/(name+'.framework'))+'/')
                    if name=='AudioToolbox':
                        children=selected.get('AudioToolbox.output-sf',[])
                        checks['AudioToolbox.output-sf']=len(children)==1 and children[0].startswith(str(frameworks/'AudioToolbox.framework')+'/')
                    if name=='VideoToolbox':
                        children=selected.get('VideoToolbox.pd',[])
                        checks['VideoToolbox.pd']=len(children)==1 and children[0].startswith(str(frameworks/'VideoToolbox.framework')+'/')
        rows.append(dict(pid=pid,type=process['type'],status=sample.returncode,images=images,
                         selected=selected,staged_media_checks=checks,
                         passed=sample.returncode==0 and bool(images) and all(checks.values())))
    return dict(processes=rows,complete=bool(rows) and all(row['passed'] for row in rows),
                timing='Post-measurement diagnostic only; not a sample of the timed interval')
