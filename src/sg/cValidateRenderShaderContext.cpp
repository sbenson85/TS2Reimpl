// nGZSceneGraph::cValidateRenderShaderContext (lane TM, scenegraph M3a): see cValidateRenderShaderContext.h.
#include "sg/cValidateRenderShaderContext.h"

#include "gz/GraphicServices.h" // GetVertexFormatsManagerCached
#include "gz/cIGZString.h"
#include "gz/cVertexFormat.h"
#include "rz/cRZStd.h"
#include "rz/cRZString.h"
#include "sg/cShader.h"      // ShaderHashMix
#include "sg/cStringPool.h"  // JenkinsHash

#include <cstring>

namespace nGZSceneGraph {

namespace {
// cRZStd's report slot (vtable 0xda3e6c slot 2), as cShader.cpp views it.
struct ReportView {
    virtual void __cdecl Slot0(const char *, ...) = 0;
    virtual void Slot1() = 0;
    virtual void __cdecl Report(const char *group, uint32_t level, const char *format, ...) = 0;
};

constexpr const char kControllersFile[] =
    "C:\\dev\\sims2remaster\\DL\\GZFrame\\SceneDB\\GZSceneGraph\\source\\GZSceneGraphShaderControllers.cpp";

ReportView *Reporter(cRZStd &raw, uint32_t line)
{
    return reinterpret_cast<ReportView *>(
        raw.Construct(8, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(kControllersFile)), line));
}

constexpr const char kGroup[] = "Shader validation";

constexpr uint32_t kNoElement = 6; // the type FindElementAny answers for an element the format lacks

// Light count `type`'s running total in the key's first two words (four bits each).
uint32_t LightTotal(const uint32_t *key, uint32_t type) { return key[type >> 3] >> ((type & 7) << 2) & 0xf; }
} // namespace

const char *__cdecl LightTypeName(uint32_t type)
{
    switch (type) {
    case 0: return "ambient";
    case 1: return "directional";
    case 2: return "point";
    case 3: return "spot";
    case 4: return "environmentCube";
    case 5: return "linearFog";
    case 6: return "drawState";
    case 7: return "user0";
    case 8: return "user1";
    case 9: return "user2";
    case 10: return "user3";
    case 11: return "user4";
    case 12: return "user5";
    default: return "UNKNOWN";
    }
}

cIGZUnknown *__cdecl cValidateRenderShaderContext::Create() { return new cValidateRenderShaderContext; }

cValidateRenderShaderContext::~cValidateRenderShaderContext() = default;

bool cValidateRenderShaderContext::Init(void *caps, void *contextArg, void *table, void *reportArg)
{
    const bool report = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(reportArg)) != 0;
    const auto *const key = static_cast<const uint32_t *>(contextArg);
    auto *const formats = static_cast<nGZGraphic4::cIVertexFormatsManager *>(nGZGraphic4::GetVertexFormatsManagerCached());
    nGZGraphic4::cIVertexFormat *const format = formats->GetFormat(key[2] >> 4 & 0x7f); // not counted
    cRZString name;
    name.Construct();
    if (report) format->Describe(reinterpret_cast<cIGZString *>(&name));
    bool ok = false;
    cRZStd raw;
    [&] {
        for (const Element &e : mElements) {
            nGZGraphic4::cVertexElement found;
            const uint32_t type = format->FindElementAny(&found, e.usage, e.index)->type;
            if (e.mode == kForbidden) {
                if (type == kNoElement) continue;
                if (report)
                    Reporter(raw, 0xc5)->Report(kGroup, 4u,
                                                "...failed -- vertex format %s has unsupported vertex format element %s:%d\n",
                                                name.Data(), nGZGraphic4::VertexUsageName(e.usage), uint32_t(e.index));
                return;
            }
            if (e.mode == kRequired && type == kNoElement) {
                if (report)
                    Reporter(raw, 0xbc)->Report(
                        kGroup, 4u, "...failed -- vertex format %s does not have required vertex format element %s:%d\n",
                        name.Data(), nGZGraphic4::VertexUsageName(e.usage), uint32_t(e.index));
                return;
            }
        }
        const uint32_t bits = key[2];
        for (uint32_t i = 0; i < kFlags; ++i) {
            const bool set = ((bits & 0xf) >> i & 1) != 0;
            if (mFlags[i] == kForbidden && set) {
                if (report)
                    Reporter(raw, 0xdf)->Report(kGroup, 4u, "...failed -- render shader context has unsupported flag %d set\n", i);
                return;
            }
            if (mFlags[i] == kRequired && !set) {
                if (report)
                    Reporter(raw, 0xd6)->Report(kGroup, 4u,
                                                "...failed -- render shader context does not have required flag %d set\n", i);
                return;
            }
        }
        for (uint32_t i = 0; i < kLightTypes; ++i) {
            if (mLights[i] < 0) continue;
            // ORIGINAL-BUG: type 0's count subtracts a total read from word 0x1fffffff of the key (i - 1 unsigned),
            // 0x7ffffffc bytes past it; nothing sets an ambient light requirement.
            const int32_t have = int32_t(LightTotal(key, i) - LightTotal(key, i - 1));
            if (have == mLights[i]) continue;
            if (report)
                Reporter(raw, 0xf4)->Report(kGroup, 4u, "...failed -- require %d lights of type %s, have %d\n", mLights[i],
                                            LightTypeName(i), have);
            return;
        }
        if (mTransforms >= 0) {
            const uint32_t have = bits >> 15 & 0x7f;
            if (uint32_t(mTransforms) != have) {
                if (report)
                    Reporter(raw, 0x101)->Report(kGroup, 4u, "...failed -- require %d transforms, have %d\n", mTransforms, have);
                return;
            }
        }
        if (mMorphChannels >= 0) {
            const uint32_t have = bits >> 22 & 0x3f;
            if (uint32_t(mMorphChannels) != have) {
                if (report)
                    Reporter(raw, 0x10d)->Report(kGroup, 4u, "...failed -- require %d morph target channels, have %d\n",
                                                 mMorphChannels, have);
                return;
            }
        }
        if (mViewerRenderType >= 0) {
            const uint32_t have = bits >> 11 & 0xf;
            if (uint32_t(mViewerRenderType) != have) {
                if (report)
                    Reporter(raw, 0x119)->Report(kGroup, 4u, "...failed -- require viewer render type %d, have %d\n",
                                                 mViewerRenderType, have);
                return;
            }
        }
        ok = cShaderControllerT<cIValidateRenderShaderContext>::Init(caps, contextArg, table, reportArg);
    }();
    name.Destruct();
    return ok;
}

void cValidateRenderShaderContext::AddVertexElement(uint32_t usage, uint8_t index, uint8_t mode)
{
    Element e{};
    e.usage = usage;
    e.index = index;
    e.mode = mode;
    mElements.push_back(e);
}

uint32_t cValidateRenderShaderContext::HashParams()
{
    uint32_t parts[6];
    parts[0] = 0;
    if (!mElements.empty())
        parts[0] = JenkinsHash(mElements.data(), static_cast<uint32_t>(mElements.size() * sizeof(Element)), 0);
    parts[1] = JenkinsHash(mFlags, sizeof mFlags, 0);
    parts[2] = JenkinsHash(mLights, sizeof mLights, 0);
    parts[3] = ShaderHashMix(static_cast<uint32_t>(mTransforms));
    parts[4] = ShaderHashMix(static_cast<uint32_t>(mMorphChannels));
    parts[5] = ShaderHashMix(static_cast<uint32_t>(mViewerRenderType));
    return JenkinsHash(parts, sizeof parts, 0);
}

bool cValidateRenderShaderContext::EqualParams(cIValidateRenderShaderContext *otherArg)
{
    const auto *const other = static_cast<cValidateRenderShaderContext *>(otherArg);
    if (mElements.size() != other->mElements.size()) return false;
    if (!mElements.empty() &&
        std::memcmp(mElements.data(), other->mElements.data(), mElements.size() * sizeof(Element)) != 0)
        return false;
    return std::memcmp(mFlags, other->mFlags, sizeof mFlags) == 0 &&
           std::memcmp(mLights, other->mLights, sizeof mLights) == 0 && mTransforms == other->mTransforms &&
           mMorphChannels == other->mMorphChannels && mViewerRenderType == other->mViewerRenderType;
}

} // namespace nGZSceneGraph
