#ifndef DNDS_PROOF_INTERNAL_H
#define DNDS_PROOF_INTERNAL_H
#include "desktop_proof.h"
#include "../record_guard/record_guard.h"
#include "../session/revisions/002/session_text.h"
struct proof_app { uint64_t rx,tx,fd_bound; uint8_t peer[4]; uint16_t port; };
int dnds_manager_proof(const uint8_t *,size_t,const uint8_t *,size_t,const char *,const struct dnds_session_summary *);
#endif
