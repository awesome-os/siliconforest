/* Original finite real Darwin DNS + HTTP socket client over the qualified
 * whitespace-framed link. No physical driver/firmware or explicit route writes. */
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
#include "owned_interfaces.h"
#include <sys/sysctl.h>
#include <netinet/tcp.h>
#include "process_context.h"

#define QMAX 16
#define LINE 3300
#define READ_BUDGET 4
#define PUBLIC_HTTP_CAP 2048u
struct item { size_t length,offset; uint8_t data[LINE]; };
struct queue { struct item item[QMAX]; unsigned head,count; };
enum phase { WAIT_OPEN, DNS_ACTIVE, DNS_DRAIN, HTTP_ACTIVE, HTTP_DRAIN, CLOSE_WAIT };
struct state {
 struct dln_tx tx; struct dln_rx rx;
 struct queue output,input;
 struct dle_interfaces interfaces;
 char nonce[49];
 size_t line_used; uint8_t bit_value; unsigned bit_count;
 uint64_t console_bytes_in,observation_bytes;
 int tcp,udp,connected,request_sent,dns_ok,http_ok,request_shutdown;
 int peer_open,peer_close,close_sent,tty_saved,flags_saved,old_in,old_out;
 int owner_initialized,processes_exclusive,dns_boundary_sent;
 struct termios terminal;
 uint8_t query[EG_DNS_QUERY_BYTES],dns_reply[EG_DNS_MAX+1];
 size_t query_size,dns_size;
 struct eg_dns_answer dns;
 uint8_t request[EG_HTTP_REQUEST_BYTES],http[PUBLIC_HTTP_CAP+1];
 size_t request_size,request_written,http_size;
 struct eg_http_facts http_facts;
 uint32_t selected;
 enum phase phase;
 uint64_t deadline,dns_deadline,lease_deadline,stage_at,closing,close_deadline;
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
  int head = snprintf(line, sizeof(line), "DL27_TX_%s ", s->nonce);
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
  int n=snprintf(line,sizeof(line),"DL27_ACK_%s bytes=%llu\n",s->nonce,
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
static int wait_ready(struct pollfd f[5]) {
  fd_set read_set, write_set, except_set;
  FD_ZERO(&read_set);
  FD_ZERO(&write_set);
  FD_ZERO(&except_set);
  int high = -1;
  for (unsigned i = 0; i < 5; i++) {
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
  for (unsigned i = 0; i < 5; i++) {
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
static void observe(const char *kind,const uint8_t *data,size_t n,void *context) {
 struct state *s=context;
 if(s->error)return;
 if(!kind || strlen(kind)>63 || n>4096 || (!data&&n)) {fail(s,"observation_bounds");return;}
 for(const char *p=kind;*p;p++) if(!((*p>='a'&&*p<='z')||*p=='_'||(*p>='0'&&*p<='9'))) {fail(s,"observation_kind");return;}
 static const char hex[]="0123456789abcdef";
 size_t offset=0;
 do {
  size_t part=n-offset;if(part>64)part=64;
  char line[400];int head=snprintf(line,sizeof(line),"DL27_OBSERVE_%s kind=%s total=%zu offset=%zu hex=",s->nonce,kind,n,offset);
  if(head<0 || (size_t)head+2*part+1>sizeof(line)) {fail(s,"observation_format");return;}
  for(size_t i=0;i<part;i++){line[head+2*i]=hex[data[offset+i]>>4];line[head+2*i+1]=hex[data[offset+i]&15];}
  size_t length=(size_t)head+2*part;line[length++]='\n';
  if(s->observation_bytes+length>65536) {fail(s,"observation_cap");return;}
  uint64_t start=ms();
  if(!start || start>UINT64_MAX-2000){fail(s,"observation_clock");return;}
  size_t sent=0;uint64_t end=start+2000;unsigned attempts=0;
  while(sent<length) {
   uint64_t now=ms();
   if(stopped || !now || now>=end || ++attempts>128) {errno=ETIMEDOUT;fail(s,"observation_write_deadline");return;}
   fd_set w;FD_ZERO(&w);FD_SET(1,&w);struct timeval wait={0,20000};
   int ready=select(2,NULL,&w,NULL,&wait);
   if(ready<0 && errno==EINTR)continue;
   if(ready<0){fail(s,"observation_select");return;}
   if(!ready)continue;
   ssize_t wrote=write(1,line+sent,length-sent);
   if(wrote<0&&(errno==EAGAIN||errno==EINTR))continue;
   if(wrote<=0){fail(s,"observation_write");return;}
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
 char line[140];int n=snprintf(line,sizeof(line),"phase=%u name=%s index=%u peer=%u.%u.%u.%u cleanup=%d failed=%d",s->interfaces.phase_count,s->interfaces.name,s->interfaces.index,(s->interfaces.peer>>24)&255,(s->interfaces.peer>>16)&255,(s->interfaces.peer>>8)&255,s->interfaces.peer&255,s->interfaces.cleanup_complete,s->interfaces.failed);
 if(n<0||(size_t)n>=sizeof(line)){fail(s,"interface_observation");return;}
 observe(kind,(uint8_t *)line,(size_t)n,s);
}
static int dns_start(struct state *s,uint64_t now) {
 uint8_t nonce[24];
 if(!dln_nonce_hex(s->nonce,48,nonce))return fail(s,"nonce");
 uint16_t id=(uint16_t)((unsigned)nonce[0]<<8|nonce[1]);
 enum eg_status status=eg_dns_query(id,s->query,sizeof(s->query),&s->query_size);
 if(status!=EG_OK)return fail(s,eg_status_name(status));
 s->udp=socket(AF_INET,SOCK_DGRAM,0);
 if(s->udp<0||nonblock(s->udp))return fail(s,"dns_socket");
 struct sockaddr_in to=addr(0x0a000203,53);
 if(sendto(s->udp,s->query,s->query_size,0,(struct sockaddr *)&to,sizeof(to))!=(ssize_t)s->query_size)return fail(s,"dns_send");
 s->dns_deadline=now+10000;s->phase=DNS_ACTIVE;return 0;
}
static int dns_receive(struct state *s,uint64_t now) {
 struct sockaddr_in from;memset(&from,0,sizeof(from));socklen_t length=sizeof(from);
 ssize_t n=recvfrom(s->udp,s->dns_reply,sizeof(s->dns_reply),0,(struct sockaddr *)&from,&length);
 if(n<0&&(errno==EAGAIN||errno==EINTR))return 0;
 if(n<=0 || length!=sizeof(from) || from.sin_family!=AF_INET)return fail(s,"dns_receive");
 s->dns_size=(size_t)n;uint8_t source[4];memcpy(source,&from.sin_addr.s_addr,4);
 enum eg_status status=eg_dns_reply(s->query,s->query_size,s->dns_reply,s->dns_size,source,ntohs(from.sin_port),&s->dns);
 if(status!=EG_OK)return fail(s,eg_status_name(status));
 memcpy(&s->selected,s->dns.ipv4[0],4);s->selected=ntohl(s->selected);
 s->lease_deadline=now+(uint64_t)s->dns.lease_seconds*1000;s->dns_ok=1;
 if(close_fd(s,&s->udp))return -1;
 s->phase=DNS_DRAIN;s->stage_at=now;return 0;
}
static int http_start(struct state *s,uint64_t now) {
 if(!s->dns_ok || !now || now>=s->lease_deadline)return fail(s,"dns_lease_expired");
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
 if(!now || now>=s->lease_deadline)return fail(s,"dns_lease_expired");
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
static int observe_dns(struct state *s) {
 observe("dns_query",s->query,s->query_size,s);
 observe("dns_reply",s->dns_reply,s->dns_size,s);
 char line[100];int n=snprintf(line,sizeof(line),"selected=%u.%u.%u.%u ttl=%u count=%u",s->dns.ipv4[0][0],s->dns.ipv4[0][1],s->dns.ipv4[0][2],s->dns.ipv4[0][3],s->dns.lease_seconds,s->dns.count);
 if(n<0||(size_t)n>=sizeof(line))return fail(s,"dns_facts_format");
 observe("dns_facts",(uint8_t *)line,(size_t)n,s);return s->error?-1:0;
}
static int can_transition(const struct state *s) {
 return !s->output.count && !s->input.count && !s->line_used &&
        !s->bit_count && !s->rx.used;
}
int main(int argc,char **argv) {
 if(argc!=5||strcmp(argv[1],"--nonce")||strcmp(argv[3],"--seconds"))return 2;
 uint8_t nonce[24];unsigned seconds=number(argv[4],75);
 if(seconds!=75||!dln_nonce_hex(argv[2],strlen(argv[2]),nonce)||geteuid()!=0)return 2;
 struct state *s=calloc(1,sizeof(*s));if(!s)return 1;
 s->tcp=s->udp=-1;memcpy(s->nonce,argv[2],49);s->deadline=ms()+seconds*1000;
 struct sigaction a;memset(&a,0,sizeof(a));a.sa_handler=stop_now;sigemptyset(&a.sa_mask);
 if(sigaction(SIGINT,&a,NULL)<0||sigaction(SIGTERM,&a,NULL)<0||sigaction(SIGHUP,&a,NULL)<0){fail(s,"signal_setup");goto cleanup;}
 a.sa_handler=SIG_IGN;
 if(sigaction(SIGPIPE,&a,NULL)<0||sigaction(SIGTTOU,&a,NULL)<0){fail(s,"signal_setup");goto cleanup;}
 if(dln_tx_init(&s->tx,nonce,DLNET_GUEST_TO_HOST)||dln_rx_init(&s->rx,nonce,DLNET_HOST_TO_GUEST)){fail(s,"codec_init");goto cleanup;}
 if(tty_raw(s)||inspect_processes(s))goto cleanup;
 s->owner_initialized=1;
 if(dle_interfaces_init(&s->interfaces,s->processes_exclusive,observe,s)||s->error){fail(s,"interface_init");goto cleanup;}
 if(dle_interfaces_open(&s->interfaces,DLE_DNS_IPV4)||s->error){fail(s,"dns_interface_open");goto cleanup;}
 observe_interface(s,"dns_interface");
 for(size_t i=0;i<s->interfaces._baseline_count;i++) {
  char line[60];int n=snprintf(line,sizeof(line),"index=%u name=%s",s->interfaces._baseline[i].index,s->interfaces._baseline[i].name);
  if(n<0||(size_t)n>=sizeof(line)){fail(s,"baseline_observation");goto cleanup;}
  observe("baseline_interface",(uint8_t *)line,(size_t)n,s);
 }
 if(s->error)goto cleanup;
 char ready[180];int n=snprintf(ready,sizeof(ready),"\nDL27_READY_%s interface=%s mtu=1500\n",s->nonce,s->interfaces.name);
 if(n<=0||(size_t)n>=sizeof(ready)||push(&s->output,(uint8_t *)ready,(size_t)n)||emit(s,DLNET_OPEN,NULL,0)){fail(s,"ready_queue");goto cleanup;}
 while(!s->error) {
  uint64_t now=ms();
  if(stopped){fail(s,"signal");break;}
  if(!now||now>=s->deadline){errno=ETIMEDOUT;fail(s,"deadline");break;}
  if(s->phase==WAIT_OPEN && s->peer_open && dns_start(s,now))break;
  if(s->phase==DNS_ACTIVE && now>=s->dns_deadline){errno=ETIMEDOUT;fail(s,"dns_deadline");break;}
  if(s->phase==DNS_DRAIN && now-s->stage_at>=500 && can_transition(s)) {
   if(!s->dns_boundary_sent) {
    char line[180];int n=snprintf(line,sizeof(line),"DL27_DNS_BOUNDARY_%s frames=%llu bytes=%llu\n",s->nonce,(unsigned long long)s->frames_in,(unsigned long long)s->console_bytes_in);
    if(n<=0||(size_t)n>=sizeof(line)||push(&s->output,(uint8_t *)line,(size_t)n)){fail(s,"dns_boundary_queue");break;}
    s->dns_boundary_sent=1;continue;
   }
   if(observe_dns(s))break;
   if(dle_interfaces_close(&s->interfaces)||s->error){fail(s,"dns_interface_cleanup");break;}
   observe_interface(s,"dns_cleanup");
   if(dle_interfaces_open(&s->interfaces,s->selected)||s->error){fail(s,"http_interface_open");break;}
   observe_interface(s,"http_interface");
   if(s->error)break;
   if(http_start(s,ms()))break;
  }
  if(s->phase==HTTP_DRAIN && now-s->stage_at>=1000 && can_transition(s)) {
   observe("http_request",s->request,s->request_size,s);observe("http_response",s->http,s->http_size,s);
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
  struct pollfd f[5]={{0,POLLIN,0},{1,s->output.count?POLLOUT:0,0},
   {s->interfaces.fd,(short)((s->peer_open&&!s->close_sent&&s->output.count<QMAX-2?POLLIN:0)|(s->input.count?POLLOUT:0)),0},
   {s->tcp,(short)(POLLIN|(!s->request_sent?POLLOUT:0)),0},{s->udp,POLLIN,0}};
  int result=wait_ready(f);
  if(result<0){if(errno==EINTR)continue;fail(s,"select");break;}
  if(f[0].revents&(POLLIN|POLLHUP)) {
   uint64_t before=s->console_bytes_in;
   if(read_console(s))break;
   if(s->console_bytes_in!=before&&acknowledge(s))break;
  }
  if((f[1].revents&POLLOUT)&&flush(s,&s->output,1,0))break;
  if((f[2].revents&POLLOUT)&&flush(s,&s->input,s->interfaces.fd,1))break;
  if(f[2].revents&POLLIN) {
   uint8_t packet[DLNET_MTU+5];ssize_t nread=read(s->interfaces.fd,packet,sizeof(packet));
   if(nread<0&&(errno==EAGAIN||errno==EINTR))continue;
   if(nread<4||nread>DLNET_MTU+4){fail(s,"utun_packet_length");break;}
   uint32_t family;memcpy(&family,packet,4);
   if(ntohl(family)!=AF_INET){if(++s->ignored_family>128){fail(s,"utun_family_cap");break;}continue;}
   if(emit(s,DLNET_IPV4,packet+4,(size_t)nread-4))break;
  }
  if(s->phase==DNS_ACTIVE && (f[4].revents&POLLIN) && dns_receive(s,ms()))break;
  if(s->phase==HTTP_ACTIVE && http_events(s,f[3].revents,ms()))break;
 }
cleanup:
 close_fd(s,&s->tcp);close_fd(s,&s->udp);
 if(s->owner_initialized && dle_interfaces_close(&s->interfaces))fail(s,"interface_final_cleanup");
 if(s->owner_initialized)observe_interface(s,"final_cleanup");
 int complete=s->owner_initialized&&!s->interfaces.failed&&s->interfaces.phase_count==2&&s->interfaces.cleanup_complete;
 if(s->tty_saved&&tcsetattr(0,TCSANOW,&s->terminal)<0)fail(s,"terminal_restore");
 if(s->flags_saved){int a=fcntl(0,F_SETFL,s->old_in),b=fcntl(1,F_SETFL,s->old_out);if(a<0||b<0)fail(s,"flags_restore");}
 int ok=!s->error&&s->dns_ok&&s->http_ok&&s->request_shutdown&&s->peer_close&&s->close_sent&&complete;
 printf("\nDL27_RESULT_%s status=%s dns_ok=%d http_ok=%d frames_out=%llu frames_in=%llu packets_out=%llu packets_in=%llu error=%s errno=%u console_bytes_in=%llu partial_line=%zu partial_bits=%u cleanup_complete=%d phases=%u http_bytes=%zu if_error=%d if_stage=%u\n",s->nonce,ok?"PASS":"FAILED",s->dns_ok,s->http_ok,(unsigned long long)s->frames_out,(unsigned long long)s->frames_in,(unsigned long long)s->packets_out,(unsigned long long)s->packets_in,s->error?s->error:"none",s->errno_saved,(unsigned long long)s->console_bytes_in,s->line_used,s->bit_count,complete,s->interfaces.phase_count,s->http_size,s->interfaces.first_error,(unsigned)s->interfaces.stage);
 if(fflush(stdout)||ferror(stdout))ok=0;free(s);return ok?0:1;
}
