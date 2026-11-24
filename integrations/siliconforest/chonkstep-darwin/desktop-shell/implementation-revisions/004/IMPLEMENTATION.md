# Visible per-application identity — revision004

This is a one-source-file delta from immutable003. It changes only allocation
of the canonical48-hex application instance. Shell/startup actions, application
CLI/CHONKTERM, PID ownership, readiness, signal policy and cleanup are unchanged.
No application, supervisor, active host, packet or target was edited here.

For allocation counter `n` starting at1, the new instance is:

```
format_lowercase_hex(n, width=4) + session_hex[0:44]
```

The counter must be in1..65535. Allocation refuses before state advance,
directory creation or spawn on exhaustion; it never wraps or silently widens.
The existing smaller manager history/active-app limits still apply. Failed
launches consume their already allocated identity as before; counters are not
reused. The suffix retains176 bits of a uniformly random host session nonce,
versus128 in the old layout. Two different full session values sharing the
first44 hex digits intentionally have the same suffix: the root must still
bind a fresh session and private runtime directory. This is not a universal
cross-session uniqueness guarantee.

The ordinary application's real source sets the title to `Terminal ` followed
by the first12 instance characters. Thus counter1 and2 have distinct titles
even when their windows render the same initial shell contents. Full48 remains
the identity in the actual CLI, private status channel and per-app records.
The shared Wayland app_id is still `chonk-terminal-darwin`; it is not a unique
instance identifier and a title is never process or kill authority.

The prompt is independently fixed at `PS1=$ ` in the ordinary application's
PTY environment. This change does not make prompts distinct, inject commands,
alter the app, draw expected text or manufacture READY. Qualification should
bind each visible title to the expected full instance, actual manager PID,
private fd3 assertions and that app's own PTY records. Caption observation alone
is not authenticated process attribution, and startup does not prove a human
menu or shortcut action.

`INSTANCE_LAYOUT.json` is the explicit contract and test vectors for root's
supervisor/packet metadata update. Root must replace the old
`session[0:32] + counter16hex` derivation for the new packet only; old supervisors,
receipts and captured IDs remain their original evidence. Do not mix layouts or
infer allocation order from SSD configure serials, focus or window positions.

The new host tests prepare two real independent private argument/record sets,
check exact full IDs and distinct first12 characters, and prove that `ffff` is
the last value with refusal/no state mutation or launch on subsequent or u64
overflow inputs. Existing ownership/Wayland tests are retained. There is no
native004 build, real pixel qualification, new input proof or hardware action
in this source checkpoint. Root owns the concrete native build, paired app
revision, updated supervisor/metadata, review and finite Linux-recovered run.
