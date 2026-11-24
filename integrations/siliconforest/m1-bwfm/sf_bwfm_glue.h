// SPDX-License-Identifier: MIT
// bwfm(4) as SFBwfm.cpp sees it, plain C so the c++ side never includes the openbsd headers
#ifndef SF_BWFM_GLUE_H
#define SF_BWFM_GLUE_H

#ifdef __cplusplus
extern "C" {
#endif

struct device;
struct ieee80211com;
struct pci_attach_args;

struct device *sf_bwfm_alloc(const char *);
void	sf_bwfm_free(struct device *);
int	sf_bwfm_match(struct pci_attach_args *);
int	sf_bwfm_attach(struct device *, struct pci_attach_args *);
int	sf_bwfm_attach_hook(struct device *);
int	sf_bwfm_running(struct device *);
const char *sf_bwfm_ctlwhy(struct device *);
int	sf_bwfm_down(struct device *, int inform);
int	sf_bwfm_up(struct device *);
void	sf_bwfm_set_gone(struct device *, int gone);
int	sf_bwfm_dma_resume(void);
void	sf_bwfm_unhook(struct device *);
int	sf_bwfm_detach(struct device *);
struct ieee80211com *sf_bwfm_ic(struct device *);
/* FXNET: power save at run time, see sf_bwfm_glue.c */
int	sf_bwfm_set_pm(struct device *, int mode, int ret_ms);
int	sf_bwfm_get_pm(struct device *, int *fw_mode, int *fw_ret_ms, int *want_mode, int *want_ret_ms);

#ifdef __cplusplus
}
#endif

#endif
