// SPDX-License-Identifier: MIT
// libsystemstats for Apple's Activity Monitor on SiliconForest (docs/roadmap/issues/SF-AA-activity-monitor.md).
// On macOS a client of the systemstats daemon's history (the energy tab's 12-hour charts and the
// battery charge graph). There is no such history here: both answer nothing (nil).
#include <stdint.h>
void *systemstats_get_battery_charge_graph(uint64_t a) { (void)a; return 0; }
void *systemstats_get_top_coalitions(uint64_t seconds, uint32_t count) { (void)seconds; (void)count; return 0; }
