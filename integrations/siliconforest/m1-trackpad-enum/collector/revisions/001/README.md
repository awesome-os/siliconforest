# Read-only T1 trackpad enumeration diagnostic collector

This process reads the copied 556-byte observation from
IOResources/PDM1TrackpadEnumerationV1. If the PDM1SPIHID registry instance
exists, its copy must be byte-identical. It uses the qualified tiny static
IOKit surface and the actual generated MIG header. No IOServiceOpen,
IOConnect, HID, MMIO, registry mutation or retry is added.

darwin-trackpad-enum --nonce HEX48 returns 0 only for a complete T1 enumeration
record; 77 for a source-consistent recorded refusal; 1 for missing, malformed,
mismatched or failed copy/output; 2 for usage.
A captured descriptor is raw acquired bytes, not proof of valid HID semantics,
an enabled mode, trackpad input, haptics or a working pointer.

DL14_TP_RECORD_BEGIN/HEX/END uses indexed 64-byte chunks (last 44 bytes),
nonce binding and SHA-256. The provider and optional instance have separate
labels. DL14_TP_VERDICT always states input_proven=false feature_writes=0.
Raw evidence stays in private captures. decode_records.py independently
checks exact bytes, count, digest, order, producer state and verdict.

The state/header contract is ../observation_layout.h. A short metadata reply
retains only its numeric-header prefix; its unread tail is zero. Optional name
bytes are never copied. A rejected descriptor has zero stored length and no
copied descriptor bytes. The producer's diagnostic copy must survive instance
failure on IOResources. Missing bytes never become a refusal/success.

Host-only checks: python3 test_collector.py -v. Tests use fake registry copies,
not a fake device or target. Framing and finite getter flow are reused from our
reviewed original DART collector002; validation is specific to T1.
Original code license selection remains pending. No upstream function copied.
