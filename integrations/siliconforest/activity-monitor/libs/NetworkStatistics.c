// SPDX-License-Identifier: MIT
// NetworkStatistics.framework (private) for Apple's Activity Monitor on SiliconForest
// (docs/roadmap/issues/SF-AA-activity-monitor.md): a client of the kernel's network statistics
// control (XNU's ntstat, "com.apple.network.statistics", the same interface macOS's framework
// speaks). A manager is a control socket read on the manager's queue; AddAllTCP/AddAllUDP ask the
// kernel for every TCP and UDP socket of the system (kernel providers), each source the kernel
// reports is handed to the manager's block, its description (process, interface) and counts
// (bytes and packets each way) are kept as the kernel sends them, and QueryAllSourcesUpdate asks
// for fresh ones. A source is a CFDictionary (Activity Monitor CFRetains it); what belongs to it
// is in a table beside it. Message layouts: <net/ntstat.h> of the kernel's headers (not copied).
#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>
#include <Block.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/kern_control.h>
#include <sys/socket.h>
#include <sys/sys_domain.h>
#include <unistd.h>
#include <net/if.h>
#include <netinet/in.h>
#include <uuid/uuid.h>
/* the kernel's message layouts are in its private part */
#define PRIVATE 1
#include <net/ntstat.h>
#undef PRIVATE

#define EXPORT __attribute__((visibility("default")))

EXPORT const CFStringRef kNStatSrcKeyInterface = CFSTR("interface");
EXPORT const CFStringRef kNStatSrcKeyPID = CFSTR("processID");
EXPORT const CFStringRef kNStatSrcKeyRxBytes = CFSTR("rxBytes");
EXPORT const CFStringRef kNStatSrcKeyRxPackets = CFSTR("rxPackets");
EXPORT const CFStringRef kNStatSrcKeyTxBytes = CFSTR("txBytes");
EXPORT const CFStringRef kNStatSrcKeyTxPackets = CFSTR("txPackets");

typedef void (^sf_source_block)(CFTypeRef source, void *unused);
typedef void (^sf_desc_block)(CFDictionaryRef description);
typedef void (^sf_removed_block)(void);

struct sf_source {
	uint64_t srcref;
	uint32_t provider;
	CFMutableDictionaryRef object;      /* what the client holds */
	CFDictionaryRef properties, counts;
	sf_desc_block describe;
	sf_removed_block removed;
	struct sf_source *next;
};

struct sf_manager {
	int fd;
	dispatch_queue_t queue;
	dispatch_source_t reader;
	sf_source_block added;
	struct sf_source *sources;
	uint64_t context;
	pthread_mutex_t lock;
	void (^pending)(void);      /* the completion of the query in flight */
	uint64_t pending_context;   /* its request's context: the kernel's success message names it */
};

/* source object -> source */
static pthread_mutex_t table_lock = PTHREAD_MUTEX_INITIALIZER;
struct sf_entry { CFTypeRef object; struct sf_source *src; struct sf_entry *next; };
static struct sf_entry *table;

static struct sf_source *source_of(CFTypeRef object)
{
	struct sf_source *s = NULL;
	pthread_mutex_lock(&table_lock);
	for (struct sf_entry *e = table; e; e = e->next)
		if (e->object == object) { s = e->src; break; }
	pthread_mutex_unlock(&table_lock);
	return s;
}

static void table_put(CFTypeRef object, struct sf_source *src)
{
	struct sf_entry *e = calloc(1, sizeof(*e));
	if (!e) return;
	e->object = object; e->src = src;
	pthread_mutex_lock(&table_lock);
	e->next = table; table = e;
	pthread_mutex_unlock(&table_lock);
}

static void table_remove(CFTypeRef object)
{
	pthread_mutex_lock(&table_lock);
	for (struct sf_entry **p = &table; *p; p = &(*p)->next)
		if ((*p)->object == object) { struct sf_entry *e = *p; *p = e->next; free(e); break; }
	pthread_mutex_unlock(&table_lock);
}

static void send_msg(struct sf_manager *m, void *msg, size_t len)
{
	if (m->fd >= 0 && send(m->fd, msg, len, 0) < 0 && errno != ENOBUFS)
		fprintf(stderr, "NetworkStatistics: send to the statistics control failed: %s\n", strerror(errno));
}

static struct sf_source *find(struct sf_manager *m, uint64_t srcref)
{
	for (struct sf_source *s = m->sources; s; s = s->next)
		if (s->srcref == srcref)
			return s;
	return NULL;
}

static CFNumberRef num64(uint64_t v) { return CFNumberCreate(NULL, kCFNumberSInt64Type, &v); }

static void set_counts(struct sf_source *s, const nstat_counts *c)
{
	const void *keys[4] = { kNStatSrcKeyRxBytes, kNStatSrcKeyRxPackets, kNStatSrcKeyTxBytes, kNStatSrcKeyTxPackets };
	CFNumberRef v[4] = { num64(c->nstat_rxbytes), num64(c->nstat_rxpackets), num64(c->nstat_txbytes), num64(c->nstat_txpackets) };
	CFDictionaryRef d = CFDictionaryCreate(NULL, keys, (const void **)v, 4, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	for (int i = 0; i < 4; i++) CFRelease(v[i]);
	if (s->counts) CFRelease(s->counts);
	s->counts = d;
}

/* the process and interface of a TCP or UDP descriptor */
static bool set_description(struct sf_source *s, uint32_t provider, const uint8_t *data, size_t len)
{
	uint32_t pid, ifindex;
	if ((provider == NSTAT_PROVIDER_TCP_KERNEL || provider == NSTAT_PROVIDER_TCP_USERLAND) && len >= sizeof(nstat_tcp_descriptor)) {
		const nstat_tcp_descriptor *d = (const void *)data;
		pid = d->pid; ifindex = d->ifindex;
	} else if ((provider == NSTAT_PROVIDER_UDP_KERNEL || provider == NSTAT_PROVIDER_UDP_USERLAND) && len >= sizeof(nstat_udp_descriptor)) {
		const nstat_udp_descriptor *d = (const void *)data;
		pid = d->pid; ifindex = d->ifindex;
	} else {
		return false;
	}
	CFNumberRef p = num64(pid), i = num64(ifindex);
	const void *keys[2] = { kNStatSrcKeyPID, kNStatSrcKeyInterface }, *vals[2] = { p, i };
	CFDictionaryRef d = CFDictionaryCreate(NULL, keys, vals, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CFRelease(p); CFRelease(i);
	/* the description as macOS hands it: the source's properties with its counts */
	if (s->counts) {
		CFMutableDictionaryRef all = CFDictionaryCreateMutableCopy(NULL, 0, d);
		CFIndex n = CFDictionaryGetCount(s->counts);
		const void *ck[8], *cv[8];
		if (n > 8) n = 8;
		CFDictionaryGetKeysAndValues(s->counts, ck, cv);
		for (CFIndex k = 0; k < n; k++) CFDictionarySetValue(all, ck[k], cv[k]);
		CFRelease(d);
		d = all;
	}
	if (s->properties) CFRelease(s->properties);
	s->properties = d;
	return true;   /* the description block hears each description, as on macOS */
}

static void get_description(struct sf_manager *m, uint64_t srcref)
{
	nstat_msg_get_src_description q;
	memset(&q, 0, sizeof(q));
	q.hdr.context = ++m->context;
	q.hdr.type = NSTAT_MSG_TYPE_GET_SRC_DESC;
	q.hdr.length = sizeof(q);
	q.srcref = srcref;
	send_msg(m, &q, sizeof(q));
}

static void source_added(struct sf_manager *m, uint64_t srcref, uint32_t provider)
{
	if (find(m, srcref))
		return;
	struct sf_source *s = calloc(1, sizeof(*s));
	if (!s) return;
	s->srcref = srcref;
	s->provider = provider;
	s->object = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	s->next = m->sources; m->sources = s;
	table_put(s->object, s);
	if (m->added)
		m->added(s->object, NULL);
	get_description(m, srcref);
}

static void source_removed(struct sf_manager *m, uint64_t srcref)
{
	for (struct sf_source **p = &m->sources; *p; p = &(*p)->next)
		if ((*p)->srcref == srcref) {
			struct sf_source *s = *p;
			*p = s->next;
			if (s->removed)
				s->removed();
			table_remove(s->object);
			if (s->describe) Block_release(s->describe);
			if (s->removed) Block_release(s->removed);
			if (s->properties) CFRelease(s->properties);
			if (s->counts) CFRelease(s->counts);
			CFRelease(s->object);
			free(s);
			return;
		}
}

/* SFNS_TRACE=1: every message from the kernel on standard error (type, length, source, provider) */
static int trace(void)
{
	static int on = -1;
	if (on < 0) on = getenv("SFNS_TRACE") != NULL;
	return on;
}

static void handle(struct sf_manager *m, const uint8_t *buf, size_t len)
{
	while (len >= sizeof(nstat_msg_hdr)) {
		const nstat_msg_hdr *h = (const void *)buf;
		size_t n = h->length;
		if (trace()) {
			uint64_t ref = n >= sizeof(nstat_msg_src_description_header) ? ((const nstat_msg_src_description_header *)(const void *)buf)->srcref : 0;
			uint32_t prov = (h->type == NSTAT_MSG_TYPE_SRC_DESC && n >= sizeof(nstat_msg_src_description_header)) ? ((const nstat_msg_src_description_header *)(const void *)buf)->provider :
			                (h->type == NSTAT_MSG_TYPE_SRC_UPDATE && n >= sizeof(nstat_msg_src_update_hdr)) ? ((const nstat_msg_src_update_hdr *)(const void *)buf)->provider : 0;
			fprintf(stderr, "SFNS type=%u len=%zu ctx=%llu src=%llu provider=%u (tcp desc %zu, udp desc %zu, desc hdr %zu, update hdr %zu)\n", h->type, n,
			        (unsigned long long)h->context, (unsigned long long)ref, prov, sizeof(nstat_tcp_descriptor), sizeof(nstat_udp_descriptor),
			        sizeof(nstat_msg_src_description_header), sizeof(nstat_msg_src_update_hdr));
		}
		if (n < sizeof(*h) || n > len)
			return;
		switch (h->type) {
		case NSTAT_MSG_TYPE_SRC_ADDED:
			if (n >= sizeof(nstat_msg_src_added)) {
				const nstat_msg_src_added *a = (const void *)buf;
				source_added(m, a->srcref, a->provider);
			}
			break;
		case NSTAT_MSG_TYPE_SRC_REMOVED:
			if (n >= sizeof(nstat_msg_src_removed))
				source_removed(m, ((const nstat_msg_src_removed *)(const void *)buf)->srcref);
			break;
		case NSTAT_MSG_TYPE_SRC_DESC:
			if (n >= sizeof(nstat_msg_src_description_header)) {
				const nstat_msg_src_description_header *d = (const void *)buf;
				struct sf_source *s = find(m, d->srcref);
				if (s && set_description(s, d->provider, buf + sizeof(*d), n - sizeof(*d)) && s->describe)
					s->describe(s->properties);
			}
			break;
		case NSTAT_MSG_TYPE_SRC_COUNTS:
			if (n >= sizeof(nstat_msg_src_counts)) {
				const nstat_msg_src_counts *c = (const void *)buf;
				struct sf_source *s = find(m, c->srcref);
				if (s) set_counts(s, &c->counts);
			}
			break;
		case NSTAT_MSG_TYPE_SRC_UPDATE:
			if (n >= sizeof(nstat_msg_src_update_hdr)) {
				const nstat_msg_src_update_hdr *u = (const void *)buf;
				struct sf_source *s = find(m, u->srcref);
				if (s) {
					set_counts(s, &u->counts);
					if (set_description(s, u->provider, buf + sizeof(*u), n - sizeof(*u)) && s->describe)
						s->describe(s->properties);
				}
			}
			break;
		case NSTAT_MSG_TYPE_SUCCESS:
		case NSTAT_MSG_TYPE_ERROR:
			/* the end of the query in flight: its completion runs after every update it brought */
			if (m->pending && h->context == m->pending_context) {
				void (^done)(void) = m->pending;
				m->pending = NULL;
				done();
				Block_release(done);
			}
			break;
		}
		buf += n;
		len -= n;
	}
}

EXPORT CFTypeRef NStatManagerCreate(CFAllocatorRef allocator, dispatch_queue_t queue, sf_source_block block)
{
	(void)allocator;
	struct sf_manager *m = calloc(1, sizeof(*m));
	struct ctl_info info;
	struct sockaddr_ctl sc;

	if (!m)
		return NULL;
	pthread_mutex_init(&m->lock, NULL);
	m->queue = queue ? queue : dispatch_get_main_queue();
	dispatch_retain(m->queue);
	m->added = block ? Block_copy(block) : NULL;
	m->fd = socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
	memset(&info, 0, sizeof(info));
	strlcpy(info.ctl_name, NET_STAT_CONTROL_NAME, sizeof(info.ctl_name));
	memset(&sc, 0, sizeof(sc));
	if (m->fd < 0 || ioctl(m->fd, CTLIOCGINFO, &info) != 0) {
		fprintf(stderr, "NetworkStatistics: no network statistics control in the kernel (%s): no sources\n", strerror(errno));
		if (m->fd >= 0) close(m->fd);
		m->fd = -1;
		return (CFTypeRef)m;
	}
	sc.sc_len = sizeof(sc);
	sc.sc_family = AF_SYSTEM;
	sc.ss_sysaddr = AF_SYS_CONTROL;
	sc.sc_id = info.ctl_id;
	if (connect(m->fd, (struct sockaddr *)&sc, sizeof(sc)) != 0) {
		fprintf(stderr, "NetworkStatistics: cannot connect to the statistics control: %s\n", strerror(errno));
		close(m->fd);
		m->fd = -1;
		return (CFTypeRef)m;
	}
	int size = 1 << 20;
	setsockopt(m->fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
	m->reader = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, (uintptr_t)m->fd, 0, m->queue);
	dispatch_source_set_event_handler(m->reader, ^{
		static uint8_t buf[65536];
		ssize_t r = recv(m->fd, buf, sizeof(buf), 0);
		if (r > 0)
			handle(m, buf, (size_t)r);
	});
	dispatch_resume(m->reader);
	return (CFTypeRef)m;
}

EXPORT void NStatManagerDestroy(CFTypeRef ref)
{
	struct sf_manager *m = (void *)ref;
	if (!m)
		return;
	if (m->reader) {
		dispatch_source_cancel(m->reader);
		dispatch_release(m->reader);
	}
	if (m->fd >= 0)
		close(m->fd);
	/* the sources are the client's to release; what belongs to them goes with the manager */
	for (struct sf_source *s = m->sources, *n; s; s = n) {
		n = s->next;
		table_remove(s->object);
		if (s->describe) Block_release(s->describe);
		if (s->removed) Block_release(s->removed);
		if (s->properties) CFRelease(s->properties);
		if (s->counts) CFRelease(s->counts);
		CFRelease(s->object);
		free(s);
	}
	if (m->added) Block_release(m->added);
	dispatch_release(m->queue);
	free(m);
}

static int add_all(struct sf_manager *m, uint32_t provider)
{
	nstat_msg_add_all_srcs q;
	if (!m || m->fd < 0)
		return ENOTSUP;
	memset(&q, 0, sizeof(q));
	q.hdr.context = ++m->context;
	q.hdr.type = NSTAT_MSG_TYPE_ADD_ALL_SRCS;
	q.hdr.length = sizeof(q);
	q.provider = provider;
	send_msg(m, &q, sizeof(q));
	return 0;
}

EXPORT int NStatManagerAddAllTCP(CFTypeRef m) { return add_all((void *)m, NSTAT_PROVIDER_TCP_KERNEL); }
EXPORT int NStatManagerAddAllUDP(CFTypeRef m) { return add_all((void *)m, NSTAT_PROVIDER_UDP_KERNEL); }

EXPORT int NStatManagerQueryAllSourcesUpdate(CFTypeRef ref, void (^done)(void))
{
	struct sf_manager *m = (void *)ref;
	__block uint64_t ctx = 0;
	if (m && m->fd >= 0) {
		nstat_msg_query_src_req q;
		memset(&q, 0, sizeof(q));
		q.hdr.context = ctx = ++m->context;
		q.hdr.type = NSTAT_MSG_TYPE_GET_UPDATE;
		q.hdr.length = sizeof(q);
		q.srcref = NSTAT_SRC_REF_ALL;
		send_msg(m, &q, sizeof(q));
	}
	/* the updates arrive on the queue after this; the completion runs when the kernel says the query is
	 * done (its success message), or after a second if it never does. (It was run at once, before any
	 * update was read: Activity Monitor read every process's network bytes as 0.) */
	if (done && m) {
		void (^copy)(void) = Block_copy(done);
		dispatch_async(m->queue, ^{
			if (m->pending) {   /* an earlier query that was never answered */
				void (^old)(void) = m->pending;
				m->pending = NULL;
				old();
				Block_release(old);
			}
			m->pending = copy;
			m->pending_context = ctx;
			uint64_t mine = ctx;
			dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), m->queue, ^{
				if (m->pending && m->pending_context == mine) {
					void (^late)(void) = m->pending;
					m->pending = NULL;
					late();
					Block_release(late);
				}
			});
		});
	}
	return 0;
}

EXPORT CFDictionaryRef NStatSourceCopyCounts(CFTypeRef object)
{
	struct sf_source *s = source_of(object);
	if (!s || !s->counts) return NULL;
	CFRetain(s->counts);
	return s->counts;
}

EXPORT CFDictionaryRef NStatSourceCopyProperties(CFTypeRef object)
{
	struct sf_source *s = source_of(object);
	if (!s || !s->properties) return NULL;
	CFRetain(s->properties);
	return s->properties;
}

EXPORT void NStatSourceSetDescriptionBlock(CFTypeRef object, sf_desc_block block)
{
	struct sf_source *s = source_of(object);
	if (!s) return;
	if (s->describe) Block_release(s->describe);
	s->describe = block ? Block_copy(block) : NULL;
	if (s->describe && s->properties)
		s->describe(s->properties);
}

EXPORT void NStatSourceSetRemovedBlock(CFTypeRef object, sf_removed_block block)
{
	struct sf_source *s = source_of(object);
	if (!s) return;
	if (s->removed) Block_release(s->removed);
	s->removed = block ? Block_copy(block) : NULL;
}
