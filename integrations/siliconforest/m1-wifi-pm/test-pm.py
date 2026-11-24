#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile the patched actual PM functions against fake firmware. No hardware."""
from pathlib import Path
import os,subprocess,tempfile,shutil
here=Path(__file__).resolve().parent
frozen=Path('/nix/store/a0whnzqlwxh2s66i2cl9vw2h1cc4krvd-puredarwin-bwfm-tree')
def block(s,needle):
 start=s.index(needle);brace=s.index('{',start);depth=0
 for i in range(brace,len(s)):
  depth+=(s[i]=='{')-(s[i]=='}')
  if not depth:return s[start:i+1]
 raise AssertionError(needle)
with tempfile.TemporaryDirectory(prefix='sf-wifi-pm-') as tmp:
 root=Path(tmp);dst=root/'src/Kernel/Extensions/SFBwfm';dst.mkdir(parents=True)
 names=['SFBwfm.cpp','bwfm.c','bwfmvar.h','sf_bwfm_compat.h','sf_bwfm_glue.c','sf_bwfm_glue.h']
 for name in names:shutil.copyfile(frozen/'src/Kernel/Extensions/SFBwfm'/name,dst/name)
 for name in ['fxnet-pm.patch','pm-admission.patch']:
  subprocess.run(['patch','-p1','--batch','--fuzz=0','-i',str(here/name)],cwd=root,check=True)
 c=(dst/'bwfm.c').read_text();cpp=(dst/'SFBwfm.cpp').read_text()
 values={'BOOT':block(cpp,'int sf_bwfm_want_pm(void)')+'\n'+block(cpp,'int sf_bwfm_want_pm2_ret(void)'),
  'SET':block(c,'int\nbwfm_pm_set('),'GET':block(c,'void\nbwfm_pm_get('),
  'PAYLOAD':block(cpp,'struct sf80211_powersave_data {')+';',
  'DISPATCH':block(cpp,'if (type == APPLE80211_IOC_POWERSAVE &&'),
  'INIT':c[c.index('\tif (!sc->sc_pm_known)',c.index('bwfm_init1(struct ifnet *ifp)\n{')):c.index('\n\tbwfm_fwvar_var_set_int(sc, "txbf", 1);')]}
 source=(here/'host-pm.cpp').read_text()
 for key,value in values.items():source=source.replace('@'+key+'@',value)
 f=root/'test.cpp';f.write_text(source)
 subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(f),'-o',str(root/'test')],check=True)
 subprocess.run([str(root/'test')],check=True,timeout=15)
