"""Python decision mutations against fake-only actual-helper tests."""
from pathlib import Path
import importlib.util,io,json,sys,tempfile,unittest
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);sys.modules[name]=module;spec.loader.exec_module(module)
    return module
def main():
    source=(HERE/'line_bridge.py').read_text()
    mutations={
      'claim-without-desktop':("result['desktop_ok'] != 1",'False'),
      'claim-without-daemon':("result['daemon_cleanup'] != 1",'False'),
      'reused-role-PID':('len(set(pids)) != 8','False'),
      'bad-process-range':('any(not 2 <= p <= (1<<31)-1 for p in pids)','False'),
      'missing-route-restoration':("result['route_stage'] != 5",'False'),
      'missing-interface-restoration':("result['if_stage'] != 7",'False'),
      'wrong-actual-packets':("result['packets_out'] != sum(x['type']==2 for x in self.guest_frames)",'False'),
      'wrong-close-window':('end == start+40000','end == start+15000'),
      'partial-stream-claim':("result['partial_line'] != 0 or result['partial_bits'] != 0",'False'),
      'expanded-credit':('MAX_CHUNK_BYTES = 192','MAX_CHUNK_BYTES = 512'),
      'renewed-ACK-window':('ACK_SECONDS = 3','ACK_SECONDS = 30'),
      'missing-chunk-history-cap':('len(self.chunk_ledger) >= MAX_CHUNKS','False'),
      'coalesced-burst-overread':('self.socket.recv(wanted)','self.socket.recv(4096)'),
      'partial-ACK-next-chunk':('self.acked_bytes == self.data_bytes and self.outstanding_since is None:','self.outstanding_since is not None:'),
      'prior-profile-namespace':("b'DL30_', b'DL29_', b'DL28_',","b'DL30_', b'DL28_',"),
    }
    results=[]
    with tempfile.TemporaryDirectory(prefix='dnds-helper-mutants-') as tmp:
        for name,(a,b) in mutations.items():
            if a not in source:raise RuntimeError('mutation absent '+name)
            path=Path(tmp)/(name+'.py');path.write_text(source.replace(a,b))
            sys.modules['line_bridge']=load('mutant_'+name.replace('-','_'),path)
            test=load('actual_helper_tests_'+name.replace('-','_'),HERE/'test_line_bridge.py')
            log=io.StringIO();result=unittest.TextTestRunner(stream=log).run(unittest.defaultTestLoader.loadTestsFromModule(test))
            # Require assertion failures; syntax/import/runtime errors alone do
            # not count as a test demonstrating the broken decision.
            if result.wasSuccessful() or not result.failures:
                raise RuntimeError('mutant not assertion killed '+name+'\n'+log.getvalue())
            results.append({'name':name,'tests':result.testsRun,'assertion_failures':len(result.failures)})
    print(json.dumps({'status':'PASS','assertion_killed_mutants':results},indent=2))
if __name__=='__main__':main()
