/* SPDX-License-Identifier: MIT */
/* Kept in a needed diagnostic dylib: executable-local interposition did not
 * reach the installed HAL on M1. Process-lifetime bounded proxy contexts. */
#include <CoreAudio/CoreAudio.h>
#include <mach/mach_time.h>
#include <stdint.h>
struct io_context { AudioDeviceIOProc proc; void *data; };
static struct io_context contexts[16];
static unsigned context_count;
static _Thread_local const AudioTimeStamp *current_output;
static _Thread_local uint64_t current_entry;
int SFAudioCopyHALBoundary(AudioTimeStamp *out,uint64_t *entry)
{
    if(!out||!entry||!current_output)return 0;
    *out=*current_output;*entry=current_entry;return 1;
}
static OSStatus io_proxy(AudioObjectID device,const AudioTimeStamp *now,
        const AudioBufferList *input,const AudioTimeStamp *input_time,
        AudioBufferList *output,const AudioTimeStamp *output_time,void *opaque)
{
    struct io_context *context=opaque;
    const AudioTimeStamp *previous=current_output;uint64_t previous_entry=current_entry;
    current_output=output_time;current_entry=mach_absolute_time();
    OSStatus result=context->proc(device,now,input,input_time,output,output_time,context->data);
    current_output=previous;current_entry=previous_entry;return result;
}
static OSStatus trace_create(AudioObjectID device,AudioDeviceIOProc proc,void *data,AudioDeviceIOProcID *out)
{
    if(!proc)return AudioDeviceCreateIOProcID(device,proc,data,out);
    unsigned index=__atomic_fetch_add(&context_count,1,__ATOMIC_RELAXED);
    if(index>=16)return AudioDeviceCreateIOProcID(device,proc,data,out);
    struct io_context *context=&contexts[index];*context=(struct io_context){proc,data};
    return AudioDeviceCreateIOProcID(device,io_proxy,context,out);
}
/* dyld excludes this dylib's original references from its own interposition. */
__attribute__((used,section("__DATA,__interpose")))
static const struct {const void *replacement,*original;} interpose={trace_create,AudioDeviceCreateIOProcID};
