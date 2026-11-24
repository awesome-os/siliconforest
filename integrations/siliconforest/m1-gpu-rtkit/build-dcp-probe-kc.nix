# Kernel collection for SF-H item H1: the bridge collection that the GPU boots use as their
# base (../m1-apcie-host/build-kc.nix, as ../m1-rust-kext/build-agx-kc.nix without Wi-Fi)
# plus SFRtkitProbe.kext. Nothing else is needed: the probe links its own hidden copy of the
# Linux layer, the mailbox/RTKit port and SFRtkitPlatform.cpp (see build-probe-kext.nix), and
# imports only kernel symbols. SFGpuDT, SFDRMDri and SFAgx are deliberately not in this
# collection: nothing here should touch the GPU while the DCP is being looked at.
#
#   boot-args: sfrtkitprobe=3      (DCP mode; without the boot-arg the probe does its
#                                   read-only GPU mode 1, sfrtkitprobe=0 does nothing)
#
# kc-builder reports unresolved symbols as `UNRESOLVED: <sym>` lines without failing; the
# recipe keeps the log ($out/kc-build.log) and fails if SFRtkitProbe has any besides
# dyld_stub_binder and the layer's three _sf_usb_* references, or if fewer than 4 pages are
# left before the kernel's __TEXT (a collection that reaches it does not boot: boot ag02).
let
  inherit (import ../m1-rust-kext/pins.nix) pkgs;
  base = import ../m1-apcie-host/build-kc.nix;
  probe = import ./build-probe-kext.nix;
  needle = "  -o kernel";
  marginPages = 4;
in base.overrideAttrs (old:
  # refuse a base recipe whose link line moved, rather than silently building it unchanged
  assert builtins.stringLength old.buildPhase
    != builtins.stringLength (builtins.replaceStrings [ needle ] [ "" ] old.buildPhase);
  {
    buildPhase = ''
      set -o pipefail
      {
      '' + builtins.replaceStrings [ needle ] [ ''  -kext "${probe}/System/Library/Extensions/SFRtkitProbe.kext" \
      -o kernel'' ] old.buildPhase + ''
      } 2>&1 | tee kc-build.log

      grep -q 'loaded kext: org.siliconforest.driver.SFRtkitProbe' kc-build.log
      # the lines between the previous kext's "merged" line and this kext's "patched kmod_info"
      awk '/patched kmod_info: org.siliconforest.driver.SFRtkitProbe/ { print buf; exit }
           /merged [0-9]+ symbols from/ { buf = ""; next }
           { buf = buf $0 "\n" }' kc-build.log | grep -o 'UNRESOLVED.*' | sed 's/.*: //' | sort -u > probe-unresolved
      echo "SFRtkitProbe unresolved: $(tr '\n' ' ' < probe-unresolved)"
      if grep -v -x -e dyld_stub_binder -e _sf_usb_module -e _sf_usb_register -e _sf_usb_unregister probe-unresolved; then
        echo "SFRtkitProbe has unresolved symbols beyond the layer's known ones" >&2
        exit 1
      fi

      last=$(grep -E '^ *kext\[[0-9]+\] .*vmaddr=0x[0-9a-f]+ size=0x[0-9a-f]+' kc-build.log | tail -1)
      khdr=$(grep -oE 'fileset: kernel hdr vmaddr=0x[0-9a-f]+' kc-build.log | tail -1 | grep -oE '0x[0-9a-f]+')
      va=$(printf '%s' "$last" | grep -oE 'vmaddr=0x[0-9a-f]+' | cut -d= -f2)
      sz=$(printf '%s' "$last" | grep -oE 'size=0x[0-9a-f]+' | cut -d= -f2)
      test -n "$va" -a -n "$sz" -a -n "$khdr"
      # 64-bit kernel addresses: compare the low 40 bits
      end=$(( (va & 0xffffffffff) + sz )); lim=$(( khdr & 0xffffffffff ))
      echo "kext area ends at +$(printf %x $end), kernel header at +$(printf %x $lim), free $(( lim - end )) bytes"
      if [ $(( lim - end )) -lt $(( ${toString marginPages} * 16384 )) ]; then
        echo "the kext area reaches the kernel's __TEXT (less than ${toString marginPages} pages free): this collection does not boot" >&2
        exit 1
      fi
    '';
    installPhase = old.installPhase + ''
      cp kc-build.log $out/kc-build.log
    '';
  })
