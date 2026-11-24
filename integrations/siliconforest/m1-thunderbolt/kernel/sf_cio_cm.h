/* SPDX-License-Identifier: MIT */
#ifndef SF_CIO_CM_H
#define SF_CIO_CM_H
#include <stdint.h>
#include <IOKit/IOService.h>
/* Native IORegistry/PM/DART owner for the private Linux CM. One pinned object
 * per controller per boot. Stop failure retains every resource and PHY lease.
 * This API accepts only already-validated mapped ACIO1 resources. */
struct SFCIOCM;
SFCIOCM *sf_cio_cm_create(IOService *owner,void *power_ctx,bool (*powered)(void *));
int sf_cio_cm_start(SFCIOCM *,void *nhi,void *pdf,uint32_t cable);
int sf_cio_cm_stop(SFCIOCM *);
int sf_cio_cm_snapshot(SFCIOCM *); // one read-only actual PHY/link query
int sf_cio_cm_authorize_ts3(SFCIOCM *); // explicit private enum profile only
#ifdef SF_CIO_ENDPOINT_MAPPER
int sf_cio_cm_prepare_i210_dma(SFCIOCM *); // Actual mapper; BME remains stripped.
#if SF_CIO_DOCK_USB
int sf_cio_cm_prepare_dock_usb(SFCIOCM *); // Explicit FL1100 binding/matching.
#endif
#endif
int sf_cio_cm_can_retrain(SFCIOCM *); // Actual two unplugged lanes, pristine PCI owner.
#endif
