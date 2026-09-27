/* CPU addressing for single-chunk SGX543 texture rectangles. */
#ifndef PVR_TEXREGION_H
#define PVR_TEXREGION_H
#include <string.h>

/* Layout: 0 = row-major, 1 = tiled, 2 = rectangular power-of-two Morton. */
static __inline unsigned PVRTextureOffset(unsigned layout, unsigned width, unsigned height,
                                         unsigned x, unsigned y)
{
    unsigned offset = 0, bit = 0;
    if(layout == 0) return y * width + x;
    if(layout == 1)
        return ((y / EURASIA_TAG_TILE_SIZEY) * ((width + EURASIA_TAG_TILE_SIZEX - 1) / EURASIA_TAG_TILE_SIZEX)
            + x / EURASIA_TAG_TILE_SIZEX) * EURASIA_TAG_TILE_SIZEX * EURASIA_TAG_TILE_SIZEY
            + (y % EURASIA_TAG_TILE_SIZEY) * EURASIA_TAG_TILE_SIZEX + x % EURASIA_TAG_TILE_SIZEX;
    while(width > 1 || height > 1)
    {
        if(height > 1) { offset |= (y & 1U) << bit++; y >>= 1; height >>= 1; }
        if(width > 1) { offset |= (x & 1U) << bit++; x >>= 1; width >>= 1; }
    }
    return offset;
}

static __inline void PVRTextureWriteRegion(void *surface, const void *pixels, unsigned layout,
    unsigned width, unsigned height, unsigned x, unsigned y, unsigned columns, unsigned rows,
    unsigned bytes, unsigned sourceStride)
{
    unsigned row, column;
    for(row = 0; row < rows; ++row)
        for(column = 0; column < columns; ++column)
            memcpy((unsigned char *)surface + bytes * PVRTextureOffset(layout, width, height, x + column, y + row),
                   (const unsigned char *)pixels + row * sourceStride + column * bytes, bytes);
}

static __inline void PVRTextureReadRegion(void *pixels, const void *surface, unsigned layout,
    unsigned width, unsigned height, unsigned x, unsigned y, unsigned columns, unsigned rows,
    unsigned bytes, int yStep)
{
    unsigned row, column;
    for(row = 0; row < rows; ++row)
        for(column = 0; column < columns; ++column)
            memcpy((unsigned char *)pixels + (row * columns + column) * bytes,
                   (const unsigned char *)surface + bytes * PVRTextureOffset(layout, width, height,
                       x + column, (unsigned)((int)y + (int)row * yStep)), bytes);
}
#endif
