# Independent saved native resolver evidence001

`resolver_evidence.py` performs no target or network operation. It exports `observations(raw, nonce)` and `native_service(obs, backend_egress) -> {checks, details}`. Observation dictionaries preserve kind/data/total/first/last and add the validated session nonce. Transport CR normalization is permitted; hex payload bytes remain exact. Per-kind caps are8192 for child stdout,65536 for child stderr and4096 for other records, under the parent512KiB serialized observation budget and32MiB raw-console cap.

The wrapper loads the unchanged `runs/m1-desktop-010/default_evidence.py` only after verifying SHA256 b6cb9bbaae47bdddf8b0b001391985e823654e34292c1ac2f3a5cceff3f87b60. It filters the exact original route kinds and feeds reconstructed bytes directly to process_context/default_lifecycle. It does not rewrite captured prefix or alter any of the11 route predicates.

Service checks require distinct newly owned probe/daemon PIDs, actual56-byte spawn records with flags0x400e/stage10/all errors0/child_created1/dupcount4, exact successful reap/stdout/stderr EOF records, probe exit0/no signals/no stderr, and daemon TERM/noKILL/normal raw wait status1024 (EINTR exit4). Captured stream bytecounts must match observation data; child groups must appear in the real source order.

The probe's one exact compact JSON line must contain unique ordered keys, matching nonce/PID and PASS/rc0/errornone/complete=true. Its IPv4 list preserves API order and duplicates. Every returned API IPv4 must belong to the backend's validated global A set; the backend's selected native TCP endpoint must equal the first API IPv4, with its actual wire-answer index recorded. It may differ from first wire A.

The caller must first authenticate closed backend records and raw packets. Pass policy.summary() as backend_egress and inject `response_hex` only from the hash-verified closed http-response.bin. Exact observed nonce GET and raw response bytes are compared; response framing/status/content are independently parsed within2048 bytes. Missing fields remain failed/not_evaluated, without fabricated defaults. This module does not qualify DNS-wire correlation/TTL/ports, codec/credit/outer process status, wrapper order, endpoint metadata or Linux recovery on the caller's behalf.

Require `set(result['checks']) == set(CHECK_KEYS)` and every value `is True`, never all(empty). CHECK_KEYS has24 fixed fields. Details are JSON-safe and retain computed byte hashes/hex and an explicit not_evaluated list.

Replay synthetic-only controls:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -B -m unittest discover -s integrations/puredarwin/m1-network-resolver-service/evidence -p 'test_*.py' -v
```

The tests construct source-shaped records and mutants, not Darwin evidence. No runtime capture was accessed while defining this decoder or its criteria.
