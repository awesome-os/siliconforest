// Minimal collection API for the extracted capability-to-endpoint selector.
// These are not IOHID descriptor-parser or IOKit ABI substitutes.
struct OSObject { virtual ~OSObject() = default; };
struct OSNumber:OSObject {
    unsigned value;
    explicit OSNumber(unsigned v):value(v) {}
    unsigned unsigned32BitValue() const { return value; }
};
struct OSDictionary:OSObject {
    std::map<std::string,OSObject *> values;
    OSObject *getObject(const char *k) { auto it=values.find(k); return it==values.end()?nullptr:it->second; }
};
struct OSArray:OSObject {
    std::vector<OSObject *> values;
    unsigned getCount() const { return (unsigned)values.size(); }
    OSObject *getObject(unsigned n) const { return values.at(n); }
};
#define OSDynamicCast(type, value) dynamic_cast<type *>(value)
static const char *kIOHIDDeviceUsagePageKey="page",*kIOHIDDeviceUsageKey="usage";
constexpr unsigned kHIDPage_GenericDesktop=1,kHIDUsage_GD_Keyboard=6,
    kHIDUsage_GD_Keypad=7,kHIDUsage_GD_Mouse=2;
