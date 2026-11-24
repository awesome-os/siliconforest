#include <stddef.h>
#include <net/route.h>
#include <netinet/in.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include "default_route.h"
_Static_assert(sizeof(struct rt_msghdr)==92,"routing header");
_Static_assert(offsetof(struct rt_msghdr,rtm_index)==4,"index offset");
_Static_assert(offsetof(struct rt_msghdr,rtm_flags)==8,"flags offset");
_Static_assert(offsetof(struct rt_msghdr,rtm_addrs)==12,"bitmap offset");
_Static_assert(offsetof(struct rt_msghdr,rtm_pid)==16,"pid offset");
_Static_assert(offsetof(struct rt_msghdr,rtm_seq)==20,"seq offset");
_Static_assert(offsetof(struct rt_msghdr,rtm_errno)==24,"error offset");
_Static_assert(sizeof(struct sockaddr_in)==16,"IPv4 sockaddr");
_Static_assert(offsetof(struct sockaddr_dl,sdl_index)==2,"link index");
_Static_assert(offsetof(struct sockaddr_dl,sdl_data)==8,"link name");
_Static_assert(RTM_VERSION==5&&RTM_ADD==1&&RTM_GET==4,"opcodes");
_Static_assert(RTA_IFA==32&&RTA_IFP==16&&RTA_NETMASK==4,"addresses");
_Static_assert((RTF_UP|RTF_GATEWAY|RTF_STATIC|RTF_DONE)==0x843,"ADD ack flags");
_Static_assert((RTF_UP|RTF_GATEWAY|RTF_STATIC|RTF_DONE|RTF_PRCLONING|RTF_GLOBAL)==DLF_REQUIRED_FLAGS,"native NECP default flags");
_Static_assert(IFT_OTHER==1&&NET_RT_DUMP==1,"utun type and legacy route dump");
