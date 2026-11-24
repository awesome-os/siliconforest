#define _POSIX_C_SOURCE 200809L
#include "desktop_proof.h"
#include <openssl/sha.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *root;
const char *rg_test_root(void){return root;}
uid_t rg_test_uid(void){return getuid();}
void rg_test_after_read(void) {
 static unsigned count;count++;const char *wanted=getenv("DNDS_TEST_RACE_AT"),*path=getenv("DNDS_TEST_RACE_PATH");
 if(wanted&&path&&count==(unsigned)strtoul(wanted,NULL,10)) {FILE *f=fopen(path,"ab");if(f){fputc(' ',f);fclose(f);}}
}
struct observer {unsigned count;int bad;char pending[64];unsigned char digest[32];};
static void observe(const char *kind,const uint8_t *b,size_t n,void *ctx) {
 struct observer *o=ctx;o->count++;
 if(o->pending[0]) {if(strcmp(kind,o->pending)||n!=32||memcmp(b,o->digest,32))o->bad=1;o->pending[0]=0;}
 else {if(n>65536)o->bad=1;snprintf(o->pending,sizeof(o->pending),"%s_sha256",kind);SHA256(b,n,o->digest);}
}
int main(int argc,char **argv) {
 if(argc!=3)return 2;root=argv[1];int expected=atoi(argv[2]);struct observer o={0};
 struct dnds_desktop_facts value,before;memset(&value,0xa5,sizeof(value));before=value;
 int rc=dnds_verify_closed("0123456789abcdef0123456789abcdef0123456789abcdef",90,observe,&o,&value);
 if((rc==0)!=expected||o.bad||o.pending[0]||(rc&&memcmp(&value,&before,sizeof(value)))) {fprintf(stderr,"proof rc=%d expected=%d callbacks=%u bad=%d\n",rc,expected,o.count,o.bad);return 1;}
 if(!rc&&(o.count!=48||value.app_pid[0]!=101||value.app_pid[1]!=102||value.shell_pid[0]!=201||value.shell_pid[1]!=202||value.client_pid[0]!=301||value.client_pid[1]!=302))return 1;
 return 0;
}
