/* Original finite routing-socket wire helpers; no allocation or I/O. */
#include "route_wire.h"
#include <string.h>

static uint16_t le16(const uint8_t *p) {
  return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}
static uint32_t le32(const uint8_t *p) {
  return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}
static uint32_t be32(const uint8_t *p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
         (uint32_t)p[2] << 8 | p[3];
}
static void put16(uint8_t *p, uint16_t v) {
  p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
}
static void put32(uint8_t *p, uint32_t v) {
  for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i));
}
static void inet(uint8_t *p, uint32_t v) {
  p[0]=16; p[1]=2;
  for(unsigned i=0;i<4;i++) p[4+i]=(uint8_t)(v>>(24-8*i));
}
int dle_route_encode(uint8_t *out, size_t cap, size_t *length,
                     uint32_t pid, uint32_t seq, uint8_t type,
                     uint32_t dst, uint32_t gw, uint16_t index) {
  if(!out || !length || !pid || pid>0x7fffffff || !seq ||
     seq>0x7fffffff || (type!=1 && type!=2 && type!=4) || !dst ||
     (type!=4 && (!gw || !index))) return -1;
  uint8_t b[160]={0}; size_t n=type==4?108:160;
  if(cap<n) return -1;
  put16(b,(uint16_t)n); b[2]=5; b[3]=type;
  put16(b+4,index);
  put32(b+8,type==4?4:0x807); /* HOST or UP|GATEWAY|HOST|STATIC */
  put32(b+12,type==4?1:0x17);
  put32(b+16,pid); put32(b+20,seq); inet(b+92,dst);
  if(type!=4) {
    inet(b+108,gw); inet(b+124,0xffffffff);
    /* sockaddr_dl: length20, AF_LINK18, index; no guessed name. */
    b[140]=20; b[141]=18; put16(b+142,index);
  }
  memcpy(out,b,n); *length=n; return 0;
}
int dle_route_decode(const uint8_t *b, size_t n, uint32_t pid,
                     uint32_t seq, uint8_t type, struct dle_route_view *out) {
  if(!b || !out || !pid || !seq || (type!=1 && type!=2 && type!=4) ||
     n<4 || n>4096) return -1;
  /* Address/interface broadcasts have a different, often shorter header.
   * They cannot acknowledge any owned ADD/DELETE/GET operation. */
  if(b[2]==5 && (b[3]==12 || b[3]==13 || b[3]==14 || b[3]==15 ||
                 b[3]==16 || b[3]==18 || b[3]==19)) return 1;
  if(n<92) return -1;
  if(le32(b+16)!=pid || le32(b+20)!=seq) return 1;
  if(le16(b)!=n || b[2]!=5 || b[3]!=type || (le32(b+12)&~255u))
    return -1;
  struct dle_route_view v={0};
  v.flags=le32(b+8); v.error=le32(b+24); v.interface_index=le16(b+4);
  size_t offset=92; uint32_t addrs=le32(b+12);
  for(unsigned i=0;i<8;i++) {
    if(!(addrs & (1u<<i))) continue;
    if(n-offset<2) return -1;
    size_t size=b[offset], padded=(size+3)&~(size_t)3;
    if(size<2 || size>128 || size>n-offset || padded>n-offset) return -1;
    const uint8_t *sa=b+offset;
    if(i==0 || i==1) {
      if(size!=16 || sa[1]!=2 || sa[2] || sa[3]) return -1;
      uint32_t ip=be32(sa+4);
      if(i==0) {v.destination=ip; v.have_destination=1;}
      else {v.gateway=ip; v.have_gateway=1;}
    } else if(i==2) {
      /* XNU compresses netmasks and permits AF_UNSPEC. Missing tail is zero. */
      if(size>16 || (sa[1]!=0 && sa[1]!=2)) return -1;
      uint8_t full[16]={0}; memcpy(full,sa,size);
      v.netmask=be32(full+4); v.have_netmask=1;
    } else if(i==4) {
      if(size<8 || sa[1]!=18 || le16(sa+2)!=v.interface_index)
        return -1;
      if((size_t)sa[5]+sa[6]+sa[7]>size-8) return -1;
    }
    offset+=padded;
  }
  if(offset!=n) return -1;
  /* Error replies echo the input; ownership/success checks belong to caller. */
  *out=v; return 0;
}
