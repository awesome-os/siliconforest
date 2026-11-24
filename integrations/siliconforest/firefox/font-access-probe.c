/* SPDX-License-Identifier: MIT
 * Two fresh sandboxed children: explicit font service permits reading the
 * bundled font, never writing it or reading an unrelated private file.
 */
#include <sandbox.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
int main(int argc,char **argv){
 if(argc!=3)return 2;int failed=0;
 for(int service=0;service<2;service++){
  pid_t pid=fork();if(pid<0)return 2;
  if(pid==0){
   char *error=NULL;const char *profile=service
    ? "(version 1) (deny default) (allow mach-lookup (global-name \"com.apple.fonts\"))"
    : "(version 1) (deny default)";
   if(sandbox_init(profile,0,&error)!=0)_exit(2);
   int readFont=open(argv[1],O_RDONLY);int re=errno;
   int writeFont=open(argv[1],O_WRONLY);int we=errno;
   int unrelated=open(argv[2],O_RDONLY);int ue=errno;
   int passed=(service?readFont>=0:readFont<0&&(re==EPERM||re==EACCES)) &&
    writeFont<0&&(we==EPERM||we==EACCES)&&unrelated<0&&(ue==EPERM||ue==EACCES);
   if(readFont>=0)close(readFont);if(writeFont>=0)close(writeFont);if(unrelated>=0)close(unrelated);
   _exit(passed?0:1);
  }
  int status=0;if(waitpid(pid,&status,0)!=pid)return 2;
  int passed=WIFEXITED(status)&&WEXITSTATUS(status)==0;
  printf("FXFONTACCESS explicit_font_service=%d status=%d pass=%d\n",service,status,passed);failed+=!passed;
 }return failed?1:0;
}
