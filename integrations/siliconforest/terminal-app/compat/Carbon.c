// SPDX-License-Identifier: MIT
// Carbon additions (wrapper over the tree's Carbon umbrella, re-exported).
#include <stdint.h>
#include <stddef.h>
typedef int32_t OSStatus;
enum { paramErr = -50, eventNotHandledErr = -9874, tsmUnsupportedTypeErr = -2516 };

// Carbon events: there is no Carbon event queue here. An event cannot be made, so nothing
// can be set on one.
OSStatus CreateEvent(void *allocator, uint32_t eventClass, uint32_t kind, double when, uint32_t attributes, void **outEvent)
{
    if (outEvent) *outEvent = NULL;
    return eventNotHandledErr;
}
void ReleaseEvent(void *event) { }
OSStatus SetEventParameter(void *event, uint32_t name, uint32_t type, long size, const void *data) { return paramErr; }

// Text Services Manager dead-key state (private). Key composition is done by the compositor's
// keyboard layout (xkb) before a key reaches the application: there is never a pending dead key.
uint32_t TSMGetDeadKeyState(void) { return 0; }
void TSMSetDeadKeyState(uint32_t state) { }
OSStatus TSMProcessRawKeyCode(void *document, uint32_t keyCode, uint32_t modifiers, uint32_t options, void *out) { return tsmUnsupportedTypeErr; }
