# SPDX-License-Identifier: MIT
# Source-based CLI comparison subset; blocked/GUI/Swift tests are not claimed.
let
  e = import ../environment.nix;
  inherit (e) pkgs finch;
  dropins = import ./build.nix;
  bt = import ../CoreBluetooth/tests.nix;
  tk = import ../CryptoTokenKit/tests.nix;
  od = import ../OpenDirectory/tests.nix;
  ck = import ../CloudKit/tests.nix;
  numeric = import ../Accelerate/build.nix { groups = [ "vDSP" "vMisc" "BLAS" "LAPACK" "Quadrature" "LinearAlgebra" "SparseBLAS" "Sparse" ]; };
  numericTests = import ../Accelerate/comparison.nix;
  sparseTests = import ../Accelerate/sparse-tests.nix;
  solverTests = import ../Accelerate/solver-tests.nix;
  objectTests = import ../Accelerate/object-tests.nix;
  colors = import ../ColorSync/build.nix;
  colorTests = import ../ColorSync/tests.nix;
  cloudkit = import ../CloudKit/build.nix;
  opendirectory = import ../OpenDirectory/build.nix;
  storekit = import ../StoreKit/build.nix;
  storekitTests = import ../StoreKit/tests.nix;
  network = import ../CFNetwork/build.nix;
  networkTests = import ../CFNetwork/tests.nix;
  entry = name: store: mode: extra: {
    inherit name mode;
    argv = [ "bin/${name}" ];
    timeout = 90;
  } // extra // { source = "${store}/bin/${name}"; };
  entries = [
    (entry "accounts-test" dropins "reference" { skip_lines = 1; })
    (entry "corebluetooth-test" bt "reference" { skip_lines = 1; })
    (entry "corebluetooth-unsupported-test" bt "expected" { expected = "expected/corebluetooth-unsupported-test.expected"; })
    (entry "cryptotokenkit-test" tk "reference" { skip_lines = 1; })
    (entry "cryptotokenkit-empty-test" tk "expected" { expected = "expected/cryptotokenkit-empty-test.expected"; })
    (entry "opendirectory-objects-test" od "reference" { skip_lines = 0;
      env = { DYLD_FRAMEWORK_PATH = "@ROOT@/System/Library/Frameworks:@ROOT@/System/Library/Frameworks/OpenDirectory.framework/Versions/A/Frameworks"; };
    })
    (entry "cloudkit-test" ck "reference" { skip_lines = 1;
      env = { DYLD_FRAMEWORK_PATH = "@ROOT@/System/Library/Frameworks"; };
    })
    (entry "cloudkit-unavailable-test" ck "expected" { expected = "expected/cloudkit-unavailable-test.expected";
      env = { DYLD_FRAMEWORK_PATH = "@ROOT@/System/Library/Frameworks"; };
    })
    (entry "cfnetwork-test" networkTests "reference" {
      skip_lines = 1; # First line reports the loaded CFNetwork image path.
      env = { DYLD_FRAMEWORK_PATH = "@ROOT@/System/Library/Frameworks"; };
    })
    { name = "cloudkit-getter-probe"; argv = [ "bin/cloudkit-getter-probe" ]; mode = "exit"; timeout = 30;
      source = "${ck}/bin/probe"; env = { DYLD_FRAMEWORK_PATH = "@ROOT@/System/Library/Frameworks"; }; }
    { name = "storekit-probe"; argv = [ "bin/storekit-probe" ]; mode = "exit"; timeout = 30;
      source = "${storekitTests}/bin/probe"; env = { DYLD_FRAMEWORK_PATH = "@ROOT@/System/Library/Frameworks"; }; }
    (entry "sparseblas-test" sparseTests "reference" { skip_lines = 0; env = { DYLD_LIBRARY_PATH = "@ROOT@/lib"; }; })
    (entry "sparse-test" solverTests "reference" { skip_lines = 0; env = { DYLD_LIBRARY_PATH = "@ROOT@/lib"; }; })
    (entry "solver-probe" solverTests "exit" { env = { DYLD_LIBRARY_PATH = "@ROOT@/lib"; }; })
    (entry "sparse-probe" sparseTests "exit" { env = { DYLD_LIBRARY_PATH = "@ROOT@/lib"; }; })
    (entry "linearalgebra-test" objectTests "reference" { skip_lines = 0; env = { DYLD_LIBRARY_PATH = "@ROOT@/lib"; }; })
    (entry "quadrature-test" objectTests "reference" { skip_lines = 0; env = { DYLD_LIBRARY_PATH = "@ROOT@/lib"; }; })
    (entry "colorsync-test" colorTests "reference" { skip_lines = 1; env = { DYLD_FRAMEWORK_PATH = "@ROOT@/System/Library/Frameworks"; }; })
  ] ++ map (n: entry "${n}-test" numericTests "reference" {
    skip_lines = 0; # These numeric tests have no image-identification line.
    env = { DYLD_LIBRARY_PATH = "@ROOT@/lib"; };
  }) [ "vdsp" "vmisc" "blas" ];
  manifest = pkgs.writeText "finch-cli-manifest.json" (builtins.toJSON (map (x: builtins.removeAttrs x [ "source" ]) entries));
in pkgs.runCommand "siliconforest-finch-cli-conformance-arm64" { nativeBuildInputs = [ pkgs.python3 ]; } ''
  mkdir -p $out/bin $out/lib $out/expected $out/tools
  ${pkgs.lib.concatMapStringsSep "\n" (x: "cp ${x.source} $out/bin/${x.name}") entries}
  python3 ${../tools/portable-tests.py} ${../../homebrew/hb_macho.py} $out/bin $out/binary-hashes.json
  cp ${numeric}/staged-bootstrap/*.dylib $out/lib/
  cp -r ${network}/System $out/
  chmod -R u+w $out/System
  cp -r ${colors}/System/Library/Frameworks/ColorSync.framework $out/System/Library/Frameworks/
  cp -r ${storekit}/System/Library/Frameworks/StoreKit.framework $out/System/Library/Frameworks/
  cp -r ${cloudkit}/System/Library/Frameworks/CloudKit.framework $out/System/Library/Frameworks/
  cp -r ${opendirectory}/System/Library/Frameworks/OpenDirectory.framework $out/System/Library/Frameworks/
  cp ${manifest} $out/manifest.json
  cp ${finch}/userland/tests/{corebluetooth-unsupported-test,cryptotokenkit-empty-test,cloudkit-unavailable-test}.expected $out/expected/
  cp ${finch}/tools/test-masks.txt ${finch}/tools/host-test-exceptions.txt $out/tools/
  cp ${../tools/compare-tests.py} $out/tools/compare-tests.py
  cp ${./README.md} $out/README.md
  mkdir -p $out/licenses
  cp ${finch}/LICENSE* $out/licenses/
''
