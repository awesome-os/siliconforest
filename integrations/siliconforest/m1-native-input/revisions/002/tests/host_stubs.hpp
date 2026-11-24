// Host-only collaborators for extracted production source. No kernel ABI claim.
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <sys/types.h>
#include <cerrno>

using UInt64=uint64_t; using UInt8=uint8_t; using UInt16=uint16_t; using UInt32=uint32_t;
using SInt8=int8_t; using AbsoluteTime=uint64_t; using user_ssize_t=ssize_t;
struct proc {};
struct uio { char *buffer; size_t remaining; };
static ssize_t uio_resid(uio *u) { return (ssize_t)u->remaining; }
static int uiomove(const char *p,int n,uio *u) {
    assert(n>=0 && (size_t)n<=u->remaining); memcpy(u->buffer,p,(size_t)n);
    u->buffer+=n; u->remaining-=(size_t)n; return 0;
}
struct IOLock { std::mutex m; };
static std::atomic<int> failLock{0}, failCall{0}, failMajor{0}, failNode{0};
static std::atomic<int> lockAlloc{0},lockFree{0},callAlloc{0},callFree{0};
static bool consume(std::atomic<int>& n) {
    int v=n.load(); while(v>0) { if(n.compare_exchange_weak(v,v-1)) return true; }
    return false;
}
static IOLock *IOLockAlloc() {
    if(consume(failLock)) return nullptr; ++lockAlloc; return new IOLock;
}
static void IOLockFree(IOLock *p) { ++lockFree; delete p; }
static void IOLockLock(IOLock *p) { assert(p); p->m.lock(); }
static void IOLockUnlock(IOLock *p) { assert(p); p->m.unlock(); }
static bool OSCompareAndSwapPtr(void *old,void *value,void *volatile *slot) {
    return __atomic_compare_exchange_n(slot,&old,value,false,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);
}
static UInt32 OSBitOrAtomic(UInt32 bits,volatile UInt32 *slot) {
    return __atomic_fetch_or(slot,bits,__ATOMIC_SEQ_CST);
}
using thread_call_param_t=void *;
using thread_call_func_t=void (*)(void *,void *);
constexpr unsigned THREAD_CALL_PRIORITY_KERNEL=1, THREAD_CALL_OPTIONS_ONCE=1;
struct Call {
    thread_call_func_t fn; void *arg; unsigned options;
    bool pending=false,delayed=false,running=false; AbsoluteTime deadline=0;
};
using thread_call_t=Call *;
static std::mutex schedulerLock;
static std::vector<Call *> calls;
static std::atomic<int> delayedSubmissions{0}, concurrentPublish{0},maxConcurrentPublish{0};
static thread_call_t thread_call_allocate_with_options(thread_call_func_t fn,void *arg,unsigned,unsigned options) {
    if(consume(failCall)) return nullptr;
    auto *p=new Call{fn,arg,options}; ++callAlloc;
    std::lock_guard<std::mutex> lock(schedulerLock); calls.push_back(p); return p;
}
static thread_call_t thread_call_allocate(thread_call_func_t fn,void *arg) {
    return thread_call_allocate_with_options(fn,arg,0,0);
}
static bool thread_call_free(thread_call_t p) {
    std::lock_guard<std::mutex> lock(schedulerLock);
    assert(!p->pending && !p->running);
    calls.erase(std::find(calls.begin(),calls.end(),p)); ++callFree; delete p; return true;
}
static std::atomic<uint64_t> nowNS{0};
static std::function<void()> beforeArmHook,afterArmHook;
static bool enter(thread_call_t p,bool delayed,AbsoluteTime deadline=0) {
    assert(p); std::lock_guard<std::mutex> lock(schedulerLock);
    bool previous=p->pending; p->pending=true; p->delayed=delayed; p->deadline=deadline; return previous;
}
static bool thread_call_enter(thread_call_t p) { return enter(p,false); }
static bool thread_call_enter_delayed(thread_call_t p,AbsoluteTime deadline) {
    if(beforeArmHook) { auto h=std::move(beforeArmHook); beforeArmHook=nullptr; h(); }
    ++delayedSubmissions; bool result=enter(p,true,deadline);
    if(afterArmHook) { auto h=std::move(afterArmHook); afterArmHook=nullptr; h(); }
    return result;
}
constexpr uint32_t kSecondScale=1000000000;
static void clock_interval_to_deadline(uint32_t interval,uint32_t scale,AbsoluteTime *d) {
    assert(scale==kSecondScale); *d=nowNS.load()+(uint64_t)interval*scale;
}
static void clock_get_uptime(AbsoluteTime *p) { *p=nowNS.load(); }
static void absolutetime_to_nanoseconds(AbsoluteTime a,uint64_t *p) { *p=a; }
static void advanceMS(uint64_t ms) { nowNS.fetch_add(ms*1000000); }
// Model the pinned ONCE contract, including a re-pend while executing. Multiple
// host runners deliberately try to execute pending callbacks concurrently.
static bool runOne(bool allowDelayed=false) {
    Call *chosen=nullptr;
    {
        std::lock_guard<std::mutex> lock(schedulerLock);
        if(allowDelayed) {
            uint64_t earliest=UINT64_MAX;
            for(auto *c:calls) if(c->pending && !c->running)
                earliest=std::min(earliest,c->delayed?c->deadline:nowNS.load());
            if(earliest!=UINT64_MAX && earliest>nowNS.load()) nowNS=earliest;
        }
        for(auto *c:calls) {
            if(c->pending && (!c->delayed || c->deadline<=nowNS.load()) &&
               (!(c->options&THREAD_CALL_OPTIONS_ONCE) || !c->running)) {
                chosen=c; c->pending=false; c->running=true; break;
            }
        }
    }
    if(!chosen) return false;
    if(chosen->options&THREAD_CALL_OPTIONS_ONCE) {
        int n=++concurrentPublish, old=maxConcurrentPublish.load();
        while(old<n && !maxConcurrentPublish.compare_exchange_weak(old,n)) {}
    }
    chosen->fn(chosen->arg,nullptr);
    if(chosen->options&THREAD_CALL_OPTIONS_ONCE) --concurrentPublish;
    { std::lock_guard<std::mutex> lock(schedulerLock); chosen->running=false; }
    return true;
}
static void drain() { for(int n=0;runOne();++n) assert(n<1000); }
struct cdevsw { template<typename... T> cdevsw(T...) {} };
static int eno_rdwrt() { return ENXIO; }
static int eno_ioctl() { return ENOTTY; }
static int eno_stop() { return ENXIO; }
static int eno_reset() { return ENXIO; }
static int eno_select() { return ENXIO; }
static int eno_mmap() { return ENXIO; }
static int eno_strat() { return ENXIO; }
static int eno_getc() { return ENXIO; }
static int eno_putc() { return ENXIO; }
static std::mutex devfsLock;
static std::atomic<bool> devfsReady{true};
static std::map<cdevsw *,int> majors;
static std::map<std::string,dev_t> nodes;
static int majorAttempts=0,nodeAttempts=0,duplicateMajors=0,duplicateNodes=0;
static std::function<void()> majorHook,nodeHook;
static int cdevsw_add(int requested,cdevsw *ops) {
    assert(requested==-1);
    if(majorHook) { auto hook=std::move(majorHook); majorHook=nullptr; hook(); }
    std::lock_guard<std::mutex> lock(devfsLock); ++majorAttempts;
    if(consume(failMajor)) return -1;
    if(majors.count(ops)) ++duplicateMajors;
    int n=100+(int)majorAttempts; majors[ops]=n; return n;
}
extern "C" int devfs_is_ready() { return devfsReady.load(); }
constexpr int DEVFS_CHAR=1;
static dev_t makedev(int major,int minor) { return (dev_t)(major*256+minor); }
static void *devfs_make_node(dev_t dev,int type,int uid,int gid,int mode,const char *fmt,...) {
    assert(type==DEVFS_CHAR && uid==0 && gid==0 && mode==0666);
    char name[80]; va_list ap; va_start(ap,fmt); vsnprintf(name,sizeof(name),fmt,ap); va_end(ap);
    if(nodeHook) { auto hook=std::move(nodeHook); nodeHook=nullptr; hook(); }
    std::lock_guard<std::mutex> lock(devfsLock); ++nodeAttempts;
    if(consume(failNode)) return nullptr;
    if(nodes.count(name)) ++duplicateNodes;
    nodes[name]=dev; return &nodes[name];
}
static std::vector<std::string> logs;
static std::mutex logLock;
static void IOLog(const char *fmt,...) {
    char b[1024]; va_list ap; va_start(ap,fmt); vsnprintf(b,sizeof(b),fmt,ap); va_end(ap);
    std::lock_guard<std::mutex> lock(logLock); logs.emplace_back(b);
}
static std::atomic<int> consoleBytes{0};
extern "C" void cons_cinput(char) { ++consoleBytes; }
