"""Explicit v2 profile selection: old/controlled/absent profiles fail before setup."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile
sys.dont_write_bytecode=True


def run(library):
    cases=[]
    with tempfile.TemporaryDirectory(prefix='public002-profile-') as tmp:
        for i,profile in enumerate(('example-http-v1','dlnet001_space_lf_ack_v2',None)):
            records=Path(tmp)/str(i)
            args=[sys.executable,'-I',str(Path(__file__).with_name('backend.py')),
                '--nonce','a'*48,'--codec-library',str(library),'--codec-sha256','0'*64,
                '--records',str(records),'--listen-port','0','--seconds','90','--accept-seconds','8']
            if profile is not None:args+=['--profile',profile]
            p=subprocess.run(args,capture_output=True,text=True,timeout=3)
            assert p.returncode==2 and not records.exists(),(profile,p.stderr)
            cases.append({'profile':profile,'returncode':p.returncode,'setup_directory_absent':True})
    return {'status':'PASS_EXPLICIT_PUBLIC_V2_PROFILE_REFUSAL','cases':cases,'network_traffic':False}


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--library',required=True,type=Path)
    a=p.parse_args();print(json.dumps(run(a.library.resolve()),indent=2))
