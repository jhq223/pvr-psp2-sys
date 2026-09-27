#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define __psp2__ 1
#define IMG_INTERNAL
#define IMG_VOID void
typedef uint8_t IMG_UINT8;
typedef uint16_t IMG_UINT16;
typedef uint32_t IMG_UINT32;
typedef int32_t IMG_INT32;
typedef uintptr_t IMG_UINTPTR_T;
typedef struct {
    void *pvInData, *pvOutData;
    IMG_UINT32 ui32Width;
    IMG_INT32 i32SrcGroupIncrement;
} GLES2PixelSpanInfo;
static unsigned kernel_copies, libc_copies;
static void *sceClibMemcpy(void *dst, const void *src, size_t bytes) {
    assert(bytes >= 64); ++kernel_copies; return memcpy(dst, src, bytes);
}
static void *GLES2MemCopy(void *dst, const void *src, size_t bytes) {
    ++libc_copies; return memcpy(dst, src, bytes);
}
#include "span_copy_functions.inc"

static void check(unsigned bpp, unsigned width, int step, unsigned src_offset, unsigned dst_offset) {
    unsigned stride = (unsigned)(step < 0 ? -step : step);
    size_t src_size = 128 + width * stride, dst_size = 128 + width * bpp;
    unsigned char *src = malloc(src_size), *dst = malloc(dst_size), *expected = malloc(dst_size);
    assert(src && dst && expected);
    for(size_t i = 0; i < src_size; ++i) src[i] = (unsigned char)(i * 37 + i / 11);
    memset(dst, 0xa5, dst_size); memset(expected, 0xa5, dst_size);
    unsigned char *first = src + 64 + src_offset + (step < 0 && width ? (width - 1) * stride : 0);
    for(unsigned x = 0; x < width; ++x)
        for(unsigned c = 0; c < bpp; ++c)
            expected[64 + dst_offset + x*bpp + c] = first[(ptrdiff_t)x*step+c];
    GLES2PixelSpanInfo span = {first, dst+64+dst_offset, width, step};
    unsigned before_kernel = kernel_copies, before_libc = libc_copies;
    if(bpp == 2) SpanPack16(&span); else SpanPack32(&span);
    assert(!memcmp(dst, expected, dst_size));
    assert(kernel_copies-before_kernel == (width*bpp >= 64 && step == (int)bpp));
    assert(libc_copies-before_libc == (width > 0 && width*bpp < 64 && step == (int)bpp));
    free(expected); free(dst); free(src);
}
int main(void) {
    const unsigned widths[] = {0,1,3,15,16,17,31,32,63,64,65,960,1024};
    unsigned cases = 0;
    GLES2PixelSpanInfo empty = {NULL,NULL,0,-4};
    SpanPack16(&empty); SpanPack32(&empty);
    assert(!kernel_copies && !libc_copies);
    for(unsigned bpp = 2; bpp <= 4; bpp *= 2)
        for(unsigned w = 0; w < sizeof(widths)/sizeof(widths[0]); ++w) {
            for(unsigned a=0; a<4; ++a) for(unsigned b=0; b<4; ++b) {
                check(bpp,widths[w],(int)bpp,a,b); ++cases;
            }
            int steps[] = {-(int)bpp,(int)bpp*16,-(int)bpp*16};
            for(unsigned s=0; s<3; ++s) { check(bpp,widths[w],steps[s],0,0); ++cases; }
        }
    printf("pixel spans: %u cases; kernel/libc paths, offsets, reversed/rotated input and guards passed\n", cases);
}
