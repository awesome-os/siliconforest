/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <fcntl.h>
#include <sandbox.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static int forbidden(int fd,int error){return fd<0&&(error==EPERM||error==EACCES);}
int main(int argc,char **argv){
    if(argc!=3)return 2;
    int positive=!strcmp(argv[1],"allow");
    int fd=open(argv[2],O_RDONLY);if(fd<0){perror("font preflight");return 2;}close(fd);
    char *error=NULL;
    const char *profile=positive?
        "(version 1)(deny default)(allow mach-lookup (global-name \"com.apple.FontObjectsServer\"))":
        "(version 1)(deny default)(allow mach-lookup (global-name \"unrelated.service\"))";
    int status=sandbox_init(profile,0,&error);
    printf("FXFONTSB positive=%d applied=%d error=%s\n",positive,status,error?error:"none");sandbox_free_error(error);if(status)return 1;
    errno=0;int config=open("/etc/fonts/fonts.conf",O_RDONLY),ce=errno;
    errno=0;int font=open(argv[2],O_RDONLY),fe=errno;
    errno=0;int write=open("/etc/fonts/fonts.conf",O_WRONLY),we=errno;
    errno=0;int other=open("/etc/passwd",O_RDONLY),oe=errno;
    printf("FXFONTSB config=%d errno=%d font=%d errno=%d write=%d errno=%d other=%d errno=%d\n",config,ce,font,fe,write,we,other,oe);
    if(config>=0)close(config);if(font>=0)close(font);if(write>=0)close(write);if(other>=0)close(other);
    return (positive?(config>=0&&font>=0):(forbidden(config,ce)&&forbidden(font,fe)))&&forbidden(write,we)&&forbidden(other,oe)?0:1;
}
