/* Native storage for ordinary NVN memory pools. Guest CPU storage remains in
 * the checked guest address space; explicit flush/invalidate copies it to/from
 * mapped native memory. Commands must resolve virtual GPU addresses via pools. */
#if defined(SP_GUEST_VULKAN) && !defined(SSBU_PS4)
typedef struct { VkBuffer buffer; VkDeviceMemory memory; void *map; size_t size; NativeGraphics *owner; } NativeBuffer;
static int native_buffer_close(NativeGraphics *gpu,NativeBuffer **pointer) {
    NativeBuffer *buffer=*pointer; if (!buffer) return SP_OK;
    PFN_vkDeviceWaitIdle wait=(PFN_vkDeviceWaitIdle)gpu->get(gpu->instance,"vkDeviceWaitIdle");
    PFN_vkUnmapMemory unmap=(PFN_vkUnmapMemory)gpu->get(gpu->instance,"vkUnmapMemory");
    PFN_vkDestroyBuffer destroy=(PFN_vkDestroyBuffer)gpu->get(gpu->instance,"vkDestroyBuffer");
    PFN_vkFreeMemory release=(PFN_vkFreeMemory)gpu->get(gpu->instance,"vkFreeMemory");
    if (!wait || !unmap || !destroy || !release || wait(gpu->device)!=VK_SUCCESS) return SP_SYSTEM;
    if (buffer->map) unmap(gpu->device,buffer->memory);
    if (buffer->buffer) destroy(gpu->device,buffer->buffer,NULL);
    if (buffer->memory) release(gpu->device,buffer->memory,NULL);
    free(buffer); *pointer=NULL; return SP_OK;
}
static int native_buffer_open(NativeGraphics *gpu,NativeBuffer **out,size_t bytes) {
    if (!gpu || !gpu->device || !bytes || bytes>512*1024*1024) return SP_INVALID;
    NativeBuffer *buffer=calloc(1,sizeof *buffer); if (!buffer) return SP_SYSTEM;
    *out=buffer; buffer->size=bytes; buffer->owner=gpu;
    PFN_vkCreateBuffer create=(PFN_vkCreateBuffer)gpu->get(gpu->instance,"vkCreateBuffer");
    PFN_vkGetBufferMemoryRequirements requirements=(PFN_vkGetBufferMemoryRequirements)gpu->get(gpu->instance,"vkGetBufferMemoryRequirements");
    PFN_vkGetPhysicalDeviceMemoryProperties properties=(PFN_vkGetPhysicalDeviceMemoryProperties)gpu->get(gpu->instance,"vkGetPhysicalDeviceMemoryProperties");
    PFN_vkAllocateMemory allocate=(PFN_vkAllocateMemory)gpu->get(gpu->instance,"vkAllocateMemory");
    PFN_vkBindBufferMemory bind=(PFN_vkBindBufferMemory)gpu->get(gpu->instance,"vkBindBufferMemory");
    PFN_vkMapMemory map=(PFN_vkMapMemory)gpu->get(gpu->instance,"vkMapMemory");
    if (!create || !requirements || !properties || !allocate || !bind || !map) return SP_UNSUPPORTED;
    VkBufferCreateInfo info={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=bytes,
        .usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
        .sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    if (create(gpu->device,&info,NULL,&buffer->buffer)!=VK_SUCCESS) return SP_SYSTEM;
    VkMemoryRequirements required; requirements(gpu->device,buffer->buffer,&required);
    VkPhysicalDeviceMemoryProperties memory; properties(gpu->physical,&memory);
    for (unsigned i=0;i<memory.memoryTypeCount;++i) if ((required.memoryTypeBits&(1u<<i)) &&
        (memory.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==
        (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        VkMemoryAllocateInfo allocation={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=required.size,.memoryTypeIndex=i};
        if (allocate(gpu->device,&allocation,NULL,&buffer->memory)!=VK_SUCCESS) continue;
        if (bind(gpu->device,buffer->buffer,buffer->memory,0)!=VK_SUCCESS ||
            map(gpu->device,buffer->memory,0,bytes,0,&buffer->map)!=VK_SUCCESS) return SP_SYSTEM;
        return SP_OK;
    }
    return SP_UNSUPPORTED;
}
#elif defined(SP_GUEST_GNM) && defined(SSBU_PS4)
typedef struct { void *map; off_t offset; size_t size,allocated; NativeGraphics *owner; } NativeBuffer;
static int native_buffer_close(NativeGraphics *gpu,NativeBuffer **pointer) {
    NativeBuffer *buffer=*pointer; if (!buffer) return SP_OK;
    int wait=native_graphics_wait(gpu); if (wait) return wait;
    /* Command submission must retire every reference before calling this. */
    if (buffer->map && sceKernelMunmap(buffer->map,buffer->allocated)) return SP_SYSTEM;
    buffer->map=NULL;
    if (buffer->offset>=0 && sceKernelReleaseDirectMemory(buffer->offset,buffer->allocated)) return SP_SYSTEM;
    free(buffer); *pointer=NULL; return SP_OK;
}
static int native_buffer_open(NativeGraphics *gpu,NativeBuffer **out,size_t bytes) {
    if (!gpu || !gpu->completed || !bytes || bytes>512*1024*1024) return SP_INVALID;
    NativeBuffer *buffer=calloc(1,sizeof *buffer); if (!buffer) return SP_SYSTEM;
    *out=buffer; buffer->offset=-1; buffer->size=bytes; buffer->owner=gpu;
    buffer->allocated=(bytes+65535)&~(size_t)65535;
    int rc=sceKernelAllocateDirectMemory(0,(off_t)sceKernelGetDirectMemorySize(),buffer->allocated,65536,3,&buffer->offset);
    if (!rc) rc=sceKernelMapDirectMemory(&buffer->map,buffer->allocated,0x33,0,buffer->offset,65536);
    return rc ? SP_SYSTEM : SP_OK;
}
#else
typedef struct { void *map; size_t size; NativeGraphics *owner; } NativeBuffer;
static int native_buffer_close(NativeGraphics *gpu,NativeBuffer **out) { (void)gpu; (void)out; return SP_UNSUPPORTED; }
static int native_buffer_open(NativeGraphics *gpu,NativeBuffer **out,size_t bytes) {
    (void)gpu; (void)out; (void)bytes; return SP_UNSUPPORTED;
}
#endif
static int native_buffer_copy(NativeBuffer *buffer,size_t offset,void *guest,size_t bytes,int invalidate) {
    if (!buffer || !buffer->map || offset>buffer->size || bytes>buffer->size-offset) return SP_INVALID;
    int rc=native_graphics_wait(buffer->owner); if (rc) return rc;
    if (invalidate) memcpy(guest,(unsigned char *)buffer->map+offset,bytes);
    else { memcpy((unsigned char *)buffer->map+offset,guest,bytes); __asm__ volatile("sfence" ::: "memory"); }
    return SP_OK;
}
