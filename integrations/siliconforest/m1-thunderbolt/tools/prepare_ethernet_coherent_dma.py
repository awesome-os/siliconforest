#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Preserve coherent native allocations and order the private DMA copy seam.

Only the dedicated I210 staging tree is modified; upstream notices remain.
"""
from pathlib import Path
import sys
root=Path(sys.argv[1])
def replace(s,old,new):
    if s.count(old)!=1:raise SystemExit('coherent Ethernet source drift: '+old[:90])
    return s.replace(old,new)
p=root/'src/Kernel/fbsdkpi/xnu/sf_fbsd_os.cpp';s=p.read_text()
start=s.index('sfos_malloc_contig(size_t size,');brace=s.index('{',start);end,depth=brace+1,1
while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
allocation=s[start:end]
allocation=replace(allocation,'sfos_malloc_contig(size_t size, size_t align, uint64_t maxaddr, uint64_t *phys)\n{', '''sfos_malloc_contig(size_t size, size_t align, uint64_t maxaddr, uint64_t *phys)
{
    // Native allocation identity owns every translated page, including padding.
    if (!size || size > UINT64_MAX - 0x3fff) return NULL;
    size = (size + 0x3fff) & ~(size_t)0x3fff;''')
allocation=replace(allocation,'\tmask &= ~(mach_vm_address_t)(align - 1);','\tif (mask > 0x0000000fffffffffULL) mask = 0x0000000fffffffffULL;\n\tmask &= ~(mach_vm_address_t)(align - 1);')
allocation=replace(allocation,'\tif (buffer->prepare() != kIOReturnSuccess) {','\tbzero(buffer->getBytesNoCopy(), buffer->getLength());\n\tif (buffer->prepare() != kIOReturnSuccess) {')
s=s[:start]+allocation+s[end:]
s=replace(s,' bool poisoned;\n};',' bool poisoned;\n bool direct; // Actual retained allocator object, never physical-address DMA.\n};')
s=replace(s,' c->source->release();c->device->release();IOFree(c,sizeof(*c));',' OSSafeReleaseNULL(c->source);c->device->release();IOFree(c,sizeof(*c));')
start=s.index('static void *sf_owned_map(');brace=s.index('{',start);end,depth=brace+1,1
while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
new=r'''static uint64_t sf_owned_direct_maps,sf_owned_bounce_maps;
static IOBufferMemoryDescriptor *sf_owned_contiguous(uint64_t phys,uint64_t len)
{
 IOBufferMemoryDescriptor *owned=NULL;
 lck_mtx_lock(sListLock);unsigned visited=0;
 for(auto m=sMappings;m && ++visited<=4096;m=m->next){
  if(!m->buffer || m->map || m->desc)continue;
  IOByteCount span=0;UInt64 pa=m->buffer->getPhysicalSegment(0,&span,kIOMemoryMapperNone);
  UInt64 bytes=m->buffer->getLength();
  if(pa!=phys || !bytes || (bytes&0x3fff) || len>bytes || span<bytes)continue;
  owned=m->buffer;owned->retain();break;
 }
 lck_mtx_unlock(sListLock);return owned;
}
static void *sf_owned_map(IOService *device,IOMapper *mapper,uint64_t phys,uint64_t len,
 int bits,uint64_t *busaddr)
{
 const uint64_t page=0x4000;
 if(device->getProperty("SFPCIDMAPoisoned")==kOSBooleanTrue || len>UINT64_MAX-(page-1)){mapper->release();return NULL;}
 auto c=(sf_dma_cookie *)IOMallocZero(sizeof(sf_dma_cookie));
 if(!c){mapper->release();return NULL;}
 // Coherent rings must retain/map the exact allocator-owned pages. Copying
 // to a second buffer breaks CPU/device sharing even when sync is omitted.
 c->owned=sf_owned_contiguous(phys,len);c->direct=c->owned!=NULL;
 if(!c->direct){
  c->source=IOMemoryDescriptor::withPhysicalAddress(phys,len,kIODirectionInOut);
  c->owned=IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
   kIODirectionInOut|kIOMemoryPhysicallyContiguous,(len+page-1)&~(page-1),page);
 }
 c->cmd=IODMACommand::withSpecification(kIODMACommandOutputHost64,bits,0,
  IODMACommand::kMapped,0,1,mapper,NULL);mapper->release();
 if((!c->direct && !c->source) || !c->owned || !c->cmd){
  OSSafeReleaseNULL(c->source);OSSafeReleaseNULL(c->owned);OSSafeReleaseNULL(c->cmd);IOFree(c,sizeof(*c));return NULL;
 }
 device->retain();c->device=device;c->bounce_len=len;
 if(!c->direct){
  bzero(c->owned->getBytesNoCopy(),c->owned->getLength());
  if(c->source->readBytes(0,c->owned->getBytesNoCopy(),len)!=len){
   c->cmd->release();c->owned->release();c->source->release();device->release();IOFree(c,sizeof(*c));return NULL;
  }
 }
 // Every path still uses the actual mapper/command. Ambiguous preparation
 // retains original ownership, including any caller-freed coherent buffer.
 IOReturn ret=c->cmd->setMemoryDescriptor(c->owned,true);
 if(ret!=kIOReturnSuccess){sf_owned_poison(c,"prepare",ret);return NULL;}
 UInt64 offset=0;UInt32 count=1;IODMACommand::Segment64 segment;
 ret=c->cmd->gen64IOVMSegments(&offset,&segment,&count);
 if(ret!=kIOReturnSuccess || count!=1 || segment.fLength<c->owned->getLength() ||
  !segment.fIOVMAddr || segment.fIOVMAddr>UINT32_MAX ||
  c->owned->getLength()-1>UINT32_MAX-segment.fIOVMAddr){
  sf_owned_poison(c,"segments",ret?ret:kIOReturnUnderrun);return NULL;
 }
 uint64_t cumulative=__atomic_add_fetch(c->direct?&sf_owned_direct_maps:&sf_owned_bounce_maps,1,__ATOMIC_RELAXED);
 device->setProperty(c->direct?"SFI210OwnedDirectMapCount":"SFI210OwnedBounceMapCount",cumulative,64);
 *busaddr=segment.fIOVMAddr;return c;
}'''
s=s[:start]+new+s[end:]
needle=' if(cookie->owned){\n  if(cookie->poisoned)return;\n  if(to_device'
s=replace(s,needle,''' if(cookie->owned){
  if(cookie->poisoned)return;
  if(cookie->direct){
   IOReturn ret=cookie->cmd->synchronize(to_device?kIODirectionOut:kIODirectionIn);
   if(ret!=kIOReturnSuccess)sf_owned_poison(cookie,"synchronize-direct",ret);
   return;
  }
  if(to_device''')
p.write_text(s)
p=root/'src/Kernel/fbsdkpi/haiku/freebsd_network/bus_dma.cpp';s=p.read_text()
s=replace(s,'''\t\tcase bus_dmamap::BUFFER_NONE:
\t\tcase bus_dmamap::BUFFER_PROHIBITED:
\t\t\t// Nothing to do.
\t\t\treturn;''','''\t\tcase bus_dmamap::BUFFER_NONE:
\t\tcase bus_dmamap::BUFFER_PROHIBITED:
            // No Haiku bounce, but native mapper synchronization still applies.
            bus_dmamap_sync_etc(dmat,map,0,0,op);
\t\t\treturn;''')
block='''\tif ((op & (BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD)) != 0) {
\t\tfor (int i = 0; i < map->sf_ncookies; i++)
\t\t\tpdos_dma_sync(map->sf_cookies[i], 1);
\t}

'''
s=replace(s,block,'')
needle='\tif ((op & BUS_DMASYNC_POSTREAD) != 0) {'
s=replace(s,needle,'''    // Publish CPU -> Haiku bounce before Haiku -> native DMA backing.
'''+block+needle)
p.write_text(s)
