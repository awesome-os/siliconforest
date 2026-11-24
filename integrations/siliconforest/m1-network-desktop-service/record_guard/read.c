#ifdef __APPLE__
#define _DARWIN_C_SOURCE 1
#else
#define _POSIX_C_SOURCE 200809L
#endif
#include "record_guard.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#ifdef RG_TEST_ONLY
/* Test-only alternate tree/UID and deterministic race injection. Not exported
 * by production object; production uses literal / and UID0 exclusively. */
extern const char *rg_test_root(void);
extern uid_t rg_test_uid(void);
extern void rg_test_after_read(void);
#define ROOT_PATH rg_test_root()
#define OWNER_UID rg_test_uid()
#else
#define ROOT_PATH "/"
#define OWNER_UID ((uid_t)0)
#endif
#ifdef __APPLE__
#define MTIME(s) ((s).st_mtimespec)
#define CTIME(s) ((s).st_ctimespec)
#else
#define MTIME(s) ((s).st_mtim)
#define CTIME(s) ((s).st_ctim)
#endif
static int overlap(const void *a,size_t n,const void *b,size_t m) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;if(!n||!m)return 0;
    if(n-1>UINTPTR_MAX-x||m-1>UINTPTR_MAX-y)return 1;
    return x<=y+m-1&&y<=x+n-1;
}
static int same_id(const struct stat *a,const struct stat *b) { return a->st_dev==b->st_dev&&a->st_ino==b->st_ino; }
static int directory(const struct stat *s,int app) {
    return S_ISDIR(s->st_mode)&&s->st_uid==OWNER_UID&&
        (app?((s->st_mode&07777)==0700):!(s->st_mode&0022));
}
static int regular(const struct stat *s) {
    return S_ISREG(s->st_mode)&&s->st_uid==OWNER_UID&&(s->st_mode&07777)==0600&&s->st_nlink==1&&
        s->st_size>=0&&(uint64_t)s->st_size<=RG_MAX_BYTES;
}
static int stable(const struct stat *a,const struct stat *b) {
    return same_id(a,b)&&a->st_size==b->st_size&&a->st_mode==b->st_mode&&a->st_uid==b->st_uid&&a->st_nlink==b->st_nlink&&
        MTIME(*a).tv_sec==MTIME(*b).tv_sec&&MTIME(*a).tv_nsec==MTIME(*b).tv_nsec&&
        CTIME(*a).tv_sec==CTIME(*b).tv_sec&&CTIME(*a).tv_nsec==CTIME(*b).tv_nsec;
}
enum rg_code rg_read(const char *session,const char *nonce,unsigned slot,const char *file,
    struct rg_read_workspace *w,struct rg_snapshot *out) {
    static const char prefix[]="/private/var/run/darwin-desktop-network/session-";
    if(!session||!nonce||!file||!w||!out||slot>2||overlap(w,sizeof(*w),out,sizeof(*out)))return RG_ARGUMENT;
    if(strnlen(nonce,49)!=48)return RG_PATH;
    for(unsigned i=0;i<48;++i)if(!((nonce[i]>='0'&&nonce[i]<='9')||(nonce[i]>='a'&&nonce[i]<='f')))return RG_PATH;
    if(strnlen(session,sizeof(prefix)+48)!=sizeof(prefix)-1+48||
        memcmp(session,prefix,sizeof(prefix)-1)||memcmp(session+sizeof(prefix)-1,nonce,48))return RG_PATH;
    size_t fn=strnlen(file,65);if(!fn||fn>64||file[0]=='.')return RG_PATH;
    for(size_t i=0;i<fn;++i)if(!((file[i]>='a'&&file[i]<='z')||(file[i]>='0'&&file[i]<='9')||file[i]=='-'||file[i]=='_'||file[i]=='.'))return RG_PATH;
    char filename[65];memcpy(filename,file,fn+1);file=filename;
    size_t file_limit=RG_MAX_BYTES;
    if(!strcmp(file,"result.json")||!strcmp(file,"network-client.json"))file_limit=16384;
    else if(!strcmp(file,"network-client.pid"))file_limit=32;
    else if(!strcmp(file,"network-shell-return.txt"))file_limit=512;
    char session_name[57],instance[49];
    memcpy(session_name,"session-",8);memcpy(session_name+8,nonce,48);session_name[56]=0;
    if(snprintf(instance,sizeof(instance),"%04u%.44s",slot,nonce)!=48)return RG_PATH;
    const char *parts[]={"private","var","run","darwin-desktop-network",session_name,instance};
    unsigned levels=slot?6:5;int dirs[7];struct stat identities[7];
    for(unsigned i=0;i<7;++i)dirs[i]=-1;
    int fd=-1;enum rg_code result=RG_IO;struct rg_snapshot candidate={0};
    dirs[0]=open(ROOT_PATH,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
    if(dirs[0]<0||fstat(dirs[0],&identities[0]))goto done;
    if(!directory(&identities[0],0)){result=RG_METADATA;goto done;}
    for(unsigned i=0;i<levels;++i){
        dirs[i+1]=openat(dirs[i],parts[i],O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
        if(dirs[i+1]<0||fstat(dirs[i+1],&identities[i+1]))goto done;
        if(!directory(&identities[i+1],i>=3)){result=RG_METADATA;goto done;}
    }
    fd=openat(dirs[levels],file,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);if(fd<0)goto done;
    struct stat before,after,binding;
    if(fstat(fd,&before))goto done;
    if(!regular(&before)){result=RG_METADATA;goto done;}
    if((uint64_t)before.st_size>file_limit){result=RG_LIMIT;goto done;}
    size_t wanted=(size_t)before.st_size,used=0;unsigned calls=0;
    while(used<wanted){
        if(++calls>128){result=RG_LIMIT;goto done;}
        size_t count=wanted-used;if(count>4096)count=4096;
        ssize_t n=read(fd,w->bytes+used,count);
        if(n<0)goto done;if(!n){result=RG_CHANGED;goto done;}used+=(size_t)n;
    }
    uint8_t extra;
    if(read(fd,&extra,1)!=0){result=RG_CHANGED;goto done;}
#ifdef RG_TEST_ONLY
    rg_test_after_read();
#endif
    if(fstat(fd,&after)||fstatat(dirs[levels],file,&binding,AT_SYMLINK_NOFOLLOW))goto done;
    if(!regular(&after)||!regular(&binding)||!stable(&before,&after)||!stable(&after,&binding)){result=RG_CHANGED;goto done;}
    for(unsigned i=0;i<levels;++i){
        if(fstatat(dirs[i],parts[i],&binding,AT_SYMLINK_NOFOLLOW))goto done;
        if(!directory(&binding,i>=3)||!same_id(&binding,&identities[i+1])){result=RG_CHANGED;goto done;}
    }
    candidate=(struct rg_snapshot){w->bytes,used,
        {(uint64_t)identities[levels].st_dev,(uint64_t)identities[levels].st_ino},
        {(uint64_t)before.st_dev,(uint64_t)before.st_ino},
        {(int64_t)MTIME(before).tv_sec,(uint32_t)MTIME(before).tv_nsec},
        {(int64_t)CTIME(before).tv_sec,(uint32_t)CTIME(before).tv_nsec}};
    result=RG_OK;
done:
    /* close is attempted once. EINTR/other close errors preserve failure and
     * never publish success; no retry that might close a recycled descriptor. */
    if(fd>=0&&close(fd))result=RG_IO;
    for(unsigned i=0;i<7;++i)if(dirs[i]>=0&&close(dirs[i]))result=RG_IO;
    if(result==RG_OK)*out=candidate;return result;
}
