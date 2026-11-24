"""Independent test sensitivity for the new policy, isolated temporary copies."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent
MUTANTS={
 'reply_id':('ident != struct.unpack_from("!H", request)[0] or not flags & 0x8000','not flags & 0x8000'),
 'query_source_port':('self.query != payload or self.dns_port != sp','self.query != payload'),
 'query_id_or_bytes':('self.query != payload or self.dns_port != sp','self.dns_port != sp'),
 'query_retry_budget':('self.dns_attempts > 3','self.dns_attempts > 300'),
 'edns_reply':('if kind == 41:','if kind == -1:'),
 'tcp_second_endpoint':('dst != self.endpoint or dp != 80 or sp < 1024','dp != 80 or sp < 1024'),
 'tcp_second_flow':('if sp != self.tcp_port:','if False:'),
 'lease_expiry':('not self.allowed_addresses or self.clock() >= self.expires','not self.allowed_addresses'),
 'request_coverage':('self.reply is not None and all(self.coverage) and self.http is not None','self.reply is not None and self.http is not None'),
 'server_fin':('self.highest == self.contiguous and self.fin_offset == self.contiguous','self.highest == self.contiguous'),
}
def run():
 source=(HERE/'policy.py').read_text();reports=[]
 with tempfile.TemporaryDirectory(prefix='resolver-policy-mutants-') as d:
  root=Path(d)
  for name,(before,after) in MUTANTS.items():
   assert source.count(before)==1,(name,source.count(before))
   text=source.replace(before,after);compile(text,'policy.py','exec')
   folder=root/name;folder.mkdir()
   for file in ('packet_fixture.py','test_policy.py'):(folder/file).write_bytes((HERE/file).read_bytes())
   (folder/'policy.py').write_text(text)
   r=subprocess.run([sys.executable,'-I','-m','unittest','discover','-s',str(folder),'-p','test_policy.py','-q'],
     stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=5)
   output=r.stdout.decode(); killed=r.returncode==1 and 'FAIL:' in output and 'ERROR:' not in output
   reports.append({'mutation':name,'assertion_killed':killed,'returncode':r.returncode,'output':output})
 assert all(x['assertion_killed'] for x in reports),json.dumps(reports)
 return {'status':'PASS_HOST_ONLY_POLICY_MUTATION_SENSITIVITY','mutations':reports}
if __name__=='__main__':print(json.dumps(run(),indent=2))
