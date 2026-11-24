# SPDX-License-Identifier: MIT
"""Apply the network notification backend in an isolated upstream build tree."""
from pathlib import Path
import sys
root=Path(sys.argv[1]); own=Path(sys.argv[2]); base=root/'src/Libraries/SystemConfiguration'
def replace(file,old,new):
 p=base/file;s=p.read_text();assert s.count(old)==1,(file,s.count(old),old[:90]);p.write_text(s.replace(old,new))
for name in ['sc-route-source.c','sc-route-source.h','sc-reach-callbacks.inc','sc-store-network.c']:
 (base/name).write_bytes((own/name).read_bytes())
replace('sf_sc_reachability.c','#include <unistd.h>','#include <unistd.h>\n#include <stdlib.h>\nstatic Boolean sf_reach_state_attach(CFMutableDictionaryRef target);')
replace('sf_sc_reachability.c','\treturn (SCNetworkReachabilityRef)target;','\tif (!sf_reach_state_attach(target)) { CFRelease(target); return NULL; }\n\treturn (SCNetworkReachabilityRef)target;')
p=base/'sf_sc_reachability.c';s=p.read_text();at=s.index('/*\n * No change notifications:');p.write_text(s[:at]+'#include "sc-reach-callbacks.inc"\n')
replace('CMakeLists.txt','    sf_sc_reachability.c','    sf_sc_reachability.c\n    sc-route-source.c\n    sc-store-network.c')
replace('SystemConfiguration.fproj/SCDynamicStoreInternal.h','\tBoolean\t\t\t\tserverNullSession;','\tBoolean\t\t\t\tserverNullSession;\n\tBoolean sfRouteOnly;\n\tCFArrayRef sfNetworkInterfaces;')
replace('SystemConfiguration.fproj/SCDynamicStoreInternal.h','__END_DECLS','Boolean SFSCStoreAvailable(void);\nBoolean SFSCStoreSetKeys(SCDynamicStorePrivateRef store, CFArrayRef keys, CFArrayRef patterns);\nCFRunLoopSourceRef SFSCStoreSource(CFAllocatorRef allocator, SCDynamicStorePrivateRef store, CFIndex order);\n__END_DECLS')
replace('SystemConfiguration.fproj/SCDOpen.c','\t/* release any keys being watched */','\tif (storePrivate->sfNetworkInterfaces) CFRelease(storePrivate->sfNetworkInterfaces);\n\t/* release any keys being watched */')
replace('SystemConfiguration.fproj/SCDOpen.c','\tif (!ok) {\n\t\tCFRelease(storePrivate);','\tif (!ok && callout != NULL && storeOptions == NULL &&\n\t    SCError() == kSCStatusNoStoreServer && SFSCStoreAvailable()) {\n\t\tstorePrivate->sfRouteOnly = TRUE;\n\t\t_SCErrorSet(kSCStatusOK);\n\t\tok = TRUE;\n\t}\n\tif (!ok) {\n\t\tCFRelease(storePrivate);')
replace('SystemConfiguration.fproj/SCDNotifierSetKeys.c','\tif (storePrivate->server == MACH_PORT_NULL) {','\tif (storePrivate->sfRouteOnly) return SFSCStoreSetKeys(storePrivate, keys, patterns);\n\tif (storePrivate->server == MACH_PORT_NULL) {')
p=base/'SystemConfiguration.fproj/SCDNotifierInformViaCallback.c';s=p.read_text();at=s.index('SCDynamicStoreCreateRunLoopSource(CFAllocatorRef');head,tail=s[:at],s[at:];old='\tif (storePrivate->server == MACH_PORT_NULL) {';assert old in tail;tail=tail.replace(old,'\tif (storePrivate->sfRouteOnly) return SFSCStoreSource(allocator, storePrivate, order);\n'+old,1);p.write_text(head+tail)
