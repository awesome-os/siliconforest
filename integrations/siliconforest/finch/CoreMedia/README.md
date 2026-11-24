# CoreMedia (staged only)

All fifteen Finch2d9b1bb CoreMedia implementation files. Build with
`nix-build --no-out-link integrations/siliconforest/finch/CoreMedia/build.nix`.
Our declarations replace SDK headers; use PD's real CFRuntime layout. The
custom-block-source callback structure is packed4 (28 bytes), verified by the
same Linux-built program on M5. PD's old declaration used32 bytes: rebuild clients
using custom allocation callbacks before integration; never mix private objects.

Candidate2609 exports retains all75 thin PD names. Installed canonical CoreMedia
was absent in the live inventory; the repository still has the thin implementation.
Original comparison exits0 on M1 and M5 but raw descriptions differ. Independent
time/block/sample/NV12 ownership probe MATCH. No system installation.
See ../media/README.md and SF-AB for decoder dependencies and integration gates.
