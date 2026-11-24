# Backend005 bounded close window

Isolated copy of frozen transport001 controlled TCP/UDP backend. `codec.py` is
byte-identical. The only effective backend changes are close-to-peer-EOF grace
2s ->15s, and exemption from the10s idle timer while that explicit closing
window is active. The whole configured active-session deadline still executes
first and bounds closing; root's new profile uses60s active time. All queues,
per-write/partial-frame deadlines, addresses/ports/payload policy, child wait/reap
and cleanup remain unchanged. Root owns guest005's corresponding close window,
new profile/bindings and any target run; helper004 is unchanged.

`CLOSE_EOF_SECONDS=15` and `ACTIVE_IDLE_SECONDS=10` are explicit constants.
Both ready/result expose `close_eof_seconds` and `active_idle_seconds`; result
also records `active_seconds` and `accept_seconds`. The controlled CLI stays
unchanged and must be invoked with root's fixed nonce/ports/profile/lifetime.
No public egress, broader listener, SSH, route, firewall, KC or m1n1 change.

The regression uses the unchanged production004 Bridge, frozen C codec and real
Linux QEMU/slirp controlled TCP/UDP echoes. It deliberately schedules the final
192-byte credited chunks over a longer total closing interval; individual ACKs
remain prompt, not delayed past the helper's3s outstanding-ACK deadline.

```
python3 -I test_close_window.py --library /ABS/FROZEN/libdlnet-host.so \
  --output /ABS/NEW-PRIVATE-TEST-DIR
```

Observed host cases:

| Backend | Intended drain | Result |
|---|---:|---|
| Frozen original2s | 5s | Expected failure: closed peer EOF deadline, exit1 |
| New15s | 5s | Complete after5.005s, exit0 |
| New15s | 11s | Complete after11.006s, exit0; idle10s did not preempt closing |

Both nonce echoes occurred in every case, including the failed lifecycle.
Every QEMU child returned0 and was reaped, with no KILL or cleanup error. The
old failure is preserved; echo success alone is not a clean link qualification.
These are actual Linux processes with synthetic guest packets/ACKs/TTY model;
they establish no Darwin console/utun cleanup, hardware or external networking.
The actual M1 candidate must produce its own full PASS and verified recovery.
