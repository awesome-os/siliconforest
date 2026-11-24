# Native private lifecycle self-report — source only

`PDBCMPrivateProbe` is an original IOResources service with bundle identity
`org.darwinlab.driver.PDBCMPrivateProbe`. Its only observation property is
`PDBCMPrivateObservation`. A permanent CAS precedes chosen/NPP/lifecycle work.
Only exact bootarg `pdbcmprivate=1` selects work. Other values publish a zero-
identity GATED record, no attempts. The service retains itself and its heap
workspace/controls until reboot, refuses terminate and module stop, and has no
manual free/retry/unload path. Standard OSDefine metaclass constructors exist;
no custom native work runs in global initialization. Platform code residency,
wiring/protection, unique domain ownership and boot quiescence/accessibility
remain required assumptions, not evidence supplied by this IOService.

Enabled work copies216 identity bytes from offsets64..279 of fixed chosen
`darwin-lab-platform-facts`, as TRUSTED HOST LABEL CONTEXT. Exact size/offset
assertions cover `nps_expected`. These labels are neither authentication nor an
independent readback of host inputs. The actual frozen NPP7d41 separately reads
and validates whole facts/envelope and calls owned_control9844/provider0ee/core
c585. Configuration can allocate and retain controls; a failed configuration
does not imply rollback or release. Full conservative boot-prefix reservations
may legitimately refuse heap controls. Never punch holes or retry to succeed.

After successful configuration the bounded sequence uses ONLY public native
facade calls: one acquire, zero,16-byte nonce copy at offset0, inspect, one guarded
private retirement, final inspect. RID0x100/SID0/vector1/IOVA0x100000/permissions3
are fixed synthetic metadata, not chip identity, actual mapping or hardware
authority. Nonzero acquisition means no token/retirement guess. After rc0
acquisition retirement is attempted once even if a later call failed; the public
owner safely refuses uncertain/halted/quarantined state. Earlier errors stay
sticky. A retained/unknown outcome stays FAILED; no raw ref manipulation or
manual cleanup occurs. All effects remain absent; `bno_native_run` is not called.

Success is ONLY `CONFIG_AND_PRIVATE_LIFECYCLE_SELF_REPORT`. It requires every
call rc0 and exact source-shaped pre/post snapshots. Actual phase values are
INITIALIZED3 and REFS_RELEASED6 (QUARANTINED7). Before retirement: identity3,
historical owned pairs3, balanced0/reset0. After: identity0, owned pairs STILL3,
balanced3/reset3, core retired1. The owner preserves historical owns_pair flags;
raw owned_pairs alone is not outstanding obligations. Busy/halt/error/pending/
effects must be zero. No API exposes private data readback: `copy_bytes=16` is
the API request/status only, and `data_readback=false` ALWAYS. No MMIO/DART/IRQ/
PHY/PMGR/config-register/firmware implementation is added or invoked.

The fixed768-byte record is little-endian, initialized to zero except defined
fields and unattempted API status0x80000000. No CPU/PA ranges, token, descriptor,
generation or sequence is exported. Layout:

| Offset | Contents |
| --- | --- |
| 0..15 | `BCMPRV01`, version1, length768 |
| 16..31 | result0self-report/1gated/2pending/3failed, stage, attempt mask, signed first error |
| 32..47 | trusted labels1, synthetic metadata1, readback0, effect commands0 |
| 48..263 | chosen nonce24 + six32-byte identity hashes |
| 264..423 | five32-byte receipt labels: reader/NPP/control/owner/core |
| 424..455 | signed API statuses: labels/config/acquire/zero/copy/preinspect/retire/postinspect |
| 456..479 | NPP version/profile/RAM count/reservation count/control page count/query count |
| 480..487 | copy request byte count, synthetic request profile1 |
| 488..495 | reserved zero |
| 496..551 |14 sanitized pre-snapshot words |
| 552..607 |14 sanitized post-snapshot words |
| 608..767 | reserved zero |

Snapshot words: phase,busy,halted,metadata_bound,core_retired,call_pending,
identity_records,owned_pairs,balanced_pairs,resets_returned,first_error,
effect_count,effect_pending,ever_resources. Attempt bits0..7 correspond to API
status order and are set before issuing each call. Final failures preserve the
actual partial mask/statuses; pending publication before work proves no completed
lifecycle. Allocation/publication failure can leave only PENDING or no property;
absence is failure. A preexisting property is not overwritten.

`/usr/bin/darwin-native-private-test <48lowerhexnonce>` uses one fixed raw MIG
read of `IOService:/IOResources` property bytes, no service open/connect/set/load.
All bounded copied bytes are hex+SHA exported and flushed before classification:
`DL32_PRIVATE_RECORD_BEGIN`, indexed64-byte `..._HEX`, `..._END`, then
`DL32_PRIVATE_VERDICT`. An exact enabled record must match the nonce. A coherent
GATED record is rc77 with `record_nonce_bound=false`: framing nonce is caller
context only. Missing property is rc1, NEVER77. Other errors, pending, malformed
bytes, cleanup failures and lifecycle failures are rc1; invalid CLI rc2.
Original `collector/decode.py` verifies the entire supplied span with no interior
line filtering, exact sequence/count/SHA/nonce/verdict and trailing-console-CR
normalization only. Raw bytes and digest remain unchanged. No verdict is WiFi,
link readiness, real chip identity, DMA, hardware ownership or activation proof.

`validate.py --output-dir NEWDIR` runs bounded host fault/counter/byte/span tests,
compiled assertion mutants and5 actual frozen same-owner-source processes under
explicit original TEST-SDK stand-ins. Kernel APIs never run. Driver CAS ordering
is checked structurally; no host fake hardware is a native service execution.
`build_objects.py --output-dir NEWDIR` compiles13 genuine-framework ARM64 TUs at
O0/O2 and partial-links MH_OBJECT only. The only module initializers are the new
service's intended metaclass registration; frozen core10TUs remain initializer-
free. `collector/build_sdk.py` links only the userspace collector with retained
9fc libSystem/428 dyld/tinyIOKit. No kext/KC, packet, deployment or target execution
is authorized by these source/object tests. Hardware STOP/GPU HOLD/serial bridge
STOP remain. Independent review and concrete admission precede any packaging.
