# Separate candidate: preserve the qualified framebuffer outputs and kernel003.
let
  base = import ../../chonkstep-darwin/build-framebuffer.nix;
in base.overrideAttrs (old: {
  pname = "siliconforest-m1-console-return-framebuffer";
  patches = (old.patches or []) ++ [ ./console-return.patch ];
})
