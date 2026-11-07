#include "ts/cTSWantSimulator.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/GscContext.h"
#include "ifaces/GscObjectTable.h"
#include "ifaces/cITSNeighbor.h"
#include "ifaces/cITSNeighborManager.h"
#include "ifaces/cITSNeighborhood.h"
#include "ifaces/cITSPerson.h"
#include "ifaces/TokenCurrentHood.h"
#include "ifaces/TokenHoodGroupSource.h"
#include "ifaces/TokenKeyHolder.h"
#include "ifaces/TokenNeighborhood.h"
#include "ifaces/TokenNeighborhoodTable.h"
#include "ifaces/TokenService23.h"
#include "ifaces/WantDefinition.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "ifaces/cIGZPersistDBRecord.h"
#include "ifaces/cIGZPersistDBSegment.h"
#include "ifaces/cIGZSerializable.h"
#include "ifaces/cITSGlobals.h"
#include "ifaces/cITSPersistSystem.h"
#include "ifaces/cITSWantInstance.h"
#include "ifaces/cITSWantSimulator.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters2.h"
#include "rz/cGZAllocatorService.h"
#include "simantics/ServiceRefVector.h"
#include "simantics/cTSBehaviorTree.h"

#include <emmintrin.h>
#include <new>

namespace {

// The person-like argument's slots (thiscall).
int16_t Attribute(void *person, uint32_t id, uint32_t flag) // slot 10 (+0x28)
{
    return VirtualCall<int16_t(__thiscall *)(void *, uint32_t, uint32_t), 10>(person, id, flag);
}
int16_t Motive(void *person, uint32_t id) // slot 16 (+0x40)
{
    return VirtualCall<int16_t(__thiscall *)(void *, uint32_t), 16>(person, id);
}
uint32_t PersonKey(void *person) // slot 18 (+0x48)
{
    return VirtualCall<uint32_t(__thiscall *)(void *), 18>(person);
}
uint16_t PersonId(void *person) // slot 57 (+0xe4)
{
    return VirtualCall<uint16_t(__thiscall *)(void *), 57>(person);
}

// The level (0..5) of `value` against thresholds [1]..[5]: each threshold reached sets its own
// level independently (the original's cmovge chain), so a later one wins even past a gap.
uint32_t LevelOf(int32_t value, const std::vector<int32_t> &t)
{
    uint32_t level = value >= t[1] ? 1 : 0;
    if (value >= t[2]) level = 2;
    if (value >= t[3]) level = 3;
    if (value >= t[4]) level = 4;
    if (value >= t[5]) level = 5;
    return level;
}

} // namespace

WantRefList::~WantRefList()
{
    if (!mFirst) return;
    for (void **it = mFirst; it != mLast; ++it)
        if (*it) VirtualCall<void(__thiscall *)(void *), 2>(*it);
    FreeThroughAllocatorService(mFirst, uint32_t(mEnd - mFirst));
    mFirst = mLast = mEnd = nullptr;
}

WantRef::~WantRef()
{
    if (object) VirtualCall<void(__thiscall *)(void *), 2>(object);
}

cTSWantSimulator::cTSWantSimulator()
{
    // The containers and the counted pointers (WantRef) are members; 0xb4, 0xbe and 0xcc are left as they were.
    mRefCount = 0;
    mInitialized = 0;
    mTick = 0;
    mOptionMask = 1;
    mUpdateState = 0;
    mCurrentSim = 0;
    mC8 = 0;
    mD0 = 0;
    mF8 = 0;
    m104 = 1;
    mPaused = 0;
    m10C = 0;
    m110 = 0;
    for (uint32_t id : {0xb3u, 0xb4u, 0xb5u, 0x9du, 0x9eu, 0x82u}) mMessageIds.insert(id);
}

// The original destroys the members in reverse order and releases the counted pointers between them (+0xc4, +0xc0
// after the vectors, +0x94, +0x90 after the 0x98 map, +0x24, +0x20, +0x1c before the id set): WantRef members, so
// the implicit member destruction is that order.
cTSWantSimulator::~cTSWantSimulator() {}

bool cTSWantSimulator::QueryInterface(uint32_t id, void **out)
{
    if (id == 1 || id == 0xd7ee268) {
        *out = static_cast<cITSWantSimulator *>(this);
    } else if (id == 0x8db5bf48) {
        *out = static_cast<cITSDataContainer *>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

uint32_t cTSWantSimulator::Release()
{
    if (mRefCount > 1) return uint32_t(--mRefCount);
    delete this;
    return 0;
}

uint32_t cTSWantSimulator::LevelForObject(void *person)
{
    void *service = GetCachedSingletonC();
    const int32_t attribute = Motive(person, 0x3a);
    const int32_t key = int32_t(VirtualCall<uint32_t(__thiscall *)(void *, int32_t), 74>(service, attribute));
    const std::vector<int32_t> &table = mLevelTables[key];
    if (table.size() < 7) return 2;
    return LevelOf(Motive(person, 0xa2) * 10, table);
}

uint32_t cTSWantSimulator::LevelForKey(void *person)
{
    const int32_t key = int32_t(PersonKey(person));
    const std::vector<int32_t> &table = mLevelTables[key];
    if (table.size() < 7) return 2;
    return LevelOf(Attribute(person, 0xa2, 1) * 10, table);
}

uint32_t cTSWantSimulator::ScoreMotiveA2(void *person, uint8_t apply)
{
    const int32_t value = Motive(person, 0xa2) * 10;
    if (!apply) return uint32_t(value);
    void *service = GetCachedSingletonC();
    const int32_t attribute = Motive(person, 0x3a);
    const uint32_t key = VirtualCall<uint32_t(__thiscall *)(void *, int32_t), 74>(service, attribute);
    return uint32_t(ScoreForValue(int32_t(key), value));
}

uint32_t cTSWantSimulator::ScoreAttributeA2(void *person, uint8_t apply)
{
    const int32_t value = Attribute(person, 0xa2, 1) * 10;
    if (!apply) return uint32_t(value);
    const uint32_t key = PersonKey(person);
    return uint32_t(ScoreForValue(int32_t(key), value));
}

int32_t cTSWantSimulator::ScaleToRange(int32_t raw, uint8_t scale)
{
    if (!scale) return raw;
    float value = float(raw);
    if (mRef20.object) {
        const float mapped = VirtualCall<float(__thiscall *)(void *, float), 8>(mRef20.object, value);
        value = mapped / *GameAddress<const float>(0xda8654);
    } else {
        value = value / *GameAddress<const float>(0xdcb608);
    }
    // maxss / minss keep their operand order (NaN handling).
    __m128 x = _mm_max_ss(_mm_setzero_ps(), _mm_set_ss(value));
    x = _mm_min_ss(_mm_set_ss(*GameAddress<const float>(0xd9c698)), x);
    x = _mm_mul_ss(x, _mm_set_ss(*GameAddress<const float>(0xd9fac4)));
    return _mm_cvtt_ss2si(x);
}

int32_t cTSWantSimulator::ScaledAttributeB2(void *person, uint8_t scale)
{
    return ScaleToRange(Attribute(person, 0xb2, 1), scale);
}

int32_t cTSWantSimulator::ScaledMotiveB2(void *person, uint8_t scale)
{
    return ScaleToRange(Motive(person, 0xb2), scale);
}

int32_t cTSWantSimulator::ClampedAttributeB0(void *person)
{
    int32_t value = Attribute(person, 0xb0, 1);
    if (value < 0) value = 0;
    return value > 4 ? 4 : value;
}

int32_t cTSWantSimulator::PositiveAttributeA1(void *person)
{
    const int32_t value = Attribute(person, 0xa1, 1) * 10;
    return value > 0 ? value : 0;
}

uint8_t cTSWantSimulator::CountsForPerson(void *person, int32_t *outA, int32_t *outB)
{
    if (!person) return 0;
    const uint32_t id = PersonId(person);
    return Counts(id, outA, outB);
}

uint8_t cTSWantSimulator::Counts(uint32_t type, int32_t *outA, int32_t *outB)
{
    const int16_t key = int16_t(type);
    if (mMemoryStores.find(key) == mMemoryStores.end()) RestoreSimWants(type);
    if (outA) *outA = int32_t(mWants[key].Size());
    if (outB) *outB = int32_t(mFears[key].Size());
    return 1;
}

uint8_t cTSWantSimulator::ValueForPerson(void *person, int32_t *out)
{
    if (!person) return 0;
    const uint32_t id = PersonId(person);
    return Value48(int16_t(id), out);
}

// The original calls the shared map<short,short>::operator[] (0x485ec0) out of line, passing the address of its key argument slot.
#pragma inline_depth(0)
uint8_t cTSWantSimulator::Value48(int16_t key, int32_t *out)
{
    if (out) *out = mMap48[key] + 1;
    return 1;
}
#pragma inline_depth()

void *cTSWantSimulator::Value40(int16_t key)
{
    return mLifetimeWants[key].object;
}

WantRefList *cTSWantSimulator::List30(int16_t key)
{
    return &mWants[key];
}

WantRefList *cTSWantSimulator::List38(int16_t key)
{
    return &mFears[key];
}

WantResourceKey *cTSWantSimulator::KeyFirst(WantResourceKey *out, int16_t key)
{
    const auto it = mMap7c.find(key);
    const uint32_t instance = it == mMap7c.end() ? 0 : it->second.first;
    *out = {0x856ddbac, 0x499db772, instance, 0};
    return out;
}

WantResourceKey *cTSWantSimulator::KeySecond(WantResourceKey *out, int16_t key)
{
    const auto it = mMap7c.find(key);
    const uint32_t instance = it == mMap7c.end() ? 0 : it->second.second;
    *out = {0x856ddbac, 0x499db772, instance, 0};
    return out;
}

uint8_t cTSWantSimulator::Find84(int16_t key, int32_t *out)
{
    const auto it = mMap84.find(int32_t(key));
    if (it == mMap84.end()) return 0;
    *out = it->second;
    return 1;
}

namespace {
uint32_t SourceSlot13(void *source) { return VirtualCall<uint32_t(__thiscall *)(void *), 13>(source); }
uint32_t SourceSlot14(void *source) { return VirtualCall<uint32_t(__thiscall *)(void *), 14>(source); }
} // namespace

uint32_t cTSWantSimulator::ForwardToSlot34(uint32_t a, void *source)
{
    const uint32_t b = SourceSlot14(source);
    const uint32_t c = SourceSlot13(source);
    return ForEachWantOf(a, c, b, 0);
}

uint32_t cTSWantSimulator::ForwardPersonToSlot34(void *person, void *source)
{
    const uint32_t b = SourceSlot14(source);
    const uint32_t c = SourceSlot13(source);
    const uint32_t id = PersonId(person);
    return ForEachWantOf(id, c, b, 0);
}

uint32_t cTSWantSimulator::ForwardPersonToSlot50(void *person, uint32_t a, uint32_t b)
{
    // With no person the original returns eax as it found it (undefined).
    if (!person) return 0;
    const uint32_t id = PersonId(person);
    return Slot50(id, a, b);
}

uint32_t cTSWantSimulator::MidpointScoreForPerson(void *person, int32_t level)
{
    // With no person the original returns eax as it found it (undefined).
    if (!person) return 0;
    const int32_t key = int32_t(PersonKey(person));
    const std::vector<int32_t> &table = mLevelTables[key];
    const uint32_t count = uint32_t(table.size());
    if (count < 7) return count; // eax still holds the element count
    if (level < 0) level = 0;
    if (level > 5) level = 5;
    const int32_t midpoint = int32_t(uint32_t(table[level + 1]) + uint32_t(table[level])) / 2;
    return Slot40(person, midpoint, 0);
}

int32_t cTSWantSimulator::ScoreForValue(int32_t key, int32_t value)
{
    if (value < -0x50000) value = -0x50000;
    if (value > 0x4fff6) value = 0x4fff6;
    const std::vector<int32_t> &table = mLevelTables[key];
    if (table.size() < 7) return 0;
    const uint32_t level = LevelOf(value, table);
    // Position within the level's band, mapped onto the level's score band (game table 0xdcab20).
    const int32_t *scores = GameAddress<const int32_t>(0xdcab20);
    const int32_t span = int32_t(uint32_t(table[level + 1]) - uint32_t(table[level]));
    const int32_t offset = int32_t(uint32_t(value) - uint32_t(table[level]));
    const int32_t low = scores[level];
    const int32_t range = int32_t(uint32_t(scores[level + 1]) - uint32_t(low));
    __m128 x = _mm_div_ss(_mm_cvtsi32_ss(_mm_setzero_ps(), offset), _mm_cvtsi32_ss(_mm_setzero_ps(), span));
    x = _mm_max_ss(_mm_setzero_ps(), x);
    x = _mm_min_ss(_mm_set_ss(*GameAddress<const float>(0xd9c698)), x);
    x = _mm_mul_ss(x, _mm_cvtsi32_ss(_mm_setzero_ps(), range));
    x = _mm_add_ss(x, _mm_cvtsi32_ss(_mm_setzero_ps(), low));
    // Adding 1.5 * 2^23 (game constant 0xd9ff00) rounds to an integer held in the low mantissa bits.
    x = _mm_add_ss(x, _mm_set_ss(*GameAddress<const float>(0xd9ff00)));
    const int32_t rounded = int32_t(uint32_t(_mm_cvtsi128_si32(_mm_castps_si128(x))) + 0xb4c00000u);
    if (rounded < -100) return -100;
    return rounded > 100 ? 100 : rounded;
}

// The want tree manager at +0x24 (cTSWantTreeManager, built by Init): its slot 5 looks a definition up by guid, its
// slot 7 returns its definition table (+0x3c). Both slots are tail jumps in the original.
uint32_t cTSWantSimulator::GetWantDefinition(uint32_t guid)
{
    return VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 5>(mDefinitions.object, guid);
}

uint32_t cTSWantSimulator::DefinitionsSlot7()
{
    return VirtualCall<uint32_t(__thiscall *)(void *), 7>(mDefinitions.object);
}

void cTSWantSimulator::TogglePaused()
{
    mPaused = mPaused == 0;
}

uint8_t cTSWantSimulator::IsPaused()
{
    return mPaused;
}

// ======================================================================================================
// Persistence (lane DB): the per-sim record {0xcd95548e, hood group, sim id, 0} holds the sim's wants block
// (WriteSimWants / ReadSimWants) and its memories block (WantMemoryStore::Write / Read). Format: notes/feedback/CY.md.
// ======================================================================================================

namespace {

constexpr uint32_t kMaxMemoryListSize = 0x15555555; // std::list<> max_size for 0xc-byte nodes
constexpr uint32_t kMaxVectorSize = 0x3fffffff;
constexpr uint32_t kDefaultWantSlots = 4;
constexpr uint32_t kDefaultFearSlots = 3;
constexpr uint32_t kRecordModeRead = 1;
constexpr uint32_t kRecordModeWrite = 2;
constexpr uint32_t kWantInstanceSize = 0x3c;

// A reference-counted pointer released on scope exit (Release is slot 2 of every counted interface).
template <typename T> class Counted {
public:
    Counted() = default;
    Counted(const Counted &) = delete;
    Counted &operator=(const Counted &) = delete;
    ~Counted() noexcept(false)
    {
        if (mObject) As<::cIGZUnknown>(mObject)->Release();
    }
    T *Get() const { return mObject; }
    T *operator->() const { return mObject; }
    explicit operator bool() const { return mObject != nullptr; }
    void **Out() { return reinterpret_cast<void **>(&mObject); }

private:
    T *mObject = nullptr;
};

// The want instance's cIGZSerializable face (QueryInterface 0xe4fda3d4 on the primary vtable, counted). The result of
// the QI is not looked at, as in the original (a refusing instance would be called through null).
class SerialFace {
public:
    explicit SerialFace(ifaces::cITSWantInstance *want) { want->QueryInterface(kSerializableIid, &mFace); }
    SerialFace(const SerialFace &) = delete;
    SerialFace &operator=(const SerialFace &) = delete;
    ~SerialFace() noexcept(false)
    {
        if (mFace) mFace->Release();
    }
    ifaces::cIGZSerializable *operator->() const { return mFace; }

private:
    ifaces::cIGZSerializable *mFace = nullptr;
};

// A new instance from the pooled allocator (0x54e570) and its default constructor (0x54e490): reference count 0, so
// the QI temporary's Release frees one nobody kept.
ifaces::cITSWantInstance *NewWantInstance()
{
    void *block = OriginalFunction<void *(__cdecl *)(uint32_t), 0x54e570>()(kWantInstanceSize);
    if (!block) return nullptr;
    return OriginalFunction<ifaces::cITSWantInstance *(__thiscall *)(void *), 0x54e490>()(block);
}

// A list node or list head from the allocator service (0x3b1d10, __thiscall, ret 8: count, hint).
MemoryNode *NewMemoryNode(void *self)
{
    return static_cast<MemoryNode *>(OriginalFunction<void *(__thiscall *)(void *, uint32_t, uint32_t), 0x3b1d10>()(self, 1, 0));
}

void ClearRefs(WantRefList &list) { reinterpret_cast<RefPtrVector *>(&list)->Clear(); }

// vector::reserve (0x41f7f0, takes the count by address) with the max_size test inline (0x1443b0 is the "vector too long" throw).
void ReserveRefs(WantRefList &list, uint32_t slots)
{
    if (slots > uint32_t(list.mEnd - list.mFirst)) {
        if (slots > kMaxVectorSize) std::_Xlength_error("vector too long");
        OriginalFunction<void(__thiscall *)(WantRefList *, const uint32_t *), 0x41f7f0>()(&list, &slots);
    }
}

// Appends a counted pointer: in place with its own count when there is room, else the reallocating insert (0x1d57e0).
void PushCounted(WantRefList &list, ifaces::cITSWantInstance *want)
{
    reinterpret_cast<ServiceRefVector *>(&list)->PushBack(want);
}

// vector::resize(slots, null) (0x4992c0, ret 8) when the list is shorter.
void PadRefs(WantRefList &list, uint32_t slots)
{
    if (list.Size() < slots) {
        void *null = nullptr;
        OriginalFunction<void(__thiscall *)(WantRefList *, uint32_t, void *const *), 0x4992c0>()(&list, slots, &null);
    }
}

// The persist group of the current neighbourhood: GetCachedSlot23 -> current hood -> key holder -> key, looked up in
// the neighbourhood table of the globals object (StoreSimWants, RestoreSimWants and DeleteSim repeat this inline).
uint32_t CurrentHoodGroup()
{
    auto *context = As<ifaces::GscContext>(GetCachedSingleton());
    auto *holder = As<ifaces::TokenService23>(GetCachedSlot23())->GetCurrentNeighborhood()->GetKeyHolder();
    auto *table = context->GetNeighborhoodTable();
    return table->FindNeighborhoodByKey(holder->GetKey())->GetGroupSource()->GetGroup();
}

WantResourceKey MemoriesKey(int16_t simId)
{
    const uint32_t group = CurrentHoodGroup();
    return {kMemoriesRecordType, group, uint32_t(int32_t(simId)), 0};
}

ifaces::cITSPersistSystem *PersistSystem()
{
    return As<ifaces::cITSPersistSystem>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>());
}

uint32_t CurrentTick()
{
    return As<ifaces::cITSGlobals>(GetCachedSingleton())->GetWantFearService()->GetTick();
}

// Removes the memories whose flag-0x10 test is set (std::list::remove_if): all unlinked first, in order, onto a chain,
// then each released and its node freed through the allocator service.
void DropFlaggedMemories(MemoryList &list)
{
    MemoryNode *removed = nullptr;
    MemoryNode **tail = &removed;
    MemoryNode *const head = list.head;
    for (MemoryNode *node = head->next; node != head;) {
        MemoryNode *next = node->next;
        if (node->memory->IsFlag10Set()) {
            --list.size;
            node->next = nullptr;
            node->prev->next = next;
            next->prev = node->prev;
            *tail = node;
            tail = &node->next;
        }
        node = next;
    }
    for (MemoryNode *node = removed; node;) {
        MemoryNode *next = node->next;
        if (node->memory) node->memory->Release();
        FreeThroughAllocatorService(node, 1);
        node = next;
    }
}

} // namespace

// ---- MemoryList ----

MemoryList::MemoryList() : head(nullptr), size(0)
{
    head = NewMemoryNode(this);
    head->next = head;
    head->prev = head;
}

MemoryList::~MemoryList()
{
    head->prev->next = nullptr;
    for (MemoryNode *node = head->next; node;) {
        MemoryNode *next = node->next;
        if (node->memory) node->memory->Release();
        FreeThroughAllocatorService(node, 1);
        node = next;
    }
    FreeThroughAllocatorService(head, 1);
}

void MemoryList::Append(ifaces::cITSWantInstance *memory)
{
    // ORIGINAL-BUG (unreachable): the length check throws after the caller's temporary count, which then leaks.
    if (size == kMaxMemoryListSize) std::_Xlength_error("list too long");
    MemoryNode *node = NewMemoryNode(this);
    node->memory = memory;
    if (memory) memory->AddRef();
    ++size;
    node->next = head;
    node->prev = head->prev;
    head->prev = node;
    node->prev->next = node;
}

// ---- WantMemoryStore ----

WantMemoryStore *WantMemoryStore::Construct()
{
    lastTick = 0x3fffffff;
    nextId = 0;
    new (&byGuid) std::map<uint32_t, MemoryList>();
    return this;
}

void WantMemoryStore::Destruct()
{
    byGuid.~map();
}

void WantMemoryStore::Clear()
{
    byGuid.clear();
}

void WantMemoryStore::SyncTick()
{
    lastTick = CurrentTick();
}

// Numbers an unnumbered memory by the ticks since the last one (at least 1), or lifts nextId to a numbered one's id,
// then files it under its definition's guid (one count held by the list).
void WantMemoryStore::AddMemory(ifaces::cITSWantInstance *want)
{
    if (want->HasId()) {
        const int32_t id = int32_t(want->GetId());
        if (id > nextId) nextId = id;
    } else {
        const uint32_t now = CurrentTick();
        const int32_t elapsed = int32_t(now - lastTick);
        lastTick = now;
        nextId += elapsed > 1 ? elapsed : 1;
        want->SetId(uint32_t(nextId));
    }
    MemoryList &list = byGuid[want->GetGuid()];
    want->AddRef();
    list.Append(want);
    want->Release();
}

// Version 2 (older: 0, 1). Instances are read into pooled objects and kept only when they have a definition.
bool WantMemoryStore::Read(ifaces::cIGZIStream *stream)
{
    uint32_t version = 0; // uninitialised stack in the original when the read fails
    stream->GetUint32(&version);
    if (version > kMemoryStoreVersion) return false;
    if (version == kMemoryStoreVersion) stream->GetSint32(&nextId); // older records number memories by their ids below
    byGuid.clear();
    uint32_t keyCount = 0; // uninitialised stack in the original when the read fails
    stream->GetUint32(&keyCount);
    for (uint32_t key = 0; key < keyCount; ++key) {
        uint32_t guid = 0; // uninitialised stack in the original when the read fails
        stream->GetUint32(&guid);
        MemoryList &list = byGuid[guid];
        uint32_t count = 0; // uninitialised stack in the original when the read fails
        stream->GetUint32(&count);
        for (uint32_t i = 0; i < count; ++i) {
            ifaces::cITSWantInstance *want = NewWantInstance();
            SerialFace face(want);
            face->Read(stream);
            if (want->GetDefinition()) {
                want->AddRef();
                list.Append(want);
                want->Release();
                if (version < 2 && int32_t(want->GetId()) > nextId) nextId = int32_t(want->GetId());
            }
        }
    }
    return !stream->GetError();
}

// Version 2: drops the memories flagged for removal (slot 28) first, then writes the id counter and every list.
bool WantMemoryStore::Write(ifaces::cIGZOStream *stream)
{
    for (auto &entry : byGuid) DropFlaggedMemories(entry.second);
    stream->SetUint32(kMemoryStoreVersion);
    stream->SetSint32(nextId);
    stream->SetUint32(uint32_t(byGuid.size()));
    for (auto &entry : byGuid) {
        MemoryList &list = entry.second;
        stream->SetUint32(entry.first);
        stream->SetUint32(list.size);
        for (MemoryNode *node = list.head->next; node != list.head; node = node->next) {
            SerialFace face(node->memory);
            face->Write(stream);
        }
    }
    return !stream->GetError();
}

// ---- cTSWantSimulator persistence ----

// 0x48d650: the sim's store; a missing one is made (with the current tick) and gets its default memory.
WantMemoryStore *cTSWantSimulator::GetMemoryStore(int16_t simId)
{
    const auto found = mMemoryStores.find(simId);
    if (found != mMemoryStores.end()) return found->second;
    auto *store = static_cast<WantMemoryStore *>(::operator new(sizeof(WantMemoryStore)));
    store->Construct();
    mMemoryStores[simId] = store;
    store->SyncTick();
    AddDefaultMemory(simId);
    return store;
}

// 0x486310: a store without a memory of the default guid gets one (a want of that guid for the sim, empty parameter).
void cTSWantSimulator::AddDefaultMemory(int16_t simId)
{
    WantMemoryStore *store = GetMemoryStore(simId);
    MemoryList existing;
    OriginalFunction<void(__thiscall *)(WantMemoryStore *, uint32_t, MemoryList *), 0x550ff0>()(store, kDefaultMemoryGuid, &existing);
    if (existing.size == 0) {
        ifaces::cITSWantInstance *want = nullptr;
        if (void *block = OriginalFunction<void *(__cdecl *)(uint32_t), 0x54e570>()(kWantInstanceSize)) {
            WantInstanceParam param;
            OriginalFunction<WantInstanceParam *(__thiscall *)(WantInstanceParam *), 0x54e500>()(&param);
            want = OriginalFunction<ifaces::cITSWantInstance *(__thiscall *)(void *, int32_t, uint32_t, WantInstanceParam *, void *, void *),
                                    0x54e2b0>()(block, simId, kDefaultMemoryGuid, &param, nullptr, nullptr);
        }
        store->AddMemory(want);
    }
}

// 0x48e2f0: a want is kept when its definition's flags meet the option mask and, for a kind-3 parameter, the level
// recorded for its sim id satisfies the definition's rule (3: extra below the level, 5: at most the level).
bool cTSWantSimulator::IsWantAllowed(ifaces::cITSWantInstance *want)
{
    if (!want || !want->GetDefinition()) return false;
    if ((want->GetDefinition()->GetOptionFlags() & uint32_t(mOptionMask)) == 0) return false;
    const auto *param = static_cast<const WantInstanceParam *>(want->GetParam());
    if (param->kind != 3) return true;
    int32_t level = 0;
    if (!Find84(int16_t(param->value), &level)) return false;
    const int32_t rule = want->GetDefinition()->GetLevelRule();
    if (rule == 3) return param->extra < level;
    return rule != 5 || param->extra <= level;
}

// 0x4983a0: version 6, the lifetime want, the want slots, the fear slots, the +0x48 short (sign-extended).
bool cTSWantSimulator::WriteSimWants(int16_t simId, ifaces::cIGZOStream *stream)
{
    stream->SetUint32(kWantsVersion);
    if (void *lifetime = mLifetimeWants[simId].object) {
        stream->SetUint32(1);
        SerialFace face(static_cast<ifaces::cITSWantInstance *>(lifetime));
        face->Write(stream);
    } else {
        stream->SetUint32(0);
    }
    auto writeSlots = [&](WantRefList &list) {
        uint32_t used = 0;
        for (void **it = list.mFirst; it != list.mLast; ++it) used += *it != nullptr;
        stream->SetUint32(list.Size());
        stream->SetUint32(used);
        for (void **it = list.mFirst; it != list.mLast; ++it) {
            if (!*it) continue;
            SerialFace face(static_cast<ifaces::cITSWantInstance *>(*it));
            face->Write(stream);
        }
    };
    writeSlots(mWants[simId]);
    writeSlots(mFears[simId]);
    stream->SetSint32(mMap48[simId]);
    return !stream->GetError();
}

// 0x492970: versions 0..6 (newer ones fail before anything is touched). Wants and fears are padded with nulls to
// their slot counts; entries past the slot count or not allowed are dropped.
bool cTSWantSimulator::ReadSimWants(int16_t simId, ifaces::cIGZIStream *stream)
{
    uint32_t version = 0; // uninitialised stack in the original when the read fails
    stream->GetUint32(&version);
    if (version > kWantsVersion) return false;

    if (void *old = mLifetimeWants[simId].object) {
        mLifetimeWants[simId].object = nullptr;
        As<::cIGZUnknown>(old)->Release();
    }
    if (version >= 3) {
        uint32_t hasLifetime = 0;
        stream->GetUint32(&hasLifetime);
        if (hasLifetime) {
            ifaces::cITSWantInstance *want = NewWantInstance();
            SerialFace face(want);
            face->Read(stream);
            if (version >= 6 && IsWantAllowed(want)) {
                WantRef &slot = mLifetimeWants[simId];
                void *old = slot.object;
                if (want != old) {
                    want->AddRef();
                    slot.object = want;
                    if (old) As<::cIGZUnknown>(old)->Release();
                }
            }
        }
    }

    auto readSlots = [&](WantRefList &list, uint32_t defaultSlots) {
        ClearRefs(list);
        uint32_t slots = defaultSlots;
        if (version >= 2) stream->GetUint32(&slots);
        uint32_t count = 0; // uninitialised stack in the original when the read fails
        stream->GetUint32(&count);
        ReserveRefs(list, slots);
        for (uint32_t i = 0; i < count; ++i) {
            ifaces::cITSWantInstance *want = NewWantInstance();
            SerialFace face(want);
            face->Read(stream);
            if (i < slots && IsWantAllowed(want)) {
                want->AddRef();
                PushCounted(list, want);
                want->Release();
            }
        }
        PadRefs(list, slots);
    };
    readSlots(mWants[simId], kDefaultWantSlots);
    readSlots(mFears[simId], kDefaultFearSlots);

    if (version >= 4) {
        int32_t value = 0;
        stream->GetSint32(&value);
        mMap48[simId] = int16_t(value);
    } else {
        mMap48[simId] = 0;
    }
    return !stream->GetError();
}

// 0x492ea0: finds the segment holding the record, opens it for reading and reads both blocks. A store that did not
// exist yet is made (with its default memory) by GetMemoryStore and then cleared by the read.
bool cTSWantSimulator::ReadSimRecord(int16_t simId, WantResourceKey *key)
{
    bool result = false;
    Counted<ifaces::cIGZPersistDBSegment> segment;
    const auto *resourceKey = reinterpret_cast<const ::cGZPersistResourceKey *>(key);
    if (PersistSystem()->FindSegment(resourceKey, kSegmentIid, segment.Out())) {
        Counted<ifaces::cIGZPersistDBRecord> record;
        if (segment->OpenRecord(resourceKey, record.Out(), kRecordModeRead)) {
            Counted<ifaces::cIGZIStream> stream;
            if (As<::cIGZUnknown>(record.Get())->QueryInterface(kIStreamIid, stream.Out())) {
                const bool wants = ReadSimWants(simId, stream.Get());
                WantMemoryStore *store = GetMemoryStore(simId);
                mMemoryStores[simId] = store;
                result = store->Read(stream.Get()) && wants;
            }
            segment->CloseRecord(record.Get());
        }
    }
    return result;
}

// 0x4985e0 (slot 6): waits for the sim's pending update, then writes both blocks into its record. Sim 0 has no record.
uint8_t cTSWantSimulator::StoreSimWants(int16_t simId)
{
    if (simId == 0) return 0;
    FinishPendingUpdate(simId);
    WantResourceKey key = MemoriesKey(simId);
    bool result = false;
    Counted<ifaces::cIGZPersistDBSegment> segment;
    const auto *resourceKey = reinterpret_cast<const ::cGZPersistResourceKey *>(&key);
    if (PersistSystem()->FindSegment(resourceKey, kSegmentIid, segment.Out())) {
        Counted<ifaces::cIGZPersistDBRecord> record;
        if (segment->OpenRecord(resourceKey, record.Out(), kRecordModeWrite)) {
            Counted<ifaces::cIGZOStream> stream;
            if (As<::cIGZUnknown>(record.Get())->QueryInterface(kOStreamIid, stream.Out())) {
                const bool wants = WriteSimWants(simId, stream.Get());
                WantMemoryStore *store = GetMemoryStore(simId);
                result = store->Write(stream.Get()) && wants;
            }
            segment->CloseRecord(record.Get());
        }
    }
    return result;
}

// ---- sim lifecycle (lane DR) ----

// RunUpdate (0x4880b0, the per-sim update state machine; not replaced yet).
void cTSWantSimulator::RunUpdate(int32_t steps, int16_t simId)
{
    OriginalFunction<void(__thiscall *)(cTSWantSimulator *, int32_t, int32_t), 0x4880b0>()(this, steps, simId);
}

// Inlined by StoreSimWants, DeleteSim and ResetMemories: runs updates until the sim is neither queued nor in progress.
void cTSWantSimulator::FinishPendingUpdate(int16_t simId)
{
    while (mPendingSims.find(simId) != mPendingSims.end() || int16_t(mCurrentSim) == simId) RunUpdate(100, simId);
}

// Inlined by DeleteSim and ResetMemories: takes the sim's store out of the table and destroys it.
void cTSWantSimulator::DestroyMemoryStore(std::map<int16_t, WantMemoryStore *>::iterator entry)
{
    WantMemoryStore *const store = entry->second;
    mMemoryStores.erase(entry);
    if (mCurrentStore == store) mCurrentStore = nullptr;
    // ORIGINAL-BUG (unreachable: the table never holds null): Clear runs before the null test, so a null entry crashes.
    store->Clear();
    if (store) {
        store->Destruct();
        ::operator delete(store, sizeof(WantMemoryStore));
    }
}

namespace {
// A counted member assigned as the original's smart pointer does: the new one counted, then the old one released.
template <typename T> void AssignCounted(T *&slot, T *value)
{
    T *const old = slot;
    if (value == old) return;
    if (value) As<::cIGZUnknown>(value)->AddRef();
    slot = value;
    if (old) As<::cIGZUnknown>(old)->Release();
}

constexpr uint32_t kAttributeNotLoadable = 0xba; // neighbour attribute: set when the sim's wants must not be loaded
constexpr int32_t kRequeuePriority = 3;          // queue priority after a load that left slots empty
} // namespace

// 0x4854b0: the sim's neighbour, its live person (if instantiated) and the attribute block of whichever exists.
WantSimContext *WantSimContext::Construct(int16_t sim)
{
    simId = 0;
    person = nullptr;
    neighbor = nullptr;
    attributes = 0;
    if (sim == 0) return this;
    simId = sim;
    auto *globals = As<ifaces::cITSGlobals>(GetCachedSingleton());
    void *live = nullptr;
    if (ifaces::cITSNeighbor *found = globals->GetNeighborManager()->GetNeighbor(sim, 1)) {
        const uint32_t handle = found->GetPersonDataId();
        live = globals->GetObjectTable()->FindObjectByHandle(handle);
    }
    AssignCounted(person, live);
    AssignCounted(neighbor, As<ifaces::cITSGlobals>(GetCachedSingleton())->GetNeighborManager()->GetNeighbor(simId, 1));
    attributes = 0;
    if (person) attributes = VirtualCall<uint32_t(__thiscall *)(void *), 12>(person); // slot not named yet
    else if (neighbor) attributes = reinterpret_cast<uint32_t>(neighbor->GetAttributeBlock(1));
    return this;
}

void WantSimContext::Destruct()
{
    if (neighbor) As<::cIGZUnknown>(neighbor)->Release();
    if (person) As<::cIGZUnknown>(person)->Release();
}

// 0x493510 (slot 7): loads the sim's record (when it has a neighbour that allows it), pads wants to 4 and fears to 3
// slots, requeues the sim when anything was missing, and makes sure its store has the default memory. Sim 0 only has
// its lists emptied.
uint8_t cTSWantSimulator::RestoreSimWants(uint32_t simIdWord)
{
    const int16_t simId = int16_t(simIdWord); // Counts passes its whole type dword through
    if (simId == 0) {
        ClearRefs(mWants[simId]);
        ClearRefs(mFears[simId]);
        return 0;
    }
    WantSimContext context;
    context.Construct(simId);
    uint8_t result = 0;
    if (context.neighbor && context.neighbor->GetAttribute(kAttributeNotLoadable) == 0) {
        if (int16_t(mCurrentSim) == simId) { // abandon the update in progress and queue it again
            mUpdateState = 0;
            mCurrentSim = 0;
            ClearRefs(mVecEC);
            QueueSimUpdate(simId, int32_t(mPendingPriority));
        }
        WantResourceKey key = MemoriesKey(simId);
        result = ReadSimRecord(simId, &key);
        bool requeue = mLifetimeWants[simId].object == nullptr;
        WantRefList &wants = mWants[simId];
        if (wants.Size() < kDefaultWantSlots) {
            requeue = true;
            PadRefs(wants, kDefaultWantSlots);
        }
        WantRefList &fears = mFears[simId];
        if (fears.Size() < kDefaultFearSlots) {
            PadRefs(fears, kDefaultFearSlots);
            requeue = true;
        }
        if (requeue) QueueSimUpdate(simId, kRequeuePriority);
        AddDefaultMemory(simId);
    }
    context.Destruct();
    return result;
}

// 0x4952b0 (slot 8): saves every sim that has a store; an instantiated one goes through the neighbourhood's
// CopyPersonToNeighbor (which ends in StoreSimWants) when `copyLivePersons` is set. Always 1.
uint8_t cTSWantSimulator::SaveAllSims(uint8_t copyLivePersons)
{
    ifaces::cITSNeighborhood *hood = As<ifaces::cITSGlobals>(GetCachedSingleton())->GetNeighborhood();
    for (auto entry = mMemoryStores.begin(); entry != mMemoryStores.end(); ++entry) {
        const int16_t simId = entry->first;
        auto *globals = As<ifaces::cITSGlobals>(GetCachedSingleton());
        void *person = nullptr;
        if (ifaces::cITSNeighbor *neighbor = globals->GetNeighborManager()->GetNeighbor(simId, 1)) {
            const uint32_t handle = neighbor->GetPersonDataId();
            person = globals->GetObjectTable()->FindObjectByHandle(handle);
        }
        if (person && copyLivePersons) hood->CopyPersonToNeighbor(static_cast<ifaces::cITSPerson *>(person));
        else StoreSimWants(simId);
    }
    return 1;
}

// 0x487be0 (messages 0x9d / 0x9e): forgets the sim: waits for its update, destroys its store and deletes its record.
// A sim without a store is left alone (its record too).
void cTSWantSimulator::DeleteSim(int16_t simId)
{
    if (simId == 0) return;
    FinishPendingUpdate(simId);
    const auto entry = mMemoryStores.find(simId);
    if (entry == mMemoryStores.end()) return;
    DestroyMemoryStore(entry);
    WantResourceKey key = MemoriesKey(simId);
    Counted<ifaces::cIGZPersistDBSegment> segment;
    const auto *resourceKey = reinterpret_cast<const ::cGZPersistResourceKey *>(&key);
    if (PersistSystem()->FindSegment(resourceKey, kSegmentIid, segment.Out())) segment->DeleteRecord(resourceKey);
}

// 0x487a70 (slot 45): replaces the sim's memories with a fresh store holding only the default memory. A sim not loaded
// yet is restored first (so the record's memories are dropped too); the record itself is not touched.
void cTSWantSimulator::ResetMemories(int16_t simId)
{
    if (simId == 0) return;
    FinishPendingUpdate(simId);
    auto entry = mMemoryStores.find(simId);
    if (entry == mMemoryStores.end()) {
        RestoreSimWants(simId);
        entry = mMemoryStores.find(simId);
    }
    if (entry != mMemoryStores.end()) DestroyMemoryStore(entry);
    GetMemoryStore(simId);
}

// 0x48d1e0 (slot 55): runs updates until nothing is queued or in progress.
void cTSWantSimulator::FlushUpdates()
{
    while (mPendingSims.size() != 0 || mUpdateState != 0) RunUpdate(100, 0);
}

// 0x494870 (slot 35): queues the sim for an update. A queued sim only takes a lower (more urgent) priority; a newly
// queued one with priority 0 or 1 also gets a copy of its wants and fears (mMap50 / mMap58).
void cTSWantSimulator::QueueSimUpdate(int16_t simId, int32_t priority)
{
    if (simId == 0) return;
    const auto queued = mPendingSims.find(simId);
    if (queued != mPendingSims.end()) {
        if (priority < queued->second) queued->second = priority;
        return;
    }
    mPendingSims[simId] = priority;
    if (priority != 0 && priority != 1) return;
    auto copy = [](WantRefList &to, WantRefList &from) { // vector assignment of counted pointers (0x483930)
        if (&to == &from) return;
        OriginalFunction<void(__thiscall *)(WantRefList *, void **, uint32_t), 0x483930>()(&to, from.mFirst, from.Size());
    };
    WantRefList &wants = mWants[simId];
    copy(mMap50[simId], wants);
    WantRefList &fears = mFears[simId];
    copy(mMap58[simId], fears);
}

// ======================================================================================================
// Game slots (lane GA): slots whose bodies are not reimplemented yet run the original body on our object (handoff D2:
// the layout is the same and the original dispatches through our vtables). Every argument and the result pass through
// as whole dwords. Listed in the constructor entry's original_calls; tests: WantSimulator_GameSlots.
// ======================================================================================================

namespace {
using Self = cTSWantSimulator;
} // namespace

// Init (slot 3), Shutdown (slot 4) and AddCategoryWant (slot 5): ours since lane IV (src/ts/cTSWantSimulatorIV.cpp).
uint32_t cTSWantSimulator::GetCategoryWants(uint32_t category)
{
    return OriginalFunction<uint32_t(__thiscall *)(Self *, uint32_t), 0x48d840>()(this, category);
}
uint32_t cTSWantSimulator::GetWantText(uint32_t key, uint32_t index, uint32_t out)
{
    return OriginalFunction<uint32_t(__thiscall *)(Self *, uint32_t, uint32_t, uint32_t), 0x48dad0>()(this, key, index, out);
}
uint32_t cTSWantSimulator::AdjustPersonA1(uint32_t person, uint32_t amount)
{
    return OriginalFunction<uint32_t(__thiscall *)(Self *, uint32_t, uint32_t), 0x496930>()(this, person, amount);
}
uint32_t cTSWantSimulator::ForEachWantOf(uint32_t a, uint32_t guid, uint32_t c, uint32_t d)
{
    return OriginalFunction<uint32_t(__thiscall *)(Self *, uint32_t, uint32_t, uint32_t, uint32_t), 0x491960>()(this, a, guid, c, d);
}
uint32_t cTSWantSimulator::Slot36(uint32_t simId) { return OriginalFunction<uint32_t(__thiscall *)(Self *, uint32_t), 0x494540>()(this, simId); }
uint32_t cTSWantSimulator::Slot37(uint32_t simId, uint32_t a, uint32_t b)
{
    return OriginalFunction<uint32_t(__thiscall *)(Self *, uint32_t, uint32_t, uint32_t), 0x4944c0>()(this, simId, a, b);
}
uint32_t cTSWantSimulator::Slot39(uint32_t person, uint32_t value, uint32_t flag)
{
    return OriginalFunction<uint32_t(__thiscall *)(Self *, uint32_t, uint32_t, uint32_t), 0x495d00>()(this, person, value, flag);
}
uint32_t cTSWantSimulator::Slot40(void *person, int32_t value, uint32_t flag)
{
    return OriginalFunction<uint32_t(__thiscall *)(Self *, void *, int32_t, uint32_t), 0x495e30>()(this, person, value, flag);
}
uint32_t cTSWantSimulator::Slot43(uint32_t a, uint32_t value)
{
    return OriginalFunction<uint32_t(__thiscall *)(Self *, uint32_t, uint32_t), 0x492840>()(this, a, value);
}
uint32_t cTSWantSimulator::Slot44(uint32_t person) { return OriginalFunction<uint32_t(__thiscall *)(Self *, uint32_t), 0x48ec20>()(this, person); }
uint32_t cTSWantSimulator::Slot46(uint32_t simId) { return OriginalFunction<uint32_t(__thiscall *)(Self *, uint32_t), 0x497480>()(this, simId); }
uint32_t cTSWantSimulator::Slot50(uint32_t simId, uint32_t a, uint32_t b)
{
    return OriginalFunction<uint32_t(__thiscall *)(Self *, uint32_t, uint32_t, uint32_t), 0x496010>()(this, simId, a, b);
}
uint32_t cTSWantSimulator::Slot51(uint32_t simId, uint32_t a)
{
    return OriginalFunction<uint32_t(__thiscall *)(Self *, uint32_t, uint32_t), 0x4962c0>()(this, simId, a);
}
uint32_t cTSWantSimulator::Slot54(uint32_t a) { return OriginalFunction<uint32_t(__thiscall *)(Self *, uint32_t), 0x497590>()(this, a); }
uint32_t cTSWantSimulator::Slot58() { return OriginalFunction<uint32_t(__thiscall *)(Self *), 0x4908e0>()(this); }
uint32_t cTSWantSimulator::Slot59() { return OriginalFunction<uint32_t(__thiscall *)(Self *), 0x494a70>()(this); }
uint32_t cTSWantSimulator::Slot60(uint32_t a) { return OriginalFunction<uint32_t(__thiscall *)(Self *, uint32_t), 0x490300>()(this, a); }
uint32_t cTSWantSimulator::Slot61(uint32_t a) { return OriginalFunction<uint32_t(__thiscall *)(Self *, uint32_t), 0x48ca60>()(this, a); }

// The +4 and +8 faces' game slots take `this` as their face, as the original bodies were compiled.
// DoMessage (0x487dd0) and Import (0x492fd0): lane GG, src/ts/cTSWantSimulatorGG.cpp.

// ======================================================================================================
// Adapters: the old RVA hooks (qualified calls, forced inline: no virtual dispatch).
// ======================================================================================================

using Adapters = cTSWantSimulatorAdapters;

Adapters::Self *__fastcall Adapters::Construct(void *memory, void *)
{
    return ::new (memory) cTSWantSimulator;
}
void __fastcall Adapters::Destruct(cTSWantSimulator *self, void *)
{
    self->cTSWantSimulator::~cTSWantSimulator();
}
uint32_t __fastcall Adapters::Release(cTSWantSimulator *self, void *)
{
    [[msvc::forceinline_calls]] return self->cTSWantSimulator::Release();
}
int32_t __fastcall Adapters::PositiveAttributeA1(cTSWantSimulator *self, void *, void *person)
{
    [[msvc::forceinline_calls]] return self->cTSWantSimulator::PositiveAttributeA1(person);
}
uint32_t __fastcall Adapters::GetWantDefinition(cTSWantSimulator *self, void *, uint32_t guid)
{
    [[msvc::forceinline_calls]] return self->cTSWantSimulator::GetWantDefinition(guid);
}
uint32_t __fastcall Adapters::DefinitionsSlot7(cTSWantSimulator *self, void *)
{
    [[msvc::forceinline_calls]] return self->cTSWantSimulator::DefinitionsSlot7();
}
void __fastcall Adapters::TogglePaused(cTSWantSimulator *self, void *)
{
    [[msvc::forceinline_calls]] return self->cTSWantSimulator::TogglePaused();
}
uint8_t __fastcall Adapters::IsPaused(cTSWantSimulator *self, void *)
{
    [[msvc::forceinline_calls]] return self->cTSWantSimulator::IsPaused();
}
WantMemoryStore *__fastcall Adapters::GetMemoryStore(cTSWantSimulator *self, void *, int16_t simId)
{
    [[msvc::forceinline_calls]] return self->cTSWantSimulator::GetMemoryStore(simId);
}
void __fastcall Adapters::AddDefaultMemory(cTSWantSimulator *self, void *, int16_t simId)
{
    [[msvc::forceinline_calls]] return self->cTSWantSimulator::AddDefaultMemory(simId);
}
bool __fastcall Adapters::IsWantAllowed(cTSWantSimulator *self, void *, ifaces::cITSWantInstance *want)
{
    [[msvc::forceinline_calls]] return self->cTSWantSimulator::IsWantAllowed(want);
}
bool __fastcall Adapters::WriteSimWants(cTSWantSimulator *self, void *, int16_t simId, ifaces::cIGZOStream *stream)
{
    [[msvc::forceinline_calls]] return self->cTSWantSimulator::WriteSimWants(simId, stream);
}
bool __fastcall Adapters::ReadSimWants(cTSWantSimulator *self, void *, int16_t simId, ifaces::cIGZIStream *stream)
{
    [[msvc::forceinline_calls]] return self->cTSWantSimulator::ReadSimWants(simId, stream);
}
bool __fastcall Adapters::ReadSimRecord(cTSWantSimulator *self, void *, int16_t simId, WantResourceKey *key)
{
    [[msvc::forceinline_calls]] return self->cTSWantSimulator::ReadSimRecord(simId, key);
}
void __fastcall Adapters::DeleteSim(cTSWantSimulator *self, void *, int16_t simId)
{
    [[msvc::forceinline_calls]] return self->cTSWantSimulator::DeleteSim(simId);
}
void __fastcall Adapters::FlushUpdates(cTSWantSimulator *self, void *)
{
    [[msvc::forceinline_calls]] return self->cTSWantSimulator::FlushUpdates();
}
