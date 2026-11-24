# Two-window live session script

`darwin-two-window-live-test` is a guest supervisor for root to stage as
`/usr/bin/darwin-desktop-test`. It does not replace the passive-child
supervisor, the clients, or the compositor.

Gating matches reviewed `darwin-passive-child-test` `c361a868`: trackpad
enumeration, a stat of both `/dev/usb_hid_kbd` and `/dev/usb_hid_mouse`, then
one `darwin-native-input-inventory` run. The compositor command is the existing
Both seat, `/usr/bin/chonkstep-darwin --run --socket ... --input pdhid`.

After the socket exists it starts the existing SSD client and then the existing
native startup observer, each with `--socket`, `--nonce`, and `--seconds 120`.
Eighty-five seconds later it runs the existing `darwin-terminal-records
--phase live` export. That export is a prefix snapshot. The script records
`cleanup_claimed=false` and does not print frame, chrome, READY, or SSD
configure lines.

Host `--runtime` stays 180 and is not changed here. A captured acceptance is a
later same-boot framebuffer plus the terminal replay, READY, an SSD configure,
no error, sealed TX, and Linux. This directory does not claim that result.

`sh -n` and `test_two_window_supervisor.py` exercise branches with stubs. The
stub rewrites the character-device checks to regular files and does not run
guest binaries.
