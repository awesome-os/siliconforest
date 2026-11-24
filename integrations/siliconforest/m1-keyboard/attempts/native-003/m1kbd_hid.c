/* Original bounded metadata validation; source facts in ADAPTER.md. */
#include "m1kbd_hid.h"
#include <string.h>
static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0]|(unsigned)p[1]<<8); }
static int bounded_string(const uint8_t *p, size_t n, unsigned field)
{
    size_t offset=u16(p+field), length=u16(p+field+2);
    return offset<=n && length<=n-offset;
}
int m1kbd_device_info(const uint8_t *p, size_t n, struct m1kbd_identity *out)
{
    if (!p || !out || n<24 || n>DLSP_MESSAGE_BYTES-10 ||
        u16(p+4)<2 || u16(p+4)>4 || u16(p+6)!=0x05ac || u16(p+8)!=0x0341 ||
        !bounded_string(p,n,12) || !bounded_string(p,n,16) || !bounded_string(p,n,20)) return -1;
    struct m1kbd_identity result;
    memset(&result,0,sizeof(result));
    result.vendor=u16(p+6); result.product=u16(p+8); result.version=u16(p+10);
    *out=result;
    return 0;
}
int m1kbd_interface_info(const uint8_t *p, size_t n, struct m1kbd_identity *out)
{
    if (!p || !out || n<16 || n>DLSP_MESSAGE_BYTES-10 || p[1]!=1 || !u16(p+6) ||
        u16(p+6)>DLSP_MESSAGE_BYTES-10 || !bounded_string(p,n,12)) return -1;
    out->max_input=u16(p+6); out->country=p[5];
    return 0;
}
int m1kbd_response_matches(const struct dlsp_message *m, uint8_t report, uint8_t subdevice, uint8_t id)
{
    return m && m->device==0xd0 && m->flags==0x40 && m->command==0x20 &&
        m->report==report && m->subdevice==subdevice && m->id==id;
}
int m1kbd_keyboard_input(const struct dlsp_message *m, uint16_t maximum)
{
    if (!m) return -1;
    if (m->device!=1 || m->flags!=0x20 || m->command!=0x10) return 0;
    if (!m->payload || !m->payload_length || !maximum || maximum>DLSP_MESSAGE_BYTES-10 ||
        m->payload_length>maximum) return -1;
    return 1;
}
void m1kbd_observe(const uint8_t *p, size_t n, const struct dlsp_message *m,
                   unsigned error, struct m1kbd_observation *out)
{
    if (!out) return;
    memset(out,0,sizeof(*out)); out->parser_error=error;
    if (p && n==DLSP_PACKET_BYTES) {
        out->packet_available=1; out->packet_flags=p[0]; out->packet_device=p[1];
        out->offset=u16(p+2); out->remain=u16(p+4); out->length=u16(p+6);
    }
    if (m) {
        out->message_available=1; out->device=m->device; out->flags=m->flags;
        out->command=m->command; out->report=m->report; out->subdevice=m->subdevice;
        out->id=m->id; out->response_length=m->response_length;
        out->payload_length=(uint32_t)m->payload_length;
    }
}
