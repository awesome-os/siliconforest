// SPDX-License-Identifier: MIT
// Read access to the kernel model's state for the host harness (the struct is public
// in sf_speaker_model.h; this only saves mirroring its layout in Rust).
#include <stddef.h>
#include "../kernel/sf_speaker_model.h"

size_t sfshim_size(void) { return sizeof(struct sf_spk_model); }
int sfshim_fault(const struct sf_spk_model *m) { return m->fault; }
unsigned sfshim_fault_speaker(const struct sf_spk_model *m) { return m->fault_speaker; }
/* degrees, Q23.40 */
int64_t sfshim_coil(const struct sf_spk_model *m, unsigned i) { return m->spk[i].base_coil + m->spk[i].heat_coil; }
int64_t sfshim_magnet(const struct sf_spk_model *m, unsigned i) { return m->spk[i].base_magnet + m->spk[i].heat_magnet; }
int64_t sfshim_coil_hyst(const struct sf_spk_model *m, unsigned i) { return m->spk[i].t_coil_hyst; }
int64_t sfshim_magnet_hyst(const struct sf_spk_model *m, unsigned i) { return m->spk[i].t_magnet_hyst; }
int64_t sfshim_alpha_coil(const struct sf_spk_model *m) { return m->alpha_coil; }
int64_t sfshim_alpha_magnet(const struct sf_spk_model *m) { return m->alpha_magnet; }
