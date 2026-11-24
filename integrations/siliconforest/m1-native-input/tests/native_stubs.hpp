// Native lifecycle collaborators: only the production start() body is extracted.
static bool nativeBaseOK=true,nativeTimerOK=true;
constexpr int kIOReturnSuccess=0,kIOTimerEventSourceOptionsPriorityWorkLoop=1;
struct IOService {};
struct IOHIDDevice {
    bool start(IOService *) { return nativeBaseOK; }
    void stop(IOService *) {}
};
struct IOWorkLoop {
    static IOWorkLoop *workLoop() { static IOWorkLoop loop; return &loop; }
    int addEventSource(void *) { return kIOReturnSuccess; }
};
struct IOCommandGate {
    template<typename T> static IOCommandGate *commandGate(T *) {
        static IOCommandGate gate; return &gate;
    }
};
struct IOTimerEventSource {
    template<typename T,typename F> static IOTimerEventSource *timerEventSource(int,T *,F) {
        static IOTimerEventSource timer; return &timer;
    }
    int setTimeoutMS(int ms) { assert(ms==2); return nativeTimerOK?0:-1; }
};
struct PDM1SPIHID:IOHIDDevice {
    IOWorkLoop *transportLoop=nullptr;
    IOCommandGate *transportGate=nullptr;
    IOTimerEventSource *pollTimer=nullptr;
    bool gateAdded=false,timerAdded=false,published=false;
    static void startGated() {}
    static void poll() {}
    void disposeTransport() {}
    void setProperty(const char *,const char *) {}
    bool start(IOService *);
};
#define super IOHIDDevice
