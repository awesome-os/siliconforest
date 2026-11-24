/* SPDX-License-Identifier: MIT */
#include <sys/random.h>
#include <stdlib.h>
#include <errno.h>
static inline void get_random_bytes(void *p,size_t n) { while(n){ssize_t got=getrandom(p,n,0);if(got<0&&errno==EINTR)continue;if(got<=0)abort();p=(char *)p+got;n-=(size_t)got;} }
