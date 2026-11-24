"""Exercise real git-apply in tiny disposable fixture repositories, NOT PureDarwin."""
from contextlib import redirect_stderr, redirect_stdout
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from tools import stage_puredarwin as stage

ARM64_FIXTURE='''# Synthetic source; the ARM32 block must remain untouched.
  kextsArm32Build = mkPureDarwinBuild ({
    buildTargets = [
      "IOPCIFamily"
    ];
    installKextNames = [
      "IOPCIFamily.kext"
    ];
  });
  kextsArm64Build = mkPureDarwinBuild ({
    src = kextsOnlySource;
    buildTargets = [
      "IOPCIFamily"
    ];
    installKexts = true;
    installKextNames = [
      "IOPCIFamily.kext"
    ];
  } // kextsOnlyArgs xnuHeadersArm64Build);
# End of synthetic source.
'''

DEBUG_ARGS='-v debug=0x218 -nogzalloc_mode keepsyms=1 serial=3 gopconsole=1 pdtrace=1 serial_video_mirror=1 no_interrupt_masked_debug=1'
RELEASE_ARGS='-v serial=3 ahci_debug=1 kext=0xffff io=0xffff'

def kc_fixture(name,kernel):
    return f'''      {name} = pkgs.callPackage ./pkgs/toolchain/kc-arm64.nix {{
        kernel = {kernel};
        inherit kernelSource;
        kexts = kextsArm64Build;
        kcTools = kc-tools.packages.${{system}}.default;
      }};
'''

def image_fixture(name,kc,args,packages='zshArm64Build libiconvArm64Build toyboxArm64Build'):
    return f'''      {name} = pkgs.callPackage ../image.nix {{
        kc = {kc};
        extraPackages = [ {packages} ];
        bootArgs = "{args}";
      }};
'''

IMAGE_FIXTURE=('''{ pkgs
, darwinCrossToolchain
, libSystemBuild
, nativeLd
}:
let
'''+
    kc_fixture('kcArm64DebugBuild','kernelArm64VirtDebugBuild')+
    kc_fixture('kcArm64ReleaseBuild','kernelArm64VirtBuild')+
    kc_fixture('kcArm64A733ReleaseBuild','kernelArm64VirtAmpBuild')+
    kc_fixture('kcArm64Sun50iDebugBuild','kernelArm64Sun50iDebugBuild')+
    image_fixture('imageArm64VirtMinimalBuild','kcArm64DebugBuild',DEBUG_ARGS,
                  'zshArm64Build libiconvArm64Build toyboxArm64Build asmjitTestArm64Build')+
    image_fixture('imageArm64VirtMinimalReleaseBuild','kcArm64ReleaseBuild',RELEASE_ARGS)+
    image_fixture('imageArm64Sg2002Build','kcArm64Sun50iDebugBuild',DEBUG_ARGS)+
    image_fixture('netbootArm64VirtMinimalBuild','kcArm64DebugBuild',DEBUG_ARGS)+
    image_fixture('imageArm64VirtBuild','kcArm64ReleaseBuild',RELEASE_ARGS)+'in {}\n')

FLAKE_FIXTURE='''# Synthetic import wiring.
          arm64 = import ./nix/arm64.nix {
            inherit pkgs;
          };
          imageContents = import ./nix/image-contents.nix {
            inherit kcGpuBuild gpuDefaultPackages;
          };
'''

class StageTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)/'checkout';self.root.mkdir()
        self.git('init','-q')
        self.originals={stage.ANCHOR:'# synthetic parent\n',stage.HEADER:'/* synthetic header */\n',
                        stage.ARM64_NIX:ARM64_FIXTURE,stage.IMAGE_NIX:IMAGE_FIXTURE,stage.FLAKE_NIX:FLAKE_FIXTURE}
        for path,data in self.originals.items():
            p=self.root/path;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(data)
        self.commit_fixture()
        for name,value in (
            ('PIN',self.git('rev-parse','HEAD').strip()),
            ('HEADER_BLOB',stage.blob((self.root/stage.HEADER).read_bytes())),
            ('ANCHOR_BLOB',stage.blob((self.root/stage.ANCHOR).read_bytes())),
            ('ARM64_VM_BLOBS',{p:stage.blob((self.root/p).read_bytes()) for p in (stage.ARM64_NIX,stage.IMAGE_NIX,stage.FLAKE_NIX)}),
        ):
            mock=patch.object(stage,name,value);mock.start();self.addCleanup(mock.stop)
    def git(self,*args):
        r=subprocess.run(['git','-C',str(self.root),*args],capture_output=True,text=True,timeout=30)
        if r.returncode:raise RuntimeError(r.stderr)
        return r.stdout
    def commit_fixture(self):
        self.git('add','.')
        self.git('-c','user.name=Lab fixture','-c','user.email=fixture@example.invalid','commit','-qm','fixture')
        return self.git('rev-parse','HEAD').strip()
    def assert_clean(self):
        self.assertEqual(self.git('status','--porcelain'),'')
        self.assertFalse((self.root/stage.DEST).exists())
    def cli(self,*args):
        out,err=io.StringIO(),io.StringIO()
        with redirect_stdout(out),redirect_stderr(err):
            code=stage.main(['--checkout',str(self.root),*args])
        return code,json.loads(err.getvalue() if code else out.getvalue())
    def test_dry_run_no_writes(self):
        report,patch_text=stage.prepare(self.root)
        self.assertFalse(report['executed']);self.assertFalse(report['kernel_built'])
        self.assertFalse(report['arm64_vm'])
        self.assertEqual(set(report['modified_files']),{stage.ANCHOR})
        self.assertNotIn('b/nix/',patch_text)
        self.assertIn('DLMemoryProbe.cpp',patch_text)
        self.assertEqual(self.git('status','--porcelain'),'');self.assertFalse((self.root/stage.DEST).exists())
    def test_real_fixture_application(self):
        report=stage.execute(self.root,True);self.assertTrue(report['executed'])
        for path,data in stage.files().items():self.assertEqual((self.root/path).read_bytes(),data)
        self.assertIn(stage.MARKER,(self.root/stage.ANCHOR).read_text())
        self.assertEqual(self.git('rev-parse','HEAD').strip(),stage.PIN)
        self.assertFalse(report['kernel_built'])
        for path in stage.ARM64_VM_BLOBS:
            self.assertEqual((self.root/path).read_text(),self.originals[path])
        self.assertEqual(self.git('diff','--cached','--name-only'),'')
    def test_missing_ack(self):
        for arm64_vm in (False,True):
            with self.assertRaises(stage.StageError):stage.execute(self.root,False,arm64_vm)
        self.assert_clean()
    def test_wrong_pin(self):
        for arm64_vm in (False,True):
            with patch.object(stage,'PIN','0'*40),self.assertRaises(stage.StageError):stage.prepare(self.root,arm64_vm)
    def test_wrong_header(self):
        for arm64_vm in (False,True):
            with patch.object(stage,'HEADER_BLOB','0'*40),self.assertRaises(stage.StageError):stage.prepare(self.root,arm64_vm)
    def test_wrong_parent_blob(self):
        for arm64_vm in (False,True):
            with patch.object(stage,'ANCHOR_BLOB','0'*40),self.assertRaises(stage.StageError):stage.prepare(self.root,arm64_vm)
    def test_dirty_tracked_file(self):
        (self.root/stage.ANCHOR).write_text('changed\n')
        for arm64_vm in (False,True):
            with self.assertRaises(stage.StageError):stage.prepare(self.root,arm64_vm)
    def test_untracked_file(self):
        (self.root/'untracked').write_text('x')
        for arm64_vm in (False,True):
            with self.assertRaises(stage.StageError):stage.prepare(self.root,arm64_vm)
    def test_nested_path_rejected(self):
        with self.assertRaises(stage.StageError):stage.prepare(self.root/'src')
    def test_repeat_apply_rejected(self):
        stage.execute(self.root,True)
        with self.assertRaises(stage.StageError):stage.execute(self.root,True)
    def test_symlink_root(self):
        alias=Path(self.tmp.name)/'alias';alias.symlink_to(self.root,target_is_directory=True)
        with self.assertRaises(stage.StageError):stage.prepare(alias)
    def test_source_hashes(self):
        r,_=stage.prepare(self.root);self.assertEqual(len(r['files']),15)
        self.assertTrue(all(len(x)==64 for x in r['files'].values()))

    def test_vm_dry_run_and_exact_application(self):
        # Only these Git operations may occur in production staging. Fixture
        # construction commits are outside this wrapper; apply remains real.
        real_git=stage.git
        def bounded_git(root,*args,**kwargs):
            self.assertIn(args[0],('rev-parse','status','apply'))
            return real_git(root,*args,**kwargs)
        with patch.object(stage,'git',side_effect=bounded_git):
            planned,patch_text=stage.prepare(self.root,True)
            self.assert_clean()
            self.assertEqual(len(planned['files']),18)
            self.assertTrue(planned['arm64_vm'])
            self.assertEqual(set(planned['modified_files']),{stage.ANCHOR,stage.ARM64_NIX,stage.IMAGE_NIX,stage.FLAKE_NIX})
            self.assertEqual(planned['patch_sha256'],hashlib.sha256(patch_text.encode()).hexdigest())
            applied=stage.execute(self.root,True,True)
        self.assertTrue(applied['executed'])
        self.assertEqual(planned['patch_sha256'],applied['patch_sha256'])
        for key in ('kernel_built','kernel_booted','mapped_reclamation_enabled'):
            self.assertFalse(applied[key])
        self.assertEqual(self.git('rev-parse','HEAD').strip(),stage.PIN)
        self.assertEqual(self.git('diff','--cached','--name-only'),'')
        for path,data in stage.files(True).items():self.assertEqual((self.root/path).read_bytes(),data)
        for source,dest in stage.ARM64_VM_FILES.items():
            self.assertEqual((self.root/dest).read_bytes(),(stage.ROOT/source).read_bytes())
        for path,receipt in applied['modified_files'].items():
            self.assertEqual(receipt['before_blob'],stage.blob(self.originals[path].encode()))
            self.assertEqual(receipt['after_sha256'],hashlib.sha256((self.root/path).read_bytes()).hexdigest())
        self.assertEqual(set(self.git('diff','--name-only').splitlines()),set(applied['modified_files']))

        arm64=(self.root/stage.ARM64_NIX).read_text()
        arm_block=re.search(r'^  kextsArm64Build = .*?^  } // kextsOnlyArgs xnuHeadersArm64Build\);\n',arm64,re.M|re.S).group()
        for field,existing in (('buildTargets','IOPCIFamily'),('installKextNames','IOPCIFamily.kext')):
            values=re.search(r'\b'+field+r' = \[(.*?)\];',arm_block,re.S).group(1)
            self.assertEqual(re.findall(r'"([^"]+)"',values),['PDDRMDri.kext','DLMemoryProbe.kext',existing])
        # Removing only the two intended insertions must restore the whole file.
        self.assertEqual(arm64.replace('      "PDDRMDri.kext" "DLMemoryProbe.kext"\n',''),ARM64_FIXTURE)

        images=(self.root/stage.IMAGE_NIX).read_text()
        restored=images
        for name in ('kcArm64DebugBuild','kcArm64ReleaseBuild','imageArm64VirtMinimalBuild','imageArm64VirtMinimalReleaseBuild'):
            pattern=r'^      '+name+r' = .*?^      };\n'
            new_block=re.search(pattern,images,re.M|re.S).group()
            old_block=re.search(pattern,IMAGE_FIXTURE,re.M|re.S).group()
            self.assertNotEqual(new_block,old_block)
            if name.startswith('kc'):
                extras=re.search(r'extraKexts = \[(.*?)\];',new_block,re.S).group(1)
                self.assertEqual(re.findall(r'"([^"]+)"',extras),[
                    '${kextsArm64Build}/System/Library/Extensions/PDDRMDri.kext',
                    '${kextsArm64Build}/System/Library/Extensions/DLMemoryProbe.kext'])
                self.assertEqual(re.sub(r'        extraKexts = \[.*?        \];\n','',new_block,flags=re.S),old_block)
            else:
                self.assertEqual(new_block.count('dlmemtest=1'),1)
                addition=re.search(r' \+\+ \[\n.*?^        \];',new_block,re.M|re.S).group()
                self.assertEqual(addition,''' ++ [
          (pkgs.callPackage ../src/Kernel/Extensions/DLMemoryProbe/memory-probe-smoke.nix {
            src = ../src/Kernel/Extensions/DLMemoryProbe/lab;
            darwinCrossToolchain = arm64CrossToolchain;
            inherit nativeLd;
            libSystem = libSystemArm64Build;
          })
        ];''')
                self.assertEqual(new_block.replace(' dlmemtest=1','').replace(addition,';'),old_block)
            restored=restored.replace(new_block,old_block)
        # Includes board KCs, a board image, netboot and the nonminimal VIRT image.
        self.assertEqual(restored.count(', arm64CrossToolchain\n'),1)
        self.assertEqual(restored.count(', libSystemArm64Build\n'),1)
        self.assertEqual(restored.replace(', arm64CrossToolchain\n','').replace(', libSystemArm64Build\n',''),IMAGE_FIXTURE)
        self.assertEqual((self.root/stage.FLAKE_NIX).read_text(),'''# Synthetic import wiring.
          arm64 = import ./nix/arm64.nix {
            inherit pkgs;
          };
          imageContents = import ./nix/image-contents.nix {
            inherit arm64CrossToolchain libSystemArm64Build;
            inherit kcGpuBuild gpuDefaultPackages;
          };
''')
        state=self.git('diff')
        with self.assertRaises(stage.StageError):stage.execute(self.root,True,True)
        self.assertEqual(self.git('diff'),state)

    def test_vm_cli_requires_execute_and_ack(self):
        for args in (('--arm64-vm',),('--arm64-vm','--acknowledge-experimental')):
            code,report=self.cli(*args)
            self.assertEqual(code,0);self.assertTrue(report['arm64_vm']);self.assertFalse(report['executed'])
            self.assert_clean()
        code,report=self.cli('--arm64-vm','--execute')
        self.assertEqual(code,2);self.assertEqual(report['status'],'BLOCKED');self.assert_clean()
        code,report=self.cli('--arm64-vm','--execute','--acknowledge-experimental')
        self.assertEqual(code,0);self.assertTrue(report['executed'])
        self.assertIn('dlmemtest=1',(self.root/stage.IMAGE_NIX).read_text())
        code,report=self.cli('--arm64-vm','--execute','--acknowledge-experimental')
        self.assertEqual(code,2);self.assertEqual(report['status'],'BLOCKED')

    def test_default_does_not_require_vm_files(self):
        for path in stage.ARM64_VM_BLOBS:(self.root/path).unlink()
        with patch.object(stage,'PIN',self.commit_fixture()):
            report,_=stage.prepare(self.root)
            self.assertEqual(len(report['files']),15)
            with self.assertRaises(stage.StageError):stage.prepare(self.root,True)
        self.assert_clean()

    def test_vm_changed_file_blobs_rejected(self):
        for path in stage.ARM64_VM_BLOBS:
            with self.subTest(path=path):
                (self.root/path).write_text(self.originals[path]+'# unrelated drift\n')
                with patch.object(stage,'PIN',self.commit_fixture()):
                    with self.assertRaisesRegex(stage.StageError,'blob mismatch'):
                        stage.execute(self.root,True,True)
                    self.assert_clean()
                (self.root/path).write_text(self.originals[path]);self.commit_fixture()

    def test_vm_linked_file_rejected(self):
        for path in stage.ARM64_VM_BLOBS:
            with self.subTest(path=path):
                target=Path(self.tmp.name)/Path(path).name;target.write_text(self.originals[path])
                (self.root/path).unlink();(self.root/path).symlink_to(target)
                with patch.object(stage,'PIN',self.commit_fixture()):
                    with self.assertRaisesRegex(stage.StageError,'linked integration path'):
                        stage.prepare(self.root,True)
                    self.assert_clean()
                (self.root/path).unlink();(self.root/path).write_text(self.originals[path]);self.commit_fixture()

    def test_vm_anchor_drift_rejected_even_with_matching_blob(self):
        mutations=(
            (stage.ARM64_NIX,'  kextsArm64Build =','  renamedArm64Build ='),
            (stage.ARM64_NIX,'    buildTargets = [','    renamedTargets = ['),
            (stage.ARM64_NIX,'    installKextNames = [','    installKextNames = [\n    installKextNames = ['),
            (stage.ARM64_NIX,'  } // kextsOnlyArgs xnuHeadersArm64Build);','  });'),
            (stage.IMAGE_NIX,'      kcArm64DebugBuild =','      renamedDebugBuild ='),
            (stage.IMAGE_NIX,'      kcArm64ReleaseBuild =','      renamedReleaseBuild ='),
            (stage.IMAGE_NIX,'        kexts = kextsArm64Build;','        kexts = otherKexts;'),
            (stage.IMAGE_NIX,'      imageArm64VirtMinimalBuild =','      renamedMinimalBuild ='),
            (stage.IMAGE_NIX,'        extraPackages = [','        renamedPackages = ['),
            (stage.IMAGE_NIX,', darwinCrossToolchain\n',', otherToolchain\n'),
            (stage.IMAGE_NIX,', libSystemBuild\n',', otherSystem\n'),
            (stage.IMAGE_NIX,DEBUG_ARGS,DEBUG_ARGS+' changed=1'),
            (stage.IMAGE_NIX,RELEASE_ARGS,RELEASE_ARGS+' changed=1'),
            (stage.IMAGE_NIX,IMAGE_FIXTURE,IMAGE_FIXTURE+kc_fixture('kcArm64ReleaseBuild','kernelArm64VirtBuild')),
            (stage.FLAKE_NIX,'          imageContents = import','          renamedContents = import'),
        )
        for path,old,new in mutations:
            with self.subTest(path=path,anchor=old[:60]):
                changed=self.originals[path].replace(old,new)
                (self.root/path).write_text(changed)
                with patch.object(stage,'PIN',self.commit_fixture()),patch.dict(stage.ARM64_VM_BLOBS,{path:stage.blob(changed.encode())}):
                    with self.assertRaisesRegex(stage.StageError,'anchor mismatch|block.*mismatch'):
                        stage.execute(self.root,True,True)
                    self.assert_clean()
                (self.root/path).write_text(self.originals[path]);self.commit_fixture()

    def test_vm_rechecks_blobs_before_application(self):
        real_prepare=stage.prepare
        def drift_after_prepare(*args,**kwargs):
            result=real_prepare(*args,**kwargs)
            stage.ARM64_VM_BLOBS[stage.IMAGE_NIX]='0'*40
            return result
        with patch.object(stage,'prepare',side_effect=drift_after_prepare):
            with self.assertRaisesRegex(stage.StageError,'blob mismatch'):
                stage.execute(self.root,True,True)
        self.assert_clean()

    def test_vm_missing_extra_sources_do_not_affect_default_mode(self):
        # Use a separate lab source fixture; never rename or write shared sources.
        source_root=Path(self.tmp.name)/'lab';source_root.mkdir()
        origins=[('integrations/puredarwin/DLMemoryProbe/'+name) for name in stage.ADAPTER_FILES]
        origins+=list(stage.LAB_FILES)+list(stage.ARM64_VM_FILES)
        for origin in origins:
            dest=source_root/origin;dest.parent.mkdir(parents=True,exist_ok=True)
            dest.write_bytes((stage.ROOT/origin).read_bytes())
        with patch.object(stage,'ROOT',source_root):
            for origin in stage.ARM64_VM_FILES:
                with self.subTest(source=origin):
                    path=source_root/origin;data=path.read_bytes();path.unlink()
                    report,_=stage.prepare(self.root)
                    self.assertEqual(len(report['files']),15)
                    with self.assertRaisesRegex(stage.StageError,'Missing/linked ARM64 VM source'):
                        stage.execute(self.root,True,True)
                    self.assert_clean()
                    path.write_bytes(data)
if __name__=='__main__':unittest.main()
