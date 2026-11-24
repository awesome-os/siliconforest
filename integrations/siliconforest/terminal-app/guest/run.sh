#!/bin/sh
# SPDX-License-Identifier: MIT
# Terminal.app from the scratch prefix /private/tmp/sfy (../tools/sf_terminal.sh push): the test
# copy of the binary, with the prefix's libraries in front of the system's.
R=/private/tmp/sfy/root
export DYLD_FRAMEWORK_PATH=$R/System/Library/Frameworks:$R/System/Library/PrivateFrameworks
export DYLD_LIBRARY_PATH=$R/usr/lib:$R/usr/lib/swift:/private/tmp/sfy/sf
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/private/var/run/gpu-desktop} WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0}
export HOME=${HOME:-/var/root}
exec $CATCH /private/tmp/sfy/Terminal.app/Contents/MacOS/Terminal.testdyld "$@"
