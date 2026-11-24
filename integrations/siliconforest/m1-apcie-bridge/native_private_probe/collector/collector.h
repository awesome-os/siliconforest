/* Original read-only byte collector. */
#ifndef BCM_PRIVATE_COLLECTOR_H
#define BCM_PRIVATE_COLLECTOR_H
#include "../protocol.h"
#include <stdio.h>
#define BCPC_CAP 4096u
enum bcpc_get { BCPC_OK=0,BCPC_MISSING=1,BCPC_RPC=2 };
typedef int (*bcpc_getter)(void *,uint8_t *,size_t,size_t *,int32_t *,int32_t *);
int bcpc_run(int,char **,bcpc_getter,void *,FILE *);
#endif
