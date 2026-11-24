# Read-only port-status wiring (host fake file only)

`apo_observe_fake_accessor` calls the existing, unchanged `apo_observe` using
`apr_bind_observer_fake` and the checked `apr_bind_fake` callback path. The only
exported observer capability is `dls_io.read32`. The shim translates the two
different region enums explicitly: PMGR/RC/PHY map to their APB counterparts,
and **DLS_PORT (3) maps to APB_PORT0 (6)**. DART and every pair outside the
observer's existing ten-register whitelist are refused before backend dispatch.

The observer still checks its exact board ranges, then power, PHY, PHYIF and
port RUN in order before reading LINKSTS. Final power rechecks, partial valid
bits, stage results and failure semantics are unchanged. Checked callback
reads preserve their output on a backend failure. Neither a fake READY setup
nor an OBSERVED result establishes live power, PCIe ownership or a hardware
link. The physical addresses in the existing board profile are compared as
integers only; they are never dereferenced or mapped.

Run `python3 integrations/puredarwin/m1-apcie-bridge/test_readonly_accessor.py`.
The harness includes the unchanged `test_linkup.c` fake arrays and callbacks.
It tests the exact 14-read trace, all staged refusals, lost power, every possible
read failure with a deliberately poisoned output, ten-pair-only dispatch, bad
boards and binding failures. Fake write, pin and delay counters must stay zero.
The original observer tests also run unchanged. Five compiled assertion-killed
mutants cover region translation, whitelist removal, bypassing the accessor,
swallowed I/O errors and invented success. ASan/UBSan are active with host leak
detection disabled. Default, KERNEL and shared-write-opt-in builds are refused;
the host assertion tests also refuse NDEBUG. The earlier accessor suite remains
the separate proof that shared PHY/PMGR writer branches are compiled out.

Only new host wiring/tests/docs are added. No old source, frozen receipt,
CMake/Nix recipe, IOKit adapter, kernel collection or target packet is changed.
No device access, real memory mapping, hardware action or retry is implemented.
All callback provenance and single-threaded lifetime restrictions of
`REGISTER_ACCESSOR.md` still apply; this is not an arbitrary-C sandbox.
