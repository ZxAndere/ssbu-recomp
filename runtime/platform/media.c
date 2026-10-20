#include "media.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static void decode_stick(uint8_t raw_x, uint8_t raw_y, int *out_x, int *out_y) {
    float x = ((int)raw_x - 128) / (raw_x < 128 ? 128.0f : 127.0f);
    float y = (128 - (int)raw_y) / (raw_y < 128 ? 128.0f : 127.0f);
    float length = sqrtf(x * x + y * y);
    if (length <= 0.15f) { *out_x = *out_y = 0; return; }
    float magnitude = ((length > 1 ? 1 : length) - 0.15f) / 0.85f;
    *out_x = (int)(x / length * magnitude * 32767);
    *out_y = (int)(y / length * magnitude * 32767);
}
void sp_ds4_decode(const SpDs4Sample *sample, NnPadState *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
    if (!sample || !sample->connected) return;
    /* Nintendo position mapping: Circle=A, Cross=B, Triangle=X, Square=Y. */
    static const uint32_t source[16] = {0x2000,0x4000,0x1000,0x8000,2,4,0x400,0x800,
                                      0x100,0x200,8,0x100000,0x80,0x10,0x20,0x40};
    for (unsigned i = 0; i < 16; ++i) if (sample->buttons & source[i]) out->buttons |= 1ULL << i;
    if (sample->l2 >= 32) out->buttons |= 1ULL << 8;
    if (sample->r2 >= 32) out->buttons |= 1ULL << 9;
    decode_stick(sample->lx, sample->ly, &out->lx, &out->ly);
    decode_stick(sample->rx, sample->ry, &out->rx, &out->ry);
    /* Transport and motion axes are not inferred from a connected bit. */
}
struct SpPcmRing {
    SpMutex *mutex; int16_t *samples;
    size_t capacity, read, count;
    uint64_t underruns, rejected;
};
int sp_pcm_create(SpPcmRing **out, size_t capacity) {
    if (!out || *out || !capacity || capacity > SIZE_MAX / (2 * sizeof(int16_t))) return SP_INVALID;
    SpPcmRing *ring = calloc(1, sizeof *ring);
    if (!ring) return SP_SYSTEM;
    ring->samples = calloc(capacity, 2 * sizeof(int16_t));
    if (!ring->samples || sp_mutex_create(&ring->mutex)) {
        free(ring->samples); free(ring); return SP_SYSTEM;
    }
    ring->capacity = capacity; *out = ring; return SP_OK;
}
size_t sp_pcm_write(SpPcmRing *ring, const int16_t *data, size_t frames) {
    if (!ring || (!data && frames) || frames > SIZE_MAX / (2 * sizeof(int16_t))) return 0;
    if (sp_mutex_lock(ring->mutex)) return 0;
    size_t done = frames < ring->capacity - ring->count ? frames : ring->capacity - ring->count;
    size_t start = (ring->read + ring->count) % ring->capacity;
    size_t first = done < ring->capacity - start ? done : ring->capacity - start;
    if (first) memcpy(ring->samples + start * 2, data, first * 2 * sizeof(int16_t));
    if (done > first) memcpy(ring->samples, data + first * 2, (done - first) * 2 * sizeof(int16_t));
    ring->count += done; ring->rejected += frames - done;
    sp_mutex_unlock(ring->mutex); return done;
}
size_t sp_pcm_read(SpPcmRing *ring, int16_t *data, size_t frames) {
    if (!ring || (!data && frames) || frames > SIZE_MAX / (2 * sizeof(int16_t))) return 0;
    if (sp_mutex_lock(ring->mutex)) return 0;
    size_t done = frames < ring->count ? frames : ring->count;
    size_t first = done < ring->capacity - ring->read ? done : ring->capacity - ring->read;
    if (first) memcpy(data, ring->samples + ring->read * 2, first * 2 * sizeof(int16_t));
    if (done > first) memcpy(data + first * 2, ring->samples, (done - first) * 2 * sizeof(int16_t));
    if (frames > done) memset(data + done * 2, 0, (frames - done) * 2 * sizeof(int16_t));
    ring->read = (ring->read + done) % ring->capacity; ring->count -= done;
    ring->underruns += frames - done;
    sp_mutex_unlock(ring->mutex); return done;
}
void sp_pcm_stats(SpPcmRing *ring, uint64_t *under, uint64_t *over, size_t *queued) {
    if (!ring || sp_mutex_lock(ring->mutex)) return;
    if (under) *under = ring->underruns;
    if (over) *over = ring->rejected;
    if (queued) *queued = ring->count;
    sp_mutex_unlock(ring->mutex);
}
int sp_pcm_destroy(SpPcmRing **ring) {
    if (!ring || !*ring) return SP_INVALID;
    if (sp_mutex_destroy(&(*ring)->mutex)) return SP_SYSTEM;
    free((*ring)->samples); free(*ring); *ring = NULL; return SP_OK;
}
