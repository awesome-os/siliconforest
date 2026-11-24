# Cached local OBJECT package only. No observer service, kext, KC or target code.
let
  pin = "e7aa00a60f0b56ef35aa24252e81467787bf71ea";
  pd = builtins.getFlake "git+file:///home/chrisk/src/darwin-lab/sources/puredarwin-baseline?submodules=1&shallow=1&rev=${pin}";
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  p = pd.packages.x86_64-linux;
  compiler = ./compile-private-policy-001.py;
  receipts = {
    mcc_snapshot = builtins.readFile ../mcc_snapshot/source-validation-001.json;
    native_policy_snapshot = builtins.readFile ../native_policy_snapshot/source-validation-001.json;
    native_policy_reader = builtins.readFile ../native_policy_reader/source-validation-001.json;
    native_private_policy = builtins.readFile ../native_private_policy/source-validation-001.json;
    owned_control = builtins.readFile ../owned_control/source-validation-001.json;
    native_owned = builtins.readFile ../native_owned/source-validation-001.json;
    private_lifecycle = builtins.readFile ../private_lifecycle/source-validation-001.json;
  };
  data = builtins.mapAttrs (_: raw: builtins.fromJSON raw) receipts;
  files = [
    { name = "mcc_snapshot/mcc_snapshot.c"; path = ../mcc_snapshot/mcc_snapshot.c; sha256 = data.mcc_snapshot.files_sha256."mcc_snapshot.c"; }
    { name = "mcc_snapshot/mcc_snapshot.h"; path = ../mcc_snapshot/mcc_snapshot.h; sha256 = data.mcc_snapshot.files_sha256."mcc_snapshot.h"; }
    { name = "native_policy_snapshot/policy_snapshot.c"; path = ../native_policy_snapshot/policy_snapshot.c; sha256 = data.native_policy_snapshot.files_sha256."policy_snapshot.c"; }
    { name = "native_policy_snapshot/policy_snapshot.h"; path = ../native_policy_snapshot/policy_snapshot.h; sha256 = data.native_policy_snapshot.files_sha256."policy_snapshot.h"; }
    { name = "native_policy_reader/policy_reader.c"; path = ../native_policy_reader/policy_reader.c; sha256 = data.native_policy_reader.files_sha256."policy_reader.c"; }
    { name = "native_policy_reader/policy_reader.h"; path = ../native_policy_reader/policy_reader.h; sha256 = data.native_policy_reader.files_sha256."policy_reader.h"; }
    { name = "native_policy_reader/policy_reader_internal.h"; path = ../native_policy_reader/policy_reader_internal.h; sha256 = data.native_policy_reader.files_sha256."policy_reader_internal.h"; }
    { name = "native_policy_reader/policy_reader_native.cpp"; path = ../native_policy_reader/policy_reader_native.cpp; sha256 = data.native_policy_reader.files_sha256."policy_reader_native.cpp"; }
    { name = "native_private_policy/private_policy.c"; path = ../native_private_policy/private_policy.c; sha256 = data.native_private_policy.files_sha256."private_policy.c"; }
    { name = "native_private_policy/private_policy.h"; path = ../native_private_policy/private_policy.h; sha256 = data.native_private_policy.files_sha256."private_policy.h"; }
    { name = "native_private_policy/private_policy_internal.h"; path = ../native_private_policy/private_policy_internal.h; sha256 = data.native_private_policy.files_sha256."private_policy_internal.h"; }
    { name = "owned_control/owned_control.c"; path = ../owned_control/owned_control.c; sha256 = data.owned_control.files_sha256."owned_control.c"; }
    { name = "owned_control/owned_control.h"; path = ../owned_control/owned_control.h; sha256 = data.owned_control.files_sha256."owned_control.h"; }
    { name = "owned_control/owned_pmap_bridge.h"; path = ../owned_control/owned_pmap_bridge.h; sha256 = data.owned_control.files_sha256."owned_pmap_bridge.h"; }
    { name = "owned_control/owned_control_native.cpp"; path = ../owned_control/owned_control_native.cpp; sha256 = data.owned_control.files_sha256."owned_control_native.cpp"; }
    { name = "native_owned/owned.c"; path = ../native_owned/owned.c; sha256 = data.native_owned.files_sha256."owned.c"; }
    { name = "native_owned/owned.h"; path = ../native_owned/owned.h; sha256 = data.native_owned.files_sha256."owned.h"; }
    { name = "native_owned/owned_types.h"; path = ../native_owned/owned_types.h; sha256 = data.native_owned.files_sha256."owned_types.h"; }
    { name = "native_owned/native_owned.cpp"; path = ../native_owned/native_owned.cpp; sha256 = data.native_owned.files_sha256."native_owned.cpp"; }
    { name = "native_owned/native_owned.h"; path = ../native_owned/native_owned.h; sha256 = data.native_owned.files_sha256."native_owned.h"; }
    { name = "native_owned/lifecycle_bridge.h"; path = ../native_owned/lifecycle_bridge.h; sha256 = data.native_owned.files_sha256."lifecycle_bridge.h"; }
    { name = "private_lifecycle/lifecycle.c"; path = ../private_lifecycle/lifecycle.c; sha256 = data.private_lifecycle.files_sha256."lifecycle.c"; }
    { name = "private_lifecycle/lifecycle.h"; path = ../private_lifecycle/lifecycle.h; sha256 = data.private_lifecycle.files_sha256."lifecycle.h"; }
  ];
  inputs = builtins.toJSON {
    inherit pin;
    scope = "Frozen configuration-only native private-policy object provider; no runtime admission";
    source_receipt_sha256 = builtins.mapAttrs (_: raw: builtins.hashString "sha256" raw) receipts;
    production_files = map (f: { inherit (f) name sha256; }) files;
    toolchain = "${p.arm64-cross-toolchain}";
    linker = "${p.native-ld}";
    kernel_headers = "${p.xnu-headers-arm64}";
    sdk = "${p.apple-sdk}";
    required_additional_KPI = "Unsupported: PE_state, kernel_pmap, pmap_find_phys";
    expected_imports = [
      "_IOLockAlloc"
      "_IOLockGetMachLock"
      "_IOLockLock"
      "_IOLockUnlock"
      "_IOMallocZero"
      "_PAGE_SHIFT_CONST"
      "_PE_state"
      "__ZN15IORegistryEntry8fromPathEPKcPK15IORegistryPlanePcPiPS_"
      "__ZN15OSMetaClassBase12safeMetaCastEPKS_PK11OSMetaClass"
      "__ZN24IOBufferMemoryDescriptor22inTaskWithPhysicalMaskEP4taskjyy"
      "__ZN6OSData9metaClassE"
      "_bzero"
      "_gIODTPlane"
      "_kernel_pmap"
      "_kernel_task"
      "_memcmp"
      "_memcpy"
      "_memmove"
      "_page_size"
      "_pmap_find_phys"
    ];
    target_execution = false;
    kext_or_KC = false;
    test_headers = false;
  };
in
assert pd.rev == pin;
assert builtins.hashString "sha256" receipts.mcc_snapshot == "5060b3baf529c046c03429ca7da882f4f1671e8864cd69aba62714148cec26d8";
assert builtins.hashString "sha256" receipts.native_policy_snapshot == "908e29b3bd8910018810eda881ec7b4a98a67df354f4a4a3ee2395b705c09302";
assert builtins.hashString "sha256" receipts.native_policy_reader == "e892e8e31f0ba8626d4e8a83188b6fe9462163394b1685e5b1094486fcaea93c";
assert builtins.hashString "sha256" receipts.native_private_policy == "7d41f394d77e20bd5776964c5a46194b120a3df705ccb996889cb6d852a09c72";
assert builtins.hashString "sha256" receipts.owned_control == "9844eb28efb1190097369b419b90aeeebb605f4aafa147dbd908a7841b3d2c57";
assert builtins.hashString "sha256" receipts.native_owned == "0ee2101c9cda5cf282c3db48b7640cef98146c3b04724e3fe59138aaad772434";
assert builtins.hashString "sha256" receipts.private_lifecycle == "c585fd8b80479975a46ad19c83b7610a53f3a4a1a420241cbe0022a2d1fb82fb";
assert builtins.hashFile "sha256" compiler == "f8888bed8e4de580ed752d72d378b1a361d0961b953f16600d600c1ba987ddd9";
pkgs.runCommand "darwin-lab-bcm4378-native-private-policy-objects-001" {
  nativeBuildInputs = [ p.arm64-cross-toolchain p.native-ld pkgs.python3 ];
  preferLocalBuild = true;
  allowSubstitutes = false;
} ''
  mkdir -p source
  ${pkgs.lib.concatMapStringsSep "\n" (f: ''
    mkdir -p "source/$(dirname '${f.name}')"
    cp ${f.path} source/${f.name}
    echo '${f.sha256}  source/${f.name}' | sha256sum -c -
  '') files}
  export DARWIN_SDK_ROOT=${p.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
  cat > build-inputs.json <<'JSON'
${inputs}
JSON
  python3 ${compiler} --source source --output objects --inputs build-inputs.json \
    --toolchain ${p.arm64-cross-toolchain}/bin --linker ${p.native-ld}/bin/ld \
    --headers ${p.xnu-headers-arm64}/System/Library/Frameworks/Kernel.framework/Versions/A \
    --availability ${pd.outPath}/src/Libraries/AvailabilityVersions/include
  mkdir -p "$out/lib" "$out/share/objects" "$out/share/inputs" "$out/share/validation"
  cp objects/private_policy_provider.o "$out/lib/"
  cp objects/*.o "$out/share/objects/"
  cp -R source/. "$out/share/inputs/"
  cp objects/*.d objects/*.su objects/*.log objects/build.json "$out/share/validation/"
  cp build-inputs.json "$out/share/build-inputs.json"
  (cd "$out"; sha256sum lib/private_policy_provider.o share/objects/*.o) > "$out/share/sha256.txt"
''
