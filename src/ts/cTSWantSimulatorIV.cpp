// Lane IV: cTSWantSimulator's Init (slot 3, 0x4905e0), Shutdown (slot 4, 0x496720) and AddCategoryWant (slot 5, 0x486430).
#include "ts/cTSWantSimulator.h"

#include "app/AppInit.h" // GetOptionMask
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZUnknown.h"
#include "misc/UtilitiesC.h"   // KeyMapSlot5Walker
#include "rz/MiscHelpersB.h"   // KeyMapWalker
#include "rz/Singleton_20ddfb.h"
#include "simantics/ServiceRefVector.h"
#include "simantics/cTSBehaviorTree.h" // RefPtrVector
#include "ts/cTSWantSimulatorTuningLoader.h"
#include "ts/cTSWantTreeManager.h"

#include <new>

namespace {

// The cheat registry Init adds two of its cheats to (the framework's slot 5 GetSystemService(class, iid, out); the
// registry's slot 4 adds a command). The other two go through 0xb39640, which adds them only when cheats are enabled.
constexpr uint32_t kCheatRegistryClass = 0x2a4915ea;
constexpr uint32_t kCheatRegistryIid = 0x4a490e00;
constexpr uint32_t kWantInstanceSize = 0x3c; // cTSWantInstance (pooled, 0x54e570)

// Init's cheat commands: game classes (cITSCheatSystem::cCheatCommand, 0xc bytes: vtable, two words), built inline as the
// original does and handed over uncounted (the registry counts them; nobody frees one the registry never took).
constexpr uint32_t kAspirationCheatVTable = 0xdcb348;
constexpr uint32_t kDumpWantsVTable = 0xdcb278;
constexpr uint32_t kAspirationPointsCheatVTable = 0xdcb3bc;
constexpr uint32_t kAspirationLevelCheatVTable = 0xdcb448;

struct GameCheatCommand {
    const void *vtable;
    uint32_t word4;
    uint32_t word8;
};
static_assert(sizeof(GameCheatCommand) == 0xc, "cCheatCommand size");

void *NewCheat(uint32_t vtable)
{
    auto *const cheat = static_cast<GameCheatCommand *>(::operator new(sizeof(GameCheatCommand)));
    cheat->word4 = 0;
    cheat->word8 = 0;
    cheat->vtable = GameAddress<void>(vtable);
    return cheat;
}

void AddCheatIfEnabled(void *cheat) { OriginalFunction<void(__cdecl *)(void *), 0xb39640>()(cheat); }

void AddCheat(void *cheat)
{
    ScopedRef registry{nullptr};
    if (void *const framework = GetServiceObject())
        VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t, void **), 5>(framework, kCheatRegistryClass, kCheatRegistryIid,
                                                                                &registry.mObject);
    if (registry.mObject) VirtualCall<bool(__thiscall *)(void *, void *), 4>(registry.mObject, cheat);
    registry.ReleaseObject();
}

// The aspiration-level scan's object-definition reads (slots 3 / 4 of what an entry's slot 1 then slot 4 return).
int32_t Field(void *data, uint32_t id) { return VirtualCall<int32_t(__thiscall *)(void *, uint32_t), 3>(data, id); }
bool Field(void *data, uint32_t id, int32_t *out) { return VirtualCall<bool(__thiscall *)(void *, uint32_t, int32_t *), 4>(data, id, out); }

} // namespace

// 0x4905e0 (slot 3): once. The option mask; a new want tree manager (definitions and trees) initialised; the tuning;
// the four cheats; the highest level of every object-definition entry per aspiration key; the message ids registered.
uint32_t cTSWantSimulator::Init()
{
    if (mInitialized) return 1; // the original leaves fs:[0]'s word in eax here; nothing reads it
    mInitialized = 1;
    mOptionMask = int32_t(appinit::GetOptionMask());
    {
        void *const manager = static_cast<cITSWantTreeManager *>(cTSWantTreeManager::Create());
        void *const old = mDefinitions.object;
        if (manager != old) {
            if (manager) static_cast<cIGZUnknown *>(manager)->AddRef();
            mDefinitions.object = manager;
            if (old) static_cast<cIGZUnknown *>(old)->Release();
        }
    }
    VirtualCall<bool(__thiscall *)(void *), 3>(mDefinitions.object); // cITSWantTreeManager::Init (no null test)
    cTSWantSimulatorTuningLoader::LoadTuning(this);
    AddCheatIfEnabled(NewCheat(kAspirationCheatVTable));
    AddCheatIfEnabled(NewCheat(kDumpWantsVTable));
    AddCheat(NewCheat(kAspirationPointsCheatVTable));
    AddCheat(NewCheat(kAspirationLevelCheatVTable));
    if (void *const globals = GetCachedSingleton()) {
        if (void *const table = VirtualCall<void *(__thiscall *)(void *), 16>(globals)) {
            const int32_t count = VirtualCall<int32_t(__thiscall *)(void *), 11>(table);
            for (int32_t i = 0; i < count; ++i) {
                void *entry = VirtualCall<void *(__thiscall *)(void *, int32_t), 13>(table, i);
                if (!entry) continue;
                entry = VirtualCall<void *(__thiscall *)(void *), 1>(entry);
                if (!entry) continue;
                void *const data = VirtualCall<void *(__thiscall *)(void *), 4>(entry);
                if (!data) continue;
                int32_t key = Field(data, 0x2f);
                if (!key || !Field(data, 0x27)) continue;
                int32_t a = 0, b = 0, c = 0;
                const bool qualifies = (Field(data, 0x45, &a) && a > 0) ||
                                       (Field(data, 0x28, &b) && Field(data, 0x5e, &c) && b > 0 && c > 0);
                if (!qualifies) continue;
                const int32_t level = Field(data, 0x12);
                int32_t &highest = mMap84[key];
                if (highest < level) highest = level;
            }
        }
    }
    reinterpret_cast<KeyMapSlot5Walker *>(&mMessageIds)
        ->CallSlot5ForEachKey(uint32_t(uintptr_t(static_cast<cIGZMessageTarget2 *>(this))), nullptr);
    return uint32_t(uintptr_t(*reinterpret_cast<void *const *>(&mMessageIds))); // what the walk leaves in eax: the set's head
}

namespace {
// A counted member dropped as the original does: cleared first, then released. Returns Release's result.
uint32_t Drop(WantRef &ref)
{
    void *const old = ref.object;
    if (!old) return 0;
    ref.object = nullptr;
    return static_cast<cIGZUnknown *>(old)->Release();
}
} // namespace

// 0x496720 (slot 4): Init undone. The message ids unregistered, every per-sim table emptied, the tree manager shut down
// and dropped, the memory stores deleted, the category wants, level tables and the 0x7c map emptied, the counted members
// dropped. The original tail-calls the last Release (+0x20), so its result is that Release's when +0x20 was set.
uint32_t cTSWantSimulator::Shutdown()
{
    if (!mInitialized) return 0;
    mInitialized = 0;
    reinterpret_cast<KeyMapWalker *>(&mMessageIds)
        ->CallSlot6ForEachKey(uint32_t(uintptr_t(static_cast<cIGZMessageTarget2 *>(this))), nullptr);
    mLifetimeWants.clear();
    reinterpret_cast<RefPtrVector *>(&mVec68)->Clear();
    mMap48.clear();
    mWants.clear();
    mFears.clear();
    mMap50.clear();
    mMap58.clear();
    if (mDefinitions.object) {
        VirtualCall<bool(__thiscall *)(void *), 4>(mDefinitions.object); // cITSWantTreeManager::Shutdown
        Drop(mDefinitions);
    }
    for (auto &entry : mMemoryStores) {
        WantMemoryStore *const store = entry.second;
        if (mCurrentStore == store) mCurrentStore = nullptr;
        // ORIGINAL-BUG (unreachable: the table never holds null): Clear runs before the null test.
        store->Clear();
        if (store) {
            store->Destruct();
            ::operator delete(store, sizeof(WantMemoryStore));
        }
    }
    mMemoryStores.clear();
    mCategoryWants.clear();
    mLevelTables.clear();
    mMap7c.clear();
    Drop(mRef90);
    Drop(mRef94);
    Drop(mRef1c);
    return Drop(mRef20);
}

// 0x486430 (slot 5): a want of `guid` (any sim, its parameter's kind the definition's object type) appended to the
// category's list, when the guid has a definition.
uint32_t cTSWantSimulator::AddCategoryWant(uint32_t category, uint32_t guid)
{
    void *const definition = reinterpret_cast<void *>(uintptr_t(GetWantDefinition(guid))); // slot 23, through the vtable
    if (!definition) return 0;
    WantInstanceParam param;
    OriginalFunction<WantInstanceParam *(__thiscall *)(WantInstanceParam *), 0x54e500>()(&param);
    param.kind = VirtualCall<uint32_t(__thiscall *)(void *), 16>(definition); // GetObjectType
    void *want = nullptr;
    if (void *block = OriginalFunction<void *(__cdecl *)(uint32_t), 0x54e570>()(kWantInstanceSize))
        want = OriginalFunction<void *(__thiscall *)(void *, int32_t, uint32_t, WantInstanceParam *, void *, void *), 0x54e2b0>()(
            block, -1, guid, &param, nullptr, nullptr);
    WantRefList &list = mCategoryWants[int32_t(category)];
    if (want) static_cast<cIGZUnknown *>(want)->AddRef();
    reinterpret_cast<ServiceRefVector *>(&list)->PushBack(want);
    if (want) static_cast<cIGZUnknown *>(want)->Release();
    return 0;
}
