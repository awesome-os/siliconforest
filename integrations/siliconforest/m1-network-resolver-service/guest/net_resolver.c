/* Original finite native-resolver service + HTTP socket client over the qualified
 * whitespace-framed link. New owned-default interface; one ADD and close-only kernel route cleanup. No physical driver/firmware. */
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

#include "egress_validate.h"
#include "native.h"
#include <sys/sysctl.h>
#include <netinet/tcp.h>
#include "process_context.h"
#include "child_owner.h"
#include "endpoint_owner.h"
#include "probe_record.h"
#include <sys/wait.h>

#define QMAX 16
#define LINE 3300
#define READ_BUDGET 4
#define PUBLIC_HTTP_CAP 2048u
struct item { size_t length,offset; uint8_t data[LINE]; };
struct queue { struct item item[QMAX]; unsigned head,count; };
enum phase { WAIT_OPEN, SERVICE_START, RESOLVING, DNS_DRAIN, HTTP_ACTIVE, HTTP_DRAIN, SERVICE_STOP, CLOSE_WAIT };
struct state {
 struct dln_tx tx; struct dln_rx rx;
 struct queue output,input;
 struct dlf_native network;
 struct dlr_child daemon,probe;
 struct dlr_endpoints endpoints;
 struct dlr_probe_record resolver;
 int resolver_ok,daemon_cleanup,probe_recorded,daemon_recorded;
 uint64_t service_deadline,probe_deadline,stop_deadline;
 char nonce[49];
 size_t line_used; uint8_t bit_value; unsigned bit_count;
 uint64_t console_bytes_in,observation_bytes;
 int tcp,connected,request_sent,http_ok,request_shutdown;
 int peer_open,peer_close,close_sent,tty_saved,flags_saved,old_in,old_out;
 int owner_initialized,processes_exclusive,observation_failed;
 struct termios terminal;
 uint8_t request[EG_HTTP_REQUEST_BYTES],http[PUBLIC_HTTP_CAP+1];
 size_t request_size,request_written,http_size;
 struct eg_http_facts http_facts;
 uint32_t selected;
 enum phase phase;
 uint64_t deadline,stage_at,closing,close_deadline;
 uint64_t frames_out,frames_in,packets_out,packets_in,ignored_family;
 const char *error; unsigned errno_saved;
};
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
static int nonblock(int fd) {
  int f = fcntl(fd, F_GETFL);
  return f < 0 ? -1 : fcntl(fd, F_SETFL, f | O_NONBLOCK);
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
  int head = snprintf(line, sizeof(line), "DL29_TX_%s ", s->nonce);
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
  int n=snprintf(line,sizeof(line),"DL29_ACK_%s bytes=%llu\n",s->nonce,
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
    if (s->console_bytes_in > 131072)
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
static struct sockaddr_in addr(uint32_t ip, uint16_t port) {
  struct sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_len = sizeof(a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(ip);
  a.sin_port = htons(port);
  return a;
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
static int close_fd(struct state *s, int *fd) {
  if (*fd < 0)
    return 0;
  int n = *fd;
  *fd = -1;
  return close(n) < 0 ? fail(s, "close_fd") : 0;
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
  char line[400];int head=snprintf(line,sizeof(line),"DL29_OBSERVE_%s kind=%s total=%zu offset=%zu hex=",s->nonce,kind,n,offset);
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
static void child_observation(struct state *s,const char *kind,const struct dlr_child *c) {
 char line[600];int n=snprintf(line,sizeof(line),"kind=%u pid=%d created=%d reaped=%d lost=%d error=%d wait_status=%d output_bytes=%zu stderr_bytes=%zu output_eof=%d stderr_eof=%d term_sent=%d kill_sent=%d stop_requested=%d started_ms=%llu last_ms=%llu",(unsigned)c->kind,c->pid,c->created,c->reaped,c->lost,c->error,c->wait_status,c->output_used,c->error_used,c->output_eof,c->error_eof,c->term_sent,c->kill_sent,c->stop_requested,(unsigned long long)c->started,(unsigned long long)c->last);
 if(n<=0||(size_t)n>=sizeof(line)){fail(s,"child_observation");return;}
 observe(kind,(const uint8_t *)line,(size_t)n,s);
 char key[64];n=snprintf(key,sizeof(key),"%s_spawn",kind);
 if(n<=0||(size_t)n>=sizeof(key)){fail(s,"child_observation");return;}
 observe(key,(const uint8_t *)&c->spawn,sizeof(c->spawn),s);
 if(c->output_used){n=snprintf(key,sizeof(key),"%s_stdout",kind);if(n<=0||(size_t)n>=sizeof(key)){fail(s,"child_observation");return;}observe(key,c->output,c->output_used,s);}
 if(c->error_used){n=snprintf(key,sizeof(key),"%s_stderr",kind);if(n<=0||(size_t)n>=sizeof(key)){fail(s,"child_observation");return;}observe(key,c->errors,c->error_used,s);}
}
static int service_start(struct state *s,uint64_t now) {
 if(dlr_endpoints_preflight(&s->endpoints))return fail(s,"resolver_endpoint_preflight");
 if(dlr_child_start(&s->daemon,DLR_DAEMON,s->nonce,now))return fail(s,"mdns_spawn");
 s->service_deadline=now+5000;s->phase=SERVICE_START;return 0;
}
static int service_tick(struct state *s,uint64_t now) {
 dlr_child_tick(&s->daemon,now);dlr_child_tick(&s->probe,now);
 if(s->probe.error)return fail(s,"probe_child_error");
 if(s->daemon.error)return fail(s,"mdns_child_error");
 if(s->phase!=SERVICE_STOP&&!s->daemon_cleanup&&s->daemon.reaped)return fail(s,"mdns_exited_early");
 if(s->phase==SERVICE_START) {
  int ready=dlr_endpoints_ready(&s->endpoints,s->daemon.pid);
  if(ready<0)return fail(s,"resolver_endpoint_identity");
  if(ready) {
   if(dlr_child_start(&s->probe,DLR_PROBE,s->nonce,now))return fail(s,"probe_spawn");
   s->probe_deadline=now+15000;s->phase=RESOLVING;
  }else if(now>=s->service_deadline)return fail(s,"mdns_endpoint_deadline");
 }
 if(s->phase==RESOLVING) {
  if(dlr_child_done(&s->probe)) {
   if(!WIFEXITED(s->probe.wait_status)||WEXITSTATUS(s->probe.wait_status)!=0||s->probe.error_used||
      !s->probe.output_eof||!s->probe.error_eof||
      !dlr_probe_parse(s->probe.output,s->probe.output_used,s->nonce,s->probe.pid,&s->resolver))return fail(s,"resolver_result");
   s->selected=s->resolver.ipv4[0];s->resolver_ok=1;s->phase=DNS_DRAIN;s->stage_at=now;
  }else if(now>=s->probe_deadline)return fail(s,"native_resolver_deadline");
 }
 if(s->phase==SERVICE_STOP) {
  if(dlr_child_done(&s->daemon)) {
   if(!WIFEXITED(s->daemon.wait_status)||WEXITSTATUS(s->daemon.wait_status)!=EINTR||!s->daemon.term_sent||s->daemon.kill_sent||
      !s->daemon.output_eof||!s->daemon.error_eof)return fail(s,"mdns_shutdown_status");
   if(!s->daemon_cleanup&&dlr_endpoints_cleanup(&s->endpoints,1))return fail(s,"resolver_endpoint_cleanup");
   s->daemon_cleanup=1;
  }else if(now>=s->stop_deadline)return fail(s,"mdns_reap_deadline");
 }
 return 0;
}
static int http_start(struct state *s,uint64_t now) {
 if(!s->resolver_ok || !now)return fail(s,"resolver_not_ready");
 enum eg_status status=eg_http_request(s->nonce,48,s->request,sizeof(s->request),&s->request_size);
 if(status!=EG_OK)return fail(s,eg_status_name(status));
 s->tcp=socket(AF_INET,SOCK_STREAM,0);
 if(s->tcp<0||nonblock(s->tcp))return fail(s,"http_socket");
 int mss=256; socklen_t size=sizeof(mss);
 if(setsockopt(s->tcp,IPPROTO_TCP,TCP_MAXSEG,&mss,sizeof(mss))<0 || getsockopt(s->tcp,IPPROTO_TCP,TCP_MAXSEG,&mss,&size)<0 || size!=sizeof(mss) || mss!=256)return fail(s,"http_mss_readback");
 struct sockaddr_in to=addr(s->selected,80);
 if(connect(s->tcp,(struct sockaddr *)&to,sizeof(to))==0)s->connected=1;
 else if(errno!=EINPROGRESS)return fail(s,"http_connect");
 s->phase=HTTP_ACTIVE;return 0;
}
static int http_events(struct state *s,short events,uint64_t now) {
 if(!now || now>=s->deadline)return fail(s,"http_deadline");
 if(!s->connected && (events&(POLLOUT|POLLERR|POLLHUP))) {
  int error=0;socklen_t size=sizeof(error);
  if(getsockopt(s->tcp,SOL_SOCKET,SO_ERROR,&error,&size)<0||size!=sizeof(error))return fail(s,"http_connect_query");
  if(error){errno=error;return fail(s,"http_connect_complete");}s->connected=1;
 }
 if(s->connected && !s->request_sent && (events&POLLOUT)) {
  ssize_t n=send(s->tcp,s->request+s->request_written,s->request_size-s->request_written,0);
  if(n<0&&(errno==EAGAIN||errno==EINTR))return 0;
  if(n<=0||(size_t)n>s->request_size-s->request_written)return fail(s,"http_send");
  s->request_written+=(size_t)n;s->request_sent=s->request_written==s->request_size;
 }
 if(events&(POLLIN|POLLHUP)) {
  ssize_t n=recv(s->tcp,s->http+s->http_size,sizeof(s->http)-s->http_size,0);
  if(n<0&&(errno==EAGAIN||errno==EINTR))return 0;
  if(n<0)return fail(s,"http_receive");
  s->http_size+=(size_t)n;
  if(s->http_size>PUBLIC_HTTP_CAP)return fail(s,"http_response_cap");
  enum eg_status status=eg_http_response(s->http,s->http_size,n==0,&s->http_facts);
  if(status!=EG_MORE && status!=EG_OK)return fail(s,eg_status_name(status));
  if(!n) {
   if(!s->request_sent||status!=EG_OK)return fail(s,"http_incomplete_eof");
   s->http_ok=1;
   if(shutdown(s->tcp,SHUT_WR)<0)return fail(s,"http_shutdown");
   s->request_shutdown=1;
   if(close_fd(s,&s->tcp))return -1;
   s->phase=HTTP_DRAIN;s->stage_at=now;
  }
 }
 return 0;
}
static int can_transition(const struct state *s) {
 return !s->output.count && !s->input.count && !s->line_used &&
        !s->bit_count && !s->rx.used;
}
int main(int argc,char **argv) {
 if(argc!=5||strcmp(argv[1],"--nonce")||strcmp(argv[3],"--seconds"))return 2;
 uint8_t nonce[24];unsigned seconds=number(argv[4],75);
 if(seconds!=75||!dln_nonce_hex(argv[2],strlen(argv[2]),nonce)||geteuid()!=0)return 2;
 umask(0077);
 struct state *s=calloc(1,sizeof(*s));if(!s)return 1;
 s->tcp=-1;memcpy(s->nonce,argv[2],49);s->deadline=ms()+seconds*1000;
 struct sigaction a;memset(&a,0,sizeof(a));a.sa_handler=stop_now;sigemptyset(&a.sa_mask);
 if(sigaction(SIGINT,&a,NULL)<0||sigaction(SIGTERM,&a,NULL)<0||sigaction(SIGHUP,&a,NULL)<0){fail(s,"signal_setup");goto cleanup;}
 a.sa_handler=SIG_DFL;
 if(sigaction(SIGCHLD,&a,NULL)<0){fail(s,"signal_setup");goto cleanup;}
 a.sa_handler=SIG_IGN;
 if(sigaction(SIGPIPE,&a,NULL)<0||sigaction(SIGTTOU,&a,NULL)<0){fail(s,"signal_setup");goto cleanup;}
 if(dln_tx_init(&s->tx,nonce,DLNET_GUEST_TO_HOST)||dln_rx_init(&s->rx,nonce,DLNET_HOST_TO_GUEST)){fail(s,"codec_init");goto cleanup;}
 if(tty_raw(s)||inspect_processes(s))goto cleanup;
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
 char ready[180];int n=snprintf(ready,sizeof(ready),"\nDL29_READY_%s interface=%s mtu=1500\n",s->nonce,s->network.owned.name);
 if(n<=0||(size_t)n>=sizeof(ready)||push(&s->output,(uint8_t *)ready,(size_t)n)||emit(s,DLNET_OPEN,NULL,0)){fail(s,"ready_queue");goto cleanup;}
 while(!s->error) {
  uint64_t now=ms();
  if(stopped){fail(s,"signal");break;}
  if(!now||now>=s->deadline){errno=ETIMEDOUT;fail(s,"deadline");break;}
  if(s->phase==WAIT_OPEN && s->peer_open && service_start(s,now))break;
  if(s->daemon.created&&service_tick(s,now))break;
  if(s->phase==DNS_DRAIN && now-s->stage_at>=500 && can_transition(s)) {
   child_observation(s,"probe_child",&s->probe);s->probe_recorded=1;
   if(s->error)break;
   /* The same independently verified default interface remains open for both
    * DNS and HTTP. No route retargeting or phase2/checkpoint is claimed. */
   if(http_start(s,ms()))break;
  }
  if(s->phase==HTTP_DRAIN && now-s->stage_at>=1000 && can_transition(s)) {
   observe("http_request",s->request,s->request_size,s);observe("http_response",s->http,s->http_size,s);
   if(s->error)break;
   dlr_child_stop(&s->daemon,now);s->stop_deadline=now+5000;s->phase=SERVICE_STOP;
  }
  if(s->phase==SERVICE_STOP && s->daemon_cleanup && can_transition(s)) {
   child_observation(s,"mdns_child",&s->daemon);s->daemon_recorded=1;
   if(s->error)break;
   s->closing=ms();s->phase=CLOSE_WAIT;
   if(emit(s,DLNET_CLOSE,NULL,0))break;
   s->close_sent=1;s->close_deadline=s->closing+15000;
  }
  if(s->close_sent&&s->peer_close&&!s->output.count&&!s->input.count) {
   if(s->line_used||s->bit_count||dln_end(&s->rx)){fail(s,"unclean_stream_end");break;}
   break;
  }
  if(s->close_sent&&now>=s->close_deadline){errno=ETIMEDOUT;fail(s,"close_deadline");break;}
  struct pollfd f[4]={{0,POLLIN,0},{1,s->output.count?POLLOUT:0,0},
   {dlf_native_fd(&s->network),(short)((s->peer_open&&!s->close_sent&&s->output.count<QMAX-2?POLLIN:0)|(s->input.count?POLLOUT:0)),0},
   {s->tcp,(short)(POLLIN|(!s->request_sent?POLLOUT:0)),0}};
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
  if(s->phase==HTTP_ACTIVE && http_events(s,f[3].revents,ms()))break;
 }
cleanup:
 /* Failure also retires both sole-owned children before interface teardown.
  * Bounded drains remain reachable even after an output cap or signal. No
  * descendant/session claims: the two source-bound children never fork. */
 { uint64_t start=ms(),end=start+5000;dlr_child_stop(&s->probe,start);dlr_child_stop(&s->daemon,start);
  for(unsigned attempt=0;attempt<600;attempt++) {
   uint64_t now=ms();dlr_child_tick(&s->probe,now);dlr_child_tick(&s->daemon,now);
   int probe_done=!s->probe.created||dlr_child_done(&s->probe)||s->probe.lost;
   int daemon_done=!s->daemon.created||dlr_child_done(&s->daemon)||s->daemon.lost;
   if(probe_done&&daemon_done)break;
   if(!now||now>=end)break;
   struct timeval pause={0,10000};(void)select(0,NULL,NULL,NULL,&pause);
  }
  if(s->probe.created&&!dlr_child_done(&s->probe))fail(s,"probe_cleanup_incomplete");
  if(s->daemon.created&&!dlr_child_done(&s->daemon))fail(s,"mdns_cleanup_incomplete");
  if(s->endpoints.initialized&&!s->endpoints.cleaned&&s->daemon.reaped&&dlr_endpoints_cleanup(&s->endpoints,1))fail(s,"resolver_endpoint_final_cleanup");
  if(s->probe.created&&!s->probe_recorded)child_observation(s,"probe_child",&s->probe);
  if(s->daemon.created&&!s->daemon_recorded)child_observation(s,"mdns_child",&s->daemon);
 }
 close_fd(s,&s->tcp);
 if(s->owner_initialized && dlf_native_close(&s->network))fail(s,"interface_final_cleanup");
 if(s->owner_initialized)observe_interface(s,"final_cleanup");
 int complete=s->owner_initialized&&!s->network.owned.failed&&s->network.owned.phase_count==1&&s->network.owned.cleanup_complete&&
  s->network.lifetime.cleanup_complete&&!s->network.lifetime.first_error&&s->network.lifetime.stage==DLF_CLOSED&&s->network.lifetime.add_attempted==1;
 if(s->tty_saved&&tcsetattr(0,TCSANOW,&s->terminal)<0)fail(s,"terminal_restore");
 if(s->flags_saved){int a=fcntl(0,F_SETFL,s->old_in),b=fcntl(1,F_SETFL,s->old_out);if(a<0||b<0)fail(s,"flags_restore");}
 int ok=s->resolver_ok&&s->daemon_cleanup&&s->endpoints.cleaned&&!s->endpoints.error&&dlr_child_done(&s->probe)&&dlr_child_done(&s->daemon)&&!s->probe.error&&!s->daemon.error&&!s->error&&!s->observation_failed&&can_transition(s)&&dln_end(&s->rx)==0&&s->resolver_ok&&s->http_ok&&s->request_shutdown&&s->peer_close&&s->close_sent&&complete;
 printf("\nDL29_RESULT_%s status=%s resolver_ok=%d http_ok=%d frames_out=%llu frames_in=%llu packets_out=%llu packets_in=%llu error=%s errno=%u console_bytes_in=%llu partial_line=%zu partial_bits=%u daemon_cleanup=%d probe_pid=%d mdns_pid=%d resolver_addresses=%u selected=%u cleanup_complete=%d phases=%u http_bytes=%zu if_error=%d if_stage=%u default_add_attempted=%u route_error=%u route_stage=%u\n",s->nonce,ok?"PASS":"FAILED",s->resolver_ok,s->http_ok,(unsigned long long)s->frames_out,(unsigned long long)s->frames_in,(unsigned long long)s->packets_out,(unsigned long long)s->packets_in,s->error?s->error:"none",s->errno_saved,(unsigned long long)s->console_bytes_in,s->line_used,s->bit_count,s->daemon_cleanup,s->probe.pid,s->daemon.pid,s->resolver.count,s->selected,complete,s->network.owned.phase_count,s->http_size,s->network.owned.first_error,(unsigned)s->network.owned.stage,s->network.lifetime.add_attempted,(unsigned)s->network.lifetime.first_error,(unsigned)s->network.lifetime.stage);
 if(fflush(stdout)||ferror(stdout))ok=0;free(s);return ok?0:1;
}
