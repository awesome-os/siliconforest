/* SPDX-License-Identifier: MIT */
/* Includes the actual observer body; host models never prove Mach-O interpose
 * binding, real device behavior, media shutdown or acoustic output. */
#define SF_MEDIA_OBSERVER_HOST_TEST 1
#include "../hal-observer.c"
#include <assert.h>
struct mock_unit {AURenderCallbackStruct callback;unsigned running,disposed;OSStatus start_status,stop_status,uninit_status,dispose_status;};
static struct mock_unit a,b;
static unsigned original_calls,property_after_dispose;
static struct sf_audio_ring ring={.version=2,.channels=2,.frames=8192,.periodFrames=512,.sampleRate=48000};
static struct sf_audio_session session={&ring};
static OSStatus fake_start(AudioUnit value){struct mock_unit *u=value;if(!u)return -50;if(u->start_status)return u->start_status;u->running=1;ring.running=1;return 0;}
static OSStatus fake_stop(AudioUnit value){struct mock_unit *u=value;if(!u)return -50;if(u->stop_status)return u->stop_status;u->running=0;ring.running=a.running||b.running;return 0;}
static OSStatus fake_uninit(AudioUnit value){struct mock_unit *u=value;if(u->uninit_status)return u->uninit_status;return fake_stop(value);}
static OSStatus fake_dispose(AudioUnit value){struct mock_unit *u=value;if(u->dispose_status)return u->dispose_status;u->running=0;u->disposed=1;ring.running=a.running||b.running;return 0;}
static OSStatus fake_set(AudioUnit value,AudioUnitPropertyID property,AudioUnitScope scope,AudioUnitElement element,const void *data,UInt32 size){
 (void)scope;(void)element;struct mock_unit *u=value;if(!u)return -50;
 if(property!=kAudioUnitProperty_SetRenderCallback||!data||size!=sizeof(u->callback))return -77;
 memcpy(&u->callback,data,size);return 0;
}
static OSStatus fake_get(AudioUnit value,AudioUnitPropertyID property,AudioUnitScope scope,AudioUnitElement element,void *data,UInt32 *size){
 (void)scope;(void)element;struct mock_unit *u=value;if(!u)return -50;
 if(u->disposed)property_after_dispose++;
 if(property==kAudioOutputUnitProperty_CurrentDevice&&*size>=4){*(UInt32 *)data=272;*size=4;return 0;}
 if(property==kAudioUnitProperty_SetRenderCallback&&*size>=sizeof(u->callback)){memcpy(data,&u->callback,sizeof(u->callback));*size=sizeof(u->callback);return 0;}
 return -78;
}
static OSStatus fake_clock(AudioObjectID device,AudioTimeStamp *stamp){(void)device;stamp->mSampleTime=ring.readFrame;return ring.running?0:-99;}
static const struct sf_audio_session *fake_session(AudioObjectID device){(void)device;return &session;}
static void *mock_dlsym(void *unused,const char *name){
 (void)unused;
 if(!strcmp(name,"AudioOutputUnitStart"))return (void *)fake_start;
 if(!strcmp(name,"AudioOutputUnitStop"))return (void *)fake_stop;
 if(!strcmp(name,"AudioUnitUninitialize"))return (void *)fake_uninit;
 if(!strcmp(name,"AudioComponentInstanceDispose"))return (void *)fake_dispose;
 if(!strcmp(name,"AudioUnitGetProperty"))return (void *)fake_get;
 if(!strcmp(name,"AudioUnitSetProperty"))return (void *)fake_set;
 if(!strcmp(name,"AudioDeviceGetCurrentTime"))return (void *)fake_clock;
 if(!strcmp(name,"SFAudioSessionIfConnected"))return (void *)fake_session;
 return NULL;
}
static int mock_dladdr(const void *unused,Dl_info *info){(void)unused;memset(info,0,sizeof(*info));info->dli_fname="HOST-MOCK-NO-HARDWARE";return 1;}
static OSStatus original(void *context,AudioUnitRenderActionFlags *flags,const AudioTimeStamp *stamp,UInt32 bus,UInt32 frames,AudioBufferList *data){
 assert(context==&original_calls&&stamp->mHostTime==123&&bus==7&&frames==512);
 assert(data->mNumberBuffers==1&&data->mBuffers[0].mData==&original_calls);
 *flags=0x456;original_calls++;return original_calls%4==0?-123:0;
}
static void install(struct mock_unit *u){
 AURenderCallbackStruct callback={original,&original_calls},read={0};UInt32 size=sizeof(read);
 assert(traced_set_property(u,kAudioUnitProperty_SetRenderCallback,1,0,&callback,sizeof(callback))==0);
 assert(traced_get_property(u,kAudioUnitProperty_SetRenderCallback,1,0,&read,&size)==0);
 assert(size==sizeof(read)&&read.inputProc==original&&read.inputProcRefCon==&original_calls);
}
int main(void){
 setenv("SF_FIREFOX_MEDIA_TRACE","1",1);setenv("SF_FIREFOX_MEDIA_TRACE_SECONDS","10",1);
 install(&a);assert(a.callback.inputProc==callback_proxy);
 AudioTimeStamp stamp={.mHostTime=123};AudioBufferList data={1,{{2,4,&original_calls}}};
 for(unsigned i=0;i<16;i++){
  unsigned before=original_calls;AudioUnitRenderActionFlags flags=0;
  OSStatus status=a.callback.inputProc(a.callback.inputProcRefCon,&flags,&stamp,7,512,&data);
  assert(status==((before+1)%4==0?-123:0)&&flags==0x456);
 }
 assert(callbacks[0].calls==16&&callbacks[0].frames==8192&&callbacks[0].successes==12&&callbacks[0].errors==4);
 assert(traced_set_property(&a,999,0,0,NULL,0)==-77);
 UInt32 size=4,value=0;assert(traced_get_property(&a,999,0,0,&value,&size)==-78);
 a.start_status=-101;assert(traced_start(&a)==-101&&!a.running);a.start_status=0;
 assert(traced_start(&a)==0&&a.running);install(&b);assert(traced_start(&b)==0&&b.running);
 a.stop_status=-102;assert(traced_stop(&a)==-102&&a.running);a.stop_status=0;
 assert(traced_stop(&a)==0&&!a.running&&b.running&&ring.running);
 b.uninit_status=-103;assert(traced_uninitialize(&b)==-103&&b.running);b.uninit_status=0;
 assert(traced_uninitialize(&b)==0&&!ring.running);
 a.dispose_status=-104;assert(traced_dispose(&a)==-104&&!a.disposed);a.dispose_status=0;
 assert(traced_dispose(&a)==0&&a.disposed);assert(traced_dispose(&b)==0&&b.disposed);
 usleep(550000);assert(property_after_dispose==0);
 uint64_t old_generation=units[0].generation;a.disposed=0;install(&a);assert(traced_start(&a)==0);
 assert(units[0].generation>old_generation);assert(traced_stop(&a)==0&&traced_dispose(&a)==0);
 /* Bounded context exhaustion keeps the real setter/getter behavior. */
 a.disposed=0;while(callback_count<MAX_CALLBACKS)install(&a);
 install(&a);assert(callback_count==MAX_CALLBACKS&&a.callback.inputProc==original);
 b.disposed=0;enabled=0;install(&b);assert(b.callback.inputProc==original);enabled=1;
 puts("OBSERVER_HOST PASS callbacks/status/getter/passthrough/multiple-units/disposal/reuse/capacity; NO target or audio claim");
 return 0;
}
