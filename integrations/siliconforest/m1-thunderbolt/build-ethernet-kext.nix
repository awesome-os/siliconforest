# SPDX-License-Identifier: MIT
# Source/SDK preparation only: this driver is NOT in a boot collection.
# Keep shared LinuxKPI/fbsdkpi and all other driver artifacts unchanged.
let
 lkpi=import ../m1-rust-kext/lkpi;
 inherit (import ../m1-rust-kext/pins.nix) pkgs;
 driver=/home/chrisk/src/darwin-lab/sources/puredarwin-m1-firstboot/src/Kernel/Extensions/SFFBSDem;
in lkpi.drmKextFinal.overrideAttrs (old: {
 pname="siliconforest-t8103-dock-ethernet";
 src=pkgs.runCommand "siliconforest-dock-ethernet-tree" { nativeBuildInputs=[pkgs.patch pkgs.python3]; } ''
  cp -R ${lkpi.finalTree} "$out"
  chmod -R u+w "$out"
  cd "$out"
  patch --fuzz=0 -p1 < ${./patches/ethernet-owned-dma.patch}
  cp ${./kernel/sf_pci_dma_retire.h} src/Kernel/fbsdkpi/xnu/sf_pci_dma_retire.h
  # The minimal DRM source package omits Ethernet directories. Verify and
  # copy the existing clean SiliconForest/FreeBSD driver, not an alternate port.
  python3 - ${driver} ${./native/ethernet-provenance.json} <<'PY'
import pathlib,sys,json,hashlib
root=pathlib.Path(sys.argv[1]);expected=json.loads(pathlib.Path(sys.argv[2]).read_text())["sha256"]
actual={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in root.rglob("*") if p.is_file()}
assert actual==expected,"Ethernet source differs from pinned manifest"
PY
  test ! -e src/Kernel/Extensions/SFFBSDem
  cp -R ${driver} src/Kernel/Extensions/SFFBSDem
  cat >> src/Kernel/Extensions/CMakeLists.txt <<'CMAKE'
if(NOT TARGET SFFBSDem.kext)
 add_subdirectory(SFFBSDem)
endif()
CMAKE
 '';
 buildPhase=''
  runHook preBuild
  ninja -C build-nix -k 0 SFFBSDem.kext
  runHook postBuild
 '';
 installPhase=''
  mkdir -p "$out/System/Library/Extensions" "$out/nix-support"
  bundle=build-nix/src/Kernel/Extensions/SFFBSDem/SFFBSDem.kext
  test -f "$bundle/Contents/MacOS/SFFBSDem"
  cp -R "$bundle" "$out/System/Library/Extensions/"
  cp ${./NOTICE} "$out/System/Library/Extensions/SFFBSDem.kext/Contents/NOTICE"
 '';
})
