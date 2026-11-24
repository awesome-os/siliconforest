/* Compile-only ABI guard: actual pinned Darwin SDK, not host structs. */
#include <stddef.h>
#include <stdint.h>
#include <net/route.h>
#include <netinet/in.h>
#include <net/if_dl.h>
_Static_assert(sizeof(struct rt_msghdr)==92,"rt_msghdr ABI");
_Static_assert(offsetof(struct rt_msghdr,rtm_msglen)==0,"length");
_Static_assert(offsetof(struct rt_msghdr,rtm_version)==2,"version");
_Static_assert(offsetof(struct rt_msghdr,rtm_type)==3,"type");
_Static_assert(offsetof(struct rt_msghdr,rtm_index)==4,"index");
_Static_assert(offsetof(struct rt_msghdr,rtm_flags)==8,"flags");
_Static_assert(offsetof(struct rt_msghdr,rtm_addrs)==12,"addresses");
_Static_assert(offsetof(struct rt_msghdr,rtm_pid)==16,"pid");
_Static_assert(offsetof(struct rt_msghdr,rtm_seq)==20,"sequence");
_Static_assert(offsetof(struct rt_msghdr,rtm_errno)==24,"error");
_Static_assert(sizeof(struct sockaddr_in)==16,"IPv4 address");
_Static_assert(offsetof(struct sockaddr_in,sin_addr)==4,"IPv4 offset");
_Static_assert(offsetof(struct sockaddr_dl,sdl_index)==2,"link index");
_Static_assert(sizeof(struct sockaddr_dl)==20,"link address");
_Static_assert(RTM_NEWADDR==12 && RTM_DELADDR==13 && RTM_IFINFO==14 &&
               RTM_NEWMADDR==15 && RTM_DELMADDR==16 && RTM_IFINFO2==18 &&
               RTM_NEWMADDR2==19,"notification values");
_Static_assert(RTM_VERSION==5 && RTM_ADD==1 && RTM_DELETE==2 && RTM_GET==4,"message values");
_Static_assert(RTA_DST==1 && RTA_GATEWAY==2 && RTA_NETMASK==4 && RTA_IFP==16,"address bits");
_Static_assert(RTF_UP==1 && RTF_GATEWAY==2 && RTF_HOST==4,"flags");
_Static_assert(AF_INET==2 && AF_LINK==18,"address families");
const uint32_t dle_route_abi_version=1;
