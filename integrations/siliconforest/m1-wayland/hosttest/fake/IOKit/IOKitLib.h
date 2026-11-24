/* Host-test stand-in for SiliconForest <IOKit/IOKitLib.h>, which includes
 * SFIOKitLib.h and defines _SF_IOKITLIB_H. Signatures mirror SFIOKitLib.h at
 * SiliconForest e7aa00a (notably the 3-argument IOObjectConformsTo returning a
 * status), not Apple's macOS headers. */
#ifndef _SF_IOKITLIB_H
#define _SF_IOKITLIB_H
#include <mach/kern_return.h>
#include <stddef.h>
#include <stdint.h>
typedef unsigned int mach_port_t;
typedef mach_port_t io_object_t;
typedef io_object_t io_service_t;
typedef io_object_t io_connect_t;
typedef mach_port_t task_t;
typedef int boolean_t;
typedef uint64_t mach_vm_address_t;
typedef uint64_t mach_vm_size_t;
typedef char io_name_t[128];
#define IO_OBJECT_NULL ((io_object_t)0)
#define kIOMapAnywhere 0x00000001u
kern_return_t IOMasterPort(mach_port_t bootstrapPort, mach_port_t *masterPort);
char *IOServiceMatching(const char *className);
kern_return_t IOServiceGetMatchingService(mach_port_t masterPort, char *matching, io_service_t *service);
kern_return_t IOServiceOpen(io_service_t service, task_t owningTask, uint32_t type, io_connect_t *connect);
kern_return_t IOServiceClose(io_connect_t connect);
kern_return_t IOConnectMapMemory64(io_connect_t connect, uint32_t memoryType, task_t intoTask,
    mach_vm_address_t *address, mach_vm_size_t *size, uint32_t options);
kern_return_t IOConnectUnmapMemory64(io_connect_t connect, uint32_t memoryType, task_t fromTask,
    mach_vm_address_t address);
kern_return_t IOConnectCallMethod(io_connect_t connect, uint32_t selector, const uint64_t *input,
    uint32_t inputCnt, const void *inputStruct, size_t inputStructCnt, uint64_t *output,
    uint32_t *outputCnt, void *outputStruct, size_t *outputStructCnt);
kern_return_t IOConnectCallStructMethod(io_connect_t connect, uint32_t selector, const void *inputStruct,
    size_t inputStructCnt, void *outputStruct, size_t *outputStructCnt);
kern_return_t IOConnectCallScalarMethod(io_connect_t connect, uint32_t selector, const uint64_t *input,
    uint32_t inputCnt, uint64_t *output, uint32_t *outputCnt);
kern_return_t IOObjectConformsTo(io_object_t object, const char *className, boolean_t *conforms);
kern_return_t IOObjectRelease(io_object_t object);
kern_return_t IORegistryEntryGetName(io_object_t entry, io_name_t name);
#endif
