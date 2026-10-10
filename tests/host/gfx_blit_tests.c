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

    gfx_rect_t huge = { INT32_MAX - 1, 0, 8, 1 };
    gfx_rect_t overlapping = { INT32_MAX - 2, 0, 8, 1 };
    assert(gfx_clip_rect(&huge, &overlapping, &out));
    assert(out.x == INT32_MAX - 1);
    assert(out.w == 7);
}

static void test_fill_and_blit(void) {
    uint32_t dst[4 * 4] = { 0 };
    uint32_t src[4 * 4];
    gfx_surface_t dst_surface = { dst, sizeof(dst), 4, 4, 4 };
    gfx_surface_t src_surface = { src, sizeof(src), 4, 4, 4 };
    gfx_rect_t rect = { 1, 1, 2, 2 };
    assert(gfx_fill(&dst_surface, &rect, 0x00FF00FFu));
    assert(dst[1 * 4 + 1] == 0x00FF00FFu);

    for (int i = 0; i < 16; ++i) {
        src[i] = (uint32_t)(0x01000000u | (uint32_t)i);
    }

    gfx_rect_t source_rect = { 0, 0, 2, 2 };
    assert(gfx_blit_opaque(&dst_surface, &src_surface, &source_rect, 0, 0));
    assert(dst[0] == 0x01000000u);
    assert(dst[1 * 4 + 1] == ((uint32_t)0x01000000u | 5u));

    uint32_t alpha_dst[4 * 4] = { 0xFFFF0000u };
    uint32_t alpha_src[4 * 4];
    gfx_surface_t alpha_dst_surface = { alpha_dst, sizeof(alpha_dst), 4, 4, 4 };
    gfx_surface_t alpha_src_surface = { alpha_src, sizeof(alpha_src), 4, 4, 4 };
    for (int i = 0; i < 16; ++i) {
        alpha_src[i] = 0x80000000u | (uint32_t)i;
    }

    assert(gfx_blit_alpha(&alpha_dst_surface, &alpha_src_surface,
                          &source_rect, 0, 0));
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
    gfx_surface_t fill_surface = { fill_dst, sizeof(fill_dst), 4, 4, 4 };
    gfx_rect_t hostile_fill = { INT32_MIN, INT32_MIN, 10, 10 };
    assert(gfx_fill(&fill_surface, &hostile_fill, 0x11223344u));
    assert(fill_dst[0] == 0u);

    uint32_t alpha_dst[4] = { 0xFF000000u, 0xFF000000u, 0xFF000000u, 0xFF000000u };
    uint32_t alpha_src[4] = { 0x00000000u, 0xFFFFFFFFu, 0x80000000u, 0x80008080u };
    gfx_surface_t dst_surface = { alpha_dst, sizeof(alpha_dst), 2, 2, 2 };
    gfx_surface_t src_surface = { alpha_src, sizeof(alpha_src), 2, 2, 2 };
    gfx_rect_t rect = { 0, 0, 2, 2 };
    assert(gfx_blit_alpha(&dst_surface, &src_surface, &rect, -1, 0));
    assert(alpha_dst[0] == 0xFFFFFFFFu);
    assert(alpha_dst[1] == 0xFF000000u);

    uint32_t alpha_dst2[2] = { 0x00000000u, 0x00000000u };
    uint32_t alpha_src2[2] = { 0xFFFFFFFFu, 0x00000000u };
    gfx_surface_t dst_surface2 = { alpha_dst2, sizeof(alpha_dst2), 2, 1, 2 };
    gfx_surface_t src_surface2 = { alpha_src2, sizeof(alpha_src2), 2, 1, 2 };
    gfx_rect_t rect2 = { 0, 0, 2, 1 };
    assert(gfx_blit_alpha(&dst_surface2, &src_surface2, &rect2, 0, 0));
    assert(alpha_dst2[0] == 0xFFFFFFFFu);
    assert(alpha_dst2[1] == 0x00000000u);
}

static void test_surface_bounds_and_present(void) {
    uint32_t dst[6] = { 0 };
    uint32_t src[6] = { 1, 2, 3, 4, 5, 6 };
    gfx_surface_t dst_surface = { dst, sizeof(dst), 2, 2, 3 };
    gfx_surface_t src_surface = { src, sizeof(src), 2, 2, 3 };
    gfx_rect_t rect = { 0, 0, 2, 2 };

    assert(gfx_surface_valid(&dst_surface));
    assert(gfx_present_rect(&dst_surface, &src_surface, &rect));
    assert(dst[0] == 1 && dst[1] == 2);
    assert(dst[3] == 4 && dst[4] == 5);
    assert(dst[2] == 0 && dst[5] == 0);

    dst_surface.byte_length = 4 * sizeof(uint32_t);
    assert(!gfx_surface_valid(&dst_surface));

    gfx_surface_t overflow = {
        dst, SIZE_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX
    };
    assert(!gfx_surface_valid(&overflow));
}

int main(void) {
    test_clip_rect();
    test_fill_and_blit();
    test_hostile_clip_and_alpha();
    test_surface_bounds_and_present();
    puts("gfx_blit_tests: PASS");
    return 0;
}
