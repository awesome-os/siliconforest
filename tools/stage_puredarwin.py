#!/usr/bin/env python3
"""Plan/stage the experimental RAM probe in an EXACT, CLEAN PureDarwin checkout.

No fetching, building, installing, booting or Git commits. Dry run by default.
Execution requires two explicit switches. Stage in a separate healthy checkout,
never in the baseline checkout or on the filesystem under recovery.
Optional --arm64-vm also stages the pinned VIRT build/KC/image source selections
and the guest smoke-test recipe and sources;
it does not run those recipes or start the driver.
"""
from __future__ import annotations
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
PIN='e7aa00a60f0b56ef35aa24252e81467787bf71ea'
HEADER='src/Kernel/fbsdkpi/pd_dri_cdev.h'
HEADER_BLOB='395083ab8450653ede86772e27e1d3eff4e4b759'
ANCHOR='src/Kernel/Extensions/CMakeLists.txt'
ANCHOR_BLOB='5715a0992837e3d823bf65b05edf24a5a838d1bf'
ARM64_NIX='nix/arm64.nix'
IMAGE_NIX='nix/image-contents.nix'
FLAKE_NIX='flake.nix'
ARM64_VM_BLOBS={
    ARM64_NIX:'2f219188a26bd26794ff1c9e33637b9d24be5329',
    IMAGE_NIX:'0472611c9c36ba65bb9c841519fb21ad829058f2',
    FLAKE_NIX:'c9a99817b7777ebba81d994fc92ab89431fbf69a',
}
DEST='src/Kernel/Extensions/DLMemoryProbe'
MARKER='# DARWIN_LAB_EXPERIMENTAL_RAM_PROBE_V1'
LAB_FILES=(
    'contracts/buffer_lifetime.h','contracts/buffer_lifetime.c',
    'core/backing_store.h','core/backing_store.c',
    'core/mapping_ticket.h','core/mapping_ticket.c',
    'probe/memory_probe.h','probe/memory_probe.c',
    'probe/memory_probe_protocol.h','probe/memory_probe_ioctl.h')
ADAPTER_FILES=('CMakeLists.txt','Info.plist','DLMemoryProbe.cpp','provider_iokit.cpp','provider_iokit.h')
ARM64_VM_FILES={
    'integrations/puredarwin/memory-probe-smoke.nix':f'{DEST}/memory-probe-smoke.nix',
    'userspace/memory_probe_smoke.c':f'{DEST}/lab/userspace/memory_probe_smoke.c',
    'userspace/memory_probe_result.h':f'{DEST}/lab/userspace/memory_probe_result.h',
}
class StageError(ValueError): pass

def git(root:Path,*args:str,input_text:str|None=None)->str:
    p=subprocess.run(['git','-c','core.hooksPath=/dev/null','-C',str(root),*args],
                     input=input_text,capture_output=True,text=True,timeout=60,check=False)
    if p.returncode:raise StageError('Git operation failed: '+p.stderr.strip())
    return p.stdout

def blob(data:bytes)->str:
    return hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()

def files(arm64_vm:bool=False)->dict[str,bytes]:
    output={}
    for name in ADAPTER_FILES:
        source=ROOT/'integrations/puredarwin/DLMemoryProbe'/name
        if source.is_symlink() or not source.is_file():raise StageError('Missing/linked adapter source: '+name)
        output[f'{DEST}/{name}']=source.read_bytes()
    for path in LAB_FILES:
        source=ROOT/path
        if source.is_symlink() or not source.is_file():raise StageError('Missing/linked lab source: '+path)
        output[f'{DEST}/lab/{path}']=source.read_bytes()
    if arm64_vm:
        for path,dest in ARM64_VM_FILES.items():
            source=ROOT/path
            if source.is_symlink() or not source.is_file():raise StageError('Missing/linked ARM64 VM source: '+path)
            output[dest]=source.read_bytes()
    return output

def verify(checkout:Path,arm64_vm:bool=False)->Path:
    if checkout.is_symlink():raise StageError('Checkout path must not be a symlink')
    root=checkout.resolve(strict=True)
    if not root.is_dir() or Path(git(root,'rev-parse','--show-toplevel').strip()).resolve()!=root:
        raise StageError('Pass the checkout root, not a nested directory')
    if git(root,'rev-parse','HEAD').strip()!=PIN:raise StageError('Checkout is not at the supported pinned commit')
    if git(root,'status','--porcelain','--untracked-files=all').strip():raise StageError('Checkout must be clean, including untracked files')
    expected={HEADER:HEADER_BLOB,ANCHOR:ANCHOR_BLOB}
    if arm64_vm:expected.update(ARM64_VM_BLOBS)
    for path,expected_blob in expected.items():
        p=root/path
        if not p.is_file() or any(x.is_symlink() for x in (p,*p.parents)):
            raise StageError('Missing or linked integration path: '+path)
        if blob(p.read_bytes())!=expected_blob:raise StageError('Pinned source blob mismatch: '+path)
    if (root/DEST).exists() or (root/DEST).is_symlink():raise StageError('Destination already exists')
    if MARKER in (root/ANCHOR).read_text():raise StageError('Already staged')
    return root

def replace_once(text:str,old:str,new:str,where:str)->str:
    if text.count(old)!=1:raise StageError('Pinned source anchor mismatch: '+where)
    return text.replace(old,new,1)

def edit_block(text:str,start:str,end:str,edits:tuple[tuple[str,str],...],where:str)->str:
    """Edit only one named, pinned Nix block; never replace a shared line globally."""
    if text.count(start)!=1:raise StageError('Pinned source block mismatch: '+where)
    before,_,tail=text.partition(start)
    block,separator,after=tail.partition(end)
    if not separator:raise StageError('Pinned source block end mismatch: '+where)
    for old,new in edits:block=replace_once(block,old,new,where)
    return before+start+block+end+after

def arm64_vm_changes(root:Path)->dict[str,str]:
    arm64=(root/ARM64_NIX).read_text()
    bundle_line='      "PDDRMDri.kext" "DLMemoryProbe.kext"\n'
    arm64=edit_block(arm64,'  kextsArm64Build = mkPureDarwinBuild ({\n',
                     '  } // kextsOnlyArgs xnuHeadersArm64Build);\n',
                     tuple((line,line+bundle_line) for line in
                           ('    buildTargets = [\n','    installKextNames = [\n')),ARM64_NIX)
    images=(root/IMAGE_NIX).read_text()
    for name in ('darwinCrossToolchain','libSystemBuild'):
        added='arm64CrossToolchain' if name=='darwinCrossToolchain' else 'libSystemArm64Build'
        images=replace_once(images,f', {name}\n',f', {name}\n, {added}\n',IMAGE_NIX+':'+name)
    kexts_line='        kexts = kextsArm64Build;\n'
    extras=('        extraKexts = [\n'
            '          "${kextsArm64Build}/System/Library/Extensions/PDDRMDri.kext"\n'
            '          "${kextsArm64Build}/System/Library/Extensions/DLMemoryProbe.kext"\n'
            '        ];\n')
    for name in ('kcArm64DebugBuild','kcArm64ReleaseBuild'):
        images=edit_block(images,f'      {name} = pkgs.callPackage ./pkgs/toolchain/kc-arm64.nix {{\n',
                          '      };\n',((kexts_line,kexts_line+extras),),IMAGE_NIX+':'+name)
    smoke_package=(' ++ [\n'
                   '          (pkgs.callPackage ../src/Kernel/Extensions/DLMemoryProbe/memory-probe-smoke.nix {\n'
                   '            src = ../src/Kernel/Extensions/DLMemoryProbe/lab;\n'
                   '            darwinCrossToolchain = arm64CrossToolchain;\n'
                   '            inherit nativeLd;\n'
                   '            libSystem = libSystemArm64Build;\n'
                   '          })\n'
                   '        ];\n')
    for name,args,packages in (
        ('imageArm64VirtMinimalBuild','-v debug=0x218 -nogzalloc_mode keepsyms=1 serial=3 gopconsole=1 pdtrace=1 serial_video_mirror=1 no_interrupt_masked_debug=1',
         'zshArm64Build libiconvArm64Build toyboxArm64Build asmjitTestArm64Build'),
        ('imageArm64VirtMinimalReleaseBuild','-v serial=3 ahci_debug=1 kext=0xffff io=0xffff',
         'zshArm64Build libiconvArm64Build toyboxArm64Build'),
    ):
        package_line=f'        extraPackages = [ {packages} ]'
        images=edit_block(images,f'      {name} = pkgs.callPackage ../image.nix {{\n','      };\n',
                          ((f'        bootArgs = "{args}";\n',
                            f'        bootArgs = "{args} dlmemtest=1";\n'),
                           (package_line+';\n',package_line+smoke_package)),IMAGE_NIX+':'+name)
    flake=(root/FLAKE_NIX).read_text()
    image_import='          imageContents = import ./nix/image-contents.nix {\n'
    flake=replace_once(flake,image_import,image_import+'            inherit arm64CrossToolchain libSystemArm64Build;\n',FLAKE_NIX)
    return {ARM64_NIX:arm64,IMAGE_NIX:images,FLAKE_NIX:flake}

def prepare(checkout:Path,arm64_vm:bool=False)->tuple[dict,str]:
    root=verify(checkout,arm64_vm)
    additions=files(arm64_vm)
    old=(root/ANCHOR).read_text()
    new=old+('' if old.endswith('\n') else '\n')+f'\n{MARKER}\nadd_subdirectory(DLMemoryProbe)\n'
    changes={ANCHOR:new}
    if arm64_vm:changes.update(arm64_vm_changes(root))
    patch=''
    modified_files={}
    for path,text in changes.items():
        original=(root/path).read_bytes()
        patch+=''.join(difflib.unified_diff(original.decode('utf-8').splitlines(keepends=True),
                                          text.splitlines(keepends=True),fromfile='a/'+path,tofile='b/'+path))
        modified_files[path]={'before_blob':blob(original),'after_sha256':hashlib.sha256(text.encode('utf-8')).hexdigest()}
    for path,data in sorted(additions.items()):
        text=data.decode('utf-8')
        if not text.endswith('\n'):raise StageError('Staged source must end in newline: '+path)
        patch+=''.join(difflib.unified_diff([],text.splitlines(keepends=True),fromfile='/dev/null',tofile='b/'+path))
    git(root,'apply','--check','--whitespace=error-all','-',input_text=patch)
    report={'status':'READY_TO_STAGE_UNBUILT_SOURCE','checkout':str(root),'required_commit':PIN,
            'header_blob':HEADER_BLOB,'patch_sha256':hashlib.sha256(patch.encode()).hexdigest(),
            'files':{p:hashlib.sha256(d).hexdigest() for p,d in sorted(additions.items())},
            'arm64_vm':arm64_vm,'modified_files':modified_files,
            'parent_change':ANCHOR,'kernel_built':False,'kernel_booted':False,
            'mapped_reclamation_enabled':False,'executed':False,
            'limits':['Staging does not build, link, include in a kernel collection, or activate the driver.',
                      'This staging command does not compile the C++ IOKit adapter against the pinned SDK.',
                      'A changed Git checkout must include these new files in the Nix source snapshot.',
                      'Mapped backing remains quarantined until reboot; a mapped smoke test must report BLOCKED.']}
    if arm64_vm:
        report['limits'] += [
            'ARM64 VM source recipes select both bundles for build/install and the VIRT DEBUG/RELEASE KCs; no resulting artifact is verified.',
            'The smoke-test recipe and sources are staged and selected in both minimal VM images; this command does not build or run the executable.',
            'Only minimal VM image boot args gain dlmemtest=1. Explicit pdkext load activation is still required; no startup job is added.',
        ]
    return report,patch

def execute(checkout:Path,acknowledge:bool,arm64_vm:bool=False)->dict:
    if not acknowledge:raise StageError('Explicit experimental acknowledgement is required')
    report,patch=prepare(checkout,arm64_vm)
    # Recheck the clean pin and all selected blobs immediately before applying.
    root=verify(checkout,arm64_vm)
    git(root,'apply','--whitespace=error-all','-',input_text=patch)
    # No git-add/commit/build: leave a reviewable, intentional source diff.
    report.update(status='STAGED_UNBUILT_SOURCE',executed=True)
    return report

def main(argv:list[str]|None=None)->int:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--checkout',type=Path,required=True)
    p.add_argument('--execute',action='store_true')
    p.add_argument('--acknowledge-experimental',action='store_true')
    p.add_argument('--arm64-vm',action='store_true',help='Also stage pinned VIRT build/KC selections, minimal VM boot args and guest smoke sources; no build or activation')
    a=p.parse_args(argv)
    try:
        if a.execute:report=execute(a.checkout,a.acknowledge_experimental,a.arm64_vm)
        else:report,_=prepare(a.checkout,a.arm64_vm)
        print(json.dumps(report,indent=2));return 0
    except (OSError,ValueError,subprocess.SubprocessError) as e:
        print(json.dumps({'status':'BLOCKED','error':str(e),'kernel_built':False}),file=sys.stderr);return 2
if __name__=='__main__':raise SystemExit(main())
