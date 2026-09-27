#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t IMG_UINT8;
typedef uint16_t IMG_UINT16;
typedef uint32_t IMG_UINT32;
typedef uint64_t IMG_UINT64;
typedef int32_t IMG_INT32;
typedef int IMG_BOOL;
#define IMG_INTERNAL
#define IMG_VOID void
#define IMG_TRUE 1
#define IMG_FALSE 0
#define IMG_NULL NULL
#define PVR_UNREFERENCED_PARAMETER(x) ((void)(x))
#define EURASIA_TAG_TILE_SIZEX 32
#define EURASIA_TAG_TILE_SIZEY 32
#include "twiddle.h"
#include "twidtabs.h"
#include "twiddle_functions.inc"
#include "texregion.h"

static void check(unsigned width, unsigned height, unsigned bytes) {
    unsigned size = width * height * bytes;
    unsigned char *linear = malloc(size), *gpu = malloc(size), *region = malloc(size), *reference = malloc(size);
    assert(linear && gpu && region && reference);
    for(unsigned i = 0; i < size; ++i) linear[i] = (unsigned char)(i * 17 + i / 7);
    if(bytes == 1) DeTwiddleAddress8bpp(gpu, linear, width, height, width);
    if(bytes == 2) DeTwiddleAddress16bpp(gpu, linear, width, height, width);
    if(bytes == 4) DeTwiddleAddress32bpp(gpu, linear, width, height, width);
    for(unsigned y = 0; y < height; ++y) for(unsigned x = 0; x < width; ++x)
        assert(!memcmp(gpu + bytes * PVRTextureOffset(2, width, height, x, y), linear + bytes * (y * width + x), bytes));
    unsigned x = width / 3, y = height / 3, columns = width - x, rows = height - y;
    PVRTextureReadRegion(region, gpu, 2, width, height, x, y, columns, rows, bytes, 1);
    for(unsigned row = 0; row < rows; ++row)
        assert(!memcmp(region + row * columns * bytes, linear + ((row + y) * width + x) * bytes, columns * bytes));
    PVRTextureReadRegion(region, gpu, 2, width, height, x, height - 1, columns, rows, bytes, -1);
    for(unsigned row = 0; row < rows; ++row)
        assert(!memcmp(region + row * columns * bytes, linear + ((height - 1 - row) * width + x) * bytes, columns * bytes));
    memset(region, 93, size);
    PVRTextureWriteRegion(gpu, region, 2, width, height, x, y, columns, rows, bytes, columns * bytes);
    unsigned wl = 0, hl = 0;
    for(unsigned n = width; n > 1; n >>= 1) ++wl;
    for(unsigned n = height; n > 1; n >>= 1) ++hl;
    if(bytes == 1) ReadBackTwiddle8bpp(reference, gpu, wl, hl, width, height, width);
    if(bytes == 2) ReadBackTwiddle16bpp(reference, gpu, wl, hl, width, height, width);
    if(bytes == 4) ReadBackTwiddle32bpp(reference, gpu, wl, hl, width, height, width);
    for(unsigned row = 0; row < height; ++row) for(unsigned col = 0; col < width; ++col)
        for(unsigned b = 0; b < bytes; ++b) assert(reference[(row * width + col) * bytes + b] ==
            (row >= y && col >= x ? 93 : linear[(row * width + col) * bytes + b]));
    free(reference); free(region); free(gpu); free(linear);
}
int main(void) {
    for(unsigned w = 1; w <= 128; w *= 2) for(unsigned h = 1; h <= 128; h *= 2)
        for(unsigned b = 1; b <= 4; b *= 2) check(w, h, b);
    /* Explicit tiled layout: horizontal and vertical tile boundaries. */
    assert(PVRTextureOffset(1, 65, 40, 32, 0) == 1024);
    assert(PVRTextureOffset(1, 65, 40, 0, 32) == 3072);
    assert(PVRTextureOffset(1, 65, 40, 64, 39) == 5344);
    puts("texture regions: 192 differential layout cases passed");
}
