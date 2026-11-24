# Network prerequisite audit (not built or staged)

Finch's Network implementation is twelve Swift files plus Network.m/globals/
generated constants. Its C facade is implemented by those Swift sources,not a
standalone C backend. No Apple SDK or Mac build is used here.

toolchain.nix assembles a Linux Swift6.3.3 cross compiler with PD's open-source
SDK declarations,Foundation/CoreFoundation/objc modules and the existing open
Darwin overlays. check-foundation.nix actually type-checks foundation-probe.swift.
The compiler imports Foundation but fails on the APIs Finch Network needs:
Data and URL are absent;UUID expression fails diagnostic production;NSLock.init
is imported optional and has no withLock. These are prerequisite failures,not
a compiled Network.framework or runtime result. No success marker is produced.

Required next dependency: the Foundation owner must provide real Darwin-target
Foundation value APIs/bridging and nullability,including Data/URL/UUID and lock
conveniences. Renaming NSData/NSURL in Network would change public Swift ABI and
would not supply the missing implementation. The existing Foundation overlay
contains a subset (String,Date,Array,Dictionary and basic bridging). It is owned
by the Cocoa stream and is retained unchanged here. An exact request and the
compiler log are in runs/m1-coord/requests-for-CLAUDE.md and runs/sffinch.

After Foundation:author our Network/Security/SystemConfiguration Clang modules,
compile all pinned Swift/C sources,port the needed Security protocol surface,
then run loopback C/Swift tests on M1 and same-byte native M5 comparisons.
Two explicit Finch Security helpers are also dependencies:
_FinchSecProtocolOptionsCopy and _FinchSecProtocolMetadataCreate. These are not
interoperable with arbitrary PD private contexts. Source builds must stay staged.
