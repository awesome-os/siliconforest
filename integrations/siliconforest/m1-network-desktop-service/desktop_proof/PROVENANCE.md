# Input and implementation provenance

The adapter is original C, written for this source-bound record composition.
No upstream implementation or GPL code is copied into it. Its known text/JSON
wire formats come from the selected original desktop sources below. Comments
and tests describe source-level consequences and synthetic host evidence only.

* `../record_guard/source-validation-001.json` (2a897614): immutable original
  NOFOLLOW filesystem reader and strict integer/ASCII JSON query parser. Its
  data/metadata limits are inherited, not reimplemented or weakened.
* `../session/frontend-binding-002.json` (965aba83): actual retained frontend
  is007, binary fda83680 and source a5b63ea9. Frontend004 is historical instance
  layout lineage, not the selected serializer. The binding correction preserves
  the old receipt rather than regrading an earlier capture.
* `../session/revisions/002/session_text.{c,h}`: new clock-domain-corrected
  parser. It is compiled unchanged by this composition. Old16f4 code is not
  compiled; its comparison of manager timestamps to NativeIo elapsed time is
  inapplicable because their origins differ.
* `../../chonkstep-darwin/desktop-shell/implementation-revisions/007/source/`
  `crates/wm-darwin/src/launch_audit.rs`: exact journal header, request/native/
  gate/status serializer, bounded diagnostic labels, final snapshot. The
  frontend's manager is the exclusive direct-child waiter; an app's record is
  not its own exit-status proof. The source serializes the second startup after
  the first app is ready. The closed session summary and journal must agree.
* `../revisions/002/terminal-source-validation-002.json` (fa6f13a4): selected
  terminal source variant and SDK build. `terminal/src/main.rs` supplies typed
  app/child/exec/cleanup/session records; `status.rs` supplies exact READY and
  CLEANUP byte counts; `startup.rs` supplies the fixed foreground-child and
  real-prompt completion contract; `grid.rs` supplies the bounded rectangular
  character serializer. `session.term_signal` is a JSON boolean because the
  producer uses an AtomicBool. Unsupported terminal controls remain measured
  facts, not a reason to fabricate a rendered or interactive outcome.
* `../revisions/002/client-source-validation-001.json` (746c298a): original
  native client C serializer and fixed startup.env. `net_client.c` emits
  source-bound BEGIN and DONE markers and returns through the foreground shell;
  `net_result.c` describes the successful exact typed tuple/HTTP record. A later
  client failure can occur even after printing DONE0, so the shell's normal
  return0 is required separately. The entry descriptor scan is before getaddrinfo,
  not a claimed post-libinfo-thread or post-exec inventory.
* `../../m1-network-egress-guest/codec/egress_validate.{c,h}`: unchanged original
  global-IPv4, nonce request and complete HTTP response predicates. The adapter
  re-parses actual retained response bytes; it never creates a success response.
  Stream bytes here are not evidence of a corresponding backend packet trace.

The validation receipt hashes all compiled SDK dependencies, the selected
source receipts, actual runtime libSystem9fc and libdyld428 import scopes, and
all local leaf files. It records six ARM64 object builds, not a linked target
runtime result. The host SHA256 implementation is OpenSSL; production uses
pinned CommonCrypto CC_SHA256 with <=65536-byte inputs. Filesystem fixtures
are isolated host files with explicit test-only root/UID hooks. Neither the
check runner nor this module acquires new target data or changes hardware.
