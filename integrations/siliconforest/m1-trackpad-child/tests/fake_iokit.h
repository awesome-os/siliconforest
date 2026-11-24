/* Host-only lifetime/failure harness. No target ABI or hardware evidence. */
#pragma once
#ifdef NDEBUG
#error "T2 harness requires assertions (NDEBUG forbidden)"
#endif
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <algorithm>

using UInt32=uint32_t; using UInt64=uint64_t; using SInt32=int32_t;
using IOOptionBits=uint32_t; using IOReturn=int32_t; using task_t=void *;
using IOHIDReportType=int;
constexpr int kIOReturnSuccess=0, kIOReturnError=-1, kIOReturnNoMemory=-2,
 kIOReturnNotReady=-3, kIOReturnBadArgument=-4, kIOReturnUnsupported=-5;
constexpr int kIOHIDReportTypeInput=0, kIODirectionNone=0;
constexpr IOOptionBits kIOServiceAsynchronous=4;
constexpr int kIOTimerEventSourceOptionsPriorityWorkLoop=1;
struct IOHIDCompletion {};
struct Faults {
    static int at, step, gateDepth, closes, releases;
    static bool corruptProperty, failAllData;
    static std::vector<std::string> trace;
    static bool hit(const char *s) {
        trace.emplace_back(s); ++step;
        return step==at || (failAllData && std::string(s)=="data");
    }
    static void reset(int n=-1) {
        at=n;step=0;gateDepth=0;closes=0;releases=0;
        corruptProperty=false;failAllData=false;trace.clear();
    }
};
int Faults::at=-1; int Faults::step=0; int Faults::gateDepth=0;
int Faults::closes=0; int Faults::releases=0;
bool Faults::corruptProperty=false; bool Faults::failAllData=false;
std::vector<std::string> Faults::trace;

class OSObject {
public:
    int refs=1; bool freed=false;
    virtual ~OSObject()=default;
    virtual void free() { assert(!freed); freed=true; }
    void retain() { assert(refs>0 && !freed); ++refs; }
    void release() { assert(refs>0 && !freed); ++Faults::releases; if (!--refs) free(); }
};
class OSBoolean : public OSObject {};
static OSBoolean trueObject,falseObject;
static OSBoolean *kOSBooleanTrue=&trueObject,*kOSBooleanFalse=&falseObject;
static const char *kIOHIDRegisterServiceKey="RegisterService";
class OSDictionary : public OSObject {};
class OSNumber : public OSObject {
public:
    uint64_t value=0; unsigned bits=0;
    static OSNumber *withNumber(uint64_t n,unsigned bits) {
        if (Faults::hit("number")) return nullptr;
        auto *p=new OSNumber;p->value=n;p->bits=bits;return p;
    }
};
class OSString : public OSObject {
public:
    std::string value;
    static OSString *withCString(const char *s) {
        if (Faults::hit("string")) return nullptr;
        auto *p=new OSString;p->value=s;return p;
    }
};
class OSData : public OSObject {
public:
    std::vector<uint8_t> bytes;
    static OSData *withBytes(const void *p,unsigned n) {
        if (Faults::hit("data")) return nullptr;
        auto *v=new OSData;v->bytes.assign((const uint8_t *)p,(const uint8_t *)p+n);return v;
    }
};
class IOMemoryDescriptor : public OSObject {
public:
    std::vector<uint8_t> bytes;
};
class IOBufferMemoryDescriptor : public IOMemoryDescriptor {
public:
    static IOBufferMemoryDescriptor *withBytes(const void *p,size_t n,int) {
        if (Faults::hit("buffer")) return nullptr;
        auto *v=new IOBufferMemoryDescriptor;
        v->bytes.assign((const uint8_t *)p,(const uint8_t *)p+n);return v;
    }
};
class IORegistryEntry : public OSObject {
public:
    bool propsPermitted=true;
    std::map<std::string,OSObject *> props;
    bool setProperty(const char *key,OSObject *v) {
        assert(propsPermitted && !freed);
        if (!v || Faults::hit("property")) return false;
        v->retain(); removeProperty(key); props[key]=v;return true;
    }
    bool setProperty(const char *key,uint64_t n,unsigned width) {
        auto *v=OSNumber::withNumber(n,width);if(!v)return false;
        bool r=setProperty(key,v);v->release();return r;
    }
    bool setProperty(const char *key,const char *s) {
        auto *v=OSString::withCString(s);if(!v)return false;
        bool r=setProperty(key,v);v->release();return r;
    }
    OSObject *getProperty(const char *k) {
        if (Faults::corruptProperty && std::string(k)==kIOHIDRegisterServiceKey) return nullptr;
        auto it=props.find(k);return it==props.end()?nullptr:it->second;
    }
    void removeProperty(const char *k) {
        assert(propsPermitted && !freed);
        auto it=props.find(k);if(it!=props.end()){auto *p=it->second;props.erase(it);p->release();}
    }
    uint64_t getRegistryEntryID() const { return 42; }
};
class IOWorkLoop : public OSObject {
public:
    int added=0,removed=0;
    static IOWorkLoop *workLoop() { return Faults::hit("workloop")?nullptr:new IOWorkLoop; }
    IOReturn addEventSource(OSObject *) { if(Faults::hit("add_source"))return kIOReturnError;added++;return 0; }
    void removeEventSource(OSObject *) { assert(!freed);removed++; }
};
class IOService;
static std::vector<IOService *> registrations;
static std::function<void(IOService *)> onRegister;
class IOService : public IORegistryEntry {
public:
    IOService *provider=nullptr; bool inactive=false; unsigned registerCount=0;
    virtual bool init(OSDictionary * =nullptr) { propsPermitted=!Faults::hit("init");return propsPermitted; }
    virtual IOService *probe(IOService *,SInt32 *) { return this; }
    virtual bool start(IOService *) { return true; }
    virtual void stop(IOService *) {}
    virtual void handleStop(IOService *) {}
    virtual bool handleStart(IOService *) { return true; }
    virtual bool terminate(IOOptionBits=0) { inactive=true;return true; }
    virtual bool willTerminate(IOService *,IOOptionBits) { inactive=true;return true; }
    virtual bool handleOpen(IOService *,IOOptionBits,void *) { return true; }
    virtual IOReturn newUserClient(task_t,void *,UInt32,OSDictionary *,class IOUserClient **) { return 0; }
    virtual IOWorkLoop *getWorkLoop() const { return provider?provider->getWorkLoop():nullptr; }
    IOService *getProvider() { return provider; }
    bool isInactive() const { return inactive; }
    bool attach(IOService *p) {
        if(Faults::hit("attach"))return false;
        assert(!provider);provider=p;retain();p->retain();return true;
    }
    void detach(IOService *p) { assert(provider==p);provider=nullptr;p->release();release(); }
    void registerService(IOOptionBits=0) {
        assert(!Faults::gateDepth);registerCount++;registrations.push_back(this);
        if(onRegister)onRegister(this);
    }
};
class IOUserClient : public IOService {};
static std::function<void(class IOHIDDevice *,IOMemoryDescriptor *)> onReport;
class IOHIDDevice : public IOService {
public:
    IOWorkLoop *_workLoop=nullptr; unsigned baseStops=0,baseOpens=0,baseClients=0;
    bool baseReady=false;
    bool start(IOService *p) override {
        assert(!Faults::gateDepth);
        _workLoop=getWorkLoop();if(_workLoop)_workLoop->retain();
        IOMemoryDescriptor *d=nullptr;
        bool ok=_workLoop && !Faults::hit("base_start") && handleStart(p) &&
            newReportDescriptor(&d)==0 && !Faults::hit("descriptor_parse");
        if(d)d->release();
        if(!ok){stop(p);return false;} // real base's virtual self-stop
        baseReady=true;
        if(getProperty(kIOHIDRegisterServiceKey)!=kOSBooleanFalse)registerService();
        return true;
    }
    void stop(IOService *p) override {
        ++baseStops;assert(baseStops==1);handleStop(p);baseReady=false;
        if(_workLoop)_workLoop->removeEventSource(this);
    }
    void free() override { if(_workLoop){_workLoop->release();_workLoop=nullptr;}OSObject::free(); }
    bool handleOpen(IOService *,IOOptionBits,void *) override { ++baseOpens;return true; }
    IOReturn newUserClient(task_t,void *,UInt32,OSDictionary *,IOUserClient **) override { ++baseClients;return 0; }
    virtual IOReturn newReportDescriptor(IOMemoryDescriptor **) const { return kIOReturnUnsupported; }
    IOReturn handleReport(IOMemoryDescriptor *d,IOHIDReportType) {
        assert(!freed && refs>0 && _workLoop && !_workLoop->freed && baseReady);
        if(onReport)onReport(this,d);
        assert(!freed && _workLoop && !_workLoop->freed);
        return 0;
    }
    virtual IOReturn getReport(IOMemoryDescriptor *,IOHIDReportType,IOOptionBits) { return kIOReturnUnsupported; }
    virtual IOReturn setReport(IOMemoryDescriptor *,IOHIDReportType,IOOptionBits) { return kIOReturnUnsupported; }
    virtual IOReturn getReport(IOMemoryDescriptor *,IOHIDReportType,IOOptionBits,UInt32,IOHIDCompletion *) { return kIOReturnUnsupported; }
    virtual IOReturn setReport(IOMemoryDescriptor *,IOHIDReportType,IOOptionBits,UInt32,IOHIDCompletion *) { return kIOReturnUnsupported; }
    virtual OSString *newTransportString() const { return nullptr; }
    virtual OSString *newProductString() const { return nullptr; }
    virtual OSNumber *newVendorIDNumber() const { return nullptr; }
    virtual OSNumber *newProductIDNumber() const { return nullptr; }
    virtual OSNumber *newVersionNumber() const { return nullptr; }
    virtual OSNumber *newCountryCodeNumber() const { return nullptr; }
    virtual OSNumber *newLocationIDNumber() const { return nullptr; }
};
using Action=IOReturn (*)(OSObject *,void *,void *,void *,void *);
class IOCommandGate : public OSObject {
public:
    OSObject *owner=nullptr;
    static IOCommandGate *commandGate(OSObject *owner) {
        if(Faults::hit("gate"))return nullptr;
        auto *p=new IOCommandGate;p->owner=owner;return p;
    }
    IOReturn runAction(Action f,void *a=nullptr,void *b=nullptr,void *c=nullptr,void *d=nullptr) {
        assert(!freed);++Faults::gateDepth;auto r=f(owner,a,b,c,d);--Faults::gateDepth;return r;
    }
};
class IOTimerEventSource : public OSObject {
public:
    bool armed=false;
    static IOTimerEventSource *timerEventSource(int,OSObject *,void (*)(OSObject *,IOTimerEventSource *)) {
        return Faults::hit("timer")?nullptr:new IOTimerEventSource;
    }
    IOReturn setTimeoutMS(unsigned n) { assert(n==2);if(Faults::hit("arm"))return kIOReturnError;armed=true;return 0; }
    void cancelTimeout() { armed=false; }
};
struct m1kbd_hw { bool busy=false; };
class SFM1KeyboardHW {
public:
    m1kbd_hw value;
    const m1kbd_hw *state() const { return &value; }
    int closeAfterQuiesce() { ++Faults::closes;return 0; }
    static uint64_t clockUS() { return 200000; }
};
inline int m1kbd_hw_stop(m1kbd_hw *) { return 0; }
constexpr int M1KBD_STATE=-10,M1KBD_INVALID=-11,M1KBD_IO=-12;
static std::map<std::string,int> bootargs;
inline bool PE_parse_boot_argn(const char *k,void *v,int n) {
    auto it=bootargs.find(k);if(it==bootargs.end())return false;
    assert(n==(int)sizeof(int));std::memcpy(v,&it->second,n);return true;
}
inline bool OSCompareAndSwap(UInt32 old,UInt32 next,volatile UInt32 *p) { if(*p!=old)return false;*p=next;return true; }
inline void IOLog(const char *,...) {}
inline void panic(const char *) { assert(false && "production panic reached");std::abort(); }
#define OSDeclareDefaultStructors(c) \
public: \
    static void *operator new(size_t n) noexcept { \
        if(Faults::hit(#c " allocation")){return nullptr;} \
        return std::calloc(1,n); \
    } \
    static void operator delete(void *p) noexcept { std::free(p); } \
    using FakeDeclaredClass=c
#define OSDefineMetaClassAndStructors(c,b)
#define APPLE_KEXT_OVERRIDE override
#define OSDynamicCast(c,p) dynamic_cast<c *>(p)
