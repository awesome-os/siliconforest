# darwin-dart-survey: read-only diagnostic collector

`darwin-dart-survey --nonce <48 lowercase hex>` reads the PDDARTSurvey V1
diagnostic from the I/O Registry, validates it, prints every raw byte, and
exits with a status the supervisor (`darwin-dart-survey-test`) acts on. It
performs **at most two registry property reads and nothing else**: no MMIO, no
user client, no HID access, no retries, no device-tree writes.

## Reads (collector_main.c)

1. `IOMasterPort` (the tiny IOKit's `host_get_io_master`), at most once.
2. `IOService:/IOResources` → `PDDARTSurveyDiagnosticV1` via
   `io_registry_entry_from_path` + `io_registry_entry_get_property_bytes`, once.
3. Only if the provider copy is well-formed: `IOService:/IOResources/PDDARTSurvey`
   → `PDDARTSurveyObservationV1`, once. A missing entry is allowed (the instance
   exists only after a successful start). If present, the copy must be byte-equal.

XNU grounding: `is_io_registry_entry_from_path` returns success with a null
entry when the path does not exist. `is_io_registry_entry_get_property_bytes`
copies an OSData's raw bytes inband (at most 4096), returns
`kIOReturnNoResources` (0xe00002be) for a missing property and
`kIOReturnIPCError` if the buffer is too small. The tiny `libIOKit.a` exports all
four routines used. The same include set (`PDIOKitLib.h`, `mach/mach.h`,
`device_user.h`) is used by the qualified tiny IOKitLib.c.

## Validation (dart_collector.c)

Exact model of the reviewed sampler (`survey.c`, unchanged since revision 001):

- length exactly 764; version 1; result in {0, 1, 2, -1, -2, -3, -4};
- sample_count ≤ 88 and `10*samples ≤ reads ≤ min(10*samples + 10, 889)`;
- sample offsets equal the sampler's order for the successful prefix; the tail is zero;
- failure region ≤ 5; region 5 requires offset 0; regions 0-4 require an
  allowlisted offset;
- successful-prefix profile: a completed sample 1, 3 or 4 cannot already fail its
  page-shift, CONFIG-lock or stream check unless the run stopped exactly there as
  UNSUPPORTED (revision 002, R1);
- result 0: 88 samples, 889 reads, region 5, offset 0, and all nine final guard
  words pass the survey.c predicates: PMGR TARGET = ACTUAL = 0xf with no
  RESET/AUTO/bit12/PARENT_OFF/DISABLE; PHY CLK bits 0-3 set with reset bit 7 clear;
  RC/port RUN/EN/DIS bit 0 set (revision 002, C1);
- NOT_READY: the stopping read is the guard read at that position, and its freshly
  read word fails its predicate (revision 002, R1);
- UNSUPPORTED: 1, 3 or 4 samples with the matching cause, at the last DART read;
- IO: no reads (adapter-level), the guard read at that position, or the 10th
  (DART) read of an unfinished sample;
- BOARD / ARGUMENT: no reads, region 5;
- LIMIT: rejected as malformed. It is unreachable under the frozen 889-read profile
  (cap 1024) (revision 002, R1).

Apart from the final guard of a SAMPLED record and the single failing word of a
NOT_READY record, power and clock words are not judged. Partial-failure snapshots
are noncoherent, and stale or unread guard words are allowed.

## Output and exit codes

Every line carries the nonce. Raw bytes from every copy actually read are
printed whether or not they are valid, as
`DL13_DART_RECORD_BEGIN/HEX/END <nonce> <property> ...`: 64-byte hex chunks with
contiguous indices, plus the SHA-256 (CommonCrypto `CC_SHA256` on Darwin,
OpenSSL `SHA256` for host tests). An unread copy prints `DL13_DART_COPY ...
status=no_entry|no_property|error detail=<kr>`. Exactly one
`DL13_DART_VERDICT ... initialization_proven=false` line ends the run. No
registry IDs or device identities are printed. Register values and root
addresses are private evidence.

| Exit | Meaning |
|---:|---|
| 0 | well-formed result 0 (SAMPLED), instance absent or equal |
| 77 | well-formed kernel-recorded refusal (any other known result), instance absent or equal |
| 1 | missing provider, MIG/IPC error, malformed bytes, instance mismatch/error, output failure |
| 2 | usage (anything other than exactly `--nonce <48 lowercase hex>`) |

Even exit 0 means only that the read sequence completed: no coherent snapshot,
translation, ownership or initialization claim.

## Host checks

```sh
python3 integrations/puredarwin/m1-dart-survey/collector-native/test_collector.py
```

Revision 002 (Astra C1/R1) changes only `dart_collector.c`. The wrapper
`collector_main.c` and the header are byte-identical to revision 001, which is
preserved read-only under `revisions/001/`.

These are ASan/UBSan host builds with `-Werror -Wpedantic`. The harness links
the real `../survey.c` with a fake register bank and classifies 1698 genuine
sampler snapshots:
- success;
- all 889 I/O-fault positions;
- all 801 guard NOT_READY positions;
- the three UNSUPPORTED causes;
- four adapter refusals.

It also covers each of the nine failing final-guard words on a SAMPLED record,
NOT_READY records with a passing fresh word or the wrong guard position, zero-read
and success-shaped LIMIT records, later stops after a failing page-shift, lock or
stream check (including UNSUPPORTED stops), noncoherent stale words on refusals,
usage refusal with no registry read, 25+ malformed edits,
missing/MIG errors, instance absent/equal/mismatch/error and an output-write
failure. The runner decodes three emitted outputs independently (index
continuity, chunk sizes, SHA-256 via hashlib, nonce on every line, one verdict),
refuses `NDEBUG`, and kills 14 compiled mutants by assertion (compiler failures
and sanitizer aborts do not count), and checks that the wrapper calls only the four IOKit routines. No SDK
build, IOKit, MMIO or hardware is involved. Root owns `../build-collector.nix`.
