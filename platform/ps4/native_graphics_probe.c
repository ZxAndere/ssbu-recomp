/* Original storage/transfer fixture. It renders no image and uses no game data. */
#include "platform.h"
#include <stdlib.h>
#include <string.h>
#include "native_graphics.c"
#if defined(SP_GUEST_VULKAN) && !defined(SSBU_PS4)
static int transfer(NativeGraphics *gpu,NativeBuffer *a,NativeBuffer *b) {
#define GET(type,name) type name=(type)gpu->get(gpu->instance,#name); if (!name) return SP_UNSUPPORTED
    GET(PFN_vkCreateCommandPool,vkCreateCommandPool);
    GET(PFN_vkDestroyCommandPool,vkDestroyCommandPool);
    GET(PFN_vkAllocateCommandBuffers,vkAllocateCommandBuffers);
    GET(PFN_vkBeginCommandBuffer,vkBeginCommandBuffer);
    GET(PFN_vkEndCommandBuffer,vkEndCommandBuffer);
    GET(PFN_vkCmdCopyBuffer,vkCmdCopyBuffer);
    GET(PFN_vkCmdPipelineBarrier2,vkCmdPipelineBarrier2);
    GET(PFN_vkQueueSubmit2,vkQueueSubmit2);
    GET(PFN_vkQueueWaitIdle,vkQueueWaitIdle);
#undef GET
    VkCommandPool pool=VK_NULL_HANDLE;
    VkCommandPoolCreateInfo creation={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=gpu->family};
    if (vkCreateCommandPool(gpu->device,&creation,NULL,&pool)!=VK_SUCCESS) return SP_SYSTEM;
    int rc=SP_SYSTEM;
    VkCommandBuffer command;
    VkCommandBufferAllocateInfo allocation={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    if (vkAllocateCommandBuffers(gpu->device,&allocation,&command)!=VK_SUCCESS) goto done;
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (vkBeginCommandBuffer(command,&begin)!=VK_SUCCESS) goto done;
    VkBufferCopy copy={.size=a->size}; vkCmdCopyBuffer(command,a->buffer,b->buffer,1,&copy);
    VkMemoryBarrier2 barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,.srcStageMask=VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask=VK_ACCESS_2_TRANSFER_WRITE_BIT,.dstStageMask=VK_PIPELINE_STAGE_2_HOST_BIT,.dstAccessMask=VK_ACCESS_2_HOST_READ_BIT};
    VkDependencyInfo dependency={.sType=VK_STRUCTURE_TYPE_DEPENDENCY_INFO,.memoryBarrierCount=1,.pMemoryBarriers=&barrier};
    vkCmdPipelineBarrier2(command,&dependency);
    if (vkEndCommandBuffer(command)!=VK_SUCCESS) goto done;
    VkCommandBufferSubmitInfo buffer={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,.commandBuffer=command};
    VkSubmitInfo2 submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO_2,.commandBufferInfoCount=1,.pCommandBufferInfos=&buffer};
    if (vkQueueSubmit2(gpu->queue,1,&submit,VK_NULL_HANDLE)!=VK_SUCCESS) goto done;
    if (vkQueueWaitIdle(gpu->queue)==VK_SUCCESS) rc=SP_OK;
done:
    vkDestroyCommandPool(gpu->device,pool,NULL); return rc;
}
#endif
int main(void) {
    NativeGraphics *gpu=NULL; NativeBuffer *a=NULL,*b=NULL; NativeFence *fence=NULL; NativeSampler *sampler=NULL,*rejected=NULL;
    NativeShader *shader=NULL;
    int result=1; int32_t value;
    unsigned char original[65536],received[65536];
    for (unsigned i=0;i<sizeof original;++i) original[i]=(unsigned char)((i*131u)^(i>>8)^0xa7);
    sp_init(NULL);
    if (native_graphics_open(&gpu)) goto done;
    const uint32_t vertex[]={
        0x07230203,0x00010000,0,12,0,0x00020011,1,0x0003000e,0,1,
        0x0006000f,0,1,0x6e69616d,0,2,0x00040047,2,11,0,
        0x00020013,3,0x00030021,4,3,0x00030016,5,32,0x00040017,6,5,4,
        0x00040020,7,3,6,0x0004003b,7,2,3,0x0004002b,5,8,0,0x0004002b,5,9,0x3f800000,
        0x0007002c,6,10,8,8,8,9,0x00050036,3,1,0,4,0x000200f8,11,0x0003003e,2,10,0x000100fd,0x00010038};
    if (native_shader_open(gpu,&shader,vertex,sizeof vertex,0)) goto done;
    if (native_graphics_wait(gpu)) goto done;
    if (native_fence_open(gpu,&fence) || native_fence_wait(gpu,fence,0)!=3) goto done;
    if (native_fence_signal(gpu,fence) || native_fence_wait(gpu,fence,1000000000)>1 || native_fence_wait(gpu,fence,0)!=0) goto done;
    NativeSamplerParameters parameters={.min_filter=1,.mag_filter=1,.wrap={1,7,6},.max_lod=16,.compare_function=2,.anisotropy=1};
    if (native_sampler_open(gpu,&sampler,&parameters)) goto done;
    parameters.anisotropy=2;
    if (native_sampler_open(gpu,&rejected,&parameters)!=SP_UNSUPPORTED || rejected) goto done;
    if (!native_graphics_policy(0xf,&value) || value!=sizeof(NativeDescriptor)) goto done;
    if (!native_graphics_integer(gpu,3,&value) || value<16384) goto done;
    if (native_buffer_open(gpu,&a,sizeof original) || native_buffer_open(gpu,&b,sizeof original)) goto done;
    if (native_buffer_copy(a,0,original,sizeof original,0)) goto done;
    if (native_buffer_copy(a,sizeof original,original,1,0)!=SP_INVALID) goto done;
    if (native_buffer_copy(a,1,original,(size_t)-1,0)!=SP_INVALID) goto done;
#if defined(SP_GUEST_VULKAN) && !defined(SSBU_PS4)
    if (transfer(gpu,a,b)) goto done;
    if (native_buffer_copy(b,0,received,sizeof received,1) || memcmp(original,received,sizeof received)) goto done;
    sp_log("NATIVE GRAPHICS PROBE: %s GPU buffer transfer verified bytes=%u",native_graphics_name(),(unsigned)sizeof received);
#else
    if (native_buffer_copy(a,0,received,sizeof received,1) || memcmp(original,received,sizeof received)) goto done;
    sp_log("NATIVE GRAPHICS PROBE: %s mapped storage verified; GPU transfer untested",native_graphics_name());
#endif
    result=0;
done:
    if (shader && native_shader_close(gpu,&shader)) result=1;
    if (sampler && native_sampler_close(gpu,&sampler)) result=1;
    if (fence && native_fence_close(gpu,&fence)) result=1;
    if (a && native_buffer_close(gpu,&a)) result=1;
    if (b && native_buffer_close(gpu,&b)) result=1;
    native_graphics_close(&gpu); sp_shutdown(); return result;
}
