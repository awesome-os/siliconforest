# Desktop candidate only. Preserve the qualified display-shell bundle separately.
let
  base = import ../m1-display/build-framebuffer.nix;
in base.overrideAttrs (old: {
  pname = "siliconforest-m1-wayland-framebuffer";
  patches = (old.patches or []) ++ [ ../m1-wayland/iogop-vram-write-combine.patch ];
})
