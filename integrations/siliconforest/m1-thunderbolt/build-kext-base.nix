# SPDX-License-Identifier: MIT
# Native ACIO1 firmware stage. No NHI/domain/PCI enumeration is implied.
{ connectionManager ? false, pciEnumeration ? false, endpointMapper ? false, pciMSI ? false, i210Network ? false, dockUSB ? false }:
assert !pciEnumeration || connectionManager;
assert !endpointMapper || pciEnumeration;
assert !pciMSI || (pciEnumeration && endpointMapper);
assert !i210Network || pciMSI;
assert !dockUSB || i210Network;
let
  lkpi=import ../m1-rust-kext/lkpi;
  inherit (import ../m1-rust-kext/pins.nix) pkgs;
in lkpi.drmKextFinal.overrideAttrs (old: {
  pname="siliconforest-apple-cio";
  src=pkgs.runCommand "siliconforest-apple-cio-tree" { nativeBuildInputs=[ pkgs.python3 ]; } ''
    cp -R ${lkpi.finalTree} "$out"
    chmod -R u+w "$out"
    dest="$out/src/Kernel/Extensions/SFAppleCIO"
    mkdir -p "$dest"
    cp ${./kernel}/* "$dest/"
    ${pkgs.lib.optionalString endpointMapper ''
      chmod u+w "$dest/CMakeLists.txt"
      substituteInPlace "$dest/CMakeLists.txt" --replace-fail \
        '"''${CMAKE_CURRENT_SOURCE_DIR}/SFCIOPCIController.cpp"' \
        '"''${CMAKE_CURRENT_SOURCE_DIR}/SFCIOPCIController.cpp" "''${CMAKE_CURRENT_SOURCE_DIR}/SFCIOPCIDartMapper.cpp"'
      printf '\ntarget_compile_definitions(SFAppleCIO.kext PRIVATE SF_CIO_ENDPOINT_MAPPER=1)\n' >> "$dest/CMakeLists.txt"
    ''}
    ${pkgs.lib.optionalString pciEnumeration ''
      chmod u+w "$dest/CMakeLists.txt"
      printf '\ntarget_sources(SFAppleCIO.kext PRIVATE SFCIOPCIEnumeration.cpp)\ntarget_compile_definitions(SFAppleCIO.kext PRIVATE SF_CIO_PCIE_ENUMERATION=1)\n' >> "$dest/CMakeLists.txt"
    ''}
    ${pkgs.lib.optionalString pciMSI ''
      printf '\ntarget_sources(SFAppleCIO.kext PRIVATE SFCIOPCIMSIController.cpp)\ntarget_compile_definitions(SFAppleCIO.kext PRIVATE SF_CIO_PCI_MSI=1)\nset_source_files_properties(SFCIOPCIMSIController.cpp SFCIOPCIHost.cpp PROPERTIES COMPILE_DEFINITIONS KERNEL_PRIVATE=1)\n' >> "$dest/CMakeLists.txt"
    ''}
    ${pkgs.lib.optionalString i210Network ''
      printf '\ntarget_sources(SFAppleCIO.kext PRIVATE SFCIOPCIAdmission.cpp)\ntarget_compile_definitions(SFAppleCIO.kext PRIVATE SF_CIO_I210_NETWORK=1)\n' >> "$dest/CMakeLists.txt"
    ''}
    ${pkgs.lib.optionalString dockUSB ''
      printf '\ntarget_compile_definitions(SFAppleCIO.kext PRIVATE SF_CIO_DOCK_USB=1)\n' >> "$dest/CMakeLists.txt"
    ''}
    cp ${./include}/* "$dest/"
    cp ${./src}/sf_apple_nhi.c ${./src}/sf_cio_tunables.c ${./src}/sf_pciec_config.c ${./src}/sf_pciec_port.c ${./src}/sf_pciec_tunables.c "$dest/"
    cp ${../m1-usb-dwc3/kernel/sf_usbc_phy_lease.h} "$dest/sf_usbc_phy_lease.h"
    cp ${../m1-usb-dwc3/kernel/sf_usb_iova.h} "$dest/sf_usb_iova.h"
    cp ${../m1-dcp-dart/include/sf_dart_table.h} "$dest/sf_dart_table.h"
    cp ${../m1-dcp-dart/src/sf_dart_table.c} "$dest/sf_dart_table.c"
    cp ${../m1-sound/kernel}/sf_soc.* "$dest/"
    chmod u+w "$dest/sf_soc.cpp"
    cat ${./kernel/sf_cio_pmgr.inc} >> "$dest/sf_soc.cpp"
    cp -R ${../m1-gpu-rtkit/src} "$dest/rtkit"
    chmod -R u+w "$dest/rtkit"
    # Keep ACK contents, ordering and return values; count their actual
    # results in this controller's own engine. Shared RTKit sources stay put.
    python3 ${./tools/observe_rtkit.py} "$dest/rtkit/rtkit.c"
    # This controller owns 24 NHI lines + DART + its two mailbox lines.
    # Keep all other RTKit consumers' private engines unchanged.
    substituteInPlace "$dest/rtkit/SFRtkitPlatform.cpp" --replace-fail '#define MAX_IRQS  16' '#define MAX_IRQS  64'
    substituteInPlace "$dest/rtkit/SFRtkitPlatform.cpp" --replace-fail 'q->handled++;' '__atomic_add_fetch(&q->handled,1,__ATOMIC_RELAXED);'
    cat ${./kernel/sf_cio_irq.inc} >> "$dest/rtkit/SFRtkitPlatform.cpp"
    ${pkgs.lib.optionalString connectionManager ''
      cm="$out/src/Kernel/Extensions/SFThunderboltCM"
      mkdir -p "$cm"
      cp -R ${./upstream/family} "$cm/driver"
      cp -R ${./upstream/lib} "$cm/lib"
      cp -R ${./pininc} "$cm/pininc"
      chmod -R u+w "$cm/pininc"
      cp -R ${./nativeinc}/. "$cm/pininc/"
      cp ${./native}/*.h ${./native}/*.c "$cm/"
      cp ${./include/sf_tb_registry.h} "$cm/sf_tb_registry.h"
      cp ${./include/sf_tb_wire.h} "$cm/sf_tb_wire.h"
      cp ${./src/sf_tb_registry.c} "$cm/sf_tb_registry.c"
      cp ${./sf_tb_cm_compat.h} "$cm/sf_tb_cm_compat.h"
      python3 ${./native/patch_device.py} "$out"
      # Preserve every BSD initializer/linker-set member explicitly while
      # dropping unreachable CM/compatibility functions from this larger kext.
      sets="$out/src/Kernel/fbsdkpi/haiku/freebsd_network/compat/sys/linker_set.h"
      chmod u+w "$sets"
      substituteInPlace "$sets" --replace-fail '__section("__DATA," __SFSET_##set) __used' '__section("__DATA," __SFSET_##set ",regular,no_dead_strip") __used'

      chmod u+w "$out/src/Kernel/fbsdkpi/freebsd/linuxkpi/common/src/linux_devres.c"
      cat ${./native/sf_tb_devres_groups.inc} >> "$out/src/Kernel/fbsdkpi/freebsd/linuxkpi/common/src/linux_devres.c"
      chmod -R u+w "$cm"
      python3 ${./tools/prepare_cm.py} "$cm/driver"
      cp ${./cm-CMakeLists.txt} "$cm/CMakeLists.txt"
      cp ${./native}/*.h "$dest/"
      cp ${../m1-typec/kernel/sf_cd321x.h} "$dest/sf_cd321x.h"
    ''}
    printf '\nadd_subdirectory(SFAppleCIO)\n' >> "$out/src/Kernel/Extensions/CMakeLists.txt"
  '';
  buildPhase=''
    runHook preBuild
    ninja -C build-nix -k 0 SFAppleCIO.kext
    runHook postBuild
  '';
  installPhase=''
    mkdir -p "$out/System/Library/Extensions" "$out/nix-support"
    bundle=build-nix/src/Kernel/Extensions/SFAppleCIO/SFAppleCIO.kext
    test -f "$bundle/Contents/MacOS/SFAppleCIO"
    cp -R "$bundle" "$out/System/Library/Extensions/"
    cat ${./NOTICE} ${../m1-usb-dwc3/NOTICE} ${../m1-sound/NOTICE} > "$out/System/Library/Extensions/SFAppleCIO.kext/Contents/NOTICE"
    "$NIX_DARWIN_TOOLCHAIN_DIR/$NIX_DARWIN_HOST-nm" -u -j "$bundle/Contents/MacOS/SFAppleCIO" | sort -u > "$out/nix-support/undefined"
  '';
})
