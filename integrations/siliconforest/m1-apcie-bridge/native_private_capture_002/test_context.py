"""Known real STATIC packet inputs only; explicitly no captured runtime."""
from pathlib import Path
import json,tempfile,unittest,subprocess,sys
import audit as a
class ContextTests(unittest.TestCase):
    def test_snapshot_list_source_is_itself_frozen(self):
        original=a.HOST
        with tempfile.TemporaryDirectory(prefix='private-snapshot-source-copy-') as d:
            copy=Path(d);(copy/'source-validation-001.json').write_bytes((original/'source-validation-001.json').read_bytes())
            for name in list(a.AFTER)+['tools/m1_kernel_cycle.py']:
                path=copy/'after'/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes((original/'after'/name).read_bytes())
            path=copy/'after/tools/m1_kernel_cycle.py';path.write_bytes(path.read_bytes()+b'\nSOURCE_SHA = {}\n')
            try:
                a.HOST=copy
                with self.assertRaisesRegex(ValueError,'snapshot-list source drift'):a.snapshot_pins()
            finally:a.HOST=original
    def test_known_static_packet_inputs_never_runtime(self):
        packet=a.ROOT/'artifacts/m1-desktop-010/native-private-probe-candidate-001';manifest=packet/'manifest.json'
        if not manifest.exists():self.skipTest('Known static packet is not locally available; no substitute proof')
        raw=a.read(manifest,1<<20);self.assertEqual(a.sha(raw),'de2fce86a50e09ade27fd516ae428bf9abe31366a6f64974430106256fe3054f')
        m=a.js(raw);context={k:{'path':str(packet/m['artifacts'][k]['path']),'sha256':m['artifacts'][k]['sha256']} for k in ('kernel','ramdisk')}
        images=a.image_context(context);self.assertEqual(images['kernel_sha256'],'2a2b9c125faa5b376d1c2f46bfd220d340443c3d2e70c15af40b0cf1f61a3c21');self.assertEqual(images['ramdisk_sha256'],'129152ba4691b8b4ba1748cef74acdc8936484234e93cc6df45cfde302920669')
        with tempfile.TemporaryDirectory(prefix='private-capture-no-runtime-') as d:
            # Synthetic adapter to REAL already-reviewed static docs only.
            # This is not a Root admission document or a recovery certificate.
            review=a.ROOT/'reports/sprint-010/bcm4378-native-private-probe-whole-packet-independent-review-001.json'
            self.assertEqual(a.sha(a.read(review,1<<20)),'14336218a14a2e11375f4f9624a85b05bf70e9266b45b43d6e76d6cf76b1acb4')
            ref=lambda p:{'path':str(p),'sha256':a.file_sha(p,1<<20)}
            binding=Path(d)/'SYNTHETIC_STATIC_DOC_ADAPTER.json';binding.write_text(json.dumps({'kind':'PRIVATE_PACKET_EXTERNAL_BINDING_V1','status':'VERIFIED_EXTERNALLY','kernel_sha256':images['kernel_sha256'],'ramdisk_sha256':images['ramdisk_sha256'],'manifest':ref(manifest),'review':ref(review),'boot_admitted':False,'synthetic_adapter_test_only':True}))
            context['packet_binding']=ref(binding)
            checked=a.external_packet(context,images);self.assertEqual(checked['packet_manifest_sha256'],'de2fce86a50e09ade27fd516ae428bf9abe31366a6f64974430106256fe3054f')
            result=a.audit(Path(d)/'NO_CAPTURE_EXISTS',context)
            self.assertFalse(result['qualified']);self.assertEqual(tuple(result['checks']),a.CHECKS);self.assertEqual(result['checks'][a.CHECKS[9]]['status'],'NOT_EVALUATED')
        print('Actual static manifest/kernel/RAM hashes verified only; no capture/recovery => unqualified')
    def test_CLI_missing_files_and_exclusive_output(self):
        with tempfile.TemporaryDirectory(prefix='private-capture-cli-') as d:
            p=Path(d);context=p/'context.json';context.write_text('{}');out=p/'audit.json'
            argv=[sys.executable,'-B',str(Path(__file__).with_name('audit_capture.py')),'--cycle',str(p/'absent'),'--context',str(context),'--output',str(out)]
            r=subprocess.run(argv,capture_output=True,timeout=10);self.assertEqual(r.returncode,1,r.stderr)
            report=json.loads(out.read_text());self.assertFalse(report['qualified']);self.assertEqual(tuple(report['checks']),a.CHECKS);self.assertEqual(out.stat().st_mode&0o777,0o600)
            raw=out.read_bytes();r=subprocess.run(argv,capture_output=True,timeout=10);self.assertEqual(r.returncode,2);self.assertEqual(out.read_bytes(),raw)
if __name__=='__main__':unittest.main(verbosity=2)
