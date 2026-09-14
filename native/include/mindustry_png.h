#ifndef MINDUSTRY_NATIVE_PNG_H
#define MINDUSTRY_NATIVE_PNG_H

#include "mindustry_format.h"

#ifdef __cplusplus
#error "The native Mindustry port is C, not C++."
#endif

typedef struct McImage{
    uint32_t width;
    uint32_t height;
    /* RGBA8, row-major, width * height * 4 bytes. */
    uint8_t *rgba;
} McImage;

MC_API McStatus mc_png_read(const uint8_t *data, size_t size, McImage *image);
MC_API McStatus mc_png_write(const McImage *image, McBuffer *output);
MC_API void mc_image_destroy(McImage *image);

#endif
