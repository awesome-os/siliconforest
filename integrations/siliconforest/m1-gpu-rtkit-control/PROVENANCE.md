# Source provenance

New source and documentation are original; license selection remains pending.
No upstream implementation is copied. The inputs are local MIT m1n1 source at
`sources/m1n1-baseline`, Omacom pin
`121fcbf35d4501814b58ca5347926a4173b6a512`.

`reports/sprint-010/gpu-context-firmware-ownership-source-001.json` records hashes
and exact Git-object verification for22 selected MIT source/license files. The
test script reverifies every entry and four additional ASC buffer definitions
against that same pin. It never imports or executes reference Python code.

The concrete wire facts are:

- `src/rtkit.c:24-86,605-778`: management type bits59:52, protocol versions11..12,
  HELLO range bits15:0/31:16, EPMAP bitmap31:0/base34:32/LAST51, ACK MORE bit0,
  IOP_INIT0x220 and control sequencing. Python `fw/asc/mgmt.py` agrees on these
  fields but picks the maximum offered version without intersection checking;
  this original implementation uses the supported C interval11..12.
- `proxyclient/m1n1/fw/agx/__init__.py:66-70`: GPU application endpoints0x20/0x21.
  Their advertisement is required for discovery, without starting them.
- `src/rtkit.c:33-35,302-345` and
  `proxyclient/m1n1/fw/asc/{crash,syslog,ioreporting}.py`: buffer size is an8-bit
  count of4KiB pages; C's address field is42 bits and the Python fields are44.
  This leaf records the common42-bit subset and refuses ambiguous upper bits.
- `src/rtkit.c:44-49` versus `proxyclient/m1n1/fw/asc/oslog.py`: OSLog layouts
  differ; this leaf records raw requests and blocks without interpretation.

The eight-map,128-step, five-second maximum, unique-map and strict reserved-field
rules are deliberately bounded implementation policies. They are not claims
about every valid firmware implementation or measured firmware timing. Unknown
system endpoints refuse; additional application endpoint advertisements are
recorded but never enabled or dispatched. Diagnostic buffers never become owned
memory or transmit responses.

Compilation uses the existing immutable OSS SDK
`6zpwyx6p9swad42f0vx6rl4z0dp2kj1g-puredarwin-oss-sdk-26.5`, ARM64 kernel headers
`rxi8m8ki35zs9pdj2vnis26wjb1cdfcb-puredarwin-xnu-headers-arm64-0.1`, and toolchain
`v0x4y2cjbasgmrqsbka2bp8allw8j9ph-darwin-cross-toolchain-nix`.
The result hashes all actual compiler dependencies. This is an object compile,
not a reproduced SDK derivation, linked runtime ABI, KC or device result.

Both frozen UAT leaves are hash-checked and remain unchanged. They are not
linked into this control engine: discovery does not allocate memory or map tables.
No Linux reference implementation or GPL code is used in this leaf.
