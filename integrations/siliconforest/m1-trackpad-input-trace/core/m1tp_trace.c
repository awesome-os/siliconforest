#include "m1tp_trace.h"
#include <string.h>

void m1tp_trace_init(struct m1tp_trace_state *s)
{ if (s) memset(s,0,sizeof(*s)); }
int m1tp_trace_announce(struct m1tp_trace_state *s)
{
    if (!s || s->announced) return 0;
    s->announced=1;
    return 1;
}
static void count(uint32_t *n)
{ if (*n!=UINT32_MAX) ++*n; }
int m1tp_trace_select(struct m1tp_trace_state *s, const struct dlsp_message *m,
                      struct m1tp_trace_record *out)
{
    uint8_t id;
    uint32_t reason=0;
    int motion=0;
    struct m1tp_trace_record r;
    if (!s || !s->announced || !m || !out || m->device!=2 ||
        m->flags!=0x20 || m->command!=0x10 || !m->payload ||
        !m->payload_length || m->payload_length>1752u) return 0;
    id=m->payload[0];
    switch (id) {
    case 0x02:
        if (m->payload_length!=8) return 0;
        motion=m->payload[2]!=0x80 && m->payload[3]!=0x80 &&
            (m->payload[2]!=0 || m->payload[3]!=0);
        count(&s->seen02);
        if (s->logged02<16) reason=1;
        else if (motion && !s->motion_logged) reason=2;
        break;
    case 0x3f:
        if (m->payload_length!=17) return 0;
        count(&s->seen3f);
        if (s->logged3f<2) reason=1;
        break;
    case 0x44:
        if (m->payload_length!=1752) return 0;
        count(&s->seen44);
        if (s->logged44<1) reason=1;
        break;
    default: return 0;
    }
    if (!reason || s->sequence>=M1TP_TRACE_MAX_RECORDS) return 0;
    if (id==0x02) {
        ++s->logged02;
        if (motion) s->motion_logged=1;
    } else if (id==0x3f) ++s->logged3f;
    else ++s->logged44;
    memset(&r,0,sizeof(r));
    r.sequence=++s->sequence; r.reason=reason; r.report_id=id;
    r.seen02=s->seen02; r.seen3f=s->seen3f; r.seen44=s->seen44;
    r.bytes=(uint16_t)m->payload_length;
    r.crc16=dlsp_crc16(m->payload,m->payload_length);
    *out=r;
    return 1;
}
size_t m1tp_trace_hex(const uint8_t *p, size_t n, size_t index,
                      char out[M1TP_TRACE_HEX_BYTES])
{
    static const char digits[]="0123456789abcdef";
    size_t off, len, i;
    if (!p || !out || !n || n>1752u || index>(n-1u)/M1TP_TRACE_CHUNK_BYTES)
        return 0;
    off=index*M1TP_TRACE_CHUNK_BYTES;
    len=n-off;
    if (len>M1TP_TRACE_CHUNK_BYTES) len=M1TP_TRACE_CHUNK_BYTES;
    for (i=0;i<len;i++) {
        out[i*2u]=digits[p[off+i]>>4];
        out[i*2u+1u]=digits[p[off+i]&15u];
    }
    out[len*2u]='\0';
    return len;
}
