// SPDX-License-Identifier: MIT
// vk-tri: a headless Vulkan triangle with readback, checked against a reference computed on the
// CPU in this program (and, separately, against lavapipe and the M1's Linux).
//
// One 256x256 RGBA8 image, cleared to (0, 0, 64, 255), then two triangles:
//   A, flat colour (255, 64, 0, 255): its pixels must equal the CPU reference EXACTLY. The
//      vertices are on a 1/4-pixel grid, so every implementation snaps them to the same place
//      (Vulkan requires at least 4 sub-pixel bits), and the program checks that no pixel centre
//      lies on an edge (so no tie-break rule matters).
//   B, colours interpolated between red, green and blue: compared with a tolerance (the float
//      interpolation and rounding differ between implementations); its coverage must be exact.
// The hash `flat_fnv` is FNV-1a over the whole image with B's pixels set to zero: equal on every
// conformant implementation. `full_fnv` (B's pixels included) is reported for comparison only.
//
//   vk-tri [frames]        frames (default 100): timed submit+wait of the same command buffer
//   VKTRI_DEVICE=<substr>  choose the physical device by name (default: the first)
//   VKTRI_PPM=<file>       also write the image
//   VKTRI_MODE=copy        (default) optimal-tiled target, read back with vkCmdCopyImageToBuffer
//                          (on Honeykrisp the copy is a compute dispatch)
//   VKTRI_MODE=linear      linear host-visible target read directly: render work only
//   VKTRI_MODE=fill        no triangle: vkCmdFillBuffer of a host-visible buffer (compute only)
//   VKTRI_TIMEOUT_MS=<ms>  fence timeout (default 5000)
//   VKTRI_HOLD_S=<s>       after a failure, stay alive s seconds before exiting
//   VKTRI_EXIT_AFTER_SUBMIT=<n>  submit the frame n times without waiting, then _exit(0): a
//                          process dying with GPU work in flight (kernel teardown test)
//   VKTRI_CLOSE_AFTER_SUBMIT=<n>  the same, but close the /dev/dri files explicitly and stay
//                          alive VKTRI_HOLD_S seconds (Darwin: F_GETPATH finds them)
// Last line: "VKTRI PASS" or "VKTRI FAIL <reason>"; exit status 0 only for PASS.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <vulkan/vulkan.h>

#include "tri_vert.h"
#include "tri_frag.h"

#define W 256
#define H 256

static void fail(const char *what, VkResult r)
{
	printf("VKTRI FAIL %s (VkResult %d)\n", what, r);
	// VKTRI_HOLD_S=<s>: stay alive that long before exiting (the queue and its scheduler
	// with it), so what the kernel does with an unfinished job can be watched in its log
	const char *hold = getenv("VKTRI_HOLD_S");
	if (hold && atoi(hold) > 0) {
		printf("VKTRI holding %d s before exit\n", atoi(hold));
		fflush(stdout);
		sleep(atoi(hold));
	}
	exit(1);
}
#define CK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) fail(#x, r_); } while (0)
static uint64_t timeout_ns = 5000000000ull;

// pixel-space vertices (must match tri.vert, which has them in NDC = p / 128 - 1)
static const double A[3][2] = { { 20.25, 30.25 }, { 200.75, 60.5 }, { 90.5, 230.25 } };
static const double B[3][2] = { { 180.25, 170.25 }, { 250.5, 200.75 }, { 220.75, 250.25 } };
static const uint8_t CLEAR[4] = { 0, 0, 64, 255 };
static const uint8_t AFLAT[4] = { 255, 64, 0, 255 };

static double edge(const double *a, const double *b, double x, double y)
{
	return (b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0]);
}

// inside test at a pixel centre; *tie set when the centre is on an edge; w = barycentrics
static int inside(const double t[3][2], double x, double y, int *tie, double w[3])
{
	double area = edge(t[0], t[1], t[2][0], t[2][1]);
	double e0 = edge(t[1], t[2], x, y), e1 = edge(t[2], t[0], x, y), e2 = edge(t[0], t[1], x, y);
	if (area < 0) { e0 = -e0; e1 = -e1; e2 = -e2; area = -area; }
	if (e0 == 0 || e1 == 0 || e2 == 0)
		*tie = 1;
	w[0] = e0 / area; w[1] = e1 / area; w[2] = e2 / area;
	return e0 > 0 && e1 > 0 && e2 > 0;
}

static uint32_t fnv(const uint8_t *p, size_t n)
{
	uint32_t h = 0x811c9dc5u;
	for (size_t i = 0; i < n; i++)
		h = (h ^ p[i]) * 0x01000193u;
	return h;
}

static double now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static uint32_t mem_type(VkPhysicalDevice pd, uint32_t bits, VkMemoryPropertyFlags want)
{
	VkPhysicalDeviceMemoryProperties mp;
	vkGetPhysicalDeviceMemoryProperties(pd, &mp);
	for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
		if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want)
			return i;
	fail("no memory type", VK_ERROR_UNKNOWN);
	return 0;
}

// VKTRI_MODE=fill: one vkCmdFillBuffer into a host-visible buffer, the fence, the contents
static int run_fill(VkDevice dev, VkPhysicalDevice pd, VkQueue q, uint32_t qf)
{
	const VkDeviceSize size = 65536;
	VkBufferCreateInfo bci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size,
		.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT };
	VkBuffer buf;
	CK(vkCreateBuffer(dev, &bci, NULL, &buf));
	VkMemoryRequirements mr;
	vkGetBufferMemoryRequirements(dev, buf, &mr);
	VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
		.memoryTypeIndex = mem_type(pd, mr.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) };
	VkDeviceMemory mem;
	CK(vkAllocateMemory(dev, &mai, NULL, &mem));
	CK(vkBindBufferMemory(dev, buf, mem, 0));
	uint32_t *p;
	CK(vkMapMemory(dev, mem, 0, size, 0, (void **)&p));
	memset(p, 0, size);
	VkCommandPoolCreateInfo cpci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = qf };
	VkCommandPool pool;
	CK(vkCreateCommandPool(dev, &cpci, NULL, &pool));
	VkCommandBufferAllocateInfo cbai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
	VkCommandBuffer cmd;
	CK(vkAllocateCommandBuffers(dev, &cbai, &cmd));
	VkCommandBufferBeginInfo cbbi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	CK(vkBeginCommandBuffer(cmd, &cbbi));
	vkCmdFillBuffer(cmd, buf, 0, size, 0xa5c3e1f0u);
	VkMemoryBarrier mb = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_HOST_READ_BIT };
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
	CK(vkEndCommandBuffer(cmd));
	VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	VkFence fence;
	CK(vkCreateFence(dev, &fci, NULL, &fence));
	VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd };
	double t0 = now_ms();
	printf("VKTRI submit\n");
	fflush(stdout);
	CK(vkQueueSubmit(q, 1, &si, fence));
	CK(vkWaitForFences(dev, 1, &fence, VK_TRUE, timeout_ns));
	printf("VKTRI fence signalled after %.2f ms\n", now_ms() - t0);
	unsigned bad = 0;
	for (unsigned i = 0; i < size / 4; i++)
		if (p[i] != 0xa5c3e1f0u)
			bad++;
	vkDeviceWaitIdle(dev);
	vkDestroyDevice(dev, NULL);
	if (bad) {
		printf("VKTRI FAIL fill: %u of %u words wrong\n", bad, (unsigned)(size / 4));
		return 1;
	}
	printf("VKTRI PASS fill %u words\n", (unsigned)(size / 4));
	return 0;
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	int frames = argc > 1 ? atoi(argv[1]) : 100;
	double t0 = now_ms();

	VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "vk-tri", .apiVersion = VK_API_VERSION_1_3 };
	VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &app };
	VkInstance inst;
	CK(vkCreateInstance(&ici, NULL, &inst));

	uint32_t n = 0;
	CK(vkEnumeratePhysicalDevices(inst, &n, NULL));
	if (n == 0)
		fail("no physical device", VK_ERROR_INITIALIZATION_FAILED);
	VkPhysicalDevice pds[8];
	if (n > 8) n = 8;
	CK(vkEnumeratePhysicalDevices(inst, &n, pds));
	const char *want = getenv("VKTRI_DEVICE");
	VkPhysicalDevice pd = VK_NULL_HANDLE;
	for (uint32_t i = 0; i < n; i++) {
		VkPhysicalDeviceDriverProperties drv = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
		VkPhysicalDeviceProperties2 p2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &drv };
		vkGetPhysicalDeviceProperties2(pds[i], &p2);
		printf("VKTRI device[%u] name=\"%s\" api=%u.%u.%u driver=\"%s\" info=\"%s\" conformance=%u.%u.%u.%u\n", i,
		       p2.properties.deviceName, VK_API_VERSION_MAJOR(p2.properties.apiVersion),
		       VK_API_VERSION_MINOR(p2.properties.apiVersion), VK_API_VERSION_PATCH(p2.properties.apiVersion),
		       drv.driverName, drv.driverInfo, drv.conformanceVersion.major, drv.conformanceVersion.minor,
		       drv.conformanceVersion.subminor, drv.conformanceVersion.patch);
		if (pd == VK_NULL_HANDLE && (!want || strstr(p2.properties.deviceName, want)))
			pd = pds[i];
	}
	if (pd == VK_NULL_HANDLE)
		fail("VKTRI_DEVICE matches no device", VK_ERROR_INITIALIZATION_FAILED);

	uint32_t nq = 0, qf = UINT32_MAX;
	vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, NULL);
	VkQueueFamilyProperties qfp[8];
	if (nq > 8) nq = 8;
	vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, qfp);
	for (uint32_t i = 0; i < nq && qf == UINT32_MAX; i++)
		if (qfp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
			qf = i;
	if (qf == UINT32_MAX)
		fail("no graphics queue", VK_ERROR_INITIALIZATION_FAILED);

	float prio = 1.0f;
	VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = qf, .queueCount = 1, .pQueuePriorities = &prio };
	VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.queueCreateInfoCount = 1, .pQueueCreateInfos = &qci };
	VkDevice dev;
	CK(vkCreateDevice(pd, &dci, NULL, &dev));
	VkQueue q;
	vkGetDeviceQueue(dev, qf, 0, &q);
	double t_dev = now_ms();
	const char *mode = getenv("VKTRI_MODE") ? getenv("VKTRI_MODE") : "copy";
	const int linear = !strcmp(mode, "linear"), fillmode = !strcmp(mode, "fill");
	if (getenv("VKTRI_TIMEOUT_MS"))
		timeout_ns = strtoull(getenv("VKTRI_TIMEOUT_MS"), NULL, 10) * 1000000ull;
	printf("VKTRI mode=%s timeout_ms=%llu\n", mode, (unsigned long long)(timeout_ns / 1000000));
	if (fillmode)
		return run_fill(dev, pd, q, qf);

	// colour target
	VkImageCreateInfo imci = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
		.format = VK_FORMAT_R8G8B8A8_UNORM, .extent = { W, H, 1 }, .mipLevels = 1, .arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT, .tiling = linear ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (linear ? 0 : VK_IMAGE_USAGE_TRANSFER_SRC_BIT) };
	if (linear) {
		VkFormatProperties fp;
		vkGetPhysicalDeviceFormatProperties(pd, VK_FORMAT_R8G8B8A8_UNORM, &fp);
		if (!(fp.linearTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT))
			fail("linear RGBA8 is not a colour attachment here", VK_ERROR_FORMAT_NOT_SUPPORTED);
	}
	VkImage img;
	CK(vkCreateImage(dev, &imci, NULL, &img));
	VkMemoryRequirements mr;
	vkGetImageMemoryRequirements(dev, img, &mr);
	VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
		.memoryTypeIndex = mem_type(pd, mr.memoryTypeBits, linear ?
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT :
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
	VkDeviceMemory imem;
	CK(vkAllocateMemory(dev, &mai, NULL, &imem));
	CK(vkBindImageMemory(dev, img, imem, 0));
	VkImageViewCreateInfo ivci = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = img,
		.viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
		.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
	VkImageView view;
	CK(vkCreateImageView(dev, &ivci, NULL, &view));

	// readback buffer
	VkBufferCreateInfo bci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = W * H * 4,
		.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT };
	VkBuffer buf;
	CK(vkCreateBuffer(dev, &bci, NULL, &buf));
	vkGetBufferMemoryRequirements(dev, buf, &mr);
	mai.allocationSize = mr.size;
	mai.memoryTypeIndex = mem_type(pd, mr.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VkDeviceMemory bmem;
	CK(vkAllocateMemory(dev, &mai, NULL, &bmem));
	CK(vkBindBufferMemory(dev, buf, bmem, 0));

	// render pass, framebuffer, pipeline
	VkAttachmentDescription att = { .format = VK_FORMAT_R8G8B8A8_UNORM, .samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = linear ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL };
	VkAttachmentReference aref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	VkSubpassDescription sub = { .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1, .pColorAttachments = &aref };
	VkSubpassDependency dep = { .srcSubpass = 0, .dstSubpass = VK_SUBPASS_EXTERNAL,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = linear ? VK_PIPELINE_STAGE_HOST_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT,
		.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.dstAccessMask = linear ? VK_ACCESS_HOST_READ_BIT : VK_ACCESS_TRANSFER_READ_BIT };
	VkRenderPassCreateInfo rpci = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1,
		.pAttachments = &att, .subpassCount = 1, .pSubpasses = &sub, .dependencyCount = 1, .pDependencies = &dep };
	VkRenderPass rp;
	CK(vkCreateRenderPass(dev, &rpci, NULL, &rp));
	VkFramebufferCreateInfo fbci = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = rp,
		.attachmentCount = 1, .pAttachments = &view, .width = W, .height = H, .layers = 1 };
	VkFramebuffer fb;
	CK(vkCreateFramebuffer(dev, &fbci, NULL, &fb));

	VkShaderModuleCreateInfo smci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = sizeof(tri_vert), .pCode = tri_vert };
	VkShaderModule vs, fs;
	CK(vkCreateShaderModule(dev, &smci, NULL, &vs));
	smci.codeSize = sizeof(tri_frag);
	smci.pCode = tri_frag;
	CK(vkCreateShaderModule(dev, &smci, NULL, &fs));
	VkPipelineShaderStageCreateInfo st[2] = {
		{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT,
		  .module = vs, .pName = "main" },
		{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		  .module = fs, .pName = "main" } };
	VkPipelineVertexInputStateCreateInfo vi = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	VkPipelineInputAssemblyStateCreateInfo ia = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
	VkViewport vp = { 0, 0, W, H, 0, 1 };
	VkRect2D sc = { { 0, 0 }, { W, H } };
	VkPipelineViewportStateCreateInfo vps = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1, .pViewports = &vp, .scissorCount = 1, .pScissors = &sc };
	VkPipelineRasterizationStateCreateInfo rs = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_CLOCKWISE,
		.lineWidth = 1.0f };
	VkPipelineMultisampleStateCreateInfo ms = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
	VkPipelineColorBlendAttachmentState cba = { .colorWriteMask = 0xf };
	VkPipelineColorBlendStateCreateInfo cb = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1, .pAttachments = &cba };
	VkPipelineLayoutCreateInfo plci = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	VkPipelineLayout pl;
	CK(vkCreatePipelineLayout(dev, &plci, NULL, &pl));
	VkGraphicsPipelineCreateInfo gpci = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = 2, .pStages = st, .pVertexInputState = &vi, .pInputAssemblyState = &ia,
		.pViewportState = &vps, .pRasterizationState = &rs, .pMultisampleState = &ms,
		.pColorBlendState = &cb, .layout = pl, .renderPass = rp };
	VkPipeline pipe;
	double t_p0 = now_ms();
	CK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipe));
	double t_pipe = now_ms() - t_p0;

	// command buffer: clear, two triangles, copy to the buffer
	VkCommandPoolCreateInfo cpci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = qf };
	VkCommandPool pool;
	CK(vkCreateCommandPool(dev, &cpci, NULL, &pool));
	VkCommandBufferAllocateInfo cbai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
	VkCommandBuffer cmd;
	CK(vkAllocateCommandBuffers(dev, &cbai, &cmd));
	VkCommandBufferBeginInfo cbbi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	CK(vkBeginCommandBuffer(cmd, &cbbi));
	VkClearValue clear = { .color = { .float32 = { CLEAR[0] / 255.0f, CLEAR[1] / 255.0f, CLEAR[2] / 255.0f, 1.0f } } };
	VkRenderPassBeginInfo rpbi = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = rp,
		.framebuffer = fb, .renderArea = sc, .clearValueCount = 1, .pClearValues = &clear };
	vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
	vkCmdDraw(cmd, 6, 1, 0, 0);
	vkCmdEndRenderPass(cmd);
	if (!linear) {
		VkBufferImageCopy reg = { .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, .imageExtent = { W, H, 1 } };
		vkCmdCopyImageToBuffer(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &reg);
		VkMemoryBarrier mb = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_HOST_READ_BIT };
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
	}
	CK(vkEndCommandBuffer(cmd));

	VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	VkFence fence;
	CK(vkCreateFence(dev, &fci, NULL, &fence));
	VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd };
	double t_s0 = now_ms();
	const char *dying = getenv("VKTRI_EXIT_AFTER_SUBMIT");
	if (dying && atoi(dying) > 0) {
		// a process that dies with GPU work queued: N submits, no wait, _exit
		// (the kernel must survive the teardown; this is the bench reproduction of 14 Nov)
		printf("VKTRI submitting %d times and exiting without waiting\n", atoi(dying));
		fflush(stdout);
		for (int i = 0; i < atoi(dying); i++)
			CK(vkQueueSubmit(q, 1, &si, VK_NULL_HANDLE));
		_exit(0);
	}
	const char *closing = getenv("VKTRI_CLOSE_AFTER_SUBMIT");
	if (closing && atoi(closing) > 0) {
		// the same, but the DRM device files are closed explicitly while the process lives on
		printf("VKTRI submitting %d times, then closing the DRM files without waiting\n", atoi(closing));
		fflush(stdout);
		for (int i = 0; i < atoi(closing); i++)
			CK(vkQueueSubmit(q, 1, &si, VK_NULL_HANDLE));
#ifdef F_GETPATH
		for (int f = 3; f < 256; f++) {
			char path[1024];
			if (fcntl(f, F_GETPATH, path) == 0 && !strncmp(path, "/dev/dri/", 9)) {
				printf("VKTRI close(%d) %s\n", f, path);
				close(f);
			}
		}
#else
		printf("VKTRI no F_GETPATH here: files not closed\n");
#endif
		const char *hold = getenv("VKTRI_HOLD_S");
		printf("VKTRI files closed; alive for %d s\n", hold ? atoi(hold) : 0);
		fflush(stdout);
		if (hold)
			sleep(atoi(hold));
		_exit(0);
	}
	printf("VKTRI submit\n");
	CK(vkQueueSubmit(q, 1, &si, fence));
	CK(vkWaitForFences(dev, 1, &fence, VK_TRUE, timeout_ns));
	double t_first = now_ms() - t_s0;
	printf("VKTRI fence signalled after %.2f ms\n", t_first);

	uint8_t *px;
	static uint8_t lin[W * H * 4];
	if (linear) {
		VkImageSubresource isr = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
		VkSubresourceLayout sl;
		vkGetImageSubresourceLayout(dev, img, &isr, &sl);
		uint8_t *m;
		CK(vkMapMemory(dev, imem, 0, VK_WHOLE_SIZE, 0, (void **)&m));
		for (int y = 0; y < H; y++)
			memcpy(lin + y * W * 4, m + sl.offset + y * sl.rowPitch, W * 4);
		px = lin;
	} else {
		CK(vkMapMemory(dev, bmem, 0, W * H * 4, 0, (void **)&px));
	}
	static uint8_t ref[W * H * 4], got[W * H * 4];
	memcpy(got, px, sizeof(got));

	// CPU reference and comparison
	int ties = 0, a_bad = 0, b_cov_bad = 0, clear_bad = 0, a_cov = 0, b_cov = 0, maxdiff = 0;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			double w[3];
			uint8_t *r = ref + (y * W + x) * 4, *g = got + (y * W + x) * 4;
			int inb = inside(B, x + 0.5, y + 0.5, &ties, w);
			int ina = inside(A, x + 0.5, y + 0.5, &ties, (double[3]){ 0 });
			if (inb) {
				b_cov++;
				// B's vertex colours: red, green, blue (tri.vert)
				r[0] = (uint8_t)(w[0] * 255.0 + 0.5); r[1] = (uint8_t)(w[1] * 255.0 + 0.5);
				r[2] = (uint8_t)(w[2] * 255.0 + 0.5); r[3] = 255;
				int gb = !(g[0] == CLEAR[0] && g[1] == CLEAR[1] && g[2] == CLEAR[2] && g[3] == CLEAR[3]);
				if (!gb)
					b_cov_bad++;
				for (int c = 0; c < 4; c++) {
					int d = abs((int)r[c] - (int)g[c]);
					if (d > maxdiff) maxdiff = d;
				}
				// the hashed image has B's pixels zeroed, in both copies
				memset(r, 0, 4);
				memset(g, 0, 4);
				continue;
			}
			memcpy(r, ina ? AFLAT : CLEAR, 4);
			if (ina) a_cov++;
			if (memcmp(r, g, 4) != 0) {
				if (ina || !memcmp(g, AFLAT, 4)) a_bad++;
				else clear_bad++;
			}
		}
	uint32_t flat_ref = fnv(ref, sizeof(ref)), flat_got = fnv(got, sizeof(got));
	uint32_t full_got = fnv(px, W * H * 4);
	const char *ppm = getenv("VKTRI_PPM");
	if (ppm) {
		FILE *f = fopen(ppm, "wb");
		if (f) {
			fprintf(f, "P6\n%d %d\n255\n", W, H);
			for (int i = 0; i < W * H; i++)
				fwrite(px + i * 4, 1, 3, f);
			fclose(f);
		}
	}

	// timed frames: the same command buffer, submit and wait each time
	double t_f0 = now_ms();
	for (int i = 0; i < frames; i++) {
		CK(vkResetFences(dev, 1, &fence));
		CK(vkQueueSubmit(q, 1, &si, fence));
		CK(vkWaitForFences(dev, 1, &fence, VK_TRUE, timeout_ns));
	}
	double t_frames = now_ms() - t_f0;
	if (linear) {
		VkImageSubresource isr = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
		VkSubresourceLayout sl;
		vkGetImageSubresourceLayout(dev, img, &isr, &sl);
		uint8_t *m;
		vkUnmapMemory(dev, imem);
		CK(vkMapMemory(dev, imem, 0, VK_WHOLE_SIZE, 0, (void **)&m));
		for (int y = 0; y < H; y++)
			memcpy(lin + y * W * 4, m + sl.offset + y * sl.rowPitch, W * 4);
	}
	uint32_t again = fnv(px, W * H * 4);

	printf("VKTRI times_ms instance+device=%.1f pipeline=%.1f first_submit=%.2f frames=%d per_frame=%.3f total=%.1f\n",
	       t_dev - t0, t_pipe, t_first, frames, frames ? t_frames / frames : 0.0, now_ms() - t0);
	printf("VKTRI flat_fnv=0x%08x expected=0x%08x full_fnv=0x%08x after_frames=0x%08x\n",
	       flat_got, flat_ref, full_got, again);
	printf("VKTRI A covered=%d wrong=%d clear_wrong=%d; B covered=%d uncovered_by_gpu=%d maxdiff=%d; ties=%d\n",
	       a_cov, a_bad, clear_bad, b_cov, b_cov_bad, maxdiff, ties);

	vkDeviceWaitIdle(dev);
	vkDestroyDevice(dev, NULL);
	vkDestroyInstance(inst, NULL);

	if (ties)
		printf("VKTRI FAIL test design: %d pixel centres on an edge\n", ties);
	else if (flat_got != flat_ref)
		printf("VKTRI FAIL flat hash differs (A wrong %d, clear wrong %d, B uncovered %d)\n", a_bad, clear_bad, b_cov_bad);
	else if (maxdiff > 2)
		printf("VKTRI FAIL interpolated triangle differs by %d\n", maxdiff);
	else if (again != full_got)
		printf("VKTRI FAIL image changed over the timed frames\n");
	else {
		printf("VKTRI PASS\n");
		return 0;
	}
	return 1;
}
