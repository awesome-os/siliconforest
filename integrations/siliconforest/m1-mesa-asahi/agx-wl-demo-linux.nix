# agx-wl-demo for the x86_64-linux builder, against nixpkgs' own Mesa and libwayland, with a
# script that runs it under a headless weston.  There is no GPU here: this exercises the client's
# Wayland and EGL logic and its buffer counting on the software path (wl_shm), where the demo
# must report a failure unless --allow-software is given.  It cannot exercise dma-buf.
let
  c = import ./common.nix;
  inherit (c) pkgs;
  xdgShell = "${pkgs.wayland-protocols}/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml";
in pkgs.stdenv.mkDerivation {
  name = "darwin-lab-agx-wl-demo-linux";
  dontUnpack = true;
  nativeBuildInputs = [ pkgs.pkg-config pkgs.wayland-scanner ];
  buildInputs = [ pkgs.wayland pkgs.libglvnd pkgs.libdrm ];
  buildPhase = ''
    wayland-scanner client-header ${xdgShell} xdg-shell-client-protocol.h
    wayland-scanner private-code ${xdgShell} xdg-shell-protocol.c
    cp ${./test/agx-panel-demo.c} agx-panel-demo.c
    cp ${./test/agx-wl-demo.c} agx-wl-demo.c
    cc -O2 -Wall -Wextra -Wno-unused-function -Wno-unused-variable -I. agx-wl-demo.c xdg-shell-protocol.c \
      $(pkg-config --cflags --libs wayland-client wayland-egl egl glesv2 libdrm) -lm -o agx-wl-demo
  '';
  installPhase = ''
    mkdir -p $out/bin
    cp agx-wl-demo $out/bin/
    cat > $out/bin/run-weston-test <<EOF
    #!/bin/sh
    # usage: run-weston-test [agx-wl-demo args]; headless weston, software rendering
    export XDG_RUNTIME_DIR=\$(mktemp -d)
    export __EGL_VENDOR_LIBRARY_FILENAMES=${pkgs.mesa}/share/glvnd/egl_vendor.d/50_mesa.json
    export LIBGL_ALWAYS_SOFTWARE=1
    ${pkgs.weston}/bin/weston --backend=headless --socket=agx-test --idle-time=0 >\$XDG_RUNTIME_DIR/weston.log 2>&1 &
    wpid=\$!
    for i in 1 2 3 4 5 6 7 8 9 10; do [ -S \$XDG_RUNTIME_DIR/agx-test ] && break; sleep 0.3; done
    WAYLAND_DISPLAY=agx-test $out/bin/agx-wl-demo "\$@"
    rc=\$?
    kill \$wpid 2>/dev/null
    exit \$rc
    EOF
    sed -i 's/^    //' $out/bin/run-weston-test
    chmod +x $out/bin/run-weston-test
  '';
}
