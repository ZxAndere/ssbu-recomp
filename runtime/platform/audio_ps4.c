#include "media.h"
#include <orbis/AudioOut.h>
#include <stdlib.h>
struct SpAudio { SpPcmRing *ring; SpMutex *lock; SpThread *thread; int port, stop, error; };
static void output_worker(void *argument) {
    SpAudio *audio = argument;
    int16_t samples[256 * 2];
    for (;;) {
        sp_mutex_lock(audio->lock);
        int stop = audio->stop;
        sp_mutex_unlock(audio->lock);
        if (stop) break;
        int rc = sceAudioOutOutput(audio->port, NULL);
        if (rc < 0) {
            sp_mutex_lock(audio->lock); audio->error = rc; sp_mutex_unlock(audio->lock); break;
        }
        sp_pcm_read(audio->ring, samples, 256);
        /* Output is paced by the device; no polling or unbounded queue. */
        rc = sceAudioOutOutput(audio->port, samples);
        if (rc < 0) {
            sp_mutex_lock(audio->lock); audio->error = rc; sp_mutex_unlock(audio->lock); break;
        }
    }
    sceAudioOutOutput(audio->port, NULL);
}
int sp_audio_open(SpAudio **out) {
    if (!out || *out) return SP_INVALID;
    SpAudio *audio = calloc(1, sizeof *audio);
    if (!audio) return SP_SYSTEM;
    audio->port = -1;
    int rc = sceAudioOutInit();
    if (rc) { free(audio); return SP_SYSTEM; }
    audio->port = sceAudioOutOpen(ORBIS_USER_SERVICE_USER_ID_SYSTEM, ORBIS_AUDIO_OUT_PORT_TYPE_MAIN,
                                 0, 256, 48000, ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
    if (audio->port < 0 || sp_pcm_create(&audio->ring, 8192) || sp_mutex_create(&audio->lock) ||
        sp_thread_start(&audio->thread, output_worker, audio)) {
        if (audio->lock) sp_mutex_destroy(&audio->lock);
        if (audio->ring) sp_pcm_destroy(&audio->ring);
        if (audio->port >= 0) sceAudioOutClose(audio->port);
        free(audio); return SP_SYSTEM;
    }
    *out = audio; return SP_OK;
}
size_t sp_audio_write(SpAudio *audio, const int16_t *stereo, size_t frames) {
    return audio ? sp_pcm_write(audio->ring, stereo, frames) : 0;
}
void sp_audio_stats(SpAudio *audio, uint64_t *under, uint64_t *over, size_t *queued, int *error) {
    if (!audio) return;
    sp_pcm_stats(audio->ring, under, over, queued);
    if (error) { sp_mutex_lock(audio->lock); *error = audio->error; sp_mutex_unlock(audio->lock); }
}
int sp_audio_close(SpAudio **audio) {
    if (!audio || !*audio) return SP_INVALID;
    sp_mutex_lock((*audio)->lock); (*audio)->stop = 1; sp_mutex_unlock((*audio)->lock);
    if (sp_thread_join(&(*audio)->thread)) return SP_SYSTEM;
    int rc = sceAudioOutClose((*audio)->port);
    sp_pcm_destroy(&(*audio)->ring); sp_mutex_destroy(&(*audio)->lock);
    free(*audio); *audio = NULL; return rc < 0 ? SP_SYSTEM : SP_OK;
}
