# External DCP / DPTX port (SF-AF milestone 2, in progress)

`src/sf_dcpext_plan.c` validates the real J293/T8103 ADT resource layout
before an external DCP instance can be published. It checks dcpext, five
dispext windows, DCP/display DARTs, bandwidth scratch/doorbell, mailbox IRQs
and the two external firmware carve-outs (73/74), and restricts firmware to
the installed 13.5-compatible ABI. Unknown/partial data leaves the output
unchanged. It performs no MMIO and invents no firmware IOVAs or EDID.

The target is port 1, physical ATC/DPTX index 1, remote DCP core 0, one
external stream. The display PIODMA uses stream 4 of the display DART; it
must not be confused with the second ADT register bank. The resource plan
is not yet a synthetic device tree or a driver attachment.

The offline SFDcp tunnel request/epoch bridge in `../m1-dcp` now compiles the
actual `apple_dcp_tb_dp_tunnel` call behind a versioned native48-byte ABI and
immutable callback generation. Actual-source host tests cover identity,
synchronous activation and retirement/error paths. This closes a C ABI boundary,
not the missing native provider publication or route/PHY/resource graph. No
DP trial is scheduled while the owner has paused the machine.

The actual directly ported T8103 DP AUX/link-rate and Thunderbolt pixel
clock programs live in `../m1-atcphy`; Thunderbolt DP-IN lifecycle/handshake
lives in `../m1-thunderbolt`. Their host tests do not establish a trained
link, powered external DCP, scanout or a second compositor output.

Next integration:

- publish external DCP/mailbox/DART/clock/firmware and port-1 route nodes;
- bind `/arm-io/dcpext` to its own RTKit device, rather than the internal
  DCP's hardcoded mailbox path; serialize platform initialization;
- supply real generic-PHY, crossbar and Type-C mux providers to the existing
  Linux DPTX endpoint code, with cable-generation/lifetime checks;
- integrate the SDK-built mirrored display-DART binding (`2cd017c99`) with
  the real external topology so both scanout paths see one framebuffer IOVA;
- retain the checked DMA retirement and failure quarantine (`5e3b77534`),
  and establish actual external producer/IRQ/work quiescence before cleanup;
- start the M1 external IOMFB pipeline, then observe HPD/training/EDID/mode
  and a connector before DESK4's multi-output compositor integration.

The `dcpext_scanout` stubs in the present DCP bundle concern later-chip
native external pipelines; T8103's normal IOMFB path is already compiled.
That does not make the missing PHY/topology/DMA plumbing optional.

`host-test/run.sh` uses synthetic ADT resources and ASan/UBSan: valid layout,
223 absent/truncated property cases, overflow and unsupported ABI/chip
refusals. No live firmware/address fixture is checked in.

`sf_dpxbar.c` and `upstream/dpxbar-t8103.inc` now port the T8103 crossbar
program for the direct DP PHY and both Thunderbolt DP-IN outputs. The
extractor preserves the upstream register bodies, selects T8103's two
inputs/cycle-slip policy, and adapts MMIO/delay/external serialization.
It includes source selection, duplicate-input refusal, DP-IN preselection,
link clock stop/start and disconnect while preserving unrelated fields.
The native caller must validate/power port1's 0x50304c000 resource, drain
old users and hold the shared ATC/crossbar lock and mux lease. Init does
not prove idle hardware. Gate status is diagnostic only, as upstream;
programming success is not HPD/training or clock readiness evidence.

Host ASan/UBSan tests pass 192 PHY/DP-IN select/link/disconnect cycles plus
duplicate-source/preselection/shared-field/gate ordering and stuck-status
cases. SDK objects build. No crossbar program is attached or executed on
the M1; generic PHY/mux/Type-C registration and the native port owner are
still required before external DCP can use it.
