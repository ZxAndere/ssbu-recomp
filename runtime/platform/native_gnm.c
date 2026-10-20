/* Native PS4 queue bootstrap via the explicitly supplied OpenGNM library.
 * This is not a renderer. Initialization succeeds only after a GPU EOP write
 * becomes visible to the CPU; firmware/queue behavior requires real hardware. */
#include <orbis/libkernel.h>
#include <gnmdriver.h>
#include <gnm_commandbuffer.h>
#include <gnm_drawcommandbuffer.h>
typedef struct {
    void *memory; off_t offset; size_t size; unsigned submitted,completed;
    volatile uint64_t *last_label; uint64_t last_nonce;
} NativeGraphics;
static const char *native_graphics_name(void) { return "PS4 OpenGNM"; }
static int native_graphics_wait(NativeGraphics *gpu) {
    if (!gpu) return SP_INVALID;
    if (gpu->submitted==gpu->completed) return SP_OK;
    uint64_t start=sp_now_us();
    while (gpu->last_label && *gpu->last_label!=gpu->last_nonce && sp_now_us()-start<2000000) sp_sleep_ms(1);
    if (!gpu->last_label || *gpu->last_label!=gpu->last_nonce) return SP_TIMEOUT;
    gpu->completed=gpu->submitted; return SP_OK;
}
static int native_graphics_integer(NativeGraphics *gpu,unsigned query,int32_t *value) {
    /* Expose no unverified shader/resource limits from the new GNM path. */
    (void)gpu; (void)query; (void)value; return 0;
}
static void native_graphics_close(NativeGraphics **pointer) {
    NativeGraphics *gpu=*pointer; if (!gpu) return;
    if (gpu->submitted!=gpu->completed) {
        /* Never release memory still potentially referenced by the GPU. The
         * process retains this allocation until kernel process teardown. */
        sp_log("GAME GRAPHICS: retaining GPU allocation after unconfirmed EOP completion"); *pointer=NULL; return;
    }
    if (gpu->memory && sceKernelMunmap(gpu->memory,gpu->size)) {
        sp_log("GAME GRAPHICS: native GPU unmap failed; allocation retained"); *pointer=NULL; return;
    }
    if (gpu->offset>=0 && sceKernelReleaseDirectMemory(gpu->offset,gpu->size)) {
        sp_log("GAME GRAPHICS: native GPU release failed; allocation retained"); *pointer=NULL; return;
    }
    free(gpu); *pointer=NULL;
}
static int native_graphics_open(NativeGraphics **out) {
    NativeGraphics *gpu=calloc(1,sizeof *gpu); if (!gpu) return SP_SYSTEM;
    *out=gpu; gpu->offset=-1; gpu->size=2*1024*1024;
    int rc=sceKernelAllocateDirectMemory(0,(off_t)sceKernelGetDirectMemorySize(),gpu->size,2*1024*1024,3,&gpu->offset);
    if (!rc) rc=sceKernelMapDirectMemory(&gpu->memory,gpu->size,0x33,0,gpu->offset,2*1024*1024);
    if (rc) { sp_log("GAME GRAPHICS: GNM direct allocation/map failed rc=%x",(unsigned)rc); return SP_SYSTEM; }
    memset(gpu->memory,0,gpu->size);
    volatile uint64_t *label=(volatile uint64_t *)((unsigned char *)gpu->memory+gpu->size-8);
    uint64_t nonce=UINT64_C(0x5353425500000001)^sp_now_us(); if (!nonce) nonce=1;
    GnmCommandBuffer command=sceGnmCmdInit(gpu->memory,65536,NULL,NULL);
    sceGnmDrawCmdInitDefaultHardwareState(&command);
    /* Zero-count draw drives normal graphics EOP ordering without accessing
     * shaders, textures or render targets. It renders no test image. */
    sceGnmDrawCmdDrawIndexAuto(&command,0);
    sceGnmDrawCmdEventWriteEop(&command,GNM_CACHE_FLUSH_AND_INV_TS_EVENT,(uint64_t)(uintptr_t)label,GNM_DATA_SEL_SEND_DATA64,nonce);
    size_t used=(size_t)((unsigned char *)command.cmdptr-(unsigned char *)command.beginptr);
    if (!used || used>65536 || (used&3)) return SP_INVALID;
    uint32_t bytes=(uint32_t)used; void *address=command.beginptr;
    __asm__ volatile("sfence" ::: "memory");
    sp_log("GAME GRAPHICS: submitting native GNM startup commands bytes=%u",bytes);
    rc=sceGnmSubmitCommandBuffers(1,&address,&bytes,NULL,NULL);
    if (rc) { sp_log("GAME GRAPHICS: GNM submit failed rc=%x",(unsigned)rc); return SP_SYSTEM; }
    gpu->submitted=1;
    gpu->last_label=label; gpu->last_nonce=nonce;
    rc=sceGnmSubmitDone();
    if (rc) { sp_log("GAME GRAPHICS: GNM submit-done failed rc=%x",(unsigned)rc); return SP_SYSTEM; }
    uint64_t started=sp_now_us();
    while (*label!=nonce && sp_now_us()-started<2000000) sp_sleep_ms(1);
    if (*label!=nonce) { sp_log("GAME GRAPHICS: GNM startup EOP timed out; native initialization failed"); return SP_TIMEOUT; }
    gpu->completed=1;
    sp_log("GAME GRAPHICS: native GNM startup EOP completed; NVN rendering pending"); return SP_OK;
}
