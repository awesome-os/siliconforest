/* Real policy and wire engine; synthetic input only, never hardware evidence. */
#ifdef NDEBUG
#error "T2 policy tests require assertions (NDEBUG forbidden)"
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "m1tp_child_policy.h"

static void put16(uint8_t *p,unsigned v) { p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8); }
int main(void)
{
    uint8_t data[2039]={0},reference[110],actual[110];
    struct dlsp_message m={2,0x20,0x10,2,0,99,65535,data,0};
    for(unsigned id=0;id<256;id++) {
        data[0]=(uint8_t)id;
        for(size_t n=0;n<=sizeof(data);n++) {
            m.payload_length=n;
            int expected=(id==2&&n==8)||(id==0x3f&&n==17)||(id==0x44&&n==1752);
            assert((m1tp_child_input(&m)==1)==expected);
        }
    }
    data[0]=2;m.payload_length=8;
    m.report=0xff;m.subdevice=0xfe;m.id=0xfd;assert(m1tp_child_input(&m)==1);
    m.device=1;assert(m1tp_child_input(&m)==0);m.device=2;
    m.flags=0x40;assert(m1tp_child_input(&m)==0);m.flags=0x20;
    m.command=0x20;assert(m1tp_child_input(&m)==0);m.command=0x10;
    m.payload=NULL;assert(m1tp_child_input(&m)==-1);assert(m1tp_child_input(NULL)==-1);
    for(unsigned i=0;i<110;i++)reference[i]=(uint8_t)(i^0xa5);
    memcpy(actual,reference,110);assert(m1tp_child_descriptor_equal(actual,110,reference,110));
    for(unsigned i=0;i<110;i++) {
        actual[i]^=1;assert(!m1tp_child_descriptor_equal(actual,110,reference,110));actual[i]^=1;
    }
    assert(!m1tp_child_descriptor_equal(NULL,110,reference,110));
    assert(!m1tp_child_descriptor_equal(actual,109,reference,110));
    assert(!m1tp_child_descriptor_equal(actual,110,reference,109));
    /* Actual production dlsp_push must assemble the 1752-byte report within
     * unchanged message2048/fragment246 capacities and verify both CRCs. */
    uint8_t message[1762]={0x10,0,2,0,0,0,0,0};
    put16(message+6,1752);message[8]=0x44;
    for(unsigned i=9;i<1760;i++)message[i]=(uint8_t)i;
    put16(message+1760,dlsp_crc16(message,1760));
    struct dlsp_state wire;dlsp_init(&wire);
    for(unsigned off=0;off<sizeof(message);) {
        uint8_t packet[256]={0x20,2};unsigned n=(unsigned)sizeof(message)-off;
        if(n>246)n=246;
        put16(packet+2,off);put16(packet+4,(unsigned)sizeof(message)-off-n);put16(packet+6,n);
        memcpy(packet+8,message+off,n);put16(packet+254,dlsp_crc16(packet,254));
        int result=dlsp_push(&wire,packet,sizeof(packet),&m);
        off+=n;assert(result==(off==sizeof(message)?DLSP_MESSAGE:DLSP_WAIT));
        if(result==DLSP_MESSAGE) {
            assert(m.payload_length==1752 && !memcmp(m.payload,message+8,1752));
            assert(m1tp_child_input(&m)==1);
        }
        packet[254]^=1;assert(dlsp_push(&wire,packet,sizeof(packet),&m)==DLSP_INVALID);
        if(off<sizeof(message)) {
            /* Restart prefix after corruption: no false reassembly success. */
            dlsp_init(&wire);
            for(unsigned prefix=0;prefix<off;) {
                uint8_t p[256]={0x20,2};unsigned k=off-prefix;if(k>246)k=246;
                put16(p+2,prefix);put16(p+4,(unsigned)sizeof(message)-prefix-k);put16(p+6,k);
                memcpy(p+8,message+prefix,k);put16(p+254,dlsp_crc16(p,254));
                assert(dlsp_push(&wire,p,256,&m)==DLSP_WAIT);prefix+=k;
            }
        }
    }
    puts("exact report policy and fragmented real wire engine: HOST ONLY PASS");
    return 0;
}
