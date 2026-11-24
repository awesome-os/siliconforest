Whole handwritten Finch Intents.m and its runtime generator ported to our
Linux/Nix toolchain. The generator takes our declarations, not an SDK. Our model
catalogue covers classes explicitly used in Finch source/tests; it does not
reproduce the much larger SDK-derived catalogue. No Foundation stand-ins are
emitted. The whole source slice now builds against COCOA3's installed Foundation
prerequisites and has clean imports. Own catalogue also includes INFile name/type
properties and NSString's source-defined variadic localization methods.
Original value/archive test FAILS on NSUserActivity targetContentIdentifier;
original isolated interaction-store test FAILS on NSDateInterval encodeWithCoder:.
Earlier tested value/archive cases complete before those failures; this is not a
passing whole-framework conformance result. Remains staged, not installed.
`declarations.py sources/finch/userland/Intents` regenerates our header.
