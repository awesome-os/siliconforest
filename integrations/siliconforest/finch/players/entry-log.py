#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Opt-in process-local stderr capture in the original GPL VLC entry source."""
from pathlib import Path
import sys
p=Path(sys.argv[1]);s=p.read_text();old='int main(int i_argc, const char *ppsz_argv[])\n{'
new=old+'''
    /* SiliconForest: explicit own-stage log path, no global logger changes. */
    const char *sf_log = getenv("SF_VLC_LOG");
    const char *sf_watchdog = getenv("SF_VLC_HEADLESS_TIMEOUT");
    if (sf_watchdog && sf_watchdog[0] == '1' && !sf_watchdog[1]) alarm(20);
    if (sf_log && *sf_log) {
        if (!freopen(sf_log, "a", stderr)) return 1;
        setvbuf(stderr, NULL, _IONBF, 0);
        fprintf(stderr, "SF_VLC_ENTRY pid=%d argc=%d\\n", getpid(), i_argc);
    }
    int sf_module = sf_vlc_module_probe();
    if (sf_module >= 0) return sf_module;
'''
assert s.count(old)==1;s=s.replace(old,new).replace('#include <stdlib.h>','#include <stdlib.h>\n#include <stdio.h>\n#include "SFVLCHAL.h"\n#include "SFVLCModule.h"',1);anchors={
 '    /* Handle parameters */':'    if (sf_log) fprintf(stderr, "SF_VLC_STAGE signals-ready\\n");\n',
 '    /* Initialize libvlc */':'''    /* Explicit test profile only: registration defaults live in this process.
     * Do not write VLCFirstRun to the user's persistent preferences. Network
     * metadata access is still separately denied by our launch arguments. */
    const char *sf_configured = getenv("SF_VLC_TEST_FIRST_RUN_CONFIGURED");
    if (sf_configured && sf_configured[0] == '1' && !sf_configured[1]) {
        @autoreleasepool {
            [[NSUserDefaults standardUserDefaults] registerDefaults:
                @{ @"VLCFirstRun": [NSDate date] }];
        }
        fprintf(stderr, "SF_VLC_TEST first-run=process-local-registration-only\\n");
    }
    if (sf_log) fprintf(stderr, "SF_VLC_STAGE before-libvlc-new argc=%d\\n", argc);
''',
 '    if (vlc == NULL)':'    if (sf_log) fprintf(stderr, "SF_VLC_STAGE libvlc-new result=%p\\n", (void *)vlc);\n    if (sf_log && !vlc) { const char *sf_error=libvlc_errmsg(); fprintf(stderr, "SF_VLC_ERROR %s\\n", sf_error ? sf_error : "no public error supplied"); }\n',
 '    libvlc_add_intf(vlc, "hotkeys,none");':'    if (sf_log) fprintf(stderr, "SF_VLC_STAGE before-interface\\n");\n',
 '    libvlc_playlist_play(vlc, -1, 0, NULL);':'    sf_hal_begin();\n    if (sf_log) fprintf(stderr, "SF_VLC_STAGE interface-ready\\n");\n',
 '    dispatch_release(sigIntSource);':'    sf_hal_end();\n',
 '    return ret;':'    if (sf_log) fprintf(stderr, "SF_VLC_STAGE exit-status=%d\\n", ret);\n',
}
for anchor,addition in anchors.items():
    assert s.count(anchor)==1
    s=s.replace(anchor,addition+anchor)
p.write_text(s)
