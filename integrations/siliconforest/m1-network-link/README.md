# Controlled Darwin-to-Linux IP link, source 001

This isolated slice connects the already-tested Darwin utun socket API to the
frozen Linux QEMU/slirp backend. The first target is one actual Linux TCP echo
and one UDP echo through the slirp host alias, each carrying the packet's fresh
48-hex nonce. No physical NIC, Wi-Fi, GPU observer, m1n1 change, host route or
firewall change is involved. Public DNS/Internet and graphical-session integration
are separate subsequent work; neither is inferred from this controlled test.

The guest daemon is a finite ARM64 executable using libSystem 9fc03c87. It owns a
new utun interface at 10.0.2.15 with peer 10.0.2.2, /32 and MTU1500. After disabling
TTY echo/translation and enabling bounded nonblocking I/O it prints an exact
READY marker. It pumps the frozen DLNET001 codec and real BSD TCP/UDP clients in
one thread. Receive queues, line/frame counts and deadlines are bounded. Both
nonce echoes and reciprocal CLOSE are necessary for success. Closing all owned
sockets, restoring terminal settings and file flags, and a successful final flush
are required for executable exit0. A later hardware receipt must bind its actual
exit as well as its result and both host echo records.

Only the new exact `network_link_experiment` opts into the host data path. The
profile requires the qualified native50 collection, the bound helper, the existing
passive-input gates and fixed controlled address/ports. It forbids the GPU observer
and every HID-injection experiment. The nonce is generated with the host CSPRNG
for one new packet and passed explicitly to ConsoleProof only in this mode. All
legacy paths retain their random challenge and sealed post-launch writer.

Guest-to-host frames are nonce-labelled complete hex lines, independently checked
against the codec header, sequence, direction, CRC32C and IPv4 checksum. Unrelated
complete kernel-console lines remain in the raw capture. Observation errors cancel
data and keep reading diagnostics; failed writes/ledgers remain fatal.

Host-to-guest **data contains only SPACE and LF**: bit0 is SPACE LF, bit1 is SPACE
SPACE LF, MSB first, eight bit-lines per codec byte. Every arbitrary suffix is
still shell whitespace. This avoids the consumed-comment-prefix hazard if a daemon
dies halfway through a serial read: remaining bytes cannot spell a shell command,
contain TAB completion or invoke control-key behavior. The C parser enforces this
alphabet, bit lengths and complete bytes before feeding the unchanged codec.
Source tests execute that actual parser with host pipes under ASan/UBSan. Legacy
bootstrap/challenge/fixed launcher bytes precede the leased data phase; only an
exact READY after the challenge and launcher permits any whitespace data write.

A single existing console owner records/fsyncs every attempted write before
sending. The helper can authorize only its next issued whitespace frame, and the
explicit data budget is131072 bytes. No other commands or host HID are admitted.
The new isolated host/controller copies live under `runs/.../net-link-001/lab`;
old lab copies, recovery, locks/latches, bootstrap and m1n1 sources remain unchanged.

Root owns the Linux backend and reverse SSH forwarding to an M5 loopback port.
The backend and forwarding must be ready before the one target cycle starts.
They must be stopped/reaped after authoritative Linux recovery, including failures.
No target run is authorized by a source test or a ready.json file alone.

Current offline evidence: clean pinned SDK build; 14 bridge tests, 12 host-mode
integration tests, 41 legacy host tests, 16 authorization-path tests, 20 controller
tests; 268 extracted-C-parser cases/8033 assertions under sanitizers; actual
Bridge+Ccodec+QEMU controlled TCP/UDP host fixture (synthetic guest packets only).
The raw Darwin console/utun handoff remains unexecuted until a separately bound
packet and one root-owned cycle, always followed by verified Linux recovery.
