# Original control helper/provider OBJECT package only. No kext, KC, image or boot.
let
  pin = "e7aa00a60f0b56ef35aa24252e81467787bf71ea";
  pd = (import ../../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  p = pd.packages.x86_64-linux;
  nativeReceiptText = builtins.readFile ../native_backing/source-validation-001.json;
  coreReceiptText = builtins.readFile ../private_lifecycle/source-validation-001.json;
  helperReceiptText = builtins.readFile ../control_protection/source-validation-002.json;
  helper = builtins.fromJSON helperReceiptText;
  native = builtins.fromJSON nativeReceiptText;
  core = builtins.fromJSON coreReceiptText;
  files = [
    { name = "native_backing/backing.c"; path = ../native_backing/backing.c; sha = native.files_sha256."backing.c"; }
    { name = "native_backing/backing.h"; path = ../native_backing/backing.h; sha = native.files_sha256."backing.h"; }
    { name = "native_backing/lifecycle_bridge.h"; path = ../native_backing/lifecycle_bridge.h; sha = native.files_sha256."lifecycle_bridge.h"; }
    { name = "native_backing/native_iokit.cpp"; path = ../native_backing/native_iokit.cpp; sha = native.files_sha256."native_iokit.cpp"; }
    { name = "native_backing/native_iokit.h"; path = ../native_backing/native_iokit.h; sha = native.files_sha256."native_iokit.h"; }
    { name = "control_protection/control_protection.c"; path = ../control_protection/control_protection.c; sha = helper.files_sha256."control_protection.c"; }
    { name = "control_protection/control_protection.h"; path = ../control_protection/control_protection.h; sha = helper.files_sha256."control_protection.h"; }
    { name = "control_protection/native_pmap_bridge.h"; path = ../control_protection/native_pmap_bridge.h; sha = helper.files_sha256."native_pmap_bridge.h"; }
    { name = "control_protection/control_native.cpp"; path = ../control_protection/control_native.cpp; sha = helper.files_sha256."control_native.cpp"; }
    { name = "private_lifecycle/lifecycle.c"; path = ../private_lifecycle/lifecycle.c; sha = core.files_sha256."lifecycle.c"; }
    { name = "private_lifecycle/lifecycle.h"; path = ../private_lifecycle/lifecycle.h; sha = core.files_sha256."lifecycle.h"; }
  ];
  inputs = builtins.toJSON {
    scope = "Original control helper and native backing ARM64 relocatable objects only, no target execution";
    inherit pin;
    helper_source_set_sha256 = helper.source_set_sha256;
    required_additional_KPI = "Unsupported: kernel_pmap and pmap_find_phys; no module built";
    native_source_set_sha256 = native.source_set_sha256;
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
assert builtins.hashString "sha256" helperReceiptText == "ef00f73d46c11167751f0f274d323fac96c994a3ff267152b800af81644415bb";
assert helper.source_set_sha256 == "e55ce2e909b02946e15f31f934d59f0e2d660fc8a9b9b981398d19b08b2c849a";
assert builtins.hashString "sha256" nativeReceiptText == "79b153f2de3c0899a1a59f42b2b2d24dd468588723ca9f75109d53d90269656b";
assert builtins.hashString "sha256" coreReceiptText == "c585fd8b80479975a46ad19c83b7610a53f3a4a1a420241cbe0022a2d1fb82fb";
assert native.source_set_sha256 == "f1778f4df6fd0dabcbd441a28b93157f30b7c16d005617a71ae8ab11da18129a";
pkgs.runCommand "darwin-lab-bcm4378-control-protection-objects-002" {
  nativeBuildInputs = [ p.arm64-cross-toolchain p.native-ld pkgs.python3 ];
  preferLocalBuild = true;
  allowSubstitutes = false;
} ''
  mkdir -p source/native_backing source/private_lifecycle source/control_protection
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
    -MD -MF backing.d -c source/native_backing/backing.c -o backing.o
  ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang++ -std=c++17 -fno-exceptions -fno-rtti -fapple-kext "''${common[@]}" \
    -MD -MF native_iokit.d -c source/native_backing/native_iokit.cpp -o native_iokit.o
  ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang -std=c11 -ffreestanding -fno-builtin "''${common[@]}" \
    -MD -MF control_protection.d -c source/control_protection/control_protection.c -o control_protection.o
  ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang++ -std=c++17 -fno-exceptions -fno-rtti -fapple-kext "''${common[@]}" \
    -include "$headers/PrivateHeaders/mach/vm_types.h" -include "$headers/PrivateHeaders/arm/locks.h" \
    -MD -MF control_native.d -c source/control_protection/control_native.cpp -o control_native.o
  ${p.native-ld}/bin/ld -r -arch arm64 -Z -platform_version macos 26.5 26.5 \
    -o control_protection_provider.o lifecycle.o backing.o native_iokit.o control_protection.o control_native.o
  ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-nm -u control_protection_provider.o > undefined.txt
  ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-nm -g --defined-only control_protection_provider.o > defined.txt
  python3 - <<'PY'
from pathlib import Path
import struct
raw=Path('control_protection_provider.o').read_bytes()
magic,cpu,sub,kind,ncmds,cmdbytes,flags,reserved=struct.unpack_from('<8I',raw)
assert (magic,cpu,kind)==(0xfeedfacf,0x100000c,1), 'ARM64 MH_OBJECT required'
actual={line.split()[-1] for line in Path('undefined.txt').read_text().splitlines() if line.strip()}
expected={'_IOLockGetMachLock','_kernel_pmap','_pmap_find_phys','_IOLockAlloc','_IOLockLock','_IOLockUnlock','_IOMallocZero','_PAGE_SHIFT_CONST',
          '__ZN24IOBufferMemoryDescriptor22inTaskWithPhysicalMaskEP4taskjyy','_bzero',
          '_kernel_task','_memcmp','_memcpy','_memmove','_page_size'}
assert actual==expected,(actual,expected)
defined={line.split()[-1] for line in Path('defined.txt').read_text().splitlines() if line.strip()}
assert {'_bpl_init','_bpn_init','_bpn_native_pin_empty','_bpn_native_configure','_bcp_native_derive_and_configure','_bcp_derive','_bcp_boot_configure'}<=defined
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
  cp control_protection_provider.o "$out/lib/"
  cp lifecycle.o backing.o native_iokit.o control_protection.o control_native.o "$out/share/objects/"
  cp -R source/native_backing source/private_lifecycle source/control_protection "$out/share/inputs/"
  cp *.d *.su undefined.txt defined.txt "$out/share/validation/"
  cat > "$out/share/build-inputs.json" <<'JSON'
${inputs}
JSON
  (cd "$out"; sha256sum lib/control_protection_provider.o share/objects/*.o) > "$out/share/sha256.txt"
''
