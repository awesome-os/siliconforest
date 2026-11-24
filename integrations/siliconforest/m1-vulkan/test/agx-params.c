// SPDX-License-Identifier: MIT
// agx-params: DRM_IOCTL_ASAHI_GET_PARAMS (group 0) of the asahi render node, every field, and the
// feature bits by name. Read-only: no buffer, no VM, no submission.
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <xf86drm.h>
#include "drm-uapi/asahi_drm.h"

static const char *feat[64] = {
	[0] = "SOFT_FAULTS", [1] = "VM_STATUS", [2] = "INDEPENDENT_QUEUES", [3] = "SCHEDULED_QUEUES",
	[4] = "RENDER_TIMESTAMP_COPIES", [5] = "COMPUTE_TIMESTAMP_COPIES", [6] = "FRAGMENT_DEPENDENCY",
	[7] = "FRAGMENT_DEPENDENCY_COMPUTE", [8] = "COMPUTE_WIDE_VISIBILITY", [9] = "CACHED_PROGRAM_PROBE",
	[10] = "PER_COMMAND_INPUT_SYNCS", [11] = "VM_STATUS_MIRROR", [12] = "EXACT_PRIOR_BARRIERS",
	[13] = "FEW_PRIMITIVES", [14] = "FRAGMENT_BARRIERS", [15] = "RENDER_RAY_QUERY",
	[16] = "BOUNDED_AS_BUILD", [17] = "RENDER_RESOURCES", [18] = "RENDER_RESOURCES_V2",
	[19] = "SPARSE_TILES", [20] = "COMPUTE_MATRIX", [21] = "TASK_MESH_STORAGE",
	[22] = "RENDER_SHADER_WRITES", [23] = "M5_DEPTH_MSAA_COMPRESSION", [24] = "COMPUTE_STAGE_BARRIERS",
	[26] = "M5_STENCIL_MSAA_COMPRESSION", [28] = "COMPUTE_RESOURCES", [29] = "CUMULATIVE_SIGNALS",
};

int main(void)
{
	for (int i = 128; i < 136; i++) {
		char p[32];
		snprintf(p, sizeof p, "/dev/dri/renderD%d", i);
		int fd = open(p, O_RDWR | O_CLOEXEC);
		if (fd < 0)
			continue;
		drmVersionPtr v = drmGetVersion(fd);
		int asahi = v && !strcmp(v->name, "asahi");
		if (v)
			drmFreeVersion(v);
		if (!asahi) {
			close(fd);
			continue;
		}
		struct drm_asahi_params_global g;
		memset(&g, 0, sizeof g);
		struct drm_asahi_get_params gp = { .param_group = 0, .pointer = (uint64_t)(uintptr_t)&g, .size = sizeof g };
		int r = drmIoctl(fd, DRM_IOCTL_ASAHI_GET_PARAMS, &gp);
		printf("PARAMS node=%s rc=%d size_asked=%zu size_back=%llu\n", p, r, sizeof g, (unsigned long long)gp.size);
		printf("PARAMS features=0x%016llx gpu_generation=%u gpu_variant=%u(%c) gpu_revision=0x%x chip_id=0x%x\n",
		       (unsigned long long)g.features, g.gpu_generation, g.gpu_variant, g.gpu_variant, g.gpu_revision, g.chip_id);
		printf("PARAMS num_dies=%u num_clusters_total=%u num_cores_per_cluster=%u max_frequency_khz=%u core_masks[0]=0x%llx\n",
		       g.num_dies, g.num_clusters_total, g.num_cores_per_cluster, g.max_frequency_khz,
		       (unsigned long long)g.core_masks[0]);
		printf("PARAMS vm_start=0x%llx vm_end=0x%llx vm_kernel_min_size=0x%llx max_commands_per_submission=%u max_attachments=%u\n",
		       (unsigned long long)g.vm_start, (unsigned long long)g.vm_end, (unsigned long long)g.vm_kernel_min_size,
		       g.max_commands_per_submission, g.max_attachments);
		printf("PARAMS command_timestamp_frequency_hz=%llu usc_generation=%u gpu_hal_generation=%u memory_limit=%llu client_memory_limit=%llu\n",
		       (unsigned long long)g.command_timestamp_frequency_hz, g.usc_generation, g.gpu_hal_generation,
		       (unsigned long long)g.memory_limit, (unsigned long long)g.client_memory_limit);
		for (int b = 0; b < 64; b++)
			if (g.features & (1ull << b))
				printf("PARAMS feature bit %d %s\n", b, feat[b] ? feat[b] : "(unnamed)");
		close(fd);
		return r ? 1 : 0;
	}
	printf("PARAMS no asahi render node\n");
	return 2;
}
