// SPDX-License-Identifier: MIT
// libIOReport for Apple's Activity Monitor on SiliconForest (docs/roadmap/issues/SF-AA-activity-monitor.md).
// The kernel publishes no IOReport channels here (no subscription is made by this application
// either: it only takes deltas of samples it has): a delta of no samples is no sample, and there
// is no channel to iterate.
#include <CoreFoundation/CoreFoundation.h>
#include <stdint.h>

CFDictionaryRef IOReportCreateSamplesDelta(CFDictionaryRef a, CFDictionaryRef b, CFTypeRef unused)
{
	(void)a; (void)b; (void)unused;
	return NULL;
}

int IOReportIterate(CFDictionaryRef samples, int (^block)(CFDictionaryRef channel))
{
	(void)samples; (void)block;
	return 0;
}

uint64_t IOReportChannelGetChannelID(CFDictionaryRef channel) { (void)channel; return 0; }
int64_t IOReportArrayGetValueAtIndex(CFDictionaryRef channel, int index) { (void)channel; (void)index; return 0; }
