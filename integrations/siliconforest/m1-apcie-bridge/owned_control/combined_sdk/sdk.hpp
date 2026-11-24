/* TEST ONLY API-shaped stand-ins. Not XNU headers or native runtime proof. */
#ifndef BPN_TEST_SDK_HPP
#define BPN_TEST_SDK_HPP
#define BNO_TEST_SDK 1
#define BOC_COMBINED_TEST_SDK 1
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <mutex>
using task_t=void *;
using IOByteCount=uint64_t;
using addr64_t=uint64_t;
using IOOptionBits=uint64_t;
using IOReturn=int;
constexpr int kIOReturnSuccess=0;
constexpr IOOptionBits kIODirectionOutIn=3, kIOMemoryMapperNone=16,
    kIOMemoryHostPhysicallyContiguous=32;
extern uint64_t page_size;
extern uint32_t test_page_shift;
#define PAGE_SHIFT test_page_shift
#define PAGE_SIZE (UINT64_C(1)<<test_page_shift)
extern int sdk_lock_depth, sdk_allocations, sdk_prepares, sdk_completes, sdk_resets, sdk_live;
extern int sdk_fail_action,sdk_fail_role,sdk_after;
void sdk_observe(int);
extern uint64_t test_descriptor_pa, test_descriptor_cpu_alias;
struct lck_mtx_t { uint64_t words[2]; };
struct IOLock { lck_mtx_t typed{}; std::mutex mutex; };
extern "C" lck_mtx_t *IOLockGetMachLock(IOLock *);
IOLock *test_mutex_allocate();
inline IOLock *IOLockAlloc() { return test_mutex_allocate(); }
inline void IOLockLock(IOLock *l) { l->mutex.lock();sdk_lock_depth++; }
inline void IOLockUnlock(IOLock *l) { sdk_lock_depth--;l->mutex.unlock(); }
void *test_control_allocate(size_t);
inline void *IOMallocZero(size_t n) { return test_control_allocate(n); }
template<class T> class OSSharedPtr {
    T *ptr=nullptr;
public:
    OSSharedPtr()=default;
    explicit OSSharedPtr(T *p):ptr(p){}
    OSSharedPtr(OSSharedPtr &&o) noexcept:ptr(o.ptr){o.ptr=nullptr;}
    OSSharedPtr &operator=(OSSharedPtr &&o) noexcept {reset();ptr=o.ptr;o.ptr=nullptr;return *this;}
    OSSharedPtr(const OSSharedPtr &)=delete;
    ~OSSharedPtr(){reset();}
    T *get() const{return ptr;}
    T *operator->() const{return ptr;}
    explicit operator bool() const{return ptr!=nullptr;}
    void reset(){if(ptr){T *p=ptr;ptr=nullptr;p->release();}}
};
template<class T> using OSPtr=OSSharedPtr<T>;
class IOBufferMemoryDescriptor {
    void *bytes;
    unsigned role,wire=1;
public:
    explicit IOBufferMemoryDescriptor(unsigned r):role(r){assert(!posix_memalign(&bytes,16384,16384));std::memset(bytes,0xac,16384);sdk_live++;}
    static OSPtr<IOBufferMemoryDescriptor> inTaskWithPhysicalMask(task_t task,IOOptionBits options,uint64_t capacity,uint64_t mask)
    {
        assert(task && !sdk_lock_depth);sdk_observe(1);
        assert(options==(kIODirectionOutIn|kIOMemoryMapperNone|kIOMemoryHostPhysicallyContiguous));
        assert(capacity==16384 && mask==UINT64_C(0xfffffc000));
        int role=sdk_allocations++;
        if(sdk_fail_action==1 && sdk_fail_role==role)return OSPtr<IOBufferMemoryDescriptor>();
        return OSPtr<IOBufferMemoryDescriptor>(new IOBufferMemoryDescriptor(static_cast<unsigned>(role)));
    }
    IOReturn prepare(int direction)
    {
        assert(!sdk_lock_depth && direction==3 && sdk_live==3 && wire==1);sdk_observe(2);sdk_prepares++;
        bool fail=sdk_fail_action==2 && sdk_fail_role==static_cast<int>(role);
        if(!fail || sdk_after)wire++;
        return fail?-88:0;
    }
    IOReturn complete(int direction)
    {
        assert(!sdk_lock_depth && direction==3 && sdk_live==3 && wire==2);sdk_observe(6);sdk_completes++;
        bool fail=sdk_fail_action==6 && sdk_fail_role==static_cast<int>(role);
        if(!fail || sdk_after)wire--;
        return fail?-99:0;
    }
    void *getBytesNoCopy(){assert(!sdk_lock_depth);return test_descriptor_cpu_alias?reinterpret_cast<void *>(test_descriptor_cpu_alias):bytes;}
    uint64_t getCapacity(){assert(!sdk_lock_depth);return 16384;}
    uint64_t getLength(){assert(!sdk_lock_depth);return 16384;}
    addr64_t getPhysicalSegment(uint64_t off,IOByteCount *span,IOOptionBits options)
    {assert(!sdk_lock_depth && off==0 && options==kIOMemoryMapperNone);sdk_observe(3);*span=16384;return test_descriptor_pa+role*16384;}
    void release(){assert(!sdk_lock_depth && wire==1);sdk_observe(7);sdk_resets++;sdk_live--;std::free(bytes);delete this;}
};
#endif
