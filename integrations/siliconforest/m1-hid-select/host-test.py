#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run the actual transformed queue/select/ioctl functions with host locks.

This checks publication/registration ordering, not XNU's select ABI or real
input delivery. Native SDK and target select checks remain separate.
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

here = Path(__file__).parent
source = Path(sys.argv[1]) / 'src/Kernel/Extensions/IOHIDFamily/IOHIDSystem'
prepared = len(sys.argv) == 3 and sys.argv[2] == '--prepared'
if len(sys.argv) > 2 and not prepared:
    raise SystemExit('usage: host-test.py <source-tree> [--prepared]')
with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    dest = temp / 'src/Kernel/Extensions/IOHIDFamily/IOHIDSystem'
    dest.mkdir(parents=True)
    (dest / 'SFHIDEventQueue.cpp').write_bytes((source / 'SFHIDEventQueue.cpp').read_bytes())
    if not prepared:
        subprocess.run([sys.executable, str(here / 'prepare.py'), str(temp)], check=True)
    s = (dest / 'SFHIDEventQueue.cpp').read_text()
    functions = []
    for name in ['sf_hid_kbd_select', 'sf_hid_mouse_select', 'sf_hid_kbd_ioctl',
                 'sf_hid_mouse_ioctl', 'SFHIDPushKeyboardEvent', 'sf_hid_mouse_enqueue']:
        match = re.search(r'(?:static )?(?:int|void)\n' + name + r'\([^)]*\)\s*\{', s)
        assert match, name
        end = match.end()
        depth = 1
        while depth:
            depth += (s[end] == '{') - (s[end] == '}')
            end += 1
        functions.append(s[match.start():end])
    (temp / 'IOKit').mkdir()
    (temp / 'IOKit/IOTypes.h').write_text('#include <cstdint>\nusing UInt8=uint8_t;using SInt8=int8_t;using UInt16=uint16_t;using UInt32=uint32_t;\n')
    (temp / 'SFHIDEventQueue.h').write_bytes((source / 'SFHIDEventQueue.h').read_bytes())
    fixture = r'''
#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <mutex>
#include <thread>
#include <sys/types.h>
#include <sys/ioctl.h>
#include "SFHIDEventQueue.h"
#define FREAD 1
#define FWRITE 2
struct proc {};
struct IOLock { std::mutex mutex; } kbdLock,mouseLock;
static thread_local int held;
static void IOLockLock(IOLock *p){p->mutex.lock();++held;}
static void IOLockUnlock(IOLock *p){--held;p->mutex.unlock();}
struct selinfo {std::atomic<unsigned> records{0},wakes{0};};
static void selrecord(proc *,selinfo *s,void *){assert(held==1);++s->records;}
static void selwakeup(selinfo *s){assert(held==0);++s->wakes;}
static IOLock *gKbdLock=&kbdLock,*gMouseLock=&mouseLock;
static constexpr UInt32 kPublishKeyboard=1,kPublishMouse=2;
static volatile UInt32 gQueueReady=3;
static UInt32 gKbdEarlyDrops,gMouseEarlyDrops;
static UInt32 OSBitOrAtomic(UInt32 value,volatile UInt32 *p){return __atomic_fetch_or(p,value,__ATOMIC_ACQ_REL);}
template <typename T> static T sf_hid_load(T const *p){return __atomic_load_n(p,__ATOMIC_ACQUIRE);}
static selinfo gKbdSelect,gMouseSelect;
static UInt32 gKbdReadIndex,gKbdWriteIndex,gKbdSequence;
static UInt32 gMouseReadIndex,gMouseWriteIndex,gMouseSequence;
static SFHIDKbdEvent gKbdEvents[64];
static SFHIDMouseEvent gMouseEvents[64];
static bool sf_hid_kbd_setup(){return true;}
void SFHIDPublishKeyboardDevice(){assert(false);}
void SFHIDPublishMouseDevice(){assert(false);}
static bool sf_hid_console_key(UInt8,bool){return false;}
static void *gConsoleCall;
static void thread_call_enter(void *){assert(false);}
FUNCTIONS
int main()
{
 for(unsigned mouse=0;mouse<2;++mouse){
  auto select=mouse?sf_hid_mouse_select:sf_hid_kbd_select;
  auto ioctl=mouse?sf_hid_mouse_ioctl:sf_hid_kbd_ioctl;
  auto &read=mouse?gMouseReadIndex:gKbdReadIndex;
  auto &write=mouse?gMouseWriteIndex:gKbdWriteIndex;
  auto &selectors=mouse?gMouseSelect:gKbdSelect;
  int bytes=-1;
  if(PUBLICATION){
   gQueueReady=0;
   assert(ioctl(0,FIONREAD,(char *)&bytes,0,NULL)==ENXIO);
   assert(select(0,FREAD,NULL,NULL)==0 && !selectors.records);
   if(mouse){SFHIDMouseEvent e={};sf_hid_mouse_enqueue(e);}else SFHIDPushKeyboardEvent(4,true);
   assert(read==write && !selectors.wakes);
   assert(mouse?gMouseEarlyDrops==1:gKbdEarlyDrops==1);
   gQueueReady=3;
  }
  assert(ioctl(0,FIONREAD,(char *)&bytes,0,NULL)==0 && bytes==0);
  assert(ioctl(0,UINT64_MAX,(char *)&bytes,0,NULL)==ENOTTY);
  assert(select(0,FWRITE,NULL,NULL)==0 && !selectors.records);
  for(unsigned trial=0;trial<2000;++trial){
   read=write=0;selectors.records=selectors.wakes=0;
   std::atomic<bool> start{false};int ready=-1;
   std::thread reader([&]{while(!start.load()){}ready=select(0,FREAD,NULL,NULL);});
   std::thread writer([&]{while(!start.load()){}if(mouse){SFHIDMouseEvent e={};sf_hid_mouse_enqueue(e);}else SFHIDPushKeyboardEvent(4,true);});
   start=true;reader.join();writer.join();
   assert(ready==1 || (ready==0 && selectors.records==1));
   assert(selectors.wakes==1 && write-read==1);
   UInt32 before=read;
   assert(ioctl(0,FIONREAD,(char *)&bytes,0,NULL)==0 && bytes==(int)(mouse?sizeof(SFHIDMouseEvent):sizeof(SFHIDKbdEvent)) && read==before);
   assert(select(0,FREAD,NULL,NULL)==1);
  }
  read=write=0;
  for(unsigned n=0;n<70;++n)if(mouse){SFHIDMouseEvent e={};sf_hid_mouse_enqueue(e);}else SFHIDPushKeyboardEvent(4,true);
  assert(write-read==64);
  assert(ioctl(0,FIONREAD,(char *)&bytes,0,NULL)==0 && bytes==(int)(64*(mouse?sizeof(SFHIDMouseEvent):sizeof(SFHIDKbdEvent))));
 }
 puts("actual HID queue/select/ioctl: 4000 concurrent publication/registration races, non-consuming byte counts, unsupported requests and overflow PASS (host only)");
}
'''.replace('FUNCTIONS', '\n'.join(functions)).replace('PUBLICATION', 'true' if 'sf_hid_load(&gKbdLock)' in s else 'false')
    (temp / 'test.cpp').write_text(fixture)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++14', '-O1', '-g',
                    '-fsanitize=address,undefined', '-pthread', '-I', str(temp),
                    str(temp / 'test.cpp'), '-o', str(temp / 'test')], check=True)
    subprocess.run([str(temp / 'test')], check=True)
