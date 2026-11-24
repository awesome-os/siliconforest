#!/usr/bin/env python3
"""Actual waitResponse body with fake transport/time; no IOKit/hardware proof."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

HERE=pathlib.Path(__file__).resolve().parent

# Compile the native method verbatim with minimal host peers. Portable request
# encoding, response parsing, CRC checks and pairing/claim helpers are real C.
# Hardware, time and failure teardown are fakes: no target/IOKit is executed.
PREFIX=r'''
#include "m1kbd_hid.h"
#include "m1kbd_hw.h"
#include <array>
#include <cassert>
#include <cstring>
#include <vector>
using Packet=std::array<uint8_t,DLSP_PACKET_BYTES>;
static uint64_t nowUS=0;
static void IOSleep(unsigned ms) { nowUS+=1000*ms; }
struct PDM1KeyboardHW { static uint64_t clockUS() { return nowUS; } };
struct FakeHW {
    unsigned writes=0;
    int error=0;
    Packet sent{};
    int writePacket(const uint8_t *p) {
        ++writes; std::memcpy(sent.data(),p,sent.size()); return error;
    }
};
struct PDM1SPIHID {
    FakeHW hardware;
    uint8_t usedRequestKeys=0, nextID=0;
    uint8_t pendingReport=0, pendingSubdevice=0, requestID=0;
    uint8_t packet[DLSP_PACKET_BYTES]{};
    bool requestPending=false;
    uint64_t otherMessages=0;
    std::vector<Packet> incoming;
    size_t consumed=0;
    unsigned reads=0;
    int error=0;
    const char *phase="";
    dlsp_state state{};
    void fail(int e,const char *p) { error=e; phase=p; }
    int receive(dlsp_message *m) {
        ++reads;
        if (consumed==incoming.size()) return DLSP_WAIT;
        const auto &p=incoming[consumed++];
        int e=dlsp_push(&state,p.data(),p.size(),m);
        if (e==DLSP_INVALID) fail(M1KBD_INVALID,"packet-or-message");
        return e;
    }
    bool waitResponse(uint8_t,uint8_t,uint16_t,dlsp_message *);
};
static Packet response(uint8_t report,uint8_t sub,uint8_t id=0,
                       uint8_t device=0xd0,uint8_t cmd=0x20,uint16_t rsplen=0) {
    Packet p{};
    const uint8_t payload=0xa5; // Synthetic opaque payload, not metadata/key evidence.
    assert(dlsp_request(device,cmd,report,sub,id,rsplen,&payload,1,p.data())==0);
    return p;
}
static void outerCRC(Packet &p) {
    auto crc=dlsp_crc16(p.data(),254); p[254]=uint8_t(crc); p[255]=uint8_t(crc>>8);
}
'''

SUFFIX=r'''
int main(int argc,char **argv) {
    assert(argc==2);
    const char *test=argv[1];
    dlsp_message m{};
    if (!std::strcmp(test,"non_echo")) {
        PDM1SPIHID h;
        for (auto key: std::array<std::array<uint8_t,2>,3>{{{{1,0xd0}},{{2,1}},{{0x10,1}}}}) {
            h.incoming.push_back(response(key[0],key[1],0));
            assert(h.waitResponse(key[0],key[1],key[0]==1?0:512,&m));
            assert(m.id==0 && m.response_length==0 && !h.requestPending);
            assert(h.requestID==h.hardware.sent[11]);
            assert(h.requestID==h.hardware.writes-1);
        }
        assert(h.usedRequestKeys==7 && h.nextID==3 && h.hardware.writes==3);
    } else if (!std::strcmp(test,"no_write")) {
        PDM1SPIHID h;
        assert(!h.waitResponse(2,0xd0,512,&m));
        assert(!h.usedRequestKeys && !h.nextID && !h.hardware.writes);
        assert(!std::strcmp(h.phase,"request-key"));
        h.incoming.push_back(response(2,1));
        assert(h.waitResponse(2,1,512,&m));
        auto previous=h.hardware.sent;
        assert(!h.waitResponse(2,1,512,&m));
        assert(h.usedRequestKeys==2 && h.nextID==1 && h.hardware.writes==1);
        assert(previous==h.hardware.sent && h.consumed==1);
    } else if (!std::strcmp(test,"failure_burns_key")) {
        for (unsigned kind=0;kind<3;kind++) {
            PDM1SPIHID h;
            if (kind==1) h.hardware.error=M1KBD_IO;
            if (kind==2) { auto p=response(0x10,1); p[254]^=1; h.incoming.push_back(p); }
            assert(!h.waitResponse(0x10,1,kind==0?513:512,&m));
            assert(h.usedRequestKeys==4 && h.nextID==1);
            assert(h.hardware.writes==(kind==0?0u:1u));
            const auto writes=h.hardware.writes;
            assert(!h.waitResponse(0x10,1,512,&m));
            assert(h.hardware.writes==writes && h.nextID==1 && h.usedRequestKeys==4);
        }
    } else if (!std::strcmp(test,"first_response")) {
        for (unsigned field=0;field<4;field++) {
            PDM1SPIHID h;
            auto wrong=response(field==0?1:2,field==1?2:1,0,field==2?1:0xd0,field==3?0x21:0x20);
            h.incoming={wrong,response(2,1)};
            assert(!h.waitResponse(2,1,512,&m));
            assert(!std::strcmp(h.phase,"response-identity"));
            assert(h.consumed==1 && h.hardware.writes==1 && h.usedRequestKeys==2);
        }
    } else if (!std::strcmp(test,"background")) {
        PDM1SPIHID h;
        auto input=response(1,0,99,1,0x10); input[0]=0x20; outerCRC(input);
        h.incoming={input,response(2,1,0)};
        assert(h.waitResponse(2,1,512,&m));
        assert(h.otherMessages==1 && h.consumed==2 && h.hardware.writes==1);
    } else if (!std::strcmp(test,"deadline")) {
        for (bool input: {false,true}) {
            PDM1SPIHID h;
            if (input) {
                auto p=response(1,0,0,1,0x10); p[0]=0x20; outerCRC(p);
                h.incoming.assign(1001,p);
            }
            nowUS=0;
            assert(!h.waitResponse(2,1,512,&m));
            assert(!std::strcmp(h.phase,"response-timeout"));
            assert(h.reads==1000 && nowUS<=1000000 && h.hardware.writes==1);
            assert(h.usedRequestKeys==2 && h.nextID==1);
            assert(!h.waitResponse(2,1,512,&m));
            assert(h.hardware.writes==1 && h.reads==1000);
        }
    } else { assert(false); }
}
'''


class RequestPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='m1kbd-request-policy-')
        temp=pathlib.Path(cls.temp.name)
        source=(HERE/'PDM1SPIHID.cpp').read_text()
        body=source[source.index('bool PDM1SPIHID::waitResponse('):
                    source.index('bool PDM1SPIHID::enumerate()')]
        cpp=temp/'wait.cpp'; cpp.write_text(PREFIX+body+SUFFIX)
        flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all']
        objects=[]
        for name in ('m1kbd_hid.c','native_protocol/dl_spi_hid.c'):
            obj=temp/(pathlib.Path(name).stem+'.o'); objects.append(str(obj))
            subprocess.run([shutil.which('cc'),'-std=c11',*flags,'-c',str(HERE/name),'-o',str(obj)],check=True)
        cls.exe=temp/'wait-policy'
        subprocess.run([shutil.which('c++'),'-std=c++17',*flags,'-I',str(HERE),str(cpp),
                        *objects,'-o',str(cls.exe)],check=True)

    @classmethod
    def tearDownClass(cls): cls.temp.cleanup()

    def case(self,name):
        p=subprocess.run([str(self.exe),name],capture_output=True,text=True,timeout=10)
        self.assertEqual(p.returncode,0,p.stderr)

    def test_sequential_non_echo_ids_match_semantic_keys(self): self.case('non_echo')
    def test_unknown_and_duplicate_keys_cannot_write_or_increment_id(self): self.case('no_write')
    def test_encode_transport_and_crc_failures_burn_the_key(self): self.case('failure_burns_key')
    def test_first_wrong_response_stops_without_searching_for_a_later_match(self): self.case('first_response')
    def test_background_input_does_not_complete_request(self): self.case('background')
    def test_deadline_and_poll_cap_survive_background_input_without_retries(self): self.case('deadline')


if __name__=='__main__': unittest.main()
