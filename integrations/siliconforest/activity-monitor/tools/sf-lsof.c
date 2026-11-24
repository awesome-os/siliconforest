// SPDX-License-Identifier: MIT
// sf-lsof: /usr/sbin/lsof for SiliconForest, as much of it as Activity Monitor's inspector asks for. Its "Open Files
// and Ports" tab runs
//   /usr/sbin/lsof -Fn -p <pid>
// and lists the names it reads back. There was no lsof here, so the tab stayed empty. This one reads the process's
// current directory, executable, mapped files and file descriptors from the kernel (proc_pidinfo, proc_pidfdinfo,
// proc_pidpath; the same calls lsof makes on macOS; proc_pidfdinfo by its system call) and writes them in lsof's field format: a "p<pid>" line, then for
// each file an "f<descriptor>" line ("cwd", "txt", or the number) and, with -Fn, an "n<name>" line.
//
//   lsof [-F <fields>] [-n] [-P] [-w] -p <pid>[,<pid>...]     (fields: p, f, n, t (type); others are ignored)
//   lsof -p <pid>                                             (the same as a plain table: FD, TYPE, NAME)
//
// Names: a file's path; a TCP or UDP socket as lsof writes it with -nP ("127.0.0.1:22->10.1.1.5:51234",
// "*:5353"); a UNIX socket by its path, or "->0x<peer>"; a pipe "->0x<peer>"; a kqueue "count=<n>, state=<s>".
#include <arpa/inet.h>
#include <errno.h>
#include <libproc.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/proc_info.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

static int fieldName = 1, fieldType = 0, fieldFormat = 0;

// proc_pidfdinfo is not exported by this libSystem: the system call it wraps (PROC_INFO_CALL_PIDFDINFO)
static int pidfdinfo(int pid, int fd, int flavor, void *buffer, int size) {
    return syscall(SYS_proc_info, 3, pid, flavor, (uint64_t)fd, buffer, size);
}

static void emit(const char *fd, const char *type, const char *name) {
    if (fieldFormat) {
        printf("f%s\n", fd);
        if (fieldType)
            printf("t%s\n", type);
        if (fieldName)
            printf("n%s\n", name);
    } else
        printf("%-6s %-6s %s\n", fd, type, name);
}

static void socketName(struct socket_fdinfo *si, char *out, size_t n, const char **type) {
    struct socket_info *s = &si->psi;
    int family = s->soi_family;

    if (family == AF_INET || family == AF_INET6) {
        struct in_sockinfo *in = (s->soi_kind == SOCKINFO_TCP) ? &s->soi_proto.pri_tcp.tcpsi_ini : &s->soi_proto.pri_in;
        char l[INET6_ADDRSTRLEN] = "*", f[INET6_ADDRSTRLEN] = "*";
        int lport = ntohs((uint16_t)in->insi_lport), fport = ntohs((uint16_t)in->insi_fport);
        const char *proto = s->soi_protocol == IPPROTO_UDP ? "UDP" : "TCP";

        if (family == AF_INET) {
            if (in->insi_laddr.ina_46.i46a_addr4.s_addr != 0)
                inet_ntop(AF_INET, &in->insi_laddr.ina_46.i46a_addr4, l, sizeof(l));
            if (in->insi_faddr.ina_46.i46a_addr4.s_addr != 0)
                inet_ntop(AF_INET, &in->insi_faddr.ina_46.i46a_addr4, f, sizeof(f));
            *type = "IPv4";
        } else {
            static const struct in6_addr any;
            if (memcmp(&in->insi_laddr.ina_6, &any, sizeof(any)) != 0)
                inet_ntop(AF_INET6, &in->insi_laddr.ina_6, l, sizeof(l));
            if (memcmp(&in->insi_faddr.ina_6, &any, sizeof(any)) != 0)
                inet_ntop(AF_INET6, &in->insi_faddr.ina_6, f, sizeof(f));
            *type = "IPv6";
        }
        if (fport != 0)
            snprintf(out, n, "%s%s%s:%d->%s%s%s:%d", family == AF_INET6 ? "[" : "", l, family == AF_INET6 ? "]" : "", lport,
                     family == AF_INET6 ? "[" : "", f, family == AF_INET6 ? "]" : "", fport);
        else
            snprintf(out, n, "%s%s%s:%s", family == AF_INET6 && strcmp(l, "*") ? "[" : "", l,
                     family == AF_INET6 && strcmp(l, "*") ? "]" : "", lport ? "" : "*");
        if (fport == 0 && lport != 0)
            snprintf(out + strlen(out), n - strlen(out), "%d", lport);
        if (s->soi_kind == SOCKINFO_TCP) {
            static const char *states[] = { "CLOSED", "LISTEN", "SYN_SENT", "SYN_RECEIVED", "ESTABLISHED", "CLOSE_WAIT",
                                            "FIN_WAIT_1", "CLOSING", "LAST_ACK", "FIN_WAIT_2", "TIME_WAIT" };
            int st = s->soi_proto.pri_tcp.tcpsi_state;
            if (st >= 0 && st < (int)(sizeof(states) / sizeof(*states)))
                snprintf(out + strlen(out), n - strlen(out), " (%s)", states[st]);
        }
        (void)proto;
    } else if (family == AF_UNIX) {
        struct un_sockinfo *un = &s->soi_proto.pri_un;
        *type = "unix";
        if (un->unsi_addr.ua_sun.sun_path[0])
            snprintf(out, n, "%s", un->unsi_addr.ua_sun.sun_path);
        else if (un->unsi_caddr.ua_sun.sun_path[0])
            snprintf(out, n, "%s", un->unsi_caddr.ua_sun.sun_path);
        else if (un->unsi_conn_pcb)
            snprintf(out, n, "->0x%llx", (unsigned long long)un->unsi_conn_pcb);
        else
            snprintf(out, n, "0x%llx", (unsigned long long)s->soi_pcb);
    } else {
        *type = "sock";
        snprintf(out, n, "family %d", family);
    }
}

static int listProcess(pid_t pid) {
    char name[PATH_MAX + 64];
    struct proc_vnodepathinfo vp;

    if (fieldFormat)
        printf("p%d\n", pid);
    else
        printf("COMMAND PID %d\nFD     TYPE   NAME\n", pid);
    if (proc_pidinfo(pid, PROC_PIDVNODEPATHINFO, 0, &vp, sizeof(vp)) == (int)sizeof(vp) && vp.pvi_cdir.vip_path[0])
        emit("cwd", "DIR", vp.pvi_cdir.vip_path);
    if (proc_pidpath(pid, name, sizeof(name)) > 0)
        emit("txt", "REG", name);

    // mapped files (libraries, the shared cache), each once
    {
        uint64_t address = 0;
        char seen[256][PATH_MAX];
        int nseen = 0;
        struct proc_regionwithpathinfo r;

        for (int i = 0; i < 4096; i++) {
            int got = proc_pidinfo(pid, PROC_PIDREGIONPATHINFO, address, &r, sizeof(r));
            if (got != (int)sizeof(r))
                break;
            if (r.prp_vip.vip_path[0]) {
                int dup = 0;
                for (int k = 0; k < nseen && !dup; k++)
                    dup = strcmp(seen[k], r.prp_vip.vip_path) == 0;
                if (!dup && strcmp(r.prp_vip.vip_path, name) != 0) {
                    if (nseen < 256)
                        strcpy(seen[nseen++], r.prp_vip.vip_path);
                    emit("txt", "REG", r.prp_vip.vip_path);
                }
            }
            if (r.prp_prinfo.pri_address + r.prp_prinfo.pri_size <= address)
                break;
            address = r.prp_prinfo.pri_address + r.prp_prinfo.pri_size;
        }
    }

    int size = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, NULL, 0);
    if (size <= 0)
        return size == 0 ? 0 : -1;
    struct proc_fdinfo *fds = malloc(size + 64 * sizeof(struct proc_fdinfo));
    size = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, fds, size + 64 * (int)sizeof(struct proc_fdinfo));
    for (int i = 0; i < size / (int)sizeof(struct proc_fdinfo); i++) {
        char fd[16];
        const char *type = "?";

        snprintf(fd, sizeof(fd), "%d", fds[i].proc_fd);
        name[0] = 0;
        switch (fds[i].proc_fdtype) {
        case PROX_FDTYPE_VNODE: {
            struct vnode_fdinfowithpath v;
            if (pidfdinfo(pid, fds[i].proc_fd, PROC_PIDFDVNODEPATHINFO, &v, sizeof(v)) == (int)sizeof(v)) {
                snprintf(name, sizeof(name), "%s", v.pvip.vip_path);
                int mode = v.pvip.vip_vi.vi_stat.vst_mode & S_IFMT;
                type = mode == S_IFDIR ? "DIR" : mode == S_IFCHR ? "CHR" : mode == S_IFIFO ? "FIFO" : "REG";
            }
            break;
        }
        case PROX_FDTYPE_SOCKET: {
            struct socket_fdinfo s;
            if (pidfdinfo(pid, fds[i].proc_fd, PROC_PIDFDSOCKETINFO, &s, sizeof(s)) == (int)sizeof(s))
                socketName(&s, name, sizeof(name), &type);
            break;
        }
        case PROX_FDTYPE_PIPE: {
            struct pipe_fdinfo p;
            type = "PIPE";
            if (pidfdinfo(pid, fds[i].proc_fd, PROC_PIDFDPIPEINFO, &p, sizeof(p)) == (int)sizeof(p))
                snprintf(name, sizeof(name), "->0x%llx", (unsigned long long)p.pipeinfo.pipe_peerhandle);
            break;
        }
        case PROX_FDTYPE_KQUEUE: {
            struct kqueue_fdinfo k;
            type = "KQUEUE";
            if (pidfdinfo(pid, fds[i].proc_fd, PROC_PIDFDKQUEUEINFO, &k, sizeof(k)) == (int)sizeof(k))
                snprintf(name, sizeof(name), "count=%lld, state=0x%x", (long long)k.kqueueinfo.kq_stat.vst_size, k.kqueueinfo.kq_state);
            break;
        }
        case PROX_FDTYPE_PSEM: type = "PSXSEM"; break;
        case PROX_FDTYPE_PSHM: type = "PSXSHM"; break;
        case PROX_FDTYPE_FSEVENTS: type = "FSEVENT"; break;
        case PROX_FDTYPE_NETPOLICY: type = "NPOLICY"; break;
        case PROX_FDTYPE_CHANNEL: type = "CHAN"; break;
        case PROX_FDTYPE_NEXUS: type = "NEXUS"; break;
        default: break;
        }
        if (!name[0])
            snprintf(name, sizeof(name), "%s", type);
        emit(fd, type, name);
    }
    free(fds);
    return 0;
}

int main(int argc, char **argv) {
    pid_t pids[64];
    int npids = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strncmp(a, "-F", 2) == 0) {
            const char *f = a[2] ? a + 2 : (i + 1 < argc && argv[i + 1][0] != '-' ? argv[++i] : "");
            fieldFormat = 1;
            fieldName = strchr(f, 'n') != NULL || !*f;
            fieldType = strchr(f, 't') != NULL;
        } else if (strncmp(a, "-p", 2) == 0) {
            const char *list = a[2] ? a + 2 : (i + 1 < argc ? argv[++i] : "");
            char *copy = strdup(list), *save = NULL;
            for (char *t = strtok_r(copy, ",", &save); t && npids < 64; t = strtok_r(NULL, ",", &save))
                pids[npids++] = (pid_t)atoi(t);
            free(copy);
        } else if (!strcmp(a, "-n") || !strcmp(a, "-P") || !strcmp(a, "-w") || !strcmp(a, "-l") || !strcmp(a, "-b")) {
            // names are numeric already; warnings are not written
        } else {
            fprintf(stderr, "lsof: only [-F fields] [-nPw] -p pid is implemented here (%s)\n", a);
            return 1;
        }
    }
    if (npids == 0) {
        fprintf(stderr, "usage: lsof [-F fields] [-nPw] -p pid[,pid...]\n");
        return 1;
    }
    int status = 0;
    for (int i = 0; i < npids; i++)
        if (listProcess(pids[i]) != 0) {
            fprintf(stderr, "lsof: process %d: %s\n", pids[i], strerror(errno));
            status = 1;
        }
    return status;
}
