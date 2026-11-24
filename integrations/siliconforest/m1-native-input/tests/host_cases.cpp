// Appended after the exact patched queue source and extracted native start().
static void emptyKeyboard() {
    PDHIDKbdEvent e{}; uio u{(char *)&e,sizeof(e)};
    assert(pd_hid_kbd_read(0,&u,0)==EAGAIN);
    assert(gKbdSequence==0 && gKbdReadIndex==0 && gKbdWriteIndex==0);
    assert(gConsReadIndex==0 && gConsWriteIndex==0 && consoleBytes==0);
}
static void emptyMouse() {
    PDHIDMouseEvent e{}; uio u{(char *)&e,sizeof(e)};
    assert(pd_hid_mouse_read(0,&u,0)==EAGAIN);
    assert(gMouseSequence==0 && gMouseReadIndex==0 && gMouseWriteIndex==0);
}
static void once() {
    assert(duplicateMajors==0 && duplicateNodes==0);
    assert(maxConcurrentPublish<=1);
    assert(pd_hid_load(&gPublishCall)->options==THREAD_CALL_OPTIONS_ONCE);
}
int main(int argc,char **argv) {
    assert(argc==2); std::string test=argv[1];
    if(test=="no-request") {
        drain(); assert(nodes.empty() && majors.empty() && calls.empty());
        assert(!PDHIDKeyboardIsGrabbed()); return 0;
    }
    if(test=="native-super-fail" || test=="native-timer-fail" || test=="native-success") {
        nativeBaseOK=test!="native-super-fail"; nativeTimerOK=test!="native-timer-fail";
        PDM1SPIHID device; bool ok=device.start(nullptr);
        assert(ok==(test=="native-success")); drain();
        if(ok) {
            assert(nodes.empty()); // native start alone is not event-consumer readiness
            OSNumber page(1),usage(6); OSDictionary pair; OSArray pairs;
            pair.values={{"page",&page},{"usage",&usage}}; pairs.values={&pair};
            pdHIDPublishSupportedQueues(true,&pairs); drain();
            assert(nodes.size()==1 && nodes.count("usb_hid_kbd")); emptyKeyboard(); once();
        }
        else { assert(nodes.empty() && majors.empty() && gPublishRequests==0); }
        assert(gMouseSequence==0); return 0;
    }
    if(test=="empty-repeat") {
        PDHIDPublishKeyboardDevice(); assert(nodes.empty()); drain(); emptyKeyboard();
        for(int i=0;i<500;++i) PDHIDPublishKeyboardDevice(); drain();
        assert(nodes.size()==1 && majors.size()==1 && nodeAttempts==1 && majorAttempts==1);
        emptyKeyboard(); assert(!PDHIDKeyboardIsGrabbed());
        assert(pd_hid_kbd_open(0,0,0,nullptr)==0 && PDHIDKeyboardIsGrabbed());
        assert(pd_hid_kbd_close(0,0,0,nullptr)==0 && !PDHIDKeyboardIsGrabbed());
    } else if(test=="keyboard-no-mouse") {
        PDHIDPublishKeyboardDevice(); drain(); emptyKeyboard();
        assert(!nodes.count("usb_hid_mouse") && !pd_hid_load(&gMouseLock));
    } else if(test=="deferred-devfs") {
        devfsReady=false; PDHIDPublishKeyboardDevice(); PDHIDPublishMouseDevice(); drain();
        assert(nodes.empty() && majors.size()==2 && delayedSubmissions==1);
        for(int n=0;n<100;++n) { PDHIDPublishKeyboardDevice(); PDHIDPublishMouseDevice(); }
        assert(!runOne() && gKbdPublication.attempts==1 && gMousePublication.attempts==1);
        assert(!runOne()); assert(runOne(true)); assert(nodes.empty() && majorAttempts==2);
        devfsReady=true; assert(runOne(true)); drain();
        assert(nodes.size()==2 && majorAttempts==2); emptyKeyboard(); emptyMouse();
    } else if(test=="reentry") {
        majorHook=[] { PDHIDPublishMouseDevice(); PDHIDPublishKeyboardDevice(); };
        nodeHook=[] { PDHIDPublishPS2KeyboardDevice(); PDHIDPublishPS2MouseDevice(); };
        PDHIDPublishKeyboardDevice(); drain();
        assert(nodes.size()==4 && majors.size()==2);
        assert(nodes["usb_hid_kbd"]==nodes["ps2_kbd"] && nodes["usb_hid_mouse"]==nodes["ps2_mouse"]);
        emptyKeyboard(); emptyMouse();
    } else if(test=="concurrent") {
        std::vector<std::thread> threads;
        for(int i=0;i<32;++i) threads.emplace_back([i] {
            for(int n=0;n<100;++n) {
                switch(i%4) {
                    case 0:PDHIDPublishKeyboardDevice();break;
                    case 1:PDHIDPublishMouseDevice();break;
                    case 2:PDHIDPublishPS2KeyboardDevice();break;
                    default:PDHIDPublishPS2MouseDevice();break;
                }
                runOne();
            }
        });
        for(auto &t:threads) t.join(); drain();
        assert(nodes.size()==4 && majorAttempts==2 && nodeAttempts==4);
        assert(lockAlloc-lockFree==3 && callAlloc-callFree==2);
        emptyKeyboard(); emptyMouse();
    } else if(test=="setup-race") {
        std::vector<std::thread> threads;
        std::atomic<bool> go{false};
        for(int i=0;i<32;++i) threads.emplace_back([&go] {
            while(!go.load()) std::this_thread::yield();
            assert(pd_hid_kbd_setup()); assert(pd_hid_setup_lock(&gMouseLock));
            PDHIDPublishKeyboardDevice(); PDHIDPublishMouseDevice(); runOne();
        });
        go=true; for(auto &t:threads) t.join(); drain();
        assert(lockAlloc-lockFree==3 && callAlloc-callFree==2);
        assert(majorAttempts==2 && nodeAttempts==2); emptyKeyboard(); emptyMouse();
    } else if(test=="call-failure") {
        failCall=1; PDHIDPublishKeyboardDevice(); drain();
        assert(nodes.empty() && !pd_hid_load(&gPublishCall) && gPublishReady==0);
        PDHIDPublishKeyboardDevice(); drain(); emptyKeyboard();
    } else if(test=="lock-failure" || test=="major-failure" || test=="node-failure") {
        if(test=="lock-failure") failLock=1;
        if(test=="major-failure") failMajor=1;
        if(test=="node-failure") failNode=1;
        PDHIDPublishKeyboardDevice(); drain();
        assert(nodes.empty() && gPublishReady==0 && delayedSubmissions==1);
        assert(!runOne()); assert(runOne(true)); drain(); emptyKeyboard();
        if(test=="node-failure") assert(majorAttempts==1 && nodeAttempts==2);
    } else if(test=="persistent-failure") {
        failNode=100; PDHIDPublishKeyboardDevice(); drain();
        for(int n=1;n<kPublishAttemptLimit;++n) { assert(!runOne()); assert(runOne(true)); }
        assert(!runOne(true)); PDHIDPublishKeyboardDevice(); assert(!runOne(true));
        assert(nodes.empty() && gPublishReady==0 && majorAttempts==1 && nodeAttempts==kPublishAttemptLimit);
        assert(gKbdPublication.lastError==kPublishNode && (gPublishStopped&kPublishKeyboard));
        assert(gKbdWriteIndex==0 && consoleBytes==0);
    } else if(test=="event-before-devfs") {
        devfsReady=false; PDHIDPublishKeyboardDevice(); drain(); PDHIDPushKeyboardEvent(4,true); drain();
        PDHIDKbdEvent e{}; uio u{(char *)&e,sizeof(e)};
        assert(pd_hid_kbd_read(0,&u,0)==0 && e.usage==4 && e.down==1);
        assert(nodes.empty() && consoleBytes==1);
        devfsReady=true; assert(runOne(true)); drain();
        assert(nodes.count("usb_hid_kbd") && gKbdWriteIndex==1);
    } else if(test=="mouse-first-event") {
        PDHIDPushMouseEvent(0,0,1,2,0);
        assert(!pd_hid_load(&gPublishCall) && !pd_hid_load(&gMouseLock) && gMouseEarlyDrops==1);
        PDHIDPublishMouseDevice(); drain(); PDHIDPushMouseEvent(0,0,3,4,0); drain();
        PDHIDMouseEvent e{}; uio u{(char *)&e,sizeof(e)};
        assert(pd_hid_mouse_read(0,&u,0)==0 && e.sequence==1 && e.dx==3 && e.dy==4);
        assert(nodes.count("usb_hid_mouse") && !nodes.count("usb_hid_kbd"));
    } else if(test=="event-no-start") {
        PDHIDPushKeyboardEvent(4,true); PDHIDPushMouseEvent(0,0,1,2,0); drain();
        assert(gKbdEarlyDrops==1 && gMouseEarlyDrops==1);
        assert(nodes.empty() && calls.empty() && lockAlloc==0 && consoleBytes==0);
        assert(pd_hid_kbd_open(0,0,0,nullptr)==ENXIO && !PDHIDKeyboardIsGrabbed());
        return 0;
    } else if(test=="push-concurrent") {
        PDHIDPublishKeyboardDevice(); drain(); std::vector<std::thread> threads;
        for(int i=0;i<32;++i) threads.emplace_back([] { PDHIDPushKeyboardEvent(0xe1,true); });
        for(auto &t:threads) t.join();
        for(unsigned i=1;i<=32;++i) {
            PDHIDKbdEvent e{}; uio u{(char *)&e,sizeof(e)};
            assert(pd_hid_kbd_read(0,&u,0)==0 && e.sequence==i && e.usage==0xe1);
        }
    } else if(test=="capabilities" || test=="capabilities-refused") {
        OSNumber page(1),keyboard(6),mouse(2),digitizerPage(13),badUsage(5);
        OSDictionary k,m,bad; OSArray pairs;
        k.values={{"page",&page},{"usage",&keyboard}};
        m.values={{"page",&page},{"usage",&mouse}};
        bad.values={{"page",&digitizerPage},{"usage",&badUsage}};
        pairs.values={&k,&m}; pdHIDPublishSupportedQueues(false,&pairs); drain();
        assert(nodes.empty() && calls.empty());
        if(test=="capabilities-refused") {
            pairs.values={&bad,&page}; pdHIDPublishSupportedQueues(true,&pairs); drain();
            assert(nodes.empty() && calls.empty()); return 0;
        }
        pdHIDPublishSupportedQueues(true,&pairs); drain();
        assert(nodes.size()==2); emptyKeyboard(); emptyMouse();
    } else { assert(!"unknown case"); }
    once(); return 0;
}
