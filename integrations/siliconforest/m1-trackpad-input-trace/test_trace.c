/* Original host-only policy and byte-boundary tests, no hardware. */
#ifdef NDEBUG
#error "NDEBUG forbidden: trace tests require assertions"
#endif
#include "m1tp_trace.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct dlsp_message message(uint8_t *p, size_t n)
{
    struct dlsp_message m={2,0x20,0x10,0x99,0x83,0xfe,65535,p,n};
    return m;
}
static struct m1tp_trace_state active(void)
{
    struct m1tp_trace_state s;
    m1tp_trace_init(&s);assert(m1tp_trace_announce(&s)==1);
    assert(m1tp_trace_announce(&s)==0);return s;
}
static void quotas_and_reserved(void)
{
    uint8_t p[1752]={2};
    struct dlsp_message m=message(p,8);
    struct m1tp_trace_state s=active();struct m1tp_trace_record r;
    for (unsigned i=1;i<=16;i++) {
        assert(m1tp_trace_select(&s,&m,&r)==1);
        assert(r.sequence==i && r.reason==1 && r.seen02==i);
        assert(r.bytes==8 && r.crc16==dlsp_crc16(p,8));
    }
    for (unsigned i=0;i<100;i++) assert(m1tp_trace_select(&s,&m,&r)==0);
    p[2]=0x80;p[3]=1;assert(m1tp_trace_select(&s,&m,&r)==0);
    p[2]=1;p[3]=0x80;assert(m1tp_trace_select(&s,&m,&r)==0);
    p[2]=0x81;p[3]=0x7f;assert(m1tp_trace_select(&s,&m,&r)==1);
    assert(r.sequence==17 && r.reason==2 && r.seen02==119);
    for (unsigned i=0;i<100;i++) assert(m1tp_trace_select(&s,&m,&r)==0);
    p[0]=0x3f;m.payload_length=17;
    for (unsigned i=0;i<2;i++) assert(m1tp_trace_select(&s,&m,&r)==1);
    assert(r.sequence==19 && r.seen3f==2);
    assert(m1tp_trace_select(&s,&m,&r)==0);
    p[0]=0x44;m.payload_length=1752;assert(m1tp_trace_select(&s,&m,&r)==1);
    assert(r.sequence==20 && r.bytes==1752 && r.seen44==1);
    assert(m1tp_trace_select(&s,&m,&r)==0);
    assert(s.sequence==20 && s.logged02==17 && s.logged3f==2 && s.logged44==1);
}
static void invalid_axes_are_observed(void)
{
    struct m1tp_trace_state s=active();struct m1tp_trace_record r;
    uint8_t p[8]={2,0xff,0x80,1};struct dlsp_message m=message(p,8);
    for (unsigned i=1;i<=16;i++) {
        assert(m1tp_trace_select(&s,&m,&r)==1);
        assert(r.reason==1 && r.seen02==i && !s.motion_logged);
    }
    assert(m1tp_trace_select(&s,&m,&r)==0 && s.seen02==17);
    p[2]=0;p[3]=0x81;assert(m1tp_trace_select(&s,&m,&r)==1);
    assert(r.reason==2 && r.sequence==17 && s.motion_logged);
    /* Any valid motion in the initial16 consumes the reserve permanently. */
    s=active();p[2]=1;p[3]=0;
    for (unsigned i=0;i<16;i++) assert(m1tp_trace_select(&s,&m,&r)==1);
    for (unsigned i=0;i<100;i++) assert(m1tp_trace_select(&s,&m,&r)==0);
    assert(s.sequence==16 && s.motion_logged);
}
static void independent_vendor_caps(void)
{
    uint8_t p[1752]={0x44};struct dlsp_message m=message(p,sizeof(p));
    struct m1tp_trace_state s=active();struct m1tp_trace_record r;
    /* Exercise each cap before the total20 ceiling can mask a bad cap. */
    assert(m1tp_trace_select(&s,&m,&r)==1);
    assert(m1tp_trace_select(&s,&m,&r)==0);
    assert(s.sequence==1 && s.seen44==2 && s.logged44==1);
    s=active();p[0]=0x3f;m.payload_length=17;
    assert(m1tp_trace_select(&s,&m,&r)==1);
    assert(m1tp_trace_select(&s,&m,&r)==1);
    assert(m1tp_trace_select(&s,&m,&r)==0);
    assert(s.sequence==2 && s.seen3f==3 && s.logged3f==2);
}
static void refusal_and_counter_boundaries(void)
{
    struct m1tp_trace_state s=active(), before;
    struct m1tp_trace_record r, old;
    uint8_t p[1753]={2};struct dlsp_message m=message(p,8);
    memset(&r,0xa5,sizeof(r));old=r;before=s;
    assert(!m1tp_trace_select(NULL,&m,&r));
    assert(!m1tp_trace_select(&s,NULL,&r));
    assert(!m1tp_trace_select(&s,&m,NULL));
    for (unsigned d=0;d<256;d++) {
        if (d==2) continue;
        m.device=(uint8_t)d;m.payload=(const uint8_t *)(uintptr_t)1;
        assert(!m1tp_trace_select(&s,&m,&r));
    }
    m.device=2;m.payload=p;
    for (unsigned f=0;f<256;f++) {
        if (f==0x20) continue;
        m.flags=(uint8_t)f;assert(!m1tp_trace_select(&s,&m,&r));
    }
    m.flags=0x20;m.command=0x20;assert(!m1tp_trace_select(&s,&m,&r));m.command=0x10;
    for (size_t n=0;n<sizeof(p);n++) {
        m.payload_length=n;
        if (n!=8) assert(!m1tp_trace_select(&s,&m,&r));
    }
    assert(!memcmp(&r,&old,sizeof(r)) && !memcmp(&s,&before,sizeof(s)));
    m.payload_length=8;p[0]=3;assert(!m1tp_trace_select(&s,&m,&r));
    p[0]=2;m.payload=NULL;assert(!m1tp_trace_select(&s,&m,&r));m.payload=p;
    s.seen02=UINT32_MAX;assert(m1tp_trace_select(&s,&m,&r));
    assert(s.seen02==UINT32_MAX && r.seen02==UINT32_MAX);
    s=active();s.announced=0;assert(!m1tp_trace_select(&s,&m,&r));
    assert(!s.sequence && !s.seen02);
}
static void hex_chunks_and_real_crc(void)
{
    struct { char before;char text[129];char after; } out;
    uint8_t p[1752], joined[1752];size_t used=0;
    for (size_t i=0;i<sizeof(p);i++) p[i]=(uint8_t)i;
    assert(dlsp_crc16((const uint8_t *)"123456789",9)==0xbb3d);
    for (size_t chunk=0;chunk<28;chunk++) {
        memset(&out,0xa5,sizeof(out));
        size_t n=m1tp_trace_hex(p,sizeof(p),chunk,out.text);
        assert(n==(chunk==27?24u:64u));assert(strlen(out.text)==n*2);
        assert((unsigned char)out.before==0xa5 && (unsigned char)out.after==0xa5);
        for (size_t i=0;i<n;i++) {
            unsigned value=0;char pair[3]={out.text[i*2],out.text[i*2+1],0};
            assert(sscanf(pair,"%2x",&value)==1);joined[used++]=(uint8_t)value;
            assert(!(pair[0]>='A' && pair[0]<='F') && !(pair[1]>='A' && pair[1]<='F'));
        }
    }
    assert(used==sizeof(p) && !memcmp(p,joined,sizeof(p)));
    assert(dlsp_crc16(p,sizeof(p))==dlsp_crc16(joined,used));
    memset(&out,0xa5,sizeof(out));
    assert(!m1tp_trace_hex(NULL,8,0,out.text));
    assert(!m1tp_trace_hex(p,0,0,out.text));
    assert(!m1tp_trace_hex(p,1753,0,out.text));
    assert(!m1tp_trace_hex(p,1752,28,out.text));
    assert(!m1tp_trace_hex(p,1752,SIZE_MAX,out.text));
    for (size_t i=0;i<sizeof(out);i++) assert(((unsigned char *)&out)[i]==0xa5);
}
int main(void)
{
    quotas_and_reserved();invalid_axes_are_observed();independent_vendor_caps();
    refusal_and_counter_boundaries();hex_chunks_and_real_crc();
    puts("HOST ONLY PASS: selection, raw invalid axes, quotas, reserve, refusal, CRC and hex bounds");
    return 0;
}
