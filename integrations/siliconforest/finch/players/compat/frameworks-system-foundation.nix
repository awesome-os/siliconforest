# SPDX-License-Identifier: MIT
# TERM8 installed these operations in Foundation proper on 2026-11-15.
# Keep unavailable AppleScript + the existing framework/Swift exports, without
# overriding the installed defaults/calendar/locale methods a second time.
let
 e=import ../../environment.nix;
 base=import ./frameworks.nix;
in base.overrideAttrs(old:{
 name="siliconforest-vlc-unavailable-frameworks-system-foundation-arm64";
 buildCommand=e.pkgs.lib.replaceStrings [
  "${e.cc} $CF -c ${./defaults.m} -o Defaults.o"
  "${e.cc} $CF -c ${./calendar-operations.m} -o Calendar-operations.o"
  "${e.cc} $CF -c ${./locale-operations.m} -o Locale-operations.o"
  "Foundation.o Defaults.o Calendar-operations.o Locale-operations.o Foundation-compat.o"
 ] [ "" "" "" "Foundation.o Foundation-compat.o" ] old.buildCommand;
})
