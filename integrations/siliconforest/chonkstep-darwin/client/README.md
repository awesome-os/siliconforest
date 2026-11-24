# Darwin Wayland client

`darwin-wayland-client --socket ABSOLUTE --nonce HEX48 --seconds N` connects
to a real Wayland server, acknowledges xdg configure, and presents an 800x480
XRGB8888 SHM surface with the fresh nonce, RGB bars and a changing paint count.
It uses an unlinked, mode-0600 regular file and passes its descriptor through
Wayland. It rewrites the file only after `wl_buffer.release`.

The main loop polls at 50 ms intervals for 1–120 seconds. The initial registry
roundtrip is blocking; the independent host worker deadline remains necessary.
Each callback prints `DL10_CLIENT_FRAME`. On completion, zero frame callbacks
or any protocol/connection error exits 1; otherwise it exits 0. A callback proves
compositor progress, not physical panel scanout or keyboard input. It does not
bind a seat or test native input. The paint count is not the callback count.

Root built the ARM64 Mach-O with the pinned SiliconForest OSS SDK and linker.
Build/runtime evidence is recorded separately from the source and host checks.
