# SPDX-License-Identifier: MIT
# The QEMU desktop loop's base image on the rebase target (SF_PD=tip, ./pd.nix): what
# ../qemu-dev/build-image.nix and ../qemu-dev/build-kc.nix build at the pin (the QEMU stream's
# recipes, which name the pin and are not changed), from the tip: upstream's arm64 `virt`
# minimal image with the boot kexts rebuilt with that stream's two kext patches and the loader
# with its sfgopmode patch. All three patch files are that stream's, read from its directory.
#   SF_PD=tip nix-build qemu-image.nix [--argstr kernelConfig debug] --no-out-link
#   -A kc: the collection alone
{ kernelConfig ? "release", variant ? "minimal" }:
let
  source = import ./pd.nix;
  inherit (source) pd pkgs;
  p = pd.packages.x86_64-linux;
  kexts = p.kexts-arm64.overrideAttrs (old: {
    src = pkgs.applyPatches {
      name = "siliconforest-qemu-dev-kexts-source";
      src = old.src;
      patches = [ ../qemu-dev/sfhid-publish-keyboard.patch ../qemu-dev/virtio-net-name-interface.patch ];
    };
  });
  kc = (if kernelConfig == "debug" then p.kc-arm64-debug else p.kc-arm64).override { inherit kexts; };
  base = if variant == "wayland" then (if kernelConfig == "debug" then p.image-arm64-virt-wayland-debug else p.image-arm64-virt-wayland)
    else if kernelConfig == "debug" then p.image-arm64-virt-minimal else p.image-arm64-virt-minimal-release;
  image = base.override (old: {
    inherit kc;
    xnuLoader = old.xnuLoader.overrideAttrs (o: {
      patches = (o.patches or [ ]) ++ [ ../qemu-dev/loader-gop-mode.patch ];
    });
  });
in
assert source.tip || throw "rebase/qemu-image.nix is the tip's image: run it with SF_PD=tip (the pin's is qemu-dev/build-image.nix)";
image // { inherit kc; }
