# One-boot collection for the display DART backend: the bridge collection
# (../m1-apcie-host/build-kc.nix) plus
#   SFRtkitProbe.kext  ../m1-gpu-rtkit/build-probe-kext.nix: links this directory's src/ and
#                      runs it with sfrtkitprobe=5 (read-only look at dart-disp0 streams 0
#                      and 4, the DCP wake through Linux's IOMMU/DMA API, the API test)
#   SFGpuDT.kext       ../m1-gpu-dt/build-kext.nix: with sfdispdt=1 logs the display device
#                      tree it would publish (sfgpudt_power=0: no PMGR write)
# Both kexts are self-contained (SFRtkitProbe links its own copy of the Linux layer).
#
#   boot-args: sfrtkitprobe=5 sfgpudt=1 sfgpudt_power=0 sfdispdt=1
#
# Fails on an unresolved symbol of SFRtkitProbe beyond the layer's four known ones, or if
# fewer than 4 pages stay free before the kernel's __TEXT. $out/kc-build.log is kc-builder's log.
let
  base = import ../m1-apcie-host/build-kc.nix;
  probe = import ../m1-gpu-rtkit/build-probe-kext.nix;
  gpuDt = import ../m1-gpu-dt/build-kext.nix;
  needle = "  -o kernel";
  marginPages = 4;
in base.overrideAttrs (old:
  assert builtins.stringLength old.buildPhase
    != builtins.stringLength (builtins.replaceStrings [ needle ] [ "" ] old.buildPhase);
  {
    buildPhase = ''
      set -o pipefail
      {
      '' + builtins.replaceStrings [ needle ] [ ''  -kext "${gpuDt}/System/Library/Extensions/SFGpuDT.kext" \
      -kext "${probe}/System/Library/Extensions/SFRtkitProbe.kext" \
      -o kernel'' ] old.buildPhase + ''
      } 2>&1 | tee kc-build.log

      grep -q 'loaded kext: org.siliconforest.driver.SFRtkitProbe' kc-build.log
      grep -q 'loaded kext: org.siliconforest.driver.SFGpuDT' kc-build.log
      awk '/patched kmod_info: org.siliconforest.driver.SFRtkitProbe/ { print buf; exit }
           /merged [0-9]+ symbols from/ { buf = ""; next }
           { buf = buf $0 "\n" }' kc-build.log | grep -o 'UNRESOLVED.*' | sed 's/.*: //' | sort -u > probe-unresolved
      echo "SFRtkitProbe unresolved: $(tr '\n' ' ' < probe-unresolved)"
      if grep -v -x -e dyld_stub_binder -e _sf_usb_module -e _sf_usb_register -e _sf_usb_unregister probe-unresolved; then
        echo "SFRtkitProbe has unresolved symbols beyond the layer's known ones" >&2
        exit 1
      fi
      awk '/patched kmod_info: org.siliconforest.driver.SFGpuDT/ { print buf; exit }
           /merged [0-9]+ symbols from/ { buf = ""; next }
           { buf = buf $0 "\n" }' kc-build.log | grep -o 'UNRESOLVED.*' | sed 's/.*: //' | sort -u > dt-unresolved
      echo "SFGpuDT unresolved: $(tr '\n' ' ' < dt-unresolved)"
      if grep -v -x -e dyld_stub_binder dt-unresolved; then
        echo "SFGpuDT has unresolved symbols" >&2
        exit 1
      fi

      last=$(grep -E '^ *kext\[[0-9]+\] .*vmaddr=0x[0-9a-f]+ size=0x[0-9a-f]+' kc-build.log | tail -1)
      khdr=$(grep -oE 'fileset: kernel hdr vmaddr=0x[0-9a-f]+' kc-build.log | tail -1 | grep -oE '0x[0-9a-f]+')
      va=$(printf '%s' "$last" | grep -oE 'vmaddr=0x[0-9a-f]+' | cut -d= -f2)
      sz=$(printf '%s' "$last" | grep -oE 'size=0x[0-9a-f]+' | cut -d= -f2)
      test -n "$va" -a -n "$sz" -a -n "$khdr"
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
