Whole pinned Finch CFNetwork C implementation, built with our source-derived
signatures and type declarations, SiliconForest's open CF/proxy/DNS-SD headers,
and the existing pinned OpenSSL 3.5.9 backend. No Apple SDK input.

Use the linked PD CFRuntime layout. Register HTTP stream clients through PD's
required schedule callback; PD owns the run-loop source while Finch's worker
signals stream events. The comparison test now fails on missing callbacks or
an incomplete 11-byte response, preserving its original stdout.

The live framework path was absent (PD source only has proxy support); clean
imports, M1 parser/Basic-auth/TXT/FTP assertions and loopback HTTP/chunked/
redirect/POST/HEAD/socket/run-loop checks passed staged and installed. Only the
new framework path was installed, with ledger hashes and uninstall support.
Native macOS comparison preserves DIFFs in version string, localhost address
ordering and unsupported reachability. No Internet TLS, DNS-SD registration, PAC
compatibility beyond the original test, or general application claim.
A staged metadata correction sets current version 3860.255.112 and compatibility
version 1, matching Finch; installed initial files remain unchanged.
Numeric ABI facts cross-checked against the maintainer's objc2-cf-network 0.3.2
bindings: https://docs.rs/objc2-cf-network/0.3.2/src/objc2_cf_network/generated/CFNetworkErrors.rs.html
