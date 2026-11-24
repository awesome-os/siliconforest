This isolated revision fixes the DL30 console transcript ceiling. The only
production change is `147456` to `278528` in ConsoleSession.send's link branch.
The latter reserves 16384 bytes for bootstrap/launcher alongside the existing
262144-byte data authority. Bridge.authorize still grants only the exact pending
192-byte chunk, with its unchanged cumulative data, credit and ACK limits.
The legacy transcript ceiling stays16384 and the native command writer stays sealed.

The new builder is runs/m1-desktop-010/net-desktop-ledger-001/lab. Its22 normalized
files differ from the frozen original only in host_progress.py; helper16dd and
all controller, bootstrap and recovery sources are unchanged. The binding updates
only that source hash, isolated path and descriptive ledger metadata.

The causal tests call both actual old/new ConsoleSession.send implementations,
real native FSM and production Bridge.authorize, with real private temporary
files/fsync and a fake serial writer checking ledger-before-write order. Pending
whitespace and ACKs are synthetic fixtures; no Darwin/network success is inferred.
The saved007 boundary147267+192 is reproduced: old refuses, new records/writes147459.
The real writer then reaches DATA262144 and rejects262145. Synthetic seeded
ledger states separately test the defensive combined278528/278529 boundary;
those states do not claim the FSM permits arbitrary bootstrap commands.

Tests: test_writer.py, test_line_bridge.py, check_mutations.py,
test_host_integration.py, test_authorization.py and test_legacy_suites.py.
The freeze includes exact results and source hashes. verify.py is read-only.
No assembly, service, SSH, boot, C/Rust/kernel/library/backend/helper changes.
Failed007 and all earlier receipts remain preserved.
