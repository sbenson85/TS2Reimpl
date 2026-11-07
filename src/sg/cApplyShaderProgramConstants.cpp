// nGZSceneGraph::cApplyShaderProgramConstants (lane TM, scenegraph M3a): see cApplyShaderProgramConstants.h.
#include "sg/cApplyShaderProgramConstants.h"

#include "sg/ShaderConstantSources.h"

#include "game/game.h"
#include "game/original.h"
#include "gz/cDeviceStateDX.h"
#include "rz/cRZStd.h"
#include "sg/cStringPool.h" // JenkinsHash

#include <cstring>

namespace nGZSceneGraph {

namespace {

// What Init's first argument answers: slot 21, a limit by number.
class cIProgramCaps {
public:
    virtual void Slot0() = 0;
    virtual void Slot1() = 0;
    virtual void Slot2() = 0;
    virtual void Slot3() = 0;
    virtual void Slot4() = 0;
    virtual void Slot5() = 0;
    virtual void Slot6() = 0;
    virtual void Slot7() = 0;
    virtual void Slot8() = 0;
    virtual void Slot9() = 0;
    virtual void Slot10() = 0;
    virtual void Slot11() = 0;
    virtual void Slot12() = 0;
    virtual void Slot13() = 0;
    virtual void Slot14() = 0;
    virtual void Slot15() = 0;
    virtual void Slot16() = 0;
    virtual void Slot17() = 0;
    virtual void Slot18() = 0;
    virtual void Slot19() = 0;
    virtual void Slot20() = 0;
    virtual uint32_t GetLimit(uint32_t which) = 0; // 21
};
// Slot 21's questions: the vertex program's float, integer and boolean constants (hardware vertex processing; the
// next number each the software limit), then the pixel program's.
constexpr uint32_t kVertexFloatLimit = 0x10;
constexpr uint32_t kVertexIntLimit = 0x12;
constexpr uint32_t kVertexBoolLimit = 0x14;
constexpr uint32_t kPixelFloatLimit = 0x16;
constexpr uint32_t kPixelIntLimit = 0x17;
constexpr uint32_t kPixelBoolLimit = 0x18;

// cRZStd's report slot (vtable 0xda3e6c slot 2), as cShader.cpp views it.
struct ReportView {
    virtual void __cdecl Slot0(const char *, ...) = 0;
    virtual void Slot1() = 0;
    virtual void __cdecl Report(const char *group, uint32_t level, const char *format, ...) = 0;
};

constexpr const char kControllersFile[] =
    "C:\\dev\\sims2remaster\\DL\\GZFrame\\SceneDB\\GZSceneGraph\\source\\GZSceneGraphShaderControllers.cpp";

bool Fail(bool report, uint32_t line, const char *text)
{
    if (report) {
        cRZStd raw;
        cRZStd *const std = raw.Construct(8, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(kControllersFile)), line);
        reinterpret_cast<ReportView *>(std)->Report("Shader validation", 4u, text);
    }
    return false;
}

constexpr const char kTooManyForSource[] = "...failed -- too many constants for source data\n";
constexpr const char kCountOne[] = "...failed -- constant count must be 1\n";

} // namespace

cIGZUnknown *__cdecl cApplyShaderProgramConstants::Create() { return new cApplyShaderProgramConstants; }

bool cApplyShaderProgramConstants::Init(void *capsArg, void *key, void *software, void *reportArg)
{
    cIProgramCaps *const caps = static_cast<cIProgramCaps *>(capsArg);
    const bool report = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(reportArg)) != 0;
    const bool soft = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(software)) != 0;
    // The three vertex limits are asked first, the pixel ones only as the checks reach them.
    const uint32_t vertexFloat = caps->GetLimit(soft ? kVertexFloatLimit : kVertexFloatLimit + 1);
    const uint32_t vertexInt = caps->GetLimit(soft ? kVertexIntLimit : kVertexIntLimit + 1);
    const uint32_t vertexBool = caps->GetLimit(soft ? kVertexBoolLimit : kVertexBoolLimit + 1);
    if (mRanges[kVertexFloat].mEnd > vertexFloat)
        return Fail(report, 0x1b5, "...failed -- too many float vertex program constants bound\n");
    if (mRanges[kVertexInt].mEnd > vertexInt)
        return Fail(report, 0x1bc, "...failed -- too many integer vertex program constants bound\n");
    if (mRanges[kVertexBool].mEnd > vertexBool)
        return Fail(report, 0x1c3, "...failed -- too many boolean vertex program constants bound\n");
    if (mRanges[kPixelFloat].mEnd > caps->GetLimit(kPixelFloatLimit))
        return Fail(report, 0x1cb, "...failed -- too many float pixel program constants bound\n");
    if (mRanges[kPixelInt].mEnd > caps->GetLimit(kPixelIntLimit))
        return Fail(report, 0x1d3, "...failed -- too many integer pixel program constants bound\n");
    if (mRanges[kPixelBool].mEnd > caps->GetLimit(kPixelBoolLimit))
        return Fail(report, 0x1db, "...failed -- too many boolean pixel program constants bound\n");

    for (const Binding &binding : mBindings) {
        const uint32_t count = binding.mCount;
        switch (binding.mSource) {
        case 0: case 1: case 2: case 3: case 4: case 5:
            if (count > 3) return Fail(report, 0x1ed, kTooManyForSource);
            break;
        case 6: case 7: case 8: case 9: case 0xa: case 0xb: case 0xc: case 0xd: case 0xe: case 0x12: case 0x13:
            if (count > 4) return Fail(report, 0x202, kTooManyForSource);
            break;
        case 0x14: case 0x15: case 0x16: case 0x18: case 0x1a: case 0x1b:
            if (count != 1) return Fail(report, 0x217, kCountOne);
            break;
        case 0x19:
            if (count != 1) return Fail(report, 0x222, kCountOne);
            if (binding.mOffset < 0) return Fail(report, 0x229, "...failed -- no attribute data offset\n");
            if (static_cast<uint32_t>(binding.mChannel) + binding.mDimension > 4)
                return Fail(report, 0x230, "...failed -- channel and data dimension exceed 4\n");
            break;
        case 0x1d:
            if ((count & 3) != 0) return Fail(report, 0x23f, "...failed -- constant count must be multiple of 4\n");
            break;
        case 0x1e:
            if (count != 0x20)
                return Fail(report, 0x24a, "...kAllStandardLightData2 failed -- constant count must be 32\n");
            break;
        case 0x27:
            if (count != 8)
                return Fail(report, 0x260, "...AllStandardLightExtraData binding - constant count must be 8\n");
            break;
        case 0x2a:
            if (count != 3)
                return Fail(report, 0x255, "...perPixelLightColor binding - constant count must be 3\n");
            break;
        default:
            break;
        }
    }
    return cShaderControllerT<cIApplyShaderProgramConstants>::Init(capsArg, key, software, reportArg);
}

uint32_t cApplyShaderProgramConstants::Slot10()
{
    for (uint32_t file = 0; file < kFiles; ++file)
        if (mRanges[file].mEnd != 0) return 1;
    return 0;
}

namespace {

// Each used file's range locked on the device state, in the image's order (the vertex and pixel float files, the
// integer ones, the boolean ones); null for an unused file.
void LockRanges(nGZGraphic4::cIDeviceState *state, const cApplyShaderProgramConstants::Range *ranges, void **out)
{
    using C = cApplyShaderProgramConstants;
    const auto count = [&](uint8_t file) { return ranges[file].mEnd - ranges[file].mBegin; };
    if (ranges[C::kVertexFloat].mEnd != 0)
        out[C::kVertexFloat] = state->LockVertexFloatConstants(ranges[C::kVertexFloat].mBegin, count(C::kVertexFloat));
    if (ranges[C::kPixelFloat].mEnd != 0)
        out[C::kPixelFloat] = state->LockPixelFloatConstants(ranges[C::kPixelFloat].mBegin, count(C::kPixelFloat));
    if (ranges[C::kVertexInt].mEnd != 0)
        out[C::kVertexInt] = state->LockVertexIntConstants(ranges[C::kVertexInt].mBegin, count(C::kVertexInt));
    if (ranges[C::kPixelInt].mEnd != 0)
        out[C::kPixelInt] = state->LockPixelIntConstants(ranges[C::kPixelInt].mBegin, count(C::kPixelInt));
    if (ranges[C::kVertexBool].mEnd != 0)
        out[C::kVertexBool] = state->LockVertexBoolConstants(ranges[C::kVertexBool].mBegin, count(C::kVertexBool));
    if (ranges[C::kPixelBool].mEnd != 0)
        out[C::kPixelBool] = state->LockPixelBoolConstants(ranges[C::kPixelBool].mBegin, count(C::kPixelBool));
}

} // namespace

void cApplyShaderProgramConstants::Apply(void *ctxArg, void *item)
{
    cShaderApplyContext *const ctx = static_cast<cShaderApplyContext *>(ctxArg);
    if (ctx->mSamePass != 0) return;
    void *locked[kFiles] = {};
    LockRanges(ctx->mState, mRanges, locked);
    for (const Binding &binding : mBindings) {
        const uint8_t file = binding.mFile;
        const uint32_t index = binding.mRegister - mRanges[file].mBegin;
        switch (file) {
        case kVertexFloat:
        case kPixelFloat:
            FillFloatConstants(reinterpret_cast<float *>(static_cast<uint8_t *>(locked[file]) + index * 0x10), &binding,
                               ctx, static_cast<const cShaderApplyItem *>(item));
            break;
        case kVertexInt:
        case kPixelInt:
            FillIntConstants(reinterpret_cast<int32_t *>(static_cast<uint8_t *>(locked[file]) + index * 0x10), &binding,
                             ctx, static_cast<const cShaderApplyItem *>(item));
            break;
        case kVertexBool:
        case kPixelBool:
            static_cast<uint32_t *>(locked[file])[index] = binding.mValues[0] != 0.0f;
            break;
        default:
            break;
        }
    }
}

void cApplyShaderProgramConstants::Slot12(void *stateArg, const void *)
{
    nGZGraphic4::cIDeviceState *const state = static_cast<nGZGraphic4::cIDeviceState *>(stateArg);
    // Each used range locked and zeroed in turn, in Apply's order: four words a register for the float and integer
    // files, one for the boolean ones.
    const auto count = [&](uint8_t file) { return mRanges[file].mEnd - mRanges[file].mBegin; };
    if (mRanges[kVertexFloat].mEnd != 0)
        std::memset(state->LockVertexFloatConstants(mRanges[kVertexFloat].mBegin, count(kVertexFloat)), 0,
                    count(kVertexFloat) * 0x10);
    if (mRanges[kPixelFloat].mEnd != 0)
        std::memset(state->LockPixelFloatConstants(mRanges[kPixelFloat].mBegin, count(kPixelFloat)), 0,
                    count(kPixelFloat) * 0x10);
    if (mRanges[kVertexInt].mEnd != 0)
        std::memset(state->LockVertexIntConstants(mRanges[kVertexInt].mBegin, count(kVertexInt)), 0,
                    count(kVertexInt) * 0x10);
    if (mRanges[kPixelInt].mEnd != 0)
        std::memset(state->LockPixelIntConstants(mRanges[kPixelInt].mBegin, count(kPixelInt)), 0,
                    count(kPixelInt) * 0x10);
    if (mRanges[kVertexBool].mEnd != 0)
        std::memset(state->LockVertexBoolConstants(mRanges[kVertexBool].mBegin, count(kVertexBool)), 0,
                    count(kVertexBool) * 4);
    if (mRanges[kPixelBool].mEnd != 0)
        std::memset(state->LockPixelBoolConstants(mRanges[kPixelBool].mBegin, count(kPixelBool)), 0,
                    count(kPixelBool) * 4);
}

void cApplyShaderProgramConstants::AddConstant(const void *bindingArg)
{
    const Binding &binding = *static_cast<const Binding *>(bindingArg);
    mBindings.push_back(binding);
    Range &range = mRanges[binding.mFile];
    if (binding.mRegister < range.mBegin) range.mBegin = binding.mRegister;
    const uint32_t end = binding.mCount + binding.mRegister;
    if (range.mEnd < end) range.mEnd = end;
}

uint32_t cApplyShaderProgramConstants::HashParams()
{
    if (mBindings.empty()) return 0;
    return JenkinsHash(mBindings.data(), static_cast<uint32_t>(mBindings.size() * sizeof(Binding)), 0);
}

bool cApplyShaderProgramConstants::EqualParams(cIApplyShaderProgramConstants *otherArg)
{
    const cApplyShaderProgramConstants *const other = static_cast<cApplyShaderProgramConstants *>(otherArg);
    if (mBindings.size() != other->mBindings.size()) return false;
    return mBindings.empty() ||
           std::memcmp(mBindings.data(), other->mBindings.data(), mBindings.size() * sizeof(Binding)) == 0;
}

} // namespace nGZSceneGraph
