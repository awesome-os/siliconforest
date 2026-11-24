# Per-file provenance

`default_route.{c,h}`, `route_wire.{c,h}`, `inventory.{c,h}`, `native.{c,h}`,
`route_abi.c`, `test_default.{c,py}`, `test_inventory.c`, `build.nix` and these
notes are original code/text for this task. Project license selection is pending.
No GPL implementation was copied or translated, and no upstream source license
was changed.

`owned/` and `route/` copy the project's original frozen
`m1-network-egress-guest/interfaces/` and `route/` helper files. The two unchanged
interface files preserve their prior bytes. Changes in this leaf are: peer2-only,
one phase; no unused global-IP validator; and GET-only admission in the inherited
query wire codec. The copied raw ADD/DELETE prototype owned_routes.c is absent.
The source receipt records all predecessor and current hashes. Frozen originals
and earlier packets/reports are untouched.

Numerical routing ABI is checked by actual pinned SDK static assertions.
Kernel behavior was read in PureDarwin pin
e7aa00a60f0b56ef35aa24252e81467787bf71ea; exact selected hashes and line references
are in reports/sprint-010/network-persistent-owned-default-source-001.json.
The default GLOBAL branch is additionally checked in the real native50 KC by
network-default-route-native50-global-bit-001.json. Interface lifetime/metadata
substrate is based on the source-bound helper exercised by public002; that runtime
does not establish this new default-route lifetime.

No Apple device firmware, private dumps, identifiers or macOS kext is included.
Build outputs and actual SDK headers stay outside tracked source. Object imports
are audited against the retained libSystem9fc and libdyld closure, not a host libc.
