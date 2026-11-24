#include "default_route.h"
#include <string.h>
static int fault(struct dlf_session *s,enum dlf_error e) {
    if(!s->first_error)s->first_error=e;
    return -1;
}
static int owner_valid(const struct dlf_owner *o) {
    if(!o->index || strncmp(o->name,"utun",4))return 0;
    size_t n=0;for(;n<sizeof(o->name) && o->name[n];n++);
    if(n<5 || n==sizeof(o->name))return 0;
    for(size_t i=4;i<n;i++)if(o->name[i]<'0'||o->name[i]>'9')return 0;
    return 1;
}
int dlf_route_matches(const struct dlf_route *r,const struct dlf_owner *o) {
    return r && o && owner_valid(o) && !r->error &&
      r->have_destination && !r->destination && r->have_gateway && r->gateway==DLF_PEER &&
      r->have_netmask && !r->netmask && r->have_ifa && r->ifa==DLF_LOCAL &&
      r->have_ifp && r->index==o->index && r->ifp_index==o->index &&
      !memcmp(r->ifp_name,o->name,sizeof(o->name)) &&
      (r->flags&DLF_REQUIRED_FLAGS)==DLF_REQUIRED_FLAGS && !(r->flags&~DLF_ALLOWED_FLAGS);
}
static int absent(const struct dlf_route *r) {
    /* Darwin ESRCH=3. A successful longest-prefix route is never absence. */
    return r->error==3 && r->have_destination && !r->destination;
}
int dlf_init(struct dlf_session *s,const struct dlf_ops *o,void *c,int exclusive) {
    if(!s||!o||!o->get_default||!o->open_owner||!o->add_default||!o->close_owner||!o->baseline_equal)return -1;
    const unsigned char *bytes=(const unsigned char *)s;
    for(size_t i=0;i<sizeof(*s);i++)if(bytes[i])return -1;
    memset(s,0,sizeof(*s));s->ops=o;s->context=c;s->cleanup_complete=1;
    if(exclusive!=1)return fault(s,DLF_CONTEXT);
    s->exclusive=1;return 0;
}
int dlf_close(struct dlf_session *s) {
    if(!s||!s->ops)return -1;
    if(s->stage==DLF_CLOSED)return s->first_error?-1:0;
    if(!s->owner_started) {
        s->stage=DLF_CLOSED;return s->first_error?-1:0;
    }
    s->stage=DLF_CLOSING;s->cleanup_complete=0;
    if(s->close_attempted)return fault(s,DLF_OWNER_CLOSE);
    s->close_attempted=1; /* A failed or ambiguous close is never retried. */
    int closed=s->ops->close_owner(s->context);
    if(closed)fault(s,DLF_OWNER_CLOSE);
    struct dlf_route r={0};
    int got=s->ops->get_default(s->context,&r);
    if(got || !absent(&r))fault(s,DLF_ROUTE_REMAINS);
    int inventory=s->ops->baseline_equal(s->context);
    if(inventory)fault(s,DLF_BASELINE);
    s->cleanup_complete=!closed&&!got&&absent(&r)&&!inventory;
    s->stage=DLF_CLOSED;return s->first_error?-1:0;
}
int dlf_open(struct dlf_session *s) {
    if(!s||!s->ops)return -1;
    if(s->first_error || !s->exclusive || s->stage!=DLF_NEW)return fault(s,DLF_ARGUMENT);
    struct dlf_route r={0};
    if(s->ops->get_default(s->context,&r) || !absent(&r))return fault(s,DLF_PREFLIGHT);
    s->stage=DLF_OPENING;s->owner_started=1;s->cleanup_complete=0;
    if(s->ops->open_owner(s->context,&s->owner)){fault(s,DLF_OWNER_OPEN);goto failed;}
    if(!owner_valid(&s->owner)){fault(s,DLF_OWNER_IDENTITY);goto failed;}
    memset(&r,0,sizeof(r));
    if(s->ops->get_default(s->context,&r) || !absent(&r)){fault(s,DLF_PREFLIGHT);goto failed;}
    s->stage=DLF_ADDING;s->add_attempted=1;
    if(s->ops->add_default(s->context,&s->owner)){fault(s,DLF_ADD);goto failed;}
    memset(&r,0,sizeof(r));
    if(s->ops->get_default(s->context,&r) || !dlf_route_matches(&r,&s->owner)){
        fault(s,DLF_READBACK);goto failed;
    }
    s->stage=DLF_ACTIVE;return 0;
failed:
    (void)dlf_close(s);return -1;
}
