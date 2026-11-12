// nTSSG::cLightSettings (lane 261108v). See cLightSettings.h.
#include "sg/cLightSettings.h"

#include "gz/CommandParser.h"    // EnumTable::Lookup
#include "gz/MaterialCommands.h" // ParserFace
#include "gz/cIGZCOM.h"
#include "rz/StaticGetters.h" // GetDirectorFrameworkObject
#include "sg/SGLightInterfaces.h"
#include "sg/cIDiffusionLightExtension.h"
#include "sg/cLightRefNode.h" // cILightRefNode

#include <bit>
#include <cstring>
#include <memory>
#include <new>

// The decorated name of the constructor: the linker fails if it is wrong.
#pragma comment(linker, "/alternatename:_cLightSettings_Constructor=??0cLightSettings@nTSSG@@QAE@XZ")

extern "C" const int cLightSettings_Anchor = 1;

namespace nTSSG {

namespace {

using namespace lightsetting;

// The attribute names (the original's table at 0xdbed50, in its order; the table sorts them as it builds).
const NameValueEntry kAttributeNames[] = {
    {"intensity", kIntensity},
    {"relSpecIntensity", kRelSpecIntensity},
    {"color", kColor},
    {"falloffRate", kFalloffRate},
    {"range", kRange},
    {"coneAngle", kConeAngle},
    {"conePenumbra", kConePenumbra},
    {"diffusionSourceMin", kSourceMin},
    {"diffusionSourceMax", kSourceMax},
    {"diffusionSourceIntensity", kSourceIntensity},
    {"diffusionSourceColor", kSourceColor},
    {"diffusionSourceIsDirectional", kSourceIsDirectional},
    {"diffusionSourceFalloff", kSourceFalloff},
    {"diffusionSourceDirectionalFalloff", kSourceDirectionalFalloff},
    {"diffusionSourceWallIntensityScale", kSourceWallIntensityScale},
    {"diffusionSourceFloorIntensityScale", kSourceFloorIntensityScale},
    {"diffusionSourceInterFloorFalloff", kSourceInterFloorFalloff},
    {nullptr, 0},
};

// Degrees to radians, as the original's constant (0x3c8efa35).
const float kDegreesToRadians = std::bit_cast<float>(0x3c8efa35u);

// What a lookup answers for a key with no values.
const cLightSettings::Values kNoValues;

// The name table looked up as the command parser's enumeration table: both are a vector of (std::string, 32-bit value)
// in the same order (shorter names first, then from the last character back), and Lookup throws on a name it lacks.
int32_t LookupAttribute(const NameValueTable &names, const char *attribute)
{
    static_assert(sizeof(NameValueTable::Entry) == sizeof(nGZCommandParser::EnumEntry) &&
                      offsetof(NameValueTable::Entry, second) == offsetof(nGZCommandParser::EnumEntry, value),
                  "a name table entry is an enumeration entry");
    return reinterpret_cast<const nGZCommandParser::EnumTable *>(&names)->Lookup(attribute);
}

const cLightSettings::Values &Find(const cLightSettings::ValueMap &map, const std::string &name, const char *tag)
{
    auto it = map.find(name + ":" + tag);
    if (it == map.end()) {
        it = map.find(std::string(tag));
        if (it == map.end())
            return kNoValues;
    }
    return it->second;
}

void Append(cLightSettings::ValueMap &map, const char *to, const cLightSettings::Values &values)
{
    if (values.empty())
        return;
    cLightSettings::Values &target = map[std::string(to)];
    target.insert(target.end(), values.begin(), values.end());
}

} // namespace

bool cLightSettings::NoCaseLess::operator()(const std::string &a, const std::string &b) const
{
    return _stricmp(a.c_str(), b.c_str()) < 0;
}

cLightSettings::cLightSettings()
{
    mName.Construct();
    mNames.Construct(kAttributeNames);
}

cLightSettings::~cLightSettings()
{
    std::destroy_at(&mNames);
    mName.Destruct();
}

cLightSettings *__fastcall cLightSettings::Construct(void *memory, void *)
{
    return new (memory) cLightSettings();
}

bool cLightSettings::QueryInterface(uint32_t riid, void **out)
{
    if (riid == kIid) {
        *out = static_cast<cILightSettings *>(this);
        AddRef();
        return true;
    }
    return rz::cRZUnknown::QueryInterface(riid, out);
}

uint32_t cLightSettings::AddRef()
{
    return rz::cRZUnknown::AddRef();
}

uint32_t cLightSettings::Release()
{
    return rz::cRZUnknown::Release();
}

bool cLightSettings::Init()
{
    return true;
}

bool cLightSettings::Shutdown()
{
    return true;
}

void cLightSettings::Clear()
{
    mLight.clear();
    mDiffusion.clear();
}

std::string &cLightSettings::SetName(const char *name)
{
    return mName.mString.assign(name);
}

const char *cLightSettings::GetName()
{
    return mName.mString.c_str();
}

void cLightSettings::AddLightValue(const char *key, const LightSettingValue &value)
{
    mLight[std::string(key)].push_back(value);
}

void cLightSettings::AddDiffusionValue(const char *key, const LightSettingValue &value)
{
    mDiffusion[std::string(key)].push_back(value);
}

const cLightSettings::Values &cLightSettings::FindLightValues(const char *tag) const
{
    return Find(mLight, mName.mString, tag);
}

const cLightSettings::Values &cLightSettings::FindDiffusionValues(const char *tag) const
{
    return Find(mDiffusion, mName.mString, tag);
}

void cLightSettings::ParseAttribute(void *parser, const char *key, const char *attribute, const char *value)
{
    nGZSceneGraph::ParserFace face(nGZSceneGraph::ParserFace::Direct{}, parser);
    LightSettingValue entry{};
    entry.id = static_cast<uint32_t>(LookupAttribute(mNames, attribute));
    float scratch[3];
    switch (entry.id) {
    case kColor: {
        const float *rgb = face.EvalVector3(scratch, value, 1);
        std::memcpy(entry.vector3, rgb, sizeof entry.vector3);
        break;
    }
    case kIntensity:
    case kRelSpecIntensity:
    case kFalloffRate:
    case kRange:
    case kConeAngle:
    case kConePenumbra:
        entry.number = face.EvalFloat(value);
        break;
    case kSourceMin:
    case kSourceMax: {
        const float *xy = face.EvalVector2(scratch, value, 1);
        std::memcpy(entry.range, xy, sizeof entry.range);
        AddDiffusionValue(key, entry);
        return;
    }
    case kSourceColor: {
        const float *rgb = face.EvalColorRGB(scratch, value);
        std::memcpy(entry.rgb, rgb, sizeof entry.rgb);
        AddDiffusionValue(key, entry);
        return;
    }
    case kSourceIsDirectional:
        entry.flag = face.EvalBool(value) ? 1 : 0;
        AddDiffusionValue(key, entry);
        return;
    case kSourceIntensity:
    case kSourceFalloff:
    case kSourceDirectionalFalloff:
    case kSourceWallIntensityScale:
    case kSourceFloorIntensityScale:
    case kSourceInterFloorFalloff:
        entry.number = face.EvalFloat(value);
        AddDiffusionValue(key, entry);
        return;
    default: // not in the table (Lookup throws first): the original keeps it as a light value with nothing set
        break;
    }
    AddLightValue(key, entry);
}

void cLightSettings::CopySettings(const char *from, const char *to)
{
    Append(mLight, to, FindLightValues(from));
    Append(mDiffusion, to, FindDiffusionValues(from));
}

void cLightSettings::Apply(nGZSceneGraph::cICompositionTreeNode *node)
{
    using namespace nGZSceneGraph;
    if (node->TestFlag(2)) { // node: cICompositionTreeNode slot 48
        const char *tag = node->GetTag(); // node: cIObjectGraphNode slot 26
        cILight *light = static_cast<cILightRefNode *>(node)->GetLight(); // node: cILightRefNode slot 95
        cIStandardLight *standard = light ? light->GetStandardLight() : nullptr; // light: cILight slot 35
        if (tag && standard) {
            for (const LightSettingValue &v : FindLightValues(tag)) {
                switch (v.id) {
                case kConeAngle: standard->SetConeAngle(v.number * kDegreesToRadians); break; // cIStandardLight slot 47
                case kRange: standard->SetRange(v.number); break;                               // slot 45
                case kColor: standard->SetColor(v.vector3); break;                              // slot 41
                case kConePenumbra: standard->SetConeFalloffAngle(v.number * kDegreesToRadians); break; // slot 48
                case kFalloffRate: standard->SetFalloff(v.number); break;                       // slot 44
                case kRelSpecIntensity: standard->SetFactor(v.number); break;                   // slot 43
                case kIntensity: standard->SetIntensity(v.number); break;                       // slot 42
                default: break;
                }
            }

            cIDiffusionLightExtension *extension = nullptr;
            // node: cIObjectGraphNode slot 24
            if (!node->FindExtension(cIDiffusionLightExtension::kIid, reinterpret_cast<void **>(&extension), nullptr)) {
                auto *com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
                if (extension) { // a miss that still answered one: let it go
                    cIDiffusionLightExtension *old = extension;
                    extension = nullptr;
                    old->Release(); // old: cIExtension slot 2 (through the virtual base)
                }
                // com: cIGZCOM slot 3
                com->GetClassObject(cIDiffusionLightExtension::kClassId, cIDiffusionLightExtension::kIid,
                                    reinterpret_cast<void **>(&extension));
                node->AddExtension(extension); // node: cIObjectGraphNode slot 22 (the cIExtension base, null kept)
            }

            // ORIGINAL-BUG: no null check on the extension here (0x3c73d0 on): when the director makes none and the
            // tag has diffusion values, the first one calls through a null face.
            for (const LightSettingValue &v : FindDiffusionValues(tag)) {
                switch (v.id) {
                case kSourceMin: extension->SetDefaultSourceMin(v.range); break;                  // extension slot 11
                case kSourceMax: extension->SetDefaultSourceMax(v.range); break;                  // slot 12
                case kSourceIntensity: extension->SetDefaultSourceIntensity(v.number); break;     // slot 13
                case kSourceColor: extension->SetDefaultSourceColor(v.rgb); break;                // slot 14
                case kSourceIsDirectional: extension->SetDefaultSourceIsDirectional(v.flag != 0); break; // slot 15
                case kSourceFalloff: extension->SetDefaultSourceFalloff(v.number); break;         // slot 16
                case kSourceDirectionalFalloff: extension->SetDefaultSourceDirectionalFalloff(v.number); break; // 17
                case kSourceWallIntensityScale: extension->SetDefaultSourceWallIntensityScale(v.number); break; // 18
                case kSourceFloorIntensityScale: extension->SetDefaultSourceFloorIntensityScale(v.number); break; // 19
                case kSourceInterFloorFalloff: extension->SetDefaultSourceInterFloorFalloff(v.number); break; // 20
                default: break;
                }
            }
            if (extension)
                extension->Release(); // extension: cIExtension slot 2
        }
    }

    const cChildRange children = node->GetChildren(); // node: cICompositionTreeNode slot 42
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd; child = child->GetNextSibling())
        Apply(child); // this: cILightSettings slot 9; child: cICompositionTreeNode slot 38
}

} // namespace nTSSG
