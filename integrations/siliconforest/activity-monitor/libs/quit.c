// SPDX-License-Identifier: MIT
// libquit for Apple's Activity Monitor on SiliconForest (docs/roadmap/issues/SF-AA-activity-monitor.md).
// LQForceQuit is what Activity Monitor calls when Launch Services does not force-quit the
// process: it ends the process (SIGKILL), as macOS's libquit does in the end. Hang reports go
// nowhere (there is no spindump service).
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <sys/types.h>

int LQForceQuit(pid_t pid, void *options)
{
	(void)options;
	if (pid <= 0)
		return EINVAL;
	return kill(pid, SIGKILL) == 0 ? 0 : errno;
}

void LQReportPotentialHang(void *a) { (void)a; }
