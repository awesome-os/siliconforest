/*
 * Copyright (c) 2026 The Finch Project contributors.
 * SPDX-License-Identifier: MIT OR Apache-2.0
 *
 * libsysmon for SiliconForest (docs/roadmap/issues/SF-AA-activity-monitor.md, GitHub issue 53).
 * Taken from Finch (userland/libsysmon/sysmon.m, commit 2d9b1bb): the request/table/row model
 * (a request with a bit array of attributes, rows whose values sit in attribute
 * order, the interval rounding, the appliers), the process table built in the calling process
 * from libproc and sysctl (no sysmond daemon), and the first attributes (flags, pid, ppid, uid,
 * ruid, rgid, comm, pgid, tdev, start, arguments).
 * Changed and added for SiliconForest (Apple's Activity Monitor as the client):
 *   - the objects are Objective-C objects of their own classes (this libSystem has no
 *     _os_object_alloc), retained and released as objects (Activity Monitor does);
 *   - the system-wide request (type 2): one row of the kernel's totals (host_statistics64,
 *     sysctl, the interfaces' counters, the block storage drivers' statistics);
 *   - the process attributes Activity Monitor asks for, each from the kernel's own interface
 *     (proc_pidinfo, proc_pid_rusage, task_info through task_for_pid): CPU time and the CPU
 *     percentage over the last interval, memory (footprint, virtual, compressed, purgeable,
 *     internal), threads, ports, faults, page-ins, messages, system calls, context switches,
 *     idle wake-ups, disk bytes;
 *   - the responsible process is the process itself (this kernel keeps no responsibility);
 *   - attributes this kernel cannot answer are left out of the row (NULL), never invented: the
 *     energy scores, private and shared memory (not computed: a walk of every region of every
 *     process each interval).
 * The attribute numbers are those Apple's Activity Monitor 10.14 (macOS 26.5) requests and the
 * names it reads them under; the table of them is in the hand-over document.
 */

#import <Foundation/Foundation.h>
#include <Block.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <dispatch/dispatch.h>
#include <errno.h>
#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/proc_info.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <xpc/xpc.h>
#include <stdio.h>

#define API __attribute__((visibility("default")))

API const char *SYSMON_XPC_SERVICE_NAME = "com.apple.sysmond";
API const char *SYSMON_XPC_KEY_TYPE = "Type";
API const char *SYSMON_XPC_KEY_ATTRIBUTES = "Attributes";
API const char *SYSMON_XPC_KEY_FLAGS = "Flags";
API const char *SYSMON_XPC_REPLY_KEY_TIMESTAMP = "Timestamp";
API const char *SYSMON_XPC_REPLY_KEY_HEADER = "Header";
API const char *SYSMON_XPC_REPLY_KEY_TABLE = "Table";

/* Attributes per request type (bit-array bytes), as Apple's library sizes them. */
static const size_t attribute_bytes[] = { 10, 5, 2 };

/* process attributes (type 1) */
enum {
	ATTR_FLAGS = 1, ATTR_STATUS = 2, ATTR_PID = 4, ATTR_PPID = 5, ATTR_UID = 6, ATTR_RUID = 8, ATTR_RGID = 9,
	ATTR_COMM = 12, ATTR_NAME = 13, ATTR_PGID = 15, ATTR_TDEV = 17, ATTR_START = 20,
	ATTR_VSIZE = 21, ATTR_RESIDENT = 22, ATTR_USER_NS = 23, ATTR_SYSTEM_NS = 24,
	ATTR_FAULTS = 28, ATTR_PAGEINS = 29, ATTR_MSGS_SENT = 31, ATTR_MSGS_RCVD = 32,
	ATTR_MACH_CALLS = 33, ATTR_UNIX_CALLS = 34, ATTR_CSW = 35, ATTR_THREADS = 36,
	ATTR_ARGUMENTS = 44, ATTR_IDLE_WAKEUPS = 46, ATTR_PORTS = 50, ATTR_CPU_PERCENT = 51,
	ATTR_POWER_SCORE = 52, ATTR_PRIVATE = 53, ATTR_SHARED = 54, ATTR_RESPONSIBLE_PID = 55,
	ATTR_APP_SLEEP = 56, ATTR_PURGEABLE = 59, ATTR_AVG_POWER_SCORE = 62, ATTR_FOOTPRINT = 66,
	ATTR_BYTES_READ = 72, ATTR_BYTES_WRITTEN = 73, ATTR_COMPRESSED = 74,
};

/* system attributes (type 2); pages are counted in pages, Activity Monitor multiplies */
enum {
	SYS_WIRED_PAGES = 4, SYS_PURGEABLE_PAGES = 14, SYS_COMPRESSOR_PAGES = 20, SYS_THREADS = 24,
	SYS_DISK_READ_OPS = 25, SYS_DISK_WRITE_OPS = 26, SYS_DISK_READ_BYTES = 27, SYS_DISK_WRITE_BYTES = 28,
	SYS_NET_PACKETS_IN = 29, SYS_NET_BYTES_IN = 30, SYS_NET_PACKETS_OUT = 31, SYS_NET_BYTES_OUT = 32,
	SYS_EXTERNAL_PAGES = 33, SYS_INTERNAL_PAGES = 34, SYS_TOTAL_PAGES = 36, SYS_USED_PAGES = 38,
};

struct bits {
	size_t size;
	uint8_t data[];
};

static bool
has(const struct bits *b, uint32_t attribute)
{
	return b != NULL && attribute / 8 < b->size && (b->data[attribute / 8] >> (attribute % 8)) & 1;
}

/* what the previous table of a request said about a process: for the CPU percentage */
struct prev {
	pid_t pid;
	int64_t start_ns;
	uint64_t cpu_ns;
	uint64_t wakeups;   /* idle wake-ups so far */
	uint64_t seen;   /* generation */
};

API
@interface OS_sysmon_object : NSObject
@end
@implementation OS_sysmon_object
@end

API
@interface OS_sysmon_request : OS_sysmon_object {
  @public
	dispatch_queue_t queue;
	void (^handler)(id);
	void (^error_handler)(id, const char *);
	dispatch_source_t timer;
	uint64_t type;
	struct bits *attributes;
	size_t attribute_bytes;
	uint64_t flags;
	uint64_t interval;
	struct prev *prev;
	size_t nprev, capprev;
	uint64_t prev_time_ns, generation;
}
@end

API
@interface OS_sysmon_table : OS_sysmon_object {
  @public
	uint64_t timestamp;
	uint64_t count;
	id *rows;
}
@end

API
@interface OS_sysmon_row : OS_sysmon_object {
  @public
	struct bits *attributes;
	xpc_object_t *values;   /* in attribute order; NULL where the value is not known */
	size_t nvalues, capvalues;
}
@end

@implementation OS_sysmon_request
- (void)dealloc
{
	if (timer) {
		dispatch_source_cancel(timer);
		dispatch_release(timer);
	}
	if (queue)
		dispatch_release(queue);
	if (handler)
		Block_release(handler);
	if (error_handler)
		Block_release(error_handler);
	free(attributes);
	free(prev);
	[super dealloc];
}
@end

@implementation OS_sysmon_table
- (void)dealloc
{
	for (uint64_t i = 0; i < count; i++)
		[rows[i] release];
	free(rows);
	[super dealloc];
}
@end

@implementation OS_sysmon_row
- (void)dealloc
{
	free(attributes);
	for (size_t i = 0; i < nvalues; i++)
		if (values[i])
			xpc_release(values[i]);
	free(values);
	[super dealloc];
}
@end

API void *sysmon_request_alloc(void) { return [OS_sysmon_request alloc]; }
API void *sysmon_table_alloc(void) { return [OS_sysmon_table alloc]; }
API void *sysmon_row_alloc(void) { return [OS_sysmon_row alloc]; }
API void *sysmon_retain(void *object) { return [(id)object retain]; }
API void sysmon_release(void *object) { [(id)object release]; }

/* ---- requests ---- */

static OS_sysmon_request *
request_create(uint64_t type)
{
	OS_sysmon_request *r = [[OS_sysmon_request alloc] init];

	r->type = type;
	r->queue = dispatch_queue_create("com.apple.sysmon.request", DISPATCH_QUEUE_SERIAL);
	return r;
}

API void *
sysmon_request_create(uint64_t type, void (^handler)(id))
{
	OS_sysmon_request *r = request_create(type);

	r->handler = Block_copy(handler);
	return r;
}

API void *
sysmon_request_create_with_error(uint64_t type, void (^handler)(id, const char *))
{
	OS_sysmon_request *r = request_create(type);

	r->error_handler = Block_copy(handler);
	return r;
}

API void
sysmon_request_add_attribute(OS_sysmon_request *r, uint32_t attribute)
{
	if (r->attributes == NULL) {
		if (r->type < 1 || r->type > 3)
			return;
		r->attribute_bytes = attribute_bytes[r->type - 1];
		r->attributes = calloc(1, sizeof(struct bits) + r->attribute_bytes);
		if (r->attributes == NULL)
			return;
		r->attributes->size = r->attribute_bytes;
	}
	if (attribute / 8 >= r->attribute_bytes)
		return;   /* Apple's logs this and ignores it */
	r->attributes->data[attribute / 8] |= (uint8_t)(1u << (attribute % 8));
	/* (TERM8) the task statistics come as one group: Activity Monitor's inspector asks for faults, page-ins, system
	 * calls, context switches and threads (28 29 33 34 35 36) and then reads the Mach messages sent and received
	 * (31 32) from the same rows without asking for them (SYSMON_DEBUG: "read of attribute 31/32, not in the
	 * request"); they come from the same proc_taskinfo call, so a row that has the group has them too */
	if (r->type == 1 && (attribute == ATTR_FAULTS || attribute == ATTR_PAGEINS || attribute == ATTR_MACH_CALLS ||
	                     attribute == ATTR_UNIX_CALLS || attribute == ATTR_CSW) && ATTR_MSGS_RCVD / 8 < r->attribute_bytes) {
		r->attributes->data[ATTR_MSGS_SENT / 8] |= (uint8_t)(1u << (ATTR_MSGS_SENT % 8));
		r->attributes->data[ATTR_MSGS_RCVD / 8] |= (uint8_t)(1u << (ATTR_MSGS_RCVD % 8));
	}
}

API void
sysmon_request_add_attributes(OS_sysmon_request *r, ...)
{
	va_list ap;
	uint32_t attribute;

	va_start(ap, r);
	while ((attribute = va_arg(ap, uint32_t)) != 0)
		sysmon_request_add_attribute(r, attribute);
	va_end(ap);
}

API void
sysmon_request_set_flags(OS_sysmon_request *r, uint64_t flags)
{
	r->flags = flags;
}

API void
sysmon_request_set_interval(OS_sysmon_request *r, uint64_t interval_ms)
{
	uint64_t rounded;

	if (interval_ms < 500) {
		rounded = 500;
	} else {
		/* To the nearest multiple of 500 (ties round up). */
		rounded = interval_ms / 500 * 500;
		if (interval_ms - rounded >= 500 - (interval_ms - rounded))
			rounded += 500;
	}
	r->interval = rounded;
	if (r->timer)   /* a running request takes the new interval, with a table now */
		dispatch_source_set_timer(r->timer, dispatch_time(DISPATCH_TIME_NOW, 0),
		    rounded * NSEC_PER_MSEC, 50 * NSEC_PER_MSEC);
}

/* ---- process tables ---- */

static mach_timebase_info_data_t timebase;

static uint64_t
ticks_to_ns(uint64_t t)
{
	if (timebase.denom == 0)
		mach_timebase_info(&timebase);
	return timebase.numer == timebase.denom ? t : t * timebase.numer / timebase.denom;
}

static uint64_t
now_ns(void)
{
	return ticks_to_ns(mach_absolute_time());
}

static xpc_object_t
arguments(pid_t pid)
{
	int mib[3] = { CTL_KERN, KERN_PROCARGS2, pid };
	size_t size = 0;
	char *buf, *p, *end;
	int argc;
	xpc_object_t array;

	if (sysctl(mib, 3, NULL, &size, NULL, 0) != 0 || size < sizeof(int))
		return NULL;
	if ((buf = malloc(size)) == NULL)
		return NULL;
	if (sysctl(mib, 3, buf, &size, NULL, 0) != 0 || size < sizeof(int)) {
		free(buf);
		return NULL;
	}
	memcpy(&argc, buf, sizeof(argc));
	p = buf + sizeof(argc);
	end = buf + size;
	p += strnlen(p, (size_t)(end - p));           /* executable path */
	while (p < end && *p == '\0')
		p++;
	array = xpc_array_create(NULL, 0);
	for (int i = 0; i < argc && p < end; i++) {
		size_t n = strnlen(p, (size_t)(end - p));

		xpc_array_set_string(array, XPC_ARRAY_APPEND, p);
		p += n + 1;
	}
	free(buf);
	return array;
}

/*
 * What a row can say about a process. As root (Activity Monitor runs as root here) the full
 * task information is there for every process; otherwise the short form (same flags, ids and
 * 15-character comm, as sysmond reports them) and the kernel's kinfo_proc (start time,
 * terminal) are, and the task's figures are not.
 */
struct procinfo {
	pid_t pid;
	bool have, have_task, have_rusage, have_vm;
	uint64_t flags, status, ppid, pgid, uid, ruid, rgid, tdev;
	char comm[MAXCOMLEN + 1];
	char name[2 * MAXCOMLEN + 1];
	int64_t start_ns;
	struct proc_taskinfo task;
	struct rusage_info_v4 ru;
	task_vm_info_data_t vm;
	uint64_t ports;
	bool have_ports;
	uint64_t cpu_ns;
	double cpu_percent;
	bool have_cpu_percent;
	double power, average_power;   /* the energy impact estimate (below) */
	bool have_power, have_average_power;
};

/* Energy impact. macOS's comes from the kernel's energy accounting of each coalition (CPU, GPU,
 * wake-ups, disk and network, weighed by a model of the machine), which this kernel does not keep.
 * What is given here is an ESTIMATE from what it does count, stated as such in
 * docs/roadmap/issues/SF-AA-activity-monitor.md: the process's CPU percentage plus its idle wake-ups
 * a second (the "Idle Wake Ups" column: the package woken from idle for it; interrupt wake-ups left
 * out, every timer tick would count), each wake-up taken as 0.5 ms of a busy core (0.05 points per
 * wake-up a second). "12 hr Power" (average power score) is the same estimate over the process's
 * life so far (there is no 12 hours of history: a process younger than that is averaged over its
 * life, an older one too). */
#define SYSMON_WAKEUP_POINTS 0.05
static uint64_t
wakeups_of(const struct rusage_info_v4 *ru)
{
	return ru->ri_pkg_idle_wkups;
}

static bool
procinfo(pid_t pid, const struct bits *want, struct procinfo *pi)
{
	struct proc_taskallinfo ta;
	struct proc_bsdshortinfo si;
	struct kinfo_proc kp;
	int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, pid };
	size_t size = sizeof(kp);

	memset(pi, 0, sizeof(*pi));
	pi->pid = pid;
	if (proc_pidinfo(pid, PROC_PIDTASKALLINFO, 0, &ta, sizeof(ta)) == sizeof(ta)) {
		struct proc_bsdinfo *bi = &ta.pbsd;

		pi->flags = bi->pbi_flags;
		pi->status = bi->pbi_status;
		pi->ppid = bi->pbi_ppid;
		pi->pgid = bi->pbi_pgid;
		pi->uid = bi->pbi_uid;
		pi->ruid = bi->pbi_ruid;
		pi->rgid = bi->pbi_rgid;
		pi->tdev = (uint32_t)bi->e_tdev;
		memcpy(pi->comm, bi->pbi_comm, MAXCOMLEN);
		memcpy(pi->name, bi->pbi_name, 2 * MAXCOMLEN);
		pi->start_ns = (int64_t)bi->pbi_start_tvsec * 1000000000LL + (int64_t)bi->pbi_start_tvusec * 1000;
		pi->task = ta.ptinfo;
		pi->have_task = true;
		pi->have = true;
	} else if (proc_pidinfo(pid, PROC_PIDT_SHORTBSDINFO, 0, &si, sizeof(si)) == sizeof(si) &&
	    sysctl(mib, 4, &kp, &size, NULL, 0) == 0 && size == sizeof(kp)) {
		pi->flags = si.pbsi_flags;
		pi->status = si.pbsi_status;
		pi->ppid = si.pbsi_ppid;
		pi->pgid = si.pbsi_pgid;
		pi->uid = si.pbsi_uid;
		pi->ruid = si.pbsi_ruid;
		pi->rgid = si.pbsi_rgid;
		pi->tdev = (uint32_t)kp.kp_eproc.e_tdev;
		memcpy(pi->comm, si.pbsi_comm, MAXCOMLEN);
		pi->start_ns = (int64_t)kp.kp_proc.p_starttime.tv_sec * 1000000000LL +
		    (int64_t)kp.kp_proc.p_starttime.tv_usec * 1000;
		pi->have = true;
	} else {
		return false;
	}
	if (proc_pid_rusage(pid, RUSAGE_INFO_V4, (rusage_info_t *)&pi->ru) == 0)
		pi->have_rusage = true;
	/* CPU time: the rusage times are in Mach time units, as on macOS */
	if (pi->have_rusage)
		pi->cpu_ns = ticks_to_ns(pi->ru.ri_user_time) + ticks_to_ns(pi->ru.ri_system_time);
	if (has(want, ATTR_PORTS) || has(want, ATTR_COMPRESSED) || has(want, ATTR_PURGEABLE)) {
		mach_port_t task = MACH_PORT_NULL;

		if (pid != 0 && task_for_pid(mach_task_self(), pid, &task) == KERN_SUCCESS) {
			mach_msg_type_number_t c = TASK_VM_INFO_COUNT;
			mach_port_name_array_t names = NULL;
			mach_port_type_array_t types = NULL;
			mach_msg_type_number_t nn = 0, nt = 0;

			if (task_info(task, TASK_VM_INFO, (task_info_t)&pi->vm, &c) == KERN_SUCCESS)
				pi->have_vm = true;
			/* the names in the task's port space, as macOS counts a process's ports */
			if (has(want, ATTR_PORTS) && mach_port_names(task, &names, &nn, &types, &nt) == KERN_SUCCESS) {
				pi->ports = nn;
				pi->have_ports = true;
				vm_deallocate(mach_task_self(), (vm_address_t)names, nn * sizeof(*names));
				vm_deallocate(mach_task_self(), (vm_address_t)types, nt * sizeof(*types));
			}
			mach_port_deallocate(mach_task_self(), task);
		}
	}
	return true;
}

static xpc_object_t
u64(uint64_t v)
{
	return xpc_uint64_create(v);
}

static xpc_object_t
value(uint32_t attribute, const struct procinfo *pi)
{
	/* this kernel keeps no "responsible process": every process answers for itself, which is also
	 * what macOS reports for a process nobody launched on its behalf (the Energy tab groups by it) */
	if (attribute == ATTR_PID || attribute == ATTR_RESPONSIBLE_PID)
		return u64((uint64_t)pi->pid);
	if (attribute == ATTR_ARGUMENTS)
		return arguments(pi->pid);
	if (!pi->have)
		return NULL;
	switch (attribute) {
	case ATTR_FLAGS: return u64(pi->flags);
	case ATTR_STATUS: return u64(pi->status);
	case ATTR_PPID: return u64(pi->ppid);
	case ATTR_UID: return u64(pi->uid);
	case ATTR_RUID: return u64(pi->ruid);
	case ATTR_RGID: return u64(pi->rgid);
	case ATTR_PGID: return u64(pi->pgid);
	case ATTR_TDEV: return u64(pi->tdev);
	case ATTR_COMM: return xpc_string_create(pi->comm);
	case ATTR_NAME: return xpc_string_create(pi->name[0] ? pi->name : pi->comm);
	case ATTR_START: return xpc_date_create(pi->start_ns);
	case ATTR_APP_SLEEP: return xpc_bool_create(false);   /* there is no App Nap here */
	}
	if (pi->have_task)
		switch (attribute) {
		case ATTR_VSIZE: return u64(pi->task.pti_virtual_size);
		case ATTR_FAULTS: return u64((uint64_t)(uint32_t)pi->task.pti_faults);
		case ATTR_PAGEINS: return u64((uint64_t)(uint32_t)pi->task.pti_pageins);
		case ATTR_MSGS_SENT: return u64((uint64_t)(uint32_t)pi->task.pti_messages_sent);
		case ATTR_MSGS_RCVD: return u64((uint64_t)(uint32_t)pi->task.pti_messages_received);
		case ATTR_MACH_CALLS: return u64((uint64_t)(uint32_t)pi->task.pti_syscalls_mach);
		case ATTR_UNIX_CALLS: return u64((uint64_t)(uint32_t)pi->task.pti_syscalls_unix);
		case ATTR_CSW: return u64((uint64_t)(uint32_t)pi->task.pti_csw);
		case ATTR_THREADS: return u64((uint64_t)(uint32_t)pi->task.pti_threadnum);
		}
	if (pi->have_rusage)
		switch (attribute) {
		case ATTR_FOOTPRINT: return u64(pi->ru.ri_phys_footprint);   /* the Memory column (anonymousMemory) */
		case ATTR_RESIDENT: return u64(pi->ru.ri_resident_size);     /* Real Mem */
		case ATTR_USER_NS: return u64(ticks_to_ns(pi->ru.ri_user_time));
		case ATTR_SYSTEM_NS: return u64(ticks_to_ns(pi->ru.ri_system_time));
		case ATTR_IDLE_WAKEUPS: return u64(pi->ru.ri_pkg_idle_wkups);
		case ATTR_BYTES_READ: return u64(pi->ru.ri_diskio_bytesread);
		case ATTR_BYTES_WRITTEN: return u64(pi->ru.ri_diskio_byteswritten);
		}
	if (pi->have_vm)
		switch (attribute) {
		case ATTR_COMPRESSED: return u64(pi->vm.compressed);
		case ATTR_PURGEABLE: return u64(pi->vm.purgeable_volatile_pmap);
		}
	if (attribute == ATTR_PORTS && pi->have_ports)
		return u64(pi->ports);
	if (attribute == ATTR_CPU_PERCENT && pi->have_cpu_percent)
		return xpc_double_create(pi->cpu_percent);
	if (attribute == ATTR_POWER_SCORE && pi->have_power)
		return xpc_double_create(pi->power);
	if (attribute == ATTR_AVG_POWER_SCORE && pi->have_average_power)
		return xpc_double_create(pi->average_power);
	return NULL;   /* not known on this system: left out, never invented */
}

static struct prev *
prev_find(OS_sysmon_request *r, pid_t pid, int64_t start_ns)
{
	for (size_t i = 0; i < r->nprev; i++)
		if (r->prev[i].pid == pid && r->prev[i].start_ns == start_ns)
			return &r->prev[i];
	return NULL;
}

static void
prev_put(OS_sysmon_request *r, pid_t pid, int64_t start_ns, uint64_t cpu_ns, uint64_t wakeups)
{
	struct prev *p = prev_find(r, pid, start_ns);

	if (p == NULL) {
		if (r->nprev == r->capprev) {
			size_t cap = r->capprev ? r->capprev * 2 : 256;
			struct prev *n = realloc(r->prev, cap * sizeof(*n));

			if (n == NULL)
				return;
			r->prev = n;
			r->capprev = cap;
		}
		p = &r->prev[r->nprev++];
		p->pid = pid;
		p->start_ns = start_ns;
	}
	p->cpu_ns = cpu_ns;
	p->wakeups = wakeups;
	p->seen = r->generation;
}

static void
prev_sweep(OS_sysmon_request *r)
{
	size_t k = 0;

	for (size_t i = 0; i < r->nprev; i++)
		if (r->prev[i].seen == r->generation)
			r->prev[k++] = r->prev[i];
	r->nprev = k;
}

static OS_sysmon_row *
row_new(const struct bits *attributes)
{
	OS_sysmon_row *row = [[OS_sysmon_row alloc] init];
	size_t bytes = sizeof(struct bits) + (attributes ? attributes->size : 0);

	row->attributes = calloc(1, bytes);
	if (attributes && row->attributes)
		memcpy(row->attributes, attributes, bytes);
	row->values = NULL;
	return row;
}

/* the row takes the value (NULL: not known; this libxpc keeps no null object in an array) */
static void
row_append(OS_sysmon_row *row, xpc_object_t v)
{
	if (row->nvalues == row->capvalues) {
		size_t cap = row->capvalues ? row->capvalues * 2 : 32;
		xpc_object_t *n = realloc(row->values, cap * sizeof(*n));

		if (n == NULL) {
			if (v)
				xpc_release(v);
			return;
		}
		row->values = n;
		row->capvalues = cap;
	}
	row->values[row->nvalues++] = v;
}

static int debugging(void);
API xpc_object_t sysmon_row_get_value(OS_sysmon_row *row, uint32_t attribute);

static OS_sysmon_table *
process_table(OS_sysmon_request *r)
{
	const struct bits *attributes = r->attributes;
	OS_sysmon_table *t = [[OS_sysmon_table alloc] init];
	int capacity = proc_listallpids(NULL, 0) + 64, n;
	pid_t *pids;
	uint64_t now = now_ns(), elapsed = r->prev_time_ns ? now - r->prev_time_ns : 0;

	r->generation++;
	t->timestamp = mach_absolute_time();
	if (debugging() && r->generation == 1 && attributes) {   /* (TERM7) which attributes this request asks for */
		fprintf(stderr, "sysmon: process request %p attributes:", (void *)r);
		for (uint32_t a = 0; a < attributes->size * 8; a++)
			if (has(attributes, a))
				fprintf(stderr, " %u", a);
		fprintf(stderr, "\n");
	}
	if (capacity <= 64 || (pids = calloc((size_t)capacity, sizeof(pid_t))) == NULL)
		return t;
	n = proc_listallpids(pids, capacity * (int)sizeof(pid_t));
	t->rows = calloc((size_t)(n > 0 ? n : 1), sizeof(*t->rows));
	for (int i = 0; i < n && t->rows; i++) {
		struct procinfo pi;
		bool have = procinfo(pids[i], attributes, &pi);
		OS_sysmon_row *row;

		if (!have && kill(pids[i], 0) != 0 && errno == ESRCH)
			continue;   /* exited meanwhile */
		if (pi.have_rusage) {
			struct prev *p = prev_find(r, pi.pid, pi.start_ns);

			uint64_t wk = wakeups_of(&pi.ru);
			struct timespec wall;

			if (p && elapsed > 0) {
				uint64_t used = pi.cpu_ns >= p->cpu_ns ? pi.cpu_ns - p->cpu_ns : 0;
				uint64_t woke = wk >= p->wakeups ? wk - p->wakeups : 0;

				pi.cpu_percent = 100.0 * (double)used / (double)elapsed;
				pi.have_cpu_percent = true;
				pi.power = pi.cpu_percent + SYSMON_WAKEUP_POINTS * (double)woke * 1e9 / (double)elapsed;
				pi.have_power = true;
			} else if (p == NULL) {
				pi.cpu_percent = 0;   /* first sample of this process: no interval yet */
				pi.have_cpu_percent = true;
				pi.power = 0;
				pi.have_power = true;
			}
			/* over the process's life so far (wall clock, as its start time is) */
			if (clock_gettime(CLOCK_REALTIME, &wall) == 0) {
				int64_t life = (int64_t)wall.tv_sec * 1000000000LL + wall.tv_nsec - pi.start_ns;

				if (life > 1000000000LL) {
					pi.average_power = 100.0 * (double)pi.cpu_ns / (double)life +
					                   SYSMON_WAKEUP_POINTS * (double)wk * 1e9 / (double)life;
					pi.have_average_power = true;
				}
			}
			prev_put(r, pi.pid, pi.start_ns, pi.cpu_ns, wk);
		}
		row = row_new(attributes);
		for (uint32_t a = 0; attributes && a < attributes->size * 8; a++) {
			if (!has(attributes, a))
				continue;
			xpc_object_t v = value(a, &pi);

			row_append(row, v);
		}
		t->rows[t->count++] = row;
	}
	prev_sweep(r);
	if (debugging()) {
		double top = 0;
		pid_t toppid = -1;
		for (uint64_t i = 0; i < t->count; i++) {
			xpc_object_t p = sysmon_row_get_value(t->rows[i], ATTR_PID), c = sysmon_row_get_value(t->rows[i], ATTR_CPU_PERCENT);
			if (p && c && xpc_double_get_value(c) > top) {
				top = xpc_double_get_value(c);
				toppid = (pid_t)xpc_uint64_get_value(p);
			}
		}
		fprintf(stderr, "sysmon: busiest pid %d %.2f%% over %.2f s\n", toppid, top, elapsed / 1e9);
	}
	r->prev_time_ns = now;
	free(pids);
	return t;
}

/* ---- the system-wide table: one row ---- */

static bool
interfaces(uint64_t *pin, uint64_t *bin, uint64_t *pout, uint64_t *bout)
{
	int mib[6] = { CTL_NET, PF_ROUTE, 0, 0, NET_RT_IFLIST2, 0 };
	size_t len = 0;
	char *buf;

	*pin = *bin = *pout = *bout = 0;
	if (sysctl(mib, 6, NULL, &len, NULL, 0) != 0 || len == 0 || (buf = malloc(len)) == NULL)
		return false;
	if (sysctl(mib, 6, buf, &len, NULL, 0) != 0) {
		free(buf);
		return false;
	}
	for (char *p = buf; p + sizeof(struct if_msghdr) <= buf + len;) {
		struct if_msghdr *ifm = (struct if_msghdr *)p;

		if (ifm->ifm_msglen == 0)
			break;
		if (ifm->ifm_type == RTM_IFINFO2) {
			struct if_msghdr2 *m2 = (struct if_msghdr2 *)p;

			if (!(m2->ifm_flags & IFF_LOOPBACK)) {   /* as macOS's totals: not lo0 */
				*pin += m2->ifm_data.ifi_ipackets;
				*bin += m2->ifm_data.ifi_ibytes;
				*pout += m2->ifm_data.ifi_opackets;
				*bout += m2->ifm_data.ifi_obytes;
			}
		}
		p += ifm->ifm_msglen;
	}
	free(buf);
	return true;
}

static uint64_t
stat_number(CFDictionaryRef d, CFStringRef key)
{
	CFNumberRef n = d ? CFDictionaryGetValue(d, key) : NULL;
	uint64_t v = 0;

	if (n && CFGetTypeID(n) == CFNumberGetTypeID())
		CFNumberGetValue(n, kCFNumberSInt64Type, &v);
	return v;
}

/* the block storage drivers' "Statistics", summed over the drivers (as iostat reads them) */
static bool
disks(uint64_t *rops, uint64_t *wops, uint64_t *rbytes, uint64_t *wbytes)
{
	io_iterator_t it;
	io_object_t o;
	bool any = false;

	*rops = *wops = *rbytes = *wbytes = 0;
	if (IOServiceGetMatchingServices(kIOMasterPortDefault, IOServiceMatching("IOBlockStorageDriver"), &it) != KERN_SUCCESS)
		return false;
	while ((o = IOIteratorNext(it)) != 0) {
		CFDictionaryRef s = IORegistryEntryCreateCFProperty(o, CFSTR("Statistics"), NULL, 0);

		if (s && CFGetTypeID(s) == CFDictionaryGetTypeID()) {
			*rops += stat_number(s, CFSTR("Operations (Read)"));
			*wops += stat_number(s, CFSTR("Operations (Write)"));
			*rbytes += stat_number(s, CFSTR("Bytes (Read)"));
			*wbytes += stat_number(s, CFSTR("Bytes (Write)"));
			any = true;
		}
		if (s)
			CFRelease(s);
		IOObjectRelease(o);
	}
	IOObjectRelease(it);
	return any;
}

static int debugging(void);

static OS_sysmon_table *
system_table(OS_sysmon_request *r)
{
	const struct bits *attributes = r->attributes;
	OS_sysmon_table *t = [[OS_sysmon_table alloc] init];
	vm_statistics64_data_t vs;
	mach_msg_type_number_t c = HOST_VM_INFO64_COUNT;
	bool have_vm = host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vs, &c) == KERN_SUCCESS;
	vm_size_t page = 0;
	uint64_t memsize = 0, threads = 0;
	size_t n = sizeof(memsize);
	uint64_t net[4], disk[4];
	bool have_net = false, have_disk = false;
	int nthreads = 0;

	host_page_size(mach_host_self(), &page);
	if (sysctlbyname("hw.memsize", &memsize, &n, NULL, 0) != 0)
		memsize = 0;
	/* the threads of every process (kern.num_threads is the limit, not the count) */
	if (has(attributes, SYS_THREADS)) {
		int cap = proc_listallpids(NULL, 0) + 64, np;
		pid_t *pids = cap > 64 ? calloc((size_t)cap, sizeof(pid_t)) : NULL;

		np = pids ? proc_listallpids(pids, cap * (int)sizeof(pid_t)) : 0;
		for (int i = 0; i < np; i++) {
			struct proc_taskinfo ti;

			if (proc_pidinfo(pids[i], PROC_PIDTASKINFO, 0, &ti, sizeof(ti)) == sizeof(ti))
				threads += (uint64_t)(uint32_t)ti.pti_threadnum;
		}
		free(pids);
	}
	(void)nthreads;
	if (has(attributes, SYS_NET_PACKETS_IN) || has(attributes, SYS_NET_BYTES_IN) || has(attributes, SYS_NET_PACKETS_OUT) || has(attributes, SYS_NET_BYTES_OUT))
		have_net = interfaces(&net[0], &net[1], &net[2], &net[3]);
	if (has(attributes, SYS_DISK_READ_OPS) || has(attributes, SYS_DISK_WRITE_OPS) || has(attributes, SYS_DISK_READ_BYTES) || has(attributes, SYS_DISK_WRITE_BYTES))
		have_disk = disks(&disk[0], &disk[1], &disk[2], &disk[3]);

	t->timestamp = mach_absolute_time();
	t->rows = calloc(1, sizeof(*t->rows));
	OS_sysmon_row *row = row_new(attributes);

	for (uint32_t a = 0; attributes && a < attributes->size * 8; a++) {
		xpc_object_t v = NULL;

		if (!has(attributes, a))
			continue;
		switch (a) {
		case SYS_WIRED_PAGES: if (have_vm) v = u64(vs.wire_count); break;
		case SYS_PURGEABLE_PAGES: if (have_vm) v = u64(vs.purgeable_count); break;
		case SYS_COMPRESSOR_PAGES: if (have_vm) v = u64(vs.compressor_page_count); break;
		case SYS_EXTERNAL_PAGES: if (have_vm) v = u64(vs.external_page_count); break;
		case SYS_INTERNAL_PAGES: if (have_vm) v = u64(vs.internal_page_count); break;
		case SYS_USED_PAGES:
			if (have_vm)
				v = u64((uint64_t)vs.active_count + vs.inactive_count + vs.speculative_count + vs.throttled_count +
				    vs.wire_count + vs.compressor_page_count);
			break;
		case SYS_TOTAL_PAGES: if (memsize && page) v = u64(memsize / page); break;
		case SYS_THREADS: if (threads) v = u64(threads); break;
		case SYS_NET_PACKETS_IN: if (have_net) v = u64(net[0]); break;
		case SYS_NET_BYTES_IN: if (have_net) v = u64(net[1]); break;
		case SYS_NET_PACKETS_OUT: if (have_net) v = u64(net[2]); break;
		case SYS_NET_BYTES_OUT: if (have_net) v = u64(net[3]); break;
		case SYS_DISK_READ_OPS: if (have_disk) v = u64(disk[0]); break;
		case SYS_DISK_WRITE_OPS: if (have_disk) v = u64(disk[1]); break;
		case SYS_DISK_READ_BYTES: if (have_disk) v = u64(disk[2]); break;
		case SYS_DISK_WRITE_BYTES: if (have_disk) v = u64(disk[3]); break;
		}
		row_append(row, v);
	}
	t->rows[t->count++] = row;
	if (debugging())
		fprintf(stderr, "sysmon: system threads %llu (asked %d) used pages %llu\n", (unsigned long long)threads,
		    has(attributes, SYS_THREADS), have_vm ? (unsigned long long)vs.active_count : 0ULL);
	return t;
}

static int
debugging(void)
{
	static int debug = -1;

	if (debug < 0)
		debug = getenv("SYSMON_DEBUG") != NULL;
	return debug;
}

static void
deliver(OS_sysmon_request *r)
{
	OS_sysmon_table *t;
	int debug = debugging();

	if (debug)
		fprintf(stderr, "sysmon: type %llu: building a table\n", (unsigned long long)r->type);

	if (r->type == 1) {
		t = process_table(r);
	} else if (r->type == 2) {
		t = system_table(r);
	} else {
		if (r->error_handler)
			r->error_handler(nil, "coalition tables are not supported without sysmond");
		else if (r->handler)
			r->handler(nil);
		return;
	}
	if (debug)
		fprintf(stderr, "sysmon: type %llu table of %llu rows, interval %llu ms\n", (unsigned long long)r->type,
		    (unsigned long long)t->count, (unsigned long long)r->interval);
	if (r->error_handler)
		r->error_handler(t, NULL);
	else if (r->handler)
		r->handler(t);
	[t release];
}

static int debugging(void);

API void
sysmon_request_execute(OS_sysmon_request *r)
{
	if (debugging())
		fprintf(stderr, "sysmon: execute type %llu interval %llu timer %p\n", (unsigned long long)r->type,
		    (unsigned long long)r->interval, (void *)r->timer);
	[r retain];
	if (r->interval == 0) {
		dispatch_async(r->queue, ^{
			@autoreleasepool {
				deliver(r);
			}
			[r release];
		});
		return;
	}
	if (r->timer) {   /* executed again: the timer runs already */
		[r release];
		return;
	}
	r->timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, r->queue);
	dispatch_source_set_timer(r->timer, dispatch_time(DISPATCH_TIME_NOW, 0),
	    r->interval * NSEC_PER_MSEC, 50 * NSEC_PER_MSEC);
	dispatch_source_set_event_handler(r->timer, ^{
		@autoreleasepool {
			deliver(r);
		}
	});
	dispatch_source_set_cancel_handler(r->timer, ^{
		[r release];
	});
	dispatch_resume(r->timer);
}

API void
sysmon_request_cancel(OS_sysmon_request *r)
{
	if (r->timer)
		dispatch_source_cancel(r->timer);
}

/* ---- tables and rows ---- */

API uint64_t
sysmon_table_get_count(OS_sysmon_table *t)
{
	return t->count;
}

API void *
sysmon_table_get_row(OS_sysmon_table *t, uint64_t index)
{
	return index < t->count ? t->rows[index] : nil;
}

API void *
sysmon_table_copy_row(OS_sysmon_table *t, uint64_t index)
{
	return index < t->count ? [t->rows[index] retain] : nil;
}

API uint64_t
sysmon_table_get_timestamp(OS_sysmon_table *t)
{
	return t->timestamp;
}

API void
sysmon_table_apply(OS_sysmon_table *t, bool (^applier)(void *))
{
	/* As Apple's: every row, whatever the applier returns. */
	for (uint64_t i = 0; i < t->count; i++)
		applier(t->rows[i]);
}

API xpc_object_t
sysmon_row_get_value(OS_sysmon_row *row, uint32_t attribute)
{
	size_t rank = 0;
	xpc_object_t v;

	if (!has(row->attributes, attribute)) {
		/* (TERM8) SYSMON_DEBUG: each attribute the application reads that its request did not ask for, once */
		static uint64_t told[4];
		if (debugging() && attribute < 256 && !(told[attribute / 64] & (1ull << (attribute % 64)))) {
			told[attribute / 64] |= 1ull << (attribute % 64);
			fprintf(stderr, "sysmon: read of attribute %u, not in the request\n", attribute);
		}
		return NULL;
	}
	for (uint32_t a = 0; a < attribute; a++)
		rank += has(row->attributes, a);
	v = rank < row->nvalues ? row->values[rank] : NULL;
	return v;
}

API void
sysmon_row_apply(OS_sysmon_row *row, bool (^applier)(uint32_t, xpc_object_t))
{
	size_t rank = 0;

	for (uint32_t a = 0; row->attributes && a < row->attributes->size * 8; a++) {
		if (!has(row->attributes, a))
			continue;
		xpc_object_t v = rank < row->nvalues ? row->values[rank] : NULL;

		rank++;
		/* every attribute, whatever the applier returns (Activity Monitor's applier returns nothing
		   it means: stopping at its first answer left all but the first attribute out) */
		applier(a, v);
	}
}

/* sysmond's server side: there is no sysmond here. */
API xpc_object_t
_sysmon_build_reply_with_diff(void *table, void *previous, uint64_t flags)
{
	(void)table;
	(void)previous;
	(void)flags;
	return NULL;
}
