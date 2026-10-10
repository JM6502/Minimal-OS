#ifndef GFX_BLIT_H
#define GFX_BLIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} gfx_rect_t;

typedef struct {
    uint32_t* pixels;
    size_t byte_length;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
} gfx_surface_t;

bool gfx_clip_rect(const gfx_rect_t* a, const gfx_rect_t* b, gfx_rect_t* out);
bool gfx_surface_valid(const gfx_surface_t* surface);
bool gfx_fill(gfx_surface_t* dst, const gfx_rect_t* rect, uint32_t color);
bool gfx_blit_opaque(gfx_surface_t* dst, const gfx_surface_t* src,
                     const gfx_rect_t* src_rect, int32_t dst_x, int32_t dst_y);
bool gfx_blit_alpha(gfx_surface_t* dst, const gfx_surface_t* src,
                    const gfx_rect_t* src_rect, int32_t dst_x, int32_t dst_y);
bool gfx_present_rect(gfx_surface_t* dst, const gfx_surface_t* src,
                      const gfx_rect_t* rect);

#endif /* GFX_BLIT_H */
