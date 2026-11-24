# SPDX-License-Identifier: MIT
"""Read only our new HOME's ring metadata; observe exact server-slot removal."""
import json
from pathlib import Path
import re
import subprocess
import time

from audio_logs import MAX_BYTES


def own_ring(run,home,pid):
    run=Path(run).resolve();home=Path(home)
    if home!=run/'home' or home.is_symlink():raise ValueError('Only the new owned HOME is admitted')
    directory=home/'Library/Logs/SiliconForest/Firefox'
    if not directory.exists():return None
    if not directory.resolve().is_relative_to(home):raise ValueError('Native logs escape own HOME')
    paths=sorted(directory.glob('firefox-*.log'))
    if len(paths)>4:raise ValueError('Unexpected own log count')
    matches=[]
    for path in paths:
        if path.is_symlink() or not path.is_file() or not re.fullmatch(r'firefox-\d+-\d+\.log',path.name):
            raise ValueError('Unexpected own log path')
        with path.open('rb') as stream:data=stream.read(MAX_BYTES+1)
        if len(data)>MAX_BYTES:raise ValueError('Own native log exceeds bound')
        for line in data.decode(errors='replace').splitlines():
            if not line.startswith('SFMEDIA_RING '):continue
            values=dict(re.findall(r'([a-z-]+)=([^ ]+)',line))
            try:
                if (int(values['pid'])!=pid or int(values['running'])!=1 or int(values['protocol'])!=2 or
                    int(values['rate'])!=48000 or int(values['period'])!=512 or int(values['capacity'])!=2048 or
                    int(values['read'])<=0 or int(values['write'])<int(values['read'])):continue
                address=int(values['ring-address'],16)
                if address<0x100000000:continue
                matches.append(dict(address=address,pid=pid,source=str(path.relative_to(run)),
                                    read=int(values['read']),write=int(values['write'])))
            except (KeyError,ValueError):continue
    addresses={r['address'] for r in matches}
    if len(addresses)>1:raise ValueError('Multiple active own rings cannot be attributed')
    return matches[-1] if matches else None


class Watch:
    def __init__(self,root,run,parent,start,identities,pause):
        self.run=run;self.process=None;self.out=None;self.err=None;self.result={}
        binary=root/'bin/audio-session-watch';spec=json.loads((root/'audio-server.json').read_text())
        if set(spec)!={'uuid','client_table_offset','source_sha256'} or not re.fullmatch('[0-9a-f]{32}',spec['uuid']):
            raise ValueError('Exact original daemon identity and table offset required')
        if not isinstance(spec['client_table_offset'],int) or not 0<spec['client_table_offset']<0x1000000:
            raise ValueError('Invalid client table offset')
        deadline=time.monotonic()+3;ring=None
        while not ring and time.monotonic()<deadline:
            ring=own_ring(run,run/'home',parent)
            if not ring:pause(.1)
        if not ring:raise RuntimeError('No active own ring pointer in bounded own native logs')
        servers=[]
        for line in subprocess.check_output(['ps','-axo','pid=,comm='],text=True).splitlines():
            fields=line.split(None,1)
            if len(fields)==2 and fields[1] in ['coreaudiod','/usr/sbin/coreaudiod']:servers.append(int(fields[0]))
        current=identities()
        if len(servers)!=1 or parent not in current or current[parent][1]!=start or servers[0] not in current:
            raise RuntimeError('Own client or exact daemon process admission failed')
        self.server=servers[0];self.server_start=current[self.server][1];self.identities=identities
        self.result=dict(own_ring=ring,server_pid=self.server,server_start=self.server_start,
                         original_daemon=spec,read_only=True,pcm_read=False)
        self.out=(run/'audio-session-watch.jsonl').open('w');self.err=(run/'audio-session-watch.stderr').open('w')
        self.process=subprocess.Popen([str(binary),str(self.server),hex(spec['client_table_offset']),spec['uuid'],
            str(parent),hex(ring['address']),'15'],stdout=self.out,stderr=self.err)
        deadline=time.monotonic()+3
        while time.monotonic()<deadline:
            rows=self.rows()
            if rows and rows[0].get('event')=='matched' and rows[0].get('client_task_released') is True:
                self.result['matched']=rows[0];return
            if self.process.poll() is not None:break
            pause(.05)
        raise RuntimeError('No shared-object match before own disconnect; exit test refused')

    def rows(self):
        path=self.run/'audio-session-watch.jsonl'
        if path.stat().st_size>65536:raise ValueError('Unexpected observer output size')
        rows=[]
        for line in path.read_text().splitlines():
            try:rows.append(json.loads(line))
            except json.JSONDecodeError:pass # A partial live final line is not evidence.
        return rows

    def finish(self,action_epoch):
        self.process.wait(timeout=17);self.out.close();self.err.close()
        self.result.update(status=self.process.returncode,events=self.rows())
        current=self.identities();self.result['same_server']=self.server in current and current[self.server][1]==self.server_start
        matched=self.result.get('matched');removed=next((r for r in self.result['events'] if r.get('event')=='removed'),None)
        if matched and removed:
            bridge=(removed['epoch']-removed['monotonic'])-(matched['epoch']-matched['monotonic'])
            latency=1000*(removed['first_empty_epoch']-action_epoch)
            self.result.update(clock_bridge_change_ms=1000*bridge,first_empty_after_action_ms=latency)
            self.result['qualified']=bool(self.process.returncode==0 and self.result['same_server'] and
                matched.get('same_shared_object') is True and matched.get('client_task_released') is True and
                matched['epoch']<action_epoch and removed.get('slot')==matched.get('slot') and
                removed.get('slot_empty') is True and removed.get('stable_empty_samples')==2 and
                abs(bridge)<.002 and 0<=latency<=1000 and removed.get('pcm_read') is False)
        return self.result
