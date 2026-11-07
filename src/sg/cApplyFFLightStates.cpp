// nGZSceneGraph::cApplyFFLightStates (lane TM, scenegraph M3a): see cApplyFFLightStates.h.
#include "sg/cApplyFFLightStates.h"

#include "game/game.h"
#include "gz/RenderStates.h"
#include "gz/cDeviceStateDX.h"
#include "compgeom/ConvexMath.h" // MulS, AddS: the original operand order for NaN pairs
#include "rz/cRZStd.h"
#include "sg/SGLightInterfaces.h" // cIStandardLight

#include <cstddef>
#include <cstring>

namespace nGZSceneGraph {

namespace {
using nGZGraphic4::cFFEnabledLights;
using nGZGraphic4::cFFLight;
using nGZCompGeom::convex::AddS;
using nGZCompGeom::convex::MulS;

constexpr uint8_t kEnabledLightsId = 0xb;
constexpr uint8_t kLightId = 10;

// What Init's first argument answers (the slots this part asks; cApplyRenderTarget asks slot 21 with 1).
class cIFixedFunctionCaps {
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
    virtual bool HasFeature(uint32_t which) = 0; // 20
    virtual uint32_t GetLimit(uint32_t which) = 0; // 21
};
constexpr uint32_t kHardwareVertexProcessing = 0; // slot 20's question
constexpr uint32_t kMaxEnabledLights = 0x19;      // slot 21's

// cRZStd's report slot (vtable 0xda3e6c slot 2), as cShader.cpp views it.
struct ReportView {
    virtual void __cdecl Slot0(const char *, ...) = 0;
    virtual void Slot1() = 0;
    virtual void __cdecl Report(const char *group, uint32_t level, const char *format, ...) = 0;
};

constexpr const char kControllersFile[] =
    "C:\\dev\\sims2remaster\\DL\\GZFrame\\SceneDB\\GZSceneGraph\\source\\GZSceneGraphShaderControllers.cpp";

void Say(uint32_t line, const char *text)
{
    cRZStd raw;
    cRZStd *const std = raw.Construct(8, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(kControllersFile)), line);
    reinterpret_cast<ReportView *>(std)->Report("Shader validation", 4u, text);
}

// The run of light type `type` (1 directional, 2 point, 3 spot) in the key: nibble type - 1 its start, nibble type its
// end.
uint32_t RunStart(uint32_t key, uint32_t type) { return key >> ((type - 1) << 2) & 0xf; }
uint32_t RunCount(uint32_t key, uint32_t type) { return (key >> (type << 2) & 0xf) - RunStart(key, type); }

uint32_t LightCount(uint32_t key) { return RunCount(key, 3) + RunCount(key, 2) + RunCount(key, 1); }

cFFEnabledLights EnabledLightsCommand(uint32_t count)
{
    cFFEnabledLights command{};
    command.mVTable = GameAddress<void>(cFFEnabledLights::kVTable);
    command.mId = kEnabledLightsId;
    command.mSize = static_cast<uint16_t>(sizeof(cFFEnabledLights));
    command.mCount = count;
    return command;
}

// The light command (nGZGraphic4::cFFLight's 0x58 bytes) as this part fills it. RenderStates.h's tentative names for
// +8..+0x34 do not fit what is stored here, so this view names them by what this part stores.
struct LightState : nGZGraphic4::cStateHeader {
    uint32_t number;                  // +8: counts every light of the item, all kinds
    uint32_t kind;                    // +0xc: kDirectional, kPoint, kSpot
    nGZGraphic4::Float3 position;     // +0x10
    nGZGraphic4::Float3 direction;    // +0x1c
    nGZGraphic4::Float3 colour;       // +0x28: the light's first three parameters
    nGZGraphic4::Float3 scaledColour; // +0x34: the colour times the fourth (the intensity)
    // +0x40: 1, 0, then the fifth parameter (point, spot), the seventh, the seventh plus the eighth (spot: the cone,
    // tentative), the sixth (point, spot).
    float params[6];
};
static_assert(sizeof(LightState) == sizeof(cFFLight));
static_assert(offsetof(LightState, params) == 0x40);

// The light command before the first light: no position, pointing down -z, the parameters 1, 0, 0, 0, 0, 1. Apply's
// starting colours come from the image's 0x10f67c4 (bss nothing writes), and every light overwrites them before the
// command goes out, so they are zero here.
LightState LightCommand()
{
    LightState command{};
    command.mVTable = GameAddress<void>(cFFLight::kVTable);
    command.mId = kLightId;
    command.mSize = static_cast<uint16_t>(sizeof(LightState));
    command.direction.z = -1.0f;
    command.params[0] = 1.0f;
    command.params[5] = 1.0f;
    return command;
}

void Send(nGZGraphic4::cIDeviceState *state, const void *command)
{
    state->ApplyStateCommand(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(command)));
}

// What the item's light table holds (the item's +0x3c, as cShaderApplyItem's mModulators: one table of holders, indexed
// by the runs of +0x30's nibbles), and the 12 bytes before the table (the ambient colour, tentative). A holder's first
// word is the light; the commands copy its words 9, 12 and 15 (the direction: the third column of its rotation,
// tentative) and 16 to 18 (the position).
struct LightHolder {
    cIStandardLight *light;
    float words[18]; // words 1..18
};
const LightHolder *const *LightTable(const cShaderApplyItem *item)
{
    return reinterpret_cast<const LightHolder *const *>(reinterpret_cast<uintptr_t>(item->mModulators));
}

float Word(const LightHolder *holder, uint32_t index) { return holder->words[index - 1]; }
} // namespace

cIGZUnknown *__cdecl cApplyFFLightStates::Create() { return new cApplyFFLightStates; }

bool cApplyFFLightStates::Init(void *capsArg, void *key, void *check, void *report)
{
    if (static_cast<uint8_t>(reinterpret_cast<uintptr_t>(check)) != 0) {
        auto *const caps = static_cast<cIFixedFunctionCaps *>(capsArg);
        const bool sayIt = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(report)) != 0;
        if (!caps->HasFeature(kHardwareVertexProcessing)) {
            if (sayIt) Say(0x943, "...failed -- no fixed function hw vp support\n");
            return false;
        }
        // Every light from the first directional one to the last spot one (unsigned).
        const uint32_t word = *static_cast<const uint32_t *>(key);
        if ((word >> 12 & 0xf) - (word & 0xf) > caps->GetLimit(kMaxEnabledLights)) {
            if (sayIt) Say(0x94b, "...failed -- hw ff max lights enabled CAP insufficient\n");
            return false;
        }
    }
    return cShaderControllerT<cIApplyFFLightStates>::Init(capsArg, key, check, report);
}

void cApplyFFLightStates::Apply(void *ctxArg, void *itemArg)
{
    auto *const ctx = static_cast<cShaderApplyContext *>(ctxArg);
    auto *const item = static_cast<const cShaderApplyItem *>(itemArg);
    const uint32_t key = item->mModulatorRange;

    auto enabled = EnabledLightsCommand(LightCount(key));
    if (const auto *const table = LightTable(item))
        std::memcpy(&enabled.mLights, reinterpret_cast<const uint8_t *>(table) - 0xc, sizeof enabled.mLights);
    Send(ctx->mState, &enabled);

    // One command for every light: what a light does not set stays as the light before left it (a point light's
    // direction is the last directional light's).
    auto command = LightCommand();
    const uint32_t itemWord = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(item));
    float params[8];
    const auto colours = [&] {
        command.colour = {params[0], params[1], params[2]};
        // mulss colour, intensity: the colour is the first operand (its NaN wins a NaN pair).
        command.scaledColour = {MulS(params[0], params[3]), MulS(params[1], params[3]), MulS(params[2], params[3])};
    };
    const auto position = [&](const LightHolder *light) {
        command.position = {Word(light, 16), Word(light, 17), Word(light, 18)};
    };
    const auto direction = [&](const LightHolder *light) {
        command.direction = {-Word(light, 9), -Word(light, 12), -Word(light, 15)};
    };

    command.kind = kDirectional;
    for (uint32_t i = 0, n = RunCount(key, 1); i < n; ++i) {
        const LightHolder *const light = LightTable(item)[RunStart(item->mModulatorRange, 1) + i];
        light->light->GetParams(params, itemWord);
        direction(light);
        colours();
        Send(ctx->mState, &command);
        ++command.number;
    }
    command.kind = kPoint;
    for (uint32_t i = 0, n = RunCount(key, 2); i < n; ++i) {
        const LightHolder *const light = LightTable(item)[RunStart(item->mModulatorRange, 2) + i];
        light->light->GetParams(params, itemWord);
        position(light);
        colours();
        command.params[2] = params[4];
        command.params[5] = params[5];
        Send(ctx->mState, &command);
        ++command.number;
    }
    command.kind = kSpot;
    for (uint32_t i = 0, n = RunCount(key, 3); i < n; ++i) {
        const LightHolder *const light = LightTable(item)[RunStart(item->mModulatorRange, 3) + i];
        light->light->GetParams(params, itemWord);
        position(light);
        direction(light);
        colours();
        command.params[2] = params[4];
        command.params[3] = params[6];
        command.params[4] = AddS(params[7], params[6]);
        command.params[5] = params[5];
        Send(ctx->mState, &command);
        ++command.number;
    }
}

void cApplyFFLightStates::Slot12(void *stateArg, const void *keyArg)
{
    auto *const state = static_cast<nGZGraphic4::cIDeviceState *>(stateArg);
    const uint32_t key = *static_cast<const uint32_t *>(keyArg);

    auto enabled = EnabledLightsCommand(LightCount(key));
    enabled.mLights = {0x3f800000u, 0x3f800000u, 0x3f800000u}; // 1.0f each: a white ambient colour
    Send(state, &enabled);

    auto command = LightCommand();
    command.colour = {1.0f, 1.0f, 1.0f};
    command.scaledColour = {1.0f, 1.0f, 1.0f};
    const uint32_t params[4] = {0x3e4ccccdu, 0x3ea0d97cu, 0x3f860a92u, 0x41200000u}; // 0.2, pi / 10, pi / 3, 10
    std::memcpy(&command.params[2], params, sizeof params);
    for (uint32_t kind = kDirectional; kind <= kSpot; ++kind) {
        command.kind = kind;
        for (uint32_t n = RunCount(key, kind + 1); n != 0; --n) {
            Send(state, &command);
            ++command.number;
        }
    }
}

} // namespace nGZSceneGraph
