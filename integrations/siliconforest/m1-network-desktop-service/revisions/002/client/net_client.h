#ifndef DNDS_NET_CLIENT_H
#define DNDS_NET_CLIENT_H
#include "egress_validate.h"
#include <stddef.h>
#include <stdint.h>
#define DN_HTTP_CAP 2048u
#define DN_JSON_CAP 16384u
#define DN_ADDRESS_CAP 32u
#define DN_RECORD_PREFIX "/private/var/run/darwin-desktop-network/session-"
#define DN_PROFILE "desktop-native-resolver-two-http-v1"
struct dn_identity {
  char nonce[49], instance[49];
  unsigned slot;
};
struct dn_result {
  struct dn_identity identity;
  int32_t pid, ppid, sid, pgid, foreground_pgid;
  const char *error;
  int error_errno, gai_rc, system_errno, address_list_complete;
  uint8_t addresses[DN_ADDRESS_CAP][4], local[4], peer[4];
  unsigned address_count;
  unsigned local_port, peer_port;
  int entry_fd_checked;
  uint32_t entry_fd_bound;
  int tcp_maxseg_observed, request_shutdown, peer_eof, socket_created,
      socket_closed;
  uint8_t request[EG_HTTP_REQUEST_BYTES], response[DN_HTTP_CAP];
  size_t request_bytes, response_bytes;
  struct eg_http_facts http;
  uint64_t started_ms, elapsed_ms;
};
int dn_hex48(const char *);
int dn_identity_parse(const char *records, const char *instance,
                      struct dn_identity *);
int dn_result_format(const struct dn_result *, char *, size_t, size_t *);
#endif
