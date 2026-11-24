# Port-only diagnostic selection, revision 002

This is an isolated, unapplied host-source change. It is not a new packet,
deployment, boot permission or requalification of readonly PCIe attempt001.

That attempt has completed diagnostic markers1 through13, followed by a started
`framebuffer-pixels` marker14 with no completion marker. Its console and raw
context were already finalized, but the host worker timed out. These records
locate the unfinished stage; they do not establish the lower-level reason the
read or its completion stopped progressing. The original failed7/10 receipt,
actual capture and controller log remain unchanged.

The patch changes exactly one constructor argument in the separately reviewed
port host source:

```python
capture_framebuffer=not (keyboard or apcie_port_observer_enabled(inputs))
```

The existing exact typed port experiment gate selects this path. Ordinary
desktop captures remain enabled; keyboard captures remain skipped. A port-only
collector still records its raw property, console, owned context, global and
register diagnostics. It skips the optional framebuffer/carveout diagnostic
reads and explicitly records `framebuffer_requested=false`. It cannot claim
framebuffer pixels, console return in pixels or physical-panel observation.
No recovery, timeout, guest-input, kernel, collector or hardware gating changes.

`binding-002.json` describes a new private sixteen-file test tree. Only the host
source differs from revision001; the controller and other fifteen files are
unchanged. The canonical host, M5 installation, candidate003 and actual saved
source snapshots are untouched. Existing packet source hashes do not bind this
new host, so it must not be used with that packet. A future reviewed packet and
explicit hardware admission would still be required; neither exists here.

The focused test reads the exact worker constructor expression and exercises
the real `ProgressCapture.diagnostics` on mocked target boundaries. Five global
reads and seven register reads complete in the skip fixture, with no framebuffer
or carveout call; the desktop fixture additionally captures the exact pixels.
The old selector and an always-skip selector both fail the relevant observations.
These are source/host tests, not a replay of the hardware timeout or a guarantee
that another diagnostic operation cannot block.

```sh
python3 integrations/puredarwin/m1-apcie-port-packet/diagnostics-revision-002/test_diagnostics.py
```

The required isolated test tree is intentionally private and identified in the
binding record. This directory does not create, upload, deploy or run a target.
