/*
 * sf-wifipm - shows or sets the Wi-Fi power save of SFBwfm (FXNET).
 *
 * Copyright (c) 2026 darwin-lab. SPDX-License-Identifier: ISC
 *
 * sf-wifipm [-i IFNAME]                      what init gives the firmware and what it says now
 * sf-wifipm [-i IFNAME] off                  constantly awake (BWFM_PM_CAM)
 * sf-wifipm [-i IFNAME] ps                   PS-Poll (BWFM_PM_PS)
 * sf-wifipm [-i IFNAME] fast [MS]            fast power save, awake MS ms after the last frame
 *
 * The request is APPLE80211_IOC_POWERSAVE (5) in SiliconForest's own layout, see SFBwfm.cpp. A
 * kernel without it answers EOPNOTSUPP. The setting lasts until the next boot; the boot-args
 * bwfm_pm and bwfm_pm2_ret give the default.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct a80211req {
    char req_if_name[16];
    int req_type;
    int req_val;
    uint32_t req_len;
    void *req_data;
};
struct sf80211_powersave_data {
    uint32_t version;
    uint32_t mode;
    uint32_t pm2_ret_ms;
    int32_t fw_mode;
    int32_t fw_pm2_ret_ms;
};
#define SIOCSA80211 _IOW('i', 200, struct a80211req)
#define SIOCGA80211 _IOWR('i', 201, struct a80211req)
#define A80211_IOC_POWERSAVE 5

static const char *mode_name(int m)
{
    return m == 0 ? "off (constantly awake)" : m == 1 ? "ps (PS-Poll)" : m == 2 ? "fast" : "?";
}

int main(int argc, char **argv)
{
    const char *ifname = "en0";
    struct sf80211_powersave_data ps;
    struct a80211req req;
    int s, set = 0;

    if (argc > 2 && strcmp(argv[1], "-i") == 0) {
        ifname = argv[2];
        argc -= 2;
        argv += 2;
    }
    memset(&ps, 0, sizeof(ps));
    ps.version = 1;
    if (argc > 1) {
        set = 1;
        if (strcmp(argv[1], "off") == 0)
            ps.mode = 0;
        else if (strcmp(argv[1], "ps") == 0)
            ps.mode = 1;
        else if (strcmp(argv[1], "fast") == 0) {
            ps.mode = 2;
            if (argc > 2)
                ps.pm2_ret_ms = (uint32_t)strtoul(argv[2], NULL, 10);
        } else {
            fprintf(stderr, "usage: sf-wifipm [-i IFNAME] [off | ps | fast [MS]]\n");
            return 2;
        }
    }
    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        perror("socket");
        return 1;
    }
    memset(&req, 0, sizeof(req));
    strlcpy(req.req_if_name, ifname, sizeof(req.req_if_name));
    req.req_type = A80211_IOC_POWERSAVE;
    req.req_data = &ps;
    req.req_len = set ? 3 * sizeof(uint32_t) : sizeof(ps);
    if (set && ioctl(s, SIOCSA80211, &req) < 0) {
        fprintf(stderr, "sf-wifipm: set on %s: %s\n", ifname, strerror(errno));
        return 1;
    }
    memset(&ps, 0, sizeof(ps));
    req.req_len = sizeof(ps);
    if (ioctl(s, SIOCGA80211, &req) < 0) {
        fprintf(stderr, "sf-wifipm: get on %s: %s\n", ifname, strerror(errno));
        return 1;
    }
    close(s);
    printf("interface: %s\n", ifname);
    printf("init:      %s, pm2_sleep_ret %u ms\n", mode_name((int)ps.mode), ps.pm2_ret_ms);
    if (ps.fw_mode < 0)
        printf("firmware:  not running\n");
    else
        printf("firmware:  %s, pm2_sleep_ret %d ms\n", mode_name(ps.fw_mode), ps.fw_pm2_ret_ms);
    return 0;
}
