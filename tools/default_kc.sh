#!/bin/bash
# SPDX-License-Identifier: MIT
# The one place that says which kernel collection a boot gets when nothing names another.
# Usage: tools/default_kc.sh [default|native|storage|reboot]     prints the collection directory
#   default   the desktop collection: integrations/siliconforest/rebase/build-kc.nix (upstream's tip;
#             kernel with every lab patch, eight cores, Wi-Fi, GPU, display, sound, Touch Bar,
#             SFAppleAVD). Every suite wrapper and tools/gpu_desktop.sh boot this.
#   native    the same with the platform expert of m1-native-boot (cores held by the loader)
#   storage   the same plus SFAppleNVMe (m1-storage; needs sfnvme= to do anything)
#   reboot    native with the SFCpuPower of m1-native-boot/reboot (restart through the SMC)
# SF_KC=pin: the last default collection of the pinned revision e7aa00a6 (default only). A pin boot
# also needs PCIE_PIN=1 (the pin's RAM root): tools/gpu_desktop.sh does both with GPU_DESKTOP_PIN=1.
# The store path is remembered by m1-boot-speed/tools/bs_nix_path.py while the recipes are unchanged.
set -euo pipefail
ROOT=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd); cd "$ROOT"
export PATH=$HOME/.nix-profile/bin:$PATH
PIN_KC=/nix/store/z66w83kk8gj61xv5ych34rz1zv18hk70-puredarwin-kc-arm64-0.1
NP=integrations/siliconforest/m1-boot-speed/tools/bs_nix_path.py
WHICH=${1:-default}
if [ "${SF_KC:-}" = pin ]; then
  [ "$WHICH" = default ] || { echo "SF_KC=pin has the default collection only" >&2; exit 2; }
  [ -f "$PIN_KC/kernel" ] || PIN_KC=$(SF_PD=pin $NP m1-boot-speed build-kc.nix)
  echo "$PIN_KC"; exit 0
fi
case "$WHICH" in
  default) set -- rebase build-kc.nix;;
  native) set -- m1-native-boot build-kc-norecovery.nix;;
  storage) set -- m1-storage build-kc.nix;;
  reboot) set -- m1-native-boot/reboot build-kc.nix;;
  *) echo "usage: default_kc.sh [default|native|storage|reboot]" >&2; exit 2;;
esac
KC=$(env -u SF_PD $NP "$@")
[ -f "$KC/kernel" ] || { echo "no collection from $*" >&2; exit 2; }
echo "$KC"
