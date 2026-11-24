#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Execute actual catalogue policy in network-only and opt-in USB builds.

Metadata checks do not establish actual matching, binding or hardware support.
"""
from pathlib import Path
import os,re,subprocess,tempfile
here=Path(__file__).parents[1];s=(here/'kernel/SFCIOPCIEnumeration.cpp').read_text()
m=re.search(r'int SFCIOPCIEnumeration::catalogueSafe\(\)\s*\{',s);assert m
end,depth=m.end(),1
while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
function=s[m.start():end]
fixture=r'''
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
using SInt32=int32_t;using UInt32=uint32_t;
struct OSObject {unsigned refs=1;virtual ~OSObject()=default;virtual void retain(){++refs;}virtual void release(){if(!--refs)delete this;}};
struct OSString:OSObject {std::string value;static OSString *withCString(const char *s){auto p=new OSString;p->value=s;return p;}bool isEqualTo(const char *s){return value==s;}};
struct Bool:OSObject {void retain()override{}void release()override{}}yes,no;
static OSObject *kOSBooleanTrue=&yes;
struct OSDictionary:OSObject {
 std::map<std::string,OSObject *> entries;
 static OSDictionary *withCapacity(unsigned){return new OSDictionary;}
 ~OSDictionary(){for(auto &p:entries)p.second->release();}
 bool setObject(const char *key,OSObject *v){auto &old=entries[key];if(old)old->release();v->retain();old=v;return true;}
 OSObject *getObject(const char *key){auto it=entries.find(key);return it==entries.end()?nullptr:it->second;}
};
struct OSOrderedSet:OSObject {std::vector<OSObject *> objects;~OSOrderedSet(){for(auto p:objects)p->release();}unsigned getCount(){return objects.size();}OSObject *getObject(unsigned n){return objects[n];}};
#define OSDynamicCast(t,p) dynamic_cast<t *>(p)
#define OSSafeReleaseNULL(p) do{if(p){(p)->release();(p)=nullptr;}}while(0)
struct Catalogue {OSOrderedSet *drivers=nullptr;OSOrderedSet *findDrivers(OSDictionary *m,SInt32 *g){auto p=OSDynamicCast(OSString,m->getObject("IOProviderClass"));assert(p && p->value=="IOPCIDevice");*g=43;if(drivers)drivers->retain();return drivers;}};
static Catalogue *gIOCatalogue;
struct Owner {template<class...T>void setProperty(T...){}};
struct SFCIOPCIEnumeration {Owner storage,*owner=&storage;int catalogueSafe();};
FUNCTION
static void text(OSDictionary *d,const char *key,const char *value){auto s=OSString::withCString(value);d->setObject(key,s);s->release();}
static OSDictionary *entry(OSOrderedSet &set,const char *cls,const char *bundle,const char *id=nullptr,bool tunnel=false){auto d=OSDictionary::withCapacity(4);text(d,"IOClass",cls);text(d,"CFBundleIdentifier",bundle);if(id)text(d,"IOPCIPrimaryMatch",id);if(tunnel)d->setObject("IOPCITunnelCompatible",kOSBooleanTrue);set.objects.push_back(d);return d;}
struct Fixture {OSOrderedSet *set=new OSOrderedSet;Catalogue catalogue;SFCIOPCIEnumeration e;Fixture(){catalogue.drivers=set;gIOCatalogue=&catalogue;for(unsigned n=0;n<3;++n)entry(*set,"IOPCI2PCIBridge","com.apple.iokit.IOPCIFamily");entry(*set,"SFBwfm","org.siliconforest.driver.SFBwfm","0x442514e4");entry(*set,"SFFBSDem","org.siliconforest.driver.SFFBSDem","0x15338086",true);}~Fixture(){set->release();}OSDictionary *usb(){return entry(*set,"RavynXHCIPort","com.ravynos.driver.RavynXHCIPort","0x11001b73",true);}};
int main(){
 {Fixture f;assert(f.e.catalogueSafe()==(SF_CIO_DOCK_USB?ENODEV:0));}
 {Fixture f;f.usb();assert(f.e.catalogueSafe()==(SF_CIO_DOCK_USB?0:EOPNOTSUPP));}
 {Fixture f;auto d=f.usb();text(d,"IOPCIPrimaryMatch","0x12421b21");assert(f.e.catalogueSafe()==EOPNOTSUPP);}
 {Fixture f;auto d=f.usb();text(d,"IOPCIClassMatch","0x0c033000");assert(f.e.catalogueSafe()==EOPNOTSUPP);}
 {Fixture f;auto d=f.usb();d->setObject("IOPCITunnelCompatible",&no);assert(f.e.catalogueSafe()==EOPNOTSUPP);}
 {Fixture f;f.usb();f.usb();assert(f.e.catalogueSafe()==(SF_CIO_DOCK_USB?ENODEV:EOPNOTSUPP));}
 {Fixture f;f.usb();entry(*f.set,"other","other","0x12341234");assert(f.e.catalogueSafe()==EOPNOTSUPP);}
 {SFCIOPCIEnumeration e;gIOCatalogue=nullptr;assert(e.catalogueSafe()==ENODEV);}
 puts("actual catalogue policy: network-only rejects USB, dedicated profile requires unique exact FL1100/tunnel match and refuses generic/extra personalities PASS (metadata fixtures only)");
}
'''.replace('FUNCTION',function)
with tempfile.TemporaryDirectory() as tmp:
 tmp=Path(tmp);(tmp/'test.cpp').write_text(fixture)
 for enabled in [0,1]:
  subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-O1','-g','-fsanitize=address,undefined','-DSF_CIO_I210_NETWORK=1',f'-DSF_CIO_DOCK_USB={enabled}',str(tmp/'test.cpp'),'-o',str(tmp/'test')],check=True)
  subprocess.run([str(tmp/'test')],check=True)
