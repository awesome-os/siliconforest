#!/bin/sh
R=/private/tmp/sfy/root
export XDG_RUNTIME_DIR=/private/var/run/gpu-desktop WAYLAND_DISPLAY=wayland-0
export DYLD_INSERT_LIBRARIES=$R/System/Library/Frameworks/AppKit.framework/Versions/A/AppKit DYLD_FRAMEWORK_PATH=$R/System/Library/Frameworks:$R/System/Library/PrivateFrameworks DYLD_LIBRARY_PATH=$R/usr/lib:$R/usr/lib/swift:/private/tmp/sfy/sf
exec "$@"
