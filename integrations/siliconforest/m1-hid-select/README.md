# Native raw HID select preparation

Detached source, excluded from the installed kernel and dock candidates.
The existing `/dev/usb_hid_kbd` and `/dev/usb_hid_mouse` queues return EAGAIN
from reads and provide `eno_select`. DESK therefore polls at250Hz while idle.
This variant registers read selectors under each existing queue lock and
wakes them after an actual event is enqueued. It adds no polling timer and
implements standard `FIONREAD` byte counts for feature detection. Older
kernels reject that ioctl, so consumers can retain their existing fallback.
It changes neither event formats nor console grabbing. Queue nodes and selinfo
objects persist for kernel lifetime; unload is not implemented.

Before installation: compile against the native SDK, test real readiness and
timeout behavior, then coordinate the compositor's blocking poll loop with
DESK. A successful build is not input or idle-wakeup evidence. Existing
compositors keep their old polling behavior until their reader changes.
No part of this source establishes the reported AppKit slowdown's cause.

Native ARM64 IOHIDFamily SDK compilation passes. The host test extracts the
actual transformed enqueue/select/ioctl functions and runs4000 concurrent
registration/publication races with host locks, non-consuming byte counts,
unsupported requests and queue overflow under ASan/UBSan. These fixtures do
not implement XNU's select machinery; the target checks remain required. Run
`python3 host-test.py <actual-native-source-tree>` to reproduce.
