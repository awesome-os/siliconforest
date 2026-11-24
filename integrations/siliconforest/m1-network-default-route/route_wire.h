#ifndef DLF_ROUTE_WIRE_H
#define DLF_ROUTE_WIRE_H
#include "default_route.h"
/* GET=4 and ADD=1 only. No DELETE encoder is provided. Outputs unchanged on error. */
int dlf_encode(uint8_t *,size_t,size_t *,uint32_t pid,uint8_t type,const struct dlf_owner *);
/* 1=uncorrelated broadcast; -1=malformed; 0=correlated (including errno reply). */
int dlf_decode(const uint8_t *,size_t,uint32_t pid,uint8_t type,struct dlf_route *);
#endif
