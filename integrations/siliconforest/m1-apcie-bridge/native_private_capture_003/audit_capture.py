#!/usr/bin/env python3
"""Offline explicit saved-cycle CLI; never overwrites an existing audit."""
import argparse,json,os
from pathlib import Path
import audit
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cycle',required=True,type=Path);p.add_argument('--context',required=True,type=Path);p.add_argument('--recovery',type=Path);p.add_argument('--recovery-sha256');p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    if bool(a.recovery)!=bool(a.recovery_sha256):p.error('recovery path and externally supplied digest must accompany one another')
    error=None
    try:context=audit.js(audit.read(a.context,1<<20))
    except Exception as e:context={};error=type(e).__name__+': '+str(e)
    recovery={'path':str(a.recovery),'sha256':a.recovery_sha256} if a.recovery else None
    result=audit.audit(a.cycle,context,recovery)
    if error:result['checks'][audit.CHECKS[0]].update(status='FAILED',errors=['context: '+error]);result.update(qualified=False,status='UNQUALIFIED_SAVED_CAPTURE_CHAIN')
    try:
        fd=os.open(a.output,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
        with os.fdopen(fd,'w') as f:json.dump(result,f,indent=2);f.write('\n')
    except OSError as e:p.exit(2,'exclusive output failed: '+str(e)+'\n')
    return 0 if result['qualified'] else 1
if __name__=='__main__':raise SystemExit(main())
