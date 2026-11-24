# SPDX-License-Identifier: MIT
# Reconciliation candidate: Finch buffer/format code, the exact PD display-link
# source used by the live97-export library, and Finch's independent host clock.
# Whole Finch candidate remains build.nix; this variant is staged separately.
let
  e = import ../environment.nix;
  a = import ../../cocoa/arm64.nix;
  whole = import ./build.nix;
  hostClock = e.pkgs.writeText "finch-cv-host-clock.c" ''
    /* SPDX-License-Identifier: MIT OR Apache-2.0 */
    /* Host clock/value portion of Finch2d9b1bb CoreVideo/CVDisplayLink.c. */
    #include <CoreVideo/CoreVideo.h>
    #include <mach/mach_time.h>
    uint64_t CVGetCurrentHostTime(void) { return mach_absolute_time(); }
    double CVGetHostClockFrequency(void) {
      mach_timebase_info_data_t tb; mach_timebase_info(&tb);
      return 1e9 * tb.denom / tb.numer;
    }
    uint32_t CVGetHostClockMinimumTimeDelta(void) { return 1; }
    const CVTime kCVZeroTime = {0, 0, 0};
    const CVTime kCVIndefiniteTime = {0, 0, kCVTimeIsIndefinite};
  '';
in whole.overrideAttrs (old: {
  name = "siliconforest-finch-CoreVideo-pd-displaylink-arm64";
  buildPhase = let
    skipped = builtins.replaceStrings
      [ "for s in userland/CoreVideo/*.[cm]; do" ]
      [ ''for s in userland/CoreVideo/*.[cm]; do
          if [ "$(basename "$s")" = CVDisplayLink.c ]; then continue; fi'' ] old.buildPhase;
    extra = ''
      # Only this TU uses PD's original NSObject-based display-link declarations.
      SFCF=$(printf '%s' "$CF" | sed 's#-I[^ ]*/headers ##')
      ${e.cc} $SFCF -fno-objc-arc -I${a.src}/src/Frameworks -c ${a.src}/src/Frameworks/CoreVideo/CVDisplayLink.m -o objects/SFDisplayLink.o
      ${e.cc} $CF -c ${hostClock} -o objects/host-clock.o
    '';
  in builtins.replaceStrings [ "${e.cc} -dynamiclib objects/*.o" ]
       [ (extra + "${e.cc} -dynamiclib objects/*.o") ] skipped;
  installPhase = old.installPhase + ''
    cat >> $out/System/Library/Frameworks/CoreVideo.framework/Versions/A/Headers/Generated.h <<'API'
uint64_t CVGetCurrentHostTime(void);
double CVGetHostClockFrequency(void);
uint32_t CVGetHostClockMinimumTimeDelta(void);
extern const CVTime kCVZeroTime;
extern const CVTime kCVIndefiniteTime;
API
  '';
})
