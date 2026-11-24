#include "dl_spi_hid.h"
#include <string.h>

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1]<<8); }
static void put16(uint8_t *p, uint16_t n) { p[0]=(uint8_t)n; p[1]=(uint8_t)(n>>8); }
static int device_ok(uint8_t d) { return d<=3 || d==0xd0; }
uint16_t dlsp_crc16(const uint8_t *p, size_t n)
{
    uint16_t crc=0;
    for (size_t i=0;i<n;i++) {
        crc^=p[i];
        for (unsigned bit=0;bit<8;bit++) crc=(uint16_t)((crc>>1)^((crc&1)?0xa001u:0));
    }
    return crc;
}
void dlsp_init(struct dlsp_state *s) { if (s) memset(s,0,sizeof(*s)); }
static void clear_partial(struct dlsp_state *s)
{ s->active=0; s->used=0; s->total=0; s->device=0; s->flags=0; }
static int fail(struct dlsp_state *s, enum dlsp_error error)
{ if (s) { clear_partial(s); s->last_error=error; } return DLSP_INVALID; }
static int deliver(struct dlsp_state *s, const uint8_t *bytes, size_t n,
                   uint8_t device, uint8_t flags, struct dlsp_message *out)
{
    if (n<10 || n>DLSP_MESSAGE_BYTES || (size_t)le16(bytes+6)+10>n)
        return fail(s,DLSP_E_LENGTH);
    if (dlsp_crc16(bytes,n-2)!=le16(bytes+n-2)) return fail(s,DLSP_E_MESSAGE_CRC);
    size_t payload=le16(bytes+6);
    if (!payload) return fail(s,DLSP_E_LENGTH);
    if (flags==0x40 && bytes[0]==0x20 && bytes[1]==0x10 && payload>DLSP_DESCRIPTOR_BYTES)
        return fail(s,DLSP_E_DESCRIPTOR);
    memcpy(s->delivered,bytes,n);
    out->device=device; out->flags=flags;
    out->command=bytes[0]; out->report=bytes[1]; out->subdevice=bytes[2]; out->id=bytes[3];
    out->response_length=le16(bytes+4);
    out->payload=s->delivered+8; out->payload_length=payload;
    return DLSP_MESSAGE;
}
int dlsp_push(struct dlsp_state *s, const uint8_t *packet, size_t bytes, struct dlsp_message *out)
{
    if (out) memset(out,0,sizeof(*out));
    if (!s || !packet || !out || bytes!=DLSP_PACKET_BYTES) return fail(s,DLSP_E_ARGUMENT);
    s->last_error=DLSP_E_NONE;
    if (dlsp_crc16(packet,254)!=le16(packet+254)) return fail(s,DLSP_E_PACKET_CRC);
    size_t n=le16(packet+6), offset=le16(packet+2), remain=le16(packet+4);
    if (n==4 && !memcmp(packet+8,"\xa0\x80\0\0",4)) {
        clear_partial(s); out->device=packet[1]; out->flags=packet[0]; return DLSP_BOOT;
    }
    if (packet[0]!=0x20 && packet[0]!=0x40) return fail(s,DLSP_E_FLAGS);
    if (!device_ok(packet[1])) return fail(s,DLSP_E_DEVICE);
    if (!n || n>DLSP_FRAGMENT_BYTES || offset+n+remain>DLSP_MESSAGE_BYTES)
        return fail(s,DLSP_E_LENGTH);
    if (!offset && !remain) return deliver(s,packet+8,n,packet[1],packet[0],out);
    if (!offset) {
        clear_partial(s);
        memcpy(s->assembling,packet+8,n);
        s->used=n; s->total=n+remain; s->device=packet[1]; s->flags=packet[0]; s->active=1;
        return DLSP_WAIT;
    }
    if (!s->active || offset!=s->used || s->device!=packet[1] || s->flags!=packet[0] ||
        offset+n+remain!=s->total) return fail(s,DLSP_E_ORDER);
    memcpy(s->assembling+s->used,packet+8,n); s->used+=n;
    if (remain) return DLSP_WAIT;
    if (s->used!=s->total) return fail(s,DLSP_E_LENGTH);
    uint8_t device=s->device, flags=s->flags; size_t total=s->total;
    int result=deliver(s,s->assembling,total,device,flags,out);
    clear_partial(s);
    return result;
}
int dlsp_request(uint8_t device, uint8_t command, uint8_t report, uint8_t subdevice,
                 uint8_t id, uint16_t response, const uint8_t *payload, size_t n,
                 uint8_t output[DLSP_PACKET_BYTES])
{
    if (!output || !device_ok(device) || n>DLSP_FRAGMENT_BYTES-10 || (n && !payload) ||
        response>DLSP_MESSAGE_BYTES-10 || (command==0x20 && report==0x10 && response>DLSP_DESCRIPTOR_BYTES))
        return DLSP_INVALID;
    uint8_t work[DLSP_PACKET_BYTES]={0};
    work[0]=0x40; work[1]=device; put16(work+6,(uint16_t)(n+10));
    work[8]=command; work[9]=report; work[10]=subdevice; work[11]=id;
    put16(work+12,response); put16(work+14,(uint16_t)n);
    if (n) memcpy(work+16,payload,n);
    put16(work+16+n,dlsp_crc16(work+8,n+8));
    put16(work+254,dlsp_crc16(work,254));
    memcpy(output,work,sizeof(work));
    return DLSP_WAIT;
}
