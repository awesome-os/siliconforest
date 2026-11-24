#ifndef DLR_CLOSE_GUARD_H
#define DLR_CLOSE_GUARD_H
#include <stdint.h>
enum dlr_drain_error { DLR_DRAIN_OK=0,DLR_DRAIN_ARGUMENT=1,DLR_DRAIN_CLOCK=2,DLR_DRAIN_TIMEOUT=3,DLR_DRAIN_STREAM=4 };
struct dlr_drain { uint64_t started,deadline,last,completed;unsigned state;enum dlr_drain_error error; };
/* Pure, fresh-zeroed caller state. Production calls 40000; 15000 exists only
 * for the preserved legacy-policy offline comparison. No timer may be reset.
 * poll returns 0 waiting, 1 once complete, -1 on sticky failure/invalid reuse.
 * rx_complete is meaningful only after actual codec CLOSE. No timer, masks or
 * empty queue substitute for that CLOSE, full bytes, or final pending ACK. */
int dlr_drain_begin(struct dlr_drain *,uint64_t now,unsigned budget_ms);
int dlr_drain_poll(struct dlr_drain *,uint64_t now,int peer_close,unsigned output_count,unsigned input_count,unsigned partial_line,unsigned partial_bits,int rx_complete);
#endif
