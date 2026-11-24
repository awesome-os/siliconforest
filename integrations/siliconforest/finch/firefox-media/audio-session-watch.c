/* SPDX-License-Identifier: MIT */
/* Read-only metadata. Caller admits the own client PID/start identity and the
 * exact installed SiliconForest daemon. No suspension, process writes, audio-port
 * duplication or PCM reads. Retain no client task port while waiting for exit. */
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/task_info.h>
#include <mach/vm_region.h>
#include <mach-o/loader.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Own declaration of the exact source-reviewed daemon layout, not a public ABI.
 * The UUID and writable segment guard must match before reading this table. */
struct client {uint32_t session,wake;uint64_t ring,size;uint32_t device;double phase;};
struct ring {uint32_t version,channels,frames,period,running,rate;uint64_t write,read,host;};
_Static_assert(sizeof(struct client)==40,"qualified daemon client layout");
_Static_assert(sizeof(struct ring)==48,"PD ring v2 metadata only");
static double seconds(clockid_t clock){struct timespec t;clock_gettime(clock,&t);return t.tv_sec+t.tv_nsec/1e9;}
static int read_memory(task_t task,uint64_t address,void *out,size_t size){
 mach_vm_size_t copied=0;if(address<0x100000000ULL||size>65536)return -1;
 return mach_vm_read_overwrite(task,address,size,(mach_vm_address_t)out,&copied)||copied!=size?-1:0;
}
static int object(task_t task,uint64_t address,uint64_t *identity,uint64_t *offset){
 mach_vm_address_t base=address;mach_vm_size_t size=0;natural_t depth=0;
 for(unsigned i=0;i<16;i++){
  vm_region_submap_info_data_64_t info={0};mach_msg_type_number_t n=VM_REGION_SUBMAP_INFO_V2_COUNT_64;
  kern_return_t status=mach_vm_region_recurse(task,&base,&size,&depth,(vm_region_recurse_info_t)&info,&n);
  if(status||base>address||address-base>=size)return -1;
  if(info.is_submap){depth++;continue;}
  if(n<VM_REGION_SUBMAP_INFO_V2_COUNT_64||!info.object_id_full)return -1;
  *identity=info.object_id_full;*offset=info.offset+address-base;return 0;
 }
 return -1;
}
static int image(task_t task,const char *uuid,uint64_t offset,uint64_t *table){
 task_dyld_info_data_t info={0};mach_msg_type_number_t n=TASK_DYLD_INFO_COUNT;
 struct {uint32_t version,count;uint64_t array;} all;
 struct {uint64_t address,path,modified;} images[128];
 if(task_info(task,TASK_DYLD_INFO,(task_info_t)&info,&n)||info.all_image_info_format!=TASK_DYLD_ALL_IMAGE_INFO_64||
    read_memory(task,info.all_image_info_addr,&all,sizeof(all))||!all.count||all.count>128||
    read_memory(task,all.array,images,all.count*sizeof(images[0])))return -1;
 for(unsigned i=0;i<all.count;i++){
  char path[128]={0};size_t amount=sizeof(path)-1;
  size_t page=4096-(images[i].path&4095);if(amount>page)amount=page;
  if(read_memory(task,images[i].path,path,amount)||strcmp(path,"/usr/sbin/coreaudiod"))continue;
  struct mach_header_64 header;
  if(read_memory(task,images[i].address,&header,sizeof(header))||header.magic!=MH_MAGIC_64||header.filetype!=MH_EXECUTE||
     !header.ncmds||header.ncmds>128||header.sizeofcmds>65536||header.sizeofcmds<8)return -1;
  unsigned char *commands=malloc(header.sizeofcmds);if(!commands)return -1;
  if(read_memory(task,images[i].address+sizeof(header),commands,header.sizeofcmds)){free(commands);return -1;}
  size_t cursor=0;int matched=0,writable=0;uint64_t preferred=0;
  /* First establish the executable's preferred text address. */
  for(unsigned k=0;k<header.ncmds;k++){
   if(cursor+8>header.sizeofcmds)break;
   struct load_command *c=(void *)(commands+cursor);
   if(c->cmdsize<8||c->cmdsize>header.sizeofcmds-cursor)break;
   if(c->cmd==LC_SEGMENT_64&&c->cmdsize>=sizeof(struct segment_command_64)){
    struct segment_command_64 *s=(void *)c;if(!strncmp(s->segname,"__TEXT",16))preferred=s->vmaddr;
   }
   cursor+=c->cmdsize;
  }
  cursor=0;
  for(unsigned k=0;k<header.ncmds;k++){
   if(cursor+8>header.sizeofcmds)break;
   struct load_command *c=(void *)(commands+cursor);
   if(c->cmdsize<8||c->cmdsize>header.sizeofcmds-cursor)break;
   if(c->cmd==LC_UUID&&c->cmdsize>=sizeof(struct uuid_command)){
    char actual[33];struct uuid_command *u=(void *)c;
    for(unsigned q=0;q<16;q++)snprintf(actual+2*q,3,"%02x",u->uuid[q]);
    matched=!strcmp(actual,uuid);
   }
   if(c->cmd==LC_SEGMENT_64&&c->cmdsize>=sizeof(struct segment_command_64)&&preferred){
    struct segment_command_64 *s=(void *)c;
    uint64_t wanted=preferred+offset;
    if(wanted>=s->vmaddr&&wanted-s->vmaddr<=s->vmsize&&sizeof(struct client)*64<=s->vmsize-(wanted-s->vmaddr)&&
       (s->initprot&VM_PROT_WRITE))writable=1;
   }
   cursor+=c->cmdsize;
  }
  free(commands);
  if(!matched||!writable||!preferred||offset>0x1000000)return -1;
  *table=images[i].address+offset;return 0;
 }
 return -1;
}
static int self_test(void){
 task_t self=mach_task_self();mach_vm_address_t first=0,alias=0;mach_vm_size_t size=16384;
 memory_object_size_t entry_size=size;mach_port_t entry=MACH_PORT_NULL;int status=1;
 if(mach_vm_allocate(self,&first,size,VM_FLAGS_ANYWHERE))return 1;
 if(mach_make_memory_entry_64(self,&entry_size,first,VM_PROT_READ|VM_PROT_WRITE,&entry,MACH_PORT_NULL))goto out;
 if(mach_vm_map(self,&alias,size,0,VM_FLAGS_ANYWHERE,entry,0,FALSE,VM_PROT_READ|VM_PROT_WRITE,
    VM_PROT_READ|VM_PROT_WRITE,VM_INHERIT_NONE))goto out;
 uint64_t a=0,b=0,ao=0,bo=0;
 status=object(self,first,&a,&ao)||object(self,alias,&b,&bo)||a!=b||ao!=bo;
 printf("{\"self_test\":%s,\"same_shared_object\":%s,\"server_opened\":false,\"pcm_read\":false}\n",status?"false":"true",status?"false":"true");
out:
 if(alias)mach_vm_deallocate(self,alias,size);
 if(entry)mach_port_deallocate(self,entry);
 mach_vm_deallocate(self,first,size);return status;
}
int main(int argc,char **argv){
 if(argc==2&&!strcmp(argv[1],"--self-test"))return self_test();
 if(argc!=7){fprintf(stderr,"usage: audio-session-watch server-pid client-table-offset uuid32 owned-client-pid own-ring-address seconds\n");return 2;}
 char *end;long server=strtol(argv[1],&end,10);if(*end||server<=1||server>INT32_MAX)return 2;
 uint64_t offset=strtoull(argv[2],&end,0);if(*end||offset>0x1000000||strlen(argv[3])!=32)return 2;
 long client=strtol(argv[4],&end,10);if(*end||client<=1||client>INT32_MAX||client==server)return 2;
 uint64_t own=strtoull(argv[5],&end,0);if(*end||own<0x100000000ULL)return 2;
 long duration=strtol(argv[6],&end,10);if(*end||duration<1||duration>30)return 2;
 task_t st=MACH_PORT_NULL,ct=MACH_PORT_NULL;uint64_t table=0,identity=0,own_offset=0;int result=1;
 if(task_for_pid(mach_task_self(),server,&st)||image(st,argv[3],offset,&table)||
    task_for_pid(mach_task_self(),client,&ct)||object(ct,own,&identity,&own_offset))goto finish;
 struct client clients[64];int selected=-1;
 if(read_memory(st,table,clients,sizeof(clients)))goto finish;
 for(unsigned i=0;i<64;i++){
  if(!clients[i].session||!clients[i].ring||clients[i].size<48||clients[i].size>67108912)continue;
  uint64_t candidate=0,position=0;
  if(object(st,clients[i].ring,&candidate,&position)||candidate!=identity||position!=own_offset)continue;
  if(selected!=-1)goto finish;selected=i;
 }
 /* Do not retain the client's task or any audio/memory-entry right over exit. */
 mach_port_deallocate(mach_task_self(),ct);ct=MACH_PORT_NULL;
 if(selected<0)goto finish;
 struct client original=clients[selected];struct ring ring;
 if(read_memory(st,original.ring,&ring,sizeof(ring))||ring.version!=2||ring.channels!=2||ring.rate!=48000||
    ring.frames!=2048||ring.period!=512||!ring.write||!ring.read)goto finish;
 printf("{\"event\":\"matched\",\"server_pid\":%ld,\"client_pid\":%ld,\"slot\":%d,\"same_shared_object\":true,\"monotonic\":%.9f,\"epoch\":%.9f,\"write\":%llu,\"read\":%llu,\"client_task_released\":true,\"pcm_read\":false}\n",
  server,client,selected,seconds(CLOCK_MONOTONIC),seconds(CLOCK_REALTIME),(unsigned long long)ring.write,(unsigned long long)ring.read);fflush(stdout);
 double deadline=seconds(CLOCK_MONOTONIC)+duration,first_empty=0,first_empty_epoch=0,last_bound=0;
 while(seconds(CLOCK_MONOTONIC)<deadline){
  struct client current;
  if(read_memory(st,table+sizeof(current)*selected,&current,sizeof(current)))goto finish;
  int empty=1;for(unsigned i=0;i<sizeof(current);i++)empty&=((unsigned char *)&current)[i]==0;
  if(empty){
   if(!first_empty){first_empty=seconds(CLOCK_MONOTONIC);first_empty_epoch=seconds(CLOCK_REALTIME);}
   else{
    printf("{\"event\":\"removed\",\"slot\":%d,\"slot_empty\":true,\"stable_empty_samples\":2,\"last_bound_monotonic\":%.9f,\"first_empty_monotonic\":%.9f,\"first_empty_epoch\":%.9f,\"monotonic\":%.9f,\"epoch\":%.9f,\"pcm_read\":false}\n",
     selected,last_bound,first_empty,first_empty_epoch,seconds(CLOCK_MONOTONIC),seconds(CLOCK_REALTIME));result=0;goto finish;
   }
  }else{
   first_empty=0;
   if(current.session==original.session&&current.ring==original.ring)last_bound=seconds(CLOCK_MONOTONIC);
   else goto finish; /* A transition or immediate reuse is not an empty-slot proof. */
  }
  usleep(10000);
 }
finish:
 if(ct)mach_port_deallocate(mach_task_self(),ct);
 if(st)mach_port_deallocate(mach_task_self(),st);
 if(result)fprintf(stderr,"AUDIO_SESSION_WATCH_UNQUALIFIED: identity/layout/query/deadline failure; no removal claim\n");
 return result;
}
