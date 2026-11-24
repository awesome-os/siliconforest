# CoreData Objective-C + SQLite (staged only)

All fourteen Objective-C implementation/constant files from Finch2d9b1bb,including
model/context/request,SQLite,in-memory,custom/atomic,XML,binary and migration code.
Own source-derived declarations plus public numeric/inheritance facts. Build in
Linux/Nix against retained Foundation compatibility updates and PD SQLite3.51.2.
The optional Swift/Combine overlay is excluded; no Swift API compatibility claim.
The model hash sort uses PD's comparator-block API with identical ordering.
SQLite implementation symbols stay private inside this library. The independent
inspection probe embeds its own Linux-built SQLite to avoid binding native calls
to CoreData's private backend. No SQLite object crosses that boundary.

Live M1 CoreData157 exports; candidate279 retains47/157. The110 missing names are
five old PD helper classes+metaclasses,99 ivars and keyObjectForSelector. Class
layouts/private contexts differ: this is a complete isolated provider family,
never a mixed-object or blind replacement. Existing CoreData is untouched.

M1 fresh SQLite probe MATCH identical Linux-built native M5 program:insert3,
close/reopen3,predicate2,update to9,delete1,final2;direct read-only SQL verifies
ZITEM values and Z_METADATA. File URL is explicitly absolute because PD's current
Foundation keeps relative NSURL paths relative; first candidate relative-path
failure is retained. Test output files stay in the unique packet directory.
Original28-line model/version-hash comparison also MATCH same-byte M5,covering
attributes,relationships,inheritance,uniqueness,derived/composite properties and
model/store checksums. It uses only an in-memory store;CloudKit is not exercised.

Unverified: cross-reading macOS-written stores,concurrent stores,relationships,
faults,batch operations,migrations,custom/XML/binary stores,CloudKit/Spotlight,
Swift/Combine and system integration. URL-copy/remove and advanced NSExpression
selectors need retained Foundation work before those paths can be claimed.
No installed file replaced and no framework installed,including new paths.
