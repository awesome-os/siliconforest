#include "m1tp_child_policy.h"

int m1tp_child_input(const struct dlsp_message *m)
{
    size_t expected;
    if (!m) return -1;
    if (m->flags!=0x20 || m->device!=2 || m->command!=0x10) return 0;
    if (!m->payload || !m->payload_length || m->payload_length>DLSP_MESSAGE_BYTES-10u)
        return -1;
    switch (m->payload[0]) {
    case 0x02: expected=DL_M1_TP_REPORT02_BYTES; break;
    case 0x3f: expected=DL_M1_TP_REPORT3F_BYTES; break;
    case 0x44: expected=DL_M1_TP_REPORT44_BYTES; break;
    default: return -1;
    }
    return m->payload_length==expected ? 1 : -1;
}

int m1tp_child_descriptor_equal(const uint8_t *a, size_t n,
                               const uint8_t *r, size_t rn)
{
    size_t i;
    if (!a || !r || n!=DL_M1_TP_DESCRIPTOR_BYTES || rn!=n) return 0;
    for (i=0;i<n;i++) if (a[i]!=r[i]) return 0;
    return 1;
}
