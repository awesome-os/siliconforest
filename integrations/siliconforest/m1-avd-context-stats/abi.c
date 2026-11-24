/* SPDX-License-Identifier: MIT */
#include <stddef.h>
#include <stdio.h>
#include <sfavd/sfavd_ioctl.h>
_Static_assert(sizeof(struct sfavd_info)==64,"old info");
_Static_assert(sizeof(struct sfavd_create)==64,"old create");
_Static_assert(sizeof(struct sfavd_map)==24,"old map");
_Static_assert(sizeof(struct sfavd_decode)==176,"old decode");
_Static_assert(sizeof(struct sfavd_destroy)==4,"old destroy");
_Static_assert(sizeof(struct sfavd_import)==528,"old import");
#ifdef __APPLE__
_Static_assert(SFAVD_IOC_INFO==0x40405601UL,"old Darwin info");
_Static_assert(SFAVD_IOC_CREATE==0xc0405602UL,"old Darwin create");
_Static_assert(SFAVD_IOC_MAP==0xc0185603UL,"old Darwin map");
_Static_assert(SFAVD_IOC_DECODE==0xc0b05604UL,"old Darwin decode");
_Static_assert(SFAVD_IOC_DESTROY==0x80045605UL,"old Darwin destroy");
_Static_assert(SFAVD_IOC_IMPORT==0x82105606UL,"old Darwin import");
#endif
#ifdef SFAVD_CAP_CONTEXT_STATS
_Static_assert(sizeof(struct sfavd_context_stats)==128,"context stats ABI");
_Static_assert(offsetof(struct sfavd_context_stats,generation)==16,"generation");
_Static_assert(offsetof(struct sfavd_context_stats,jobs)==24,"jobs");
#ifdef __APPLE__
_Static_assert(SFAVD_IOC_CONTEXT_STATS==0xc0805607UL,"Darwin ioctl");
#endif
#endif
int main(void){printf("%zu %zu %zu %zu %zu %zu %lu %lu %lu %lu %lu %lu\n",sizeof(struct sfavd_info),sizeof(struct sfavd_create),sizeof(struct sfavd_map),sizeof(struct sfavd_decode),sizeof(struct sfavd_destroy),sizeof(struct sfavd_import),(unsigned long)SFAVD_IOC_INFO,(unsigned long)SFAVD_IOC_CREATE,(unsigned long)SFAVD_IOC_MAP,(unsigned long)SFAVD_IOC_DECODE,(unsigned long)SFAVD_IOC_DESTROY,(unsigned long)SFAVD_IOC_IMPORT);return 0;}
