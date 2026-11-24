# SPDX-License-Identifier: MIT
# The lab's default libSystem (../libsystem-default) plus upstream's newer sandbox profile
# compiler, taken from upstream's tip as one discrete patch (patches/0007, upstream commit
# 1ebd97cb, the libsandbox part): SBPL function definitions, (import ...), named profiles from
# /System/Library/Sandbox/Profiles. The official Firefox's process sandbox profiles use these;
# with the pin's compiler sandbox_init() rejects them ("define expects a name and a value").
# The kernel side (Sandbox.kext) is the same at the pin and the tip.
(import ../libsystem-default/build-libsystem.nix).overrideAttrs (old: {
  pname = "siliconforest-libsystem-cocoa";
  # tip cherry-pick (upstream 7f10e1a1, one line of it): proc_name is in libproc at the pin but
  # not exported; Firefox binds it lazily and aborts when it is first called. Appended rather
  # than patched, because the default libSystem's own patch also edits the export list.
  postPatch = (old.postPatch or "") + ''
    grep -qx _proc_name src/Libraries/libSystem/stub/libSystem.exports || echo _proc_name >> src/Libraries/libSystem/stub/libSystem.exports
  '';
  patches = (old.patches or [ ]) ++ [
    ./patches/0007-tip-libsandbox-sbpl-functions-and-profiles.patch
    # ours, on top of the tip's compiler: operation names as values, string functions, numbers
    # and comparisons, when/unless, defined?, a profile's own function as a filter, and
    # require-all / require-not / with-filter compiled to the strict side (what cannot be
    # checked makes a deny rule broader and leaves an allow rule out). With it all six process
    # profiles of Firefox 160 compile (checked on the build host against Mozilla's sources).
    ./patches/0008-libsandbox-profile-language-for-firefox.patch
    # tip cherry-pick (upstream 1ebd97cb, the dirhelper part): per-user local, temporary and cache
    # directories for confstr(_CS_DARWIN_USER_*). Firefox's content and GPU processes ask for
    # the cache directory before they start their sandbox and stop without it.
    ./patches/0010-tip-libsystem-per-user-directories.patch
    # ours: a profile's permission for the window server's Mach service
    # (com.apple.windowserver.active) is also written as what it means here, an outbound
    # connection to the compositor's socket. Profiles without it get nothing.
    ./patches/0014-libsandbox-window-server-permission-is-the-compositor-socket.patch
  ];
})
