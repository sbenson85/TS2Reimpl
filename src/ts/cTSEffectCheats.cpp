#include "ts/cTSEffectCheats.h"

#include "game/original.h"
#include "game/vtable.h"
#include "gz/CommandParser.h"
#include "gz/cArguments.h"
#include "rz/RZAutoRef.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/cRZString.h"
#include "ts/EffectsTransform.h"
#include "ts/cTSEffectsManager.h"

#include <new>
#include <string>
#include <utility>

// The effects cheats (lane YB). The option parser is the game's (CheatOptionSpec: every member an OriginalFunction); the
// editor helpers the `-edit` option calls (0xbc9c00, 0x1b14c5) are not ours yet either. Calls follow the originals' order,
// including their re-reads of the effect slot before each call.

using nGZCommandParser::cIArguments;

namespace {

// The game's option parser members.
using SpecCtorFn = void(__thiscall *)(CheatOptionSpec *, uint32_t);
using SpecDtorFn = void(__thiscall *)(CheatOptionSpec *);
using SpecDefineFn = void(__cdecl *)(CheatOptionSpec *, const char *, ...);
using SpecParseFn = void(__thiscall *)(CheatOptionSpec *, cIArguments *, void *);
using SpecHelpFn = const char *(__thiscall *)(CheatOptionSpec *, const char *, uint32_t);

// A named value of an option type (`:managerState`, `:overlayType`): the parser keeps the table, so it is static.
struct EnumValue {
    const char *name;
    int32_t value;
};
const EnumValue kManagerStates[] = {{"active", 0}, {"inactive", 1}, {"pause", 2}, {"hidden", 3}, {nullptr, 0}};
const EnumValue kOverlayTypes[] = {{"terrain", 0}, {"water", 1}, {nullptr, 0}};

// The visual effect's slots past cITSVisualEffect's that the effect cheat calls (cTSVisualEffect's 9-14).
class cITSVisualEffectControls : public cITSVisualEffect {
public:
    virtual void UpdateInside(const fx::EffectsTransform *parent) = 0;              // 9
    virtual void UpdateFrom(const fx::EffectsTransform *local) = 0;                 // 10
    virtual void SetParameters(uint32_t a, uint32_t b) = 0;                         // 11
    virtual bool EventAlias(uint32_t kind, const float *values, uint32_t count) = 0; // 12 (count of vec3s)
    virtual bool Event(uint32_t kind, const float *values, uint32_t count) = 0;      // 13
    virtual bool SetResource(uint32_t kind, cIGZUnknown *resource) = 0;             // 14
};

cITSVisualEffectControls *Controls(cITSVisualEffect *effect)
{
    return static_cast<cITSVisualEffectControls *>(effect);
}

// A cRZString on the stack, built and destroyed by the hook-shaped members.
struct LocalString {
    union {
        cRZString s;
    };
    LocalString() { s.Construct(); }
    explicit LocalString(const char *text) { s.Construct(text); }
    ~LocalString() { s.Destruct(); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
};

// A framework service (the service object's slot 5) held for a scope: released once, explicitly or at the end of the scope
// (an exception on the way: the originals release it in their unwind).
template <class Holder> struct HeldService {
    Holder ref{nullptr};
    bool released = false;
    HeldService(uint32_t classId, uint32_t interfaceId)
    {
        if (void *framework = GetServiceObject())
            VirtualSlot<bool(__thiscall *)(void *, uint32_t, uint32_t, void **)>(framework, 5)(framework, classId,
                                                                                             interfaceId, &ref.mObject);
    }
    void Release()
    {
        released = true;
        ref.ReleaseObject();
    }
    ~HeldService()
    {
        if (!released) ref.ReleaseObject();
    }
    HeldService(const HeldService &) = delete;
    HeldService &operator=(const HeldService &) = delete;
};

constexpr uint32_t kResourceClass = 0x056b906e;     // the resource manager service
constexpr uint32_t kResourceInterface = 0x656b8efc;
constexpr uint32_t kOverlayClass = 0x8af76dc8;      // the overlay managers' owner
constexpr uint32_t kOverlayInterface = 0x4af76d8c;

} // namespace

// ---- CheatOptionSpec: the game's members ----

CheatOptionSpec::CheatOptionSpec(uint32_t mode)
{
    OriginalFunction<SpecCtorFn, 0xaf88a0>()(this, mode);
}

CheatOptionSpec::~CheatOptionSpec()
{
    OriginalFunction<SpecDtorFn, 0x2b3ef0>()(this);
}

void CheatOptionSpec::Parse(cIArguments *args, void *parser)
{
    OriginalFunction<SpecParseFn, 0xafb500>()(this, args, parser);
}

const char *CheatOptionSpec::Help(const char *name, uint32_t index)
{
    return OriginalFunction<SpecHelpFn, 0xafa670>()(this, name, index);
}

// ---- the cheat family's QueryInterface ----

bool CheatQueryInterface(nGZCommandParser::cICommand *self, uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid != 1 && iid != cCheatCommand::kIid) return false;
    *out = self;
    self->AddRef();
    return true;
}

// ---- effect ----

cTSEffectCheat::cTSEffectCheat(cITSEffectsManager *manager)
    : mManager(static_cast<cTSEffectsManager *>(manager)), mSpec(1)
{
    // ORIGINAL-BUG: -texture is declared `string` (type 6: the parser std::string-assigns through the pointer, 0xafb8c7)
    // on mTexture, a const char *. The assignment treats +0xac..+0xc3 as a std::string: "reggrid2" as its pointer, the
    // slot (+0xbc) as its size and the frame (+0xc0) as its capacity. A capacity above 15 copies the text into the
    // "reggrid2" literal (read-only: the game faults); text no longer than a smaller capacity lands in the bytes at +0xac
    // (the pointer becomes characters); otherwise it grows on the heap (leaked), the slot becomes the text's length and the
    // frame the new capacity. Kept: the parser is the game's, and the declaration is what the game passes it.
    OriginalFunction<SpecDefineFn, 0xaf90e0>()(
        &mSpec, "Starts the named effect, or, with no arguments, stops the current effect.",
        "[<effectName:cstring>]", &mName,
        "Set the current effect and start it, or stop the current effect, if no effect name or options given.",
        "-pos^ <x:float> <y:float> [<z:float>^]", kPos, &mPosition[0], &mPosition[1], &mPosition[2], kPosZ,
        "Where to place the effect, or move the current effect",
        "-id <n:int>", &mId,
        "Apply this command to effect slot 'n'. The default slot is 0. This can be used to run several effects "
        "simultaneously.",
        "-start^", kStart, "(Re)start current effect.",
        "-stop^", kStop, "Stop current effect.",
        "-hard^", kHard, "Modifier on -start or -stop.",
        "-soft^", kSoft, "Modifier on -start or -stop.",
        "-clear^", kClear, "Drop reference to the current effect. (for testing.)",
        "-edit^", kEdit, "If file location tracking is on, open effect in editor",
        "-texture^ <name:string>", kTexture, &mTexture, "Set an override texture for the current effect.",
        "-frame^ <n:int>", kFrame, &mFrame, "Select the given texture particle frame.",
        "-state^ <n:int>", kState, &mState, "Select the given state. (See the effect 'states' command).",
        "-attract^ <strength:float> [<position:vector3>^]", kAttract, &mAttractStrength, &mAttractPosition,
        kAttractPosition, "Set the attractor strength/position for the current effect.",
        "-paused^ <bool>", kPaused, &mPaused, "Set whether current effect is paused.",
        "-hidden^ <bool>", kHidden, &mHidden, "Set whether current effect is hidden.",
        0);
}

cTSEffectCheat::~cTSEffectCheat() = default;

const char *cTSEffectCheat::GetName()
{
    return "effect";
}

const char *cTSEffectCheat::GetHelp(uint32_t index)
{
    return mSpec.Help(GetName(), index);
}

void cTSEffectCheat::Execute(cIArguments *args)
{
    mName = nullptr;
    mTexture = "reggrid2";
    mPosition[0] = 0.0f;
    mPosition[1] = 0.0f;
    mPosition[2] = 0.0f;
    mId = 0;
    mSpec.Parse(args, mParser);

    cITSEffectsManager *manager = mManager;
    // The terrain (the BCC's slot 7 with 0): its bounds centre a new effect, its height places one.
    void *terrain = VirtualCall<void *(__thiscall *)(void *, uint32_t), 7>(manager->GetBCC(), 0u);

    if (mName) {
        if (mId < mEffects.size()) {
            if (mEffects[mId].Get()) {
                mEffects[mId]->SetSuspended(false);
                mEffects.data()[mId]->Slot4(0);
                mEffects[mId].Reset();
            }
        } else {
            mEffects.resize(mId + 1);
        }
        if (!manager->HasEffect(mName)) {
            cTSEffectsManager::SendText("No such effect");
        } else {
            mEffects[mId].Reset();
            if (manager->CreateVisualEffect(mName, reinterpret_cast<void **>(mEffects[mId].Address()))) {
                if (!mSpec.Has(kPos) && terrain) {
                    float left, top, right, bottom;
                    VirtualCall<void(__thiscall *)(void *, float *, float *, float *, float *), 3>(terrain, &left, &top,
                                                                                                    &right, &bottom);
                    mPosition[0] = (left + right) * 0.5f;
                    mPosition[1] = (top + bottom) * 0.5f;
                    mSpec.mBits |= 1u << kPos;
                }
                cTSEffectsManager::SendText(nullptr);
            }
        }
    }

    // ORIGINAL-BUG: the slot is read without checking it exists, so `effect -pos x y` before any effect was started (or
    // with an `-id` past the slots) reads past the vector (an empty one: a null read).
    if (mSpec.Has(kPos) && mEffects.data()[mId].Get()) {
        if (!mSpec.Has(kPosZ)) {
            if (terrain && VirtualCall<bool(__thiscall *)(void *, float, float), 4>(terrain, mPosition[0], mPosition[1]))
                mPosition[2] = VirtualCall<float(__thiscall *)(void *, float, float), 5>(terrain, mPosition[0], mPosition[1]);
            // The up axis (the manager's +0xaf8): the height goes to y for 1, to x for 0.
            if (mManager->mWordAF8 == 1)
                std::swap(mPosition[1], mPosition[2]);
            else if (mManager->mWordAF8 == 0)
                std::swap(mPosition[0], mPosition[2]);
        }
        fx::EffectsTransform placement;
        placement.flags = fx::EffectsTransform::kTranslated;
        placement.pad[0] = 0; // the original leaves this byte as the stack had it
        placement.Version() = 1;
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column) placement.m[row][column] = row == column ? 1.0f : 0.0f;
        placement.scale = 1.0f;
        placement.translation = {mPosition[0], mPosition[1], mPosition[2]};
        Controls(mEffects.data()[mId].Get())->UpdateFrom(&placement);
    }

    if (mSpec.Has(kEdit) && mName) {
        void *renderer = mManager->mRenderer.mObject;
        if (const char *location =
                VirtualCall<const char *(__thiscall *)(void *, const char *, uint32_t), 5>(renderer, mName, 1u)) {
            // "file:line": the file opened in the editor at the line, else the file shown.
            LocalString file(location);
            LocalString line;
            const size_t colon = file.s.mString.rfind(':');
            if (colon != std::string::npos) {
                line.s.mString.assign(file.s.mString, colon + 1, std::string::npos);
                file.s.mString.resize(colon);
            }
            if (!OriginalFunction<bool(__cdecl *)(const char *, const char *), 0xbc9c00>()(file.s.Data(), line.s.Data()))
                OriginalFunction<void(__cdecl *)(const char *), 0x1b14c5>()(file.s.Data());
        }
    }

    if (mId < mEffects.size() && mEffects[mId].Get()) {
        if (mSpec.Has(kTexture)) {
            void *memory = ::operator new(sizeof(cRZString));
            cRZString *texture = memory ? static_cast<cRZString *>(memory)->Construct(mTexture) : nullptr;
            Controls(mEffects.data()[mId].Get())->SetResource(0, reinterpret_cast<cIGZUnknown *>(texture));
        }
        if (mSpec.Has(kFrame)) {
            const float frame = static_cast<float>(mFrame);
            Controls(mEffects.data()[mId].Get())->Event(0xd, &frame, 1);
        }
        if (mSpec.Has(kState)) {
            const float state = static_cast<float>(mState);
            Controls(mEffects.data()[mId].Get())->Event(0xf, &state, 1);
        }
        if (mSpec.Has(kAttract)) Controls(mEffects.data()[mId].Get())->Event(8, &mAttractStrength, 1);
        if (mSpec.Has(kAttractPosition)) Controls(mEffects.data()[mId].Get())->EventAlias(7, mAttractPosition, 1);
        if (mSpec.Has(kHidden)) mEffects.data()[mId]->SetHidden(mHidden);
        if (mSpec.Has(kPaused)) mEffects.data()[mId]->SetSuspended(mPaused);
        if (mName || mSpec.Has(kStart)) mEffects.data()[mId]->Slot3(mSpec.Has(kHard) ? 1 : 0);
        if (args->Count() <= 1 || mSpec.Has(kStop)) mEffects.data()[mId]->Slot4(mSpec.Has(kHard) ? 1 : 0);
        if (mSpec.Has(kClear)) mEffects[mId].Reset();
    }
}

// ---- effectsManager ----

cTSEffectsManagerCheat::cTSEffectsManagerCheat(cITSEffectsManager *manager)
    : mManager(static_cast<cTSEffectsManager *>(manager)), mSpec(1)
{
    mBoundsSchema = -1;
    OriginalFunction<SpecDefineFn, 0xaf90e0>()(
        &mSpec, "Used to control the effect manager and global effect settings.",
        ":managerState", kManagerStates,
        "", "Used for querying and controlling the effects manager.",
        "-stats^", kStats, "Lists various effect system statistics",
        "-listActive^ [<pattern:cstring>]", kListActive, &mPattern, "Lists all active effects",
        "-list^ [<pattern:cstring>]", kList, &mPattern, "Lists all or specified effects",
        "-where^ [<pattern:cstring>]", kWhere, &mPattern,
        "Lists specified effects and shows where in the script files they come from",
        "-locationInfo^", kLocationInfo, "Enables gathering of effect locations",
        "-kill^ [<pattern:cstring>]", kKill, &mPattern, "Kill any orphaned effects, or, with pattern, the named effects",
        "-showBounds^ <on:bool>", kShowBounds, &mShowBounds, "Show particle system bounds",
        "-boundsSchema^ <id:int>", kBoundsSchema, &mBoundsSchema, "Set particle bounding box schema",
        "-showColliders^ <on:bool>", kShowColliders, &mShowColliders, "Show particle colliders",
        "-noAlpha^ <on:bool>", kNoAlpha, &mNoAlpha, "Force no alpha in particles -- makes amount of overdraw clearer",
        "-appFlag^ <flag:int> <setting:bool>", kAppFlag, &mAppFlag, &mAppFlagSetting, "Set the given application flag",
        "-memStats^", kMemStats, "Lists various memory statistics",
        "-version^", kVersion, "Print current effects script version",
        "-v^", kVersion, "Shorthand for -version",
        "-save^", kSave, "Save any resource sections",
        "-load^", kLoad, "Load effects from resource ID 1",
        "-state^ <managerState>", kState, &mManagerState, "Set effects manager state",
        0);
}

cTSEffectsManagerCheat::~cTSEffectsManagerCheat() = default;

const char *cTSEffectsManagerCheat::GetName()
{
    return "effectsManager";
}

const char *cTSEffectsManagerCheat::GetHelp(uint32_t index)
{
    return mSpec.Help(GetName(), index);
}

void cTSEffectsManagerCheat::Execute(cIArguments *args)
{
    mPattern = nullptr;
    mSpec.Parse(args, mParser);
    cITSEffectsManager *manager = mManager;

    if (mSpec.Has(kStats)) {
        LocalString text;
        manager->PrintStats(&text.s);
        cTSEffectsManager::SendText(text.s.Data());
    }

    if (mSpec.Has(kListActive)) {
        // Each active effect added to a scratch scene (its component face's slot 5 with no time and zeroed counters) and
        // asked its two flags; the answers are not used.
        std::vector<fx::EffectListEntry> list;
        manager->ListActive(&list, mPattern);
        for (size_t i = 0; i < list.size(); ++i) {
            cIGZUnknown *effect = list[i].mObject;
            uint8_t counters[0x30] = {};
            VirtualCall<void(__thiscall *)(void *, void *, void *), 5>(reinterpret_cast<uint8_t *>(effect) + 4, nullptr,
                                                                      counters);
            VirtualCall<bool(__thiscall *)(void *), 20>(list[i].mObject);
            VirtualCall<bool(__thiscall *)(void *), 21>(list[i].mObject);
        }
    }

    if (mSpec.Has(kWhere) || mSpec.Has(kLocationInfo)) {
        // Location gathering (flag 1), and the parser told (its slot 8).
        if (!manager->GetFlag(1)) {
            manager->SetFlag(1, true);
            VirtualCall<void(__thiscall *)(void *, uint32_t), 8>(mManager->mParser.Get(), 1u);
        }
    }

    if (mSpec.Has(kList) || mSpec.Has(kWhere)) {
        std::vector<fx::EffectListEntry> list;
        manager->ListEffects(&list, mPattern);
    }

    if (mSpec.Has(kKill)) {
        if (!mPattern) {
            mManager->KillOrphans();
        } else {
            std::vector<fx::EffectListEntry> list;
            manager->ListActive(&list, mPattern);
            for (size_t i = 0; i < list.size(); ++i) static_cast<cITSVisualEffect *>(list[i].mObject)->Slot4(1);
        }
    }

    uint8_t *settings = reinterpret_cast<uint8_t *>(&mManager->mHalfB50);
    if (mSpec.Has(kShowBounds)) settings[0] = mShowBounds;
    // kBoundsSchema: the original hands mBoundsSchema to an empty function (0x140fe0); nothing happens.
    if (mSpec.Has(kShowColliders)) settings[1] = mShowColliders;
    if (mSpec.Has(kNoAlpha)) mManager->mByteB52 = mNoAlpha;

    if (mSpec.Has(kState)) manager->SetState(mManagerState);

    if (mSpec.Has(kAppFlag)) {
        if (static_cast<uint32_t>(mAppFlag) > 0x1f) nGZCommandParser::ThrowFormatted("invalid flag");
        manager->SetDebugBit(static_cast<uint32_t>(mAppFlag), mAppFlagSetting);
    }

    if (mSpec.Has(kMemStats)) {
        // Every description of the thirteen tables asked its size; nothing is printed.
        cTSEffectsCollection *collection = mManager;
        for (int table = 0; table < 13; ++table) {
            const uint32_t count = static_cast<uint32_t>(collection->mTables.data()[table].mDescriptions.size());
            for (uint32_t i = 0; i < count; ++i)
                static_cast<cTSEffectsCollection *>(mManager)->mTables.data()[table].mDescriptions.data()[i].Get()->GetSize();
        }
    }

    if (mSpec.Has(kVersion)) {
        LocalString text;
        text.s.Sprintf("Manager: %d\nScript: %d\nBrushes: %d\nCameras: %d\nDecals: %d\nGame: %d\nLights: %d\n"
                       "MetaParticles: %d\nModels: %d\nParticles: %d\nScreens: %d\nScrubbers: %d\nSequences: %d\n"
                       "Sounds: %d\nWater: %d\n",
                       2, 0x16, 1, 1, 1, 1, 1, 4, 1, 0xb, 1, 1, 1, 1, 1);
        cTSEffectsManager::SendText(text.s.Data());
    }

    if (mSpec.Has(kSave)) {
        // The renderer given the resource manager (its slot 2), the parser's answer shown, the renderer's manager cleared.
        void *renderer = mManager->mRenderer.mObject;
        {
            HeldService<RZAutoRef> resources(kResourceClass, kResourceInterface);
            VirtualCall<void(__thiscall *)(void *, void *), 2>(renderer, resources.ref.mObject);
            resources.Release();
        }
        void *parser = mManager->mParser.Get();
        const char *text = nullptr;
        if (!VirtualCall<bool(__thiscall *)(void *), 7>(parser)) {
            void *state = VirtualCall<void *(__thiscall *)(void *), 9>(mManager->mParser.Get());
            text = VirtualCall<const char *(__thiscall *)(void *), 44>(state);
        }
        cTSEffectsManager::SendText(text);
        VirtualCall<void(__thiscall *)(void *, void *), 2>(mManager->mRenderer.mObject, nullptr);
    }

    if (mSpec.Has(kLoad)) {
        // The main effects resource (its key, of type 0xca51189b) loaded into the collection, cleared first.
        HeldService<RZAutoRef> resources(kResourceClass, kResourceInterface);
        const uint32_t key[4] = {0xea5118b0, 0xea5118b1, 1, 0};
        fx::CountedRef<cIGZUnknown> resource;
        if (VirtualCall<bool(__thiscall *)(void *, const void *, uint32_t, void **, uint32_t, uint32_t), 5>(
                resources.ref.mObject, key, 0xca51189bu, reinterpret_cast<void **>(resource.Address()), 0u, 0u)) {
            static_cast<cITSEffectsCollection *>(mManager)->Clear();
            VirtualCall<void(__thiscall *)(void *, void *), 5>(resource.Get(), static_cast<cITSEffectsCollection *>(mManager));
            static_cast<cITSEffectsCollection *>(mManager)->Link();
        } else {
            cTSEffectsManager::SendText("Couldn't load main effect resource");
        }
    }
}

// ---- overlay ----

cOverlayCheat::cOverlayCheat(cITSEffectsManager *manager) : mSpec(1), mManager(manager)
{
    OriginalFunction<SpecDefineFn, 0xaf90e0>()(
        &mSpec, "Controls the given overlay manager",
        ":overlayType", kOverlayTypes,
        "<type:overlayType>", &mType, "Perform an operation on the given type of overlay",
        "-status^", kStatus, "Show status",
        "-decal^ <name:cstring> <location:vector3> <size:float> [<turns:float>]", kDecal, &mDecalName, &mDecalLocation,
        &mDecalSize, &mDecalTurns, "Create a decal",
        0);
}

cOverlayCheat::~cOverlayCheat() = default;

const char *cOverlayCheat::GetName()
{
    return "overlay";
}

const char *cOverlayCheat::GetHelp(uint32_t index)
{
    return mSpec.Help(GetName(), index);
}

void cOverlayCheat::Execute(cIArguments *args)
{
    mDecalSize = 32.0f;
    mDecalTurns = 0.0f;
    mWordB8 = 0x20;
    mSpec.Parse(args, mParser);

    // The overlay manager of the type: the service's slot 4 (not checked), then that object's slot 26.
    void *overlays;
    {
        HeldService<ScopedRef> service(kOverlayClass, kOverlayInterface);
        void *owner = VirtualCall<void *(__thiscall *)(void *), 4>(service.ref.mObject);
        overlays = VirtualCall<void *(__thiscall *)(void *, int32_t), 26>(owner, mType);
        service.Release();
    }
    if (!overlays) nGZCommandParser::ThrowFormatted("no manager for this overlay type");

    if (mSpec.Has(kStatus)) {
        // The status text is made and dropped: nothing shows it.
        LocalString status;
        VirtualCall<void(__thiscall *)(void *, void *), 10>(overlays, &status.s);
    }
    if (mSpec.Has(kDecal))
        VirtualCall<void(__thiscall *)(void *, const char *, const float *, float, float), 5>(
            overlays, mDecalName, mDecalLocation, mDecalSize, mDecalTurns);
}

// ---- the effects manager's KillOrphans (0xbc97f0) ----

// The active effects nobody else holds (count 1: the pool's) stopped at once, with flushing held off and the active count
// cleared. Not inlined into the manager cheat's Execute: the Execute tests stand in for it there.
__declspec(noinline) void cTSEffectsManager::KillOrphans()
{
    mWordB00 = 0;
    mNoFlush = true;
    for (auto it = mActive.begin(); it != mActive.end(); ++it) {
        cITSVisualEffect *effect = mPool[*it].Get();
        if (VisualEffectFields::RefCount(effect) == 1) effect->Slot4(1);
    }
    mNoFlush = false;
}
