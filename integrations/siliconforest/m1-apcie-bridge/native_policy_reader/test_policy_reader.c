/* Trusted host-only fake-adapter tests. Reuse unchanged synthetic wire fixture. */
#define main frozen_facts_fixture_tests_not_called
#include "../native_policy_snapshot/test_policy_snapshot.c"
#undef main
#include "policy_reader_internal.h"
static struct npr_workspace reader_workspace;
static unsigned reader_checks;
struct fake {
    uint8_t prefix[40],prefix_other[40],dram_base[8],dram_size[8];
    size_t length,boot_bytes,source_bytes[3];
    uint32_t initialized;
    unsigned calls,fail_at,boot_calls,view_calls[3],refs,acquired,released,finishes;
    int finish_error,mutation,alias,begin_status,status;
    unsigned null_source;
};
static int step(struct fake *f) { return ++f->calls==f->fail_at?NPR_SOURCE:NPR_OK; }
static int fake_begin(void *opaque)
{
    struct fake *f=opaque;
    for(unsigned i=0;i<4;i++) {
        int error=step(f);if(error)return error;
        f->refs++;f->acquired++;
    }
    if(f->begin_status)return f->begin_status;
    if(f->mutation==20)expected.sha256[0][0]^=1;
    return NPR_OK;
}
static int fake_boot(void *opaque,struct npr_boot_view *view)
{
    struct fake *f=opaque;int error=step(f);if(error)return error;
    f->boot_calls++;
    if(f->boot_calls==2 && f->mutation==1)f->prefix[32]^=1;
    *view=(struct npr_boot_view){f->prefix,f->boot_bytes,123,f->initialized};
    if(f->boot_calls==2 && f->mutation==2)view->bytes=f->prefix_other;
    if(f->boot_calls==2 && f->mutation==3)view->object++;
    if(f->boot_calls==2 && f->mutation==4)view->count--;
    if(f->boot_calls==2 && f->mutation==5)view->initialized=0;
    if(f->alias==10)view->bytes=(const uint8_t *)&reader_workspace;
    return NPR_OK;
}
static int fake_view(void *opaque,enum npr_source_id source,struct npr_view *view)
{
    struct fake *f=opaque;int error=step(f);if(error)return error;
    f->view_calls[source]++;
    const uint8_t *p=source==NPR_DRAM_BASE?f->dram_base:source==NPR_DRAM_SIZE?f->dram_size:raw;
    *view=(struct npr_view){source==f->null_source?0:p,f->source_bytes[source],100+(unsigned)source};
    if(f->view_calls[source]==2) {
        if(f->mutation==6)view->object++;
        if(f->mutation==7)view->count++;
        if(f->mutation==8)view->bytes=0;
        if(f->mutation==9 && source==NPR_FACTS_DATA)raw[496+8]^=1;
    }
    if(source==NPR_FACTS_DATA) {
        if(f->alias==1)view->bytes=(const uint8_t *)&reader_workspace;
        if(f->alias==2)view->bytes=(const uint8_t *)&out;
        if(f->alias==3)view->bytes=(const uint8_t *)&expected;
        if(f->alias==4)view->bytes=f->prefix;
    }
    if(source==NPR_DRAM_SIZE && f->alias==5)view->bytes=f->dram_base;
    return NPR_OK;
}
static int fake_finish(void *opaque)
{
    struct fake *f=opaque;f->finishes++;
    while(f->refs) { f->refs--;f->released++; }
    return f->finish_error?NPR_CLEANUP:NPR_OK;
}
static struct fake prepare(void)
{
    struct fake f={0};f.length=fixture(1,0);f.boot_bytes=40;f.initialized=1;f.null_source=99;
    f.source_bytes[0]=f.source_bytes[1]=8;f.source_bytes[2]=f.length;
    uint64_t low=MCP_DRAM_BASE+0x20000000;
    span(352,low,0x140000);span(1280+6*32+16,low,0x140000);
    put16(f.prefix,2);put16(f.prefix+2,2);put64(f.prefix+16,low);put64(f.prefix+24,0x10000000);put64(f.prefix+32,low+0x123456);
    put64(f.dram_base,MCP_DRAM_BASE);put64(f.dram_size,UINT64_C(0x400000000));
    memcpy(f.prefix_other,f.prefix,40);return f;
}
static void run(struct fake *f,int ok)
{
    struct npr_ops ops={f,fake_begin,fake_boot,fake_view,fake_finish};
    memset(&out,0xa5,sizeof out);before=out;
    int status=npr_read_with_ops(&ops,&expected,&reader_workspace,&out);f->status=status;reader_checks++;
    assert(ok?status==NPR_OK:status!=NPR_OK);
    assert(f->finishes==1 && !f->refs && f->acquired==f->released);
    if(!ok)assert(!memcmp(&out,&before,sizeof out));
    else {
        assert(out.retained_boot.base==out.envelope.base);
        assert(out.retained_boot.bytes==0x140000 && out.record_count==7);
    }
}
int main(void)
{
    struct fake f=prepare();run(&f,1);assert(f.calls==12 && f.boot_calls==2);
    for(unsigned fail=1;fail<=12;fail++) { f=prepare();f.fail_at=fail;run(&f,0); }
    f=prepare();f.finish_error=1;run(&f,0);
    for(int mutation=1;mutation<=9;mutation++) { f=prepare();f.mutation=mutation;run(&f,0); }
    f=prepare();f.mutation=20;run(&f,0);
    f=prepare();f.initialized=0;run(&f,0);
    for(unsigned size=0;size<40;size++) { f=prepare();f.boot_bytes=size;run(&f,0); }
    f=prepare();f.boot_bytes=41;run(&f,0);
    for(unsigned source=0;source<3;source++)for(unsigned size=0;size<9;size++) {
        if(source<2 && size==8)continue;
        f=prepare();f.source_bytes[source]=size;run(&f,0);
    }
    for(unsigned size=0;size<2;size++) { f=prepare();f.source_bytes[2]=size?NPS_WIRE_MAX+1:80;run(&f,0); }
    for(int alias=1;alias<=5;alias++) { f=prepare();f.alias=alias;run(&f,0);assert(f.status==NPR_FORMAT); }
    f=prepare();f.alias=10;run(&f,0);assert(f.status==NPR_FORMAT);
    for(unsigned source=0;source<3;source++) { f=prepare();f.null_source=source;run(&f,0);assert(f.status==NPR_FORMAT); }
    f=prepare();f.begin_status=NPR_NOT_READY;run(&f,0);assert(f.status==NPR_NOT_READY);
    f=prepare();f.begin_status=NPR_FORMAT;run(&f,0);assert(f.status==NPR_FORMAT);
    f=prepare();f.prefix[0]=3;run(&f,0);
    f=prepare();f.prefix[2]=3;run(&f,0);
    /* Legacy736 and native1152 share these40bytes: no layout/authentication claim. */
    f=prepare();uint8_t legacy736[736]={0};memcpy(legacy736,f.prefix,40);
    memset(legacy736+40,0x5a,sizeof legacy736-40);memcpy(f.prefix,legacy736,40);run(&f,1);
    f=prepare();put64(f.dram_base,MCP_DRAM_BASE+0x1000);run(&f,0);
    f=prepare();put64(f.dram_size,UINT64_C(0x400001000));run(&f,0);
    f=prepare();put64(f.prefix+16,MCP_DRAM_BASE+0x20004000);run(&f,0);
    f=prepare();put64(f.prefix+24,0x10004000);run(&f,0);
    f=prepare();put64(f.prefix+16,MCP_DRAM_BASE+0x20004000);
    span(352,MCP_DRAM_BASE+0x20004000,0x140000);span(1280+6*32+16,MCP_DRAM_BASE+0x20004000,0x140000);run(&f,0);
    f=prepare();put64(f.prefix+32,MCP_DRAM_BASE+0x20000000);run(&f,0);
    f=prepare();put64(f.prefix+32,MCP_DRAM_BASE+0x20140001);run(&f,0);
    f=prepare();put64(f.prefix+32,UINT64_MAX);run(&f,0);
    f=prepare();span(352,MCP_DRAM_BASE+0x20004000,0x140000);span(1280+6*32+16,MCP_DRAM_BASE+0x20004000,0x140000);run(&f,0);
    f=prepare();raw[48]=2;run(&f,0);
    /* No public expected/pointer/object-span failure may acquire references. */
    f=prepare();struct npr_ops ops={&f,fake_begin,fake_boot,fake_view,fake_finish};
    memset(&out,0xa5,sizeof out);before=out;
    assert(npr_read_with_ops(0,&expected,&reader_workspace,&out)==NPR_ARGUMENT);
    assert(npr_read_with_ops(&ops,0,&reader_workspace,&out)==NPR_ARGUMENT);
    assert(npr_read_with_ops(&ops,&expected,0,&out)==NPR_ARGUMENT);
    assert(npr_read_with_ops(&ops,&expected,&reader_workspace,0)==NPR_ARGUMENT);
    assert(npr_read_with_ops(&ops,&expected,&reader_workspace,&reader_workspace.staged)==NPR_ARGUMENT);
    assert(npr_read_with_ops(&ops,(struct nps_expected *)(void *)&out,&reader_workspace,&out)==NPR_ARGUMENT);
    assert(npr_read_with_ops((struct npr_ops *)(void *)&reader_workspace,&expected,&reader_workspace,&out)==NPR_ARGUMENT);
    memset(expected.nonce,0,24);assert(npr_read_with_ops(&ops,&expected,&reader_workspace,&out)==NPR_ARGUMENT);
    assert(!f.calls && !f.finishes && !f.refs && !memcmp(&out,&before,sizeof out));
    printf("reader synthetic checks=%u refs=balanced failure-output=unchanged common-prefix=40 no-native-API-calls\n",reader_checks);
    return 0;
}
