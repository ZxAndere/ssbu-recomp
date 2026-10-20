#include "media.h"
#include <orbis/libkernel.h>
#include <orbis/VideoOut.h>
#include <orbis/SystemService.h>
#include <stdlib.h>
struct SpVideo {
    int handle, registered; void *memory; off_t offset; size_t size, stride;
    unsigned buffer; int64_t sequence;
};
int sp_video_open(SpVideo **out) {
    if (!out || *out) return SP_INVALID;
    SpVideo *video = calloc(1, sizeof *video);
    if (!video) return SP_SYSTEM;
    video->handle = -1; video->offset = -1;
    video->stride = (1920 * 1080 * 4 + 0x3fff) & ~(size_t)0x3fff;
    video->size = video->stride * 2;
    video->handle = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, NULL);
    int rc = video->handle < 0 ? video->handle :
        sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), video->size, 0x4000, 3, &video->offset);
    if (!rc) rc = sceKernelMapDirectMemory(&video->memory, video->size, 0x33, 0, video->offset, 0x4000);
    if (!rc) {
        void *buffers[] = {video->memory, (char *)video->memory + video->stride};
        OrbisVideoOutBufferAttribute attribute = {0};
        sceVideoOutSetBufferAttribute(&attribute, ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB,
                                     ORBIS_VIDEO_OUT_TILING_MODE_LINEAR, 0, 1920, 1080, 1920);
        rc = sceVideoOutRegisterBuffers(video->handle, 0, buffers, 2, &attribute);
        if (!rc) video->registered = 1;
    }
    if (!rc) rc = sceVideoOutSetFlipRate(video->handle, 0);
    if (rc) {
        sp_log("Video initialization failed: 0x%08x", (unsigned)rc);
        sp_video_close(&video); return SP_SYSTEM;
    }
    sceSystemServiceHideSplashScreen();
    sp_log("Video initialized: 1920x1080, two linear buffers, %llu bytes", (unsigned long long)video->size);
    *out = video; return SP_OK;
}
uint32_t *sp_video_pixels(SpVideo *video, unsigned *width, unsigned *height, unsigned *pitch) {
    if (!video || !video->memory) return NULL;
    if (width) *width = 1920;
    if (height) *height = 1080;
    if (pitch) *pitch = 1920;
    return (uint32_t *)((char *)video->memory + video->stride * video->buffer);
}
int sp_video_present(SpVideo *video) {
    if (!video || !video->registered) return SP_INVALID;
    int64_t sequence = ++video->sequence;
    int rc = sceVideoOutSubmitFlip(video->handle, (int)video->buffer, ORBIS_VIDEO_OUT_FLIP_VSYNC, sequence);
    if (rc) return SP_SYSTEM;
    uint64_t start = sp_now_us();
    for (;;) {
        OrbisVideoOutFlipStatus status = {0};
        if (sceVideoOutGetFlipStatus(video->handle, &status)) return SP_SYSTEM;
        if (status.flipArg == sequence) break;
        if (sp_now_us() - start > 2000000) return SP_TIMEOUT;
        sp_sleep_ms(1);
    }
    video->buffer ^= 1;
    return SP_OK;
}
int sp_video_close(SpVideo **out) {
    if (!out || !*out) return SP_INVALID;
    SpVideo *video = *out;
    /* A failed close retains the allocation, rather than freeing scanout memory. */
    if (video->handle >= 0) {
        if (video->registered) sceVideoOutUnregisterBuffers(video->handle, 0);
        if (sceVideoOutClose(video->handle)) return SP_SYSTEM;
        video->handle = -1;
    }
    if (video->memory && sceKernelMunmap(video->memory, video->size)) return SP_SYSTEM;
    video->memory = NULL;
    if (video->offset >= 0 && sceKernelReleaseDirectMemory(video->offset, video->size)) return SP_SYSTEM;
    free(video); *out = NULL; return SP_OK;
}
