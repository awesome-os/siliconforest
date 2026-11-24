/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define TRUE 1
#define FALSE 0
#define HAS_MTE 0
#define __assert_only
#define MACRO_BEGIN do {
#define MACRO_END } while(0)
#define VM_PURGABLE_NONVOLATILE 0
#define VM_PURGABLE_DENY 3
#define VM_DYNAMIC_PAGING_ENABLED() 1
#define VM_PURGABLE_VOLATILE 1
#define VM_PURGABLE_EMPTY 2
#define UPL_IO_WIRE 1
#define UPL_COMMIT_FREE_ABSENT 2
#define UPL_COMMIT_INACTIVATE 4
#define VM_PAGE_NOT_ON_Q 0
#define VM_PAGE_IS_WIRED 1
#define VM_PAGE_ON_THROTTLED_Q 2
#define VM_PAGE_ON_ACTIVE_Q 3
#define VM_PAGE_ON_INACTIVE_INTERNAL_Q 4
#define VM_PAGE_ON_INACTIVE_EXTERNAL_Q 5
#define VM_PAGE_USED_BY_COMPRESSOR 6
#define VM_PAGE_ON_PAGEOUT_Q 7
#define DW_clear_busy 1
#define DW_PAGE_WAKEUP 2
#define DW_vm_page_deactivate_internal 4
#define DW_vm_page_free 8
#define DW_vm_page_activate 16
#define DW_vm_page_unwire 32
#define VM_MEM_REFERENCED 1
#define VM_KERN_MEMORY_NONE 0
#define os_add_overflow(a,b,c) __builtin_add_overflow((a),(b),(c))
typedef int boolean_t;
typedef int ledger_t;
typedef int vmo_ledgers_t;
struct object;
struct page {struct object *object;bool canonical,vmp_absent,vmp_busy,vmp_gobbled,vmp_laundry;unsigned vmp_wire_count;int vmp_q_state;struct {uintptr_t next,prev;}vmp_pageq;};
typedef struct page *vm_page_t;
struct queue {vm_page_t first;};
struct object {bool private,internal;int purgable,wire_tag;uint32_t wired_page_count;uint64_t vo_size;struct queue memq;};
typedef struct object *vm_object_t;
#define VM_PAGE_NULL NULL
#define VM_OBJECT_NULL NULL
#define VM_PAGE_CONVERT_TO_QUEUE_ENTRY(p) ((uintptr_t)(p))
#define VM_PAGE_OBJECT(p) ((p)->object)
#define VM_PAGE_WIRED(p) ((p)->vmp_wire_count!=0)
#define VM_PAGE_CHECK(p) ((void)(p))
#define LCK_MTX_ASSERT(a,b) ((void)0)
#define vm_object_lock_assert_exclusive(o) ((void)(o))
static bool vm_page_is_canonical(vm_page_t p){return p->canonical;}
static bool vm_page_queue_empty(struct queue *q){return q->first==NULL;}
static vm_page_t vm_page_queue_first(struct queue *q){return q->first;}
static void panic(const char *fmt,...){(void)fmt;abort();}
static int64_t vm_page_wire_count;
static void apply_delta(vm_object_t object,int64_t __wireddelta){@CHECK_DELTA@}
#define VM_OBJECT_WIRED_PAGE_UPDATE_START(o) {int64_t __wireddelta=0;
#define VM_OBJECT_WIRED_PAGE_UPDATE_END(o,t) apply_delta((o),__wireddelta);}
@ACCOUNT_MACROS@
static void slow_unwire(vm_page_t mem,boolean_t queueit){@UNWIRE@}
static void active(vm_page_t m){@ACTIVATE@ m->vmp_q_state=VM_PAGE_ON_ACTIVE_Q;}
static void inactive(vm_page_t m){@DEACTIVATE@ m->vmp_q_state=VM_PAGE_ON_INACTIVE_EXTERNAL_Q;}
static int run(unsigned count,bool priv,int flags,int vector,int purgable,int wires,bool mixed){
 struct page *pages=calloc(count?count:1,sizeof(*pages));assert(pages);
 struct object o={.private=priv,.purgable=purgable,.vo_size=(uint64_t)count*16384};vm_object_t shadow_object=&o,m_object=&o;
 for(unsigned i=0;i<count;i++){pages[i].object=&o;pages[i].canonical=!priv && (!mixed || !(i&1));pages[i].vmp_wire_count=pages[i].canonical?(unsigned)wires:(mixed?2u:(unsigned)wires);pages[i].vmp_q_state=VM_PAGE_IS_WIRED;if(pages[i].canonical)o.wired_page_count++;}
 o.memq.first=count?pages:NULL;vm_page_wire_count=10000+o.wired_page_count;
 struct upl_type{int flags;}u={UPL_IO_WIRE};struct upl_type *upl=&u;int isVectorUPL=vector;uint64_t size=o.vo_size;
 int fast_path_possible=0,fast_path_full_commit=0,throttle_page=0,unwired_count=0,local_queue_count=0;vm_page_t nxt_page=NULL,first_local=NULL,last_local=NULL;
 @CHOICE@
 for(unsigned i=0;i<count;i++){vm_page_t m=&pages[i];struct {int dw_mask;}dw={0},*dwp=&dw;int clear_refmod=0;
 @PER_PAGE@
 if(dwp->dw_mask&DW_vm_page_unwire){slow_unwire(m,!(dwp->dw_mask&DW_vm_page_deactivate_internal));if(m->vmp_wire_count==0){if(dwp->dw_mask&DW_vm_page_deactivate_internal)inactive(m);else active(m);}}
 }
 if(fast_path_possible){int64_t __wireddelta=0;@GLOBAL_LINE@ @COUNT_LINE@ apply_delta(shadow_object,__wireddelta);}
 unsigned remaining_canonical=0;
 for(unsigned i=0;i<count;i++){struct page *p=&pages[i];assert(p->vmp_wire_count==(p->canonical?(unsigned)wires-1:(mixed?1u:(unsigned)wires-1)));if(p->canonical&&p->vmp_wire_count)remaining_canonical++;if(!p->canonical)assert(p->vmp_q_state==VM_PAGE_NOT_ON_Q||p->vmp_q_state==VM_PAGE_IS_WIRED);}
 assert(o.wired_page_count==remaining_canonical);assert(vm_page_wire_count==10000+remaining_canonical);
 if(priv)assert(!fast_path_possible && local_queue_count==0);
 if(!priv&&!vector&&purgable==0&&!(flags&UPL_COMMIT_FREE_ABSENT)&&count)assert(fast_path_possible);
 free(pages);return 0;
}
int main(int argc,char **argv){
 if(argc>1){(void)argv;return run(426,true,0,0,0,1,false);}
 for(int flags=0;flags<8;flags+=2)for(int vector=0;vector<2;vector++)for(int purge=0;purge<3;purge++)for(int wires=1;wires<=3;wires++){run(426,true,flags,vector,purge,wires,false);run(426,false,flags,vector,purge,wires,false);run(426,false,flags,vector,purge,wires,true);}
 run(0,true,0,0,0,1,false);run(0,false,0,0,0,1,false);
 puts("PATCHED actual UPL condition/branches/macros: private426, canonical/mixed nonprivate, wire1/2/3, INACTIVATE/FREE_ABSENT, vector/purgable and empty guards PASS");return 0;
}
