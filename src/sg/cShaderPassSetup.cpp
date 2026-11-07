// ShaderPass::Setup (0xaa53e0) and the part-vector hash (0xaa4fe0): a pass made from its cShaderInit descriptor. See
// src/sg/cShader.h. The calls follow the original's order and count (the tests compare the call streams): the managers are
// fetched again for each use, the name pools' counts are asked again on every turn, and counted pointers are taken and
// given back in the original's order.
#include "sg/cShader.h"

#include "game/original.h"
#include "gz/GraphicServices.h"
#include "gz/cDeviceDX.h"
#include "gz/cDeviceEnum.h"
#include "gz/cDeviceStateDX.h"
#include "gz/cShaderProgramsDX.h"
#include "gz/cStateBlockDX.h"
#include "gz/cTextureDX.h"
#include "gz/cVertexFormat.h"
#include "misc/UtilitiesE.h"
#include "rz/cRZStd.h"
#include "sg/cGeometryNodeFormats.h"
#include "sg/cMaterialManager.h"
#include "sg/cShaderInit.h"
#include "sg/cStringPool.h"
#include "sg/cTextureImage.h"
#include "sg/cTextureManager.h"
#include "simantics/cTSBehaviorTree.h"

#include <cstdint>

namespace nGZSceneGraph {

using nGZGraphic4::cIDeviceDX;
using nGZGraphic4::cIDeviceEnum;
using nGZGraphic4::cIDeviceState;
using nGZGraphic4::cIShaderProgram;
using nGZGraphic4::cIStateBlock;
using nGZGraphic4::cIVertexDeclarationDX;
using nGZGraphic4::cIVertexDeclarationsManagerDX;
using nGZGraphic4::cIVertexFormat;
using nGZGraphic4::cIVertexFormatsManager;

namespace {

// cRZStd's report slot (vtable 0xda3e6c slot 2), as cShader.cpp views it.
struct ReportView {
    virtual void __cdecl Slot0(const char *, ...) = 0;
    virtual void Slot1() = 0;
    virtual void __cdecl Report(const char *group, uint32_t level, const char *format, ...) = 0;
};

constexpr const char kShaderFile[] = "C:\\dev\\sims2remaster\\DL\\GZFrame\\SceneDB\\GZSceneGraph\\source\\GZSceneGraphShader.cpp";
constexpr const char kGroup[] = "Shader validation";

ReportView *OpenLog(uint32_t line, cRZStd &raw)
{
    return reinterpret_cast<ReportView *>(raw.Construct(8, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(kShaderFile)), line));
}

void SayPass(uint32_t line, const char *format, uint32_t pass)
{
    cRZStd raw;
    OpenLog(line, raw)->Report(kGroup, 4u, format, pass);
}

// A shader controller as the pass setup sees it: the descriptor's parts, the init's objects and what the material manager
// registers for them (iid 0x2d2df503). Slots 3..7 are not called here.
class ShaderControllerView : public cIGZUnknown {
public:
    virtual void Slot3() = 0;
    virtual void Slot4() = 0;
    virtual void Slot5() = 0;
    virtual void Slot6() = 0;
    virtual void Slot7() = 0;
    // 8 (ret 0x10): make the device resources for the key; false when the device cannot run the controller.
    virtual bool InitDeviceResourcesAndValidate(cIDeviceEnum *caps, const ShaderKey *key, bool hardware, bool report) = 0;
    virtual void Apply(cIDeviceState *state, const ShaderKey *key) = 0; // 9: record its state for the key
    virtual uint32_t GetDynamicStateFlags() = 0;                         // 10: bit 1 also wants the pass's end-state call
};

constexpr uint32_t kIidPartMapper = 0x2ced4fc6;  // what the descriptor's object +0 registers as (the pass's mP8)
constexpr uint32_t kIidController = 0x2d2df503;  // what each controller registers as
constexpr uint32_t kMostStreams = 0x10;
constexpr uint32_t kCapHardwareVp = 0;           // device processing caps asked
constexpr uint32_t kCapSoftwareVp = 3;
constexpr uint32_t kGolden = 0x9e3779b1;

cIShaderPart *AsPart(void *p) { return static_cast<cIShaderPart *>(p); }
cIShaderProgram *AsProgram(cIShaderPart *p) { return reinterpret_cast<cIShaderProgram *>(p); }
cIGZUnknown *AsUnknown(cIShaderPart *p) { return reinterpret_cast<cIGZUnknown *>(p); }
ShaderControllerView *AsController(void *p) { return static_cast<ShaderControllerView *>(p); }

cIVertexFormatsManager *Formats() { return static_cast<cIVertexFormatsManager *>(nGZGraphic4::GetVertexFormatsManagerCached()); }
cIVertexDeclarationsManagerDX *Declarations()
{
    return static_cast<cIVertexDeclarationsManagerDX *>(nGZGraphic4::GetVertexDeclarationsManagerCached());
}
cIMaterialManager *Materials() { return static_cast<cIMaterialManager *>(GetCachedQueriedObject()); }

// A counted member set to `value`: nothing when it holds it already; else the new one is counted and stored, then the
// old one released.
template <class T> void CountedAssign(T *&member, T *value)
{
    T *const old = member;
    if (value == old) return;
    if (value) value->AddRef();
    member = value;
    if (old) old->Release();
}

// Release a counted pointer, clearing it before the call.
template <class T> void ReleaseFirst(T *&member)
{
    T *const p = member;
    if (p) {
        member = nullptr;
        p->Release();
    }
}

template <class T> void ReleaseIf(T *p)
{
    if (p) p->Release();
}

// push_back of a counted pointer: in place (stored, then counted) when there is room, else the vector's reallocating
// emplace, given the address of the caller's copy.
void PushCounted(ShaderPartVec &vec, cIShaderPart *const *value)
{
    if (vec.mEnd != vec.mCap) {
        cIShaderPart *const p = *value;
        *vec.mEnd = p;
        if (p) p->AddRef();
        ++vec.mEnd;
    } else {
        reinterpret_cast<RefPtrVector *>(&vec)->EmplaceReallocate(reinterpret_cast<void **>(vec.mEnd),
                                                                   reinterpret_cast<void *const *>(value));
    }
}

bool Holds(const ShaderPartVec &vec, const cIShaderPart *p)
{
    for (cIShaderPart *const *it = vec.mBegin; it != vec.mEnd; ++it)
        if (*it == p) return true;
    return false;
}

// The hardware flag as the callees' bool parameter: the pass stores the program's or the device's byte as it is (the
// enumeration's slot 20 answers its table byte, not 0 / 1), and the original passes that byte on unchanged.
bool StoredFlag(const uint8_t &flag) { return reinterpret_cast<const bool &>(flag); }

int32_t Streams(cIVertexFormat *format) { return static_cast<int32_t>(format->GetStreamCount()); }

uint32_t Scaled(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) >> 2) * kGolden; }

} // namespace

uint32_t __stdcall ShaderPartVecHash(const ShaderPartVec *vec)
{
    uint32_t h = 0;
    for (cIShaderPart *const *it = vec->mBegin; it != vec->mEnd; ++it) {
        const uint32_t scaled = Scaled(*it);
        const uint32_t high = ShaderHashMix(h);
        h = ShaderHashCombine(ShaderHashMix(scaled), high);
    }
    return h;
}

// 0xaa53e0
bool ShaderPass::Setup(cIDeviceDX *device, uint32_t index, const ShaderInitView *view, const ShaderKey *key, bool report)
{
    cIDeviceState *const state = static_cast<cIDeviceState *>(device->GetStateConst());
    cIDeviceEnum *const caps = static_cast<cIDeviceEnum *>(device->GetDeviceEnumConst());
    const ShaderInitData *const init = reinterpret_cast<const ShaderInitData *>(view);
    const ShaderDescriptor *const desc = reinterpret_cast<const ShaderDescriptor *>(view->mDescBegin) + index;

    mB5 = desc->mByteC4;
    mB3c = desc->mByteC5;
    mB3d = desc->mByteC6;
    mB3e = desc->mByteC7;
    CountedAssign(mPc, AsPart(desc->mObject1c));
    m40 = init->mAttribute;
    CountedAssign(mP50, AsPart(desc->mObject24));
    CountedAssign(mP54, AsPart(desc->mObject28));
    if (desc->mObject00) {
        cIMaterialManager *const materials = Materials();
        ReleaseFirst(mP8);
        materials->Register(reinterpret_cast<cIGZUnknown *>(desc->mObject00), kIidPartMapper, reinterpret_cast<void **>(&mP8));
    }
    // A single descriptor without a preprocess program clears mB4.
    if (static_cast<uint32_t>(view->mDescEnd - view->mDescBegin) - ShaderInitView::kDescriptorBytes < ShaderInitView::kDescriptorBytes &&
        !mPc)
        mB4 = 0;
    if (mPc)
        mB5 = 0;
    else if (index == 0 || mP8)
        mB5 = 1;

    ShaderKey mapped;
    if (mP8) {
        ShaderKey scratch;
        const uint32_t *const words = mP8->Slot9(&scratch, key);
        mapped.w[0] = words[0];
        mapped.w[1] = words[1];
        mapped.w[2] = words[2];
    } else {
        mapped = *key;
    }

    const uint8_t hardware = mP50 ? AsProgram(mP50)->GetInfo()->hardware : caps->HasProcessingCap(kCapHardwareVp);
    mB6 = hardware;
    if (hardware && !(mapped.w[2] & 1)) {
        if (report) SayPass(0x72, "...failed due to missing hw vp support at pass %d\n", index);
        return false;
    }
    if ((((mapped.w[2] >> 2) | (mapped.w[2] >> 1)) & 1) && mB5) mB7 = 1;
    mApply = ApplyFor((mPc ? 1u : 0u) | (mB5 ? 2u : 0u) | (mP8 ? 4u : 0u));

    cIVertexFormat *const format = Formats()->GetFormat((mapped.w[2] >> 4) & 0x7f);
    if (Streams(format) > static_cast<int32_t>(kMostStreams)) {
        if (report) SayPass(0xa2, "...failed due to too many components in vertex format at pass %d\n", index);
        return false;
    }
    {
        cIVertexDeclarationsManagerDX *const declarations = Declarations();
        ReleaseFirst(mP48);
        if (!declarations->GetDeclaration(device, format, reinterpret_cast<cIVertexDeclarationDX **>(&mP48))) {
            if (report)
                SayPass(0xa7, "...failed to create vertex declaration for the preprocess vertex program at pass %d\n", index);
            return false;
        }
    }

    // From here every exit gives the stream format back.
    cIVertexFormat *stream = format;
    stream->AddRef();
    const bool ok = [&]() -> bool {
        if (!mB7) {
            ReleaseFirst(stream);
            MakeStreamVertexFormat(&stream, format, 0, 0);
        }
        if (mB6) {
            cIVertexFormat *deviceFormat = nullptr;
            bool made = false;
            if (!MakeDeviceVertexFormat(&deviceFormat, device, stream)) {
                if (report) SayPass(0xb5, "...failed to create hw vertex format for at pass %d\n", index);
            } else if (Streams(deviceFormat) > static_cast<int32_t>(kMostStreams)) {
                if (report) SayPass(0xba, "...failed due to too many components in hw vertex format at pass %d\n", index);
            } else {
                cIVertexDeclarationsManagerDX *const declarations = Declarations();
                ReleaseFirst(mP4c);
                if (declarations->GetDeclaration(device, deviceFormat, reinterpret_cast<cIVertexDeclarationDX **>(&mP4c)))
                    made = true;
                else if (report)
                    SayPass(0xbf, "...failed to create hw vertex declaration at pass %d\n", index);
            }
            ReleaseIf(deviceFormat);
            if (!made) return false;
        } else {
            cIVertexDeclarationsManagerDX *const declarations = Declarations();
            ReleaseFirst(mP4c);
            if (!declarations->GetDeclaration(device, stream, reinterpret_cast<cIVertexDeclarationDX **>(&mP4c))) {
                if (report) SayPass(0xc6, "...failed to create vertex declaration at pass %d\n", index);
                return false;
            }
        }

        // A preprocess program needs its output format, and is the only thing that may have parts of the first kind.
        cIVertexFormat *const output = reinterpret_cast<cIVertexFormat *>(desc->mObject20);
        if (mPc ? !output : output != nullptr) {
            if (report) SayPass(0xcf, "...failed due to missing preprocess vertex program or output vertex format at pass %d\n", index);
            return false;
        }
        if (!mPc && desc->mPartsA.mBegin != desc->mPartsA.mEnd) {
            if (report) SayPass(0xd5, "...failed due to missing preprocess vertex program at pass %d\n", index);
            return false;
        }
        if (mPc && !caps->HasProcessingCap(kCapSoftwareVp)) {
            if (report) SayPass(0xdb, "...failed, preprocess vertex program requires device SW VP support at pass %d\n", index);
            return false;
        }
        if (mPc && !AsProgram(mPc)->IsCreated()) {
            if (report) SayPass(0xe1, "...failed to initialize preprocess vertex program at pass %d\n", index);
            return false;
        }
        if (mP50 && !AsProgram(mP50)->IsCreated()) {
            if (report) SayPass(0xe7, "...failed to initialize vertex program at pass %d\n", index);
            return false;
        }
        if (mP54 && !AsProgram(mP54)->IsCreated()) {
            if (report) SayPass(0xed, "...failed to initialize pixel program at pass %d\n", index);
            return false;
        }
        for (uint32_t i = 0; i < init->mNames.Count(); ++i)
            if (!state->ValidateCommand(reinterpret_cast<const void *>(static_cast<uintptr_t>(init->mNames.At(i))), StoredFlag(mB6), report))
                return false;
        for (uint32_t i = 0; i < desc->mNames.Count(); ++i)
            if (!state->ValidateCommand(reinterpret_cast<const void *>(static_cast<uintptr_t>(desc->mNames.At(i))), StoredFlag(mB6), report))
                return false;

        if (output) {
            cIVertexFormat *outputFormat = output;
            outputFormat->AddRef();
            const bool made = [&]() -> bool {
                if (mB6) {
                    ReleaseFirst(outputFormat);
                    if (!MakeDeviceVertexFormat(&outputFormat, device, output)) {
                        if (report) SayPass(0x10d, "...failed to create hw vertex format for preprocess vertex program at pass %d\n", index);
                        return false;
                    }
                    if (Streams(outputFormat) > static_cast<int32_t>(kMostStreams)) {
                        if (report)
                            SayPass(0x112, "...failed due to too many components in preprocess output vertex format at pass %d\n", index);
                        return false;
                    }
                }
                cIVertexDeclarationsManagerDX *const declarations = Declarations();
                ReleaseFirst(mP10);
                if (declarations->GetDeclaration(device, outputFormat, reinterpret_cast<cIVertexDeclarationDX **>(&mP10))) return true;
                if (report) SayPass(0x118, "...failed to create preprocess output vertex declaration at pass %d\n", index);
                return false;
            }();
            ReleaseIf(outputFormat);
            if (!made) return false;
        }

        // The preprocess program's controllers: static state only (flags exactly 1), kept in mV14.
        for (cIShaderObject *const *it = desc->mPartsA.mBegin; it != desc->mPartsA.mEnd; ++it) {
            ShaderControllerView *const controller = AsController(*it);
            if (!controller->InitDeviceResourcesAndValidate(caps, &mapped, false, report)) {
                if (report) SayPass(0x122, "...failed on cIShaderController::InitDeviceResourcesAndValidate() at pass %d\n", index);
                return false;
            }
            if (controller->GetDynamicStateFlags() != 1) {
                if (report)
                    SayPass(0x12a,
                            "...failed due to illegal shader controller dynamic state flags for preprocess vertex program at pass %d\n",
                            index);
                return false;
            }
            cIShaderPart *registered = nullptr;
            cIMaterialManager *const materials = Materials();
            materials->Register(reinterpret_cast<cIGZUnknown *>(*it), kIidController, reinterpret_cast<void **>(&registered));
            cIShaderPart *copy = registered;
            if (copy) copy->AddRef();
            PushCounted(mV14, &copy);
            ReleaseIf(copy);
            ReleaseIf(registered);
        }

        // The init's controllers, then the descriptor's: the dynamic ones (any flag) once each in mV24 (bit 1 also in mV30),
        // every one in mV58.
        const ObjectVec *const lists[2] = {&init->mObjects, &desc->mPartsB};
        const uint32_t failLines[2] = {0x138, 0x156};
        const uint32_t duplicateLines[2] = {0x144, 0x162};
        for (uint32_t l = 0; l < 2; ++l) {
            for (cIShaderObject *const *it = lists[l]->mBegin; it != lists[l]->mEnd; ++it) {
                if (!AsController(*it)->InitDeviceResourcesAndValidate(caps, &mapped, StoredFlag(mB6), report)) {
                    if (report)
                        SayPass(failLines[l], "...failed on cIShaderController::InitDeviceResourcesAndValidate() at pass %d\n", index);
                    return false;
                }
                cIShaderPart *registered = nullptr;
                cIMaterialManager *const materials = Materials();
                materials->Register(reinterpret_cast<cIGZUnknown *>(*it), kIidController, reinterpret_cast<void **>(&registered));
                const uint32_t flags = AsController(registered)->GetDynamicStateFlags();
                if (flags) {
                    if (Holds(mV24, registered)) {
                        if (report) SayPass(duplicateLines[l], "...failed due to duplicate dynamic shader controller at pass %d\n", index);
                        ReleaseIf(registered);
                        return false;
                    }
                    PushCounted(mV24, &registered);
                    if (flags & 2) PushCounted(mV30, &registered);
                }
                cIShaderPart *copy = registered;
                if (copy) copy->AddRef();
                PushCounted(mV58, &copy);
                ReleaseIf(copy);
                ReleaseIf(registered);
            }
        }

        for (uint32_t i = 0; i < 16; ++i) {
            cTextureHandle *const handle = cTextureHandle::At(&mHandles[i]);
            handle->Assign(cTextureHandle::At(&desc->mTextures[i].mHandle));
            cITexture *const texture = static_cast<cITexture *>(handle->Get());
            if (!texture) continue;
            texture->Restore();
            if (texture->IsCreated()) continue;
            if (report) {
                cITextureManager *const textures = TextureManager();
                cRZStd raw;
                ReportView *const log = OpenLog(0x17d, raw);
                const TextureInfo info = textures->GetInfo(mHandles[i]);
                log->Report(kGroup, 4u, "...failed to initialize device resources for texture '%s'\n", info.name);
            }
            return false;
        }

        // Record the pass's state into a state block.
        state->Reset(1);
        state->SetVertexShader(AsUnknown(mP50));
        state->SetPixelShader(AsUnknown(mP54));
        for (uint32_t i = 0; i < init->mNames.Count(); ++i) state->ApplyStateCommand(init->mNames.At(i));
        for (uint32_t i = 0; i < desc->mNames.Count(); ++i) state->ApplyStateCommand(desc->mNames.At(i));
        for (uint32_t i = 0; i < 16; ++i) {
            cITexture *const texture = static_cast<cITexture *>(cTextureHandle::At(&mHandles[i])->Get());
            state->SetTexture(i, texture ? texture->GetDeviceTexture(desc->mTextures[i].mValue) : nullptr);
        }
        for (cIShaderPart **it = mV58.mBegin; it != mV58.mEnd; ++it) AsController(*it)->Apply(state, &mapped);
        ReleaseFirst(mP20);
        state->CreateStateBlock(1, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&mP20)));
        if (!mP20 || !reinterpret_cast<cIStateBlock *>(mP20)->IsCreated()) {
            if (report) SayPass(0x1ac, "...failed to create state block at pass %d\n", index);
            return false;
        }

        uint32_t bits = mB4;
        for (uint8_t b : {mB5, mB6, mB7, mB3c, mB3d, mB3e}) bits = (bits << 1) | b;
        uint32_t h[9];
        h[0] = ShaderHashMix(bits);
        h[1] = Scaled(mP8);
        h[2] = Scaled(mPc);
        h[3] = Scaled(mP10);
        h[4] = ShaderPartVecHash(&mV14);
        h[5] = Scaled(mP20);
        h[6] = ShaderPartVecHash(&mV24);
        h[7] = ShaderPartVecHash(&mV30);
        h[8] = ShaderHashMix(m40);
        m44 = JenkinsHash(h, sizeof(h), 0);
        return true;
    }();
    ReleaseIf(stream);
    return ok;
}

} // namespace nGZSceneGraph
