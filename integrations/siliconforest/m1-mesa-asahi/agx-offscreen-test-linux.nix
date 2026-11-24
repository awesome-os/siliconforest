# The test program and the ioctl tracer for the x86_64-linux builder, against the native build of
# the same Mesa.  Nothing here touches a GPU: `run-shim` runs the real asahi driver on Mesa's asahi
# drm-shim (no hardware, garbage pixels) to record its ioctl sequence, `run-sw` runs the scene on
# llvmpipe to validate the test's own expectations.
let
  c = import ./common.nix;
  inherit (c) pkgs;
  mesa = import ./mesa-native.nix;
in pkgs.stdenv.mkDerivation {
  name = "darwin-lab-agx-offscreen-test-linux";
  dontUnpack = true;
  buildInputs = [ pkgs.libdrm ];
  nativeBuildInputs = [ pkgs.pkg-config ];
  buildPhase = ''
    cc -O2 -Wall -Wextra -I${mesa}/include $(pkg-config --cflags libdrm) ${./test/agx-offscreen-test.c} \
      -L${mesa}/lib -Wl,-rpath,${mesa}/lib -lEGL -lGLESv2 $(pkg-config --libs libdrm) -o agx-offscreen-test
    cc -O2 -Wall -Wextra -I${mesa}/include $(pkg-config --cflags libdrm) ${./test/agx-panel-demo.c} \
      -L${mesa}/lib -Wl,-rpath,${mesa}/lib -lEGL -lGLESv2 $(pkg-config --libs libdrm) -lm -o agx-panel-demo
    cc -O2 -Wall -shared -fPIC ${./trace/ioctl-trace.c} -ldl -o libagx-ioctl-trace.so
  '';
  installPhase = ''
    mkdir -p $out/bin $out/lib
    cp agx-offscreen-test agx-panel-demo $out/bin/
    cp libagx-ioctl-trace.so $out/lib/
    cat > $out/bin/run-shim <<EOF
    #!/bin/sh
    # usage: run-shim <trace-file> [test args]; the shim impersonates the M1's G13G
    t=\$1; shift
    AGX_IOCTL_TRACE=\$t AGX_IOCTL_TRACE_FAKE_SYNC_FILE=1 ASAHI_SHIM_CHIP=g13g MESA_SHADER_CACHE_DISABLE=1 \\
    LD_PRELOAD=$out/lib/libagx-ioctl-trace.so:${mesa}/lib/libasahi_noop_drm_shim.so \\
      exec $out/bin/agx-offscreen-test "\$@"
    EOF
    cat > $out/bin/run-sw <<EOF
    #!/bin/sh
    LIBGL_ALWAYS_SOFTWARE=1 exec $out/bin/agx-offscreen-test --no-drm "\$@"
    EOF
    sed -i 's/^    //' $out/bin/run-shim $out/bin/run-sw
    chmod +x $out/bin/run-shim $out/bin/run-sw
  '';
}
