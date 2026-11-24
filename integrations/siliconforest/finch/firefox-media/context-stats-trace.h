/* SPDX-License-Identifier: MIT */
/* Exact additive ABI1 diagnostics from m1-avd-context-stats/context-stats.patch.
 * Included in libsfavd's own TU after its private device declaration. Metadata
 * only, before DESTROY. No claim about post-destroy mappings or physical RAM. */
#ifndef SF_CONTEXT_STATS_TRACE_H
#define SF_CONTEXT_STATS_TRACE_H
#include <stdint.h>
#include <sys/ioctl.h>
struct sf_sfavd_context_stats {
    uint32_t size,version,ctx,flags;
    uint64_t generation,jobs,done,errors,timeouts;
    uint64_t last_decode_us,total_decode_us,max_decode_us;
    uint64_t capture_backing_bytes,capture_iova_bytes,capture_prepared_bytes;
    uint32_t capture_buffers,reserved;
    uint64_t api_decode_calls,api_decode_errors;
};
#define SF_CONTEXT_STATS_IOCTL _IOWR('V',7,struct sf_sfavd_context_stats)
_Static_assert(sizeof(struct sf_sfavd_context_stats)==128,"context stats v1 layout");
_Static_assert(SF_CONTEXT_STATS_IOCTL==0xc0805607u,"additive Darwin ABI1 opcode");

static void sf_context_init(sfavd_dev *dev)
{
    const char *enable=getenv("SF_SFAVD_CONTEXT_STATS");
    if(!enable||strcmp(enable,"1"))return;
    int saved=errno;struct sfavd_info info={0};
    if(dev->fd>=0&&ioctl(dev->fd,SFAVD_IOC_INFO,&info)==0&&info.abi==SFAVD_ABI_VERSION&&
       (info.caps&(1u<<17)))dev->sf_trace=1;
    else fprintf(stderr,"SF_AVD_CONTEXT pid=%d phase=open unavailable=1 no-zero-counter-claim=1\n",getpid());
    errno=saved;
}

static void sf_context_snapshot(sfavd_dev *dev,uint32_t ctx,const char *phase,int discover)
{
    if(!dev->sf_trace)return;
    int saved=errno;
    if(ctx>=16){fprintf(stderr,"SF_AVD_CONTEXT pid=%d ctx=%u phase=%s untracked=1\n",getpid(),ctx,phase);goto out;}
    uint64_t token=__atomic_load_n(&dev->sf_generation[ctx],__ATOMIC_ACQUIRE);
    if(!discover&&!token){fprintf(stderr,"SF_AVD_CONTEXT pid=%d ctx=%u phase=%s undiscovered=1\n",getpid(),ctx,phase);goto out;}
    struct sf_sfavd_context_stats r={.size=sizeof(r),.version=1,.ctx=ctx,.generation=discover?0:token};
    if(ioctl(dev->fd,SF_CONTEXT_STATS_IOCTL,&r)){
        fprintf(stderr,"SF_AVD_CONTEXT pid=%d ctx=%u phase=%s query-error=%d no-zero-counter-claim=1\n",getpid(),ctx,phase,errno);goto out;
    }
    if(r.size!=sizeof(r)||r.version!=1||r.ctx!=ctx||r.reserved||!r.generation||
       (!discover&&r.generation!=token)||(r.flags&3)!=3||r.capture_buffers>32||
       r.capture_iova_bytes>r.capture_backing_bytes||r.capture_prepared_bytes>r.capture_backing_bytes){
        fprintf(stderr,"SF_AVD_CONTEXT pid=%d ctx=%u phase=%s invalid-snapshot=1\n",getpid(),ctx,phase);goto out;
    }
    if(discover)__atomic_store_n(&dev->sf_generation[ctx],r.generation,__ATOMIC_RELEASE);
    fprintf(stderr,"SF_AVD_CONTEXT pid=%d ctx=%u generation=%llu phase=%s flags=%u jobs=%llu done=%llu errors=%llu timeouts=%llu api-calls=%llu api-errors=%llu last-us=%llu total-us=%llu max-us=%llu capture-buffers=%u capture-backing-bytes=%llu capture-iova-bytes=%llu capture-prepared-bytes=%llu scope=live-owned-capture-only\n",
        getpid(),ctx,(unsigned long long)r.generation,phase,r.flags,
        (unsigned long long)r.jobs,(unsigned long long)r.done,(unsigned long long)r.errors,(unsigned long long)r.timeouts,
        (unsigned long long)r.api_decode_calls,(unsigned long long)r.api_decode_errors,
        (unsigned long long)r.last_decode_us,(unsigned long long)r.total_decode_us,(unsigned long long)r.max_decode_us,
        r.capture_buffers,(unsigned long long)r.capture_backing_bytes,(unsigned long long)r.capture_iova_bytes,
        (unsigned long long)r.capture_prepared_bytes);
out:
    errno=saved;
}
#endif
