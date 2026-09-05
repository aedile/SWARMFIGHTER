/*
 * galaga_video.c - starfield (05XX), sprites and tile layer, per MAME galaga_v.cpp
 */
#include "galaga_internal.h"
#include <string.h>

static const ga_roms_t *R;
static uint16_t palette[GA_PALETTE_SIZE];
static uint8_t char_pen[256];      /* color*4+pix -> palette index, 0xff = transparent */
static uint8_t tile_blank[256];    /* 1 if every pixel of the tile is 0 */
static uint8_t sprite_pen[256];

/* 05XX starfield */
#define LFSR_HIT_MASK  0xFA14
#define LFSR_HIT_VALUE 0x7800
#define LFSR_SEED      0x7FFF
#define STAR_X_OFFSET  16
#define STAR_X_LIMIT   (256 + STAR_X_OFFSET)
/* The 16-bit LFSR cycles through 65535 states. Instead of stepping it per pixel we
 * track our position in that cycle and keep a sorted table of the positions where a
 * "hit" (a star) occurs, with the state at each hit. Per frame that is ~256 lookups
 * instead of ~65,000 shift-register steps. */
#define LFSR_PERIOD 65535
static uint32_t lfsr_idx;                 /* position in the cycle; 0 = LFSR_SEED */
static uint16_t hit_pos[512], hit_state[512];
static int nhits;
static int star_enable, star_set_a, star_set_b;
static int pre_vis_cycles, post_vis_cycles;
static const int speed_x_offset[8] = { 0, 1, 2, 3, -4, -3, -2, -1 };
static const int pre_vis_values[8]  = { 22*256, 23*256, 22*256, 23*256, 19*256, 20*256, 20*256, 22*256 };
static const int post_vis_values[8] = { 10*256, 10*256, 12*256, 12*256,  9*256,  9*256, 10*256,  9*256 };

static void build_hit_table(void);

static uint16_t rgb565(int r, int g, int b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void ga_video_init(const ga_roms_t *roms)
{
    R = roms;
    build_hit_table();
    /* 32-color PROM: 3 bits R (1k/470/220), 3 bits G, 2 bits B (470/220) */
    for (int i = 0; i < 32; i++) {
        uint8_t v = R->prom_palette[i];
        int r = ((v >> 0) & 1) * 0x21 + ((v >> 1) & 1) * 0x47 + ((v >> 2) & 1) * 0x97;
        int g = ((v >> 3) & 1) * 0x21 + ((v >> 4) & 1) * 0x47 + ((v >> 5) & 1) * 0x97;
        int b = ((v >> 6) & 1) * 0x51 + ((v >> 7) & 1) * 0xAE;
        palette[i] = rgb565(r, g, b);
    }
    /* 64 star colors: 2 bits per channel */
    for (int i = 0; i < 64; i++) {
        int r = ((i >> 0) & 1) * 0x55 + ((i >> 1) & 1) * 0xAA;
        int g = ((i >> 2) & 1) * 0x55 + ((i >> 3) & 1) * 0xAA;
        int b = ((i >> 4) & 1) * 0x55 + ((i >> 5) & 1) * 0xAA;
        palette[32 + i] = rgb565(r, g, b);
    }
    palette[96] = 0;
    for (int t = 0; t < 256; t++) {
        int blank = 1;
        for (int k = 0; k < 64; k++) if (R->tiles[t * 64 + k]) { blank = 0; break; }
        tile_blank[t] = (uint8_t)blank;
    }
    for (int i = 0; i < 256; i++) {
        uint8_t c = R->prom_charlut[i] & 0x0f;
        char_pen[i] = (c == 0x0f) ? 0xff : (uint8_t)(0x10 | c);
        uint8_t s = R->prom_spritelut[i] & 0x0f;
        sprite_pen[i] = (s == 0x0f) ? 0xff : s;
    }
    ga_video_reset();
}

static inline uint16_t lfsr_next(uint16_t v)
{
    uint16_t bit = ((v >> 0) ^ (v >> 3) ^ (v >> 5) ^ (v >> 10)) & 1;
    return (uint16_t)((v >> 1) | (bit << 15));
}

static void build_hit_table(void)
{
    uint16_t v = LFSR_SEED;
    nhits = 0;
    for (uint32_t i = 0; i < LFSR_PERIOD; i++) {
        if ((v & LFSR_HIT_MASK) == LFSR_HIT_VALUE && nhits < 512) { hit_pos[nhits] = (uint16_t)i; hit_state[nhits] = v; nhits++; }
        v = lfsr_next(v);
    }
}

void ga_video_reset(void)
{
    lfsr_idx = 0;
    star_enable = 0; star_set_a = star_set_b = 0;
    pre_vis_cycles = pre_vis_values[0]; post_vis_cycles = post_vis_values[0];
}

const uint16_t *ga_palette(void) { return palette; }

/* latch the video control bits at the end of the visible frame, like screen_vblank_galaga */
void ga_video_vblank(void)
{
    uint8_t l = ga_videolatch;
    int speed_x = l & 7;
    pre_vis_cycles = pre_vis_values[0] + speed_x_offset[speed_x];
    post_vis_cycles = post_vis_values[0];
    star_set_a = (l >> 3) & 1;
    star_set_b = ((l >> 4) & 1) | 2;
    int on = (l >> 5) & 1;
    if (!on) lfsr_idx = 0;
    star_enable = on;
}

/* first hit-table entry at or after cycle position p */
static int first_hit_from(uint32_t p)
{
    int lo = 0, hi = nhits;
    while (lo < hi) { int mid = (lo + hi) >> 1; if (hit_pos[mid] < p) lo = mid + 1; else hi = mid; }
    return lo;
}

static void draw_starfield(uint8_t *fb)
{
    if (!star_enable) return;
    lfsr_idx = (lfsr_idx + (uint32_t)pre_vis_cycles) % LFSR_PERIOD;
    const uint32_t span = (uint32_t)GA_FB_H * 256;    /* visible LFSR steps this frame */
    uint32_t start = lfsr_idx;
    /* walk hits in [start, start+span) around the cycle */
    int i = first_hit_from(start);
    for (uint32_t consumed = 0; consumed < span; ) {
        if (i >= nhits) { i = 0; }
        uint32_t pos = hit_pos[i];
        uint32_t off = (pos + LFSR_PERIOD - start) % LFSR_PERIOD;
        if (off >= span) break;
        uint16_t v = hit_state[i];
        int star_set = ((v >> 10) & 1) << 1 | ((v >> 8) & 1);
        int y = (int)(off >> 8), x = (int)(off & 0xff) + STAR_X_OFFSET;
        if ((star_set_a == star_set || star_set_b == star_set) && x < STAR_X_LIMIT) {
            uint8_t color = (v >> 5) & 0x7;
            color |= (v << 3) & 0x18;
            color |= (v << 2) & 0x20;
            color = (~color) & 0x3f;
            fb[y * GA_FB_W + x] = (uint8_t)(32 + color);
        }
        consumed = off + 1;
        i++;
        if (i >= nhits && nhits) { i = 0; if (start == 0) break; }
        if (consumed == 0) break;
    }
    lfsr_idx = (start + span + (uint32_t)post_vis_cycles) % LFSR_PERIOD;
}

static void draw_sprite_tile(uint8_t *fb, int code, int color, int flipx, int flipy, int sx, int sy)
{
    const uint8_t *src = R->sprites + (code & 0x7f) * 256;
    const uint8_t *pens = sprite_pen + (color & 0x3f) * 4;
    for (int y = 0; y < 16; y++) {
        int dy = sy + y;
        if (dy < 0 || dy >= GA_FB_H) continue;
        const uint8_t *srow = src + (flipy ? 15 - y : y) * 16;
        uint8_t *drow = fb + dy * GA_FB_W;
        for (int x = 0; x < 16; x++) {
            int dx = sx + x;
            if (dx < 0 || dx >= GA_FB_W) continue;
            uint8_t pen = pens[srow[flipx ? 15 - x : x]];
            if (pen != 0xff) drow[dx] = pen;
        }
    }
}

static void draw_sprites(uint8_t *fb)
{
    const uint8_t *s1 = ga_ram1 + 0x380, *s2 = ga_ram2 + 0x380, *s3 = ga_ram3 + 0x380;
    static const int gfx_offs[2][2] = { { 0, 1 }, { 2, 3 } };
    for (int offs = 0; offs < 0x80; offs += 2) {
        int sprite = s1[offs] & 0x7f;
        int color = s1[offs + 1] & 0x3f;
        int sx = s2[offs + 1] - 40 + 0x100 * (s3[offs + 1] & 3);
        int sy = 256 - s2[offs] + 1;
        int flipx = s3[offs] & 1;
        int flipy = (s3[offs] >> 1) & 1;
        int sizex = (s3[offs] >> 2) & 1;
        int sizey = (s3[offs] >> 3) & 1;
        sy -= 16 * sizey;
        sy = (sy & 0xff) - 32;
        for (int y = 0; y <= sizey; y++)
            for (int x = 0; x <= sizex; x++)
                draw_sprite_tile(fb, sprite + gfx_offs[y ^ (sizey * flipy)][x ^ (sizex * flipx)],
                                 color, flipx, flipy, sx + 16 * x, sy + 16 * y);
    }
}

/* 36x28 tile layer; the tilemap scan converts (col,row) to the 32x32 VRAM layout */
static inline int tilemap_scan(int col, int row)
{
    row += 2; col -= 2;
    if (col & 0x20) return row + ((col & 0x1f) << 5);
    return col + (row << 5);
}

static void draw_tiles(uint8_t *fb)
{
    for (int row = 0; row < 28; row++) {
        for (int col = 0; col < 36; col++) {
            int idx = tilemap_scan(col, row) & 0x3ff;
            int code = ga_videoram[idx] & 0x7f;
            int color = ga_videoram[idx + 0x400] & 0x3f;
            const uint8_t *pens = char_pen + color * 4;
            if (tile_blank[code] && pens[0] == 0xff) continue;     /* nothing to draw */
            const uint8_t *src = R->tiles + code * 64;
            uint8_t *dst = fb + (row * 8) * GA_FB_W + col * 8;
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    uint8_t pen = pens[src[y * 8 + x]];
                    if (pen != 0xff) dst[x] = pen;
                }
                dst += GA_FB_W;
            }
        }
    }
}

void ga_video_render(uint8_t *fb)
{
    memset(fb, 96, GA_FB_W * GA_FB_H);
    draw_starfield(fb);
    draw_sprites(fb);
    draw_tiles(fb);
}
