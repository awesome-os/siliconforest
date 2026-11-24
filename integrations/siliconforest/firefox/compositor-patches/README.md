# Firefox test-seat cancellation

`0001-targetless-key-release-clears-xkb.patch` is a review candidate against the
source frozen in `desk10-next-003` (the installed compositor before this change).
It belongs in the DESK compositor series after native qualification. This file
is not an installer or permission to bypass the shared GUI/core/bench locks.

A Firefox Command-Q can destroy the targeted window before the test request's
key releases. The old delivery path rejects the releases before removing the
virtual held-key records; end-of-request and owner-input cleanup use that same
path. The next request can therefore inherit a virtual Command modifier.

The candidate cancels only keys recorded as virtual-held, using Smithay's
`input_intercept` to update its pressed-key/xkb modifier state without forwarding
the event to a client. It retains the borrowed-focus token until cancellation
finishes, then restores normal focus. It never falls back to the focused client
for a targetless key press. Physical-key ownership and deferral logic are kept.

Required native checks: the own test application closes on Q-down, the remaining
releases/end-of-request clear its virtual keys, the next own window receives no
unexpected modifiers and accepts a fresh Command press, and owner-input
interruption preserves the real seat. Record target identities, actual input
modifiers, focus restoration and cleanup; a successful build is not these checks.
No running compositor or installed binary has been changed by this patch.
