#include "media.h"
#include "tex_decode.h"
#include <stdlib.h>
#include <string.h>
int sp_media_test(void) {
    unsigned checks = 0, failures = 0;
#define VERIFY(expr) do { ++checks; if (!(expr)) { ++failures; sp_log("MEDIA FAIL %d: %s", __LINE__, #expr); } } while (0)
    SpDs4Sample raw = {0,128,128,128,128,0,0,1}; NnPadState pad;
    sp_ds4_decode(&raw, &pad); VERIFY(!pad.buttons && !pad.lx && !pad.ly);
    const uint32_t buttons[16] = {0x2000,0x4000,0x1000,0x8000,2,4,0x400,0x800,0x100,0x200,8,0x100000,0x80,0x10,0x20,0x40};
    for (unsigned i = 0; i < 16; ++i) {
        raw.buttons = buttons[i]; sp_ds4_decode(&raw, &pad); VERIFY(pad.buttons == (1ULL << i));
    }
    raw.buttons = 0; raw.lx = 255; raw.ly = 0; sp_ds4_decode(&raw, &pad);
    VERIFY(pad.lx > 23000 && pad.ly > 23000 && pad.lx * (int64_t)pad.lx + pad.ly * (int64_t)pad.ly <= 32767LL * 32767);
    raw.lx = 0; raw.ly = 128; sp_ds4_decode(&raw, &pad); VERIFY(pad.lx == -32767 && !pad.ly);
    raw.connected = 0; sp_ds4_decode(&raw, &pad); VERIFY(!pad.buttons && !pad.lx && !pad.has_motion);
    SpPcmRing *ring = NULL; VERIFY(sp_pcm_create(&ring, 4) == SP_OK);
    if (ring) {
        int16_t input[] = {1,2,3,4,5,6,7,8,9,10}, output[12];
        VERIFY(sp_pcm_write(ring,input,3) == 3);
        VERIFY(sp_pcm_read(ring,output,2) == 2 && !memcmp(input,output,8));
        VERIFY(sp_pcm_write(ring,input+6,2) == 2);
        VERIFY(sp_pcm_read(ring,output,4) == 3 && output[0] == 5 && output[5] == 10 && !output[6] && !output[7]);
        VERIFY(sp_pcm_write(ring,input,5) == 4);
        uint64_t under = 0, over = 0; size_t queued = 0;
        sp_pcm_stats(ring,&under,&over,&queued); VERIFY(under == 1 && over == 1 && queued == 4);
        VERIFY(sp_pcm_destroy(&ring) == SP_OK);
    }
    /* Integrate an actual upstream graphics component with synthetic BC1 data. */
    uint8_t bc1[8] = {0x00,0xf8,0,0,0,0,0,0}, rgba[64] = {0};
    tex_decode_rgba8(TEX_FMT_RGB_DXT1,bc1,4,4,rgba);
    VERIFY(rgba[0] == 255 && !rgba[1] && !rgba[2] && rgba[3] == 255);
    for (unsigned i = 0; i < 16; ++i) VERIFY(!memcmp(rgba,rgba+i*4,4));
    size_t n = 320 * 180;
    uint32_t *pixels = malloc((n + 2) * sizeof *pixels);
    VERIFY(pixels != NULL);
    if (pixels) {
        pixels[0] = 0xdeadbeef; pixels[n+1] = 0xdeadbeef;
        SpUiState state = {"HOST TEST", "CLIPPED SOFTWARE FRAME", {0},0,0,0,0};
        sp_ui_draw(pixels+1,320,180,320,&state);
        VERIFY(pixels[0] == 0xdeadbeef && pixels[n+1] == 0xdeadbeef);
        free(pixels);
    }
    sp_log("MEDIA RESULT: %u checks, %u failures", checks, failures);
    return failures ? 1 : 0;
#undef VERIFY
}
