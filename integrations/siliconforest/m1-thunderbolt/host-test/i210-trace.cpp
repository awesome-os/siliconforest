// SPDX-License-Identifier: MIT
#include "sf_i210_trace.h"
#include <atomic>
#include <cassert>
#include <thread>
#include <vector>
#include <string>
static std::string lastLog;
static unsigned logCount;
extern "C" void sfos_log(const char *text)
{
    ++logCount;
    lastLog = text;
}
int main()
{
    sf_i210_trace_log("SFI210TRACE output len=%u error=%d enabled=%u\n",342u,-1,1u);
    assert(logCount == 1 && lastLog == "SFI210TRACE output len=342 error=-1 enabled=1\n");
    std::string longText(900,'x');
    sf_i210_trace_log("%s",longText.c_str());
    assert(logCount == 2 && lastLog == std::string(511,'x'));
    uint32_t budget = 0;
    std::atomic<unsigned> accepted{0};
    std::vector<std::thread> threads;
    for (unsigned n = 0; n < 8; ++n)
        threads.emplace_back([&] {
            for (unsigned i = 0; i < 10000; ++i)
                if (sf_i210_trace_take(&budget)) ++accepted;
        });
    for (auto &thread : threads) thread.join();
    assert(accepted == 8 && budget == 8);
    assert(logCount == 2); // A budget query does not emit or start a worker.
    assert(!sf_i210_trace_take(&budget) && budget == 8);
    budget = UINT32_MAX;
    assert(!sf_i210_trace_take(&budget) && budget == UINT32_MAX);
}
