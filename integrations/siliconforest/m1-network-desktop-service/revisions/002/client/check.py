from pathlib import Path
import json,re,subprocess,tempfile
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[5]
CODEC=ROOT/'integrations/puredarwin/m1-network-egress-guest/codec'
def main():
    with tempfile.TemporaryDirectory(prefix='dnds-client-result-') as tmp:
        tmp=Path(tmp)
        flags=['clang','-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-O1',
               '-fsanitize=address,undefined','-I'+str(HERE),'-I'+str(CODEC)]
        def run(source,name):
            path=tmp/(name+'.c');path.write_text(source);exe=tmp/name
            p=subprocess.run(flags+[str(path),str(HERE/'test_result.c'),str(CODEC/'egress_validate.c'),'-o',str(exe)],capture_output=True,text=True,timeout=30)
            if p.returncode:raise RuntimeError(p.stderr)
            return exe,subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
        source=(HERE/'net_result.c').read_text();exe,p=run(source,'baseline')
        if p.returncode:raise RuntimeError(p.stdout+p.stderr)
        # Independent Python checks on the actual C serializer's emitted bytes.
        emitted=subprocess.check_output([str(exe),'--emit'])
        d=json.loads(emitted)
        assert d['status']=='PASS' and d['pid']==77 and d['ppid']==76
        assert d['addresses']==['1.1.1.1','93.184.216.34'] and d['selected']==d['addresses'][0]
        assert d['local']=={'address':'10.0.2.15','port':49152} and d['peer']=={'address':'1.1.1.1','port':80}
        request=bytes.fromhex(d['request_hex']);response=bytes.fromhex(d['response_hex'])
        assert len(request)==d['request_bytes']==186 and len(response)==d['response_bytes']
        assert b'X-Darwin-Lab-Nonce: '+d['nonce'].encode()+b'\r\n' in request
        header,body=response.split(b'\r\n\r\n',1)
        assert header.startswith(b'HTTP/1.1 200 OK\r\n') and b'Example Domain' in body
        assert d['http']['status']==200 and d['http']['body_bytes']==len(body) and d['http']['header_bytes']==len(header)+4
        mutants={
          'wrong-slot-instance':('count!=48||strcmp(expected_instance,r->identity.instance)','count!=48'),
          'same-client-shell-pid':('r->pid==r->ppid||','0||'),
          'not-first-api-address':('memcmp(r->peer,r->addresses[0],4)','0'),
          'private-address':('if(!eg_ipv4_global(r->addresses[i]))','if(0)'),
          'wrong-mss':('r->tcp_maxseg_observed!=256||','0||'),
          'missing-shutdown':('r->request_shutdown!=1||','0||'),
          'wrong-request-bytes':('memcmp(request,r->request,request_size)||','0||'),
          'wrong-response-facts':('actual.body_bytes!=r->http.body_bytes||','0||'),
          'missing-success-errno':('r->system_errno||r->error_errno||','r->system_errno||'),
          'late-client':('r->elapsed_ms>=60000||','0||'),
        }
        killed=[]
        for name,(a,b) in mutants.items():
            pattern=r'\s*'.join(re.escape(ch) for ch in a)
            modified,count=re.subn(pattern,lambda _:b,source)
            assert count==1,(name,count)
            _,p=run(modified,name)
            if p.returncode==0 or 'Assertion' not in p.stderr:raise RuntimeError('mutation not assertion killed '+name+' '+p.stderr)
            killed.append(name)
        print(json.dumps({'status':'PASS','failed_PASS_field_cases':43,'all_short_output_caps':True,
                          'independent_emitted_JSON_HTTP_checks':True,'assertion_killed_mutants':killed},indent=2))
if __name__=='__main__':main()
