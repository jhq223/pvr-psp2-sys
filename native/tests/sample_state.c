#include "usp_sample.c"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>

static void check(IMG_BOOL skip, IMG_BOOL memory, IMG_UINT32 descriptor_flags) {
    USP_CONTEXT context = {0};
    USP_PROGDESC program = {0};
    USP_SHDR_TEX_CTR_WORDS words = {0};
    USP_SHADER shader = {0};
    USP_SAMPLE sample = {0};
    USP_SAMPLEUNPACK unpack = {0};
    USP_TEXSTATE_DATA state = {0};
    USP_INPUT_DATA input = {0};
    shader.psProgDesc = &program;
    shader.psTexCtrWrds = &words;
    sample.psShader = &shader;
    sample.psSampleUnpack = &unpack;
    sample.uInstDescFlags = descriptor_flags;
    sample.eBaseInstOpcode = USP_OPCODE_SMP;
    sample.sBaseInst.uWord1 = (EURASIA_USE1_OP_SMP << EURASIA_USE1_OP_SHIFT)
        | (EURASIA_USE1_SMP_CDIM_UV << EURASIA_USE1_SMP_CDIM_SHIFT)
        | (skip ? EURASIA_USE1_SKIPINV : 0);
    sample.eBaseSampleDestRegType = USP_REGTYPE_TEMP;
    sample.eSampleTempRegType = USP_REGTYPE_TEMP;
    sample.uSampleTempRegNum = 8;
    sample.sTexChanInfo.uTexChunkMask = 1;
    sample.sTexChanInfo.auNumRegsForChunk[0] = 4;
    state.uRegCount = 4;
    state.uRegNum = 4;
    state.bInMemory = memory;
    input.uTexStateDataCount = 1;
    input.psTexStateData = &state;
    context.asSamplerDesc[0].sTexFormat.eTexFmt = USP_TEXTURE_FORMAT_R8G8B8A8;
    assert(SampleTextureData(&sample, &input, &context));
    unsigned loads = 0, samples = 0;
    for (unsigned i = 0; i < sample.uSampleInstCount_; ++i) {
        USP_OPCODE opcode = sample.aeOpcodeForSampleInsts_[i];
        if (opcode == USP_OPCODE_LD || opcode == USP_OPCODE_SMP) {
            assert(!!(sample.asSampleInsts_[i].uWord1 & EURASIA_USE1_SKIPINV) == !!skip);
            loads += opcode == USP_OPCODE_LD;
            samples += opcode == USP_OPCODE_SMP;
        }
    }
    assert(loads == (unsigned)memory && samples == 1);
    assert(sample.uSampleInstCount_ == (memory ? 4u : 2u));
}

int main(void) {
    for (unsigned skip = 0; skip < 2; ++skip)
        for (unsigned memory = 0; memory < 2; ++memory)
            for (unsigned preserve = 0; preserve < 2; ++preserve)
                check(skip, memory, preserve ? USP_INSTDESC_FLAG_DONT_SKIPINV : 0);
    puts("sample state: memory/register paths preserve encoded sample coverage");
    return 0;
}
