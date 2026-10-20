#include "media.h"
#include <stdio.h>
#include <string.h>
/* Original compact 5x7 glyphs. Coordinates are clipped before every pixel write. */
static const struct { char character; const char *rows; } font[] = {
 {'A',"01110100011000111111100011000110001"}, {'B',"11110100011000111110100011000111110"},
 {'C',"01111100001000010000100001000001111"}, {'D',"11110100011000110001100011000111110"},
 {'E',"11111100001000011110100001000011111"}, {'F',"11111100001000011110100001000010000"},
 {'G',"01111100001000010111100011000101111"}, {'H',"10001100011000111111100011000110001"},
 {'I',"11111001000010000100001000010011111"}, {'J',"00111000100001000010100101001001100"},
 {'K',"10001100101010011000101001001010001"}, {'L',"10000100001000010000100001000011111"},
 {'M',"10001110111010110101100011000110001"}, {'N',"10001110011010110011100011000110001"},
 {'O',"01110100011000110001100011000101110"}, {'P',"11110100011000111110100001000010000"},
 {'Q',"01110100011000110001101011001001101"}, {'R',"11110100011000111110101001001010001"},
 {'S',"01111100001000001110000010000111110"}, {'T',"11111001000010000100001000010000100"},
 {'U',"10001100011000110001100011000101110"}, {'V',"10001100011000110001100010101000100"},
 {'W',"10001100011000110101101011101110001"}, {'X',"10001100010101000100010101000110001"},
 {'Y',"10001100010101000100001000010000100"}, {'Z',"11111000010001000100010001000011111"},
 {'0',"01110100011001110101110011000101110"}, {'1',"00100011000010000100001000010001110"},
 {'2',"01110100010000100110010001000011111"}, {'3',"11110000010000101110000010000111110"},
 {'4',"00010001100101010010111110001000010"}, {'5',"11111100001000011110000010000111110"},
 {'6',"01110100001000011110100011000101110"}, {'7',"11111000010001000100010000100001000"},
 {'8',"01110100011000101110100011000101110"}, {'9',"01110100011000101111000010000101110"},
 {':',"00000001000010000000001000010000000"}, {'-',"00000000000000011111000000000000000"},
 {'.',"00000000000000000000000000011000110"}, {'/',"00001000100001000100010000100010000"},
};
static void rect(uint32_t *pixels, unsigned width, unsigned height, unsigned pitch,
                 int x, int y, int w, int h, uint32_t color) {
    for (int j = 0; j < h; ++j) for (int i = 0; i < w; ++i)
        if (x + i >= 0 && y + j >= 0 && (unsigned)(x + i) < width && (unsigned)(y + j) < height)
            pixels[(size_t)(y + j) * pitch + x + i] = color;
}
static void text(uint32_t *pixels, unsigned w, unsigned h, unsigned pitch,
                 int x, int y, int scale, uint32_t color, const char *message) {
    if (!message) return;
    for (unsigned n = 0; message[n] && n < 90; ++n) {
        char character = message[n];
        if (character >= 'a' && character <= 'z') character -= 'a' - 'A';
        for (unsigned g = 0; g < sizeof font / sizeof font[0]; ++g) if (font[g].character == character) {
            for (int j = 0; j < 7; ++j) for (int i = 0; i < 5; ++i)
                if (font[g].rows[j * 5 + i] == '1') rect(pixels, w, h, pitch, x + i * scale, y + j * scale, scale, scale, color);
            break;
        }
        x += 6 * scale;
    }
}
void sp_ui_draw(uint32_t *pixels, unsigned w, unsigned h, unsigned pitch, const SpUiState *state) {
    if (!pixels || !state || !w || !h || pitch < w || h > SIZE_MAX / pitch / sizeof(uint32_t)) return;
    const uint32_t white = 0xffeee9df, accent = 0xffdcb942;
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) pixels[(size_t)y * pitch + x] = 0xff261b16;
    int scale = w >= 1000 ? 3 : 1, left = (int)w / 16, y = (int)h / 12;
#define LABEL(msg,col) do { text(pixels,w,h,pitch,left,y,scale,col,msg); y += 12 * scale; } while (0)
    LABEL("SSBU NATIVE PS4 - DEVELOPMENT", accent);
    LABEL("PLATFORM AND RECOMPILER BOOTSTRAP", white);
    y += 8 * scale;
    LABEL(state->status, state->core_result ? 0xff6666ff : 0xff88dd88);
    LABEL(state->detail, white);
    LABEL("SMASH GAMEPLAY IS NOT RUNNING", white);
    y += 8 * scale;
    LABEL(state->pad_present ? "DUALSHOCK 4 CONNECTED" : "WAITING FOR DUALSHOCK 4", accent);
    LABEL(state->audio_result ? "AUDIO OUTPUT UNAVAILABLE" : "AUDIOOUT 48000 HZ STEREO", white);
    LABEL("CROSS - SHORT AUDIO TONE", white);
    LABEL("OPTIONS - RELEASE DEVICES AND IDLE", white);
    char line[80];
    snprintf(line, sizeof line, "BUTTONS %04X  STICK %d %d", (unsigned)state->pad.buttons, state->pad.lx, state->pad.ly);
    LABEL(line, white);
    rect(pixels,w,h,pitch,left,y,200 * scale,5 * scale,0xff54483b);
    int position = (state->pad.lx + 32767) * (180 * scale) / 65534;
    rect(pixels,w,h,pitch,left + position,y,20 * scale,5 * scale,accent);
#undef LABEL
}
