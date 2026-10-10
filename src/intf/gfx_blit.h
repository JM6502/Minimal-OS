#ifndef GFX_BLIT_H
#define GFX_BLIT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} gfx_rect_t;

bool gfx_clip_rect(const gfx_rect_t* a, const gfx_rect_t* b, gfx_rect_t* out);
void gfx_fill(uint32_t* dst, uint32_t width, uint32_t height, uint32_t stride,
              const gfx_rect_t* rect, uint32_t color);
void gfx_blit_opaque(uint32_t* dst, uint32_t dst_w, uint32_t dst_h, uint32_t dst_pitch,
                     const uint32_t* src, uint32_t src_w, uint32_t src_h,
                     int32_t src_x, int32_t src_y, int32_t src_wi, int32_t src_hi,
                     int32_t dst_x, int32_t dst_y, int32_t clip_w, int32_t clip_h);
void gfx_blit_alpha(uint32_t* dst, uint32_t dst_w, uint32_t dst_h, uint32_t dst_pitch,
                    const uint32_t* src, uint32_t src_w, uint32_t src_h,
                    int32_t src_x, int32_t src_y, int32_t src_wi, int32_t src_hi,
                    int32_t dst_x, int32_t dst_y, int32_t clip_w, int32_t clip_h);
void gfx_present_rect(uint32_t* dst, uint32_t dst_w, uint32_t dst_h, uint32_t dst_pitch,
                     const uint32_t* src, uint32_t src_pitch,
                     uint32_t x, uint32_t y, uint32_t w, uint32_t h);

#endif /* GFX_BLIT_H */
