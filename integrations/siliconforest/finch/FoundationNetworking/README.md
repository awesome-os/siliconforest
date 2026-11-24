# Foundation networking as an isolated prefix

Whole Finch NSURLSession.m, NSURLSessionSockets.m, NSURLCache.m,
NSHTTPCookie.m and NSURLCredential.m compile on Linux/Nix with our declarations.
They retain Finch MIT/Apache notices. OpenSSL is linked privately; its license
and Nix Mozilla CA bundle provenance ship alongside the dylib. No Apple SDK or
Apple trust-store bytes are used.

build.nix exposes SF-prefixed classes/protocols. Installed Foundation is never
changed, and no duplicate native NSURLSession classes are loaded by this test.
The HTTP request header passes through the installed CFNetwork parser; socket,
HTTP response/body, redirect, WebSocket and cache logic remain Finch's code.
This is not a claim that the whole transport runs through CFReadStream.

TLS now requires OpenSSL peer-chain verification and DNS/IP hostname matching.
The CA bundle is relative to the prefix library. Missing/invalid CA input fails
closed; SF_URLSESSION_CA_FILE is a test override, not a verification bypass.
No delegate may disable verification. HTTP GMT dates use retained CF/ICU
because installed NSDateFormatter lacks the setter expected by Finch. Cache
unarchiving tolerates the retained runtime's older selector set.

Linux-built probe on M1: HTTPS GET https://example.com gives HTTP200,577bytes
with Example Domain marker. A missing CA bundle gives error-1202 and0bytes.
Same-byte M5 native GET gives the same status/body checks. Prefix stagev1 uses
no DYLD overrides. WebSocket, proxy, redirect, background-session behavior and
concurrent cancellation are source-compiled but not runtime-qualified here.

The separate model probe passes on native M5: cookie Secure/HttpOnly/Max-Age,
future HTTP-date parsing and400-day cap, Cookie request header; private memory
cache stores/looks up/purges5bytes. A first native test used a noncacheable
response and too small a cache; its failure remains recorded. A rebuilt binary
initially missed portable Foundation load names; that dyld failure is also
retained. The corrected portable binary is used for both systems; M1 model
comparison now passes with the same cookie flags/dates/header and5-byte cache
accounting. The port removes Finch's arbitrary256-byte per-entry overhead to
match native accounting. Prefixv4 also repeats HTTPS and missing-CA checks.

For TERM6: canonical-objects.nix compiles the SAME implementation with native
class names, without linking Foundation itself. Its archive builds and has no
SFPrevious/SFNSURL undefined class references. integration.nix is the exact
proposed source/link change against TERM6's selected Foundation derivation:
remove the four old implementation units (NSURLRequest, NSURLSession,
NSURLProtocol, NSURLAuthenticationChallenge), force-load these canonical
objects, privately link OpenSSL, link CFNetwork, and ship the CA bundle/notices.
It asserts the expected link command before modification. Existing public
headers remain retained; extending their source API declarations and auditing
ABI/layout consumers are TERM6 integration work. Runtime apps see canonical
class identities, not factory-returned objects from another class family.

This proposal is not a merged or installed Foundation. TERM6 must preserve its
other patches, audit the Foundation/CFNetwork load cycle, run the same HTTPS,
missing-CA and model probes in QEMU, then use its guarded installer/regressions.
The canonical artifact and proposal evaluate successfully; full Foundation
link/QEMU/installation have not been performed by this stream.

Evidence: runs/sffinch/networking-prefix-v1/candidate.json,
avkit-network-native-v1-reference.json, m5-network-models-v3-reference.json,
foundation-networking-canonical-v4, foundation-networking-integration.drv,
networking-prefix-v4/candidate.json.
