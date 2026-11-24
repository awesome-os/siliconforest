// SPDX-License-Identifier: MIT
// What the kernel answers to the calls libsysmon builds Activity Monitor's tables from
// (docs/roadmap/issues/SF-AA-activity-monitor.md). Run on the target as root:
//   sf-am-probe [pid]
#include <errno.h>
#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/proc_info.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <net/if.h>
#include <net/route.h>
#include <net/if_dl.h>

static void sysctl_u64(const char *name)
{
	uint64_t v = 0; size_t n = sizeof(v);
	int r = sysctlbyname(name, &v, &n, NULL, 0);
	printf("sysctl %-28s rc=%d size=%zu value=%llu\n", name, r ? errno : 0, n, (unsigned long long)(n == 4 ? (uint32_t)v : v));
}

int main(int argc, char **argv)
{
	pid_t pid = argc > 1 ? atoi(argv[1]) : getpid();
	mach_timebase_info_data_t tb; mach_timebase_info(&tb);
	printf("timebase %u/%u\n", tb.numer, tb.denom);

	int n = proc_listallpids(NULL, 0);
	printf("proc_listallpids count %d\n", n);

	struct proc_taskallinfo ta;
	int r = proc_pidinfo(pid, PROC_PIDTASKALLINFO, 0, &ta, sizeof(ta));
	printf("PROC_PIDTASKALLINFO pid %d -> %d (want %zu) errno %d\n", pid, r, sizeof(ta), r <= 0 ? errno : 0);
	if (r == sizeof(ta)) {
		struct proc_taskinfo *t = &ta.ptinfo;
		printf("  comm %s name %s status %u flags 0x%x nice %d\n", ta.pbsd.pbi_comm, ta.pbsd.pbi_name, ta.pbsd.pbi_status, ta.pbsd.pbi_flags, ta.pbsd.pbi_nice);
		printf("  virtual %llu resident %llu user %llu system %llu threads %d running %d\n",
		    t->pti_virtual_size, t->pti_resident_size, t->pti_total_user, t->pti_total_system, t->pti_threadnum, t->pti_numrunning);
		printf("  faults %d pageins %d cow %d msgs sent %d rcvd %d mach %d unix %d csw %d prio %d\n",
		    t->pti_faults, t->pti_pageins, t->pti_cow_faults, t->pti_messages_sent, t->pti_messages_received,
		    t->pti_syscalls_mach, t->pti_syscalls_unix, t->pti_csw, t->pti_priority);
	}
	struct rusage_info_v4 ri;
	r = proc_pid_rusage(pid, RUSAGE_INFO_V4, (rusage_info_t *)&ri);
	printf("proc_pid_rusage V4 -> %d errno %d\n", r, r ? errno : 0);
	if (r == 0)
		printf("  footprint %llu resident %llu user %llu system %llu wired %llu idle_wkups %llu intr_wkups %llu\n"
		    "  diskio read %llu written %llu  energy %llu instr %llu cycles %llu lifetime_max_footprint %llu interval_max %llu\n",
		    ri.ri_phys_footprint, ri.ri_resident_size, ri.ri_user_time, ri.ri_system_time, ri.ri_wired_size,
		    ri.ri_pkg_idle_wkups, ri.ri_interrupt_wkups, ri.ri_diskio_bytesread, ri.ri_diskio_byteswritten,
		    ri.ri_billed_energy, ri.ri_instructions, ri.ri_cycles, ri.ri_lifetime_max_phys_footprint, ri.ri_interval_max_phys_footprint);
	struct rusage_info_v6 r6;
	r = proc_pid_rusage(pid, RUSAGE_INFO_V6, (rusage_info_t *)&r6);
	printf("proc_pid_rusage V6 -> %d errno %d\n", r, r ? errno : 0);

	mach_port_t task = MACH_PORT_NULL;
	kern_return_t kr = task_for_pid(mach_task_self(), pid, &task);
	printf("task_for_pid -> %d\n", kr);
	if (kr == KERN_SUCCESS) {
		mach_port_name_array_t names; mach_msg_type_number_t nn = 0; mach_port_type_array_t types; mach_msg_type_number_t nt = 0;
		kr = mach_port_names(task, &names, &nn, &types, &nt);
		printf("  mach_port_names -> %d count %u\n", kr, nn);
		task_vm_info_data_t vi; mach_msg_type_number_t c = TASK_VM_INFO_COUNT;
		kr = task_info(task, TASK_VM_INFO, (task_info_t)&vi, &c);
		printf("  TASK_VM_INFO -> %d count %u footprint %llu internal %llu compressed %llu purgeable_vol %llu reusable %llu\n",
		    kr, c, vi.phys_footprint, vi.internal, vi.compressed, vi.purgeable_volatile_pmap, vi.reusable);
		struct task_power_info_v2 pi; c = TASK_POWER_INFO_V2_COUNT;
		kr = task_info(task, TASK_POWER_INFO_V2, (task_info_t)&pi, &c);
		printf("  TASK_POWER_INFO_V2 -> %d idle wakeups %llu\n", kr, pi.cpu_energy.task_platform_idle_wakeups);
	}
	struct proc_regioninfo rg;
	r = proc_pidinfo(pid, PROC_PIDREGIONINFO, 0, &rg, sizeof(rg));
	printf("PROC_PIDREGIONINFO -> %d\n", r);
	char path[PROC_PIDPATHINFO_MAXSIZE];
	r = proc_pidpath(pid, path, sizeof(path));
	printf("proc_pidpath -> %d %s\n", r, r > 0 ? path : "");
	struct proc_bsdshortinfo si;
	r = proc_pidinfo(1, PROC_PIDT_SHORTBSDINFO, 0, &si, sizeof(si));
	printf("SHORTBSDINFO pid 1 -> %d comm %s\n", r, r > 0 ? si.pbsi_comm : "");

	/* system-wide */
	vm_statistics64_data_t vs; mach_msg_type_number_t c = HOST_VM_INFO64_COUNT;
	kr = host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vs, &c);
	vm_size_t pagesize = 0; host_page_size(mach_host_self(), &pagesize);
	printf("host_statistics64 VM -> %d count %u page %lu\n", kr, c, (unsigned long)pagesize);
	if (kr == 0)
		printf("  free %u active %u inactive %u wire %u spec %u throttled %u compressor %u internal %u external %u purgeable %u uncompressed_in_compressor %llu\n"
		    "  pageins %llu pageouts %llu faults %llu compressions %llu swapins %llu swapouts %llu\n",
		    vs.free_count, vs.active_count, vs.inactive_count, vs.wire_count, vs.speculative_count, vs.throttled_count,
		    vs.compressor_page_count, vs.internal_page_count, vs.external_page_count, vs.purgeable_count, vs.total_uncompressed_pages_in_compressor,
		    vs.pageins, vs.pageouts, vs.faults, vs.compressions, vs.swapins, vs.swapouts);
	natural_t ncpu = 0; processor_info_array_t pinfo; mach_msg_type_number_t pcount = 0;
	kr = host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &ncpu, &pinfo, &pcount);
	printf("host_processor_info CPU_LOAD -> %d cpus %u\n", kr, ncpu);
	for (natural_t i = 0; kr == 0 && i < ncpu; i++) {
		processor_cpu_load_info_t l = (processor_cpu_load_info_t)pinfo + i;
		printf("  cpu%u user %u system %u idle %u nice %u\n", i, l->cpu_ticks[CPU_STATE_USER], l->cpu_ticks[CPU_STATE_SYSTEM], l->cpu_ticks[CPU_STATE_IDLE], l->cpu_ticks[CPU_STATE_NICE]);
	}
	host_cpu_load_info_data_t hl; c = HOST_CPU_LOAD_INFO_COUNT;
	kr = host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, (host_info_t)&hl, &c);
	printf("host_statistics CPU_LOAD -> %d user %u sys %u idle %u\n", kr, hl.cpu_ticks[0], hl.cpu_ticks[1], hl.cpu_ticks[2]);
	sysctl_u64("hw.memsize"); sysctl_u64("hw.ncpu"); sysctl_u64("hw.activecpu"); sysctl_u64("hw.perflevel0.logicalcpu");
	sysctl_u64("hw.perflevel1.logicalcpu"); sysctl_u64("kern.memorystatus_level"); sysctl_u64("vm.memory_pressure");
	sysctl_u64("kern.memorystatus_vm_pressure_level"); sysctl_u64("vm.page_free_count"); sysctl_u64("kern.num_tasks");
	sysctl_u64("kern.num_threads"); sysctl_u64("vm.compressor_mode");
	struct xsw_usage sw; size_t sn = sizeof(sw);
	r = sysctlbyname("vm.swapusage", &sw, &sn, NULL, 0);
	printf("vm.swapusage rc %d total %llu used %llu\n", r ? errno : 0, sw.xsu_total, sw.xsu_used);
	int level = -1;
	r = syscall(SYS_memorystatus_get_level, &level);
	printf("syscall memorystatus_get_level -> %d errno %d level %d\n", r, r ? errno : 0, level);
	struct timeval bt; size_t bn = sizeof(bt);
	r = sysctlbyname("kern.boottime", &bt, &bn, NULL, 0);
	printf("kern.boottime rc %d sec %ld\n", r, (long)bt.tv_sec);
	double la[3]; printf("getloadavg %d: %.2f %.2f %.2f\n", getloadavg(la, 3), la[0], la[1], la[2]);

	/* interfaces */
	int mib[6] = { CTL_NET, PF_ROUTE, 0, 0, NET_RT_IFLIST2, 0 };
	size_t len = 0;
	r = sysctl(mib, 6, NULL, &len, NULL, 0);
	printf("NET_RT_IFLIST2 size rc %d len %zu\n", r ? errno : 0, len);
	if (r == 0 && len) {
		char *buf = malloc(len);
		if (sysctl(mib, 6, buf, &len, NULL, 0) == 0)
			for (char *p = buf; p < buf + len;) {
				struct if_msghdr *ifm = (struct if_msghdr *)p;
				if (ifm->ifm_type == RTM_IFINFO2) {
					struct if_msghdr2 *m2 = (struct if_msghdr2 *)p;
					struct sockaddr_dl *sdl = (struct sockaddr_dl *)(m2 + 1);
					printf("  if %.*s ipackets %llu opackets %llu ibytes %llu obytes %llu\n", sdl->sdl_nlen, sdl->sdl_data,
					    m2->ifm_data.ifi_ipackets, m2->ifm_data.ifi_opackets, m2->ifm_data.ifi_ibytes, m2->ifm_data.ifi_obytes);
				}
				p += ifm->ifm_msglen;
			}
		free(buf);
	}
	/* disk */
	return 0;
}
