// SPDX-License-Identifier: MIT
// CoreSymbolication.framework (private) for Apple's Activity Monitor on SiliconForest
// (docs/roadmap/issues/SF-AA-activity-monitor.md). Activity Monitor calls it only to symbolicate
// a sample with a dSYM the user picks (-[SMSymbolication loadDSYM:]). Not implemented: no
// symbolicator is found for any path, so nothing is symbolicated; every object is the null one.
// (Finch's CoreSymbolication symbolicates the running kernel for zprint and zlog; none of the
// functions Activity Monitor calls is among them.)
#include <CoreFoundation/CoreFoundation.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct { uintptr_t _opaque_1, _opaque_2; } CSTypeRef;
#define CSNULL ((CSTypeRef){ 0, 0 })

const CFSetCallBacks kCSTypeSetCallBacks = { 0, NULL, NULL, NULL, NULL, NULL };

bool CSIsNull(CSTypeRef cs) { return cs._opaque_1 == 0 && cs._opaque_2 == 0; }
CSTypeRef CSRetain(CSTypeRef cs) { return cs; }
void CSRelease(CSTypeRef cs) { (void)cs; }

size_t CSSymbolicatorForeachSymbolicatorWithPath(const char *path, void (^block)(CSTypeRef symbolicator)) { (void)path; (void)block; return 0; }
size_t CSSymbolicatorForeachSymbolOwnerAtTime(CSTypeRef symbolicator, uint64_t time, void (^block)(CSTypeRef owner)) { (void)symbolicator; (void)time; (void)block; return 0; }
CSTypeRef CSSymbolicatorGetSymbolOwnerWithAddressAtTime(CSTypeRef symbolicator, uint64_t address, uint64_t time) { (void)symbolicator; (void)address; (void)time; return CSNULL; }
CSTypeRef CSSymbolOwnerGetSymbolWithAddress(CSTypeRef owner, uint64_t address) { (void)owner; (void)address; return CSNULL; }
CSTypeRef CSSymbolOwnerGetSourceInfoWithAddress(CSTypeRef owner, uint64_t address) { (void)owner; (void)address; return CSNULL; }
const char *CSSymbolOwnerGetName(CSTypeRef owner) { (void)owner; return NULL; }
uint64_t CSSymbolOwnerGetBaseAddress(CSTypeRef owner) { (void)owner; return 0; }
const char *CSSymbolGetName(CSTypeRef symbol) { (void)symbol; return NULL; }
const char *CSSourceInfoGetFilename(CSTypeRef info) { (void)info; return NULL; }
uint32_t CSSourceInfoGetLineNumber(CSTypeRef info) { (void)info; return 0; }
