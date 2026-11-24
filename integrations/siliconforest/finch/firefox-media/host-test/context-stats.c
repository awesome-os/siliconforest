/* SPDX-License-Identifier: MIT */
/* Literal patched user library, fake ioctls on /dev/null. No target evidence. */
#define _GNU_SOURCE
#include <stdarg.h>
#define ioctl sf_test_ioctl
#include "sfavd-traced.c"
#undef ioctl
#include <assert.h>
static unsigned queried,decoded,created,destroyed,info_calls;
static int capability=1,fail_query,fail_destroy,invalid;
static uint64_t generation=23,last_input;
int sf_test_ioctl(int fd,unsigned long request,...)
{
    (void)fd;va_list ap;va_start(ap,request);void *arg=va_arg(ap,void *);va_end(ap);
    if(request==SFAVD_IOC_INFO){struct sfavd_info *r=arg;info_calls++;r->abi=1;r->caps=capability?1u<<17:0;return 0;}
    if(request==SFAVD_IOC_CREATE){((struct sfavd_create *)arg)->ctx=2;created++;return 0;}
    if(request==SFAVD_IOC_DECODE){decoded++;return 0;}
    if(request==SFAVD_IOC_DESTROY){destroyed++;if(fail_destroy){errno=EBUSY;return -1;}return 0;}
    if(request==SF_CONTEXT_STATS_IOCTL){
        struct sf_sfavd_context_stats *r=arg;queried++;last_input=r->generation;
        assert(r->size==128&&r->version==1&&r->ctx==2&&!r->reserved);
        if(fail_query||(r->generation&&r->generation!=generation)){errno=EINVAL;return -1;}
        r->generation=generation;r->flags=3;r->jobs=decoded;r->done=decoded;
        r->capture_buffers=32;r->capture_backing_bytes=32768;r->capture_iova_bytes=32768;
        if(invalid)r->capture_iova_bytes=65536;
        return 0;
    }
    assert(!"unexpected fake ioctl");return -1;
}
static sfavd_dev *make(int enabled)
{
    if(enabled)setenv("SF_SFAVD_CONTEXT_STATS","1",1);else unsetenv("SF_SFAVD_CONTEXT_STATS");
    sfavd_dev *dev=sfavd_open("/dev/null");assert(dev);return dev;
}
int main(void)
{
    struct sfavd_create c={0};struct sfavd_decode d={0};
    sfavd_dev *dev=make(0);assert(!info_calls);
    assert(!sfavd_create(dev,&c));assert(!sfavd_destroy(dev,c.ctx));assert(!queried);sfavd_close(dev);
    capability=0;dev=make(1);assert(!dev->sf_trace&&!queried);sfavd_close(dev);capability=1;
    dev=make(1);assert(dev->sf_trace);errno=EAGAIN;
    assert(!sfavd_create(dev,&c)&&dev->sf_generation[c.ctx]==generation&&last_input==0&&errno==EAGAIN);
    unsigned before=queried;
    for(unsigned i=0;i<10000;i++)assert(!sfavd_decode(dev,&d,NULL));
    assert(queried==before); /* No per-frame stats query, even when enabled. */
    fail_destroy=1;assert(sfavd_destroy(dev,c.ctx)==-1&&errno==EBUSY);
    assert(last_input==generation&&dev->sf_generation[c.ctx]==generation);
    fail_destroy=0;generation++;assert(!sfavd_destroy(dev,c.ctx)); /* Stale query fails, real destroy unchanged. */
    assert(last_input==generation-1&&!dev->sf_generation[c.ctx]);
    invalid=1;assert(!sfavd_create(dev,&c)&&!dev->sf_generation[c.ctx]);invalid=0;
    fail_query=1;assert(!sfavd_create(dev,&c)&&!dev->sf_generation[c.ctx]);fail_query=0;
    generation++;assert(!sfavd_create(dev,&c)&&dev->sf_generation[c.ctx]==generation);
    assert(!sfavd_destroy(dev,c.ctx)&&!dev->sf_generation[c.ctx]);
    assert(created==5&&destroyed==4&&decoded==10000);sfavd_close(dev);
    puts("CONTEXT_TRACE_HOST PASS default-off/no-per-frame-query/generation/query-failure/invalid-bytes/errno/destroy; FAKE ioctls only");
}
