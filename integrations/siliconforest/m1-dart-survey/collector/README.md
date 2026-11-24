# darwin-dart-survey

Read-only userspace collector for the persistent `IOResources` property `PDDARTSurveyDiagnosticV1`. The wire image is the version 1 snapshot in `../survey.h`: 764 bytes, little-endian, alignment 4. Power and clock words are decoded and are not checked for equality or coherence. There is no per-word validity bitmap.

The program is `/usr/bin/darwin-dart-survey --nonce HEX48`. The nonce is 48 lowercase hex digits. Stdout is a short receipt: nonce, length, SHA-256, indexed hex chunks of 16 bytes, numeric fields, and the samples below `sample_count`. No registry id, serial, or device name is printed. A refusal still prints the full raw bytes and digest.

## Exit

| Exit | When |
|---:|---|
| 0 | Well-formed record and `result == 0`. That also requires `sample_count == 88`, `reads_attempted == 889`, `failure_region == 5`, and `failure_offset == 0`. |
| 77 | Well-formed record and `result` is one of `1`, `2`, `-1`, `-2`, `-3`, `-4`. The raw result is printed. |
| 1 | Missing property, MIG or kernel error, length other than 764, bad version, `sample_count > 88`, `reads_attempted > 1024`, `failure_region > 5`, unknown result, sample offsets that leave the sampler prefix, a result 0 that is not the 88/889/sentinel shape, or an instance copy that is not byte-equal. |
| 2 | Usage. The nonce is missing, not 48 lowercase hex digits, or the arguments are not exactly `--nonce HEX48`. |

A record is well-formed only when the length is 764, the version is 1, the three bounds above hold, the result is one of those seven values, and `samples[0 .. sample_count)` offsets follow the `survey.c` `sample_one` order. The unused sample tail is not evidence.

## Registry reads

One `io_registry_entry_from_path` of `IOService:/IOResources`, then one `io_registry_entry_get_property_bytes` of `PDDARTSurveyDiagnosticV1`. The call is the declaration in the generated `device_user.h`. The count passed in is 764. `device.defs` marks that argument `CountInOut` over `io_struct_inband_t` (`char[4096]`). `IOUserClient.cpp` copies raw `OSData` bytes only when the provided count is at least the object length.

If a read-only class match finds no `PDDARTSurvey` service, that absence is allowed. If it finds one, one read of `PDDARTSurveyObservationV1` must be the same 764 bytes. A missing persistent property is always a failure. There is no retry, `IOConnect`, HID, MMIO, or registry write.

Compile this file only on the pinned ARM64 SDK, with the same switches as the qualified tiny archive:

`-std=c11 -Wall -Wextra -Wpedantic -Werror -DIOKIT=1 -DKERNEL=0 -UKERNEL`

`device_types.h` defines `io_struct_inband_t` only when `IOKIT` is set and `KERNEL` is not. The header and the link recipe belong to root (`build-iokit-mig-header.nix`). This directory does not invent a prototype or a second build recipe.

## Host test

From this directory, with no SDK:

```sh
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g -I.. -c main.c
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=address,undefined -I.. \
  classify.c sha256.c receipt.c test_classify.c -o test_classify
ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ./test_classify
```

`main.c` is typechecked on the host and is not linked here. `fetch_mig.c` is the target wrapper.
