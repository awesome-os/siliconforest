/* SPDX-License-Identifier: MIT */
/* Port-1 physical ownership metadata. The IOService owner serializes all
 * calls and supplies actual mode/drain/stop operations; tokens prove none
 * of those hardware facts. An error retains ownership until real cleanup. */
#ifndef SF_USBC_PHY_LEASE_H
#define SF_USBC_PHY_LEASE_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <errno.h>
#ifdef __cplusplus
extern "C" {
#endif
enum sf_usbc_phy_mode { SF_USBC_PHY_NONE,SF_USBC_PHY_DP,SF_USBC_PHY_TBT,SF_USBC_PHY_USB4 };
enum sf_usbc_phy_phase { SF_USBC_PHY_FREE,SF_USBC_PHY_PREPARING,SF_USBC_PHY_LIVE,SF_USBC_PHY_REVOKED };
#define SF_USBC_PHY_MODE_BIT(mode) (1u<<(unsigned)(mode))
struct sf_usbc_phy_lease {
 void *owner;
 uint64_t epoch,serial,token;
 uint64_t cable_key;
 uint32_t eligible;
 enum sf_usbc_phy_mode mode;
 enum sf_usbc_phy_phase phase;
 bool have_cable,reversed,quarantined;
};
/* Invalid samples retain the preceding lease and epoch, but cannot admit a
 * new claimant. HPD changes do not change the cable key (the native owner
 * excludes HPD); unplug/mode/orientation changes revoke any current lease. */
static inline int sf_usbc_phy_observe(struct sf_usbc_phy_lease *s,uint64_t key,uint32_t eligible,bool reversed)
{
 if(!s)return EINVAL;
 if(!s->have_cable || s->cable_key!=key || s->eligible!=eligible || s->reversed!=reversed) {
  if(s->epoch==UINT64_MAX){s->quarantined=true;if(s->owner)s->phase=SF_USBC_PHY_REVOKED;return EOVERFLOW;}
  ++s->epoch;s->cable_key=key;s->eligible=eligible;s->reversed=reversed;s->have_cable=true;
  if(s->owner)s->phase=SF_USBC_PHY_REVOKED;
 }
 return 0;
}
static inline bool sf_usbc_phy_identity(const struct sf_usbc_phy_lease *s,void *owner,uint64_t token)
{ return s && owner && token && s->owner==owner && s->token==token; }
static inline int sf_usbc_phy_claim(struct sf_usbc_phy_lease *s,void *owner,uint64_t epoch,enum sf_usbc_phy_mode mode,uint64_t *token)
{
 if(!s || !owner || !token || mode<SF_USBC_PHY_DP || mode>SF_USBC_PHY_USB4)return EINVAL;
 if(s->quarantined)return EIO;
 if(s->owner)return EBUSY;
 if(!s->have_cable || epoch!=s->epoch)return ESTALE;
 if(!(s->eligible&SF_USBC_PHY_MODE_BIT(mode)))return ENODEV;
 if(s->serial==UINT64_MAX){s->quarantined=true;return EOVERFLOW;}
 s->owner=owner;s->mode=mode;s->token=++s->serial;s->phase=SF_USBC_PHY_PREPARING;
 *token=s->token;return 0;
}
static inline int sf_usbc_phy_commit(struct sf_usbc_phy_lease *s,void *owner,uint64_t token,uint64_t epoch)
{
 if(!sf_usbc_phy_identity(s,owner,token))return ESTALE;
 if(s->quarantined)return EIO;
 if(s->phase!=SF_USBC_PHY_PREPARING || epoch!=s->epoch)return ESTALE;
 s->phase=SF_USBC_PHY_LIVE;return 0;
}
static inline int sf_usbc_phy_live(const struct sf_usbc_phy_lease *s,void *owner,uint64_t token)
{
 if(!sf_usbc_phy_identity(s,owner,token))return ESTALE;
 if(s->quarantined)return EIO;
 return s->phase==SF_USBC_PHY_LIVE?0:EBUSY;
}
/* Called only AFTER the native owner has checked real client drain and PHY
 * stop. A failure leaves the lease intact; quarantine permanently prohibits
 * another mode even if a subsequent cleanup succeeds. */
static inline int sf_usbc_phy_release(struct sf_usbc_phy_lease *s,void *owner,uint64_t token,int stop_error,bool quarantine)
{
 if(!sf_usbc_phy_identity(s,owner,token))return ESTALE;
 if(quarantine)s->quarantined=true;
 if(stop_error){s->phase=SF_USBC_PHY_REVOKED;return stop_error;}
 s->owner=NULL;s->token=0;s->mode=SF_USBC_PHY_NONE;s->phase=SF_USBC_PHY_FREE;return 0;
}
#define SF_USBC_PHY_ABI 1
#define SF_USBC_PHY_FUNCTION "SFAppleUSBCPhy"
#define SF_USBC_PHY_REVOKE_FUNCTION "SFAppleUSBCRevoke"
#define SF_USBC_PHY_DRAIN_FUNCTION "SFAppleUSBCDrainStatus"
enum sf_usbc_phy_command { SF_USBC_PHY_QUERY,SF_USBC_PHY_ACQUIRE,SF_USBC_PHY_RELEASE,SF_USBC_PHY_DP_RATE,SF_USBC_PHY_TUNNEL_RATE,SF_USBC_PHY_SNAPSHOT };
/* Kernel-only callPlatformFunction ABI; not an IOUserClient/userspace ABI.
 * owner is a retained IOService while claimed. QUERY returns epoch/eligible/
 * reversed. ACQUIRE requires that epoch, prepares PHY and returns token.
 * RELEASE requires a real, NONBLOCKING DrainStatus check from that owner:
 * it must have already stopped/joined controller, DMA, IRQ and display users.
 * Revoke is delivered asynchronously outside the PHY gate, never a drain
 * ACK. Clients copy its token and discard stale notifications. No client may
 * hold a lock needed by its DrainStatus handler across a broker call.
 * Both IOReturn and error must indicate success before using hardware. */
/* SNAPSHOT requires the live owner/token and publishes bounded read-only
 * PHY/PIPE register state on the provider. It never changes mode or rate. */
struct sf_usbc_phy_request {
 uint32_t version,size,command,port;
 void *owner;
 uint64_t epoch,token;
 uint32_t mode,rate,eligible,phase;
 int32_t error;
 bool reversed,quarantined;
};
#ifdef __cplusplus
}
#endif
#endif
