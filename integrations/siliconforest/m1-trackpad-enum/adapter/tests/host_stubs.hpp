/* Host-only stand-ins for the few IOKit facilities the extracted adapter
 * functions use. Not an IOKit ABI; the real bodies are pasted in unchanged. */
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#define bzero(p, n) memset((p), 0, (n))
static int gLogs;
static char gLastLog[1024];
#define IOLog(...) (snprintf(gLastLog, sizeof(gLastLog), __VA_ARGS__), gLogs++)

#include "observation_layout.h"
#include "m1tp_enum.h"

enum { M1KBD_INVALID = -1, M1KBD_STATE = -8 };
typedef int IOReturn;
static const IOReturn kIOReturnSuccess = 0;
struct dlsp_message { const uint8_t *payload; size_t payload_length; };

struct OSData {
    std::vector<uint8_t> bytes;
    int refs = 1;
    static bool failNext;
    static int live;
    static OSData *withBytes(const void *p, unsigned n) {
        if (failNext) return nullptr;
        OSData *d = new OSData;
        d->bytes.assign(static_cast<const uint8_t *>(p), static_cast<const uint8_t *>(p) + n);
        live++;
        return d;
    }
    void release() { if (--refs == 0) { live--; delete this; } }
};
bool OSData::failNext = false;
int OSData::live = 0;

struct IOService {
    std::map<std::string, std::vector<uint8_t>> props;
    bool failSet = false;
    bool setProperty(const char *key, OSData *d) {
        if (failSet) return false;
        props[key] = d->bytes;
        return true;
    }
};

struct Gate {
    IOReturn result = kIOReturnSuccess;
    int calls = 0;
    std::function<void()> hook;
    template <typename F> IOReturn runAction(F) { calls++; if (hook) hook(); return result; }
};

struct Base : IOService {
    bool handleStartResult = true;
    bool handleStart(IOService *) { return handleStartResult; }
};
#define super Base

struct Response { bool ok; std::vector<uint8_t> data; };

class PDM1SPIHID : public Base {
public:
    struct m1tp_observation trackpadEnumeration;
    struct m1tp_interface_info trackpadInterface;
    uint8_t usedRequestKeys = 0;
    Gate gate;
    Gate *transportGate = &gate;
    std::vector<Response> script;
    std::vector<std::pair<unsigned, unsigned>> requests;
    std::vector<std::string> fails;
    PDM1SPIHID() {
        bzero(&trackpadEnumeration, sizeof(trackpadEnumeration));
        bzero(&trackpadInterface, sizeof(trackpadInterface));
        trackpadEnumeration.version = M1TP_OBSERVATION_VERSION;
        trackpadEnumeration.result = M1TP_OBS_NOT_ATTEMPTED;
        trackpadEnumeration.stage = M1TP_OBS_BEFORE_IO;
    }
    static IOReturn startGated(void *, void *, void *, void *, void *) { return kIOReturnSuccess; }
    void fail(int, const char *phase) { fails.push_back(phase); }
    /* Mirrors the real claim-before-send rule; scripted responses replace I/O. */
    bool waitResponse(uint8_t report, uint8_t subdevice, uint16_t length, struct dlsp_message *m) {
        requests.push_back({report, subdevice});
        assert(length == 512);
        if (!m1tp_claim_request(&usedRequestKeys, report, subdevice)) { fail(M1KBD_STATE, "request-key"); return false; }
        assert(!script.empty());
        Response r = script.front();
        script.erase(script.begin());
        if (!r.ok) { fail(M1KBD_STATE, "response-timeout"); return false; }
        held = r.data;
        m->payload = held.empty() ? nullptr : held.data();
        m->payload_length = held.size();
        return true;
    }
    bool enumerateTrackpad();
    bool preserveTrackpadEnumeration(IOService *provider);
    bool handleStart(IOService *provider);
private:
    std::vector<uint8_t> held;
};
