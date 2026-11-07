// cTSSimSystem (lane IV): the whole class in the standalone shape (src/ts/cTSSimSystem.h).
#include "rz/RZMessageServer.h"
#include "ts/cTSSimSystem.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZAny.h" // GZString, gz::AnyBoolean
#include "gz/cIGZString.h"
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZMessage2Standard.h"
#include "ifaces/cIGZMessageServer.h"
#include "ifaces/cIGZPersistResourceKeyFilter.h"
#include "ifaces/cIGZPersistResourceManager.h"
#include "ifaces/cITSPersistSystem.h"
#include "ifaces/cTSBehavior.h"
#include "ifaces/cTSObject.h"
#include "misc/UtilitiesC.h" // KeyMapSlot5Walker
#include "rz/MiscHelpersB.h" // KeyMapWalker
#include "rz/RZMemory.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/StaticGetters2.h"
#include "rz/cGZAllocatorService.h" // FreeThroughAllocatorService
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZStopwatch.h"

#include <bit>
#include <cstring>
#include <new>
#include <string>

namespace {

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// Original callees nobody has replaced: the cached resource manager, the debugger service (null when not running), the
// message server, the world getter behind PlaceOnLot, the constants clients' base constructor and the cheat helper.
void *ResourceManager() { return OriginalFunction<void *(__cdecl *)(), 0x20deb5>()(); }
void *DebuggerService() { return OriginalFunction<void *(__cdecl *)(), 0x20ddda>()(); }
void *MessageServer() { return GetCachedMessageServer(); }
void *WorldSource() { return OriginalFunction<void *(__cdecl *)(), 0x20e087>()(); }
void ConstructConstantsClient(void *memory, uint32_t id)
{
    OriginalFunction<void *(__thiscall *)(void *, uint32_t), 0x4df0a0>()(memory, id);
}
void AddCheatIfEnabled(void *cheat) { OriginalFunction<void(__cdecl *)(void *), 0xb39640>()(cheat); }

void *PersistSystem() { return CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>(); }

// The COM framework's slot 3 GetClassObject(class, iid, out) into a counted member (released and nulled first).
void GetClassObject(uint32_t classId, uint32_t iid, SimSystemRef &out)
{
    void *const framework = GetDirectorFrameworkObject();
    RawSlot<3, bool>(framework, classId, iid, out.AsPPVoid());
}

// The framework service cached at 0x10f5194 (filled from GetServiceObject the first time).
void *CachedFramework()
{
    void *&framework = *GameAddress<void *>(0x10f5194);
    if (!framework) framework = GetServiceObject();
    return framework;
}

// The cheats: game classes (cITSCheatSystem::cCheatCommand subclasses: vtable, two zero words; the fixed-money ones add an
// amount and a name) built inline with the game's vtables as the original does.
constexpr uint32_t kCheatRegistryClass = 0x2a4915ea;
constexpr uint32_t kCheatRegistryIid = 0x4a490e00;

void *NewCheat(uint32_t vtable)
{
    auto *const cheat = static_cast<uint32_t *>(::operator new(0xc));
    cheat[1] = 0;
    cheat[2] = 0;
    cheat[0] = Word(GameAddress<void>(vtable));
    return cheat;
}
void *NewFixedMoneyCheat(uint32_t amount, uint32_t nameRva)
{
    auto *const cheat = static_cast<uint32_t *>(::operator new(0x14));
    cheat[1] = 0;
    cheat[2] = 0;
    cheat[0] = Word(GameAddress<void>(0xdc31ac)); // cFixedMoneyCheat
    cheat[3] = amount;
    cheat[4] = Word(GameAddress<void>(nameRva));  // the image's text
    return cheat;
}
// The cheat registry (framework slot 5 GetSystemService), slot 4 adds a command.
void AddCheat(void *cheat)
{
    ScopedRef registry{nullptr};
    if (void *const framework = GetServiceObject())
        As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kCheatRegistryClass, kCheatRegistryIid, &registry.mObject);
    if (registry.mObject) RawSlot<4, bool>(registry.mObject, cheat);
    registry.ReleaseObject();
}

// The settings registry Init and Shutdown use (framework slot 5, class 0x0ac1887e): slot 4 registers a named setting with
// a default (target, id, name, value), slot 5 unregisters it (target, id, name).
constexpr uint32_t kSettingsClass = 0x0ac1887e;
constexpr uint32_t kSettingsIid = 0x6bb22f38;
constexpr uint32_t kFreeWillSetting = 0xebd68ff9;
constexpr uint32_t kInterruptSetting = 0xabd68fed;

void *GetSettings(ScopedRef &settings)
{
    if (void *const framework = GetServiceObject()) As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kSettingsClass, kSettingsIid, &settings.mObject);
    return settings.mObject;
}

// A cRZString local (the hook-shaped string, built and destroyed by hand as the original's inlined code does).
struct LocalString {
    union {
        cRZString s;
    };
    explicit LocalString(const char *text) { s.Construct(text); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
    ~LocalString() { s.Destruct(); }
};

// The app's option object (the app singleton's slot 3) gets an option change (slot 16(id, value)).
void NotifyOption(uint32_t id, uint8_t value)
{
    if (void *const options = RawSlot<3, void *>(GetCachedSingleton())) RawSlot<16, void>(options, id, uint32_t(value));
}

// PlaceOnLot's placement request (slot 186 of the object; 0x24 bytes on the original's stack). The bytes marked unset are
// not written by the original (stack leftovers there); ours zeroes them.
#pragma pack(push, 1)
struct PlaceRequest {
    uint8_t flag;          // +0x00 1
    uint8_t unset1[3];
    int32_t x;             // +0x04 the lot centre's x << 16
    int32_t y;             // +0x08 y << 16
    int32_t level;         // +0x0c level + 1
    int32_t word10;        // +0x10 -1
    uint8_t exact;         // +0x14 1, then 0 for the second try
    uint32_t word15;       // +0x15 1 (unaligned)
    uint8_t byte19;        // +0x19 0
    uint16_t word1a;       // +0x1a 0xffff
    uint32_t word1c;       // +0x1c 0
    uint8_t byte20;        // +0x20 0xff
    uint8_t unset21[3];
};
#pragma pack(pop)
static_assert(sizeof(PlaceRequest) == 0x24, "PlaceOnLot's request");

constexpr float kMinusOne = -1.0f;

} // namespace

// ---- construction ----

// 0x416810 (888 B): the service base (id 0xdae3783f, priority -0xf4240), the message id set, the counted pointers null,
// the flag bytes, the tuning floats, the two tables, both strings, the generator seeded from the clock, the cache, both
// key lists, then the five message ids. +0x38, +0x3c, +0x40, +0x46, +0xae, +0xf5, +0x114..+0x11f, +0x165 and +0x16c are
// not written.
cTSSimSystem::cTSSimSystem() : rz::cRZSystemService(kServiceId, kPriority)
{
    mFlag41 = 0;
    mFlag42 = 0;
    mFlag43 = 0;
    mFlag44 = 0;
    mFlag45 = 0;
    static constexpr uint32_t kTuning[6] = {0x3b03126f, 0x3ca3d70a, 0x3dcccccd, 0x3b03126f, 0x3ca3d70a, 0x3dcccccd};
    for (int i = 0; i < 6; ++i) mTuning[i] = std::bit_cast<float>(kTuning[i]);
    mFreeWill = 1;
    mFlagA9 = 1;
    mFlagAA = 0;
    mInterrupt = 0;
    mFlagAC = 1;
    mFlagAD = 0;
    mCounterB0 = 0;
    mFlagF4 = 1;
    mWordF8 = 0x19;
    mFloatFC = 0.0f;
    mWord100 = 0;
    mWord104 = 0;
    mRandom.r.ConstructAt(0xffffffff, 0x416991); // the original's call returns to 0x416991
    mLoadingCount = 0;
    mFlag164 = 0;
    static constexpr int32_t kTableA[9] = {0, 1, 1, 1, 0, -1, -1, -1, 0};
    static constexpr int32_t kTableB[9] = {-1, -1, 0, 1, 1, 1, 0, -1, 0};
    for (int i = 0; i < 9; ++i) mTableA[i] = kTableA[i], mTableB[i] = kTableB[i];
    for (uint32_t id : {0xb2u, 0xafu, 0xa4u, 0xa6u, 0xc6u}) mMessageIds.insert(id);
}

// 0x416ce0: the controller shut down (its slot 5) and dropped; the members then go in reverse order (the sub-system, both
// key lists, the cache, the four service pointers, both strings, the behaviour and the object source, the id set).
cTSSimSystem::~cTSSimSystem() noexcept
{
    if (mController) {
        RawSlot<5, void>(mController.Get());
        mController.Assign(nullptr);
    }
}

cTSSimSystem *__fastcall cTSSimSystemAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cTSSimSystem;
}

// ---- cIGZUnknown ----

// 0x41a610: cITSSimSystem -> +0x1c, cIGZMessageTarget2 -> +0x20 (counted through the primary face's AddRef), else the
// service's QueryInterface.
bool cTSSimSystem::QueryInterface(uint32_t riid, void **out)
{
    void *face;
    if (riid == kMessageTargetIid) face = static_cast<cIGZMessageTarget2 *>(this);
    else if (riid == kIid) face = static_cast<cITSSimSystem *>(this);
    else return rz::cRZSystemService::QueryInterface(riid, out);
    *out = face;
    AddRef();
    return true;
}

uint32_t cTSSimSystem::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cTSSimSystem::Release() { return rz::cRZSystemService::Release(); }

// ---- cIGZSystemService ----

// 0x419100 (2512 B).
bool cTSSimSystem::Init()
{
    if (!rz::cRZSystemService::Init()) return false; // the base's (shared 0x144910): true
    cIGZMessageTarget2 *const target = this;
    reinterpret_cast<KeyMapSlot5Walker *>(&mMessageIds)->CallSlot5ForEachKey(Word(target), nullptr);
    GetClassObject(0x1b5a2044, 0xfb5a203a, mRef120);
    GetClassObject(0x1b269450, 0x7b269499, mBehavior);

    AddCheatIfEnabled(NewCheat(0xdc30fc));          // cMoneyCheat
    AddCheat(NewFixedMoneyCheat(1000, 0xdc3b0c));   // "kaChing"
    AddCheat(NewFixedMoneyCheat(50000, 0xdc3b14));  // "motherLode"
    AddCheat(NewCheat(0xdc3200));                   // cFamilyFundsCheat
    AddCheat(NewCheat(0xdc32d8));                   // cSetHourCheat
    AddCheat(NewCheat(0xdc3334));                   // cModifyNeighborhoodTerrainCheat
    AddCheatIfEnabled(NewCheat(0xdc3420));          // cAllMenusCheat
    AddCheatIfEnabled(NewCheat(0xdc34b4));          // cDebugSocialCheat
    AddCheatIfEnabled(NewCheat(0xdc3550));          // cDumpHappyCheat
    AddCheatIfEnabled(NewCheat(0xdc3648));          // cDumpLotStateCheat
    AddCheatIfEnabled(NewCheat(0xdc35fc));          // cSimCheckTreeLoggingFullCheat
    AddCheat(NewCheat(0xdc3750));                   // cMaxMotivesCheat
    AddCheat(NewCheat(0xdc37c8));                   // cMotiveDecayCheat
    AddCheat(NewCheat(0xdc3858));                   // cLockAspirationCheat
    AddCheat(NewCheat(0xdc3a60));                   // cPlumbBobToggle
    AddCheat(NewCheat(0xdc39d8));                   // cForceTwins
    AddCheat(NewCheat(0xdc395c));                   // cAddNeighborToFamily
    AddCheatIfEnabled(NewCheat(0xdc38ec));          // cDumpObjectInstancesCheat

    ScopedRef settings{nullptr};
    void *const registry = GetSettings(settings);
    {
        GZString name("freeWill");
        gz::AnyBoolean value(1);
        RawSlot<4, void>(registry, Word(target), kFreeWillSetting, &name, static_cast<cIGZAny *>(&value));
    }
    {
        GZString name("interrupt");
        gz::AnyBoolean value(0);
        RawSlot<4, void>(registry, Word(target), kInterruptSetting, &name, static_cast<cIGZAny *>(&value));
    }

    // The constants clients: game classes (base constructor 0x4df0a0 with their table ids 2 and 4).
    void *autonomy = ::operator new(0xc);
    if (autonomy) {
        ConstructConstantsClient(autonomy, 2);
        *static_cast<const void **>(autonomy) = GameAddress<void>(0xdc2f18); // cTSAutonomyConstantsClient
    }
    mAutonomyConstants = autonomy;
    void *neighborhood = ::operator new(0x28);
    if (neighborhood) {
        ConstructConstantsClient(neighborhood, 4);
        auto *const bytes = static_cast<uint8_t *>(neighborhood);
        *reinterpret_cast<const void **>(bytes) = GameAddress<void>(0xdc2ef8); // cTSNeighborhoodConstants
        bytes[0xc] = 0;
        *reinterpret_cast<uint32_t *>(bytes + 0x10) = 0;
        *reinterpret_cast<uint32_t *>(bytes + 0x14) = 0;
        *reinterpret_cast<uint32_t *>(bytes + 0x18) = 0;
        *reinterpret_cast<float *>(bytes + 0x20) = 1.0f;
        *reinterpret_cast<float *>(bytes + 0x24) = 1.0f;
    }
    mNeighborhoodConstants = neighborhood;

    GetClassObject(0x9b7ef292, 0x9b7ef291, mObjectSource);
    GetClassObject(0x5b561442, 0x3b56143a, mRef124);
    mResources.Assign(static_cast<cIGZUnknown *>(ResourceManager()));
    mDebugger.Assign(static_cast<cIGZUnknown *>(DebuggerService()));
    GetRandom()->Seed(0xffffffff);
    As<ifaces::cIGZFrameWork>(CachedFramework())->AddToTick(As<ifaces::cIGZSystemService>(static_cast<cIGZSystemService *>(this)));
    As<ifaces::cIGZFrameWork>(CachedFramework())->AddToOnIdle(As<ifaces::cIGZSystemService>(static_cast<cIGZSystemService *>(this)));
    GetClassObject(0x2f7f3357, 0x0f7f333d, mSubsystem);
    RawSlot<3, bool>(mSubsystem.Get());
    settings.ReleaseObject();
    return true;
}

// 0x41a950 (659 B).
bool cTSSimSystem::Shutdown()
{
    cIGZMessageTarget2 *const target = this;
    reinterpret_cast<KeyMapWalker *>(&mMessageIds)->CallSlot6ForEachKey(Word(target), nullptr);
    mResourceCache.clear();
    mPendingLoads.clear();
    ScopedRef settings{nullptr};
    void *const registry = GetSettings(settings);
    {
        GZString name("freeWill");
        RawSlot<5, void>(registry, Word(target), kFreeWillSetting, &name);
    }
    {
        GZString name("interrupt");
        RawSlot<5, void>(registry, Word(target), kInterruptSetting, &name);
    }
    mBehavior.Assign(nullptr);
    SetController(nullptr);
    mRef120.Assign(nullptr);
    // The constants clients are deleted (their slot 4, the deleting destructor) but the pointers kept.
    if (mAutonomyConstants) RawSlot<4, void>(mAutonomyConstants, 1u);
    if (mNeighborhoodConstants) RawSlot<4, void>(mNeighborhoodConstants, 1u);
    mRef124.Assign(nullptr);
    mResources.Assign(nullptr);
    mDebugger.Assign(nullptr);
    if (mSubsystem) {
        RawSlot<4, bool>(mSubsystem.Get());
        mSubsystem.Assign(nullptr);
    }
    As<ifaces::cIGZFrameWork>(CachedFramework())->RemoveFromTick(As<ifaces::cIGZSystemService>(static_cast<cIGZSystemService *>(this)));
    As<ifaces::cIGZFrameWork>(CachedFramework())->RemoveFromOnIdle(As<ifaces::cIGZSystemService>(static_cast<cIGZSystemService *>(this)));
    const bool result = rz::cRZSystemService::Shutdown(); // the base's (shared 0x144910): true
    settings.ReleaseObject();
    return result;
}

// 0x419fb0 (612 B): the pending keys go to the persist system's loader (slot 6) while fewer than 1000 are there; then,
// for at most 10 ms, the first loading key: loaded (slot 8) -> taken into the cache (slot 72); else, unless the loader is
// on it (slot 9), every key is requeued until one the loader is on. Last the sub-system's tick (slot 5).
bool cTSSimSystem::OnTick(uint32_t counter)
{
    void *const persist = PersistSystem();
    cRZStopwatch watch;
    watch.Construct(4);
    watch.Start();
    while (!mPendingLoads.empty() && mLoadingCount < 1000) {
        cGZPersistResourceKey key = mPendingLoads.front();
        mLoading.splice(mLoading.end(), mPendingLoads, mPendingLoads.begin());
        ++mLoadingCount;
        As<ifaces::cITSPersistSystem>(persist)->QueueResourceLoad(reinterpret_cast<const Key16 *>(&key), 0u, 60000u, 1u, 0u);
    }
    while (!mLoading.empty() && watch.Elapsed() < 10) {
        cGZPersistResourceKey key = mLoading.front();
        if (As<ifaces::cITSPersistSystem>(persist)->IsResourceAvailable(&key)) {
            mLoading.pop_front();
            --mLoadingCount;
            SimSystemRef resource;
            GetCachedResource(&key, 1, resource.AsPPVoid());
        } else {
            if (As<ifaces::cITSPersistSystem>(persist)->IsResourcePending(&key)) break;
            auto it = mLoading.begin();
            for (;;) {
                As<ifaces::cITSPersistSystem>(persist)->QueueResourceLoad(reinterpret_cast<const Key16 *>(&key), 0u, 60000u, 1u, 0u);
                if (++it == mLoading.end()) break;
                key = *it;
                if (As<ifaces::cITSPersistSystem>(persist)->IsResourcePending(&key)) break;
            }
        }
    }
    if (mSubsystem) RawSlot<5, bool>(mSubsystem.Get(), counter);
    return true;
}

// 0x419f90: the sub-system's idle (slot 6).
bool cTSSimSystem::OnIdle(uint32_t counter)
{
    if (mSubsystem) RawSlot<6, bool>(mSubsystem.Get(), counter);
    return true;
}

// ---- cIGZMessageTarget2 ----

// 0x417280 (669 B). The message as cIGZMessage2Standard (0x65297976): 0xb2 clears the pending keys; the two settings'
// messages carry an any (slot 15) whose byte becomes interrupt (slot 15) / free will (slot 9). Family deleted (0xa4),
// family evicted (0xa6), pre-lot removal (0xaf) and neighbourhood removed (0xc6) go to the scripts (the app's slot 26, its
// slot 54(name, value 0)) when slot 23's object answers 0xabea28c7. The message's slots 9 and 14 are its getters (no
// arguments: the original pushes three / two zeros to them, which they do not take).
// ORIGINAL-BUG: those 12 bytes stay on the original's stack, so its epilogue pops them into edi / ebx / esi (zeros, or
// slot 14's value) and returns them to its caller; ours keeps the caller's registers, as the calling convention requires.
bool cTSSimSystem::DoMessage(void *message)
{
    if (!message) return false;
    SimSystemRef standard;
    bool handled = false;
    if (As<ifaces::cIGZMessage2Standard>(message)->QueryInterface(0x65297976u, standard.AsPPVoid())) {
        void *const m = standard.Get();
        const uint32_t type = As<ifaces::cIGZMessage2Standard>(m)->GetType();
        if (type == 0xb2) {
            mPendingLoads.clear();
            handled = true;
        } else if (type == kInterruptSetting && RawSlot<13, bool>(m)) {
            SetInterrupt(static_cast<const uint8_t *>(RawSlot<15, void *>(m))[4]);
            handled = true;
        } else if (type == kFreeWillSetting && RawSlot<13, bool>(m)) {
            SetFreeWill(static_cast<const uint8_t *>(RawSlot<15, void *>(m))[4]);
            handled = true;
        } else {
            void *const slot23 = GetCachedSlot23();
            void *const scripts = RawSlot<26, void *>(GetCachedSingleton());
            if (scripts && slot23 && RawSlot<5>(slot23) == 0xabea28c7) {
                uint32_t nameCell = 0;
                switch (As<ifaces::cIGZMessage2Standard>(m)->GetType()) {
                case 0xaf:
                    if (As<ifaces::cIGZMessage2Standard>(m)->HasData1()) nameCell = 0x1011d1c; // NS_PreLotRemoval
                    break;
                case 0xa4:
                    if (As<ifaces::cIGZMessage2Standard>(m)->HasData1()) nameCell = 0x1011d20; // NS_FamilyDeleted
                    break;
                case 0xa6:
                    if (As<ifaces::cIGZMessage2Standard>(m)->HasData1() && RawSlot<13, bool>(m)) nameCell = 0x1011d24; // NS_FamilyEvictedFromLot
                    break;
                case 0xc6:
                    if (As<ifaces::cIGZMessage2Standard>(m)->HasData1() && RawSlot<13, bool>(m)) nameCell = 0x1011d28; // NS_NeighborhoodRemoved
                    break;
                default:
                    break;
                }
                if (nameCell) {
                    LocalString name(*GameAddress<const char *>(nameCell));
                    if (nameCell == 0x1011d24) As<ifaces::cIGZMessage2Standard>(m)->GetData2();
                    const uint32_t value = As<ifaces::cIGZMessage2Standard>(m)->GetData1();
                    RawSlot<54, void>(scripts, &name.s, value);
                    handled = true;
                }
            }
        }
    }
    return handled;
}

// ---- cITSSimSystem ----

// 0x418ab0: ids 1000-1005; *out is zeroed first, also for other ids.
bool cTSSimSystem::GetTuning(uint32_t id, float *out)
{
    *reinterpret_cast<uint32_t *>(out) = 0;
    if (id - 1000 > 5) return false;
    *out = mTuning[id - 1000];
    return true;
}

// 0x41a710.
bool cTSSimSystem::SetTuning(uint32_t id, float value)
{
    if (id - 1000 > 5) return false;
    mTuning[id - 1000] = value;
    return true;
}

// 0x41a8f0: nothing for the same controller; the old one shut down (slot 5) and dropped, the new one counted and started
// (slot 4).
void cTSSimSystem::SetController(cIGZUnknown *controller)
{
    if (mController.Get() == controller) return;
    if (mController) {
        RawSlot<5, void>(mController.Get());
        mController.Assign(nullptr);
    }
    if (controller) {
        mController.Assign(controller);
        RawSlot<4, void>(mController.Get());
    }
}

// 0x41a7e0, 0x41a680, 0x41a7b0, 0x41a6b0: the byte, then the app's option.
void cTSSimSystem::SetFreeWill(uint8_t on)
{
    mFreeWill = on;
    NotifyOption(0x1e, on);
}
void cTSSimSystem::SetFlagA9(uint8_t on)
{
    mFlagA9 = on;
    NotifyOption(0x2a, on);
}
void cTSSimSystem::SetFlagAA(uint8_t on)
{
    mFlagAA = on;
    NotifyOption(0x38, on);
}
void cTSSimSystem::SetFlagF4(uint8_t on)
{
    mFlagF4 = on;
    NotifyOption(0x39, on);
}

// 0x41b360.
void cTSSimSystem::PopB0()
{
    if (mCounterB0) --mCounterB0;
}

// 0x418bf0: out's slot 17 (Copy) from the string; true.
bool cTSSimSystem::CopyStringB4(cIGZString *out)
{
    out->Copy(*As<cIGZString>(&mStringB4.s));
    return true;
}

// 0x41a890 / 0x41a850: the string's slot 16 (operator=), its result returned.
void *cTSSimSystem::SetStringB4(const cIGZString *text) { return &As<cIGZString>(&mStringB4.s)->operator=(*text); }
void *cTSSimSystem::SetStringD4(const cIGZString *text) { return &As<cIGZString>(&mStringD4.s)->operator=(*text); }

// 0x41a860.
void cTSSimSystem::SetStringD4Text(const char *text)
{
    if (text) mStringD4.s.mString.assign(text, std::strlen(text));
}

// 0x41a260: the kind's string index (0 -> 1) in table 0x89; false for the two kinds that have none.
bool cTSSimSystem::GetSkillName(uint32_t skill, cIGZString **out)
{
    int32_t index = SkillStringIndex(skill);
    if (index == -1) return false;
    if (index == 0) index = 1;
    return LoadTableString(0x89, uint32_t(index), out);
}
bool cTSSimSystem::GetString89_5B(cIGZString **out) { return LoadTableString(0x89, 0x5b, out); } // 0x41a240
bool cTSSimSystem::GetString89_5C(cIGZString **out) { return LoadTableString(0x89, 0x5c, out); } // 0x41a220

// 0x41a390: table 0xad by flag; false for other flags.
bool cTSSimSystem::GetStringAD_A(uint32_t flag, cIGZString **out)
{
    uint32_t index;
    switch (flag) {
    case 0x800: index = 6; break;
    case 1: index = 2; break;
    case 2: index = 1; break;
    case 4: index = 5; break;
    case 0x2000: index = 7; break;
    case 0x4000: index = 4; break;
    default: return false;
    }
    return LoadTableString(0xad, index, out);
}

// 0x41ac80: table 0xae by value; false for other values.
bool cTSSimSystem::GetStringAE_A(uint32_t value, cIGZString **out)
{
    uint32_t index;
    switch (value) {
    case 2: index = 1; break;
    case 3: index = 2; break;
    case 4: index = 3; break;
    case 7: index = 4; break;
    case 8: index = 5; break;
    case 9: index = 6; break;
    case 10: index = 7; break;
    case 12: index = 0xf; break;
    default: return false;
    }
    return LoadTableString(0xae, index, out);
}

// 0x41a2a0: table 0xad by flag; other flags are the index themselves.
bool cTSSimSystem::GetStringAD_B(uint32_t flag, cIGZString **out)
{
    uint32_t index;
    switch (flag) {
    case 0x800: index = 0xd; break;
    case 1: index = 9; break;
    case 2: index = 8; break;
    case 4: index = 0xc; break;
    case 0x2000: index = 0xe; break;
    case 0x4000: index = 0xb; break;
    default: index = flag; break;
    }
    return LoadTableString(0xad, index, out);
}

// 0x41abf0: table 0xae by value; false for other values.
bool cTSSimSystem::GetStringAE_B(uint32_t value, cIGZString **out)
{
    uint32_t index;
    switch (value) {
    case 2: index = 8; break;
    case 3: index = 9; break;
    case 4: index = 10; break;
    case 7: index = 0xb; break;
    case 8: index = 0xc; break;
    case 9: index = 0xd; break;
    case 10: index = 0xe; break;
    case 12: index = 0x10; break;
    default: return false;
    }
    return LoadTableString(0xae, index, out);
}

// 0x4170c0, 0x4170e0, 0x417100, 0x417110, 0x4170d0, 0x4170f0: the behaviour's slots (no null check).
uint32_t cTSSimSystem::GetBaseIdForKind(uint32_t kind) { return As<ifaces::cTSBehavior>(mBehavior.Get())->GetBaseIdForKind(kind); }
uint32_t cTSSimSystem::GetMaxIdForKind(uint32_t kind) { return As<ifaces::cTSBehavior>(mBehavior.Get())->GetMaxIdForKind(kind); }
void cTSSimSystem::LoadBreakpoints(void *file) { As<ifaces::cTSBehavior>(mBehavior.Get())->LoadBreakpoints(file); }
void cTSSimSystem::BehaviorNothing() { RawSlot<42, void>(mBehavior.Get()); }
void cTSSimSystem::GetKindName(uint32_t kind, cIGZString *out) { As<ifaces::cTSBehavior>(mBehavior.Get())->GetKindName(kind, out); }
uint32_t cTSSimSystem::GetKindForId(uint32_t treeId) { return As<ifaces::cTSBehavior>(mBehavior.Get())->GetKindForId(treeId); }

// 0x419bf0 (660 B): the definition's object (the app's slot 26: slot 16 makes one, slot 29 finds it by id) is placed at
// the lot centre (the lot manager's slot 76 for the world's half sizes, slots 50 / 49), exactly first (slot 186), then
// anywhere near; placed: moved there (slot 171) and, for a kind-0x13 object, announced (0xaf6129b0). Not placeable: the
// object is deleted again (slot 17). Definitions of kind 7 (their slot 4's slot 3(9)) are skipped.
void cTSSimSystem::PlaceOnLot(cIGZUnknown *definition)
{
    void *const objects = RawSlot<26, void *>(GetCachedSingleton());
    const uint16_t id = static_cast<uint16_t>(RawSlot<16>(objects, definition, 0u, 0u, 0u));
    void *const object = RawSlot<29, void *>(objects, int32_t(int16_t(id)));
    if (!object) return;
    if (RawSlot<3>(RawSlot<4, void *>(definition), 9u) == 7) return;
    void *const lots = GetCachedSingletonB();
    void *const world = RawSlot<3, void *>(WorldSource());
    struct {
        float x, y;
        int32_t level;
    } centre = {kMinusOne, kMinusOne, 0};
    const int32_t a = RawSlot<50, int32_t>(world) > 0 ? RawSlot<50, int32_t>(world) / 2 - 1 : 0;
    const int32_t b = RawSlot<49, int32_t>(world) > 0 ? RawSlot<49, int32_t>(world) / 2 - 1 : 0;
    if (RawSlot<76, bool>(lots, b, a, &centre)) {
        PlaceRequest request{};
        request.flag = 1;
        request.x = int32_t(centre.x) << 16;
        request.y = int32_t(centre.y) << 16;
        request.level = centre.level + 1;
        request.word10 = -1;
        request.exact = 1;
        request.word15 = 1;
        request.byte19 = 0;
        request.word1a = 0xffff;
        request.word1c = 0;
        request.byte20 = 0xff;
        float placed[3] = {kMinusOne, kMinusOne, 0.0f};
        if (RawSlot<186, bool>(object, &request, placed, 0u)) {
            RawSlot<171, void>(object, placed, 0u, 1u);
            if (As<ifaces::cTSObject>(object)->ObjectKind() != 0x13) return;
            auto *const announce = static_cast<cRZMessage2Standard *>(RZMemory::Malloc(cRZMessage2Standard::kSize));
            if (announce) {
                announce->mVTable = GameAddress<void>(cRZMessage2Standard::kVTable);
                announce->mRefCount = 0;
                announce->mFlags = 0;
                announce->mId = 0;
                for (uint32_t &value : announce->mValue) value = 0;
                announce->mPtr20 = announce->mPtr24 = announce->mPtr28 = nullptr;
                As<ifaces::cIGZMessage2Standard>(announce)->AddRef();
            }
            As<ifaces::cIGZMessage2Standard>(announce)->SetType(0xaf6129b0u);
            RawSlot<12, void>(announce, object);
            As<ifaces::cIGZMessageServer>(MessageServer())->MessageSend(announce);
            if (announce) As<ifaces::cIGZMessage2Standard>(announce)->Release();
            return;
        }
        request.exact = 0;
        if (RawSlot<186, bool>(object, &request, placed, 0u)) {
            RawSlot<171, void>(object, placed, 0u, 1u);
            return;
        }
    }
    RawSlot<17, void>(objects, uint32_t(id));
}

// 0x418ea0 (526 B): the resource of `type` and `instance` in the container's group (its slot 17, the group's slot 11):
// the resource manager's when it has it (slot 11, then slot 3); else a 'TPRP' or 'NCRT' resource made by its class (the
// framework's classes 0x1cb36828 / 0x5452434e), keyed (slot 4), added to the manager (slot 8) and asked for `iid`.
bool cTSSimSystem::GetObjectResource(cIGZUnknown *container, uint32_t type, uint32_t instance, uint32_t iid, void **out)
{
    void *const group = RawSlot<17, void *>(container, instance);
    if (!group) return false;
    cGZPersistResourceKey key;
    key.type = type;
    key.group = RawSlot<11>(group);
    key.instance = uint32_t(int32_t(int16_t(instance)));
    key.resource = 0;
    if (As<ifaces::cIGZPersistResourceManager>(mResources.Get())->IsResourceCached(&key)) return RawSlot<3, bool>(mResources.Get(), &key, iid, out, 0u, 0u);
    SimSystemRef resource;
    if (type == 0x54505250) { // 'TPRP'
        SimSystemRef maker;
        RawSlot<3, bool>(GetDirectorFrameworkObject(), 0x1cb36828u, 0x1b36c827u, maker.AsPPVoid());
        if (RawSlot<6, bool>(maker.Get(), container, instance)) {
            RawSlot<9, void>(maker.Get());
            resource.Assign(RawSlot<32, cIGZUnknown *>(maker.Get()));
        }
    } else if (type == 0x5452434e) { // 'NCRT'
        SimSystemRef maker;
        RawSlot<3, bool>(GetDirectorFrameworkObject(), 0x5452434eu, 0x5c1286d6u, maker.AsPPVoid());
        if (RawSlot<10>(maker.Get(), group, instance) == 0) resource.Assign(RawSlot<23, cIGZUnknown *>(maker.Get()));
    } else {
        return false;
    }
    if (!resource) return false;
    RawSlot<4, void>(resource.Get(), &key);
    As<ifaces::cIGZPersistResourceManager>(mResources.Get())->CacheResource(As<ifaces::cIGZPersistResource>(resource.Get()));
    return RawSlot<0, bool>(resource.Get(), iid, out);
}

// 0x418c50 (589 B): the cached resource asked for `iid`; with the debugger running, or for a record the resource manager
// marks as plain (its slot 34 record answers 0x3b2271bb with slot 6 == 1), the manager's slot 3 instead. Otherwise the
// persist system loads it (slot 7) and it is cached.
bool cTSSimSystem::GetCachedResource(const cGZPersistResourceKey *key, uint32_t iid, void **out)
{
    if (mDebugger) return RawSlot<3, bool>(mResources.Get(), key, iid, out, 0u, 0u);
    const auto found = mResourceCache.find(*key);
    if (found != mResourceCache.end()) return RawSlot<0, bool>(found->second.Get(), iid, out);
    bool cache = true;
    SimSystemRef record;
    bool result;
    if (As<ifaces::cIGZPersistResourceManager>(mResources.Get())->FindSegmentWithKey(key, record.AsPPVoid())) {
        SimSystemRef info;
        if (RawSlot<0, bool>(record.Get(), 0x3b2271bbu, info.AsPPVoid())) cache = RawSlot<6>(info.Get()) != 1;
    }
    if (!cache) {
        result = RawSlot<3, bool>(mResources.Get(), key, iid, out, 0u, 0u);
    } else {
        SimSystemRef resource;
        uint8_t loaded = 0;
        void *const persist = PersistSystem();
        if (!RawSlot<7, bool>(persist, key, 1u, resource.AsPPVoid(), &loaded)) return false;
        mResourceCache.insert(std::pair<cGZPersistResourceKey, SimSystemRef>(*key, resource));
        result = RawSlot<0, bool>(resource.Get(), iid, out);
    }
    return result;
}

// 0x41a460 (426 B): every key of `type` the resource manager has (slot 18 with a type filter, a game class) appended to
// the pending keys; the key block (the allocator service's) freed through the service.
void cTSSimSystem::QueueResourcesOfType(uint32_t type, uint32_t)
{
    struct {
        cGZPersistResourceKey *first = nullptr, *last = nullptr, *end = nullptr;
    } keys;
    auto *filter = static_cast<uint32_t *>(::operator new(0xc));
    if (filter) {
        filter[1] = 0;
        filter[0] = Word(GameAddress<void>(0xda5cf0)); // cGZPersistResourceKeyFilterByType
        filter[2] = type;
        As<ifaces::cIGZPersistResourceKeyFilter>(filter)->AddRef();
    }
    As<ifaces::cIGZPersistResourceManager>(mResources.Get())->GetResourceKeyList(&keys, As<ifaces::cIGZPersistResourceKeyFilter>(filter));
    for (const cGZPersistResourceKey *key = keys.first; key != keys.last; ++key) mPendingLoads.push_back(*key);
    if (filter) As<ifaces::cIGZPersistResourceKeyFilter>(filter)->Release();
    if (keys.first) FreeThroughAllocatorService(keys.first, 1);
}

// 0x417120.
uint32_t cTSSimSystem::AgeGroup(int32_t age)
{
    if (age <= 1) return 0xd;
    if (age <= 2) return 2;
    if (age <= 0xf) return 1;
    if (age <= 0x12) return 3;
    return age <= 0x32 ? 0 : 4;
}

// 0x418b50: the image's table at 0x1014a18.
void *cTSSimSystem::GetStaticTable() { return GameAddress<void>(0x1014a18); }

// ---- the string helpers ----

// 0x41b1d0 (386 B). The original first makes an empty cRZString (counted), drops it again when the out-parameter is
// cleared for the resource call: made and freed here too.
bool cTSSimSystem::LoadTableString(uint32_t table, uint32_t index, cIGZString **out)
{
    cGZPersistResourceKey key;
    key.type = 0x2026960b;
    key.group = 0x7fa7dc7c;
    key.instance = table << 16 | index;
    key.resource = 0;
    void *resource = ::operator new(0x20);
    if (resource) {
        static_cast<cRZString *>(resource)->Construct();
        As<cIGZString>(resource)->AddRef();
    }
    void *const resources = ResourceManager();
    if (resource) {
        void *const held = resource;
        resource = nullptr;
        As<cIGZString>(held)->Release();
    }
    bool result = false;
    if (As<ifaces::cIGZPersistResourceManager>(resources)->GetResource(&key, cRZString::kGZIID_cIGZString, &resource, 0u, 0u)) {
        auto *text = static_cast<cRZString *>(::operator new(0x20));
        if (text) {
            const uint32_t length = As<cIGZString>(resource)->Strlen();
            const char *const chars = As<cIGZString>(resource)->Data();
            ::new (&text->mString) std::string(chars, length);
            text->mVTable = GameAddress<void>(cRZString::kVTable);
            text->mRefCount = 0;
        }
        cIGZString *const old = *out;
        if (reinterpret_cast<cIGZString *>(text) != old) {
            if (text) As<cIGZString>(text)->AddRef();
            *out = reinterpret_cast<cIGZString *>(text);
            if (old) old->Release();
        }
        result = true;
    }
    if (resource) As<cIGZString>(resource)->Release();
    return result;
}

// 0x41ad10 (636 B): a switch over the kinds 1-0xa7.
int32_t __cdecl cTSSimSystem::SkillStringIndex(uint32_t skill)
{
    switch (skill) {
    case 1: return 0x4a;
    case 2: case 0x40: case 0x45: case 0x51: case 0x5a: case 0x63: case 0x7f: case 0xa7: return 1;
    case 3: return 2;
    case 4: return 3;
    case 6: return 5;
    case 7: case 0x44: return 6;
    case 8: case 0x41: case 0x43: case 0x77: case 0x85: return 7;
    case 0xa: return 9;
    case 0xb: return 0xa;
    case 0xc: case 0x7b: case 0x80: case 0x84: return 0xb;
    case 0xe: return 0xd;
    case 0xf: return 0xe;
    case 0x10: return 0xf;
    case 0x11: return 0x10;
    case 0x12: case 0x49: case 0x53: return 0x11;
    case 0x13: case 0x21: case 0x48: case 0x52: return 0x20;
    case 0x14: case 0x55: return 0x13;
    case 0x15: case 0x54: return 0x14;
    case 0x16: return 0x15;
    case 0x17: return 0x16;
    case 0x18: return 0x17;
    case 0x19: case 0x25: case 0x32: case 0x33: case 0x34: case 0x35: case 0x37:
    case 0x8d: case 0x8e: case 0x8f: case 0x90: case 0x91: case 0x92: return 0x5b;
    case 0x1a: return 0x19;
    case 0x1b: return 0x1a;
    case 0x1c: case 0x6e: return 0x3f;
    case 0x1d: case 0x1e: case 0x47: case 0x57: case 0x5e: return 0x1d;
    case 0x1f: return 0x1e;
    case 0x22: return 0x21;
    case 0x23: return 0x22;
    case 0x24: return 0x23;
    case 0x26: return 0x25;
    case 0x27: case 0x39: case 0x3a: return 0x26;
    case 0x28: return 0x27;
    case 0x29: return 0x28;
    case 0x2a: return 0x29;
    case 0x2b: return 0x2a;
    case 0x2c: return 0x2b;
    case 0x2d: return 0x2c;
    case 0x2e: return 0x2d;
    case 0x2f: return 8;
    case 0x30: return 0x61;
    case 0x31: return 4;
    case 0x38: return 0x5d;
    case 0x3b: return 0x62;
    case 0x3c: return 0x6c;
    case 0x3d: return 0x6d;
    case 0x3e: return 0x6e;
    case 0x3f: return 0x6f;
    case 0x46: case 0x56: case 0x5f: case 0x60: return 0x1b;
    case 0x4a: case 0x83: return 0x4c;
    case 0x4b: return 0x4b;
    case 0x4c: return 0x2f;
    case 0x4d: return 0x30;
    case 0x4e: return 0x31;
    case 0x4f: return 0x32;
    case 0x50: return 0x5e;
    case 0x58: return 0x55;
    case 0x5b: return 0x33;
    case 0x5c: return 0x34;
    case 0x5d: return 0x35;
    case 0x62: return 0x36;
    case 0x64: case 0x6c: return 0x37;
    case 0x65: return 0x38;
    case 0x66: return 0x39;
    case 0x67: return 0x3a;
    case 0x69: return 0x3b;
    case 0x6a: return 0x3c;
    case 0x6b: return 0x3d;
    case 0x6d: return 0x3e;
    case 0x6f: return 0x40;
    case 0x70: return 0x41;
    case 0x71: return 0x42;
    case 0x72: return 0x43;
    case 0x73: return 0x44;
    case 0x74: case 0x75: return 0x45;
    case 0x76: return 0x46;
    case 0x78: return 0x47;
    case 0x79: return 0x48;
    case 0x7a: case 0x89: return 0x4f;
    case 0x7c: case 0x81: return -1;
    case 0x7d: return 0x49;
    case 0x82: return 0xc;
    case 0x86: return 0x4d;
    case 0x87: return 0x4e;
    case 0x88: return 0x5f;
    case 0x8a: return 0x56;
    case 0x8b: return 0x5a;
    case 0x8c: return 0x59;
    case 0x93: return 0x60;
    case 0x94: return 0x63;
    case 0x95: return 0x64;
    case 0x96: return 0x65;
    case 0x97: return 0x66;
    case 0x98: return 0x67;
    case 0x99: return 0x68;
    case 0x9a: return 0x69;
    case 0x9b: return 0x6a;
    case 0x9c: return 0x6b;
    case 0x9d: return 0x70;
    case 0x9e: return 0x71;
    case 0xa6: return 0x79;
    default: return 0;
    }
}
