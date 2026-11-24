# Candidate-002 shared-shell qualification helper

Host-only checker for one copied candidate-002 capture. It pins criteria 002
(`2b09ca60`), the frozen export parser, both record decoders, and the
candidate-002 packet review. It does not edit those files and it does not use
the attempt-001 helper.

`decode_exports` is the C6 authority. A nonzero export status fails C6. When
the session wrapper rc is 0, that session span is graded anyway, including
failures. An application whose own terminal status is nonzero is not accepted.
D1 records the kernel exit reason and never upgrades C1–C9. GUI and two windows
stay out of scope.

```sh
PYTHONDONTWRITEBYTECODE=1 python3 test_qualify.py
PYTHONDONTWRITEBYTECODE=1 python3 qualify.py --capture DIRECTORY --attestation FILE
```
