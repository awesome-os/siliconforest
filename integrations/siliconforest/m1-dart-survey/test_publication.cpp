#ifdef NDEBUG
#error DLS_TEST_REQUIRES_ASSERTIONS_NDEBUG_FORBIDDEN
#endif
#include "observation_layout.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>

// These are host ownership fakes, not IOKit ABI substitutes. The tested
// publication body is extracted verbatim from PDDARTSurvey::start.
struct OSData {
    static bool failAllocation;
    static unsigned live;
    unsigned refs;
    unsigned char bytes[sizeof(dls_snapshot)];
    static OSData *withBytes(const void *p,unsigned n) {
        assert(n==sizeof(dls_snapshot));
        if (failAllocation) return nullptr;
        OSData *d=new OSData;d->refs=1;std::memcpy(d->bytes,p,n);live++;return d;
    }
    void retain() { refs++; }
    void release() { assert(refs);if (!--refs) { live--;delete this; } }
};
bool OSData::failAllocation=false;
unsigned OSData::live=0;
struct Registry {
    bool fail=false;
    OSData *data=nullptr;
    const char *key=nullptr;
    unsigned calls=0;
    bool setProperty(const char *name,OSData *value) {
        calls++;if (fail) return false;
        value->retain();if (data) data->release();data=value;key=name;return true;
    }
    ~Registry() { if (data) data->release(); }
};
struct Base : Registry {
    unsigned stopped=0,published=0;
    void stop(Registry *) { stopped++; }
    void registerService() { published++; }
};
static struct { int result;unsigned allocated,provider,instance,count; } logResult;
static void IOLog(const char *format,...)
{
    assert(std::strstr(format,"provider_preserved=%u"));
    va_list ap;va_start(ap,format);logResult.result=va_arg(ap,int);
    for (unsigned i=0;i<4;i++) (void)va_arg(ap,unsigned);
    logResult.allocated=va_arg(ap,unsigned);logResult.provider=va_arg(ap,unsigned);
    logResult.instance=va_arg(ap,unsigned);logResult.count++;va_end(ap);
}
struct Survey : Base {
    using super=Base;
    dls_snapshot snapshot{};
    bool finish(Registry *provider,int result) {
#include "publication.inc"
    }
};
static void one(int raw,bool allocationFail,bool providerFail,bool instanceFail)
{
    assert(OSData::live==0);Registry provider;provider.fail=providerFail;
    OSData::failAllocation=allocationFail;logResult={};
    dls_snapshot before{};
    before.version=1;before.result=raw;before.reads_attempted=11;before.sample_count=1;
    before.power[0]=0xff;before.power[1]=0;before.clocks[0]=15;
    before.samples[0]={0,14u<<24};before.failure_region=DLS_PMGR;before.failure_offset=0x348;
    {
        Survey s;s.fail=instanceFail;s.snapshot=before;
        bool ok=s.finish(&provider,raw);
        assert(ok==(raw==DLS_SAMPLED&&!allocationFail&&!providerFail&&!instanceFail));
        assert(s.published==(ok?1u:0u)&&s.stopped==(ok?0u:1u));
        assert(!std::memcmp(&s.snapshot,&before,sizeof(before))); // raw result untouched
        assert(logResult.count==1&&logResult.result==raw);
        assert(logResult.allocated==!allocationFail);
        assert(logResult.provider==(!allocationFail&&!providerFail));
        assert(logResult.instance==(!allocationFail&&!instanceFail));
        assert(provider.calls==(allocationFail?0u:1u));
        if (provider.data) {
            assert(!std::strcmp(provider.key,"PDDARTSurveyDiagnosticV1"));
            assert(provider.data!=reinterpret_cast<void *>(&s.snapshot));
            assert(!std::memcmp(provider.data->bytes,&before,sizeof(before)));
            std::memset(&s.snapshot,0xa5,sizeof(s.snapshot)); // retained data is a copy
            assert(!std::memcmp(provider.data->bytes,&before,sizeof(before)));
        }
        if (s.data) assert(!std::strcmp(s.key,"PDDARTSurveyObservationV1"));
    }
    // Instance has detached/freed; provider's independent ownership survives.
    assert((provider.data!=nullptr)==(!allocationFail&&!providerFail));
    if (provider.data) assert(!std::memcmp(provider.data->bytes,&before,sizeof(before)));
    assert(OSData::live==(provider.data?1u:0u));OSData::failAllocation=false;
}
int main()
{
    const int statuses[]={DLS_SAMPLED,DLS_NOT_READY,DLS_UNSUPPORTED,DLS_BOARD,DLS_IO};
    for (int raw:statuses) for (unsigned mask=0;mask<8;mask++)
        one(raw,(mask&1)!=0,(mask&2)!=0,(mask&4)!=0);
    assert(OSData::live==0);
    std::puts("HOST_ONLY extracted publication: 40 status/allocation/provider/instance cases; copy survives instance destruction");
}
