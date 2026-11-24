# SPDX-License-Identifier: MIT
# Port-1 USB2/USB3 lifecycle + paired DART + prepared xHCI, one bundle.
let
  source = import ../rebase/pd.nix;
  inherit (source) pkgs;
  base = import ./build-xhci.nix;
in base.overrideAttrs (old: {
  pname = "siliconforest-apple-usb-host";
  src = pkgs.runCommand "siliconforest-apple-usb-host-tree" { nativeBuildInputs = [ pkgs.python3 ]; } ''
    cp -R ${old.src} "$out"
    chmod -R u+w "$out"
    dest="$out/src/Kernel/Extensions/RavynXHCIPort"
    cat ${./kernel}/sf_usbc_pmgr.inc >> "$dest/sf_soc.cpp"
    cp ${./kernel}/SFAppleUSB.cpp ${./kernel}/sf_dwc3_host.* ${./kernel}/sf_usbc_phy_lease.h ${./kernel}/sf_acio_pinmux.h "$dest/"
    cp ${../m1-atcphy/include}/* ${../m1-atcphy/src}/*.c ${../m1-atcphy/upstream}/*.inc "$dest/"
    cp ${../m1-typec/kernel/sf_cd321x.h} "$dest/sf_cd321x.h"
    printf '\ntarget_sources(RavynXHCIPort.kext PRIVATE SFAppleUSB.cpp sf_atc_usb2.c sf_atc_dp.c sf_atc_modes.c sf_atc_calibration.c sf_atc_pipe.c sf_atc_pipe_bootstrap.c sf_dwc3_host.c)\n' >> "$dest/CMakeLists.txt"
    python3 - "$dest/Info.plist" <<'PY'
import plistlib, sys
p=sys.argv[1]
with open(p,'rb') as f: info=plistlib.load(f)
info['IOKitPersonalities']['Apple USB lifecycle']={
 'CFBundleIdentifier':info['CFBundleIdentifier'], 'IOClass':'SFAppleUSB',
 'IOProviderClass':'SFAppleTypeC','IOProbeScore':5000}
info['CFBundleGetInfoString'] += '; ATC GPL-2.0/BSD-2-Clause, Asahi Linux Contributors; DWC3 GPL-2.0, Texas Instruments 2010-2011; Apple glue Linux Foundation 2018/Texas Instruments 2015. See NOTICE.'
with open(p,'wb') as f: plistlib.dump(info,f)
PY
  '';
  installPhase = old.installPhase + ''
    chmod u+w "$out/System/Library/Extensions/RavynXHCIPort.kext/Contents/NOTICE"
    cat ${../m1-atcphy/NOTICE} >> "$out/System/Library/Extensions/RavynXHCIPort.kext/Contents/NOTICE"
  '';
})
