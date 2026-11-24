# SPDX-License-Identifier: MIT
# The lab kernel on the rebase target (SF_PD=tip, ./pd.nix): upstream's own T8103 kernel
# derivation with the lab's kernel series applied to its source, in one list.
#
# On the pin the same kernel is assembled by a chain of recipes, each adding its patches to
# the one before (m1-dcp/kernel.nix: the bring-up source snapshot and sf_lfile; m1-smp;
# m1-quiet-boot; m1-boot-speed). Those recipes are not changed and keep building the pin.
# Here the order is the same and every patch is the pin's own file unless a port was needed,
# in which case it is the file of the same number in <integration>/kernel-patches-tip/:
#
#   m1-bringup/kernel-patches-tip/0001   the bring-up base, merged with upstream's T8103 (ported)
#   m1-dcp/kernel-patches/0001..0003     sf_lfile: sendable, select/poll/kevent, mmap
#   m1-dcp/kernel-patches/0004           T8103 may be built as DEVELOPMENT (development only)
#   m1-smp/kernel-patches/0001           AppleARMSMP passes the reset vector to IOPMGR
#   m1-smp/kernel-patches/0002           dropped: upstream has it (cpu.c, the MIDR line uses kprintf)
#   m1-smp/kernel-patches-tip/0003       park offline cores, idle accounting, deep idle (ported)
#   m1-quiet-boot/kernel-patches/0001,0002
#   m1-boot-speed/kernel-patches/0001    registry-quiet wait ignores the daemon hold
#   m1-native-boot/kernel-patches/0001   IOSetRecoveryBoot does nothing (in every kernel since the switch)
#
#   nix-build kernel.nix --no-out-link                       # DEVELOPMENT, as the default collection
#   nix-build kernel.nix --argstr kernelConfig debug --no-out-link
#   `upTo` stops the series early, for bisecting: "bringup", "dcp", "smp", "quiet" or "all".
#   `bootSpeed = false` leaves out m1-boot-speed's patch alone: the kernel that does not know
#   sf_quiet_wait/sf_rtc_wait (the "before" kernel of m1-boot-speed/tools/bs_boot.sh BS_KC=stock).
{ kernelConfig ? "development", upTo ? "all", bootSpeed ? true }:
let
  source = import ./pd.nix;
  inherit (source) pd pkgs;
  configs = { debug = "DEBUG"; development = "DEVELOPMENT"; };
  config = configs.${kernelConfig};
  configNeedle = "-DSILICONFOREST_XNU_KERNEL_CONFIG=DEBUG";
  stages = [ "bringup" "dcp" "smp" "quiet" "all" ];
  rank = name: (pkgs.lib.lists.findFirstIndex (s: s == name) (throw "upTo=${name}: one of ${toString stages}") stages);
  through = name: rank upTo >= rank name;
  series =
    [ ../m1-bringup/kernel-patches-tip/0001-xnu-m1-bringup-on-upstream-t8103.patch ]
    ++ pkgs.lib.optionals (through "dcp") ([
      ../m1-dcp/kernel-patches/0001-xnu-sflfile-descriptors-are-sendable.patch
      ../m1-dcp/kernel-patches/0002-xnu-sflfile-select-poll-kevent-waiters.patch
      ../m1-dcp/kernel-patches/0003-xnu-sflfile-mmap.patch
    ] ++ pkgs.lib.optionals (kernelConfig != "debug") [
      ../m1-dcp/kernel-patches/0004-xnu-t8103-may-be-built-as-development.patch
    ])
    ++ pkgs.lib.optionals (through "smp") [
      ../m1-smp/kernel-patches/0001-xnu-applearmsmp-pass-reset-vector-to-iopmgr.patch
      ../m1-smp/kernel-patches-tip/0003-xnu-park-offline-cores-idle-accounting-deep-idle.patch
    ]
    ++ pkgs.lib.optionals (through "quiet") [
      ../m1-quiet-boot/kernel-patches/0001-xnu-noprogress-refuses-user-progress-meter.patch
      ../m1-quiet-boot/kernel-patches/0002-xnu-graphics-boot-replays-early-log-to-uart.patch
    ]
    ++ pkgs.lib.optionals (through "all" && bootSpeed) [
      ../m1-boot-speed/kernel-patches/0001-xnu-registry-quiet-wait-ignores-the-daemon-hold.patch
    ]
    ++ pkgs.lib.optionals (through "all") [
      # a safety fix that belongs in every kernel: IOSetRecoveryBoot logs and returns, so no
      # panic or failed boot can ask the machine's firmware for recovery mode
      ../m1-native-boot/kernel-patches/0001-xnu-IOSetRecoveryBoot-does-nothing.patch
    ];
  base = pd.packages.x86_64-linux.kernel-arm64-t8103-debug;
in
assert source.tip || throw "rebase/kernel.nix is the tip's kernel: run it with SF_PD=tip (the pin's is m1-boot-speed/kernel.nix)";
assert kernelConfig == "debug" || through "dcp" || throw "DEVELOPMENT needs m1-dcp's 0004: upTo=dcp or later";
base.overrideAttrs (old:
  assert builtins.stringLength old.configurePhase
    != builtins.stringLength (builtins.replaceStrings [ configNeedle ] [ "" ] old.configurePhase);
  {
    pname = "siliconforest-kernel-arm64-t8103-siliconforest";
    name = "siliconforest-kernel-arm64-t8103-siliconforest-0.1";
    patches = (old.patches or [ ]) ++ series;
    configurePhase = builtins.replaceStrings [ configNeedle ]
      [ "-DSILICONFOREST_XNU_KERNEL_CONFIG=${config}" ] old.configurePhase;
  })
