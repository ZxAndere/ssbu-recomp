#include "platform.h"
#include "media.h"
#include "guest.h"
#include <orbis/libkernel.h>
#include <math.h>
#include <string.h>

int sp_diagnostic(const char *fixture);
int sp_services_test(const char *fixture, const char *scratch);
int sp_media_test(void);
int sp_fiber_test(void);
int main(void) {
    if (sp_init("/data/ssbu-ps4-diagnostic.log")) sp_init(NULL);
    int result = sp_diagnostic("/app0/fixture.txt");
    result |= sp_services_test("/app0/fixture.txt", NULL);
    result |= sp_media_test();
    result |= sp_fiber_test();
    result |= sp_recomp_test("/app0/smoke.elf");
#ifdef SP_GAME_APP0
    int game_result=sp_game_bootstrap("/app0/main.elf");
#else
    int game_result=sp_game_bootstrap("/data/ssbu/main.elf");
#endif
    /* These tiny non-destructive probes expose the first game-host blocker.
     * A failure does not prevent the rest of the standalone diagnostic. */
    const uint64_t addresses[] = {0x7100000000ULL, 0x7300000000ULL};
    for (unsigned i = 0; i < 2; ++i) {
        SpMemory memory = {0};
        int rc = sp_memory_map(&memory, sp_page_size(), (void *)(uintptr_t)addresses[i]);
        sp_log("GUEST ADDRESS PROBE 0x%llx: status %d (0 is exact)",
               (unsigned long long)addresses[i], rc);
        if (!rc) sp_memory_unmap(&memory);
    }
    SpVideo *video = NULL; SpAudio *audio = NULL;
    int video_result = sp_video_open(&video), pad_result = nnpad_init();
    int audio_result = sp_audio_open(&audio);
    sp_log("Device initialization: video=%d pad=%d audio=%d", video_result, pad_result, audio_result);
    SpUiState state = {"PLATFORM AND RECOMPILER BOOTSTRAP", game_result==SP_IO ? "GAME MAIN.ELF MISSING IN /DATA/SSBU" :
        game_result==SP_OK ? "GUEST ENTRY RETURNED - SEE LOG" : "GAME BOOT STOPPED - SEE LOG", {0},0,result,audio_result,0};
    uint64_t previous = 0;
    SpUiState cached[2] = {0}; unsigned draws=0;
    for (unsigned frame = 0; video; ++frame) {
        nnpad_poll(0); state.pad_present = nnpad_get(0,&state.pad); state.frame = frame;
        uint64_t pressed = state.pad.buttons & ~previous; previous = state.pad.buttons;
        if ((pressed & 2) && audio) {
            int16_t tone[4800 * 2];
            for (unsigned i=0; i<4800; ++i) {
                float envelope = i < 240 ? i/240.0f : i > 4560 ? (4800-i)/240.0f : 1;
                tone[i*2] = tone[i*2+1] = (int16_t)(1000 * envelope * sinf(i * 6.283185307f * 440 / 48000));
            }
            sp_log("Queued %llu audio tone frames", (unsigned long long)sp_audio_write(audio,tone,4800));
        }
        if (pressed & (1ULL<<10)) break;
        unsigned width,height,pitch;
        uint32_t *pixels = sp_video_pixels(video,&width,&height,&pitch);
        SpUiState *last=&cached[frame&1];
        if (frame<2 || memcmp(&last->pad,&state.pad,sizeof state.pad) || last->pad_present!=state.pad_present ||
            last->audio_result!=state.audio_result || last->core_result!=state.core_result) {
            sp_ui_draw(pixels,width,height,pitch,&state); *last=state; ++draws;
        }
        int rc = sp_video_present(video);
        if (rc) { sp_log("Video present failed: %d",rc); break; }
        if (!(frame % 600)) {
            uint64_t under=0,over=0; size_t queued=0; int error=0;
            sp_audio_stats(audio,&under,&over,&queued,&error);
            state.audio_result=error ? SP_SYSTEM : audio_result;
            sp_log("Device status: frame=%u redraws=%u pad=%d audio-error=%08x queued=%llu silent=%llu rejected=%llu",
                   frame,draws,state.pad_present,(unsigned)error,(unsigned long long)queued,
                   (unsigned long long)under,(unsigned long long)over);
        }
        sp_sleep_ms(16);
    }
    if (audio) sp_log("Audio close: %d",sp_audio_close(&audio));
    sp_pad_shutdown();
    if (video) sp_log("Video close: %d",sp_video_close(&video));
    sp_log("Bootstrap finished, result %d. Close the title from the PS menu.", result);
    sp_shutdown();
    /* Keep a completed title idle. Returning through this SDK's CRT is not yet
     * validated on hardware. No busy spin, process kill or console shutdown. */
    for (;;) sceKernelUsleep(1000000);
}
