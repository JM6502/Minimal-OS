#include <assert.h>
#include <limits.h>
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

static void test_hostile_clip_and_alpha(void) {
    gfx_rect_t a = { INT32_MIN, 0, 4, 2 };
    gfx_rect_t b = { INT32_MIN + 1, 0, 4, 2 };
    gfx_rect_t out = { 0, 0, 0, 0 };
    assert(gfx_clip_rect(&a, &b, &out));
    assert(out.x == INT32_MIN + 1);
    assert(out.w == 3);

    uint32_t fill_dst[4 * 4] = { 0 };
    gfx_rect_t hostile_fill = { INT32_MIN, INT32_MIN, 10, 10 };
    gfx_fill(fill_dst, 4, 4, 4, &hostile_fill, 0x11223344u);
    assert(fill_dst[0] == 0u);

    uint32_t alpha_dst[4] = { 0xFF000000u, 0xFF000000u, 0xFF000000u, 0xFF000000u };
    uint32_t alpha_src[4] = { 0x00000000u, 0xFFFFFFFFu, 0x80000000u, 0x80008080u };
    gfx_blit_alpha(alpha_dst, 2, 2, 2, alpha_src, 2, 2, 0, 0, 2, 2, -1, 0, 2, 2);
    assert(alpha_dst[0] == 0xFFFFFFFFu);
    assert(alpha_dst[1] == 0xFF000000u);

    uint32_t alpha_dst2[2] = { 0x00000000u, 0x00000000u };
    uint32_t alpha_src2[2] = { 0xFFFFFFFFu, 0x00000000u };
    gfx_blit_alpha(alpha_dst2, 2, 1, 2, alpha_src2, 2, 1, 0, 0, 2, 1, 0, 0, 2, 1);
    assert(alpha_dst2[0] == 0xFFFFFFFFu);
    assert(alpha_dst2[1] == 0x00000000u);
}

int main(void) {
    test_clip_rect();
    test_fill_and_blit();
    test_hostile_clip_and_alpha();
    puts("gfx_blit_tests: PASS");
    return 0;
}
