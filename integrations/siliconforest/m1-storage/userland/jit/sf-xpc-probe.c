// SPDX-License-Identifier: MIT
// The call that killed the Claude Code CLI (docs/roadmap/issues/SF-X-claude-code.md):
// xpc_copy_description() of a connection, as Security's client makes it for every failed
// request. With a libSystem from before patches/xpc-copy-description-never-aborts.patch the
// process is ended by libxpc ("SiliconForest libxpc misuse: Unknown XPC type", status 137 or 134);
// with the patch it prints "SFXPC ok (connection) <...>" and returns 0. Used to tell which
// libSystem a process got (DYLD_LIBRARY_PATH) before the installed one is replaced.
#include <stdio.h>
#include <stdlib.h>

extern void *xpc_connection_create(const char *name, void *queue);
extern char *xpc_copy_description(void *object);

int
main(void)
{
	void *c = xpc_connection_create("org.siliconforest.xpc-probe", NULL);
	char *d;

	if (c == NULL) {
		printf("SFXPC FAIL no connection object\n");
		return 2;
	}
	d = xpc_copy_description(c);
	printf("SFXPC ok %s", d != NULL ? d : "(null)\n");
	return d != NULL ? 0 : 3;
}
