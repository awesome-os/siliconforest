/* SPDX-License-Identifier: MIT */
/* Read-only native BSD inventory; no ifconfig/netstat/route binary required.
 * Routing sockaddr alignment follows XNU rtsock.c ROUNDUP32, not host long. */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/route.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
static unsigned char *snapshot(int selector,int family,int last,size_t *length)
{
 int mib[6]={CTL_NET,PF_ROUTE,0,family,selector,last};
 for(unsigned attempt=0;attempt<3;++attempt){
  size_t size=0;
  if(sysctl(mib,6,NULL,&size,NULL,0) || !size || size>1024*1024)return NULL;
  unsigned char *bytes=malloc(size);if(!bytes)return NULL;
  if(!sysctl(mib,6,bytes,&size,NULL,0)){*length=size;return bytes;}
  int error=errno;free(bytes);if(error!=ENOMEM)return NULL;
 }
 return NULL;
}
static int counters(unsigned index)
{
 size_t size=0;unsigned char *data=snapshot(NET_RT_IFLIST2,AF_UNSPEC,index,&size);
 if(!data){perror("NET_RT_IFLIST2");return 1;}
 unsigned found=0;int error=0;
 for(size_t offset=0;offset<size;){
  uint16_t length=0;if(size-offset<4){error=1;break;}memcpy(&length,data+offset,2);
  if(length<4 || length>size-offset){error=1;break;}
  if(data[offset+2]==RTM_VERSION && data[offset+3]==RTM_IFINFO2){
   struct if_msghdr2 h;
   if(length<sizeof(h)){error=1;break;}memcpy(&h,data+offset,sizeof(h));
   if(h.ifm_index==index){
    ++found;
    printf("COUNTERS index=%u ipackets=%llu opackets=%llu ibytes=%llu obytes=%llu ierrors=%llu oerrors=%llu drops=%llu\n",
     index,(unsigned long long)h.ifm_data.ifi_ipackets,(unsigned long long)h.ifm_data.ifi_opackets,
     (unsigned long long)h.ifm_data.ifi_ibytes,(unsigned long long)h.ifm_data.ifi_obytes,
     (unsigned long long)h.ifm_data.ifi_ierrors,(unsigned long long)h.ifm_data.ifi_oerrors,
     (unsigned long long)h.ifm_data.ifi_iqdrops);
   }
  }
  offset+=length;
 }
 free(data);if(error || found!=1){fprintf(stderr,"native interface counter records incomplete\n");return 1;}return 0;
}
static int routes(void)
{
 size_t size=0;unsigned char *data=snapshot(NET_RT_DUMP2,AF_INET,0,&size);
 if(!data){perror("NET_RT_DUMP2");return 1;}
 int error=0;
 for(size_t offset=0;offset<size;){
  uint16_t length=0;if(size-offset<4){error=1;break;}memcpy(&length,data+offset,2);
  if(length<sizeof(struct rt_msghdr2) || length>size-offset ||
     data[offset+2]!=RTM_VERSION || data[offset+3]!=RTM_GET2){error=1;break;}
  struct rt_msghdr2 h;memcpy(&h,data+offset,sizeof(h));
  char text[RTAX_MAX][INET_ADDRSTRLEN+16];memset(text,0,sizeof(text));
  size_t position=sizeof(h);
  for(unsigned n=0;n<RTAX_MAX;++n)if(h.rtm_addrs&(1u<<n)){
   if(position>length || length-position<2){error=1;break;}
   unsigned char *sa=data+offset+position;size_t bytes=sa[0];
   size_t rounded=bytes?(bytes+3)&~(size_t)3:4;
   if(bytes>length-position || rounded>length-position){error=1;break;}
   if((sa[1]==AF_INET && bytes>=offsetof(struct sockaddr_in,sin_addr)+sizeof(struct in_addr)) ||
      (n==RTAX_NETMASK && sa[1]==AF_UNSPEC && bytes<=sizeof(struct sockaddr_in))){
    struct sockaddr_in address={0};size_t copy=bytes<sizeof(address)?bytes:sizeof(address);
    memcpy(&address,sa,copy);inet_ntop(AF_INET,&address.sin_addr,text[n],sizeof(text[n]));
   }else if(sa[1]==AF_LINK && bytes>=offsetof(struct sockaddr_dl,sdl_type)){
    uint16_t link=0;memcpy(&link,sa+offsetof(struct sockaddr_dl,sdl_index),sizeof(link));
    snprintf(text[n],sizeof(text[n]),"link#%u",link); // Never print link-layer addresses.
   }else snprintf(text[n],sizeof(text[n]),"family%u/bytes%zu",sa[1],bytes);
   position+=rounded;
  }
  if(error)break;
  printf("ROUTE index=%u flags=0x%x dst=%s gateway=%s mask=%s\n",h.rtm_index,h.rtm_flags,
   text[RTAX_DST],text[RTAX_GATEWAY],text[RTAX_NETMASK]);
  offset+=length;
 }
 free(data);if(error){fprintf(stderr,"native route records incomplete\n");return 1;}return 0;
}
int main(int argc,char **argv)
{
 if((argc!=2 && argc!=3) || !argv[1][0] || strlen(argv[1])>=IFNAMSIZ ||
    (argc==3 && strcmp(argv[2],"--routes"))){fprintf(stderr,"usage: sf-dock-netstatus IFNAME [--routes]\n");return 2;}
 const char *name=argv[1];unsigned index=if_nametoindex(name);
 if(!index){fprintf(stderr,"interface absent: %s\n",name);return 1;}
 struct ifaddrs *all=NULL;if(getifaddrs(&all)){perror("getifaddrs");return 1;}
 unsigned seen=0,flags=0;
 for(struct ifaddrs *p=all;p;p=p->ifa_next)if(!strcmp(p->ifa_name,name)){
  ++seen;flags=p->ifa_flags;
  if(p->ifa_addr && p->ifa_addr->sa_family==AF_INET){
   char address[INET_ADDRSTRLEN],mask[INET_ADDRSTRLEN]="unknown";
   if(!inet_ntop(AF_INET,&((struct sockaddr_in *)p->ifa_addr)->sin_addr,address,sizeof(address)))continue;
   if(p->ifa_netmask)inet_ntop(AF_INET,&((struct sockaddr_in *)p->ifa_netmask)->sin_addr,mask,sizeof(mask));
   printf("inet %s netmask %s\n",address,mask);
  }
 }
 freeifaddrs(all);if(!seen){fprintf(stderr,"native interface inventory absent\n");return 1;}
 printf("INTERFACE name=%s index=%u flags=0x%x up=%u running=%u\n",name,index,flags,!!(flags&IFF_UP),!!(flags&IFF_RUNNING));
 int fd=socket(AF_INET,SOCK_DGRAM,0);
 if(fd>=0){
  struct ifreq request={0};strcpy(request.ifr_name,name);
  if(!ioctl(fd,SIOCGIFMTU,&request))printf("MTU %d\n",request.ifr_mtu);
  struct ifmediareq media={0};strcpy(media.ifm_name,name);
  if(!ioctl(fd,SIOCGIFMEDIA,&media))printf("MEDIA current=0x%x active=0x%x status=0x%x valid=%u link=%u\n",media.ifm_current,media.ifm_active,media.ifm_status,!!(media.ifm_status&IFM_AVALID),!!(media.ifm_status&IFM_ACTIVE));
  else printf("MEDIA unavailable errno=%d\n",errno);
  close(fd);
 }
 int error=counters(index);if(argc==3)error|=routes();return error?1:0;
}
