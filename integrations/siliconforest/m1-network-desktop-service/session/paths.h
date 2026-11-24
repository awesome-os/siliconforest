#ifndef DNDS_PATHS_H
#define DNDS_PATHS_H
#include <stdint.h>
struct dnds_paths {
 int initialized,error,fd[6];
 char session[57];
};
/* Fresh zeroed state, after process-exclusivity preflight. Creates only the
 * new private base/session dirs. Existing paths refuse; nothing is removed.
 * Every acquired FD remains owned on failure until close(). */
int dnds_paths_create(struct dnds_paths *,const char nonce[49]);
int dnds_paths_verify(struct dnds_paths *);
int dnds_paths_close(struct dnds_paths *);
#endif
