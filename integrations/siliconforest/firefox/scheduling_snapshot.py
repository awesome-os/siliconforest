# SPDX-License-Identifier: MIT
"""Read task/thread policy only for admitted owned Firefox process identities."""
import json
from pathlib import Path
import subprocess


def inspect(helper,identities,owned,output):
    if not helper.startswith('/private/var/db/sffx/') or '..' in Path(helper).parts:
        raise ValueError('Scheduling helper must be in the private prefix')
    rows=[]
    for pid,start in sorted(owned.items()):
        if pid not in identities or identities[pid][1]!=start:continue
        current=subprocess.run(['ps','-p',str(pid),'-o','lstart='],capture_output=True,text=True).stdout.strip()
        if current!=start.strip():continue
        command=subprocess.run(['ps','-p',str(pid),'-o','command='],capture_output=True,text=True).stdout.strip()
        result=subprocess.run([helper,str(pid)],capture_output=True,text=True,timeout=5)
        row=dict(pid=pid,lstart=start,command=command,status=result.returncode,stderr=result.stderr)
        try:row['policy']=json.loads(result.stdout)
        except ValueError:row['error']='Native helper did not return JSON';row['stdout']=result.stdout
        rows.append(row)
    result=dict(processes=rows,read_only=True,complete=bool(rows) and all(row['status']==0 and row.get('policy',{}).get('thread_list_status')==0 for row in rows),
        method='Read-only task_for_pid/task_threads/thread_info/thread_policy_get snapshot of PID/lstart-admitted owned processes. Missing policy flavors retain their status; no task suspension, scheduling setter or affinity write. An instantaneous policy snapshot does not prove sustained P/E placement.')
    Path(output).write_text(json.dumps(result,indent=2)+'\n')
    return result
