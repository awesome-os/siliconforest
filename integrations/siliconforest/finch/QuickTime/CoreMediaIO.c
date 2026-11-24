/* SPDX-License-Identifier: MIT */
/* The documented seven-argument data getter, with no capture-object backend.
 * Report unimplemented; never claim device inventory or write fabricated data. */
#include <stdint.h>
#include <stdio.h>
typedef struct {uint32_t selector,scope,element;} SFCMIOPropertyAddress;
int32_t CMIOObjectGetPropertyData(uint32_t objectID,const SFCMIOPropertyAddress *address,
 uint32_t qualifierSize,const void *qualifier,uint32_t dataSize,uint32_t *used,void *data){
 (void)objectID;(void)address;(void)qualifierSize;(void)qualifier;(void)dataSize;(void)data;
 if(used)*used=0;
 static int announced;
 if(!__atomic_exchange_n(&announced,1,__ATOMIC_RELAXED))fputs("CoreMediaIO: capture properties unavailable (no backend)\n",stderr);
 return -4; /* unimplemented OSStatus, not an empty successful device list */
}
