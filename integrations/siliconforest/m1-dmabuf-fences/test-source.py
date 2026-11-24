#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Actual deployed sync-file ioctl code, isolated provider failure/refcount checks."""
from pathlib import Path
import os,re,subprocess,tempfile
here=Path(__file__).resolve().parent
frozen=Path('/nix/store/li7scbkkb5s797ffk6wyb64m40wgv92k-puredarwin-agx-killable-tree/src/Kernel/fbsdkpi/drm/src/sf_dma_buf_syncfile.c')
source=frozen.read_text();source=re.sub(r'^#include .*\n','',source,flags=re.M)
# The wrapper uses the real private_data object; retain it in the host fixture too.
template=(here/'host-sync.cpp').read_text().replace('@SOURCE@',source)
# Model sync_file private_data lifetime instead of leaking the test wrapper.
template=template.replace('struct file {dma_fence *fence;};','struct sync_file;\nstruct file {dma_fence *fence;sync_file *owner=nullptr;};')
template=template.replace('s->file=new file{dma_fence_get(f)};','s->file=new file{dma_fence_get(f),s};')
template=template.replace('delete f;--files;','delete f->owner;delete f;--files;')
with tempfile.TemporaryDirectory(prefix='sf-sync-source-') as tmp:
 root=Path(tmp);p=root/'host.cpp';p.write_text(template)
 subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-variable','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(p),'-o',str(root/'host')],check=True)
 subprocess.run([str(root/'host')],check=True,timeout=15)
