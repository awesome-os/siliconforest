/* Distinct finite desktop network owner. Actual network requests belong only
 * to the two ordinary PTY clients; this parent never performs DNS or HTTP. */
#ifndef __APPLE__
#error Darwin guest only
#endif
#include "transport.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <net/if_utun.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/kern_control.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/stat.h>
#include <sys/sys_domain.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "native.h"
#include <sys/sysctl.h>
#include "process_context.h"
#include "owned_child.h"
#include "desktop_proof.h"
#include "paths.h"
#include "endpoint_owner.h"

#include "close_guard.h"
#include <sys/wait.h>

#define QMAX 16
#define LINE 3300
#define READ_BUDGET 4
struct item { size_t length,offset; uint8_t data[LINE]; };
struct queue { struct item item[QMAX]; unsigned head,count; };
enum phase { WAIT_OPEN, SERVICE_START, DESKTOP_ACTIVE, SERVICE_STOP, CLOSE_WAIT };
struct state {
 struct dln_tx tx; struct dln_rx rx;
 struct queue output,input;
 struct dlf_native network;
 struct dlr_drain drain;
 struct dnds_child daemon,frontend;
 struct dlr_endpoints endpoints;
 struct dnds_desktop_facts desktop;
 struct dnds_paths paths;
 int desktop_ok,daemon_cleanup,frontend_recorded,daemon_recorded;
 uint64_t setup_deadline,service_deadline,frontend_deadline,stop_deadline;
 char nonce[49];
 size_t line_used;uint8_t bit_value;unsigned bit_count;
 uint64_t console_bytes_in,observation_bytes;
 int peer_open,peer_close,close_sent,tty_saved,flags_saved,old_in,old_out;
 int owner_initialized,processes_exclusive,observation_failed;
 struct termios terminal;
 enum phase phase;
 uint64_t deadline,closing,close_deadline;
 uint64_t frames_out,frames_in,packets_out,packets_in,ignored_family;
 const char *error;unsigned errno_saved;
};
static int can_transition(const struct state *s);
static volatile sig_atomic_t stopped;
static void stop_now(int n) {
  (void)n;
  stopped = 1;
}
static uint64_t ms(void) {
  struct timespec t;
  if (clock_gettime(CLOCK_MONOTONIC, &t))
    return 0;
  return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}
static int fail(struct state *s, const char *e) {
  if (!s->error) {
    s->error = e;
    s->errno_saved = (unsigned)errno;
  }
  return -1;
}
static int push(struct queue *q, const uint8_t *p, size_t n) {
  if (!p || !n || n > LINE || q->count == QMAX)
    return -1;
  struct item *a = &q->item[(q->head + q->count) % QMAX];
  memcpy(a->data, p, n);
  a->length = n;
  a->offset = 0;
  q->count++;
  return 0;
}
static int flush(struct state *s, struct queue *q, int fd, int packet) {
  if (!q->count)
    return 0;
  struct item *a = &q->item[q->head];
  ssize_t n = write(fd, a->data + a->offset, a->length - a->offset);
  if (n < 0 && (errno == EAGAIN || errno == EINTR))
    return 0;
  if (n <= 0 || (packet && (size_t)n != a->length))
    return fail(s, packet ? "utun_write" : "console_write");
  a->offset += (size_t)n;
  if (a->offset == a->length) {
    q->head = (q->head + 1) % QMAX;
    q->count--;
  }
  return 0;
}
static int emit(struct state *s, uint8_t type, const uint8_t *p, size_t n) {
  uint8_t frame[DLNET_FRAME_BYTES];
  char line[LINE];
  size_t bytes = 0;
  if (s->frames_out >= 128)
    return fail(s, "transmit_frame_cap");
  if (s->output.count == QMAX)
    return fail(s, "console_queue_full");
  enum dln_error e =
      dln_encode(&s->tx, type, p, n, frame, sizeof(frame), &bytes);
  if (e)
    return fail(s, dln_error_name(e));
  int head = snprintf(line, sizeof(line), "DL30_TX_%s ", s->nonce);
  static const char hex[] = "0123456789abcdef";
  if (head <= 0 || (size_t)head + 2 * bytes + 1 > sizeof(line))
    return fail(s, "encoded_length");
  for (size_t i = 0; i < bytes; i++) {
    line[head + 2 * i] = hex[frame[i] >> 4];
    line[head + 2 * i + 1] = hex[frame[i] & 15];
  }
  line[head + 2 * bytes] = '\n';
  if (push(&s->output, (uint8_t *)line, (size_t)head + 2 * bytes + 1))
    return fail(s, "console_queue_full");
  s->frames_out++;
  if (type == DLNET_IPV4)
    s->packets_out++;
  return 0;
}
static int acknowledge(struct state *s) {
  char line[160];
  int n=snprintf(line,sizeof(line),"DL30_ACK_%s bytes=%llu\n",s->nonce,
                 (unsigned long long)s->console_bytes_in);
  if(n<=0 || (size_t)n>=sizeof(line) ||
     push(&s->output,(const uint8_t *)line,(size_t)n))
    return fail(s,"acknowledgement_queue");
  return 0;
}
static int received(void *ctx, uint8_t type, uint64_t sequence,
                    const uint8_t *p, size_t n) {
  struct state *s = ctx;
  (void)sequence;
  if (s->frames_in >= 128)
    return fail(s, "receive_frame_cap");
  s->frames_in++;
  if (type == DLNET_OPEN) {
    if (s->peer_open)
      return fail(s, "duplicate_open");
    s->peer_open = 1;
    return 0;
  }
  if (type == DLNET_CLOSE) {
    s->peer_close = 1;
    return s->closing ? 0 : fail(s, "peer_closed_early");
  }
  if (type != DLNET_IPV4 || !s->peer_open || s->peer_close)
    return fail(s, "frame_phase");
  uint8_t packet[DLNET_MTU + 4];
  uint32_t family = htonl(AF_INET);
  memcpy(packet, &family, 4);
  memcpy(packet + 4, p, n);
  if (push(&s->input, packet, n + 4))
    return fail(s, "utun_queue_full");
  s->packets_in++;
  return 0;
}
static int line_received(struct state *s) {
  if (s->line_used != 1 && s->line_used != 2)
    return fail(s, "ingress_bit_length");
  s->bit_value = (uint8_t)((s->bit_value << 1) | (s->line_used - 1));
  if (++s->bit_count == 8) {
    uint8_t value = s->bit_value;
    s->bit_count = 0;
    s->bit_value = 0;
    enum dln_error e = dln_feed(&s->rx, &value, 1, received, s);
    if (e)
      return fail(s, dln_error_name(e));
  }
  return 0;
}
static int read_console(struct state *s) {
  uint8_t data[4096];
  for (unsigned k = 0; k < READ_BUDGET; k++) {
    ssize_t n = read(STDIN_FILENO, data, sizeof(data));
    if (n < 0 && (errno == EAGAIN || errno == EINTR))
      return 0;
    if (n <= 0)
      return fail(s, "console_eof");
    s->console_bytes_in += (uint64_t)n;
    if (s->console_bytes_in > 262144)
      return fail(s, "ingress_byte_cap");
    for (ssize_t i = 0; i < n; i++) {
      if (data[i] == '\n') {
        if (line_received(s))
          return -1;
        s->line_used = 0;
      } else {
        if (data[i] != ' ' || s->line_used == 2)
          return fail(s, "ingress_alphabet");
        s->line_used++;
      }
    }
  }
  return 0;
}
static int tty_raw(struct state *s) {
  if (tcgetattr(0, &s->terminal) < 0)
    return fail(s, "terminal_get");
  s->tty_saved = 1;
  struct termios t = s->terminal;
  cfmakeraw(&t);
  if (tcsetattr(0, TCSANOW, &t) < 0)
    return fail(s, "terminal_raw");
  s->old_in = fcntl(0, F_GETFL);
  s->old_out = fcntl(1, F_GETFL);
  if (s->old_in < 0 || s->old_out < 0)
    return fail(s, "terminal_flags");
  s->flags_saved = 1;
  if (fcntl(0, F_SETFL, s->old_in | O_NONBLOCK) < 0 ||
      fcntl(1, F_SETFL, s->old_out | O_NONBLOCK) < 0)
    return fail(s, "terminal_nonblock");
  return 0;
}
static unsigned number(const char *p, unsigned high) {
  unsigned n = 0;
  if (!p || !*p)
    return 0;
  for (; *p; p++) {
    if (*p < '0' || *p > '9' || n > (high - (unsigned)(*p - '0')) / 10)
      return 0;
    n = n * 10 + (unsigned)(*p - '0');
  }
  return n <= high ? n : 0;
}
static int wait_ready(struct pollfd f[4]) {
  fd_set read_set, write_set, except_set;
  FD_ZERO(&read_set);
  FD_ZERO(&write_set);
  FD_ZERO(&except_set);
  int high = -1;
  for (unsigned i = 0; i < 4; i++) {
    f[i].revents = 0;
    int fd = f[i].fd;
    if (fd < 0 || !f[i].events)
      continue;
    if (fd >= FD_SETSIZE) {
      errno = EINVAL;
      return -1;
    }
    if (f[i].events & POLLIN)
      FD_SET(fd, &read_set);
    if (f[i].events & POLLOUT)
      FD_SET(fd, &write_set);
    FD_SET(fd, &except_set);
    if (fd > high)
      high = fd;
  }
  struct timeval delay = {0, 20000};
  int rc = select(high + 1, &read_set, &write_set, &except_set, &delay);
  if (rc <= 0)
    return rc;
  for (unsigned i = 0; i < 4; i++) {
    int fd = f[i].fd;
    if (fd < 0 || !f[i].events)
      continue;
    if (FD_ISSET(fd, &read_set))
      f[i].revents |= POLLIN;
    if (FD_ISSET(fd, &write_set))
      f[i].revents |= POLLOUT;
    if (FD_ISSET(fd, &except_set))
      f[i].revents |= POLLERR;
  }
  return rc;
}

/* Observation writes occur only at barriers without partial queued frame output.
 * A callback error is sticky; it never authorizes interface/client progress. */
static void observation_fail(struct state *s,const char *reason) {
 s->observation_failed=1;(void)fail(s,reason);
}
static void observe(const char *kind,const uint8_t *data,size_t n,void *context) {
 struct state *s=context;
 if(s->observation_failed)return;
 if(!kind || strlen(kind)>63 || n>65536 || (!data&&n)) {observation_fail(s,"observation_bounds");return;}
 for(const char *p=kind;*p;p++) if(!((*p>='a'&&*p<='z')||*p=='_'||(*p>='0'&&*p<='9'))) {observation_fail(s,"observation_kind");return;}
 static const char hex[]="0123456789abcdef";
 size_t offset=0;
 do {
  size_t part=n-offset;if(part>64)part=64;
  char line[400];int head=snprintf(line,sizeof(line),"DL30_OBSERVE_%s kind=%s total=%zu offset=%zu hex=",s->nonce,kind,n,offset);
  if(head<0 || (size_t)head+2*part+1>sizeof(line)) {observation_fail(s,"observation_format");return;}
  for(size_t i=0;i<part;i++){line[head+2*i]=hex[data[offset+i]>>4];line[head+2*i+1]=hex[data[offset+i]&15];}
  size_t length=(size_t)head+2*part;line[length++]='\n';
  if(s->observation_bytes+length>524288) {observation_fail(s,"observation_cap");return;}
  uint64_t start=ms();
  if(!start || start>UINT64_MAX-2000){observation_fail(s,"observation_clock");return;}
  size_t sent=0;uint64_t end=start+2000;unsigned attempts=0;
  while(sent<length) {
   uint64_t now=ms();
   if(stopped || !now || now>=end || now>=s->deadline || ++attempts>128) {errno=ETIMEDOUT;observation_fail(s,"observation_write_deadline");return;}
   fd_set w;FD_ZERO(&w);FD_SET(1,&w);struct timeval wait={0,20000};
   int ready=select(2,NULL,&w,NULL,&wait);
   if(ready<0 && errno==EINTR)continue;
   if(ready<0){observation_fail(s,"observation_select");return;}
   if(!ready)continue;
   ssize_t wrote=write(1,line+sent,length-sent);
   if(wrote<0&&(errno==EAGAIN||errno==EINTR))continue;
   if(wrote<=0){observation_fail(s,"observation_write");return;}
   sent+=(size_t)wrote;
  }
  s->observation_bytes+=length;offset+=part;
 }while(offset<n);
}
static int inspect_processes(struct state *s) {
 int mib[3]={CTL_KERN,KERN_PROC,KERN_PROC_ALL};
 size_t cap=64*sizeof(struct kinfo_proc),used=cap;
 struct kinfo_proc *list=calloc(1,cap);
 if(!list)return fail(s,"process_memory");
 if(sysctl(mib,3,list,&used,NULL,0)<0 || used>cap || used%sizeof(*list)) {free(list);return fail(s,"process_snapshot");}
 int bad=0;struct eg_process rows[64];memset(rows,0,sizeof(rows));
 for(size_t i=0;i<used/sizeof(*list);i++) {
  struct kinfo_proc *p=&list[i];char comm[sizeof(p->kp_proc.p_comm)+1];
  memcpy(comm,p->kp_proc.p_comm,sizeof(p->kp_proc.p_comm));comm[sizeof(p->kp_proc.p_comm)]=0;
  for(char *q=comm;*q;q++)if(!((*q>='a'&&*q<='z')||(*q>='A'&&*q<='Z')||(*q>='0'&&*q<='9')||*q=='_'||*q=='-'))*q='?';
  char line[120];int n=snprintf(line,sizeof(line),"pid=%d parent=%d comm=%s euid=%u",p->kp_proc.p_pid,p->kp_eproc.e_ppid,comm,p->kp_eproc.e_ucred.cr_uid);
  if(n<0||(size_t)n>=sizeof(line)){bad=1;break;}
  observe("process",(uint8_t *)line,(size_t)n,s);
  rows[i].pid=p->kp_proc.p_pid;rows[i].parent=p->kp_eproc.e_ppid;
  rows[i].euid=p->kp_eproc.e_ucred.cr_uid;
  if(strlen(comm)>=sizeof(rows[i].name)){bad=1;break;}
  memcpy(rows[i].name,comm,strlen(comm)+1);
 }
 size_t count=used/sizeof(*list);
 free(list);
 if(bad||s->error||!eg_process_context(rows,count,getpid(),getppid()))return fail(s,"process_exclusivity");
 s->processes_exclusive=1;return 0;
}
static void observe_interface(struct state *s,const char *kind) {
 char line[140];int n=snprintf(line,sizeof(line),"phase=%u name=%s index=%u peer=%u.%u.%u.%u cleanup=%d failed=%d",s->network.owned.phase_count,s->network.owned.name,s->network.owned.index,(s->network.owned.peer>>24)&255,(s->network.owned.peer>>16)&255,(s->network.owned.peer>>8)&255,s->network.owned.peer&255,s->network.owned.cleanup_complete,s->network.owned.failed);
 if(n<0||(size_t)n>=sizeof(line)){fail(s,"interface_observation");return;}
 observe(kind,(uint8_t *)line,(size_t)n,s);
}
static void child_observation(struct state *s,const char *kind,const struct dnds_child *c) {
 char line[600];int n=snprintf(line,sizeof(line),"kind=%u pid=%d created=%d reaped=%d lost=%d error=%d wait_status=%d output_bytes=%zu stderr_bytes=%zu output_eof=%d stderr_eof=%d term_sent=%d kill_sent=%d stop_requested=%d started_ms=%llu last_ms=%llu",(unsigned)c->kind,c->pid,c->created,c->reaped,c->lost,c->error,c->wait_status,c->output_used,c->error_used,c->output_eof,c->error_eof,c->term_sent,c->kill_sent,c->stop_requested,(unsigned long long)c->started,(unsigned long long)c->last);
 if(n<=0||(size_t)n>=sizeof(line)){fail(s,"child_observation");return;}
 observe(kind,(const uint8_t *)line,(size_t)n,s);
 char key[64];n=snprintf(key,sizeof(key),"%s_spawn",kind);
 if(n<=0||(size_t)n>=sizeof(key)){fail(s,"child_observation");return;}
 observe(key,(const uint8_t *)&c->spawn,sizeof(c->spawn),s);
 if(c->output_used){n=snprintf(key,sizeof(key),"%s_stdout",kind);if(n<=0||(size_t)n>=sizeof(key)){fail(s,"child_observation");return;}observe(key,c->output,c->output_used,s);}
 if(c->error_used){n=snprintf(key,sizeof(key),"%s_stderr",kind);if(n<=0||(size_t)n>=sizeof(key)){fail(s,"child_observation");return;}observe(key,c->errors,c->error_used,s);}
}
/* Failure evidence only, before restoring stdout's original file flags. The
 * caller still has O_NONBLOCK. One bounded write per owned child; no retry,
 * new deadline, network permission, or success inference on a full console. */
static void failure_child(const struct state *s,const struct dnds_child *c) {
 if(!c->created||!s->flags_saved)return;
 int flags=fcntl(1,F_GETFL);if(flags<0||!(flags&O_NONBLOCK))return;
 char line[420];int n=snprintf(line,sizeof(line),
  "\nDNDS_FAILURE_CHILD nonce=%s kind=%u pid=%d created=%d reaped=%d lost=%d error=%d wait_status=%d output_eof=%d stderr_eof=%d term_sent=%d kill_sent=%d stop_requested=%d\n",
  s->nonce,(unsigned)c->kind,c->pid,c->created,c->reaped,c->lost,c->error,
  c->wait_status,c->output_eof,c->error_eof,c->term_sent,c->kill_sent,c->stop_requested);
 if(n>0&&(size_t)n<sizeof(line))(void)write(1,line,(size_t)n);
}
static int service_start(struct state *s,uint64_t now) {
 if(now>=s->setup_deadline)return fail(s,"setup_deadline");
 if(dlr_endpoints_preflight(&s->endpoints))return fail(s,"resolver_endpoint_preflight");
 if(dnds_child_start(&s->daemon,DNDS_MDNS,s->nonce,now))return fail(s,"mdns_spawn");
 s->service_deadline=now+5000;s->phase=SERVICE_START;return 0;
}
static int service_tick(struct state *s,uint64_t now) {
 dnds_child_tick(&s->daemon,now);dnds_child_tick(&s->frontend,now);
 if(s->frontend.error)return fail(s,"frontend_child_error");
 if(s->daemon.error)return fail(s,"mdns_child_error");
 if(s->phase!=SERVICE_STOP&&!s->daemon_cleanup&&s->daemon.reaped)return fail(s,"mdns_exited_early");
 if(s->phase==SERVICE_START) {
  int ready=dlr_endpoints_ready(&s->endpoints,s->daemon.pid);
  if(ready<0)return fail(s,"resolver_endpoint_identity");
  if(now>=s->service_deadline||now>=s->setup_deadline)return fail(s,"mdns_endpoint_deadline");
  if(ready&&can_transition(s)) {
   if(dnds_child_start(&s->frontend,DNDS_FRONTEND,s->nonce,now))return fail(s,"frontend_spawn");
   s->frontend_deadline=now+105000;s->phase=DESKTOP_ACTIVE;
  }
 }
 if(s->phase==DESKTOP_ACTIVE) {
  if(dnds_child_done(&s->frontend)&&can_transition(s)) {
   if(!WIFEXITED(s->frontend.wait_status)||WEXITSTATUS(s->frontend.wait_status)!=0||
      s->frontend.kill_sent||!s->frontend.output_eof||!s->frontend.error_eof)return fail(s,"frontend_shutdown_status");
   child_observation(s,"frontend_child",&s->frontend);s->frontend_recorded=1;
   if(s->error)return -1;
   if(dnds_paths_verify(&s->paths))return fail(s,"desktop_path_identity");
   if(dnds_verify_closed(s->nonce,s->frontend.pid,observe,s,&s->desktop))return fail(s,"desktop_closed_records");
   if(s->error)return -1;
   s->desktop_ok=1;
   uint64_t fresh=ms();
   if(!fresh||fresh>=s->deadline)return fail(s,"desktop_proof_deadline");
   dnds_child_stop(&s->daemon,fresh);s->stop_deadline=fresh+5000;s->phase=SERVICE_STOP;
  }else if(now>=s->frontend_deadline)return fail(s,"frontend_deadline");
 }
 if(s->phase==SERVICE_STOP) {
  if(dnds_child_done(&s->daemon)) {
   if(!WIFEXITED(s->daemon.wait_status)||WEXITSTATUS(s->daemon.wait_status)!=EINTR||!s->daemon.term_sent||s->daemon.kill_sent||
      !s->daemon.output_eof||!s->daemon.error_eof)return fail(s,"mdns_shutdown_status");
   if(!s->daemon_cleanup&&dlr_endpoints_cleanup(&s->endpoints,1))return fail(s,"resolver_endpoint_cleanup");
   s->daemon_cleanup=1;
  }else if(now>=s->stop_deadline)return fail(s,"mdns_reap_deadline");
 }
 return 0;
}
static int can_transition(const struct state *s) {
 return !s->output.count && !s->input.count && !s->line_used &&
        !s->bit_count && !s->rx.used;
}
int main(int argc,char **argv) {
 if(argc!=5||strcmp(argv[1],"--nonce")||strcmp(argv[3],"--seconds"))return 2;
 uint8_t nonce[24];unsigned seconds=number(argv[4],180);
 if(seconds!=180||!dln_nonce_hex(argv[2],strlen(argv[2]),nonce)||geteuid()!=0)return 2;
 umask(0077);
 struct state *s=calloc(1,sizeof(*s));if(!s)return 1;
 memcpy(s->nonce,argv[2],49);
 uint64_t started=ms();if(!started||started>UINT64_MAX-180000){free(s);return 1;}
 s->deadline=started+seconds*1000;s->setup_deadline=started+20000;
 struct sigaction a;memset(&a,0,sizeof(a));a.sa_handler=stop_now;sigemptyset(&a.sa_mask);
 if(sigaction(SIGINT,&a,NULL)<0||sigaction(SIGTERM,&a,NULL)<0||sigaction(SIGHUP,&a,NULL)<0){fail(s,"signal_setup");goto cleanup;}
 a.sa_handler=SIG_DFL;
 if(sigaction(SIGCHLD,&a,NULL)<0){fail(s,"signal_setup");goto cleanup;}
 a.sa_handler=SIG_IGN;
 if(sigaction(SIGPIPE,&a,NULL)<0||sigaction(SIGTTOU,&a,NULL)<0){fail(s,"signal_setup");goto cleanup;}
 if(dln_tx_init(&s->tx,nonce,DLNET_GUEST_TO_HOST)||dln_rx_init(&s->rx,nonce,DLNET_HOST_TO_GUEST)){fail(s,"codec_init");goto cleanup;}
 if(tty_raw(s)||inspect_processes(s))goto cleanup;
 if(dnds_paths_create(&s->paths,s->nonce)){fail(s,"desktop_private_paths");goto cleanup;}
 s->owner_initialized=1;
 if(dlf_native_init(&s->network,s->processes_exclusive,observe,s)||s->error){fail(s,"interface_init");goto cleanup;}
 if(dlf_native_open(&s->network)||s->error){fail(s,"default_interface_open");goto cleanup;}
 if(dlf_native_fd(&s->network)<0){fail(s,"default_descriptor_not_ready");goto cleanup;}
 observe_interface(s,"default_interface");
 for(size_t i=0;i<s->network.owned._baseline_count;i++) {
  char line[60];int n=snprintf(line,sizeof(line),"index=%u name=%s",s->network.owned._baseline[i].index,s->network.owned._baseline[i].name);
  if(n<0||(size_t)n>=sizeof(line)){fail(s,"baseline_observation");goto cleanup;}
  observe("baseline_interface",(uint8_t *)line,(size_t)n,s);
 }
 if(s->error)goto cleanup;
 char ready[180];int n=snprintf(ready,sizeof(ready),"\nDL30_READY_%s interface=%s mtu=1500\n",s->nonce,s->network.owned.name);
 if(n<=0||(size_t)n>=sizeof(ready)||push(&s->output,(uint8_t *)ready,(size_t)n)||emit(s,DLNET_OPEN,NULL,0)){fail(s,"ready_queue");goto cleanup;}
 while(!s->error) {
  uint64_t now=ms();
  if(stopped){fail(s,"signal");break;}
  if(!now||now>=s->deadline){errno=ETIMEDOUT;fail(s,"deadline");break;}
  if(s->phase==WAIT_OPEN && s->peer_open && service_start(s,now))break;
  if(s->daemon.created&&service_tick(s,now))break;
  if(s->phase==SERVICE_STOP && s->daemon_cleanup && can_transition(s)) {
   child_observation(s,"mdns_child",&s->daemon);s->daemon_recorded=1;
   if(s->error)break;
   s->closing=ms();s->phase=CLOSE_WAIT;
   if(dlr_drain_begin(&s->drain,s->closing,40000)){fail(s,"close_guard_init");break;}
   s->close_deadline=s->drain.deadline;
   if(emit(s,DLNET_CLOSE,NULL,0))break;
   s->close_sent=1;
  }
  if(s->close_sent) {
   uint64_t close_now=ms();
   if(!close_now||close_now>=s->deadline){errno=ETIMEDOUT;fail(s,"deadline");break;}
   int state=dlr_drain_poll(&s->drain,close_now,s->peer_close,s->output.count,s->input.count,
       (unsigned)s->line_used,s->bit_count,s->peer_close?dln_end(&s->rx)==0:0);
   if(state==1)break;
   if(state<0){errno=s->drain.error==DLR_DRAIN_TIMEOUT?ETIMEDOUT:EPROTO;fail(s,s->drain.error==DLR_DRAIN_TIMEOUT?"close_deadline":"unclean_stream_end");break;}
  }
  struct pollfd f[4]={{0,POLLIN,0},{1,s->output.count?POLLOUT:0,0},
   {dlf_native_fd(&s->network),(short)((s->peer_open&&!s->close_sent&&s->output.count<QMAX-2?POLLIN:0)|(s->input.count?POLLOUT:0)),0},
   {-1,0,0}};
  int result=wait_ready(f);
  if(result<0){if(errno==EINTR)continue;fail(s,"select");break;}
  if(f[0].revents&(POLLIN|POLLHUP)) {
   uint64_t before=s->console_bytes_in;
   if(read_console(s))break;
   if(s->console_bytes_in!=before&&acknowledge(s))break;
  }
  if((f[1].revents&POLLOUT)&&flush(s,&s->output,1,0))break;
  if((f[2].revents&POLLOUT)&&flush(s,&s->input,dlf_native_fd(&s->network),1))break;
  if(f[2].revents&POLLIN) {
   uint8_t packet[DLNET_MTU+5];ssize_t nread=read(dlf_native_fd(&s->network),packet,sizeof(packet));
   if(nread<0&&(errno==EAGAIN||errno==EINTR))continue;
   if(nread<4||nread>DLNET_MTU+4){fail(s,"utun_packet_length");break;}
   uint32_t family;memcpy(&family,packet,4);
   if(ntohl(family)!=AF_INET){if(++s->ignored_family>128){fail(s,"utun_family_cap");break;}continue;}
   if(emit(s,DLNET_IPV4,packet+4,(size_t)nread-4))break;
  }
 }
cleanup:
 /* Retain all real child identities through failure. The frontend owns apps;
  * only its complete closed records can discharge their lifetimes. A forced
  * frontend kill is recorded failure. Never infer a client reap from killpg. */
 { uint64_t start=ms(),end=start+20000;
  if(end>s->deadline)end=s->deadline;
  dnds_child_stop(&s->frontend,start);dnds_child_stop(&s->daemon,start);
  for(unsigned attempt=0;attempt<2200;attempt++) {
   uint64_t now=ms();dnds_child_tick(&s->frontend,now);dnds_child_tick(&s->daemon,now);
   int frontend_done=!s->frontend.created||dnds_child_done(&s->frontend)||s->frontend.lost;
   int daemon_done=!s->daemon.created||dnds_child_done(&s->daemon)||s->daemon.lost;
   if(frontend_done&&daemon_done)break;
   if(!now||now>=end)break;
   /* On failure preserve link progress while owned children retire. Any error
    * stays sticky. These writes never convert cleanup into a network PASS. */
   if(s->owner_initialized&&s->tty_saved&&!s->close_sent) {
    struct pollfd f[4]={{0,POLLIN,0},{1,s->output.count?POLLOUT:0,0},
     {dlf_native_fd(&s->network),(short)((s->peer_open&&s->output.count<QMAX-2?POLLIN:0)|(s->input.count?POLLOUT:0)),0},{-1,0,0}};
    if(wait_ready(f)>0) {
     if(f[0].revents&POLLIN){uint64_t before=s->console_bytes_in;(void)read_console(s);if(before!=s->console_bytes_in)(void)acknowledge(s);}
     if(f[1].revents&POLLOUT)(void)flush(s,&s->output,1,0);
     if(f[2].revents&POLLOUT)(void)flush(s,&s->input,dlf_native_fd(&s->network),1);
     if(f[2].revents&POLLIN) {
      uint8_t packet[DLNET_MTU+5];ssize_t n=read(dlf_native_fd(&s->network),packet,sizeof(packet));
      if(n>=4&&n<=DLNET_MTU+4){uint32_t family;memcpy(&family,packet,4);if(ntohl(family)==AF_INET)(void)emit(s,DLNET_IPV4,packet+4,(size_t)n-4);}
     }
    }
   }else {struct timeval pause={0,10000};(void)select(0,NULL,NULL,NULL,&pause);}
  }
  if(s->frontend.created&&!dnds_child_done(&s->frontend))fail(s,"frontend_cleanup_incomplete");
  if(s->daemon.created&&!dnds_child_done(&s->daemon))fail(s,"mdns_cleanup_incomplete");
  if(s->endpoints.initialized&&!s->endpoints.cleaned&&s->daemon.reaped&&dlr_endpoints_cleanup(&s->endpoints,1))fail(s,"resolver_endpoint_final_cleanup");
  if(s->frontend.created&&!s->frontend_recorded)child_observation(s,"frontend_child",&s->frontend);
  if(s->daemon.created&&!s->daemon_recorded)child_observation(s,"mdns_child",&s->daemon);
 }
 if(s->owner_initialized&&dlf_native_close(&s->network))fail(s,"interface_final_cleanup");
 if(s->owner_initialized)observe_interface(s,"final_cleanup");
 int complete=s->owner_initialized&&!s->network.owned.failed&&s->network.owned.phase_count==1&&s->network.owned.cleanup_complete&&
  s->network.lifetime.cleanup_complete&&!s->network.lifetime.first_error&&s->network.lifetime.stage==DLF_CLOSED&&s->network.lifetime.add_attempted==1;
 if(dnds_paths_close(&s->paths))fail(s,"desktop_path_close");
 if(s->error){failure_child(s,&s->frontend);failure_child(s,&s->daemon);}
 if(s->tty_saved&&tcsetattr(0,TCSANOW,&s->terminal)<0)fail(s,"terminal_restore");
 if(s->flags_saved){int a=fcntl(0,F_SETFL,s->old_in),b=fcntl(1,F_SETFL,s->old_out);if(a<0||b<0)fail(s,"flags_restore");}
 uint64_t finished=ms();
 if(!finished||finished>=s->deadline||finished<s->drain.completed)fail(s,"final_cleanup_deadline");
 int ok=s->drain.state==2&&!s->drain.error&&s->desktop_ok&&s->daemon_cleanup&&s->endpoints.cleaned&&!s->endpoints.error&&
  dnds_child_done(&s->frontend)&&dnds_child_done(&s->daemon)&&!s->frontend.error&&!s->daemon.error&&!s->error&&!s->observation_failed&&
  can_transition(s)&&dln_end(&s->rx)==0&&s->peer_close&&s->close_sent&&complete;
 printf("\nDL30_RESULT_%s status=%s desktop_ok=%d frames_out=%llu frames_in=%llu packets_out=%llu packets_in=%llu error=%s errno=%u console_bytes_in=%llu partial_line=%zu partial_bits=%u daemon_cleanup=%d frontend_pid=%d mdns_pid=%d app1_pid=%d app2_pid=%d shell1_pid=%d shell2_pid=%d client1_pid=%d client2_pid=%d cleanup_complete=%d phases=%u if_error=%d if_stage=%u default_add_attempted=%u route_error=%u route_stage=%u close_started_ms=%llu close_deadline_ms=%llu close_completed_ms=%llu finished_ms=%llu\n",s->nonce,ok?"PASS":"FAILED",s->desktop_ok,(unsigned long long)s->frames_out,(unsigned long long)s->frames_in,(unsigned long long)s->packets_out,(unsigned long long)s->packets_in,s->error?s->error:"none",s->errno_saved,(unsigned long long)s->console_bytes_in,s->line_used,s->bit_count,s->daemon_cleanup,s->frontend.pid,s->daemon.pid,s->desktop.app_pid[0],s->desktop.app_pid[1],s->desktop.shell_pid[0],s->desktop.shell_pid[1],s->desktop.client_pid[0],s->desktop.client_pid[1],complete,s->network.owned.phase_count,s->network.owned.first_error,(unsigned)s->network.owned.stage,s->network.lifetime.add_attempted,(unsigned)s->network.lifetime.first_error,(unsigned)s->network.lifetime.stage,(unsigned long long)s->drain.started,(unsigned long long)s->drain.deadline,(unsigned long long)s->drain.completed,(unsigned long long)finished);
 if(fflush(stdout)||ferror(stdout))ok=0;
 uint64_t returned=ms();if(!returned||returned<finished||returned>=s->deadline)ok=0;
 free(s);return ok?0:1;
}
