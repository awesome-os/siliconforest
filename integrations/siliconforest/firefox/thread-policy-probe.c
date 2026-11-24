/* SPDX-License-Identifier: MIT
 * Read-only kernel scheduling snapshot. Caller admits an owned PID/lstart pair.
 * Never suspends a task or writes a policy, affinity or frequency setting.
 */
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/thread_info.h>
#include <mach/thread_policy.h>
#include <mach/task_policy.h>
#include <dlfcn.h>
#include <sys/resource.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
/* Exact pinned XNU osfmk/mach/thread_policy_private.h public RPC layout. */
struct qos_policy {integer_t tier,importance;};
static void string(const char *s,size_t maximum){
 putchar('"');
 for(size_t i=0;i<maximum&&s[i];i++){
  unsigned char c=(unsigned char)s[i];
  if(c=='"'||c=='\\')printf("\\%c",c);
  else if(c<32||c>126)printf("\\u%04x",c);
  else putchar(c);
 }
 putchar('"');
}
int main(int argc,char **argv){
 int read_array=argc==4&&!strcmp(argv[2],"--read-four-u64");
 char *end=NULL;long value=(argc==2||read_array)?strtol(argv[1],&end,10):0;
 if(value<=1||value>INT32_MAX||!end||*end){fprintf(stderr,"usage: thread-policy-probe <owned-pid>\n");return 2;}
 pid_t pid=(pid_t)value;mach_port_t task=MACH_PORT_NULL;int task_status=task_for_pid(mach_task_self(),pid,&task);
 errno=0;int nice=getpriority(PRIO_PROCESS,pid),nice_error=errno;
 printf("{\"pid\":%d,\"read_only\":true,\"task_suspended\":false,\"task_status\":%d,\"nice\":%d,\"nice_errno\":%d",pid,task_status,nice,nice_error);
 if(task_status){puts(",\"threads\":[]}");return 1;}
 if(read_array){
  char *stop=NULL;uint64_t address=strtoull(argv[3],&stop,0),values[4]={0};mach_vm_size_t copied=0;
  if(!stop||*stop||(address&7)||address<0x100000000ULL){mach_port_deallocate(mach_task_self(),task);puts(",\"read_status\":4,\"raw_u64\":null,\"threads\":[]}");return 2;}
  int read_status=mach_vm_read_overwrite(task,address,sizeof(values),(mach_vm_address_t)values,&copied);
  printf(",\"read_status\":%d,\"read_bytes\":%llu,\"raw_u64\":",read_status,(unsigned long long)copied);
  if(!read_status&&copied==sizeof(values))printf("[%llu,%llu,%llu,%llu]",(unsigned long long)values[0],(unsigned long long)values[1],(unsigned long long)values[2],(unsigned long long)values[3]);else printf("null");
  puts(",\"threads\":[]}");mach_port_deallocate(mach_task_self(),task);return read_status||copied!=sizeof(values)?1:0;
 }
 task_category_policy_data_t category={0};mach_msg_type_number_t category_count=TASK_CATEGORY_POLICY_COUNT;boolean_t category_default=FALSE;
 kern_return_t (*get_task_policy)(task_t,task_policy_flavor_t,task_policy_t,mach_msg_type_number_t *,boolean_t *)=dlsym(RTLD_DEFAULT,"task_policy_get");
 int category_status=get_task_policy?get_task_policy(task,TASK_CATEGORY_POLICY,(task_policy_t)&category,&category_count,&category_default):KERN_NOT_SUPPORTED;
 printf(",\"task_role_status\":%d,\"task_role\":",category_status);
 if(!category_status)printf("%d",category.role);else printf("null");
 printf(",\"threads\":[");
 thread_act_array_t list=NULL;mach_msg_type_number_t count=0;int status=task_threads(task,&list,&count);
 if(status){printf("],\"thread_list_status\":%d}\n",status);mach_port_deallocate(mach_task_self(),task);return 1;}
 for(mach_msg_type_number_t i=0;i<count;i++){
  thread_extended_info_data_t ext={0};mach_msg_type_number_t n=THREAD_EXTENDED_INFO_COUNT;
  int ext_status=thread_info(list[i],THREAD_EXTENDED_INFO,(thread_info_t)&ext,&n);
  thread_identifier_info_data_t ident={0};n=THREAD_IDENTIFIER_INFO_COUNT;
  int id_status=thread_info(list[i],THREAD_IDENTIFIER_INFO,(thread_info_t)&ident,&n);
  struct qos_policy qos={0};n=2;boolean_t defaults=FALSE;
  int qos_status=thread_policy_get(list[i],9,(thread_policy_t)&qos,&n,&defaults);
  thread_precedence_policy_data_t precedence={0};n=THREAD_PRECEDENCE_POLICY_COUNT;defaults=FALSE;
  int precedence_status=thread_policy_get(list[i],THREAD_PRECEDENCE_POLICY,(thread_policy_t)&precedence,&n,&defaults);
  if(i)putchar(',');
  printf("{\"tid_status\":%d,\"tid\":%llu,\"extended_status\":%d,\"name\":",id_status,(unsigned long long)ident.thread_id,ext_status);string(ext.pth_name,sizeof(ext.pth_name));
  printf(",\"current_priority\":%d,\"base_priority\":%d,\"max_priority\":%d,\"policy\":%d,\"cpu_usage\":%d,\"qos_status\":%d,\"qos_tier\":%d,\"qos_importance\":%d,\"precedence_status\":%d,\"precedence_importance\":%d}",ext.pth_curpri,ext.pth_priority,ext.pth_maxpriority,ext.pth_policy,ext.pth_cpu_usage,qos_status,qos.tier,qos.importance,precedence_status,precedence.importance);
  mach_port_deallocate(mach_task_self(),list[i]);
 }
 printf("],\"thread_list_status\":0}\n");
 mach_vm_deallocate(mach_task_self(),(mach_vm_address_t)list,count*sizeof(*list));mach_port_deallocate(mach_task_self(),task);return 0;
}
