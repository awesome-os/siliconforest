/* Host-only actual frozen reader/coordinator over synthetic immutable bytes. */
#define main frozen_facts_fixture_main_not_invoked
#include "../native_policy_snapshot/test_policy_snapshot.c"
#undef main
#include "../native_policy_reader/policy_reader_internal.h"
static int fail_reader;
static uint8_t prefix[40],dram_bytes[16];
static size_t wire_bytes;
static unsigned references;
void npp_test_identity(struct nps_expected *e,int fail)
{ wire_bytes=fixture(1,0);*e=expected;fail_reader=fail; }
static int begin(void *unused) { (void)unused;assert(!references);references=4;return NPR_OK; }
static int boot(void *unused,struct npr_boot_view *v)
{ (void)unused;*v=(struct npr_boot_view){prefix,40,1,1};return NPR_OK; }
static int view(void *unused,enum npr_source_id id,struct npr_view *v)
{
    (void)unused;assert(id<=NPR_FACTS_DATA);
    const uint8_t *p=id==NPR_FACTS_DATA?raw:dram_bytes+8*(unsigned)id;
    *v=(struct npr_view){p,id==NPR_FACTS_DATA?wire_bytes:8,2+(unsigned)id};return NPR_OK;
}
static int finish(void *unused)
{ (void)unused;assert(references==4);references=0;return fail_reader?NPR_CLEANUP:NPR_OK; }
int npr_read_native(const struct nps_expected *e,struct npr_workspace *w,struct nps_result *r)
{
    assert(!memcmp(e,&expected,sizeof *e));
    uint64_t low=MCP_DRAM_BASE;
    span(336,low,0x1000000);span(368,low,0x1000000);span(352,low,0x140000);
    span(1280+6*32+16,low,0x140000);
    put16(prefix,2);put16(prefix+2,2);
    put64(prefix+16,low);put64(prefix+24,0x1000000);put64(prefix+32,low+0x123456);
    put64(dram_bytes,MCP_DRAM_BASE);put64(dram_bytes+8,UINT64_C(0x400000000));
    struct npr_ops ops={NULL,begin,boot,view,finish};
    int rc=npr_read_with_ops(&ops,e,w,r);
    assert(!references);
    return rc;
}
