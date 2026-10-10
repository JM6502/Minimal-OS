#include "gfx_blit.h"

static inline int32_t gfx_min_i32(int32_t a, int32_t b) {
    return a < b ? a : b;
}

static inline int32_t gfx_max_i32(int32_t a, int32_t b) {
    return a > b ? a : b;
}

static uint32_t gfx_blend_argb(uint32_t src, uint32_t dst) {
    const uint8_t src_a = (uint8_t)((src >> 24) & 0xFFu);
    const uint8_t src_r = (uint8_t)((src >> 16) & 0xFFu);
    const uint8_t src_g = (uint8_t)((src >> 8) & 0xFFu);
    const uint8_t src_b = (uint8_t)(src & 0xFFu);

    const uint8_t dst_a = (uint8_t)((dst >> 24) & 0xFFu);
    const uint8_t dst_r = (uint8_t)((dst >> 16) & 0xFFu);
    const uint8_t dst_g = (uint8_t)((dst >> 8) & 0xFFu);
    const uint8_t dst_b = (uint8_t)(dst & 0xFFu);

    if (src_a == 0u) {
        return dst;
    }
    if (src_a == 255u) {
        return src;
    }

    const uint32_t inv = 255u - (uint32_t)src_a;
    const uint32_t out_a = (uint32_t)src_a + ((uint32_t)dst_a * inv) / 255u;
    const uint32_t out_r = ((uint32_t)src_r * (uint32_t)src_a + (uint32_t)dst_r * inv) / 255u;
    const uint32_t out_g = ((uint32_t)src_g * (uint32_t)src_a + (uint32_t)dst_g * inv) / 255u;
    const uint32_t out_b = ((uint32_t)src_b * (uint32_t)src_a + (uint32_t)dst_b * inv) / 255u;

    return (out_a << 24) | (out_r << 16) | (out_g << 8) | out_b;
}

bool gfx_clip_rect(const gfx_rect_t* a, const gfx_rect_t* b, gfx_rect_t* out) {
    if (!a || !b || !out) {
        return false;
    }
    if (a->w <= 0 || a->h <= 0 || b->w <= 0 || b->h <= 0) {
        return false;
    }

    const int32_t left = gfx_max_i32(a->x, b->x);
    const int32_t top = gfx_max_i32(a->y, b->y);
    const int32_t right = gfx_min_i32(a->x + a->w, b->x + b->w);
    const int32_t bottom = gfx_min_i32(a->y + a->h, b->y + b->h);

    if (right <= left || bottom <= top) {
        return false;
    }

    out->x = left;
    out->y = top;
    out->w = right - left;
    out->h = bottom - top;
    return true;
}

void gfx_fill(uint32_t* dst, uint32_t width, uint32_t height, uint32_t stride,
              const gfx_rect_t* rect, uint32_t color) {
    if (!dst || !rect || width == 0u || height == 0u || stride < width) {
        return;
    }
    if (rect->w <= 0 || rect->h <= 0) {
        return;
    }

    const int32_t x0 = gfx_max_i32(0, rect->x);
    const int32_t y0 = gfx_max_i32(0, rect->y);
    const int32_t x1 = gfx_min_i32((int32_t)width, rect->x + rect->w);
    const int32_t y1 = gfx_min_i32((int32_t)height, rect->y + rect->h);

    if (x1 <= x0 || y1 <= y0) {
        return;
    }

    for (int32_t y = y0; y < y1; ++y) {
        uint32_t* row = dst + ((uint32_t)y * stride);
        for (int32_t x = x0; x < x1; ++x) {
            row[x] = color;
        }
    }
}

void gfx_blit_opaque(uint32_t* dst, uint32_t dst_w, uint32_t dst_h, uint32_t dst_pitch,
                     const uint32_t* src, uint32_t src_w, uint32_t src_h,
                     int32_t src_x, int32_t src_y, int32_t src_wi, int32_t src_hi,
                     int32_t dst_x, int32_t dst_y, int32_t clip_w, int32_t clip_h) {
    if (!dst || !src || dst_w == 0u || dst_h == 0u || src_w == 0u || src_h == 0u) {
        return;
    }
    if (src_wi <= 0 || src_hi <= 0 || clip_w <= 0 || clip_h <= 0) {
        return;
    }

    const int32_t max_y = gfx_min_i32(src_hi, (int32_t)src_h - src_y);
    const int32_t max_x = gfx_min_i32(src_wi, (int32_t)src_w - src_x);
    const int32_t copy_h = gfx_min_i32(max_y, clip_h);
    const int32_t copy_w = gfx_min_i32(max_x, clip_w);

    for (int32_t y = 0; y < copy_h; ++y) {
        const int32_t src_row = src_y + y;
        const int32_t dst_row = dst_y + y;
        if (src_row < 0 || src_row >= (int32_t)src_h || dst_row < 0 || dst_row >= (int32_t)dst_h) {
            continue;
        }
        const uint32_t* src_ptr = src + (uint32_t)src_row * src_w;
        uint32_t* dst_ptr = dst + (uint32_t)dst_row * dst_pitch;
        const int32_t x_start = gfx_max_i32(0, src_x);
        const int32_t x_end = gfx_min_i32((int32_t)src_w, src_x + copy_w);
        if (x_start >= x_end) {
            continue;
        }
        for (int32_t x = x_start; x < x_end; ++x) {
            const int32_t dst_col = dst_x + (x - src_x);
            if (dst_col < 0 || dst_col >= (int32_t)dst_w) {
                continue;
            }
            dst_ptr[dst_col] = src_ptr[x];
        }
    }
}

void gfx_blit_alpha(uint32_t* dst, uint32_t dst_w, uint32_t dst_h, uint32_t dst_pitch,
                    const uint32_t* src, uint32_t src_w, uint32_t src_h,
                    int32_t src_x, int32_t src_y, int32_t src_wi, int32_t src_hi,
                    int32_t dst_x, int32_t dst_y, int32_t clip_w, int32_t clip_h) {
    if (!dst || !src || dst_w == 0u || dst_h == 0u || src_w == 0u || src_h == 0u) {
        return;
    }
    if (src_wi <= 0 || src_hi <= 0 || clip_w <= 0 || clip_h <= 0) {
        return;
    }

    const int32_t max_y = gfx_min_i32(src_hi, (int32_t)src_h - src_y);
    const int32_t max_x = gfx_min_i32(src_wi, (int32_t)src_w - src_x);
    const int32_t copy_h = gfx_min_i32(max_y, clip_h);
    const int32_t copy_w = gfx_min_i32(max_x, clip_w);

    for (int32_t y = 0; y < copy_h; ++y) {
        const int32_t src_row = src_y + y;
        const int32_t dst_row = dst_y + y;
        if (src_row < 0 || src_row >= (int32_t)src_h || dst_row < 0 || dst_row >= (int32_t)dst_h) {
            continue;
        }
        const uint32_t* src_ptr = src + (uint32_t)src_row * src_w;
        uint32_t* dst_ptr = dst + (uint32_t)dst_row * dst_pitch;
        for (int32_t x = 0; x < copy_w; ++x) {
            const int32_t src_col = src_x + x;
            const int32_t dst_col = dst_x + x;
            if (src_col < 0 || src_col >= (int32_t)src_w || dst_col < 0 || dst_col >= (int32_t)dst_w) {
                continue;
            }
            dst_ptr[dst_col] = gfx_blend_argb(src_ptr[src_col], dst_ptr[dst_col]);
        }
    }
}

void gfx_present_rect(uint32_t* dst, uint32_t dst_w, uint32_t dst_h, uint32_t dst_pitch,
                     const uint32_t* src, uint32_t src_pitch,
                     uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!dst || !src || w == 0u || h == 0u || x >= dst_w || y >= dst_h) {
        return;
    }

    const uint32_t copy_w = w < (dst_w - x) ? w : (dst_w - x);
    const uint32_t copy_h = h < (dst_h - y) ? h : (dst_h - y);
    for (uint32_t row = 0u; row < copy_h; ++row) {
        const uint32_t* src_row = src + row * src_pitch;
        uint32_t* dst_row = dst + (y + row) * dst_pitch;
        for (uint32_t col = 0u; col < copy_w; ++col) {
            dst_row[x + col] = src_row[col];
        }
    }
}
