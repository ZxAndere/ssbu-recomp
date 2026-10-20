/* Optional real desktop Vulkan context, used to continue CPU startup while
 * developing the native renderer. No NVN entry is advertised by this context.
 * PS4 requires a separately validated native backend; it never takes this path. */
#if defined(SP_GUEST_VULKAN) && !defined(SSBU_PS4)
#include <vulkan/vulkan.h>
typedef struct {
    SpModule *module; VkInstance instance; VkPhysicalDevice physical; VkDevice device; VkQueue queue;
    PFN_vkGetInstanceProcAddr get; PFN_vkDestroyInstance destroy_instance; PFN_vkDestroyDevice destroy_device;
} NativeGraphics;
static void native_graphics_close(NativeGraphics **pointer) {
    NativeGraphics *gpu=*pointer; if (!gpu) return;
    if (gpu->device) gpu->destroy_device(gpu->device,NULL);
    if (gpu->instance) gpu->destroy_instance(gpu->instance,NULL);
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
        VkPhysicalDeviceVulkan13Features f13={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 f2={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&f13}; features(devices[i],&f2);
        if (!f13.dynamicRendering || !f13.synchronization2) continue;
        uint32_t total=0; families(devices[i],&total,NULL); if (!total || total>128) continue;
        VkQueueFamilyProperties queues[128]; families(devices[i],&total,queues);
        for (uint32_t family=0;family<total;++family) if (queues[family].queueCount && (queues[family].queueFlags&3)==3) {
            float priority=1;
            VkDeviceQueueCreateInfo queue={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
            VkPhysicalDeviceVulkan13Features enabled={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,.dynamicRendering=VK_TRUE,.synchronization2=VK_TRUE};
            VkDeviceCreateInfo device={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&enabled,.queueCreateInfoCount=1,.pQueueCreateInfos=&queue};
            if (create_device(devices[i],&device,NULL,&gpu->device)!=VK_SUCCESS) continue;
            gpu->physical=devices[i]; gpu->destroy_device=(PFN_vkDestroyDevice)gpu->get(gpu->instance,"vkDestroyDevice");
            PFN_vkGetDeviceQueue get_queue=(PFN_vkGetDeviceQueue)gpu->get(gpu->instance,"vkGetDeviceQueue");
            if (!gpu->destroy_device || !get_queue) return SP_SYSTEM;
            get_queue(gpu->device,family,0,&gpu->queue); if (!gpu->queue) return SP_SYSTEM;
            sp_log("GAME GRAPHICS: real Vulkan device %s API=%u.%u; NVN translation pending",info.deviceName,
                VK_API_VERSION_MAJOR(info.apiVersion),VK_API_VERSION_MINOR(info.apiVersion)); return SP_OK;
        }
    }
    return SP_UNSUPPORTED;
}
#else
typedef struct NativeGraphics NativeGraphics;
static void native_graphics_close(NativeGraphics **pointer) { (void)pointer; }
static int native_graphics_open(NativeGraphics **out) { (void)out; return SP_UNSUPPORTED; }
#endif
