/* Original single-threaded lifecycle; synthetic fault tests do not prove I/O. */
#include "interface_internal.h"
#include "../codec/egress_validate.h"
#include <errno.h>
#include <string.h>

static int failed(struct dle_interfaces *s, int error) {
  if(!error) error=EIO;
  s->failed=1;
  if(!s->first_error) s->first_error=error;
  errno=s->first_error;
  return -1;
}
static int valid_ops(const struct dle_interface_ops *o) {
  return o && o->inventory && o->query && o->create && o->configure &&
         o->inspect && o->close_fd && o->now && o->pause;
}
int dle_interfaces_init(struct dle_interfaces *s, int exclusive,
    void (*record)(const char *,const uint8_t *,size_t,void *),void *context) {
  if(!s) {errno=EINVAL;return -1;}
  memset(s,0,sizeof(*s));s->fd=-1;s->_owned_fd=-1;
  s->_record=record;s->_context=context;s->cleanup_complete=1;
  if(exclusive!=1) return failed(s,EPERM);
  s->_exclusive=1;return 0;
}
static int absent(const struct dle_route_view *v, uint32_t dst) {
  return v->error==ESRCH && v->have_destination && v->destination==dst;
}
static int active_route(const struct dle_interfaces *s,const struct dle_route_view *v) {
  /* UP/HOST/DONE required. Only optional IFREF may accompany them;
   * plain point-to-point rtinit uses AF_INET local address as direct gateway.
   * Numeric values are checked against the actual SDK in interface_darwin.c. */
  const uint32_t required=0x45, allowed=0x04000045;
  return !v->error && v->have_destination && v->destination==s->peer &&
    v->have_gateway && v->gateway==DLE_LOCAL_IPV4 &&
    v->interface_index==s->index && (v->flags&required)==required &&
    !(v->flags&~allowed) && (!v->have_netmask || v->netmask==UINT32_MAX);
}
int dle_interfaces_close_core(struct dle_interfaces *s,const struct dle_interface_ops *o,void *ctx) {
  if(!s || !valid_ops(o)) {errno=EINVAL;return -1;}
  s->fd=-1;
  if(s->cleanup_complete && s->_owned_fd<0) {
    if(s->failed) {errno=s->first_error;return -1;}
    return 0;
  }
  s->stage=DLE_IF_CLEANUP;
  int fd=s->_owned_fd;s->_owned_fd=-1; /* Never retry ambiguous close. */
  if(fd>=0 && o->close_fd(ctx,fd)) {
    s->_descriptor_uncertain=1;(void)failed(s,errno);
  }
  uint64_t start=o->now(ctx);
  if(!start || start>UINT64_MAX-6000) return failed(s,EIO);
  const uint64_t end=start+6000;
  for(unsigned attempts=0;attempts<64;attempts++) {
    uint64_t now=o->now(ctx);
    if(!now || now>=end) break;
    int inventory=o->inventory(ctx,s,0);
    if(inventory<0) return failed(s,errno);
    if(inventory==0) {
      struct dle_route_view local,peer;
      if(o->query(ctx,DLE_LOCAL_IPV4,&local)) return failed(s,errno);
      now=o->now(ctx); if(!now || now>=end) break;
      if(o->query(ctx,s->peer,&peer)) return failed(s,errno);
      now=o->now(ctx); if(!now || now>=end) break;
      if(absent(&local,DLE_LOCAL_IPV4) && absent(&peer,s->peer)) {
        if(s->_descriptor_uncertain) return failed(s,EIO);
        s->cleanup_complete=1;s->stage=DLE_IF_CLOSED;
        if(s->failed) {errno=s->first_error;return -1;}
        return 0;
      }
    }
    if(o->pause(ctx,25)) return failed(s,errno);
  }
  return failed(s,ETIMEDOUT);
}
int dle_interfaces_open_core(struct dle_interfaces *s,uint32_t peer,
                             const struct dle_interface_ops *o,void *ctx) {
  if(!s || !valid_ops(o)) {errno=EINVAL;return -1;}
  if(s->failed) {errno=s->first_error;return -1;}
  if(!s->_exclusive || s->fd>=0 || s->_owned_fd>=0 || !s->cleanup_complete ||
     s->phase_count>=2) return failed(s,EINVAL);
  uint8_t ip[4]={(uint8_t)(peer>>24),(uint8_t)(peer>>16),(uint8_t)(peer>>8),(uint8_t)peer};
  if((!s->phase_count && peer!=DLE_DNS_IPV4) ||
     (s->phase_count && !eg_ipv4_global(ip))) return failed(s,EINVAL);
  s->stage=DLE_IF_PREFLIGHT;s->peer=peer;s->index=0;s->name[0]=0;s->_identity_known=0;
  int inv=o->inventory(ctx,s,!s->_baseline_known);
  if(inv) return failed(s,inv<0?errno:EBUSY);
  s->_baseline_known=1;
  struct dle_route_view v;
  if(o->query(ctx,DLE_LOCAL_IPV4,&v)) return failed(s,errno);
  if(!absent(&v,DLE_LOCAL_IPV4)) return failed(s,EEXIST);
  if(o->query(ctx,peer,&v)) return failed(s,errno);
  if(!absent(&v,peer)) return failed(s,EEXIST);
  s->cleanup_complete=0;s->stage=DLE_IF_CREATE;
  if(o->create(ctx,s)) goto error;
  if(s->_owned_fd<0 || !s->_identity_known || !s->index || s->index>UINT16_MAX)
    {errno=EPROTO;goto error;}
  s->stage=DLE_IF_CONFIGURE;
  if(o->configure(ctx,s)) goto error;
  s->stage=DLE_IF_READBACK;
  if(o->inspect(ctx,s)) goto error;
  if(o->query(ctx,peer,&v)) goto error;
  if(!active_route(s,&v)) {errno=EPROTO;goto error;}
  if(o->query(ctx,DLE_LOCAL_IPV4,&v)) goto error;
  if(!absent(&v,DLE_LOCAL_IPV4)) {errno=EPROTO;goto error;}
  s->fd=s->_owned_fd;s->phase_count++;s->stage=DLE_IF_ACTIVE;return 0;
error:
  (void)failed(s,errno);
  (void)dle_interfaces_close_core(s,o,ctx);
  errno=s->first_error;return -1;
}
