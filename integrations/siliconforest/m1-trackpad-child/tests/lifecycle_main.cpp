/* Included after the real extracted parent bodies and complete child TU. */
static bool badAcquisition=false;
static std::vector<dlsp_message> incoming;
bool SFM1SPIHID::handleStart(IOService *)
{
    /* Explicit enumeration fake: no MMIO, device or input evidence. */
    if(Faults::hit("enumeration"))return false;
    identity={0x05ac,0x0341,0x175,10,33}; descriptorLength=90;
    std::memset(descriptor,0,descriptorLength);
    trackpadInterface.interface=2;trackpadInterface.country=33;
    trackpadEnumeration.result=0;trackpadEnumeration.stage=5;
    trackpadEnumeration.request_mask=31;
    trackpadEnumeration.descriptor_bytes=110;trackpadEnumeration.descriptor_payload_bytes=110;
    std::memcpy(trackpadEnumeration.descriptor,dl_m1_tp_descriptor_reference,110);
    if(badAcquisition)trackpadEnumeration.descriptor[109]^=1;
    return true;
}
int SFM1SPIHID::receive(dlsp_message *m)
{
    if(incoming.empty())return DLSP_WAIT;
    *m=incoming.front();incoming.erase(incoming.begin());return DLSP_MESSAGE;
}

static void reset()
{
    Faults::reset();onRegister=nullptr;onReport=nullptr;registrations.clear();incoming.clear();
    badAcquisition=false;bootargs.clear();gSFM1KeyboardProbeClaimed=0;
}
static SFM1SPIHID *make(bool t2=true)
{
    auto *provider=new IOService;auto *p=new SFM1SPIHID;
    assert(p->init());assert(p->attach(provider));p->t2Enabled=t2;
    return p;
}
static dl_m1_trackpad_child_state_v1 state(IORegistryEntry *p)
{
    auto *d=dynamic_cast<OSData *>(p->getProperty(DL_M1_TP_CHILD_STATE_PROPERTY));
    assert(d && d->bytes.size()==32);dl_m1_trackpad_child_state_v1 out;
    std::memcpy(&out,d->bytes.data(),32);return out;
}
static void assert_not_ready_if_present(IORegistryEntry *p)
{
    if(p && p->getProperty(DL_M1_TP_CHILD_STATE_PROPERTY))assert(!state(p).route_ready);
}
static void startup_sweep()
{
    reset();auto *p=make();Faults::reset();
    onRegister=[](IOService *s){
        auto *parent=dynamic_cast<SFM1SPIHID *>(s);
        if(parent)assert(parent->t2Committed());
        else assert(dynamic_cast<SFM1SPITrackpad *>(s)->committed());
        assert(s->handleOpen(nullptr,0,nullptr));
    };
    assert(p->start(p->provider));assert(p->t2Committed());
    assert(registrations.size()==2 && registrations[0]==p && registrations[1]==p->childOwned);
    assert(state(p->childOwned).route_ready==1);assert(p->childSink==p->childOwned);
    auto *number=dynamic_cast<OSNumber *>(p->childOwned->getProperty(DL_M1_TP_INTERFACE_PROPERTY));
    assert(number && number->bits==32 && number->value==2);
    const int steps=Faults::step;
    for(int fault=1;fault<=steps+8;fault++) {
        reset();auto *q=make();Faults::reset(fault);
        onRegister=[](IOService *s){
            auto *p=dynamic_cast<SFM1SPIHID *>(s);
            if(p)assert(p->t2Committed());
            else assert(dynamic_cast<SFM1SPITrackpad *>(s)->committed());
        };
        bool ok=q->start(q->provider);
        if(!ok) {
            assert(registrations.empty());assert(!q->t2Committed());
            assert(!q->childOwned && !q->childSink && !q->transportLoop);
            assert(q->baseStops<=1);
        } else {
            assert(q->t2Committed() && q->refs>1);
            if(q->childSink)assert(q->childOwned && state(q->childOwned).route_ready);
            else assert_not_ready_if_present(q);
            auto *loop=q->transportLoop;int closes=Faults::closes;
            q->stop(q->provider);q->handleStop(q->provider);q->disposeTransport();q->free();
            assert(q->transportLoop==loop && !loop->freed && !q->baseStops);
            assert(Faults::closes==closes && !q->freed);
        }
    }
    std::printf("startup failure sweep: %d boundaries\n",steps+8);
}
static void admission_and_fallback()
{
    reset();auto *p=make();IOUserClient *out=reinterpret_cast<IOUserClient *>(uintptr_t(1));
    assert(!p->handleOpen(nullptr,0,nullptr));
    assert(p->newUserClient(nullptr,nullptr,0,nullptr,&out)==kIOReturnNotReady && !out);
    assert(!p->baseOpens && !p->baseClients);
    badAcquisition=true;assert(p->start(p->provider));assert(p->t2Committed());
    assert(!p->childOwned && !p->childSink && registrations.size()==1);
    assert(state(p).faulted && state(p).last_error && !state(p).route_ready);
    reset();auto *legacy=make(false);
    onRegister=[](IOService *s){assert(s->handleOpen(nullptr,0,nullptr));};
    assert(legacy->handleOpen(nullptr,0,nullptr));
    assert(legacy->newUserClient(nullptr,nullptr,0,nullptr,&out)==0);
    assert(legacy->start(legacy->provider));assert(!legacy->t2Committed());
    assert(registrations.size()==1 && !legacy->childOwned);
    assert(!legacy->getProperty(kIOHIDRegisterServiceKey));
    reset();p=make();Faults::corruptProperty=true;
    assert(!p->start(p->provider) && registrations.empty());
    for(int which=0;which<4;which++) {
        reset();auto *q=new SFM1SPIHID;assert(q->init());
        bootargs={{"sfm1kbd",1},{"sfm1trackpadenum",1},{"sfm1trackpadchild",1}};
        if(which<3)bootargs[which==0?"sfm1kbd":which==1?"sfm1trackpadenum":"sfm1trackpadchild"]=0;
        assert((q->probe(nullptr,nullptr)!=nullptr)==(which==3));
        assert(gSFM1KeyboardProbeClaimed==(which==3?1u:0u));
    }
}
static void report_and_recursive_stop()
{
    reset();auto *p=make();assert(p->start(p->provider));auto *c=p->childOwned;
    IOMemoryDescriptor *d=nullptr;assert(c->newReportDescriptor(&d)==0);
    assert(d->bytes==std::vector<uint8_t>(dl_m1_tp_descriptor_reference,dl_m1_tp_descriptor_reference+110));
    std::memset(p->trackpadEnumeration.descriptor,0,110);
    assert(d->bytes==std::vector<uint8_t>(dl_m1_tp_descriptor_reference,dl_m1_tp_descriptor_reference+110));d->release();
    auto before=state(c);
    assert(c->getReport(nullptr,0,0)==kIOReturnUnsupported);
    assert(c->setReport(nullptr,0,0)==kIOReturnUnsupported);
    assert(c->getReport(nullptr,0,0,1,nullptr)==kIOReturnUnsupported);
    assert(c->setReport(nullptr,0,0,1,nullptr)==kIOReturnUnsupported);
    auto after=state(c);assert(!std::memcmp(&before,&after,sizeof(before)));
    assert(state(c).last_error==0 && state(c).route_ready==1);
    std::vector<uint8_t> bytes(1752,0x5a);bytes[0]=0x44;
    dlsp_message msg={2,0x20,0x10,0x99,0x83,0xfe,65535,bytes.data(),bytes.size()};
    auto *loop=p->transportLoop;int closes=Faults::closes;
    onReport=[&](IOHIDDevice *dev,IOMemoryDescriptor *report){
        assert(dev==c && report->bytes==bytes && Faults::gateDepth==1);
        std::fill(bytes.begin(),bytes.end(),0);assert(report->bytes[0]==0x44);
        c->stop(p);c->stop(p);assert(Faults::gateDepth==1); // recursive runAction returns
        assert(!p->childSink && !c->baseStops && c->baseReady);
        c->detach(p);assert(c->getWorkLoop()==nullptr && c->_workLoop==loop);
        p->stop(p->provider);assert(!p->baseStops && p->transportLoop==loop);
    };
    ++Faults::gateDepth;p->routeTrackpad(&msg);--Faults::gateDepth;
    assert(!p->t2Dispatches && p->childSubmitted==1 && !p->childSink);
    assert(!p->handleOpen(nullptr,0,nullptr) && !c->handleOpen(nullptr,0,nullptr));
    assert(!state(c).route_ready && state(c).stop_requested && !state(c).faulted && !state(c).last_error);
    c->stop(p);c->handleStop(p);assert(!c->baseStops); // zero dispatches, already detached
    assert(Faults::closes==closes && !loop->freed && !c->freed && !p->freed);
    c->stop(nullptr);assert(!c->getProperty(DL_M1_TP_CHILD_STATE_PROPERTY));
    assert(!c->baseStops && c->_workLoop==loop && c->getWorkLoop()==nullptr);
    p->free();c->free();assert(!p->freed && !c->freed);
    p->detach(p->provider);p->release();c->release();
    assert(p->refs>=1 && c->refs>=1 && !p->freed && !c->freed);
}
static void rejects_and_snapshot_failure()
{
    reset();auto *p=make();assert(p->start(p->provider));auto *c=p->childOwned;
    uint8_t b[9]={2};dlsp_message m={2,0x20,0x10,0,0,0,0,b,sizeof(b)};
    ++Faults::gateDepth;p->routeTrackpad(&m);--Faults::gateDepth;
    assert(p->childRejected==1 && state(c).route_ready && !state(c).last_error);
    m.payload_length=8;Faults::failAllData=true;
    ++Faults::gateDepth;p->childFault(-98765);--Faults::gateDepth;
    assert(!p->childSink && p->childState.last_error==-98765);
    assert(!c->getProperty(DL_M1_TP_CHILD_STATE_PROPERTY));
    assert(!p->getProperty(DL_M1_TP_CHILD_STATE_PROPERTY));
    assert(!p->t2Provider->getProperty(DL_M1_TP_CHILD_STATE_PROPERTY));
    assert(!c->admitting() && p->handleOpen(nullptr,0,nullptr)); // child-only fault
    Faults::failAllData=false;
    p->fail(-777,"host-fake-fault");
    assert(!p->handleOpen(nullptr,0,nullptr));
    assert(p->childState.last_error==-98765); // original cause preserved
}
static void stop_between_registrations()
{
    reset();auto *p=make();
    onRegister=[&](IOService *s){assert(s==p);p->stop(p->provider);};
    assert(p->start(p->provider));assert(p->t2Committed());
    assert(registrations.size()==1 && !p->childSink && !p->childOwned->registerCount);
    assert(!p->baseStops && !p->childOwned->baseStops && !p->childOwned->admitting());
    assert(!state(p->childOwned).route_ready && state(p->childOwned).stop_requested);
}
int main()
{
    startup_sweep();admission_and_fallback();report_and_recursive_stop();rejects_and_snapshot_failure();
    stop_between_registrations();
    std::puts("real extracted lifecycle/report bodies: HOST ONLY PASS");
}
