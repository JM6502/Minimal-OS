#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "gfx_blit.h"

static void test_clip_rect(void) {
    gfx_rect_t a = { 0, 0, 10, 10 };
    gfx_rect_t b = { -5, 2, 20, 6 };
    gfx_rect_t out = { 0, 0, 0, 0 };

    assert(gfx_clip_rect(&a, &b, &out));
    assert(out.x == 0);
    assert(out.y == 2);
    assert(out.w == 10);
    assert(out.h == 6);

    gfx_rect_t c = { 0, 0, 0, 0 };
    assert(!gfx_clip_rect(&a, &c, &out));

    gfx_rect_t d = { 100, 100, 10, 10 };
    gfx_rect_t e = { 0, 0, 10, 10 };
    assert(!gfx_clip_rect(&d, &e, &out));
}

static void test_fill_and_blit(void) {
    uint32_t dst[4 * 4] = { 0 };
    gfx_rect_t rect = { 1, 1, 2, 2 };
    gfx_fill(dst, 4, 4, 4, &rect, 0x00FF00FFu);
    assert(dst[1 * 4 + 1] == 0x00FF00FFu);

    uint32_t src[4 * 4];
    for (int i = 0; i < 16; ++i) {
        src[i] = (uint32_t)(0x01000000u | (uint32_t)i);
    }

    gfx_blit_opaque(dst, 4, 4, 4, src, 4, 4, 0, 0, 2, 2, 0, 0, 2, 2);
    assert(dst[0] == 0x01000000u);
    assert(dst[1 * 4 + 1] == ((uint32_t)0x01000000u | 5u));

    uint32_t alpha_dst[4 * 4] = { 0xFFFF0000u };
    uint32_t alpha_src[4 * 4];
    for (int i = 0; i < 16; ++i) {
        alpha_src[i] = 0x80000000u | (uint32_t)i;
    }

    gfx_blit_alpha(alpha_dst, 4, 4, 4, alpha_src, 4, 4, 0, 0, 2, 2, 0, 0, 2, 2);
    assert(alpha_dst[0] != 0xFFFF0000u);
}

int main(void) {
    test_clip_rect();
    test_fill_and_blit();
    puts("gfx_blit_tests: PASS");
    return 0;
}
