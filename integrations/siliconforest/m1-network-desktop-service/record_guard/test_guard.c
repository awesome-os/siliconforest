#define _POSIX_C_SOURCE 200809L
#include "record_guard.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { ++checks; if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);} }while(0)
static unsigned checks,groups,race,wrong_uid,hooks;
static char root[128],base[256],sessiondir[320],instance[384],file[448],held[448],linkfile[448],moved[448];
static const char nonce[]="0123456789abcdef0123456789abcdef0123456789abcdef";
static char exact[128];
const char *rg_test_root(void){return root;}
uid_t rg_test_uid(void){return getuid()+(wrong_uid?1:0);}
static void writefile(const char *p,const void *b,size_t n){
    int fd=open(p,O_WRONLY|O_CREAT|O_TRUNC,0600);CHECK(fd>=0);
    const uint8_t *s=b;while(n){ssize_t r=write(fd,s,n);CHECK(r>0);s+=r;n-=(size_t)r;}CHECK(close(fd)==0);
}
void rg_test_after_read(void){
    ++hooks;
    switch(race){
    case 1:writefile(file,"x",1);break;
    case 2:CHECK(rename(file,held)==0);writefile(file,"{}",2);break;
    case 3:CHECK(link(file,linkfile)==0);break;
    case 4:CHECK(chmod(file,0644)==0);break;
    case 5:CHECK(rename(instance,moved)==0);CHECK(mkdir(instance,0700)==0);break;
    case 6:{struct stat s;CHECK(stat(file,&s)==0);writefile(file,"[]",2);
        struct timespec ts[2]={s.st_atim,s.st_mtim};CHECK(utimensat(AT_FDCWD,file,ts,0)==0);break;}
    default:break;
    }
}
static void startfs(void){
    const char *parent=getenv("RG_TEST_PARENT");if(!parent)parent="/tmp";
    CHECK(snprintf(root,sizeof(root),"%s/record-guard-XXXXXX",parent)>0);CHECK(mkdtemp(root));char p[256];
    snprintf(p,sizeof(p),"%s/private",root);CHECK(mkdir(p,0755)==0);
    snprintf(p,sizeof(p),"%s/private/var",root);CHECK(mkdir(p,0755)==0);
    snprintf(p,sizeof(p),"%s/private/var/run",root);CHECK(mkdir(p,0755)==0);
    snprintf(base,sizeof(base),"%s/private/var/run/darwin-desktop-network",root);CHECK(mkdir(base,0700)==0);
    snprintf(sessiondir,sizeof(sessiondir),"%s/session-%s",base,nonce);CHECK(mkdir(sessiondir,0700)==0);
    snprintf(instance,sizeof(instance),"%s/0001%.44s",sessiondir,nonce);CHECK(mkdir(instance,0700)==0);
    snprintf(file,sizeof(file),"%s/result.json",instance);snprintf(held,sizeof(held),"%s/held",instance);
    snprintf(linkfile,sizeof(linkfile),"%s/link",instance);snprintf(moved,sizeof(moved),"%s/moved",sessiondir);
    snprintf(exact,sizeof(exact),"/private/var/run/darwin-desktop-network/session-%s",nonce);
    writefile(file,"{}",2);race=wrong_uid=0;
}
static void finishfs(void){
    char p[512];unlink(file);unlink(held);unlink(linkfile);rmdir(instance);
    snprintf(p,sizeof(p),"%s/result.json",moved);unlink(p);rmdir(moved);
    snprintf(p,sizeof(p),"%s/result.json",sessiondir);unlink(p);
    snprintf(p,sizeof(p),"%s/0002%.44s",sessiondir,nonce);char q[600];snprintf(q,sizeof(q),"%s/result.json",p);unlink(q);rmdir(p);
    rmdir(sessiondir);rmdir(base);
    snprintf(p,sizeof(p),"%s/private/var/run",root);rmdir(p);
    snprintf(p,sizeof(p),"%s/private/var",root);rmdir(p);
    snprintf(p,sizeof(p),"%s/private",root);rmdir(p);CHECK(rmdir(root)==0);
}
static struct rg_json_workspace *jw;
static struct rg_read_workspace *rw;
static struct rg_document parse(const char *s){struct rg_document d={0};CHECK(rg_json_parse((const uint8_t *)s,strlen(s),jw,&d)==RG_OK);return d;}
static void bad(const char *s,enum rg_code code){
    struct rg_document d,old;memset(&d,0xa5,sizeof(d));old=d;
    CHECK(rg_json_parse((const uint8_t *)s,strlen(s),jw,&d)==code);CHECK(!memcmp(&d,&old,sizeof(d)));
}
static uint32_t key(struct rg_document *d,const char *k){struct rg_step s={RG_KEY,k,strlen(k),0};uint32_t at=99;CHECK(rg_json_path(d,&s,1,&at)==RG_OK);return at;}
static void test_json_values(void){
    ++groups;struct rg_document d=parse(" {\"a\":[true,false,null,-9223372036854775808,18446744073709551615],\"s\":\"A\\n\\u0000B\\u007f\",\"unknown\":{\"ok\":[]}} \n");
    struct rg_step path[2]={{RG_KEY,"a",1,0},{RG_INDEX,NULL,0,0}};uint32_t at;int b=7;int64_t i=7;uint64_t u=7;
    CHECK(rg_json_path(&d,path,2,&at)==RG_OK);CHECK(rg_json_bool(&d,at,&b)==RG_OK&&b==1);
    CHECK(rg_json_u64(&d,at,&u)==RG_TYPE&&u==7);
    path[1].index=1;CHECK(rg_json_path(&d,path,2,&at)==RG_OK);CHECK(rg_json_bool(&d,at,&b)==RG_OK&&b==0);
    path[1].index=2;CHECK(rg_json_path(&d,path,2,&at)==RG_OK);CHECK(rg_json_null(&d,at)==RG_OK);
    path[1].index=3;CHECK(rg_json_path(&d,path,2,&at)==RG_OK);CHECK(rg_json_i64(&d,at,&i)==RG_OK&&i==INT64_MIN);
    CHECK(rg_json_u64(&d,at,&u)==RG_RANGE&&u==7);
    path[1].index=4;CHECK(rg_json_path(&d,path,2,&at)==RG_OK);CHECK(rg_json_u64(&d,at,&u)==RG_OK&&u==UINT64_MAX);
    b=7;CHECK(rg_json_bool(&d,at,&b)==RG_TYPE&&b==7);
    i=7;CHECK(rg_json_i64(&d,at,&i)==RG_RANGE&&i==7);
    uint32_t str=key(&d,"s");char output[16];memset(output,'x',sizeof(output));size_t length=99;
    CHECK(rg_json_string(&d,str,output,5,&length)==RG_LIMIT&&output[0]=='x'&&length==99);
    CHECK(rg_json_string(&d,str,output,sizeof(output),&length)==RG_OK&&length==5&&!memcmp(output,"A\n\0B\177",5)&&output[5]==0);
    at=99;path[1].index=5;CHECK(rg_json_path(&d,path,2,&at)==RG_MISSING&&at==99);
    path[0].key="missing";path[0].length=7;CHECK(rg_json_path(&d,path,1,&at)==RG_MISSING&&at==99);
    CHECK(rg_json_path(&d,NULL,0,&at)==RG_OK&&at==0);
    CHECK(rg_json_path(&d,path,RG_MAX_DEPTH+1,&at)==RG_ARGUMENT);
    CHECK(rg_json_null(&d,str)==RG_TYPE);CHECK(rg_json_bool(&d,999,&b)==RG_ARGUMENT);
    CHECK(rg_json_string(&d,str,(char *)d.bytes,16,&length)==RG_ARGUMENT);
    CHECK(rg_json_path(&d,NULL,0,(uint32_t *)d.bytes)==RG_ARGUMENT);
    d=parse("{\"\\u0061\":3,\"\":4,\"z\":-0}");at=key(&d,"a");CHECK(rg_json_u64(&d,at,&u)==RG_OK&&u==3);
    at=key(&d,"");CHECK(rg_json_u64(&d,at,&u)==RG_OK&&u==4);
    at=key(&d,"z");CHECK(rg_json_i64(&d,at,&i)==RG_OK&&i==0);CHECK(rg_json_u64(&d,at,&u)==RG_RANGE);
}
static void test_json_reject(void){
    ++groups;
    const char *invalid[]={" ","{}x","{}{}","[1,]","{\"a\":1,}","{\"a\" 1}","[","{","[true false]","NaN","Infinity","1.0","1e0","01","-01","+1","-","truex","\"a\n\"","\"\\x41\"","\"\\u0080\"","\"\\ud800\"","\"\\u00xz\"","\"\\u00\"","\"unfinished","\"\\"};
    for(size_t j=0;j<sizeof(invalid)/sizeof(*invalid);++j)bad(invalid[j],RG_JSON);
    bad("18446744073709551616",RG_RANGE);bad("-9223372036854775809",RG_RANGE);
    const char *duplicates[]={"{\"a\":1,\"a\":2}","{\"a\":1,\"\\u0061\":2}","{\"x\":{\"a\":1,\"a\":2}}","[{\"\":1,\"\":2}]","{\"\\u0000\":1,\"\\u0000\":2}"};
    for(size_t j=0;j<sizeof(duplicates)/sizeof(*duplicates);++j)bad(duplicates[j],RG_DUPLICATE);
    char raw[]={ '"',(char)0x80,'"',0};bad(raw,RG_JSON);
    struct rg_document d={0};CHECK(rg_json_parse(NULL,1,jw,&d)==RG_ARGUMENT);CHECK(rg_json_parse((uint8_t *)"",0,jw,&d)==RG_LIMIT);
    CHECK(rg_json_parse((uint8_t *)"{}",RG_MAX_BYTES+1,jw,&d)==RG_LIMIT);
    char depth[100];for(unsigned n=31;n<=32;++n){memset(depth,'[',n);depth[n]='0';memset(depth+n+1,']',n);depth[2*n+1]=0;
        if(n==31)(void)parse(depth);else bad(depth,RG_LIMIT);}
    char *many=malloc(RG_MAX_BYTES+1);CHECK(many);
    for(unsigned n=4095;n<=4096;++n){size_t pos=0;many[pos++]='[';for(unsigned j=0;j<n;++j){if(j)many[pos++]=',';many[pos++]='0';}many[pos++]=']';many[pos]=0;
        if(n==4095)(void)parse(many);else bad(many,RG_LIMIT);}
    many[0]='"';memset(many+1,'a',RG_MAX_BYTES-2);many[RG_MAX_BYTES-1]='"';many[RG_MAX_BYTES]=0;
    d=parse(many);CHECK(d.tokens[0].magnitude==RG_MAX_BYTES-2);
    size_t used=0;many[used++]='{';
    for(unsigned j=0;j<1024;++j){int n=snprintf(many+used,RG_MAX_BYTES-used,"%s\"fixed-prefix-%04u\":0",j?",":"",j);CHECK(n>0);used+=(size_t)n;}
    many[used++]='}';many[used]=0;bad(many,RG_LIMIT);
    free(many);
}
static unsigned fdcount(void){unsigned n=0;for(int fd=0;fd<1024;++fd)if(fcntl(fd,F_GETFD)>=0)++n;return n;}
static enum rg_code readone(struct rg_snapshot *s){unsigned n=fdcount();enum rg_code r=rg_read(exact,nonce,1,"result.json",rw,s);CHECK(fdcount()==n);return r;}
static void rejected(enum rg_code expected){struct rg_snapshot s,old;memset(&s,0xa5,sizeof(s));old=s;enum rg_code r=readone(&s);CHECK(expected==RG_OK?r!=RG_OK:r==expected);CHECK(!memcmp(&s,&old,sizeof(s)));}
static void test_files(void){
    ++groups;startfs();struct rg_snapshot s,t;
    CHECK(readone(&s)==RG_OK&&s.length==2&&!memcmp(s.bytes,"{}",2));
    CHECK(readone(&t)==RG_OK&&s.directory.inode==t.directory.inode&&s.file.inode==t.file.inode);
    char p[512];snprintf(p,sizeof(p),"%s/result.json",sessiondir);writefile(p,"session",7);
    CHECK(rg_read(exact,nonce,0,"result.json",rw,&t)==RG_OK&&t.length==7&&t.directory.inode!=s.directory.inode);
    snprintf(p,sizeof(p),"%s/0002%.44s",sessiondir,nonce);CHECK(mkdir(p,0700)==0);char q[600];snprintf(q,sizeof(q),"%s/result.json",p);writefile(q,"two",3);
    CHECK(rg_read(exact,nonce,2,"result.json",rw,&t)==RG_OK&&t.length==3);
    CHECK(rg_read(exact,nonce,3,"result.json",rw,&t)==RG_ARGUMENT);
    CHECK(rg_read(exact,nonce,1,"../result.json",rw,&t)==RG_PATH);
    CHECK(rg_read(exact,nonce,1,"/result.json",rw,&t)==RG_PATH);
    CHECK(rg_read(exact,nonce,1,".hidden",rw,&t)==RG_PATH);
    CHECK(rg_read("/private/var/run/elsewhere",nonce,1,"result.json",rw,&t)==RG_PATH);
    char badnonce[49];memcpy(badnonce,nonce,49);badnonce[0]='A';CHECK(rg_read(exact,badnonce,1,"result.json",rw,&t)==RG_PATH);
    CHECK(chmod(file,0644)==0);rejected(RG_METADATA);CHECK(chmod(file,0600)==0);
    CHECK(link(file,linkfile)==0);rejected(RG_METADATA);CHECK(unlink(linkfile)==0);
    CHECK(chmod(instance,0755)==0);rejected(RG_METADATA);CHECK(chmod(instance,0700)==0);
    CHECK(chmod(base,0770)==0);rejected(RG_METADATA);CHECK(chmod(base,0700)==0);
    snprintf(p,sizeof(p),"%s/private/var",root);CHECK(chmod(p,0777)==0);rejected(RG_METADATA);CHECK(chmod(p,0755)==0);
    wrong_uid=1;rejected(RG_METADATA);wrong_uid=0;
    CHECK(rename(file,held)==0);CHECK(symlink("held",file)==0);unsigned prior=hooks;rejected(RG_OK);CHECK(hooks==prior);CHECK(unlink(file)==0);CHECK(rename(held,file)==0);
    CHECK(unlink(file)==0);CHECK(mkfifo(file,0600)==0);rejected(RG_METADATA);CHECK(unlink(file)==0);
    CHECK(mkdir(file,0600)==0);rejected(RG_METADATA);CHECK(rmdir(file)==0);
    writefile(file,"",0);CHECK(readone(&s)==RG_OK&&s.length==0);
    uint8_t *bytes=malloc(RG_MAX_BYTES+1);CHECK(bytes);memset(bytes,0x5a,RG_MAX_BYTES+1);
    writefile(file,bytes,16384);CHECK(readone(&s)==RG_OK&&s.length==16384);
    writefile(file,bytes,16385);rejected(RG_LIMIT);CHECK(rename(file,held)==0);
    writefile(held,bytes,RG_MAX_BYTES);CHECK(rg_read(exact,nonce,1,"held",rw,&s)==RG_OK&&s.length==RG_MAX_BYTES&&s.bytes[RG_MAX_BYTES-1]==0x5a);
    writefile(held,bytes,RG_MAX_BYTES+1);CHECK(rg_read(exact,nonce,1,"held",rw,&s)==RG_METADATA);
    const char *names[]={"network-client.json","network-client.pid","network-shell-return.txt"};const size_t caps[]={16384,32,512};
    for(unsigned j=0;j<3;++j){snprintf(q,sizeof(q),"%s/%s",instance,names[j]);writefile(q,bytes,caps[j]);CHECK(rg_read(exact,nonce,1,names[j],rw,&s)==RG_OK&&s.length==caps[j]);
        writefile(q,bytes,caps[j]+1);CHECK(rg_read(exact,nonce,1,names[j],rw,&s)==RG_LIMIT);CHECK(unlink(q)==0);}
    free(bytes);finishfs();
}
static void test_races(void){
    ++groups;for(unsigned n=1;n<=6;++n){startfs();race=n;rejected(RG_CHANGED);race=0;finishfs();}
    startfs();CHECK(rename(instance,moved)==0);CHECK(symlink("moved",instance)==0);unsigned prior=hooks;rejected(RG_OK);CHECK(hooks==prior);CHECK(unlink(instance)==0);CHECK(rename(moved,instance)==0);finishfs();
}
int main(void){
    jw=malloc(sizeof(*jw));rw=malloc(sizeof(*rw));CHECK(jw&&rw);
    test_json_values();test_json_reject();test_files();test_races();free(jw);free(rw);
    printf("PASS %u groups %u checks\n",groups,checks);return 0;
}
