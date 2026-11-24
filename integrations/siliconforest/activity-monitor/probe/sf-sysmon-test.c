// SPDX-License-Identifier: MIT
// libsysmon as Activity Monitor uses it (its attribute lists), from the command line:
//   sf-sysmon-test [type] [rounds] [pid]   prints each table's row count and the first rows' values
//                                          (or the row of that pid)
#include <dispatch/dispatch.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xpc/xpc.h>
typedef void *obj;
obj sysmon_request_create(uint64_t, void (^)(obj));
void sysmon_request_add_attribute(obj, uint32_t);
void sysmon_request_set_interval(obj, uint64_t);
void sysmon_request_execute(obj);
void sysmon_request_cancel(obj);
uint64_t sysmon_table_get_count(obj);
void sysmon_table_apply(obj, bool (^)(obj));
void sysmon_row_apply(obj, bool (^)(uint32_t, xpc_object_t));
int main(int argc, char **argv)
{
	int type = argc > 1 ? atoi(argv[1]) : 1, rounds = argc > 2 ? atoi(argv[2]) : 3;
	__block long want = argc > 3 ? atol(argv[3]) : -1;
	static const uint32_t proc[] = { 13, 2, 56, 4, 6, 5, 55, 15, 12, 44, 51, 21, 22, 66, 50, 53, 54, 59, 46, 35, 29, 28, 33, 34, 36, 52, 62, 23, 24, 72, 73, 74, 31, 32 };
	static const uint32_t sys[] = { 24, 4, 1, 20, 33, 34, 36, 38, 14, 25, 26, 27, 28, 29, 30, 31, 32 };
	__block int n = 0;
	dispatch_semaphore_t done = dispatch_semaphore_create(0);
	obj r = sysmon_request_create(type, ^(obj t) {
		printf("round %d: %llu rows\n", n, t ? (unsigned long long)sysmon_table_get_count(t) : 0ULL);
		__block int shown = 0;
		if (t && n == rounds - 1)
			sysmon_table_apply(t, ^bool(obj row) {
				if (want >= 0) {
					__block long pid = -1;
					sysmon_row_apply(row, ^bool(uint32_t a, xpc_object_t v) {
						if (a == 4 && v) pid = (long)xpc_uint64_get_value(v);
						return a < 4;
					});
					if (pid != want) return true;
				} else if (shown++ >= (type == 1 ? 6 : 1))
					return true;
				printf("--\n");
				sysmon_row_apply(row, ^bool(uint32_t a, xpc_object_t v) {
					char *d = v ? xpc_copy_description(v) : strdup("(none)");
					if (strlen(d) > 100) d[100] = 0;
					printf("  %2u %s\n", a, d);
					free(d);
					return true;
				});
				return true;
			});
		fflush(stdout);
		if (++n >= rounds) dispatch_semaphore_signal(done);
	});
	const uint32_t *a = type == 1 ? proc : sys;
	size_t na = type == 1 ? sizeof(proc) / 4 : sizeof(sys) / 4;
	for (size_t i = 0; i < na; i++)
		sysmon_request_add_attribute(r, a[i]);
	sysmon_request_set_interval(r, 500);
	sysmon_request_execute(r);
	if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 20LL * NSEC_PER_SEC)) != 0)
		printf("timed out after %d tables\n", n);
	sysmon_request_cancel(r);
	return 0;
}
