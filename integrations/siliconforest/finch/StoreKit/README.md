Whole Finch StoreKit.m and constants, with our source-derived declarations.
Linux/Nix build uses COCOA3's rehearsed Foundation update for real NSDecimalNumber
and NSHashTable object/weak methods. No SDK input or substitute Foundation class.

M1 staged and installed without DYLD overrides: assertions PASS for product price,
payment copy, weak observer lifetime, offline product-request callback, refused
purchase, finish/removal and refused restoration callbacks. Main-queue callbacks
must arrive within five seconds. Import scan clean,60 exports,version379/compat1,
ad-hoc hashes valid. Installed only at the previously absent framework path and
recorded in the shared hash/uninstall ledger; no existing file replaced.

Finch provides value models and explicit unavailable-service responses. It has
no App Store, payments or review UI; GUI entry points are not exercised. The
probe is a candidate assertion check, not a native StoreKit comparison program.
