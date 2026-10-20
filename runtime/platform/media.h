#ifndef SP_MEDIA_H
#define SP_MEDIA_H
#include "platform.h"
#include "../nnpad.h"
typedef struct {
    uint32_t buttons;
    uint8_t lx, ly, rx, ry, l2, r2, connected;
} SpDs4Sample;
void sp_ds4_decode(const SpDs4Sample *sample, NnPadState *out);
typedef struct SpPcmRing SpPcmRing;
int sp_pcm_create(SpPcmRing **out, size_t capacity_frames);
size_t sp_pcm_write(SpPcmRing *ring, const int16_t *stereo, size_t frames);
size_t sp_pcm_read(SpPcmRing *ring, int16_t *stereo, size_t frames);
void sp_pcm_stats(SpPcmRing *ring, uint64_t *underrun_frames, uint64_t *rejected_frames, size_t *queued);
int sp_pcm_destroy(SpPcmRing **ring);
typedef struct SpVideo SpVideo;
typedef struct SpAudio SpAudio;
int sp_video_open(SpVideo **out);
uint32_t *sp_video_pixels(SpVideo *video, unsigned *width, unsigned *height, unsigned *pitch);
int sp_video_present(SpVideo *video);
int sp_video_close(SpVideo **video);
int sp_audio_open(SpAudio **out);
size_t sp_audio_write(SpAudio *audio, const int16_t *stereo, size_t frames);
void sp_audio_stats(SpAudio *audio, uint64_t *underruns, uint64_t *rejected, size_t *queued, int *error);
int sp_audio_close(SpAudio **audio);
void sp_pad_shutdown(void);
typedef struct {
    const char *status, *detail;
    NnPadState pad;
    unsigned frame;
    int core_result, audio_result, pad_present;
} SpUiState;
void sp_ui_draw(uint32_t *pixels, unsigned width, unsigned height, unsigned pitch, const SpUiState *state);
#endif
