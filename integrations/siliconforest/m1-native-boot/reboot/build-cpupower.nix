# SPDX-License-Identifier: GPL-2.0
# SFCpuPower.kext of the default desktop collection (../../m1-cpufreq/build-kext.nix: its
# sources, unchanged, read from that stream's directory) with ./cpupower-patches on top and
# ./SFAppleReboot.cpp, ./sf_pmu_scratch.c compiled in: restart and power-off through the SMC
# and the PMU's boot state bytes, as Linux macsmc-reboot (boot-args sfreboot=, sfpmu=), and the
# hardware clock read as Linux rtc-macsmc reads it (kern.sf_rtc_usec; sfrtc=0 turns it off).
#   nix-build --extra-experimental-features nix-command --extra-experimental-features flakes build-cpupower.nix --no-out-link
let
  pkgs = (import ../../m1-rust-kext/pins.nix).pkgs;
in
(import ../../m1-cpufreq/build-kext.nix).overrideAttrs (old: {
  pname = "siliconforest-cpupower-reboot";
  src = pkgs.runCommand "siliconforest-cpupower-reboot-tree" { nativeBuildInputs = [ pkgs.patch ]; } ''
    cp -R ${old.src} "$out"
    chmod -R u+w "$out"
    d="$out/src/Kernel/Extensions/SFCpuPower"
    test -f "$d/SFCpuPower.cpp"
    for f in ${./SFAppleReboot.cpp} ${./SFAppleReboot.h} ${./sf_pmu_scratch.c} ${./sf_pmu_scratch.h} ${./sf_halt_gate.h}; do
      cp "$f" "$d/''${f#*-}"
    done
    patch -s -d "$d" -p1 < ${./cpupower-patches/0001-SFCpuPower-restart-and-power-off-through-the-SMC.patch}
  '';
})
