# Darwin Wayland SSD client

Separate offline candidate copied from `../client` (the original source and
executable remain unchanged). The binary is named `darwin-wayland-client-ssd`:

```sh
darwin-wayland-client-ssd --socket ABSOLUTE_PATH --nonce HEX48 --seconds N
```

`N` is 1–120 seconds. This keeps the original 800x480 XRGB8888 SHM client,
unlinked mode-0600 backing file, buffer-release discipline, nonce text, RGB
bars, moving marker and frame callbacks. It adds no seat, input, GPU or network
backend. This is protocol qualification, not a fix for the original client's
valid edge-only/default client-side decoration policy.

The client requires the real `zxdg_decoration_manager_v1` global, binds version
1, creates a decoration object before any buffer attachment, and requests
`ServerSide` once before its initial empty surface commit. A decoration configure
is pending state: only the following `xdg_surface.configure`, acknowledged with
its real serial, applies it. A surface configure alone cannot authorize drawing,
nor can an earlier acknowledgement apply a later decoration change. While a new
decoration mode is pending, attachments pause. An applied client-side mode or an
unknown mode is a terminal error; no fallback or synthetic configure is used.

`DL10_SSD_CONFIGURED NONCE mode=server_side serial=N` records receipt/application
of that mode and queuing its acknowledgement, before the first buffer attachment.
It is not a frame or panel result. `DL10_CLIENT_FRAME NONCE N` retains its original
meaning: the client received a compositor frame callback. Pixel capture must
independently show the matching client and server-owned titlebar/chrome. No
callback, no acknowledged SSD configuration, connection/protocol error or refusal
exits nonzero. At normal completion the decoration object is destroyed before its
toplevel; closing the client does not close the separate compositor's SFGOP owner.

The main loop polls at 50 ms intervals and has the original 1–120-second deadline,
including the SSD handshake. The inherited initial registry roundtrip remains
blocking, so the existing independent host-worker timeout is still required.
Do not infer an end-to-end process deadline from the guest loop alone.

Offline checks:

```sh
cargo test --offline --locked --target-dir target
cargo build --offline --locked --target-dir target
python3 -B test_source_contract.py
```

The Rust tests exercise pending/acknowledged mode transitions and refusals only;
they do not connect to a compositor or fabricate runtime evidence. The source
checks bind those state transitions to the actual callbacks/attach gate and
verify that the inherited pixel workload and dependency pins remain intact.
Linux host compilation is not Darwin linking or execution. Root owns the later
native cross-build, staging, independent review and any authorized hardware test.
