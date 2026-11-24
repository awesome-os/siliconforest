#!/usr/bin/env python3
"""Create a PRIVATE GitHub repo from this clean local repository, only on --execute.

Uses the user's existing gh login; never asks for or reads a token. Refuses an
existing remote, a different authenticated owner, or a dirty/non-main worktree.
No force push, deletion, visibility changes or automatic retries.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]

def run(args: list[str]) -> str:
    completed=subprocess.run(args,cwd=ROOT,capture_output=True,text=True,timeout=120,check=False)
    if completed.returncode:
        raise RuntimeError(f"Command failed ({completed.returncode}): {' '.join(args)}\n{completed.stderr}")
    return completed.stdout.strip()

def plan(owner: str, name: str) -> dict:
    for value in (owner,name):
        if not re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_.-]{0,99}',value):raise ValueError('Invalid owner/name')
    return {'status':'PLAN_ONLY','repository':owner+'/'+name,'private':True,'executed':False,
            'command':['gh','repo','create',owner+'/'+name,'--private','--source',str(ROOT),'--remote','origin','--push',
                       '--description','Experimental Apple Silicon Darwin lab: source contracts, platform research and Chonkstep integration']}

def publish(owner: str, name: str) -> dict:
    proposed=plan(owner,name)
    top=Path(run(['git','rev-parse','--show-toplevel'])).resolve()
    if top!=ROOT.resolve():raise RuntimeError('Not the expected repository root')
    if run(['git','status','--porcelain']):raise RuntimeError('Commit or remove local changes before publishing')
    if run(['git','branch','--show-current'])!='main':raise RuntimeError('Expected main branch')
    if run(['git','remote']):raise RuntimeError('Existing remotes detected; review them manually instead of overwriting')
    login=run(['gh','api','user','--jq','.login'])
    if login.lower()!=owner.lower():raise RuntimeError(f'Authenticated as {login}, not requested owner {owner}')
    # gh itself safely rejects an existing repository with this name. A failure
    # after creation may leave a private remote or local origin; do not retry
    # blindly and do not delete anything as automatic rollback.
    run(proposed['command'])
    meta=json.loads(run(['gh','repo','view',owner+'/'+name,'--json','nameWithOwner,isPrivate,url']))
    if meta.get('nameWithOwner','').lower()!=(owner+'/'+name).lower() or meta.get('isPrivate') is not True:
        raise RuntimeError('Post-create identity/visibility verification failed; inspect GitHub manually')
    head=run(['git','rev-parse','HEAD'])
    remote=run(['git','ls-remote','origin','refs/heads/main']).split()
    if not remote or remote[0]!=head:raise RuntimeError('Remote main does not match local HEAD')
    return {'status':'PUBLISHED_AND_VERIFIED','repository':meta['nameWithOwner'],'private':True,'url':meta['url'],'commit':head}

def main(argv: list[str]|None=None)->int:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--owner',default='chriskearney');p.add_argument('--name',default='darwin-lab')
    p.add_argument('--execute',action='store_true')
    a=p.parse_args(argv)
    try:
        result=publish(a.owner,a.name) if a.execute else plan(a.owner,a.name)
        print(json.dumps(result,indent=2));return 0
    except (ValueError,RuntimeError,OSError,subprocess.TimeoutExpired) as exc:
        print(json.dumps({'status':'NOT_PUBLISHED_OR_VERIFICATION_FAILED','error':str(exc),
                          'note':'No automatic rollback. A failed create/push may have partially completed; inspect before retrying.'}),file=sys.stderr)
        return 2
if __name__=='__main__':raise SystemExit(main())
