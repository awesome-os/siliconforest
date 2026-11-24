#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Private dock variants of existing sources; preserve their original notices.
# No shared Wi-Fi source or installed network configuration is changed.
import hashlib,pathlib,sys

def load(path,expected):
 p=pathlib.Path(path);raw=p.read_bytes()
 if hashlib.sha256(raw).hexdigest()!=expected:raise SystemExit('network source differs from reviewed input: '+str(p))
 return raw.decode()
def replace(text,old,new,count=1):
 if text.count(old)!=count:raise SystemExit('network source shape mismatch: '+old[:70])
 return text.replace(old,new)

dhcp=load(sys.argv[1],'c59f8176d153dcab78bd7658272089cfa5a6406db2b61febfc6efc0986136b44')
net=load(sys.argv[2],'90a3feedacf6b20f12aee0094d808c13fae0a05967725a29bf7432f402092878')
out=pathlib.Path(sys.argv[3]);out.mkdir(parents=True,exist_ok=True)
# Every caller uses the separate interface-only helper; no fallback to netsetup.
dhcp=replace(dhcp,'"/bin/netsetup"','"/private/var/db/sftb/sf-dock-netsetup"',2)
dhcp=replace(dhcp,'    setsockopt(d->udp, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));', '''    unsigned int bound_if = if_nametoindex(d->ifname);
    if (!bound_if || setsockopt(d->udp, IPPROTO_IP, IP_BOUND_IF,
        &bound_if, sizeof(bound_if)) != 0) {
        udp_shut(d); return -1; /* No renewal through the Wi-Fi route. */
    }
    setsockopt(d->udp, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));''')
start=dhcp.index('    if (d->applied.router != 0 && (s = socket(PF_ROUTE, SOCK_RAW, AF_INET)) >= 0) {')
end=dhcp.index('    if ((s = socket(AF_INET, SOCK_DGRAM, 0)) >= 0) {',start)
dhcp=dhcp[:start]+dhcp[end:]
dhcp=replace(dhcp,'    struct {\n        struct rt_msghdr hdr;\n        struct sockaddr_in dst, gw, mask;\n    } rt;\n','')
dhcp=replace(dhcp,'    if (i < argc)\n        ifname = argv[i];','    if (i + 1 != argc) { usage(); return 2; }\n    ifname = argv[i];')
dhcp=replace(dhcp,'    changed = d->applied.addr != l.addr || d->applied.mask != l.mask || d->applied.router != l.router ||\n              d->applied.dns != l.dns;','    changed = d->applied.addr != l.addr || d->applied.mask != l.mask;')
dhcp=replace(dhcp,'d->applied.mask != l.mask || d->applied.router != l.router))','d->applied.mask != l.mask))')
dhcp=replace(dhcp,'        d->applied = l;','        if (rc == 0) d->applied = l;')
dhcp=replace(dhcp,'    /* the address stays configured: a restarted daemon is offered the same one */','    /* Retire only the address successfully applied by this dock client. */')
dhcp=replace(dhcp,'    unlink(d->leasepath);\n    bpf_shut(d);\n    udp_shut(d);\n    return 0;','    deconfigure(d, "client stopped");\n    bpf_shut(d);\n    udp_shut(d);\n    return 0;')
dhcp=replace(dhcp,'int main(int argc, char **argv)', '''/* Refuse pre-existing IPv4 ownership before any DHCP operation. */
static int sf_dock_address_available(const char *name)
{
    struct ifaddrs *all = NULL;
    if (getifaddrs(&all) != 0) return 0;
    int available = 1;
    for (struct ifaddrs *p = all; p; p = p->ifa_next)
        if (p->ifa_addr && !strcmp(p->ifa_name, name) &&
            p->ifa_addr->sa_family == AF_INET) { available = 0; break; }
    freeifaddrs(all);
    return available;
}
int main(int argc, char **argv)''')
dhcp=replace(dhcp,'    if (daemon_mode && !uponly) {','    if (!uponly && !sf_dock_address_available(ifname)) {\n        fprintf(stderr, "dock interface already has IPv4 configuration or inventory failed\\n");\n        return 1;\n    }\n    if (daemon_mode && !uponly) {')
dhcp=replace(dhcp,'    snprintf(tmp, sizeof(tmp), "%s.new", d->leasepath);','    if (d->applied.addr != d->cur.addr || d->applied.mask != d->cur.mask) {\n        unlink(d->leasepath); return; /* Do not publish an unapplied lease. */\n    }\n    snprintf(tmp, sizeof(tmp), "%s.new", d->leasepath);')
dhcp=dhcp.replace('sf-dhcp.%s.lease','sf-dock-dhcp.%s.lease').replace('sf-dhcp:', 'sf-dock-dhcp:')
dhcp=dhcp.replace('[IFNAME]', 'IFNAME').replace('usage: sf-dhcp', 'usage: sf-dock-dhcp')
# This native helper touches only the explicitly supplied leased interface.
net=replace(net,'    if (add_addr(sock, "lo0", "127.0.0.1", "255.0.0.0", "127.255.255.255") < 0)\n        rc = 1;\n    if (set_flags(sock, "lo0", IFF_UP, 0) < 0)\n        rc = 1;\n','')
net=replace(net,'    if (add_default_route(cfg->router) < 0)\n        rc = 1;\n    if (cfg->write_resolver && write_resolver_config(cfg->dns) < 0)\n        rc = 1;','    /* Dock interface-only: no default route, resolver, or other interface. */')
net=replace(net,'    int argi = 1;','    int argi = 1;\n    if (argc != 7 || !argv[1][0] || argv[1][0] == 45) {\n        fprintf(stderr, "usage: sf-dock-netsetup IFNAME ADDRESS MASK GATEWAY DNS BROADCAST\\n");\n        return 2;\n    }')
net=replace(net,'    cfg.write_resolver = 1;','    cfg.write_resolver = 0;')
net=replace(net,'        if (errno == EEXIST) {\n            sf_log("%s already has %s", ifname, addr);\n            return 0;\n        }','        /* Never acquire ownership of an address added by another client. */')
(out/'sf-dock-dhcp.c').write_text(dhcp)
(out/'sf-dock-netsetup.c').write_text(net)
