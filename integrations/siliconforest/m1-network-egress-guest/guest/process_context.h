#ifndef EG_PROCESS_CONTEXT_H
#define EG_PROCESS_CONTEXT_H
#include <stddef.h>
#include <stdint.h>
struct eg_process { int32_t pid,parent; uint32_t euid; char name[17]; };
/* Exact finite process inventory: self, audited shell ancestry, init and
 * optional kernel task only. Not a general kernel network-ownership lease. */
int eg_process_context(const struct eg_process *,size_t,int32_t self,int32_t parent);
#endif
