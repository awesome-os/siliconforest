/* Original host-only fake mailbox composition; no physical transport. */
#include "envelope.h"
#include "../m1-gpu-rtkit-control/control.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define C(x) do { ++checks; if (!(x)) { fprintf(stderr,"check line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
#define P ACE_PIN121FCBF_LEGACY
static void fixture(uint8_t *b, uint64_t w, uint64_t m)
{
    for (unsigned i=0;i<8;i++) { b[i]=(uint8_t)w; w/=256; b[i+8]=(uint8_t)m; m/=256; }
}
static void boundaries(void)
{
    uint8_t b[17], keep[17]; struct ace_rx r, saved; struct ace_message a, old;
    memset(b,0xa5,sizeof b); memcpy(keep,b,sizeof b);
    memset(&r,0x5a,sizeof r); saved=r; memset(&a,0x5a,sizeof a); old=a;
    for (size_t n=0;n<=33;n++) if(n!=16) {
        C(ace_decode_rx(P,b,n,&r)==ACE_LENGTH); C(!memcmp(&r,&saved,sizeof r));
        C(ace_rtkit_input(P,b,n,&a)==ACE_LENGTH); C(!memcmp(&a,&old,sizeof a));
        C(ace_encode_tx(P,0,0,b,n)==ACE_LENGTH); C(!memcmp(b,keep,sizeof b));
    }
    for (unsigned p=0;p<=3;p++) if(p!=1) {
        C(ace_decode_rx((enum ace_profile)p,b,16,&r)==ACE_PROFILE);
        C(ace_rtkit_input((enum ace_profile)p,b,16,&a)==ACE_PROFILE);
        C(ace_encode_tx((enum ace_profile)p,0,0,b,16)==ACE_PROFILE);
        C(!memcmp(&r,&saved,sizeof r)&&!memcmp(&a,&old,sizeof a)&&!memcmp(b,keep,sizeof b));
    }
    C(ace_encode_tx(P,256,0,b,16)==ACE_ENDPOINT);
    C(ace_encode_tx(P,UINT32_MAX,0,b,16)==ACE_ENDPOINT); C(!memcmp(b,keep,sizeof b));
    C(ace_encode_tx(P,0,0,NULL,16)==ACE_ARGUMENT);
    C(ace_decode_rx(P,NULL,16,&r)==ACE_ARGUMENT);
    C(ace_decode_rx(P,b,16,NULL)==ACE_ARGUMENT);
    C(ace_rtkit_input(P,NULL,16,&a)==ACE_ARGUMENT);
    C(ace_rtkit_input(P,b,16,NULL)==ACE_ARGUMENT);
    const uint8_t *overflow=(const uint8_t *)(uintptr_t)(UINTPTR_MAX-7);
    C(ace_decode_rx(P,overflow,16,&r)==ACE_ARGUMENT);
    C(ace_rtkit_input(P,overflow,16,&a)==ACE_ARGUMENT);
    C(ace_encode_tx(P,0,0,(uint8_t *)(uintptr_t)(UINTPTR_MAX-7),16)==ACE_ARGUMENT);
    C(!memcmp(&r,&saved,sizeof r)&&!memcmp(&a,&old,sizeof a));
    union { struct ace_rx align; uint8_t bytes[192]; } u;
    memset(&u,0xa5,sizeof u); uint8_t snapshot[192]; memcpy(snapshot,u.bytes,192);
    for(unsigned at=49;at<128;at++) {
        C(ace_decode_rx(P,u.bytes+at,16,(struct ace_rx *)(void *)(u.bytes+64))==ACE_OVERLAP);
        C(!memcmp(snapshot,u.bytes,192));
    }
    for(unsigned at=49;at<80;at++) {
        C(ace_rtkit_input(P,u.bytes+at,16,(struct ace_message *)(void *)(u.bytes+64))==ACE_OVERLAP);
        C(!memcmp(snapshot,u.bytes,192));
    }
    C(ace_decode_rx(P,u.bytes+48,16,(struct ace_rx *)(void *)(u.bytes+64))==ACE_OK);
    C(ace_decode_rx(P,u.bytes+128,16,(struct ace_rx *)(void *)(u.bytes+64))==ACE_OK);
}
static void bits(void)
{
    uint8_t b[17]; struct ace_rx r; struct ace_message a, old;
    const uint8_t golden[16]={0xef,0xcd,0xab,0x89,0x67,0x45,0x23,0x01,0xff,0,0,0,0,0,0,0};
    C(ace_encode_tx(P,255,UINT64_C(0x0123456789abcdef),b,16)==ACE_OK);
    C(!memcmp(b,golden,16));
    const uint8_t rx_golden[16]={0x0b,0,0x0c,0,0,0,0x10,0,0x20,0,0,0,0,0x56,0x34,0x01};
    C(ace_decode_rx(P,rx_golden,16,&r)==ACE_OK);
    C(r.word==UINT64_C(0x00100000000c000b)&&r.metadata==UINT64_C(0x0134560000000020));
    C(r.endpoint==32&&r.outcnt==19&&r.incnt==4&&r.outptr==5&&r.inptr==6&&!r.opaque);
    for(unsigned ep=0;ep<256;ep++) {
        C(ace_encode_tx(P,ep,UINT64_MAX,b+1,16)==ACE_OK);
        C(ace_decode_rx(P,b+1,16,&r)==ACE_OK);
        C(r.endpoint==ep && r.metadata==ep && r.word==UINT64_MAX && !r.opaque);
        C(!r.outcnt&&!r.incnt&&!r.outptr&&!r.inptr);
    }
    for(unsigned bit=0;bit<64;bit++) {
        uint64_t m=UINT64_C(1)<<bit;
        fixture(b,UINT64_C(1)<<(63-bit),m);
        C(ace_decode_rx(P,b,16,&r)==ACE_OK);
        C(r.word==(UINT64_C(1)<<(63-bit))&&r.metadata==m);
        C(r.endpoint==(bit<8?m:0));
        C(r.outcnt==(bit>=52&&bit<=56?UINT64_C(1)<<(bit-52):0));
        C(r.incnt==(bit>=48&&bit<=51?UINT64_C(1)<<(bit-48):0));
        C(r.outptr==(bit>=44&&bit<=47?UINT64_C(1)<<(bit-44):0));
        C(r.inptr==(bit>=40&&bit<=43?UINT64_C(1)<<(bit-40):0));
        C(r.opaque==((bit>=8&&bit<=39)||bit>=57?m:0));
        memset(&a,0xa5,sizeof a); old=a;
        enum ace_code c=ace_rtkit_input(P,b,16,&a);
        if(bit>=8&&bit<=31) { C(c==ACE_METADATA); C(!memcmp(&a,&old,sizeof a)); }
        else { C(c==ACE_OK); C(a.endpoint==r.endpoint&&a.word==r.word); }
    }
    fixture(b,UINT64_MAX,UINT64_MAX); C(ace_decode_rx(P,b,16,&r)==ACE_OK);
    C(r.outcnt==31&&r.incnt==15&&r.outptr==15&&r.inptr==15&&r.endpoint==255);
    C(r.opaque==UINT64_C(0xfe0000ffffffff00));
}
static enum rtc_code receive(struct rtc_context *c,uint64_t now,uint64_t w,uint64_t meta)
{
    uint8_t b[16]; struct ace_message m; struct ace_rx r;
    fixture(b,w,meta);
    C(ace_decode_rx(P,b,16,&r)==ACE_OK); C(r.metadata==meta&&r.word==w);
    C(ace_rtkit_input(P,b,16,&m)==ACE_OK);
    return rtc_receive(c,now,(uint32_t)m.endpoint,m.word);
}
static struct rtc_ticket tx(struct rtc_context *c,uint64_t now,uint64_t expected)
{
    struct rtc_packet p; struct rtc_context saved; uint8_t b[16], golden[16];
    C(rtc_tx_peek(c,now,&p)==RTC_OK); saved=*c;
    C(ace_encode_tx(P,p.endpoint,p.word,b,sizeof b)==ACE_OK);
    fixture(golden,expected,0); C(!memcmp(b,golden,16));
    C(!memcmp(c,&saved,sizeof saved)); /* Encoding never commits. */
    return p.ticket;
}
static void composition(void)
{
    for(unsigned ver=11;ver<=12;ver++) {
        struct rtc_context c={0}; struct rtc_result result; uint64_t t=1;
        C(rtc_init(&c,t++,1000)==RTC_OK);
        struct rtc_ticket ticket=tx(&c,t++,UINT64_C(0x0060000000000220));
        C(rtc_tx_commit(&c,t++,ticket,RTC_WOULD_BLOCK)==RTC_OK);
        C(c.result.phase==RTC_WAKE_QUEUED&&c.result.accepted_tx==0);
        ticket=tx(&c,t++,UINT64_C(0x0060000000000220));
        C(rtc_tx_commit(&c,t++,ticket,RTC_ACCEPTED)==RTC_OK);
        C(receive(&c,t++,UINT64_C(0x001000000000000b)|((uint64_t)ver<<16),UINT64_C(0xff12340000000000))==RTC_OK);
        ticket=tx(&c,t++,UINT64_C(0x0020000000000000)|((uint64_t)ver<<16)|ver);
        C(rtc_tx_commit(&c,t++,ticket,RTC_ACCEPTED)==RTC_OK);
        C(receive(&c,t++,UINT64_C(0x008000000000011f),0)==RTC_OK);
        ticket=tx(&c,t++,UINT64_C(0x0080000000000001));
        C(rtc_tx_commit(&c,t++,ticket,RTC_ACCEPTED)==RTC_OK);
        C(receive(&c,t++,UINT64_C(0x0088000100000003),0)==RTC_OK);
        ticket=tx(&c,t++,UINT64_C(0x0088000100000000));
        C(c.result.phase==RTC_MAP_ACK_QUEUED);
        C(rtc_tx_commit(&c,t++,ticket,RTC_ACCEPTED)==RTC_OK);
        C(rtc_finish(&c,t++,&result)==RTC_OK);
        C(result.phase==RTC_CONTROL_DISCOVERED_PROTOCOL_ONLY&&result.accepted_tx==4&&result.negotiated_version==ver);
    }
    struct rtc_context c={0}; C(rtc_init(&c,1,1000)==RTC_OK);
    struct rtc_context unchanged=c; struct ace_message rejected={123,456}, old=rejected;
    uint8_t malformed[16]; fixture(malformed,UINT64_C(0x00100000000c000b),256);
    C(ace_rtkit_input(P,malformed,16,&rejected)==ACE_METADATA);
    C(!memcmp(&rejected,&old,sizeof old)&&!memcmp(&c,&unchanged,sizeof c));
    (void)tx(&c,2,UINT64_C(0x0060000000000220));
    C(receive(&c,3,UINT64_C(0x00100000000c000b),0)==RTC_STATE);
    for(unsigned d=RTC_DELIVERY_FAILED;d<=RTC_DELIVERY_UNCERTAIN;d++) {
        struct rtc_context f={0}; C(rtc_init(&f,1,1000)==RTC_OK);
        struct rtc_ticket ticket=tx(&f,2,UINT64_C(0x0060000000000220));
        C(rtc_tx_commit(&f,3,ticket,(enum rtc_delivery)d)==(d==RTC_DELIVERY_FAILED?RTC_SEND_FAILED:RTC_SEND_UNCERTAIN));
        C(f.result.phase==RTC_FAILED&&f.result.accepted_tx==0);
    }
    struct rtc_context f={0}; C(rtc_init(&f,1,1000)==RTC_OK);
    C(receive(&f,2,UINT64_C(0x0010200001234000),2)==RTC_BLOCKED_SYSTEM_BUFFER);
    C(f.result.buffer.outstanding&&f.result.buffer.raw_word==UINT64_C(0x0010200001234000));
    struct rtc_context g={0}; C(rtc_init(&g,1,1000)==RTC_OK);
    struct rtc_ticket ticket=tx(&g,2,UINT64_C(0x0060000000000220));
    C(rtc_tx_commit(&g,3,ticket,RTC_ACCEPTED)==RTC_OK);
    C(rtc_tx_commit(&g,4,ticket,RTC_ACCEPTED)!=RTC_OK);
    struct rtc_context h={0}; C(rtc_init(&h,1,1000)==RTC_OK);
    ticket=tx(&h,2,UINT64_C(0x0060000000000220));
    C(rtc_tx_commit(&h,3,ticket,RTC_ACCEPTED)==RTC_OK);
    C(receive(&h,4,UINT64_C(0x80100000000c000b),0)==RTC_MESSAGE);
}
int main(void) { boundaries(); bits(); composition(); printf("%u host checks PASS; protocol-only fake mailbox\n",checks); return 0; }
