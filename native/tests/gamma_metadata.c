#include <assert.h>
#include "sgxfeaturedefs.h"

typedef enum {
    USP_CHAN_FORMAT_U8, USP_CHAN_FORMAT_S8, USP_CHAN_FORMAT_U16,
    USP_CHAN_FORMAT_S16, USP_CHAN_FORMAT_F16, USP_CHAN_FORMAT_F32,
    USP_CHAN_FORMAT_U24, USP_CHAN_FORMAT_U32, USP_CHAN_FORMAT_S32
} USP_CHAN_FORMAT, *PUSP_CHAN_FORMAT;
typedef struct { USP_CHAN_FORMAT aeChanForm[4]; } USP_TEX_CHAN_FORMAT, *PUSP_TEX_CHAN_FORMAT;
typedef struct { IMG_BOOL bAnisoEnabled, bGammaEnabled; } FLAGS;
typedef struct { FLAGS asSamplerDesc[4]; } USP_CONTEXT, *PUSP_CONTEXT;
typedef FLAGS USP_SAMPLER_CHAN_DESC, *PUSP_SAMPLER_CHAN_DESC;
#include "gamma-functions.inc"

int main(void) {
    USP_CONTEXT context = {0};
    for (unsigned aniso = 0; aniso != 2; ++aniso) {
        for (unsigned gamma = 0; gamma != 2; ++gamma) {
            context.asSamplerDesc[2].bAnisoEnabled = aniso;
            context.asSamplerDesc[2].bGammaEnabled = gamma;
            USP_SAMPLER_CHAN_DESC flags = {0};
            SetTextureChanFlags(&context, 2, &flags);
            assert(flags.bAnisoEnabled == aniso && flags.bGammaEnabled == gamma);
            for (unsigned channel = 0; channel != 4; ++channel) {
                for (unsigned format = 0; format != 9; ++format) {
                    const unsigned bits[] = {8, 8, 16, 16, 16, 32, 24, 32, 32};
                    USP_TEX_CHAN_FORMAT texture = {{0}};
                    texture.aeChanForm[channel] = format;
                    IMG_UINT32 source_bits, register_bits;
                    USP_CHAN_FORMAT result;
                    CalcTexChanBitCountAndFmt(&texture, channel, flags.bGammaEnabled,
                        &source_bits, &register_bits, &result);
                    assert(source_bits == bits[format]);
                    if (gamma && format == USP_CHAN_FORMAT_U8) {
                        assert(register_bits == 16);
#if defined(SGX543)
                        assert(result == USP_CHAN_FORMAT_F16);
#else
                        assert(result == USP_CHAN_FORMAT_U16);
#endif
                    } else {
                        assert(register_bits == bits[format] && result == format);
                    }
                }
            }
        }
    }
    return 0;
}
