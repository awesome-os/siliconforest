#!/bin/bash
# Boot a display-driver (SFDcp) collection once. Usage: tools/dcp_boot.sh <new-name> <kc-store-path> [extra boot-args]
#   DCP_TEST=1 tools/dcp_boot.sh ...   also stages and runs dcp-flip-test in the guest, with sfdcp=2
#   DCP_TEST=kms tools/dcp_boot.sh ... stages and runs the zero-copy GL present demo (m1-mesa-asahi)
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd); cd "$ROOT"
N=${1:?name}; KC=${2:?kernel collection store path}; EXTRA=${3:-}
ln -sfn "$KC" runs/m1-desktop-010/apcie-host-kc-build-001/result
# DCP_TEST=1: build the KMS flip test (integrations/siliconforest/m1-dcp/dcp-flip-test.nix), stage it
# with its libdrm and the guest script that runs it (DL43 lines), and boot with sfdcp=2.
# DCP_TEST=prime: the same test binary with the cross-driver script (DL44 lines): needs a
# collection with SFAgx and SFDcp; adds sfagx=1.
# DCP_TEST=wait: the flip loop once per way of waiting on the DRM fd (read, poll, select,
# kqueue), DL45 lines. Works on a SFDcp-only or a combined collection.
# The next three need a collection on the patched kernel (integrations/siliconforest/m1-dcp/kernel.nix):
# DCP_TEST=scm: a child process sends GPU buffers to the parent over SCM_RIGHTS, which imports
# and flips them (DL47 lines). SFAgx + SFDcp; adds sfagx=1.
# DCP_TEST=fence: sync_file (out-fence) and dma-buf descriptors waited on with poll, select and
# kevent (DL48 lines). The last of its four runs needs SFAgx; adds sfagx=1.
# DCP_TEST=newkernel: DL45 (wait), DL48 and DL47 in one boot. SFAgx + SFDcp; adds sfagx=1.
# DCP_TEST=mmap: mmap of dma-buf descriptors of both drivers (DL49 lines). SFAgx + SFDcp on the
# kernel with kernel-patches/0003; adds sfagx=1.
# DCP_TEST=timing: 3 x 20 s of flips, "timing:" lines (DL51). Run once as is and once with
# extra boot-arg sflkpi_wq_highpri=0 to compare. SFAgx + SFDcp; adds sfagx=1. Needs DCP_RUNTIME=240.
case "${DCP_TEST:-0}" in scm|fence|newkernel|mmap|timing)
  DCP_TEMPLATE=${DCP_TEMPLATE:-$ROOT/integrations/siliconforest/m1-dcp/supervisor/darwin-dcp-$DCP_TEST-test.in}
  EXTRA="sfagx=1 $EXTRA"; DCP_TEST=1;;
esac
if [ "${DCP_TEST:-0}" = wait ]; then
  DCP_TEMPLATE=${DCP_TEMPLATE:-$ROOT/integrations/siliconforest/m1-dcp/supervisor/darwin-dcp-wait-test.in}
  DCP_TEST=1
fi
if [ "${DCP_TEST:-0}" = prime ]; then
  DCP_TEMPLATE=${DCP_TEMPLATE:-$ROOT/integrations/siliconforest/m1-dcp/supervisor/darwin-dcp-prime-test.in}
  EXTRA="sfagx=1 $EXTRA"; DCP_TEST=1
fi
if [ "${DCP_TEST:-0}" = 1 ]; then
  export PATH=$HOME/.nix-profile/bin:$PATH
  T=$(tools/staged.sh dcp-flip)
  export PCIE_EXTRA_FILES="$T/usr/local/bin/dcp-flip-test=/usr/local/bin/dcp-flip-test;$T/usr/local/lib/mesa-asahi/libdrm.2.dylib=/usr/local/lib/mesa-asahi/libdrm.2.dylib"
  # The guest script replaces the supervisor directly (PCIE_GUEST_TEMPLATE is only honoured on the Wi-Fi join path).
  export PCIE_EXTRA_FILES="$PCIE_EXTRA_FILES;${DCP_TEMPLATE:-$ROOT/integrations/siliconforest/m1-dcp/supervisor/darwin-dcp-flip-test.in}=/usr/bin/darwin-desktop-test"
  DCP_MODE=${DCP_MODE:-2}
fi
# DCP_TEST=kms: the zero-copy present demo (integrations/siliconforest/m1-mesa-asahi): agx-panel-demo
# with its Mesa libraries and libdrm, guest script darwin-agx-kms-demo.in (DL46 lines). Needs a
# collection with SFAgx and SFDcp; adds sfagx=1, sfdcp=2. Optional reference fingerprint:
# DCP_KMS_REF=<panel-linux-kms.fp>.
if [ "${DCP_TEST:-0}" = kms ]; then
  export PATH=$HOME/.nix-profile/bin:$PATH
  T=$(tools/staged.sh mesa)
  X="$T/usr/local/bin/agx-panel-demo=/usr/local/bin/agx-panel-demo"
  for l in libEGL.1.dylib libGLESv2.2.dylib libgallium-26.3.0-devel.dylib libdrm.2.dylib; do X="$X;$T/usr/local/lib/mesa-asahi/$l=/usr/local/lib/mesa-asahi/$l"; done
  [ -n "${DCP_KMS_REF:-}" ] && X="$X;$DCP_KMS_REF=/usr/local/share/agx/panel-linux-kms.fp"
  export PCIE_EXTRA_FILES="$X;${DCP_TEMPLATE:-$ROOT/integrations/siliconforest/m1-mesa-asahi/supervisor/darwin-agx-kms-demo.in}=/usr/bin/darwin-desktop-test"
  EXTRA="sfagx=1 $EXTRA"; DCP_MODE=${DCP_MODE:-2}
fi
PCIE_BOOTARGS="sfgpudt=2 sfgpudt_stringprops=1 sfdispdt=2 sfdcp=${DCP_MODE:-1} $EXTRA" PCIE_SKIP_BUILD=1 LINES_MAX=1 \
  tools/pcie_iterate.sh "$N" "${DCP_RUNTIME:-150}" 2>&1 | grep -a '"status"\|rror\|No space' | cut -c1-200
strings -n 6 runs/m1-desktop-010/pcie-enum-attempt-$N/guest/console.bin | grep -a 'SFDcp\|pd platform: no\|panic\|SF_STUB\|Backtrace\|IOMemoryDescriptor phys\|DL[45][0-9]\|^AGX\|agx-panel-demo:' | grep -av 'SFDcp:   reg \|interrupt controller none\|dart-regs\|dart-l1' | cut -c1-${DCP_COLS:-260}
