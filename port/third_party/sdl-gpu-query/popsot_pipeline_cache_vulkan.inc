/* POPSoT: include inside SDL_gpu_vulkan.c, before VULKAN_CreateGraphicsPipeline.
 * SDL 3.4.18 creates every Vulkan pipeline without a VkPipelineCache. On
 * Adreno each pipeline using POPSoT's NV2A pixel shader then took 0.5-1 s to
 * compile, on every run (2026-10-19 device logs). One cache per logical
 * device is loaded from, and saved to, the file named by
 * POPSOT_SetVulkanPipelineCachePath; the driver validates the data's header
 * and ignores data from another device or driver. MIT, see repository LICENSE. */
static char popsot_pc_path[1024];
static VkPipelineCache popsot_pc;
static VkDevice popsot_pc_device;
static SDL_AtomicInt popsot_pc_dirty;

#if defined(_WIN32)
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
void SDLCALL POPSOT_SetVulkanPipelineCachePath(const char *path)
{
    SDL_strlcpy(popsot_pc_path, path ? path : "", sizeof(popsot_pc_path));
}

static VkPipelineCache popsot_pipeline_cache(VulkanRenderer *r)
{
    if (popsot_pc_device != r->logicalDevice) {
        size_t size = 0;
        void *data = popsot_pc_path[0] ? SDL_LoadFile(popsot_pc_path, &size) : NULL;
        VkPipelineCacheCreateInfo ci;
        SDL_zero(ci);
        ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        ci.initialDataSize = data ? size : 0;
        ci.pInitialData = data;
        popsot_pc = VK_NULL_HANDLE;
        if (r->vkCreatePipelineCache(r->logicalDevice, &ci, NULL, &popsot_pc) != VK_SUCCESS) {
            ci.initialDataSize = 0;
            ci.pInitialData = NULL;
            size = 0;
            if (r->vkCreatePipelineCache(r->logicalDevice, &ci, NULL, &popsot_pc) != VK_SUCCESS)
                popsot_pc = VK_NULL_HANDLE;
        }
        SDL_free(data);
        popsot_pc_device = r->logicalDevice;
        SDL_Log("POPSoT pipeline cache: %u bytes loaded from %s", (unsigned)size,
                popsot_pc_path[0] ? popsot_pc_path : "(no file)");
    }
    SDL_SetAtomicInt(&popsot_pc_dirty, 1);
    return popsot_pc;
}

/* Writes the cache when pipelines were created since the last save. Returns
 * false only when a save was needed and failed. */
#if defined(_WIN32)
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
bool SDLCALL POPSOT_SaveVulkanPipelineCache(SDL_GPUDevice *device)
{
    VulkanRenderer *r = (VulkanRenderer *)device->driverData;
    char tmp[1040];
    size_t size = 0;
    void *data;
    bool ok;
    if (!popsot_pc || popsot_pc_device != r->logicalDevice || !popsot_pc_path[0])
        return true;
    if (!SDL_CompareAndSwapAtomicInt(&popsot_pc_dirty, 1, 0))
        return true;
    if (r->vkGetPipelineCacheData(r->logicalDevice, popsot_pc, &size, NULL) != VK_SUCCESS || !size)
        return false;
    data = SDL_malloc(size);
    if (!data)
        return false;
    ok = r->vkGetPipelineCacheData(r->logicalDevice, popsot_pc, &size, data) == VK_SUCCESS;
    SDL_snprintf(tmp, sizeof(tmp), "%s.tmp", popsot_pc_path);
    ok = ok && SDL_SaveFile(tmp, data, size);
    SDL_free(data);
    if (ok) {
        SDL_RemovePath(popsot_pc_path);
        ok = SDL_RenamePath(tmp, popsot_pc_path);
    }
    if (!ok)
        SDL_SetAtomicInt(&popsot_pc_dirty, 1);
    return ok;
}
