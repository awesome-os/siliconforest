/* One actual Darwin libc lookup; parent owns timeout, output and process reap. */
#ifndef __APPLE__
#error Actual Darwin resolver probe only
#endif
#include "resolver_result.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
_Static_assert(AF_INET==2&&SOCK_STREAM==1&&IPPROTO_TCP==6,"selected Darwin constants");
_Static_assert(sizeof(struct sockaddr_in)==16,"Darwin IPv4 sockaddr size");
_Static_assert(offsetof(struct sockaddr_in,sin_len)==0&&offsetof(struct sockaddr_in,sin_family)==1&&
 offsetof(struct sockaddr_in,sin_port)==2&&offsetof(struct sockaddr_in,sin_addr)==4&&
 offsetof(struct sockaddr_in,sin_zero)==8,"Darwin IPv4 sockaddr offsets");
_Static_assert(sizeof(struct addrinfo)==48&&offsetof(struct addrinfo,ai_addrlen)==16&&
 offsetof(struct addrinfo,ai_canonname)==24&&offsetof(struct addrinfo,ai_addr)==32&&
 offsetof(struct addrinfo,ai_next)==40,"selected SDK addrinfo LP64 ABI");
static int output(const char *data,size_t n) {
 size_t sent=0;
 for(unsigned attempts=0;sent<n&&attempts<128;attempts++){
  ssize_t wrote=write(STDOUT_FILENO,data+sent,n-sent);
  if(wrote<0&&errno==EINTR)continue;
  if(wrote<=0)return -1;
  sent+=(size_t)wrote;
 }
 return sent==n?0:-1;
}
int main(int argc,char **argv) {
 if(argc!=5||strcmp(argv[1],"--nonce")||!rr_nonce(argv[2],strlen(argv[2]))||
    strcmp(argv[3],"--name")||strcmp(argv[4],"example.com"))return 2;
 struct rr_result r;memset(&r,0,sizeof(r));r.pid=getpid();if(r.pid<2)return 1;
 struct sigaction action;memset(&action,0,sizeof(action));action.sa_handler=SIG_IGN;
 if(sigemptyset(&action.sa_mask)||sigaction(SIGPIPE,&action,NULL))return 1;
 struct addrinfo hints;memset(&hints,0,sizeof(hints));
 hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;hints.ai_flags=AI_NUMERICSERV;
 struct addrinfo *answer=NULL;errno=0;
 r.gai_rc=getaddrinfo("example.com","80",&hints,&answer);
 if(r.gai_rc==EAI_SYSTEM)r.system_errno=errno;
 if(r.gai_rc){r.error=answer?RR_UNEXPECTED_FAILURE_RESULT:RR_GAI;}
 else if(!answer)r.error=RR_EMPTY;
 else{
  struct addrinfo *seen[RR_ADDRESS_CAP];size_t count=0;
  for(struct addrinfo *p=answer;p;){
   if(count==RR_ADDRESS_CAP){r.error=RR_CAP;break;}
   for(size_t i=0;i<count;i++)if(seen[i]==p){r.error=RR_CYCLE;break;}
   if(r.error)break;
   seen[count++]=p;
   if(rr_address(&r,p->ai_family,p->ai_socktype,p->ai_protocol,
                 (const uint8_t *)p->ai_addr,p->ai_addrlen))break;
   p=p->ai_next;
  }
  /* Free only the completely validated finite chain. A broken/overcap libc
   * chain is a failed process; process teardown owns its remaining allocation. */
  if(!r.error)freeaddrinfo(answer);
 }
 char data[RR_OUTPUT_CAP];size_t bytes=0;
 if(rr_format(&r,argv[2],data,sizeof(data),&bytes)||output(data,bytes))return 1;
 return r.error?1:0;
}
