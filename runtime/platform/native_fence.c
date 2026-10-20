#if defined(SP_GUEST_VULKAN) && !defined(SSBU_PS4)
typedef struct { VkFence fence; unsigned submitted; } NativeFence;
static int native_fence_open(NativeGraphics *gpu,NativeFence **out) {
    NativeFence *f=calloc(1,sizeof *f); if (!f) return SP_SYSTEM; *out=f;
    PFN_vkCreateFence create=(PFN_vkCreateFence)gpu->get(gpu->instance,"vkCreateFence");
    VkFenceCreateInfo info={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    return create && create(gpu->device,&info,NULL,&f->fence)==VK_SUCCESS ? SP_OK : SP_SYSTEM;
}
static int native_fence_close(NativeGraphics *gpu,NativeFence **out) {
    NativeFence *f=*out; if (!f) return SP_OK;
    if (native_graphics_wait(gpu)) return SP_SYSTEM;
    PFN_vkDestroyFence destroy=(PFN_vkDestroyFence)gpu->get(gpu->instance,"vkDestroyFence");
    if (!destroy) return SP_SYSTEM;
    if (f->fence) destroy(gpu->device,f->fence,NULL); free(f); *out=NULL; return SP_OK;
}
static int native_fence_signal(NativeGraphics *gpu,NativeFence *f) {
    if (native_graphics_wait(gpu)) return SP_SYSTEM;
    PFN_vkResetFences reset=(PFN_vkResetFences)gpu->get(gpu->instance,"vkResetFences");
    PFN_vkQueueSubmit2 submit=(PFN_vkQueueSubmit2)gpu->get(gpu->instance,"vkQueueSubmit2");
    if (!reset || !submit || reset(gpu->device,1,&f->fence)!=VK_SUCCESS) return SP_SYSTEM;
    f->submitted=0;
    VkSubmitInfo2 info={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    if (submit(gpu->queue,1,&info,f->fence)!=VK_SUCCESS) return SP_SYSTEM;
    f->submitted=1; return SP_OK;
}
static unsigned native_fence_wait(NativeGraphics *gpu,NativeFence *f,uint64_t ns) {
    if (!f->submitted) return 3;
    PFN_vkGetFenceStatus status=(PFN_vkGetFenceStatus)gpu->get(gpu->instance,"vkGetFenceStatus");
    PFN_vkWaitForFences wait=(PFN_vkWaitForFences)gpu->get(gpu->instance,"vkWaitForFences");
    if (!status || !wait) return 3;
    VkResult rc=status(gpu->device,f->fence);
    if (rc==VK_SUCCESS) return 0;
    if (rc!=VK_NOT_READY) return 3;
    rc=wait(gpu->device,1,&f->fence,VK_TRUE,ns);
    return rc==VK_SUCCESS ? 1 : rc==VK_TIMEOUT ? 2 : 3;
}
#elif defined(SP_GUEST_GNM) && defined(SSBU_PS4)
typedef struct { NativeBuffer *storage; uint64_t nonce; unsigned submitted; } NativeFence;
static int native_fence_open(NativeGraphics *gpu,NativeFence **out) {
    NativeFence *f=calloc(1,sizeof *f); if (!f) return SP_SYSTEM; *out=f;
    int rc=native_buffer_open(gpu,&f->storage,65536);
    if (!rc) memset(f->storage->map,0,65536); return rc;
}
static int native_fence_close(NativeGraphics *gpu,NativeFence **out) {
    NativeFence *f=*out; if (!f) return SP_OK;
    int rc=native_graphics_wait(gpu); if (rc) return rc;
    rc=native_buffer_close(gpu,&f->storage); if (rc) return rc;
    free(f); *out=NULL; return SP_OK;
}
static int native_fence_signal(NativeGraphics *gpu,NativeFence *f) {
    int rc=native_graphics_wait(gpu); if (rc) return rc;
    volatile uint64_t *label=(volatile uint64_t *)f->storage->map;
    *label=0; if (!++f->nonce) ++f->nonce; f->submitted=0;
    GnmCommandBuffer command=sceGnmCmdInit(gpu->memory,65536,NULL,NULL);
    sceGnmDrawCmdEventWriteEop(&command,GNM_CACHE_FLUSH_AND_INV_TS_EVENT,(uint64_t)(uintptr_t)label,GNM_DATA_SEL_SEND_DATA64,f->nonce);
    uint32_t bytes=(uint32_t)((unsigned char *)command.cmdptr-(unsigned char *)command.beginptr); void *address=command.beginptr;
    __asm__ volatile("sfence" ::: "memory");
    if (sceGnmSubmitCommandBuffers(1,&address,&bytes,NULL,NULL)) return SP_SYSTEM;
    ++gpu->submitted; gpu->last_label=label; gpu->last_nonce=f->nonce; f->submitted=1;
    return sceGnmSubmitDone() ? SP_SYSTEM : SP_OK;
}
static unsigned native_fence_wait(NativeGraphics *gpu,NativeFence *f,uint64_t ns) {
    (void)gpu; if (!f->submitted) return 3;
    volatile uint64_t *label=(volatile uint64_t *)f->storage->map;
    if (*label==f->nonce) return 0;
    uint64_t start=sp_now_us(),us=ns/1000+(ns%1000!=0);
    while (*label!=f->nonce) {
        if (sp_now_us()-start>=us) return 2;
        sp_sleep_ms(1);
    }
    return 1;
}
#else
typedef struct NativeFence NativeFence;
static int native_fence_open(NativeGraphics *gpu,NativeFence **out) { (void)gpu; (void)out; return SP_UNSUPPORTED; }
static int native_fence_close(NativeGraphics *gpu,NativeFence **out) { (void)gpu; (void)out; return SP_UNSUPPORTED; }
static int native_fence_signal(NativeGraphics *gpu,NativeFence *f) { (void)gpu; (void)f; return SP_UNSUPPORTED; }
static unsigned native_fence_wait(NativeGraphics *gpu,NativeFence *f,uint64_t ns) { (void)gpu; (void)f; (void)ns; return 3; }
#endif
