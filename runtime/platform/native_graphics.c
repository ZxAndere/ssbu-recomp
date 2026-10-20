/* Virtual NVN descriptors are decoded by the command translator. They never
 * contain raw Switch hardware descriptor words or native Vulkan handles. */
typedef struct { uint64_t object,offset,extent,generation; } NativeDescriptor;
static int native_graphics_policy(unsigned query,int32_t *value) {
    switch (query) {
    case 0xf: case 0x10: *value=sizeof(NativeDescriptor); break;
    case 0x11: case 0x12: *value=0; break; /* No reserved descriptor slots. */
    case 0x13: *value=4; break; /* Command words. */
    case 0x14: *value=8; break; /* Control pointers. */
    case 0x15: case 0x16: *value=4096; break; /* Recorder allocation blocks. */
    case 0x17: case 0x18: case 0x37: *value=0; break; /* Scratch shaders are not implemented. */
    case 0x19: case 0x1a: case 0x27: case 0x3a: *value=65536; break; /* Virtual pool page. */
    case 0x24: *value=1<<20; break; /* Guest handle texture-index field. */
    case 0x25: *value=1<<12; break; /* Guest handle sampler-index field. */
    case 0x28: case 0x2a: case 0x2b: case 0x32: case 0x34: case 0x36: case 0x3b:
    case 0x42: case 0x43: case 0x44: case 0x45: case 0x47: case 0x48: case 0x49:
    case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x57: case 0x5a: case 0x61:
        *value=0; break; /* No translated support, or no portable L2-size query. */
    case 0x35: case 0x3c: *value=4; break;
    case 0x38: case 0x39: *value=256; break; /* Linear upload pitch policy. */
    case 0x46: *value=1; break; /* Only vsync presentation is planned. */
    case 0x4e: *value=0; break; /* Guest shader code is decoded, never fetched by the host GPU. */
    case 0x50: case 0x53: case 0x56: case 0x5d: *value=4096; break;
    case 0x51: case 0x52: case 0x54: case 0x55: case 0x5b: case 0x5c: *value=65536; break;
    case 0x58: *value=3; break;
    case 0x59: *value=2; break;
    case 0x60: *value=65535; break;
    default: return 0;
    }
    return 1;
}
/* Optional real desktop Vulkan context, used to continue CPU startup while
 * developing the native renderer. No NVN entry is advertised by this context.
 * PS4 requires a separately validated native backend; it never takes this path. */
#if defined(SP_GUEST_VULKAN) && !defined(SSBU_PS4)
#include <vulkan/vulkan.h>
typedef struct {
    SpModule *module; VkInstance instance; VkPhysicalDevice physical; VkDevice device; VkQueue queue;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceFeatures enabled_features; unsigned shader_demote,draw_parameters;
    uint32_t family;
    PFN_vkGetInstanceProcAddr get; PFN_vkDestroyInstance destroy_instance; PFN_vkDestroyDevice destroy_device;
} NativeGraphics;
static const char *native_graphics_name(void) { return "desktop Vulkan"; }
static int native_graphics_wait(NativeGraphics *gpu) {
    if (!gpu || !gpu->device) return SP_INVALID;
    PFN_vkQueueWaitIdle wait=(PFN_vkQueueWaitIdle)gpu->get(gpu->instance,"vkQueueWaitIdle");
    return wait && wait(gpu->queue)==VK_SUCCESS ? SP_OK : SP_SYSTEM;
}
static void native_graphics_close(NativeGraphics **pointer) {
    NativeGraphics *gpu=*pointer; if (!gpu) return;
    if (gpu->device && gpu->destroy_device) gpu->destroy_device(gpu->device,NULL);
    if (gpu->instance && gpu->destroy_instance) gpu->destroy_instance(gpu->instance,NULL);
    if (gpu->module) sp_module_close(&gpu->module);
    free(gpu); *pointer=NULL;
}
static int native_graphics_open(NativeGraphics **out) {
    NativeGraphics *gpu=calloc(1,sizeof *gpu); if (!gpu) return SP_SYSTEM;
    *out=gpu; void *symbol=NULL;
    if (sp_module_open(&gpu->module,"C:/Windows/System32/vulkan-1.dll") ||
        sp_module_symbol(gpu->module,"vkGetInstanceProcAddr",&symbol)) return SP_UNSUPPORTED;
    memcpy(&gpu->get,&symbol,sizeof gpu->get);
    PFN_vkCreateInstance create_instance=(PFN_vkCreateInstance)gpu->get(NULL,"vkCreateInstance");
    if (!create_instance) return SP_UNSUPPORTED;
    VkApplicationInfo application={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="SSBU native CPU startup",.apiVersion=VK_API_VERSION_1_3};
    VkInstanceCreateInfo instance={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&application};
    if (create_instance(&instance,NULL,&gpu->instance)!=VK_SUCCESS) return SP_UNSUPPORTED;
    gpu->destroy_instance=(PFN_vkDestroyInstance)gpu->get(gpu->instance,"vkDestroyInstance");
    PFN_vkEnumeratePhysicalDevices enumerate=(PFN_vkEnumeratePhysicalDevices)gpu->get(gpu->instance,"vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties properties=(PFN_vkGetPhysicalDeviceProperties)gpu->get(gpu->instance,"vkGetPhysicalDeviceProperties");
    PFN_vkGetPhysicalDeviceQueueFamilyProperties families=(PFN_vkGetPhysicalDeviceQueueFamilyProperties)gpu->get(gpu->instance,"vkGetPhysicalDeviceQueueFamilyProperties");
    PFN_vkGetPhysicalDeviceFeatures2 features=(PFN_vkGetPhysicalDeviceFeatures2)gpu->get(gpu->instance,"vkGetPhysicalDeviceFeatures2");
    PFN_vkCreateDevice create_device=(PFN_vkCreateDevice)gpu->get(gpu->instance,"vkCreateDevice");
    if (!gpu->destroy_instance || !enumerate || !properties || !families || !features || !create_device) return SP_UNSUPPORTED;
    uint32_t count=0;
    if (enumerate(gpu->instance,&count,NULL)!=VK_SUCCESS || !count || count>32) return SP_UNSUPPORTED;
    VkPhysicalDevice devices[32]; if (enumerate(gpu->instance,&count,devices)!=VK_SUCCESS) return SP_SYSTEM;
    for (uint32_t i=0;i<count;++i) {
        VkPhysicalDeviceProperties info; properties(devices[i],&info);
        if (info.apiVersion<VK_API_VERSION_1_3) continue;
        VkPhysicalDeviceVulkan11Features f11={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
        VkPhysicalDeviceVulkan13Features f13={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,.pNext=&f11};
        VkPhysicalDeviceFeatures2 f2={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&f13}; features(devices[i],&f2);
        if (!f13.dynamicRendering || !f13.synchronization2) continue;
        uint32_t total=0; families(devices[i],&total,NULL); if (!total || total>128) continue;
        VkQueueFamilyProperties queues[128]; families(devices[i],&total,queues);
        for (uint32_t family=0;family<total;++family) if (queues[family].queueCount && (queues[family].queueFlags&3)==3) {
            float priority=1;
            VkDeviceQueueCreateInfo queue={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
            VkPhysicalDeviceVulkan11Features enable11={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,.shaderDrawParameters=f11.shaderDrawParameters};
            VkPhysicalDeviceVulkan13Features enabled={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,.pNext=&enable11,
                .dynamicRendering=VK_TRUE,.synchronization2=VK_TRUE,.shaderDemoteToHelperInvocation=f13.shaderDemoteToHelperInvocation};
            VkPhysicalDeviceFeatures core={.shaderInt64=f2.features.shaderInt64,.shaderFloat64=f2.features.shaderFloat64,
                .shaderInt16=f2.features.shaderInt16,.vertexPipelineStoresAndAtomics=f2.features.vertexPipelineStoresAndAtomics,
                .fragmentStoresAndAtomics=f2.features.fragmentStoresAndAtomics,.geometryShader=f2.features.geometryShader,
                .tessellationShader=f2.features.tessellationShader,.shaderStorageImageReadWithoutFormat=f2.features.shaderStorageImageReadWithoutFormat,
                .shaderStorageImageWriteWithoutFormat=f2.features.shaderStorageImageWriteWithoutFormat};
            VkDeviceCreateInfo device={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&enabled,.queueCreateInfoCount=1,.pQueueCreateInfos=&queue,.pEnabledFeatures=&core};
            if (create_device(devices[i],&device,NULL,&gpu->device)!=VK_SUCCESS) continue;
            gpu->physical=devices[i]; gpu->properties=info; gpu->family=family; gpu->enabled_features=core;
            gpu->shader_demote=f13.shaderDemoteToHelperInvocation; gpu->draw_parameters=f11.shaderDrawParameters;
            gpu->destroy_device=(PFN_vkDestroyDevice)gpu->get(gpu->instance,"vkDestroyDevice");
            PFN_vkGetDeviceQueue get_queue=(PFN_vkGetDeviceQueue)gpu->get(gpu->instance,"vkGetDeviceQueue");
            if (!gpu->destroy_device || !get_queue) return SP_SYSTEM;
            get_queue(gpu->device,family,0,&gpu->queue); if (!gpu->queue) return SP_SYSTEM;
            sp_log("GAME GRAPHICS: real Vulkan device %s API=%u.%u; NVN translation pending",info.deviceName,
                VK_API_VERSION_MAJOR(info.apiVersion),VK_API_VERSION_MINOR(info.apiVersion)); return SP_OK;
        }
    }
    return SP_UNSUPPORTED;
}
/* Limits describe the selected backend, not a copied Tegra capability dump.
 * Resource operations still stop individually until translated. Transform
 * feedback is not enabled and therefore has zero available bindings. */
static int native_graphics_integer(NativeGraphics *gpu,unsigned query,int32_t *value) {
    if (!gpu) return 0;
    const VkPhysicalDeviceLimits *p=&gpu->properties.limits;
    switch (query) {
    case 2: *value=(int32_t)p->maxPerStageDescriptorUniformBuffers; break;
    case 3: *value=(int32_t)p->maxUniformBufferRange; break;
    case 4: *value=(int32_t)p->minUniformBufferOffsetAlignment; break;
    case 5: *value=(int32_t)p->maxColorAttachments; break;
    case 6: *value=(int32_t)p->maxVertexInputBindings; break;
    case 7: *value=0; break;
    case 8: *value=(int32_t)p->maxPerStageDescriptorStorageBuffers; break;
    case 9: *value=(int32_t)(p->maxPerStageDescriptorSamplers<p->maxPerStageDescriptorSampledImages ?
                               p->maxPerStageDescriptorSamplers : p->maxPerStageDescriptorSampledImages); break;
    case 0xa: case 0xb: case 0xc: case 0xd: *value=4; break; /* 32-bit counters/indirect records. */
    case 0xe: *value=(int32_t)p->maxVertexInputAttributes; break;
    case 0x1b: *value=1; break; /* Anisotropy is not enabled. */
    case 0x1c: case 0x1d: case 0x1e: *value=(int32_t)p->maxComputeWorkGroupSize[query-0x1c]; break;
    case 0x1f: *value=(int32_t)p->maxComputeWorkGroupInvocations; break;
    case 0x20: case 0x21: case 0x22: *value=(int32_t)p->maxComputeWorkGroupCount[query-0x20]; break;
    case 0x23: *value=(int32_t)p->maxPerStageDescriptorStorageImages; break;
    case 0x26: *value=1; break; /* multiViewport feature is not enabled. */
    case 0x29: *value=0; break; /* Separate stencil texture translation not implemented. */
    case 0x2c: {
        uint32_t extent=p->maxImageDimension2D,levels=0;
        do { ++levels; extent>>=1; } while (extent); *value=(int32_t)levels; break;
    }
    case 0x2d: *value=(int32_t)p->maxImageArrayLayers; break;
    case 0x33: *value=(int32_t)p->subPixelPrecisionBits; break;
    case 0x3d: case 0x41: *value=(int32_t)p->maxImageDimension2D; break;
    case 0x3e: *value=(int32_t)p->maxTexelBufferElements; break;
    case 0x3f: *value=(int32_t)p->maxImageDimension3D; break;
    case 0x40: *value=(int32_t)p->maxImageDimensionCube; break;
    case 0x4f: *value=0; break; /* Tessellation feature is not enabled. */
    case 0x5e: *value=(int32_t)p->maxPerStageDescriptorSampledImages; break;
    case 0x5f: *value=(int32_t)p->maxPerStageDescriptorSamplers; break;
    default: return 0;
    }
    return 1;
}
#elif defined(SP_GUEST_GNM) && defined(SSBU_PS4)
#include "native_gnm.c"
#else
typedef struct NativeGraphics NativeGraphics;
static const char *native_graphics_name(void) { return "unavailable"; }
static void native_graphics_close(NativeGraphics **pointer) { (void)pointer; }
static int native_graphics_open(NativeGraphics **out) { (void)out; return SP_UNSUPPORTED; }
static int native_graphics_wait(NativeGraphics *gpu) { (void)gpu; return SP_UNSUPPORTED; }
static int native_graphics_integer(NativeGraphics *gpu,unsigned query,int32_t *value) {
    (void)gpu; (void)query; (void)value; return 0;
}
#endif
#include "native_buffer.c"
#include "native_fence.c"
#include "native_sampler.c"
#include "native_shader.c"
