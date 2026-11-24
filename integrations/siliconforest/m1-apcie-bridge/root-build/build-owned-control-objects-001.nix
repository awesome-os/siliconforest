# Original control helper/provider OBJECT package only. No kext, KC, image or boot.
let
  pin = "e7aa00a60f0b56ef35aa24252e81467787bf71ea";
  pd = (import ../../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  p = pd.packages.x86_64-linux;
  ownerReceiptText = builtins.readFile ../native_owned/source-validation-001.json;
  coreReceiptText = builtins.readFile ../private_lifecycle/source-validation-001.json;
  helperReceiptText = builtins.readFile ../owned_control/source-validation-001.json;
  helper = builtins.fromJSON helperReceiptText;
  owner = builtins.fromJSON ownerReceiptText;
  core = builtins.fromJSON coreReceiptText;
  files = [
    { name = "native_owned/owned.c"; path = ../native_owned/owned.c; sha = owner.files_sha256."owned.c"; }
    { name = "native_owned/owned.h"; path = ../native_owned/owned.h; sha = owner.files_sha256."owned.h"; }
    { name = "native_owned/owned_types.h"; path = ../native_owned/owned_types.h; sha = owner.files_sha256."owned_types.h"; }
    { name = "native_owned/native_owned.cpp"; path = ../native_owned/native_owned.cpp; sha = owner.files_sha256."native_owned.cpp"; }
    { name = "native_owned/native_owned.h"; path = ../native_owned/native_owned.h; sha = owner.files_sha256."native_owned.h"; }
    { name = "native_owned/lifecycle_bridge.h"; path = ../native_owned/lifecycle_bridge.h; sha = owner.files_sha256."lifecycle_bridge.h"; }
    { name = "owned_control/owned_control.c"; path = ../owned_control/owned_control.c; sha = helper.files_sha256."owned_control.c"; }
    { name = "owned_control/owned_control.h"; path = ../owned_control/owned_control.h; sha = helper.files_sha256."owned_control.h"; }
    { name = "owned_control/owned_pmap_bridge.h"; path = ../owned_control/owned_pmap_bridge.h; sha = helper.files_sha256."owned_pmap_bridge.h"; }
    { name = "owned_control/owned_control_native.cpp"; path = ../owned_control/owned_control_native.cpp; sha = helper.files_sha256."owned_control_native.cpp"; }
    { name = "private_lifecycle/lifecycle.c"; path = ../private_lifecycle/lifecycle.c; sha = core.files_sha256."lifecycle.c"; }
    { name = "private_lifecycle/lifecycle.h"; path = ../private_lifecycle/lifecycle.h; sha = core.files_sha256."lifecycle.h"; }
  ];
  inputs = builtins.toJSON {
    scope = "Original owned-control helper and native-owner ARM64 objects only; effect backend unavailable";
    inherit pin;
    helper_source_set_sha256 = helper.source_set_sha256;
    required_additional_KPI = "Unsupported: kernel_pmap and pmap_find_phys; no module built";
    owner_source_set_sha256 = owner.source_set_sha256;
    core_source_set_sha256 = core.source_set_sha256;
    production_files = map (f: { inherit (f) name sha; }) files;
    toolchain = "${p.arm64-cross-toolchain}";
    linker = "${p.native-ld}";
    kernel_headers = "${p.xnu-headers-arm64}";
    sdk = "${p.apple-sdk}";
    test_sdk_included = false;
    module_or_image_built = false;
  };
in
assert pd.rev == pin;
assert builtins.hashString "sha256" helperReceiptText == "9844eb28efb1190097369b419b90aeeebb605f4aafa147dbd908a7841b3d2c57";
assert helper.source_set_sha256 == "a5859402f6211e0f0a8941898b5365667206bb175a3cf80f2b42a1641bbaca08";
assert builtins.hashString "sha256" ownerReceiptText == "0ee2101c9cda5cf282c3db48b7640cef98146c3b04724e3fe59138aaad772434";
assert builtins.hashString "sha256" coreReceiptText == "c585fd8b80479975a46ad19c83b7610a53f3a4a1a420241cbe0022a2d1fb82fb";
assert owner.source_set_sha256 == "b2e858d89559763325de955553b40d059dc847a27e05696b214262e672cca966";
pkgs.runCommand "darwin-lab-bcm4378-owned-control-objects-001" {
  nativeBuildInputs = [ p.arm64-cross-toolchain p.native-ld pkgs.python3 ];
  preferLocalBuild = true;
  allowSubstitutes = false;
} ''
  mkdir -p source/native_owned source/private_lifecycle source/owned_control
  ${pkgs.lib.concatMapStringsSep "\n" (f: ''
    cp ${f.path} source/${f.name}
    echo '${f.sha}  source/${f.name}' | sha256sum -c -
  '') files}
  export DARWIN_SDK_ROOT=${p.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
  headers=${p.xnu-headers-arm64}/System/Library/Frameworks/Kernel.framework/Versions/A
  availability=${pd.outPath}/src/Libraries/AvailabilityVersions/include
  common=(-Wall -Wextra -Werror -Wpedantic -Wframe-larger-than=1024 -fstack-usage
          -fno-stack-protector -fno-common -DKERNEL -DKERNEL_PRIVATE -DTARGET_OS_OSX
          -isystem "$headers/Headers" -isystem "$headers/PrivateHeaders" -isystem "$availability" -O2)
  ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang -std=c11 -ffreestanding -fno-builtin "''${common[@]}" \
    -MD -MF lifecycle.d -c source/private_lifecycle/lifecycle.c -o lifecycle.o
  ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang -std=c11 -ffreestanding -fno-builtin "''${common[@]}" \
    -MD -MF owned.d -c source/native_owned/owned.c -o owned.o
  ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang++ -std=c++17 -fno-exceptions -fno-rtti -fapple-kext "''${common[@]}" \
    -MD -MF native_owned.d -c source/native_owned/native_owned.cpp -o native_owned.o
  ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang -std=c11 -ffreestanding -fno-builtin "''${common[@]}" \
    -MD -MF owned_control.d -c source/owned_control/owned_control.c -o owned_control.o
  ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang++ -std=c++17 -fno-exceptions -fno-rtti -fapple-kext "''${common[@]}" \
    -include "$headers/PrivateHeaders/mach/vm_types.h" -include "$headers/PrivateHeaders/arm/locks.h" \
    -MD -MF owned_control_native.d -c source/owned_control/owned_control_native.cpp -o owned_control_native.o
  ${p.native-ld}/bin/ld -r -arch arm64 -Z -platform_version macos 26.5 26.5 \
    -o owned_control_provider.o lifecycle.o owned.o native_owned.o owned_control.o owned_control_native.o
  ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-nm -u owned_control_provider.o > undefined.txt
  ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-nm -g --defined-only owned_control_provider.o > defined.txt
  python3 - <<'PY'
from pathlib import Path
import struct
raw=Path('owned_control_provider.o').read_bytes()
magic,cpu,sub,kind,ncmds,cmdbytes,flags,reserved=struct.unpack_from('<8I',raw)
assert (magic,cpu,kind)==(0xfeedfacf,0x100000c,1), 'ARM64 MH_OBJECT required'
actual={line.split()[-1] for line in Path('undefined.txt').read_text().splitlines() if line.strip()}
expected={'_IOLockGetMachLock','_kernel_pmap','_pmap_find_phys','_IOLockAlloc','_IOLockLock','_IOLockUnlock','_IOMallocZero','_PAGE_SHIFT_CONST',
          '__ZN24IOBufferMemoryDescriptor22inTaskWithPhysicalMaskEP4taskjyy','_bzero',
          '_kernel_task','_memcmp','_memcpy','_memmove','_page_size'}
assert actual==expected,(actual,expected)
defined={line.split()[-1] for line in Path('defined.txt').read_text().splitlines() if line.strip()}
assert {'_bpl_init','_bno_init','_bno_native_pin_empty','_bno_native_configure','_boc_native_derive_and_configure','_boc_derive','_boc_boot_configure'}<=defined
for dep in Path('.').glob('*.d'):
    assert '/test_sdk/' not in dep.read_text(), 'stand-in headers must not enter native build'
for su in Path('.').glob('*.su'):
    for line in su.read_text().splitlines():
        name,frame,mode=line.split('\t')
        assert mode=='static' and int(frame)<=1024,(name,frame,mode)
off=32
for _ in range(ncmds):
    cmd,size=struct.unpack_from('<II',raw,off)
    assert size>=8 and off+size<=32+cmdbytes
    if cmd==0x19:
        sections=struct.unpack_from('<I',raw,off+64)[0]
        for i in range(sections):
            name=raw[off+72+i*80:off+88+i*80].split(b'\0',1)[0]
            assert name not in (b'__mod_init_func',b'__mod_term_func'),name
    off+=size
assert off==32+cmdbytes
PY
  mkdir -p "$out/lib" "$out/share/objects" "$out/share/inputs" "$out/share/validation"
  cp owned_control_provider.o "$out/lib/"
  cp lifecycle.o owned.o native_owned.o owned_control.o owned_control_native.o "$out/share/objects/"
  cp -R source/native_owned source/private_lifecycle source/owned_control "$out/share/inputs/"
  cp *.d *.su undefined.txt defined.txt "$out/share/validation/"
  cat > "$out/share/build-inputs.json" <<'JSON'
${inputs}
JSON
  (cd "$out"; sha256sum lib/owned_control_provider.o share/objects/*.o) > "$out/share/sha256.txt"
''
