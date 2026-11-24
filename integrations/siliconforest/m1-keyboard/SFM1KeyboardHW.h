/* Native mapping owner. Original code; see ADAPTER.md. */
#ifndef SF_M1_KEYBOARD_HW_H
#define SF_M1_KEYBOARD_HW_H
#include "m1kbd_hw.h"
#include <IOKit/IOMemoryDescriptor.h>
#include <libkern/c++/OSArray.h>

class SFM1KeyboardHW {
public:
    SFM1KeyboardHW();
    /* No MMIO until sfm1kbd=1, exact board validation and all maps succeed. */
    int open();
    int start();
    int readPacket(uint8_t packet[M1KBD_PACKET_SIZE]);
    int writePacket(const uint8_t packet[M1KBD_PACKET_SIZE]);
    /* Caller must have disabled and joined its workloop producers. */
    int closeAfterQuiesce();
    const struct m1kbd_hw *state() const { return &hw; }
    static uint64_t clockUS();
private:
    SFM1KeyboardHW(const SFM1KeyboardHW &) = delete;
    SFM1KeyboardHW &operator=(const SFM1KeyboardHW &) = delete;
    struct m1kbd_hw hw;
    IOMemoryDescriptor *descriptors[M1KBD_REGIONS];
    IOMemoryMap *maps[M1KBD_REGIONS];
    OSArray *propertyCache;
    bool opened;
    static int property(void *, const char *, const char *, const uint8_t **, size_t *);
    static int read32(void *, unsigned, uint32_t, uint32_t *);
    static int write32(void *, unsigned, uint32_t, uint32_t);
    static uint64_t nowUS(void *);
    static void delayUS(void *, uint32_t);
    void releaseMaps();
};
#endif
