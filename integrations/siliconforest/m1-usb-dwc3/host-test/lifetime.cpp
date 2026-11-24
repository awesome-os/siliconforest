// SPDX-License-Identifier: MIT
#include "sf_usb_lifetime.h"
#include "sf_usb_iova.h"
#include <atomic>
#include <thread>
#include <vector>
#include <cassert>
#include <cstdio>
#include <cstdlib>

int main()
{
    // A held operation prevents reclamation after close; new calls fail.
    sf_usb_lifetime gate;
    sf_usb_lifetime_init(&gate);
    assert(sf_usb_lifetime_enter(&gate));
    sf_usb_lifetime_close(&gate);
    assert(!sf_usb_lifetime_enter(&gate) && sf_usb_lifetime_active(&gate) == 1);
    sf_usb_lifetime_leave(&gate);
    assert(sf_usb_lifetime_active(&gate) == 0);
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        sf_usb_lifetime_init(&gate);
        std::atomic<unsigned> ready{0}, touched{0};
        std::atomic<bool> go{false};
        unsigned *resource = new unsigned(0x12345678);
        std::vector<std::thread> workers;
        for (unsigned w = 0; w < 8; ++w) workers.emplace_back([&] {
            ++ready;
            while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
            for (;;) {
                SFUSBOperation op(&gate);
                if (!op) break;
                assert(*resource == 0x12345678);
                ++touched;
            }
        });
        while (ready != 8) std::this_thread::yield();
        go.store(true, std::memory_order_release);
        while (touched < 16) std::this_thread::yield();
        sf_usb_lifetime_close(&gate);
        while (sf_usb_lifetime_active(&gate)) std::this_thread::yield();
        delete resource; // workers can still attempt entry, but cannot touch it
        for (auto &w : workers) w.join();
        for (unsigned n = 0; n < 100; ++n) assert(!sf_usb_lifetime_enter(&gate));
    }
    // Exhaustively compare production IOVA search to independent brute force
    // across every 16-page fragmentation pattern, lengths and alignments.
    uint8_t busy[16];
    unsigned cases = 0;
    for (unsigned bits = 0; bits < 65536; ++bits) {
        for (unsigned i = 0; i < 16; ++i) busy[i] = (bits >> i) & 1;
        for (unsigned count = 1; count <= 16; ++count)
            for (unsigned align = 1; align <= 8; align *= 2) {
                size_t expected = SIZE_MAX;
                for (size_t i = 0; i + count <= 16; ++i) {
                    if ((i + 1) % align) continue; // deliberately non-aligned base
                    bool free = true;
                    for (size_t j = i; j < i + count; ++j) if (busy[j]) free = false;
                    if (free) { expected = i; break; }
                }
                assert(sf_usb_iova_find(busy, 16, 0x4000, 0x4000, count, align * 0x4000) == expected);
                ++cases;
            }
    }
    assert(sf_usb_iova_find(busy, 16, 0, 0x4000, 0, 0x4000) == SIZE_MAX);
    assert(sf_usb_iova_find(busy, 16, 0, 0x4000, 1, 0x6000) == SIZE_MAX);
    printf("100 concurrent removal cycles; %u IOVA fragmentation/alignment cases passed\n", cases);
}
