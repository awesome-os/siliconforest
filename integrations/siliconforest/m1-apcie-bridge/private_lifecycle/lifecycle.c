/* Original private metadata lifecycle. No MMIO, provider or hardware API. */
#include "lifecycle.h"
#define MAGIC UINT64_C(0x42504c4d45544131)
#define PA_LIMIT UINT64_C(0x1000000000)
#define IOVA_BASE UINT64_C(0x100000)
#define IOVA_END UINT64_C(0xbff00000)

static int range(uint64_t base, uint64_t bytes, uint64_t limit)
{ return bytes && base < limit && bytes <= limit-base; }
static int overlap(uint64_t a, uint64_t n, uint64_t b, uint64_t k)
{ return a < b+k && b < a+n; } /* only previously bounded spans */
static int alias(const void *a, size_t n, const void *b, size_t k)
{
    uintptr_t x=(uintptr_t)a, y=(uintptr_t)b;
    if (n>UINTPTR_MAX-x || k>UINTPTR_MAX-y) return 1;
    return x<y+k && y<x+n;
}
static int live(uint32_t state)
{ return state>=BPL_CLAIMED && state<=BPL_QUARANTINED; }
static int valid(const struct bpl_registry *r)
{ return r && r->magic==MAGIC; }
static int entry(const struct bpl_registry *r, const struct bpl_lease *l,
                 const struct bpl_entry **out)
{
    if (!valid(r) || !l) return BPL_ARGUMENT;
    if (l->registry!=r || l->slot>=BPL_SLOTS || !l->generation) return BPL_STALE;
    const struct bpl_entry *e=&r->entries[l->slot];
    if (!live(e->state) || e->generation!=l->generation) return BPL_STALE;
    *out=e; return BPL_OK;
}
static void quarantine(struct bpl_entry *e, int32_t error)
{
    if (!e->first_error) e->first_error=error;
    e->state=BPL_QUARANTINED;
}
static int policy_valid(const struct bpl_policy *p)
{
    if (!p || !p->platform_domain || !p->dart_domain || !p->irq_domain ||
        !p->ram_count || p->ram_count>BPL_RANGES || p->reserved_count>BPL_RANGES) return 0;
    for (unsigned group=0;group<2;group++) {
        const struct bpl_range *v=group?p->reserved:p->ram;
        uint32_t n=group?p->reserved_count:p->ram_count;
        for (uint32_t i=0;i<n;i++) {
            if (!range(v[i].base,v[i].bytes,PA_LIMIT)) return 0;
            for (uint32_t j=0;j<i;j++)
                if (overlap(v[i].base,v[i].bytes,v[j].base,v[j].bytes)) return 0;
        }
    }
    return 1;
}
int bpl_init(struct bpl_registry *r, const struct bpl_policy *p)
{
    if (!r || !p || alias(r,sizeof *r,p,sizeof *p)) return BPL_ARGUMENT;
    const unsigned char *bytes=(const unsigned char *)r;
    for (size_t i=0;i<sizeof *r;i++) if (bytes[i]) return BPL_STATE;
    if (!policy_valid(p)) return BPL_RANGE;
    r->policy=*p; r->next_generation=1; r->magic=MAGIC; return BPL_OK;
}
int bpl_claim(struct bpl_registry *r, const struct bpl_request *q, struct bpl_lease *out)
{
    if (!valid(r) || !q || !out || alias(r,sizeof *r,q,sizeof *q) ||
        alias(r,sizeof *r,out,sizeof *out) || alias(q,sizeof *q,out,sizeof *out)) return BPL_ARGUMENT;
    if (q->rid<0x100 || q->rid>0xffff || q->sid>15 || !q->vectors || (q->vectors&~255u)) return BPL_RANGE;
    if (r->exhausted || !r->next_generation) return BPL_LIMIT;
    uint32_t slot=BPL_SLOTS;
    for (uint32_t i=0;i<BPL_SLOTS;i++) {
        const struct bpl_entry *e=&r->entries[i];
        if (live(e->state)) {
            if (e->request.rid==q->rid || e->request.sid==q->sid ||
                (e->request.vectors&q->vectors)) return BPL_CONFLICT;
        } else if (slot==BPL_SLOTS) slot=i;
    }
    if (slot==BPL_SLOTS) return BPL_LIMIT;
    /* All refusal checks precede this bounded, callback-free commit. No large
     * automatic entry copy; only an unused/retired metadata slot is cleared. */
    struct bpl_entry *e=&r->entries[slot];
    unsigned char *bytes=(unsigned char *)e;
    for (size_t i=0;i<sizeof *e;i++) bytes[i]=0;
    e->generation=r->next_generation; e->request=*q; e->state=BPL_CLAIMED;
    if (r->next_generation==UINT64_MAX) r->exhausted=1;
    else r->next_generation++;
    *out=(struct bpl_lease){r,e->generation,slot}; return BPL_OK;
}
static int span_valid(const struct bpl_registry *r, const struct bpl_span *s, uint32_t role)
{
    if (s->role!=role || !s->handle || !s->generation || s->bytes!=BPL_PAGE ||
        !s->cpu || !range(s->cpu,s->bytes,UINT64_MAX) ||
        !range(s->pa,s->bytes,PA_LIMIT) || ((s->cpu|s->pa)&(BPL_PAGE-1))) return 0;
    if (role==BPL_DATA) {
        if (!s->permissions || (s->permissions&~(BPL_READ|BPL_WRITE)) ||
            s->iova<IOVA_BASE || !range(s->iova,s->bytes,IOVA_END) ||
            (s->iova&(BPL_PAGE-1))) return 0;
    } else if (s->permissions || s->iova) return 0;
    int in_ram=0;
    for (uint32_t i=0;i<r->policy.ram_count;i++) {
        struct bpl_range p=r->policy.ram[i];
        if (s->pa>=p.base && s->bytes<=p.bytes && s->pa-p.base<=p.bytes-s->bytes) in_ram=1;
    }
    if (!in_ram) return 0;
    for (uint32_t i=0;i<r->policy.reserved_count;i++) {
        struct bpl_range p=r->policy.reserved[i];
        if (overlap(s->pa,s->bytes,p.base,p.bytes)) return 0;
    }
    return 1;
}
static int spans_conflict(const struct bpl_span *a, const struct bpl_span *b)
{
    return a->handle==b->handle || overlap(a->cpu,a->bytes,b->cpu,b->bytes) ||
        overlap(a->pa,a->bytes,b->pa,b->bytes) ||
        (a->role==BPL_DATA && b->role==BPL_DATA && overlap(a->iova,a->bytes,b->iova,b->bytes));
}
int bpl_bind_metadata(struct bpl_registry *r, const struct bpl_lease *l,
                      const struct bpl_span s[BPL_SPANS])
{
    const struct bpl_entry *found;
    int rc=entry(r,l,&found); if (rc) return rc;
    if (!s || alias(r,sizeof *r,s,sizeof *s*BPL_SPANS) || alias(r,sizeof *r,l,sizeof *l)) return BPL_ARGUMENT;
    if (found->state!=BPL_CLAIMED) return BPL_STATE;
    for (uint32_t i=0;i<BPL_SPANS;i++) {
        if (!span_valid(r,&s[i],i+1)) return BPL_RANGE;
        if (overlap(s[i].cpu,s[i].bytes,(uint64_t)(uintptr_t)r,sizeof *r)) return BPL_CONFLICT;
        for (uint32_t j=0;j<i;j++) if (spans_conflict(&s[i],&s[j])) return BPL_CONFLICT;
        for (uint32_t k=0;k<BPL_SLOTS;k++) {
            const struct bpl_entry *e=&r->entries[k];
            if (!live(e->state)) continue;
            for (uint32_t j=0;j<e->span_count;j++)
                if (spans_conflict(&s[i],&e->spans[j])) return BPL_CONFLICT;
        }
    }
    struct bpl_entry *e=&r->entries[l->slot];
    for (uint32_t i=0;i<BPL_SPANS;i++) e->spans[i]=s[i];
    e->span_count=BPL_SPANS; e->state=BPL_PRIVATE_METADATA; return BPL_OK;
}
int bpl_effect_begin(struct bpl_registry *r, const struct bpl_lease *l,
                     uint32_t kind, uint32_t resources, struct bpl_ticket *out)
{
    const struct bpl_entry *found;
    int rc=entry(r,l,&found); if (rc) return rc;
    if (!out || alias(r,sizeof *r,l,sizeof *l) || alias(r,sizeof *r,out,sizeof *out) ||
        alias(l,sizeof *l,out,sizeof *out) || kind<BPL_CONFIG || kind>BPL_INTERRUPT ||
        !resources || (resources&~BPL_RESOURCES)) return BPL_ARGUMENT;
    if (found->state==BPL_QUARANTINED || found->pending) return BPL_STATE;
    static const uint32_t required[]={0,8,7,12,12,8,16};
    if ((resources&required[kind])!=required[kind]) return BPL_ARGUMENT;
    if ((resources&7u) && found->span_count!=BPL_SPANS) return BPL_STATE;
    if (found->effect_count==BPL_EFFECTS) return BPL_LIMIT;
    struct bpl_entry *e=&r->entries[l->slot];
    uint32_t seq=e->effect_count+1;
    e->effects[e->effect_count]=(struct bpl_effect){seq,kind,resources,BPL_PENDING,0};
    e->effect_count=seq; e->pending=seq; e->ever_resources|=resources;
    e->state=BPL_EFFECT_RECORDED;
    *out=(struct bpl_ticket){*l,seq}; return BPL_OK;
}
int bpl_effect_result(struct bpl_registry *r, const struct bpl_ticket *t,
                      uint32_t outcome, int32_t error)
{
    const struct bpl_entry *found;
    if (!t) return BPL_ARGUMENT;
    int rc=entry(r,&t->lease,&found); if (rc) return rc;
    if (alias(r,sizeof *r,t,sizeof *t) || outcome<BPL_REPORTED_SUCCESS || outcome>BPL_UNKNOWN ||
        (outcome==BPL_REPORTED_SUCCESS ? error!=0 : error==0)) return BPL_ARGUMENT;
    struct bpl_entry *e=&r->entries[t->lease.slot];
    if (e->state==BPL_QUARANTINED) return BPL_RETAINED;
    if (!t->sequence || !e->pending || t->sequence!=e->pending) {
        quarantine(e,BPL_STALE); return BPL_STALE;
    }
    e->effects[e->pending-1].outcome=outcome;
    e->effects[e->pending-1].error=error;
    e->pending=0;
    if (outcome!=BPL_REPORTED_SUCCESS) { quarantine(e,error); return BPL_RETAINED; }
    return BPL_OK;
}
int bpl_quarantine(struct bpl_registry *r, const struct bpl_lease *l, int32_t error)
{
    const struct bpl_entry *found;
    int rc=entry(r,l,&found); if (rc) return rc;
    if (!error || alias(r,sizeof *r,l,sizeof *l)) return BPL_ARGUMENT;
    quarantine(&r->entries[l->slot],error); return BPL_RETAINED;
}
int bpl_retire_private(struct bpl_registry *r, const struct bpl_lease *l)
{
    const struct bpl_entry *found;
    int rc=entry(r,l,&found); if (rc) return rc;
    if (alias(r,sizeof *r,l,sizeof *l)) return BPL_ARGUMENT;
    if ((found->state!=BPL_CLAIMED && found->state!=BPL_PRIVATE_METADATA) ||
        found->effect_count || found->pending || found->ever_resources) return BPL_RETAINED;
    r->entries[l->slot].state=BPL_RETIRED; return BPL_OK;
}
int bpl_inspect(const struct bpl_registry *r, const struct bpl_lease *l, struct bpl_snapshot *out)
{
    const struct bpl_entry *e;
    int rc=entry(r,l,&e); if (rc) return rc;
    if (!out || alias(r,sizeof *r,out,sizeof *out) || alias(l,sizeof *l,out,sizeof *out)) return BPL_ARGUMENT;
    *out=(struct bpl_snapshot){e->state,e->span_count,e->effect_count,e->pending,e->ever_resources,e->first_error};
    return BPL_OK;
}
