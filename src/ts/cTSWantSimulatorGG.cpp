// cTSWantSimulator, lane GG: the message handler (cIGZMessageTarget2 slot 3), the data container's Import (cITSDataContainer
// slot 5) and the small helpers they reach (WantMemoryStore::AdvanceToTick / CollectMemories, FindWantObject). Kept in
// its own file so lane GF's slot 52 and lane GA's class file stay separate.
#include "ts/cTSWantSimulator.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/cITSGlobals.h"
#include "ifaces/cITSWantInstance.h"
#include "ifaces/cITSWantSimulator.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/cGZAllocatorService.h"
#include "simantics/ServiceRefVector.h"

#include <new>

namespace wantsim_gg {

// Message ids the simulator registers for (the constructor's set).
constexpr uint32_t kMsgObjectDeleted = 0x82; // an object went away: drop +0x94 when it is that object
constexpr uint32_t kMsgSimDeleted = 0x9d;    // 0x9d / 0x9e: a sim left the neighbourhood (DeleteSim)
constexpr uint32_t kMsgSimRemoved = 0x9e;
constexpr uint32_t kMsgLotLoaded = 0xb3;     // the tick restarts; +0x90 / +0x94 are looked up again
constexpr uint32_t kMsgFlush = 0xb4;         // run every queued update (slot 55)
constexpr uint32_t kMsgLotUnloaded = 0xb5;   // everything per sim is dropped

constexpr uint32_t kSimMessageIid = 0x65297976;   // the message's typed face (QueryInterface on the message)
constexpr uint32_t kSimMappingIid = 0xdb5c0c2;    // the import context's sim-id mapping (QueryInterface on the context)
constexpr uint32_t kWantObjectGuid = 0x6d9e58f0;  // FindWantObject: the object kept at +0x94
constexpr uint32_t kRecord90Guid = 0x8d92dd2e;    // message 0xb3: the definition record whose object is kept at +0x90

// A counted interface pointer released on scope exit (Release is slot 2 of every counted interface).
class Held {
public:
    Held() = default;
    Held(const Held &) = delete;
    Held &operator=(const Held &) = delete;
    ~Held() noexcept(false)
    {
        if (mObject) As<::cIGZUnknown>(mObject)->Release();
    }
    void *Get() const { return mObject; }
    void **Out() { return &mObject; }

private:
    void *mObject = nullptr;
};

bool QueryFace(void *object, uint32_t iid, void **out) { return As<::cIGZUnknown>(object)->QueryInterface(iid, out); }

// A counted member assigned as the original's smart pointer does: the new one counted, then the old one released.
void AssignCounted(void *&slot, void *value)
{
    void *const old = slot;
    if (value == old) return;
    if (value) As<::cIGZUnknown>(value)->AddRef();
    slot = value;
    if (old) As<::cIGZUnknown>(old)->Release();
}

// Drops a counted member: cleared first, then released.
void DropCounted(void *&slot)
{
    if (void *old = slot) {
        slot = nullptr;
        As<::cIGZUnknown>(old)->Release();
    }
}

ifaces::cITSGlobals *Globals() { return As<ifaces::cITSGlobals>(GetCachedSingleton()); }

// ---- the typed message (kSimMessageIid) ----
uint32_t MessageType(void *message) { return VirtualCall<uint32_t(__thiscall *)(void *), 4>(message); }
bool MessageHasSim(void *message) { return VirtualCall<bool(__thiscall *)(void *), 8>(message); }
uint16_t MessageSimId(void *message) { return VirtualCall<uint16_t(__thiscall *)(void *), 9>(message); }

// ---- the import context (cITSDataImportExportContext) and its sim-id mapping ----
bool ContextGetSegment(void *context, uint32_t iid, void **out)
{
    return VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 12>(context, iid, out);
}
uint32_t ContextGroup(void *context) { return VirtualCall<uint32_t(__thiscall *)(void *), 14>(context); }
void MappingCopyIds(void *mapping, ServiceBlockVector *ids)
{
    VirtualCall<void(__thiscall *)(void *, ServiceBlockVector *), 30>(mapping, ids);
}
bool MappingMapId(void *mapping, uint32_t id, uint32_t *out)
{
    return VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t *), 32>(mapping, id, out);
}

// A want's two sim references (its parameter's sim, slot 15 / 30, and its own sim, slot 11 / 31) moved through the
// import mapping; an id of 0 or one the mapping does not know stays. The new ids are passed on as whole dwords.
void RemapSims(void *want, void *mapping)
{
    if (!want || !mapping) return;
    const int16_t paramSim = VirtualCall<int16_t(__thiscall *)(void *), 15>(want); // GetParamSimId
    uint32_t mapped = 0;
    if (paramSim != 0 && MappingMapId(mapping, uint32_t(int32_t(paramSim)), &mapped))
        VirtualCall<void(__thiscall *)(void *, uint32_t), 30>(want, mapped); // SetParamSimId
    const int16_t ownSim = VirtualCall<int16_t(__thiscall *)(void *), 11>(want); // GetSimId
    mapped = 0;
    if (ownSim != 0 && MappingMapId(mapping, uint32_t(int32_t(ownSim)), &mapped))
        VirtualCall<void(__thiscall *)(void *, uint32_t), 31>(want, mapped); // SetSimId
}

} // namespace wantsim_gg

using namespace wantsim_gg;

// ---- WantMemoryStore (lane GG) ----

// 0x551320: the id counter moves on by the ticks since the last call (never back), and the tick is recorded.
int32_t WantMemoryStore::AdvanceToTick()
{
    const uint32_t now = Globals()->GetWantFearService()->GetTick();
    const int32_t elapsed = int32_t(now - lastTick);
    lastTick = now;
    nextId += elapsed > 0 ? elapsed : 0;
    return nextId;
}

// 0x550d80: appends counted copies of the store's memories to `out`, guid by guid: all of them, or (all false) those
// not flagged for removal (slot 28).
void WantMemoryStore::CollectMemories(MemoryList *out, bool all)
{
    for (auto &entry : byGuid) {
        MemoryList &list = entry.second;
        for (MemoryNode *node = list.head->next; node != list.head; node = node->next) {
            ifaces::cITSWantInstance *memory = node->memory;
            if (all) {
                out->Append(memory);
            } else if (!memory->IsFlag10Set()) {
                memory->AddRef();
                out->Append(memory);
                memory->Release();
            }
        }
    }
}

// ---- cTSWantSimulator (lane GG) ----

// 0x48df40: the object of guid kWantObjectGuid, looked up in the object table when +0x94 is empty; when there is none,
// the game state controller's slot 19 is called.
void *cTSWantSimulator::FindWantObject()
{
    ifaces::cITSGlobals *globals = Globals();
    if (!mRef94.object) {
        void *table = globals->GetObjectTable();
        void *found = VirtualCall<void *(__thiscall *)(void *, uint32_t), 42>(table, kWantObjectGuid); // FindObjectWithGuid
        AssignCounted(mRef94.object, found);
        if (!mRef94.object) VirtualCall<void(__thiscall *)(void *), 19>(globals->GetGameStateController());
    }
    return mRef94.object;
}

// 0x487dd0 (cIGZMessageTarget2 slot 3): the sim and lot messages. True for every message with the typed face.
bool cTSWantSimulator::DoMessage(void *message)
{
    if (!message) return false;
    Held typed;
    if (!QueryFace(message, kSimMessageIid, typed.Out())) return false;
    void *const m = typed.Get();
    switch (MessageType(m)) {
    case kMsgObjectDeleted:
        if (MessageHasSim(m)) {
            const uint16_t id = MessageSimId(m);
            if (mRef94.object && VirtualCall<uint16_t(__thiscall *)(void *), 47>(mRef94.object) == id) DropCounted(mRef94.object);
        }
        break;
    case kMsgSimDeleted:
    case kMsgSimRemoved:
        if (MessageHasSim(m)) DeleteSim(int16_t(MessageSimId(m)));
        break;
    case kMsgLotLoaded: {
        mTick = 0;
        for (auto &entry : mMemoryStores) entry.second->AdvanceToTick();
        void *defs = Globals()->GetObjectDefTable();
        void *record = VirtualCall<void *(__thiscall *)(void *, uint32_t), 14>(defs, kRecord90Guid); // FindRecord
        if (record) {
            if (VirtualCall<void *(__thiscall *)(void *), 27>(record)) record = VirtualCall<void *(__thiscall *)(void *), 27>(record);
            AssignCounted(mRef90.object, VirtualCall<void *(__thiscall *)(void *), 18>(record));
        }
        FindWantObject();
        break;
    }
    case kMsgFlush:
        FlushUpdates();
        break;
    case kMsgLotUnloaded:
        DropCounted(mRef90.object);
        DropCounted(mRef94.object);
        mLifetimeWants.clear();
        mMap48.clear();
        mWants.clear();
        mFears.clear();
        mMap50.clear();
        mMap58.clear();
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
        break;
    default:
        break;
    }
    return true;
}

// 0x492fd0 (cITSDataContainer slot 5): an imported household's sims get their records read under the context's group
// and every sim reference in their memories, wants, fears and lifetime want moved through the context's id mapping.
// True once the context has the mapping and a segment.
bool cTSWantSimulator::Import(ifaces::cITSDataImportExportContext *context)
{
    bool result = false;
    Held mapping;
    if (context && QueryFace(context, kSimMappingIid, mapping.Out())) {
        Held segment;
        if (ContextGetSegment(context, kSegmentIid, segment.Out())) {
            result = true;
            ServiceBlockVector ids{};
            MappingCopyIds(mapping.Get(), &ids);
            for (const uint32_t *it = static_cast<const uint32_t *>(ids.mFirst); it != ids.mLast; ++it) {
                const uint32_t oldId = *it;
                uint32_t simWord = 0;
                if (!MappingMapId(mapping.Get(), oldId, &simWord)) continue;
                WantResourceKey key{kMemoriesRecordType, ContextGroup(context), oldId, 0};
                const int16_t simId = int16_t(simWord);
                if (!ReadSimRecord(simId, &key)) continue;
                if (WantMemoryStore *store = GetMemoryStore(simId)) {
                    MemoryList memories;
                    store->CollectMemories(&memories, true);
                    for (MemoryNode *node = memories.head->next; node != memories.head; node = node->next)
                        RemapSims(node->memory, mapping.Get());
                }
                const WantRefList &wants = mWants[simId];
                for (void **want = wants.mFirst; want != wants.mLast; ++want) RemapSims(*want, mapping.Get());
                const WantRefList &fears = mFears[simId];
                for (void **fear = fears.mFirst; fear != fears.mLast; ++fear) RemapSims(*fear, mapping.Get());
                RemapSims(mLifetimeWants[simId].object, mapping.Get());
            }
            ids.FreeBlock();
        }
    }
    return result;
}

// ---- the tick and the memory ids (lane GG) ----

// 0x5516a0: the ids restart at `id`, counted from the current tick.
void WantMemoryStore::RestartIds(int32_t id)
{
    const uint32_t now = Globals()->GetWantFearService()->GetTick();
    nextId = id;
    lastTick = now;
}

// 0x551350
void WantMemoryStore::AddToIds(int32_t amount) { nextId += amount; }

// 0x494a00: the tick goes back to 0 with every store caught up to it (and, when asked, to the old tick first).
void cTSWantSimulator::RestartTick(bool advanceFirst)
{
    if (advanceFirst)
        for (auto &entry : mMemoryStores) entry.second->AdvanceToTick();
    mTick = 0;
    for (auto &entry : mMemoryStores) entry.second->AdvanceToTick();
}

namespace wantsim_gg {
constexpr int32_t kMaxTick = 0x3fffffff; // past it the tick restarts (RestartTick)
constexpr int32_t kTicksPerUpdate = 2;
constexpr int32_t kUpdateSteps = 5;      // RunUpdate steps per Update
constexpr uint8_t kClockRunning = 2;     // the clock's slot 14 flag that lets the tick move
} // namespace wantsim_gg

// 0x491e00 (slot 38): moves the tick on while the game clock runs, then lets RunUpdate take 5 steps. Byte 0x111 guards
// against re-entry.
void cTSWantSimulator::Update()
{
    uint8_t &updating = reinterpret_cast<uint8_t *>(&m110)[1];
    if (updating) return;
    updating = 1;
    void *app = GetCachedSingleton();
    // ORIGINAL-BUG (unreachable: GetCachedSingleton makes the object when there is none): no app leaves the guard set.
    if (!app) return;
    if (void *clock = VirtualCall<void *(__thiscall *)(void *), 3>(app)) {
        if (!VirtualCall<bool(__thiscall *)(void *), 26>(clock) && (VirtualCall<uint8_t(__thiscall *)(void *), 14>(clock) & kClockRunning)) {
            mTick += kTicksPerUpdate;
            if (int32_t(mTick) > kMaxTick) RestartTick(true);
        }
    }
    RunUpdate(kUpdateSteps, 0);
    updating = 0;
}

// 0x496660 (slot 47): the sim's memory ids restart at `nextId`. A sim with no live person and no store is restored first.
void cTSWantSimulator::SetMemoryIds(uint32_t simWord, int32_t nextId)
{
    const int16_t simId = int16_t(simWord);
    WantSimContext context;
    context.Construct(simId);
    if (!context.person && mMemoryStores.find(simId) == mMemoryStores.end()) RestoreSimWants(simWord);
    GetMemoryStore(simId)->RestartIds(nextId);
    context.Destruct();
}

// 0x490520 (slot 48): the same, adding `amount` to the sim's next memory id.
void cTSWantSimulator::AddMemoryIds(uint32_t simWord, int32_t amount)
{
    const int16_t simId = int16_t(simWord);
    WantSimContext context;
    context.Construct(simId);
    if (!context.person && mMemoryStores.find(simId) == mMemoryStores.end()) RestoreSimWants(simWord);
    GetMemoryStore(simId)->AddToIds(amount);
    context.Destruct();
}

// ---- adapters (lane GG) ----

using GGAdapters = cTSWantSimulatorAdapters;

void *__fastcall GGAdapters::FindWantObject(cTSWantSimulator *self, void *)
{
    [[msvc::forceinline_calls]] return self->cTSWantSimulator::FindWantObject();
}
