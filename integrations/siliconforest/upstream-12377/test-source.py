#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Selected actual-source controls. No network, M1, device writes or checkout edits."""
from pathlib import Path
import hashlib,json,os,plistlib,subprocess,tempfile,struct,zlib,uuid

here=Path(__file__).resolve().parent
repo=here.parents[2]
upstream=repo/'sources/puredarwin-rebase'
manifest=json.loads((here/'manifest.json').read_text())

def git(*args):
    return subprocess.check_output(['git','-C',str(upstream),*args])

def block(text,needle):
    start=text.index(needle);at=text.index('{',start);depth=0
    for end in range(at,len(text)):
        depth+=(text[end]=='{')-(text[end]=='}')
        if not depth:return text[start:end+1]
    raise AssertionError(needle)

def compile_run(root,name,fixture,values,cpp=False,extra=()):
    source=(here/fixture).read_text()
    for key,value in values.items():source=source.replace('@'+key+'@',value)
    path=root/(name+('.cpp' if cpp else '.c'));path.write_text(source)
    subprocess.run([os.environ.get('CXX' if cpp else 'CC','clang++' if cpp else 'clang'),
        '-std=c++17' if cpp else '-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
        '-Wno-unused-parameter','-Wno-unused-function','-fsanitize=address,undefined',
        '-fno-sanitize-recover=all',*extra,str(path),'-o',str(root/name)],check=True)
    subprocess.run([str(root/name)],check=True,timeout=15)

def fixture(root,rev,paths):
    for name in paths:
        path=root/name
        if path.exists():continue
        result=subprocess.run(['git','-C',str(upstream),'show',rev+':'+name],capture_output=True)
        if not result.returncode:path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(result.stdout)

def gpt_control(root,script):
    # Entirely synthetic regular files. Never pass a device, real disk data or identifiers.
    size=16<<20;count=128;table=bytearray(count*128);diskid=uuid.uuid4().bytes_le
    first,last=2048,4095
    struct.pack_into('<16s16sQQQ72s',table,0,uuid.uuid4().bytes_le,uuid.uuid4().bytes_le,first,last,0,b'fixture'.decode().encode('utf-16-le'))
    def header(me,alt,table_lba):
        out=bytearray(struct.pack('<8sIIIIQQQQ16sQIII',b'EFI PART',0x10000,92,0,0,me,alt,34,size//512-34,diskid,table_lba,count,128,zlib.crc32(table)))
        struct.pack_into('<I',out,16,zlib.crc32(out));return out.ljust(512,b'\0')
    image=root/'gpt-synthetic.img'
    def seed(unaligned=False):
        with image.open('wb') as f:
            f.truncate(size);f.seek(512);f.write(header(1,size//512-1,2));f.write(table)
            f.seek(size-33*512);f.write(table);f.write(header(size//512-1,1,size//512-33))
            f.seek(first*512);f.write(b'PAYLOAD-'+bytes(range(256))*8)
        if unaligned:
            with image.open('r+b') as f:f.seek(2*512+32);f.write(struct.pack('<Q',first+1))
    seed();payload=image.read_bytes()[first*512:(last+1)*512]
    subprocess.run(['python3',str(script),str(image)],check=True)
    data=image.read_bytes();assert data[first*512:(last+1)*512]==payload
    decoded=[]
    for offset in [4096,size-4096]:
        h=data[offset:offset+4096];assert h[:8]==b'EFI PART'
        crc=struct.unpack_from('<I',h,16)[0];assert zlib.crc32(h[:16]+bytes(4)+h[20:92])==crc
        me,alt,fu,lu=struct.unpack_from('<QQQQ',h,24);assert me==offset//4096
        entries,n,entrysize,ecrc=struct.unpack_from('<QIII',h,72)
        t=data[entries*4096:entries*4096+n*entrysize];assert zlib.crc32(t)==ecrc
        assert struct.unpack_from('<QQ',t,32)==(first//8,(last+1)//8-1)
        assert fu==6 and lu==size//4096-6 and h[56:72]==diskid
        decoded.append(t)
    assert decoded[0]==decoded[1]
    seed(True);before=image.read_bytes();result=subprocess.run(['python3',str(script),str(image)],capture_output=True)
    assert result.returncode and b'not 4 KiB aligned' in result.stderr and image.read_bytes()==before
    print('PASS unmodified GPT converter on synthetic regular image: both header/table CRCs, byte spans/payload preserved, unaligned refusal before writes')
    print('LIMIT upstream converter assumes valid source GPT/standard layout; not a general disk repair/installer or authorization to write any device')

with tempfile.TemporaryDirectory(prefix='sf-upstream-12377-') as tmp:
    root=Path(tmp);tree=root/'selected';tree.mkdir()
    for item in manifest['ports']:
        patch=here/'patches'/item['patch'];assert hashlib.sha256(patch.read_bytes()).hexdigest()==item['patch_sha256']
        fixture(tree,manifest['base'],item['files'])
        for name,digest in item['before'].items():
            assert (hashlib.sha256((tree/name).read_bytes()).hexdigest() if (tree/name).exists() else None)==digest
        subprocess.run(['git','apply','--check',str(patch)],cwd=tree,check=True,capture_output=True)
        subprocess.run(['git','apply',str(patch)],cwd=tree,check=True,capture_output=True)
        for name,digest in item['after'].items():
            assert (hashlib.sha256((tree/name).read_bytes()).hexdigest() if (tree/name).exists() else None)==digest
    print('PASS seven selected source ports apply to verified c713 snapshots, before/after hashes exact')
    # Full original commits, including pre-reorganisation Nix hunks, in chronological order.
    full=root/'full';full.mkdir()
    for rev in ['2617f566','aaf3604d','0163b2db','a25f0365','aa82116c','a08eb9b6','63a61e89','ca1fd5ed']:
        names=git('diff','--no-renames','--name-only',rev+'^',rev).decode().splitlines();fixture(full,manifest['base'],names)
        subprocess.run(['git','apply','--check','-'],input=git('diff','--no-renames',rev+'^',rev),cwd=full,check=True,capture_output=True)
        subprocess.run(['git','apply','-'],input=git('diff','--no-renames',rev+'^',rev),cwd=full,check=True,capture_output=True)
    assert (full/'nix/arm64.nix').read_bytes()==git('show',manifest['base']+':nix/arm64.nix')
    print('PASS all eight original commits clean in sequence; a08/63 git argument changes cancel exactly; no rebase/checkout mutation')
    rel='src/Userspace/ext4util/ext4.util.c'
    frozen=git('show',manifest['base']+':'+rel).decode();patched=(tree/rel).read_text()
    sb=block(patched,'struct ext4_super_block {')+';'
    for label,text in [('frozen',frozen),('patched',patched)]:
        compile_run(root,'ext4-'+label,'host-ext4util.c',{'SUPERBLOCK':sb,'READER':block(text,'static int\nread_superblock(')},extra=('-DFROZEN',) if label=='frozen' else ())
    pci=(tree/'src/Kernel/Extensions/IOPCIFamily/IOPCIConfigurator.cpp').read_text()
    fallback=pci[pci.index('// The link-active bit is clear'):pci.index('\n\t\tif ((kPCIDeviceStateNoLink',pci.index('// The link-active bit is clear'))]
    usb=(tree/'src/Kernel/Extensions/RavynXHCIPort/RavynXHCIPort.cpp').read_text()
    heap=usb[usb.index('template <typename T> struct SFXferTable'):usb.index('\nbool RavynXHCIPort::bulkTransfer(',usb.index('template <typename T> struct SFXferTable'))]
    walk=usb[usb.index('    bool foundMSC = false;'):usb.index('    IOFree(cfgBuf, totalLen);',usb.index('    bool foundMSC = false;'))]
    compile_run(root,'pci-usb','host-pci-usb.cpp',{'FALLBACK':fallback,'HEAP_HELPER':heap,'DESCRIPTOR_WALK':walk},cpp=True)
    iad=plistlib.loads((tree/'src/Kernel/Extensions/IOUSBFamily/IOUSBCompositeDriver/Info.plist').read_bytes())['IOKitPersonalities']['IOUSBCompositeDriverIAD']
    assert (iad['bDeviceClass'],iad['bDeviceSubClass'],iad['bDeviceProtocol'],iad['IOClass'])==(239,2,1,'IOUSBCompositeDriver')
    glue=(tree/'src/Kernel/fbsdkpi/sf/sf_fbsd_glue.c').read_text();glue=glue[glue.index('int\npci_disable_busmaster'):]
    compile_run(root,'glue','host-glue.c',{'GLUE':glue})
    app=(tree/'src/Userspace/sfres/sfres.c').read_text()
    app='\n'.join(line for line in app.splitlines() if not line.startswith('#include <Core'))
    # unistd's real sleep declaration must precede the fixture's macro.
    app=app.replace('#include <unistd.h>','')
    compile_run(root,'sfres','host-sfres.c',{'APP':app})
    gpt_control(root,tree/'nix/lib/gpt4k.py')
    print('LIMIT source/host fixtures only; no native dock, DMA, enumeration, display mode switch or installed utility proof')
