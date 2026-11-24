// Host-only notification lifetime and synchronous-existing behavior.
struct IOService;
struct IONotifier;
using NotificationHandler=bool (*)(void *,void *,IOService *,IONotifier *);
static std::atomic<int> failMatching{0}, failNotification{0};
static unsigned registrationCalls=0, notificationRemovals=0;
static bool bsdPublished=false;
static thread_local unsigned inRegistration=0,callbackDepth=0;
static std::function<void()> registrationHook,callbackHook;
struct IONotifier {
    NotificationHandler callback;
    bool removed=false;
    explicit IONotifier(NotificationHandler h):callback(h) {}
    void remove() {
        assert(!inRegistration && !callbackDepth);
        assert(!removed); removed=true; ++notificationRemovals;
    }
};
static std::vector<IONotifier *> notifiers;
static const char *gIOPublishNotification="publish";
static void invoke(IONotifier *n) {
    if(n->removed) return;
    ++callbackDepth;
    if(callbackHook) { auto h=std::move(callbackHook); callbackHook=nullptr; h(); }
    assert(n->callback(nullptr,nullptr,nullptr,n));
    --callbackDepth;
}
static void publishBSD() {
    bsdPublished=true;
    for(auto *n:notifiers) invoke(n);
}
struct IOService {
    static OSDictionary *resourceMatching(const char *name) {
        assert(!strcmp(name,"IOBSD"));
        if(consume(failMatching)) return nullptr;
        return new OSDictionary;
    }
    static IONotifier *addMatchingNotification(const char *kind,OSDictionary *,
            NotificationHandler handler,void *,void *) {
        assert(kind==gIOPublishNotification); ++registrationCalls;
        if(consume(failNotification)) return nullptr;
        auto *n=new IONotifier(handler); notifiers.push_back(n);
        ++inRegistration;
        if(bsdPublished) invoke(n);
        if(registrationHook) { auto h=std::move(registrationHook); registrationHook=nullptr; h(); }
        --inRegistration;
        return n;
    }
};
