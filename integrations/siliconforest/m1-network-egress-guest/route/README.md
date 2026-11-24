# Darwin routing helpers: runtime admission blocked

The original portable routing-message encoder/decoder passes host sanitizer
tests. route_abi.c compiles against the pinned Darwin SDK and checks the actual
message layout and numeric constants. Those checks establish neither route
ownership nor successful routing-socket execution.

Independent source inspection found three blockers in owned_routes.c:

1. The pinned kernel emits protocol-zero ADD and failed-GET acknowledgements.
   An AF_INET-only routing socket filters those replies.
2. A GET followed by DELETE is not atomic. The kernel does not treat the supplied
   gateway/interface as conditional ownership checks; a replacement route can
   be deleted. Another readback cannot close that race.
3. An installed ADD with a lost acknowledgement is not recorded as owned.

The adapter is now blocked at compilation unless the explicit analysis-only
macro is defined. It has never been staged or executed. Public networking must
resolve these contracts or use a different owned-interface route mechanism.
The current controlled TCP/UDP link uses only its original point-to-point peer
and does not include this adapter. No default route, host route, physical
networking, GPU, firmware or kernel change has occurred.
