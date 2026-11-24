# Time resolver observations, not an implementation

The Linux-built `time-resolver-probe` uses our own runtime-derived method
declarations. With no argument it creates only the resolver. With one argument
it attaches a real controller to our generated local five-second movie while
paused. It never starts playback, creates a window or changes services. M5
references use a fresh temporary directory, remote SHA verification,30-second
per-program limit, no DYLD overrides or builds, and delete that directory.
Only our program's stdout/stderr return to Linux.

The v2 discriminator established that the four time setters are not independent
stored current/target values when no controller is attached: each changes the
values subsequently read through all four getters. Default interval is1;
other initial scalar values are0. Explicit min/max setters store their values,
including inverted bounds. Changing interval/resolution refreshes the bounds.
These are observed cases, not a general specification.

V3 attached our paused movie and observed bounds0..5 and remaining time
5-current. V4 adds the controller's actual current time to distinguish a
cached immediate result from a completed asynchronous seek. Its portable probe
SHA is `41aa4d5d5921e76921245432227bf932cd6d9f0d7c79e3bf1b73efa6ba21b04b`;
M5 returned0 in0.896s. The four setters followed by a run-loop advance move
the actual player to0.75,1.375,2.125 and3.625s respectively.

The boundary loops deliberately read immediately after the setter. They show
that the resolver's current value can precede completion of the actual seek;
the target also clamps outside the attached0..5 bounds. Queued negative/end
seeks affect subsequent controller snapshots. Consequently, precision refresh
observations from that sequence do not establish an independent rounding or
notification rule. Nor does a single at-end flag establish its full threshold
formula. The raw sequence is retained rather than reduced to invented rules.

V5 adds a0.1s run-loop advance after each boundary assignment and a3.001s
vector. It returned0 in3.551s, portable SHA
`2f00114b82a9a654b6e169de519d6734884b912fab326f59ccb62791c2ffe7e8`.
For this five-second file, the at-end flag is false at3s and true at3.001s,
for both interval1 and0.5. After the advance the actual player clamps negative
and beyond-end seeks to0 and5, while the resolver can retain the requested
out-of-range value. The transient target-clamp observations in v4 are not a
permanent getter-clamping contract. A precision change can also reset the
resolver's cache before the player's time changes. These observations rule
out a simple always-read-through adapter; no KVO/settling guarantee follows
from waiting a fixed0.1s.

No `AVPlayerControllerTimeResolver` class is supplied just to resolve its name.
Before a real port, the remaining work is to establish settled seeks,
precision/end-time behavior and the controller/KVO notification linkage, then
compare the same Linux-built program on the staged M1 provider. Raw native
results remain in `runs/sffinch/m5-avkit-time-resolver-v{1,2,3,4,5}-reference.json`;
v1/v2 have no player, and v3/v4 use only our own local fixture. None is a GUI
or M1 playback qualification.
