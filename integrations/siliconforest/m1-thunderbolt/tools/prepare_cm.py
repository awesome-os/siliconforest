#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Apply explicit native-only changes to a private copy, never the pin."""
import pathlib,re,sys
p=pathlib.Path(sys.argv[1]);names=set()
# Native has no NVMEM provider ABI. Disable that optional facility before
# allocating its state; the disabled Linux nvmem_register() otherwise returns
# EOPNOTSUPP late in tb_switch_add(), which removes an otherwise valid router.
# Keep DROM, port initialization and every actual enumeration error intact.
q=p/'switch.c';s=q.read_text();needle='static int tb_switch_nvm_init(struct tb_switch *sw)\n{';assert s.count(needle)==1
s=s.replace(needle,needle+'''
\tif (!IS_ENABLED(CONFIG_NVMEM)) {
\t\tsw->no_nvm_upgrade = true;
\t\treturn 0; /* NVM export/upgrade is disabled, not a successful registration. */
\t}
''')
needle='\tret = ida_alloc_range(ida, min_hopid, max_hopid, GFP_KERNEL);';assert s.count(needle)==1
q.write_text(s.replace(needle,needle+'''
\tif (ret < 0)
\t\ttb_port_warn(port, "HopID allocation failed: in=%u min=%d max=%d cap=%d error=%d\\n",
\t\t\t     in, min_hopid, max_hopid, port_max_hopid, ret);
'''))
q=p/'domain.c';s=q.read_text();needle='int tb_domain_init(void)\n{';assert s.count(needle)==1
q.write_text(s.replace(needle,needle+'\n\tif (!sf_tb_native_ready()) return -EPROBE_DEFER;'))
# Intel x86 Apple NHI resume device-links do not apply to the ARM platform.
q=p/'tb.c';s=q.read_text();a=s.index('static bool tb_apple_add_links(struct tb_nhi *nhi)\n{');b=s.index('\nstruct tb *tb_probe',a)
s=s[:a]+'#ifdef CONFIG_X86\n'+s[a:b]+'\n#else\nstatic bool tb_apple_add_links(struct tb_nhi *nhi) { return false; }\n#endif\n'+s[b:];q.write_text(s)
q=p/'usb4_port.c';s=q.read_text();needle='device_unregister(&usb4->dev);\n\t}';assert s.count(needle)==1
q.write_text(s.replace(needle,'device_unregister(&usb4->dev);\n\t\treturn ERR_PTR(ret);\n\t}'))
# Native Apple stop must observe VALID/IRQ disabled before clearing addresses.
# A failed check quarantines DMA backing; leave running set for final teardown.
q=p/'nhi.c';s=q.read_text()
# Close actual frame submission when native PM/publication has faulted. The
# IRQ path masks its hardware bit and quarantines DMA on that same latch.
needle='\tspin_lock_irqsave(&ring->lock, flags);\n\tif (ring->running) {';assert s.count(needle)==1
s=s.replace(needle,'\tspin_lock_irqsave(&ring->lock, flags);\n\tif (!sf_tb_native_ready()) {\n\t\tret = -ENODEV;\n\t} else if (ring->running) {')
needle='\tring_iowrite32options(ring, 0, 0);\n\tring_iowrite64desc(ring, 0, 0);';assert s.count(needle)==1
s=s.replace(needle,'\tring_iowrite32options(ring, 0, 0);\n\tif (sf_tb_nhi_check_stopped(ring)) goto err;\n\tring_iowrite64desc(ring, 0, 0);')
# Native release_ring_irq masks and joins the AIC source. Then join the ring
# worker before retiring descriptor mappings, rather than afterwards.
needle='\t\tnhi->ops->release_ring_irq(ring);\n\n\tdma_free_coherent(ring->nhi->dev,\n';assert s.count(needle)==1
s=s.replace(needle,'\t\tnhi->ops->release_ring_irq(ring);\n\n\tflush_work(&ring->work);\n\n\tdma_free_coherent(ring->nhi->dev,\n')
q.write_text(s)
# Upstream PCI tunnel allocation collapses path/credit failures into NULL
# and the caller reports ENOMEM. Preserve actual refusal details without
# changing the path, credit limits or its error/cleanup behavior.
q=p/'path.c';s=q.read_text()
needle='\tif (first_port != src || last_port != dst)\n\t\treturn NULL;';assert s.count(needle)==1
q.write_text(s.replace(needle,'''\tif (first_port != src || last_port != dst) {
\t\tif (src)
\t\t\ttb_port_warn(src, "path endpoints not reachable: %s (%d ports)\\n", name ? name : "unnamed", i);
\t\treturn NULL;
\t}'''))
q=p/'tunnel.c';s=q.read_text()
needle='\t\tif (credits < TB_MIN_PCIE_CREDITS)\n\t\t\treturn -ENOSPC;';assert s.count(needle)==1
s=s.replace(needle,'''\t\tif (credits < TB_MIN_PCIE_CREDITS) {
\t\t\ttb_port_warn(port, "PCIe credit shortage: max=%u available=%u minimum=%u\\n",
\t\t\t\t     sw->max_pcie_credits, available, TB_MIN_PCIE_CREDITS);
\t\t\treturn -ENOSPC;
\t\t}''')
q.write_text(s)
for source in p.glob('*.c'):
 names.update(re.findall(r'\b(trace_tb_\w+)\s*\(',source.read_text()))
with (p/'trace.h').open('w') as f:
 f.write('/* SPDX-License-Identifier: GPL-2.0 */\n/* Linux tracepoint instrumentation disabled in the native port. */\n')
 for name in sorted(names):f.write('#define '+name+'(...) '+('false' if name.endswith('_enabled') else '((void)0)')+'\n')
