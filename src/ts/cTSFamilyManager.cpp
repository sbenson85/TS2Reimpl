#include "ts/cTSFamilyManager.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "misc/UtilitiesC.h"
#include "rz/MiscHelpersB.h"
#include "rz/RZMemory.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/StaticGetters2.h"
#include "rz/cRZString.h"
#include "simantics/PrimGenericSimCallMessage.h"
#include "simantics/ServiceRefVector.h"
#include "ts/cTSNeighborGeneticDataTable.h" // NeighborDataRef: a counted pointer with the game's copy semantics

#include "gz/cGZDBSegmentPackedFile.h"

#include <algorithm>
#include <vector>
#include <new>

namespace {
// Calls slot `slot` of `object` (__thiscall) with the given dword arguments.
template <typename R = uint32_t, typename... A> R Call(const void *object, int slot, A... args)
{
    using Fn = R(__thiscall *)(const void *, A...);
    return VirtualSlot<Fn>(object, slot)(object, args...);
}
void AddRefObject(void *object) { Call(object, 1); }
void ReleaseObject(void *object) { Call(object, 2); }

// Slots of the family objects (cTSFamily) the manager uses.
constexpr int kFamSetId = 6;            // (id)
constexpr int kFamAddMember = 7;        // (neighbour id)
constexpr int kFamRemoveMember = 8;     // (neighbour id)
constexpr int kFamMemberMoved = 9;      // (neighbour id)
constexpr int kFamMemberCount = 24;     // (flags): members of the kinds the flags name
constexpr int kFamMemberIdAt = 26;      // (index): address of the n-th member id, or null
constexpr int kFamIsMemberDirty = 28;   // (neighbour id)
constexpr int kFamHasMember = 29;       // (neighbour id), AL
constexpr int kFamRecord = 31;          // the persist record object (its slot 3 gives the key, slot 4 sets it)
constexpr int kFamLoad = 32;            // (id, group)
constexpr int kFamSaveRecord = 34;      // (version), AL
constexpr int kFamSaveMembers = 35;     // (0), AL
constexpr int kFamMarkSaved = 36;
constexpr int kFamSetName = 38;         // (cIGZString *)
constexpr int kFamId = 41;
constexpr int kFamHomeLot = 42;
constexpr int kFamSetHomeLot = 43;      // (lot)
constexpr int kFamSlot44 = 44;
constexpr int kFamMoveIn = 45;          // (lot)
constexpr int kFamSortKey = 51;
constexpr int kFamSetSortKey = 52;
constexpr int kFamFunds = 53;
constexpr int kFamSetValue54 = 54;
constexpr int kFamSetValue56 = 56;
constexpr int kFamSetValue58 = 58;
constexpr int kFamHomeLotWorth = 59;
constexpr int kFamSyncMembers = 61;
constexpr int kFamSetModified72 = 72;
constexpr int kFamDropResources = 75;
constexpr int kFamSetDirty = 23;        // (1)

// Messages the manager sends.
constexpr uint32_t kMsgFamilyChanged = 0x94; // value: the family id
constexpr uint32_t kMsgFamilyRemoved = 0xa4;
constexpr uint32_t kMsgFamilyCreated = 0xa5;

constexpr uint32_t kDefaultNameRva = 0xdcc470; // the name of family 0 (a game string)

void *PersistSystem() { return CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>(); }
void *LiveHousehold() { return OriginalFunction<void *(__cdecl *)(), 0x20df9a>()(); }

// The game's family ordering (0x4b6e60): highest slot 51 value first, then highest id first.
bool FamilyBefore(const NeighborDataRef &a, const NeighborDataRef &b)
{
    const int32_t keyA = Call<int32_t>(a.Get(), kFamSortKey);
    const int32_t keyB = Call<int32_t>(b.Get(), kFamSortKey);
    if (keyA != keyB) return keyA > keyB;
    const int32_t idA = Call<int32_t>(a.Get(), kFamId);
    return idA > Call<int32_t>(b.Get(), kFamId);
}

void SortFamilies(RefPtrVector &families)
{
    auto *first = reinterpret_cast<NeighborDataRef *>(families.mFirst);
    auto *last = reinterpret_cast<NeighborDataRef *>(families.mLast);
    std::sort(first, last, FamilyBefore);
}

// push_back of a counted pointer that is already held elsewhere (no temporary).
void PushBack(RefPtrVector &vector, void *const *held)
{
    if (vector.mLast == vector.mEnd) {
        vector.EmplaceReallocate(vector.mLast, held);
        return;
    }
    *vector.mLast = *held;
    if (*held) AddRefObject(*held);
    ++vector.mLast;
}

// push_back through a counted temporary, as the game writes it when the source is a plain pointer.
void PushBackCounted(RefPtrVector &vector, void *object)
{
    NeighborDataRef held(object);
    void *raw = held.Get();
    PushBack(vector, &raw);
}

// Sends a message with an id and one value (slot 11) to the message server: slot 3 (post) or slot 4 (send, flag 0).
// With a family, the value is its id (slot 41), read after the message id is set.
void SendFamilyMessage(uint32_t id, int32_t value, bool post, void *family = nullptr)
{
    void *server = gscmsg::MessageServer();
    gscmsg::MessageRef message;
    message.SetId(id);
    if (family) value = Call<int32_t>(family, kFamId);
    Call<void>(message.mMessage, 11, value);
    if (post) Call<void>(server, 3, message.mMessage);
    else Call<void>(server, 4, message.mMessage, uint32_t(0));
}
} // namespace

// ---- the log entry ----

FamilyLogEntry::FamilyLogEntry(const char *text) { reinterpret_cast<cRZString *>(mString)->Construct(text); }
FamilyLogEntry::~FamilyLogEntry() { Call<void *>(mString, 33, uint32_t(0)); }

// ---- construction ----

cTSFamilyManager *cTSFamilyManager::Create() { return new (std::nothrow) cTSFamilyManager; }

cTSFamilyManager::cTSFamilyManager() : mRefCount(0), mFamilies{}, mRemovedFamilies{}, mRecording(0)
{
    new (&mMessageIds) std::set<uint32_t>();
    new (&mLog) FamilyLog();
}

cTSFamilyManager::~cTSFamilyManager()
{
    mLog.~FamilyLog();
    mMessageIds.~set();
    mRemovedFamilies.Destroy();
    mFamilies.Destroy();
}

// ---- cIGZUnknown ----

bool cTSFamilyManager::QueryInterface(uint32_t id, void **out)
{
    switch (id) {
    case 1:
    case 0x0c1e4682:
        *out = static_cast<cITSFamilyManager *>(this);
        AddRef();
        return true;
    case 0x8db5bf48:
        *out = static_cast<cITSDataContainer *>(this);
        AddRef();
        return true;
    }
    return false;
}

uint32_t cTSFamilyManager::Release()
{
    if (mRefCount <= 1) {
        delete this;
        return 0;
    }
    return --mRefCount;
}

// ---- messages ----

uint8_t cTSFamilyManager::RegisterMessages()
{
    for (uint32_t id : {0xb2u, 0xb5u, 0xcded7b51u, 0xb6u, 0xafu}) mMessageIds.insert(id);
    reinterpret_cast<KeyMapSlot5Walker *>(&mMessageIds)
        ->CallSlot5ForEachKey(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<cIGZMessageTarget2 *>(this))),
            nullptr);
    return 1;
}

uint8_t cTSFamilyManager::UnregisterMessages()
{
    reinterpret_cast<KeyMapWalker *>(&mMessageIds)
        ->CallSlot6ForEachKey(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<cIGZMessageTarget2 *>(this))),
            nullptr);
    return 1;
}

// Null message: 0. The payload (interface 0x65297976) is asked for first; without one the answer is 0. By message id:
// 0xb2 / 0xb5 turn recording on / off and empty the log; 0xaf (when the payload's slot 8 reports) moves the first family
// whose slot 44 matches the payload's slot 9 value into its own home lot (slot 45 with slot 42); 0xb6 removes the empty
// families (slot 15); 0xcded7b51, while recording, appends the payload's text (its cIGZString, interface 0x089b7dc8) to
// the log, dropping the oldest entry past 25. Those answer 1, other ids 0. The payload is released.
bool cTSFamilyManager::DoMessage(void *message)
{
    if (!message) return false;
    cTSFamilyManager *m = this;
    void *payload = nullptr;
    uint8_t result;
    if (!Call<uint8_t>(message, 0, 0x65297976u, &payload)) {
        result = 0;
    } else {
        const uint32_t id = Call<uint32_t>(message, 4);
        result = 1;
        switch (id) {
        case 0xb2:
        case 0xb5:
            m->mRecording = id == 0xb2;
            m->mLog.clear();
            break;
        case 0xaf:
            if (Call<uint8_t>(payload, 8)) {
                const uint32_t value = Call<uint32_t>(payload, 9);
                for (void **it = m->mFamilies.mFirst; it != m->mFamilies.mLast; ++it) {
                    void *family = *it;
                    if (family && Call<uint32_t>(family, kFamSlot44) == value) {
                        Call<void>(family, kFamMoveIn, Call<uint32_t>(family, kFamHomeLot));
                        break;
                    }
                }
            }
            break;
        case 0xb6: RemoveEmptyFamilies(); break;
        case 0xcded7b51:
            if (m->mRecording) {
                void *text = nullptr;
                if (Call<uint8_t>(payload, 29, cRZString::kGZIID_cIGZString, &text)) {
                    m->mLog.emplace_back(Call<const char *>(text, 5));
                    if (m->mLog.size() > cTSFamilyManager::kLogLimit) m->mLog.pop_front();
                }
                if (text) ReleaseObject(text);
            }
            break;
        default: result = 0; break;
        }
    }
    if (payload) ReleaseObject(payload);
    return result != 0;
}

// ---- loading and saving ----

// Records the group, empties both vectors, and asks the persist system (slot 36) for the keys of the group's family
// records (type 0x46414d49). For each of the count it reports, the family with that instance (16 bits, sign-extended) is
// fetched (persist slot 5, interface 0x7ab5bcbb) and told its id and group (slot 32). When the lot manager's (singleton
// slot 10) current neighbourhood (its slot 33, then slot 6) is this group, the family's members the lot manager does not
// know (its slot 17 answers 0) are removed (slot 8), one per pass over the first slot 24 (3) members, until a pass removes none. A family with members
// (slot 24 with 3) joins the families. Without a family 0 one is made through the framework (id 0, record key {type, group,
// 0, 0}, the default name). The families are then sorted.
uint8_t cTSFamilyManager::Load(uint32_t group)
{
    void *lots = Call<void *>(GetCachedSingleton(), 10);
    void *persist = PersistSystem();
    mGroup = group;
    mFamilies.Clear();
    mRemovedFamilies.Clear();
    ServiceBlockVector keys{};
    const int32_t count = Call<int32_t>(persist, 36, &keys, mGroup, kFamilyType);
    for (int32_t i = 0; i < count; ++i) {
        void *family = nullptr;
        const int32_t id = *reinterpret_cast<const int16_t *>(static_cast<const uint8_t *>(keys.mFirst) + i * 16 + 8);
        const uint32_t key[4] = {kFamilyType, mGroup, uint32_t(id), 0};
        Call<uint8_t>(persist, 5, key, kFamilyInterface, &family, uint32_t(0), uint32_t(0));
        Call<void>(family, kFamLoad, id, mGroup);
        if (lots) {
            void *neighborhood = Call<void *>(lots, 33);
            if (Call<uint32_t>(neighborhood, 6) == group) {
                // Passes over the first slot 24 (3) members, starting over after each removal, until a pass removes none.
                for (bool removed = true; removed;) {
                    removed = false;
                    const int32_t members = Call<int32_t>(family, kFamMemberCount, uint32_t(3));
                    for (int32_t index = 0; index < members; ++index) {
                        const uint32_t *member = Call<const uint32_t *>(family, kFamMemberIdAt, index);
                        if (member && Call<uint32_t>(lots, 17, *member) == 0) {
                            Call<void>(family, kFamRemoveMember, *member);
                            removed = true;
                            break;
                        }
                    }
                }
            }
        }
        if (Call<int32_t>(family, kFamMemberCount, uint32_t(3)) != 0) PushBack(mFamilies, &family);
        if (family) ReleaseObject(family);
    }
    if (!GetFamily(0)) {
        void *family = nullptr;
        Call<void>(GetDirectorFrameworkObject(), 3, kFamilyType, kFamilyInterface, &family);
        Call<void>(family, kFamSetId, 0);
        const uint32_t key[4] = {kFamilyType, mGroup, 0, 0};
        Call<void>(Call<void *>(family, kFamRecord), 4, key);
        alignas(4) uint8_t name[sizeof(cRZString)];
        reinterpret_cast<cRZString *>(name)->Construct(GameAddress<const char>(kDefaultNameRva));
        Call<void>(family, kFamSetName, name);
        PushBack(mFamilies, &family);
        reinterpret_cast<cRZString *>(name)->Destruct();
        if (family) ReleaseObject(family);
    }
    if (mFamilies.Size() > 1) SortFamilies(mFamilies);
    keys.FreeBlock();
    return 1;
}

// Every family (and every removed family) has its record dropped from the persist cache (persist slot 3 object, slot 10
// with the record's key); the families also drop their resources (slot 75).
void cTSFamilyManager::Unload()
{
    void *persist = PersistSystem();
    for (void **it = mFamilies.mFirst; it != mFamilies.mLast; ++it) {
        void *family = *it;
        if (!family) continue;
        void *record = Call<void *>(family, kFamRecord);
        void *cache = Call<void *>(persist, 3);
        uint32_t key[4];
        Call<void>(cache, 10, Call<const uint32_t *>(record, 3, key));
        Call<void>(family, kFamDropResources);
    }
    for (void **it = mRemovedFamilies.mFirst; it != mRemovedFamilies.mLast; ++it) {
        void *record = Call<void *>(*it, kFamRecord);
        void *cache = Call<void *>(persist, 3);
        uint32_t key[4];
        Call<void>(cache, 10, Call<const uint32_t *>(record, 3, key));
    }
    mFamilies.Clear();
    mRemovedFamilies.Clear();
}

// For each removed family: slot 36, then when the persist cache (persist slot 3) finds its record (slot 34 with the key and
// a holder), the holder deletes it (slot 26 with the key again). Then the removed families are emptied, every family syncs
// (slot 61) and saves its record (slot 34, version 0x55), and every family with an id in 1..31999 saves its members (slot
// 35 with 0).
uint8_t cTSFamilyManager::Save()
{
    void *persist = PersistSystem();
    for (void **it = mRemovedFamilies.mFirst; it != mRemovedFamilies.mLast; ++it) {
        NeighborDataRef family(*it);
        void *f = family.Get();
        Call<void>(f, kFamMarkSaved);
        void *holder = nullptr;
        void *record = Call<void *>(f, kFamRecord);
        void *cache = Call<void *>(persist, 3);
        uint32_t key[4];
        if (Call<uint8_t>(cache, 34, Call<const uint32_t *>(record, 3, key), &holder)) {
            uint32_t again[4];
            Call<void>(holder, 26, Call<const uint32_t *>(Call<void *>(f, kFamRecord), 3, again));
        }
        if (holder) ReleaseObject(holder);
    }
    mRemovedFamilies.Clear();
    for (void **it = mFamilies.mFirst; it != mFamilies.mLast; ++it) {
        if (!*it) continue;
        Call<void>(*it, kFamSyncMembers);
        Call<uint8_t>(*it, kFamSaveRecord, 0x55u);
    }
    for (void **it = mFamilies.mFirst; it != mFamilies.mLast; ++it) {
        void *family = *it;
        if (family && Call<int32_t>(family, kFamId) != 0 && Call<int32_t>(family, kFamId) > 0 &&
            Call<int32_t>(family, kFamId) < 32000)
            Call<uint8_t>(family, kFamSaveMembers, 0);
    }
    return 1;
}

// ---- lookups ----

int32_t cTSFamilyManager::GetFamilyWorth(void *family)
{
    const int32_t funds = Call<int32_t>(family, kFamFunds);
    return Call<int32_t>(family, kFamHomeLotWorth) + funds;
}

void *cTSFamilyManager::FindFamilyByLot(int32_t lot)
{
    for (void **it = mFamilies.mFirst; it != mFamilies.mLast; ++it)
        if (*it && Call<int32_t>(*it, kFamHomeLot) == lot) return *it;
    return nullptr;
}

void *cTSFamilyManager::GetFamily(int32_t id)
{
    if (id == -2) {
        void *best = nullptr;
        int32_t lowest = 0x7fffffff;
        for (void **it = mFamilies.mFirst; it != mFamilies.mLast; ++it) {
            void *family = *it;
            if (!family) continue;
            const int32_t familyId = Call<int32_t>(family, kFamId);
            if (uint32_t(familyId - 1) < 31999 && familyId < lowest) {
                lowest = Call<int32_t>(family, kFamId);
                best = family;
            }
        }
        return best;
    }
    for (void **it = mFamilies.mFirst; it != mFamilies.mLast; ++it)
        if (*it && Call<int32_t>(*it, kFamId) == id) return *it;
    return nullptr;
}

void *cTSFamilyManager::FindFamilyBySlot44(int32_t value)
{
    for (void **it = mFamilies.mFirst; it != mFamilies.mLast; ++it)
        if (*it && Call<int32_t>(*it, kFamSlot44) == value) return *it;
    return nullptr;
}

// ---- creating and removing families ----

// An id below 31999 is replaced by the first id from 1 up that no family has (AL 0 when that passes 31999). The new family
// sorts after every family and removed family (one more than the highest slot 51 value, at least 1). It is made through
// the framework into *out, given the id, the sort key, two values from the singleton-C object's slot 64 object (its slot 6,
// 0 without one), a mode (2 when the singleton's slot 23 object's slot 6 object reports 2 in its slot 18, else 1) and the
// record key {type, group, id, 0}; it is marked (slot 36), added and the families sorted. Message 0xa5 (the id) is sent.
uint8_t cTSFamilyManager::CreateFamily(void **out, int32_t id)
{
    if (id < 31999) {
        int32_t free = 1;
        while (GetFamily(free)) ++free;
        id = free;
        if (free > 31999) return 0;
    }
    int32_t next = 1;
    for (RefPtrVector *vector : {&mFamilies, &mRemovedFamilies})
        for (void **it = vector->mFirst; it != vector->mLast; ++it)
            if (*it && next <= Call<int32_t>(*it, kFamSortKey)) next = Call<int32_t>(*it, kFamSortKey) + 1;
    void *lotState = Call<void *>(GetCachedSingleton(), 23);
    uint32_t mode = 1;
    if (Call<void *>(lotState, 6)) {
        mode = Call<uint32_t>(Call<void *>(lotState, 6), 18);
        if (mode != 2) mode = 1;
    }
    uint32_t value = 0;
    if (GetCachedSingletonC() && Call<void *>(GetCachedSingletonC(), 64))
        Call<void>(Call<void *>(GetCachedSingletonC(), 64), 6, uint32_t(0), &value);
    Call<void>(GetDirectorFrameworkObject(), 3, kFamilyType, kFamilyInterface, out);
    Call<void>(*out, kFamSetId, id);
    Call<void>(*out, kFamSetSortKey, next);
    Call<void>(*out, kFamSetValue54, value);
    Call<void>(*out, kFamSetValue56, value);
    Call<void>(*out, kFamSetValue58, mode);
    const uint32_t key[4] = {kFamilyType, mGroup, uint32_t(id), 0};
    Call<void>(Call<void *>(*out, kFamRecord), 4, key);
    Call<void>(*out, kFamMarkSaved);
    PushBackCounted(mFamilies, *out);
    if (mFamilies.Size() > 1) SortFamilies(mFamilies);
    SendFamilyMessage(kMsgFamilyCreated, id, false);
    return 1;
}

// For a family with a non-zero id and no home lot that is in the families: it joins the removed families and leaves the
// families (the later ones move down); when the live household's (0x20df9a) value 9 (its slot 15) is the family's id, that
// value becomes 0 (slot 16). Message 0xa4 (the id) is posted.
int32_t cTSFamilyManager::RemoveFamily(void *family)
{
    if (!family) return -1;
    const int32_t id = Call<int32_t>(family, kFamId);
    if (Call<int32_t>(family, kFamId) == 0 || Call<int32_t>(family, kFamHomeLot) != 0) return -1;
    void *household = LiveHousehold();
    for (void **it = mFamilies.mFirst; it != mFamilies.mLast; ++it) {
        if (*it != family) continue;
        PushBackCounted(mRemovedFamilies, family);
        // erase: each later element is assigned one place down, then the last is released.
        void **last = mFamilies.mLast;
        for (void **at = it; at + 1 != last; ++at) {
            void *moved = at[1];
            void *old = *at;
            if (moved == old) continue;
            if (moved) AddRefObject(moved);
            *at = moved;
            if (old) ReleaseObject(old);
        }
        void *tail = mFamilies.mLast[-1];
        if (tail) ReleaseObject(tail);
        --mFamilies.mLast;
        if (Call<int16_t>(household, 15, 9) == id) Call<void>(household, 16, 9, 0);
        SendFamilyMessage(kMsgFamilyRemoved, id, true);
        return 0;
    }
    return -1;
}

void cTSFamilyManager::RemoveEmptyFamilies()
{
    for (;;) {
        void *found = nullptr;
        for (void **it = mFamilies.mFirst; it != mFamilies.mLast; ++it) {
            void *family = *it;
            if (family && Call<int32_t>(family, kFamMemberCount, 3) == 0 && Call<int32_t>(family, kFamId) != 0) {
                found = family;
                break;
            }
        }
        if (!found) return;
        if (Call<int32_t>(found, kFamHomeLot) != 0) Call<void>(found, kFamSetHomeLot, 0);
        RemoveFamily(found);
    }
}

// ---- members ----

// Unless the family already has the neighbour (slot 29 with its id, slot 15): the neighbour's family value (0x3d, slot 16)
// is read, the family adds it (slot 7), the singleton's slot 6 object refreshes (slot 26). When the family then reports the
// member (slot 28): the neighbour manager's (singleton slot 26) record of it (slot 43) gets values 0x3d = family id and
// 0x43 = 0 (slot 11, flag 1), the neighbour too (slot 17), the family is marked dirty (slot 23) and told (slot 9). For a
// family and a previous family both with ids in 1..31999 where the previous one still has members, the lot objects of the
// neighbour's lot (key {0xac598eac, its slot 10 / 67 / 11 value, 1, 0}, interface 0xac598ea8) are refreshed (slots 22,
// 23, 24). Message 0x94 (family id) is posted and 0 returned. Otherwise -1.
int32_t cTSFamilyManager::AddNeighborToFamily(void *neighbor, void *family, void *previousFamily)
{
    if (!family || !neighbor) return -1;
    if (Call<uint8_t>(family, kFamHasMember, Call<uint32_t>(neighbor, 15))) return -1;
    Call<int16_t>(neighbor, 16, 0x3d);
    Call<void>(family, kFamAddMember, Call<uint32_t>(neighbor, 15));
    void *app = GetCachedSingleton();
    Call<void>(Call<void *>(app, 6), 26);
    if (!Call<uint32_t>(family, kFamIsMemberDirty, Call<uint32_t>(neighbor, 15))) return -1;
    void *neighbors = Call<void *>(app, 26);
    void *record = Call<void *>(neighbors, 43, Call<uint32_t>(neighbor, 15));
    if (record) {
        Call<void>(record, 11, 0x3d, Call<uint32_t>(family, kFamId), 1);
        Call<void>(record, 11, 0x43, 0, 1);
    }
    Call<void>(neighbor, 17, 0x3d, Call<uint32_t>(family, kFamId));
    Call<void>(neighbor, 17, 0x43, 0);
    Call<void>(family, kFamSetDirty, 1);
    Call<void>(family, kFamMemberMoved, Call<uint32_t>(neighbor, 15));
    if (Call<int32_t>(family, kFamId) >= 1 && Call<int32_t>(family, kFamId) < 32000 && previousFamily &&
        Call<int32_t>(previousFamily, kFamId) >= 1 && Call<int32_t>(previousFamily, kFamId) < 32000) {
        void *persist = PersistSystem();
        ServiceRefVector unused{};
        if (Call<int32_t>(previousFamily, kFamMemberCount, 3) != 0) {
            const uint32_t group = Call<uint32_t>(Call<void *>(Call<void *>(neighbor, 10), 67), 11);
            const uint32_t key[4] = {0xac598eac, group, 1, 0};
            void *objects = nullptr;
            if (Call<uint8_t>(persist, 5, key, 0xac598ea8u, &objects, 0, 0)) {
                Call<void>(objects, 22);
                Call<void>(objects, 23);
                Call<void>(objects, 24);
            }
            if (objects) ReleaseObject(objects);
        }
        unused.ReleaseAndFree();
    }
    SendFamilyMessage(kMsgFamilyChanged, 0, true, family);
    return 0;
}

// Without a family the neighbour's family value (0x3d, 16 bits) names it (none for 0). When the family reports the member
// (slot 28): the neighbour manager's record of it gets 0x3d = 0 (slot 11, flag 1), the neighbour too (slot 17), the family
// removes it (slot 8), the singleton's slot 6 object refreshes (slot 26), the family is marked dirty (slot 23) and message
// 0x94 (family id) is posted: 0. Otherwise -1.
int32_t cTSFamilyManager::RemoveNeighborFromFamily(void *neighbor, void *family)
{
    const int32_t familyId = Call<int16_t>(neighbor, 16, 0x3d);
    if (!family) {
        if (familyId == 0) return -1;
        family = GetFamily(familyId);
        if (!family) return -1;
    }
    if (!Call<uint32_t>(family, kFamIsMemberDirty, Call<uint32_t>(neighbor, 15))) return -1;
    void *app = GetCachedSingleton();
    void *neighbors = Call<void *>(app, 26);
    void *record = Call<void *>(neighbors, 43, Call<uint32_t>(neighbor, 15));
    if (record) Call<void>(record, 11, 0x3d, 0, 1);
    Call<void>(neighbor, 17, 0x3d, 0);
    Call<void>(family, kFamRemoveMember, Call<uint32_t>(neighbor, 15));
    Call<void>(Call<void *>(app, 6), 26);
    Call<void>(family, kFamSetDirty, 1);
    SendFamilyMessage(kMsgFamilyChanged, 0, true, family);
    return 0;
}

// ---- id lists ----

using FamilyIdVector = std::vector<uint16_t, GZServiceAllocator<uint16_t>>;

void cTSFamilyManager::GetFamilyIds(void *ids)
{
    auto *out = static_cast<FamilyIdVector *>(ids);
    for (void **it = mFamilies.mFirst; it != mFamilies.mLast; ++it)
        if (*it) out->push_back(Call<uint16_t>(*it, kFamId));
}

void cTSFamilyManager::GetHomelessFamilyIds(void *ids)
{
    auto *out = static_cast<FamilyIdVector *>(ids);
    for (void **it = mFamilies.mFirst; it != mFamilies.mLast; ++it) {
        void *family = *it;
        if (!family || Call<int32_t>(family, kFamHomeLot) >= 1) continue;
        const uint32_t id = Call<uint32_t>(family, kFamId);
        if (id - 1 < 31999) out->push_back(uint16_t(id));
    }
}

// ---- lots ----

uint8_t cTSFamilyManager::MoveFamilyIntoLot(int32_t id, int32_t lot)
{
    void *family = GetFamily(id);
    if (!family) return 1;
    if (FindFamilyByLot(lot)) return 0;
    Call<void>(family, kFamSetHomeLot, lot);
    Call<void>(family, kFamMoveIn, lot);
    Call<void>(family, kFamSetModified72, 1);
    return 1;
}

uint8_t cTSFamilyManager::IsObjectInFamily(int32_t id, void *object)
{
    using FamilyOfNeighbor = void *(__cdecl *)(uint32_t);
    void *family = OriginalFunction<FamilyOfNeighbor, 0x54a5e0>()(Call<uint32_t>(object, 33));
    NeighborDataRef held(family);
    return family && Call<int32_t>(family, kFamId) == id;
}

// -1 without the singleton or its lot manager (slot 6); 2 without the lot (lot manager slot 57) or for a lot of type 6, 7
// or 10 (the lot's slot 4 object's slot 8); 1 without the family. Then: type 1 gives 3, type 5 gives 10, type 3 without
// slot 24 (8) of the same object gives 4. For a lot someone lives in (slot 10): type 8 defers to slot 25; otherwise the
// occupant's slot 25 with the family decides (1-3, 8: 5; 4-6: 6; 7: 8; 9: 9; else 0). For an empty lot: type 8 defers to
// slot 25 (13 and 9 kept, else 0); otherwise 9 when the family has no members of kind 1 (slot 24), 0 for a lot of
// type 2 (slot 32), and else the lot manager's price check (slot 23): 7 when the family's funds are below the price.
int32_t cTSFamilyManager::CanMoveFamilyIntoLot(int32_t id, int32_t lot)
{
    void *app = GetCachedSingleton();
    if (!app) return -1;
    void *lots = Call<void *>(app, 6);
    if (!lots) return -1;
    void *lotObject = Call<void *>(lots, 57, lot);
    if (!lotObject) return 2;
    void *family = GetFamily(id);
    if (!family) return 1;
    void *occupant = FindFamilyByLot(lot);
    bool deferred = false;
    switch (Call<int32_t>(Call<void *>(lotObject, 4), 8)) {
    case 1: return 3;
    case 3:
        if (!Call<uint8_t>(Call<void *>(lotObject, 4), 24, 8)) return 4;
        break;
    case 5: return 10;
    case 6:
    case 7:
    case 10: return 2;
    case 8: deferred = true; break;
    default: break;
    }
    if (occupant && !deferred) {
        switch (Call<int32_t>(occupant, 25, family)) {
        case 1:
        case 2:
        case 3:
        case 8: return 5;
        case 4:
        case 5:
        case 6: return 6;
        case 7: return 8;
        case 9: return 9;
        default: return 0;
        }
    }
    if (deferred) {
        const int32_t result = CheckLotMembers(id, lot);
        return result == 13 ? 13 : result == 9 ? 9 : 0;
    }
    if (Call<int32_t>(family, kFamMemberCount, 1) == 0) return 9;
    if (Call<int32_t>(lotObject, 32) == 2) return 0;
    int32_t price = 0;
    uint8_t unused = 0;
    uint8_t priced = 0;
    if (!Call<uint8_t>(lots, 23, lot, &price, &unused, &priced, 1) || !priced) return 0;
    return Call<int32_t>(family, kFamFunds) < price ? 7 : 0;
}

// For an apartment base lot (lot manager slot 57, type 8 in slot 32): 12 unless one of its sub-lots (slots 42 / 43) has a
// neighbour (singleton slot 9, slot 10) whose slot 24 (0xf) value plus `count` is at most 8 (unsigned), then 0; past that,
// when the lot's sub-lot count reaches its slot 47 value (signed byte), 11 becomes 13 and 0 becomes 11.
int32_t cTSFamilyManager::CheckLotAges(int32_t count, int32_t lot)
{
    void *app = GetCachedSingleton();
    if (!app) return -1;
    void *lots = Call<void *>(app, 6);
    if (!lots) return -1;
    void *lotObject = Call<void *>(lots, 57, lot);
    if (!lotObject || Call<int32_t>(lotObject, 32) != 8) return 2;
    int32_t result = 12;
    void *neighbors = Call<void *>(GetCachedSingleton(), 9);
    for (int32_t i = 0; i < Call<int32_t>(lotObject, 42); ++i) {
        void *neighbor = Call<void *>(neighbors, 10, Call<uint32_t>(lotObject, 43, i));
        if (uint32_t(Call<int32_t>(neighbor, 24, 0xf) + count) <= 8) {
            result = 0;
            break;
        }
    }
    const int32_t limit = Call<int8_t>(lotObject, 47);
    if (Call<int32_t>(lotObject, 42) >= limit) result = result == 12 ? 13 : 11;
    return result;
}

// The same shape: 12 unless slot 23 of this (the id and a sub-lot) answers 0 for one of the sub-lots, then 0; the count
// against slot 47 as above; and when the family (slot 11) has no members of kind 1 (slot 24), 12 becomes 9 and 0 or 13
// become 11.
int32_t cTSFamilyManager::CheckLotMembers(int32_t id, int32_t lot)
{
    void *app = GetCachedSingleton();
    if (!app) return -1;
    void *lots = Call<void *>(app, 6);
    if (!lots) return -1;
    void *lotObject = Call<void *>(lots, 57, lot);
    if (!lotObject || Call<int32_t>(lotObject, 32) != 8) return 2;
    int32_t result = 12;
    for (int32_t i = 0; i < Call<int32_t>(lotObject, 42); ++i) {
        if (CanMoveFamilyIntoLot(id, Call<uint32_t>(lotObject, 43, i)) == 0) {
            result = 0;
            break;
        }
    }
    const int32_t limit = Call<int8_t>(lotObject, 47);
    if (Call<int32_t>(lotObject, 42) >= limit) result = result == 12 ? 13 : 11;
    if (Call<int32_t>(GetFamily(id), kFamMemberCount, 1) == 0) result = result != 12 ? 11 : 9;
    return result;
}

// ---- the data container face (import and export) ----

namespace {
constexpr uint32_t kFamilyContextInterface = 0x6db6f3ff; // cITSFamilyDataImportExportContext
constexpr uint32_t kSegmentInterface = 0x656b8f0c;       // the persist database segment the container carries
constexpr uint32_t kLotRecordType = 0x8db8ad90;          // the type of the record that holds a lot's data
constexpr uint32_t kRecordWriterInterface = 0xc51b11cb;  // a record's stream writer
constexpr uint32_t kLotRecordVersion = 0xcb;             // the version the lot record is written with
constexpr uint32_t kFamilyRecordVersion = 0x55;          // cTSFamily::kVersion

// Slots of the container (cITSDataContainer) and of the pieces it hands out.
constexpr int kContainerQuery = 0;       // (id, out): QueryInterface; also the segment's record and the writer
constexpr int kContainerGetSegment = 12; // (interface id, out)
constexpr int kContainerGetGroup = 14;   // (): the neighbourhood group
constexpr int kContextCopyIds = 23;      // (ServiceBlockVector *): the family ids of the context
constexpr int kSegmentOpen = 22;         // (key, out, mode)
constexpr int kSegmentClose = 24;        // (record)
constexpr int kStoreAddSegment = 5;      // (segment)
constexpr int kPersistAdd = 16;          // (record, segment)
constexpr int kFamPersistFace = 31;      // the cIGZPersistResource face at +4
constexpr int kFamLoadFirstGroup = 33;
constexpr int kFamGetName = 37;
constexpr int kFaceGetKey = 3;           // (key *): the record key
constexpr int kFaceSetKey = 4;           // (key *)
constexpr int kLotsGetLotData = 38;      // (lot): the lot's data object
constexpr int kLotWrite = 3;             // (stream)
constexpr int kStreamSetUint32 = 9;      // (value)
constexpr int kStreamSetMetadata = 19;   // (metadata)
} // namespace

bool cTSFamilyManager::Export(ifaces::cITSDataImportExportContext *exportContext)
{
    void *container = exportContext;
    uint8_t result = 0;
    void *store = CachedServiceGetter<0x10faf78, 0x0c4f252f, 0x2c4f2954>();
    void *context = nullptr;
    if (container && Call<uint8_t>(container, kContainerQuery, kFamilyContextInterface, &context)) {
        void *segment = nullptr;
        if (Call<uint8_t>(container, kContainerGetSegment, kSegmentInterface, &segment)) {
            void *app = GetCachedSingleton();
            void *persist = PersistSystem();
            ServiceBlockVector ids{};
            Call<void>(context, kContextCopyIds, &ids);
            for (const uint32_t *id = static_cast<const uint32_t *>(ids.mFirst); id != ids.mLast; ++id) {
                void *family = GetFamily(*id);
                if (!family) continue;
                // Writes the family's record into the container's segment under the container's group, then puts the
                // family's own key back.
                uint32_t savedKey[4];
                Call<void>(Call<void *>(family, kFamPersistFace), kFaceGetKey, savedKey);
                Call<void>(family, kFamLoadFirstGroup);
                uint32_t key[4] = {savedKey[0], savedKey[1], savedKey[2], savedKey[3]};
                key[1] = Call<uint32_t>(container, kContainerGetGroup);
                Call<void>(Call<void *>(family, kFamPersistFace), kFaceSetKey, key);
                const uint8_t saved = Call<uint8_t>(family, kFamSaveRecord, kFamilyRecordVersion);
                const uint8_t members = Call<uint8_t>(family, kFamSaveMembers, uint32_t(1));
                result = members & saved & 1;
                void *face = Call<void *>(family, kFamPersistFace);
                Call<void>(persist, kPersistAdd, face, segment);
                Call<void>(Call<void *>(family, kFamPersistFace), kFaceSetKey, savedKey);
                // Hands the family its own name back: the setter marks the family modified.
                Call<void>(family, kFamSetName, Call<void *>(family, kFamGetName));
                Call<void>(store, kStoreAddSegment, segment);
                if (!app || !Call<void *>(app, 6)) continue;
                void *lots = Call<void *>(app, 6);
                void *lotData = Call<void *>(lots, kLotsGetLotData, Call<uint32_t>(family, kFamId));
                if (!lotData) continue;
                // The home lot's data goes into a record of the segment too.
                const uint32_t recordKey[4] = {kLotRecordType, Call<uint32_t>(container, kContainerGetGroup), 1, 0};
                void *record = nullptr;
                if (Call<uint8_t>(segment, kSegmentOpen, recordKey, &record, uint32_t(2))) {
                    void *writer = nullptr;
                    if (Call<uint8_t>(record, kContainerQuery, kRecordWriterInterface, &writer)) {
                        Call<void>(writer, kStreamSetUint32, kLotRecordVersion);
                        auto *metadata = static_cast<UtilObjectB *>(::operator new(sizeof(UtilObjectB)));
                        if (metadata) metadata->Construct(kLotRecordVersion);
                        Call<void>(writer, kStreamSetMetadata, metadata);
                        result &= Call<uint8_t>(lotData, kLotWrite, writer);
                    }
                    Call<void>(segment, kSegmentClose, record);
                    if (writer) ReleaseObject(writer);
                }
                if (record) ReleaseObject(record);
            }
            ids.FreeBlock();
        }
        if (segment) ReleaseObject(segment);
    }
    if (context) ReleaseObject(context);
    return result;
}

namespace {
constexpr uint32_t kNeighborhoodContextInterface = 0x4e9c59c7; // cITSNeighborDataImportExportContext (id maps of the lots)
constexpr uint32_t kHoodRecordType = 0x4e474248;
constexpr uint32_t kHoodRecordInterface = 0x7aa51c46;
constexpr uint32_t kRecordReaderInterface = 0x51b11bd;         // a record's stream reader
constexpr uint32_t kLotDataClass = 0xaba73af;                   // framework class of the lot data read from a record
constexpr uint32_t kLotDataInterface = 0xaba72b8;
constexpr uint32_t kObjectMapInterface = 0xdb5c0c2;             // the destination context's object-id mapping
constexpr uint32_t kLotObjectTypeA = 0x4dd398a5;                // the two entry types that carry per-slot ids
constexpr uint32_t kLotObjectTypeB = 0xedd39930;

constexpr int kPersistGet = 5;           // (key, interface, out, 0, 0)
constexpr int kPersistListKeys = 36;     // (vector, group, type)
constexpr int kStoreOpenSegment = 4;     // (segment, 1)
constexpr int kContextGetMappedId = 25;  // (id, *mapped)
constexpr int kContextGetMappedLot = 25;    // (id, *mapped) of the neighbourhood context
constexpr int kContextGetMappedUnique = 29; // (id, *mapped) of the neighbourhood context
constexpr int kContextGetObjectMap = 27; // (interface, out) of the family context
constexpr int kMapMapTag = 36;           // (tag, *mapped) of the object mapping
constexpr int kMapMapValue = 32;         // (value, *mapped)
constexpr int kFamGetDescription = 39;
constexpr int kFamSetDescription = 40;
constexpr int kFamGetFlagBit1 = 71, kFamSetFlagBit1 = 70;
constexpr int kFamGetFlagBit0 = 67, kFamSetFlagBit0 = 66;
constexpr int kFamGetFlagBit3 = 69, kFamSetFlagBit3 = 68;
constexpr int kFamGetUniqueId = 49, kFamSetUniqueId = 50;
constexpr int kFamCollectObjects = 14;   // (ServiceRefVector *)
constexpr int kFamInsertObject = 11;     // (object)
constexpr int kLotDataClear = 9;         // (0)
constexpr int kLotDataReset = 10;
constexpr int kLotDataAddEntry = 15;     // (entry)
constexpr int kLotDataAddExtra = 32;     // (entry)
constexpr int kLotSourceFirst = 28, kLotSourceLast = 29;      // (iterator *): the entries
constexpr int kLotSourceExtraFirst = 42, kLotSourceExtraLast = 43;
constexpr int kEntryGetTag = 6, kEntrySetTag = 7;
constexpr int kEntryCount = 18, kEntryGetValue = 21, kEntrySetValue = 22;
constexpr int kStreamGetUint32 = 9;      // (uint32 *)
constexpr int kFrameworkCreate = 3;      // (class id, interface, out)
constexpr int kAppGetLotCatalog = 24;
constexpr int kCatalogGetLot = 14;       // (tag)

// Copies the imported family's data onto the family it maps to, then brings its lot data over (see Import).
void ImportFamily(void *container, void *context, void *lotContext, void *app, void *hood,
    void *segment, void *source, void *target, ServiceRefVector &objects, uint8_t &result)
{
    Call<void>(target, kFamSetName, Call<void *>(source, kFamGetName));
    Call<void>(target, kFamSetValue54, Call<uint32_t>(source, kFamFunds));
    Call<void>(target, kFamSetValue56, Call<uint32_t>(source, kFamFunds));
    Call<void>(target, kFamSetFlagBit1, uint32_t(Call<uint8_t>(source, kFamGetFlagBit1)));
    Call<void>(target, kFamSetFlagBit0, uint32_t(Call<uint8_t>(source, kFamGetFlagBit0)));
    Call<void>(target, kFamSetFlagBit3, uint32_t(Call<uint8_t>(source, kFamGetFlagBit3)));
    Call<void>(target, kFamSetDescription, Call<void *>(source, kFamGetDescription));
    Call<void>(target, kFamSetDirty, uint32_t(1));
    Call<void>(target, kFamSetUniqueId, Call<uint32_t>(source, kFamGetUniqueId, uint32_t(0)));
    if (lotContext) {
        // The lots map through the neighbourhood context.
        uint32_t mapped = 0;
        if (Call<uint8_t>(lotContext, kContextGetMappedLot, Call<uint32_t>(source, kFamHomeLot), &mapped))
            Call<void>(target, kFamSetHomeLot, mapped);
        mapped = 0;
        if (Call<uint8_t>(lotContext, kContextGetMappedLot, Call<uint32_t>(source, kFamSlot44), &mapped))
            Call<void>(target, kFamMoveIn, mapped);
        uint32_t uniqueMapped = 0;
        if (Call<uint8_t>(lotContext, kContextGetMappedUnique, Call<uint32_t>(target, kFamGetUniqueId, uint32_t(0)),
                &uniqueMapped))
            Call<void>(target, kFamSetUniqueId, uniqueMapped);
    }
    Call<void>(source, kFamCollectObjects, &objects);
    for (void **object = objects.mFirst, **end = objects.mLast; object != end; ++object)
        Call<void>(target, kFamInsertObject, *object);
    result = 1;
    if (!app || !Call<void *>(app, 6)) return;
    void *lots = Call<void *>(app, 6);
    void *lotData = Call<void *>(lots, kLotsGetLotData, Call<uint32_t>(target, kFamId));
    if (!lotData) return;
    Call<void>(lotData, kLotDataClear, uint32_t(0));
    Call<void>(lotData, kLotDataReset);
    // The lot's source data: the hood record's entry for the family, else the lot record read from the segment.
    void *lotSource = nullptr;
    if (hood) {
        void *found = Call<void *>(hood, kLotsGetLotData, Call<uint32_t>(source, kFamId));
        if (found) {
            AddRefObject(found);
            lotSource = found;
        }
    }
    if (!lotSource) {
        const uint32_t recordKey[4] = {kLotRecordType, Call<uint32_t>(container, kContainerGetGroup), 1, 0};
        void *record = nullptr;
        if (Call<uint8_t>(segment, kSegmentOpen, recordKey, &record, uint32_t(1))) {
            void *reader = nullptr;
            if (Call<uint8_t>(record, kContainerQuery, kRecordReaderInterface, &reader)) {
                uint32_t version = kLotRecordVersion;
                Call<void>(reader, kStreamGetUint32, &version);
                auto *metadata = static_cast<UtilObjectB *>(::operator new(sizeof(UtilObjectB)));
                if (metadata) metadata->Construct(version);
                Call<void>(reader, kStreamSetMetadata, metadata);
                void *created = nullptr;
                void *framework = GetDirectorFrameworkObject();
                Call<void>(framework, kFrameworkCreate, kLotDataClass, kLotDataInterface, &created);
                result = Call<uint8_t>(created, 4, reader) & 1;
                if (created != lotSource) {
                    if (created) AddRefObject(created);
                    lotSource = created;
                }
                if (created) ReleaseObject(created);
            }
            Call<void>(segment, kSegmentClose, record);
            if (reader) ReleaseObject(reader);
        }
        if (record) ReleaseObject(record);
    }
    if (lotSource) {
        // Entries keep their tag mapped through the destination context's object map; a tag that is a plain zero or one the
        // lot catalog knows goes into the lot data.
        void *map = nullptr;
        Call<void>(context, kContextGetObjectMap, kObjectMapInterface, &map);
        void **entry = nullptr, **last = nullptr;
        Call<void>(lotSource, kLotSourceFirst, &entry);
        Call<void>(lotSource, kLotSourceLast, &last);
        for (; entry != last; ++entry) {
            void *item = *entry;
            uint32_t mappedTag = 0;
            const uint32_t entryTag = Call<uint32_t>(item, kEntryGetTag); // asked before the map is looked at
            if (map && Call<uint8_t>(map, kMapMapTag, entryTag, &mappedTag))
                Call<void>(item, kEntrySetTag, mappedTag);
            const uint32_t tag = Call<uint32_t>(item, kEntryGetTag);
            const uint32_t known = Call<uint32_t>(Call<void *>(app, kAppGetLotCatalog), kCatalogGetLot, tag);
            if (tag == 0) {
                if (known == 0) continue;
            } else if (tag == kLotObjectTypeA || tag == kLotObjectTypeB) {
                // ORIGINAL-BUG: the object map is used unchecked here (a null map crashes when an entry has ids).
                const int32_t count = int16_t(Call<uint32_t>(item, kEntryCount));
                if (count != 0) {
                    uint32_t index = 0;
                    do {
                        const uint32_t slotIndex = uint16_t(index);
                        const int32_t value = int16_t(Call<uint32_t>(item, kEntryGetValue, slotIndex));
                        uint32_t mappedValue = 0xffffffff;
                        Call<void>(map, kMapMapValue, value, &mappedValue);
                        Call<void>(item, kEntrySetValue, slotIndex, mappedValue);
                        ++index;
                    } while (index < uint32_t(count));
                }
            }
            Call<void>(lotData, kLotDataAddEntry, item);
        }
        void **extra = nullptr, **extraLast = nullptr;
        Call<void>(lotSource, kLotSourceExtraFirst, &extra);
        Call<void>(lotSource, kLotSourceExtraLast, &extraLast);
        for (; extra != extraLast; ++extra) {
            void *item = *extra;
            uint32_t mappedTag = 0;
            const uint32_t entryTag = Call<uint32_t>(item, kEntryGetTag); // asked before the map is looked at
            if (map && Call<uint8_t>(map, kMapMapTag, entryTag, &mappedTag))
                Call<void>(item, kEntrySetTag, mappedTag);
            void *catalog = Call<void *>(app, kAppGetLotCatalog);
            const uint32_t known = Call<uint32_t>(catalog, kCatalogGetLot, Call<uint32_t>(item, kEntryGetTag));
            if (Call<uint32_t>(item, kEntryGetTag) == 0 || known != 0) Call<void>(lotData, kLotDataAddExtra, item);
        }
        if (map) ReleaseObject(map);
        ReleaseObject(lotSource);
    }
}
} // namespace

bool cTSFamilyManager::Import(ifaces::cITSDataImportExportContext *importContext)
{
    void *container = importContext;
    uint8_t result = 0;
    void *context = nullptr;
    if (container && Call<uint8_t>(container, kContainerQuery, kFamilyContextInterface, &context)) {
        void *lotContext = nullptr;
        Call<uint8_t>(container, kContainerQuery, kNeighborhoodContextInterface, &lotContext);
        void *segment = nullptr;
        if (Call<uint8_t>(container, kContainerGetSegment, kSegmentInterface, &segment)) {
            void *app = GetCachedSingleton();
            void *persist = PersistSystem();
            void *store = CachedServiceGetter<0x10faf78, 0x0c4f252f, 0x2c4f2954>();
            if (store) Call<void>(store, kStoreOpenSegment, segment, uint32_t(1));
            void *hood = nullptr;
            const uint32_t hoodKey[4] = {kHoodRecordType, Call<uint32_t>(container, kContainerGetGroup), 1, 0};
            Call<void>(persist, kPersistGet, hoodKey, kHoodRecordInterface, &hood, uint32_t(0), uint32_t(0));
            ServiceBlockVector keys{};
            {
                const uint32_t group = Call<uint32_t>(container, kContainerGetGroup);
                Call<void>(persist, kPersistListKeys, &keys, group, cTSFamilyManager::kFamilyType);
            }
            for (const uint32_t *keyAt = static_cast<const uint32_t *>(keys.mFirst); keyAt != keys.mLast; keyAt += 4) {
                const uint32_t key[4] = {keyAt[0], keyAt[1], keyAt[2], keyAt[3]};
                void *source = nullptr;
                Call<void>(persist, kPersistGet, key, cTSFamilyManager::kFamilyInterface, &source, uint32_t(0), uint32_t(0));
                Call<void>(source, kFamLoad, key[2], key[1]);
                uint32_t mappedId = 0xffffffff;
                const uint32_t sourceId = Call<uint32_t>(source, kFamId);
                if (Call<uint8_t>(context, kContextGetMappedId, sourceId, &mappedId)) {
                    void *target = GetFamily(mappedId);
                    if (target) {
                        Call<void>(source, kFamLoadFirstGroup);
                        ServiceRefVector objects{};
                        ImportFamily(container, context, lotContext, app, hood, segment, source, target, objects,
                            result);
                        objects.ReleaseAndFree();
                    }
                }
                if (source) ReleaseObject(source);
            }
            if (store) Call<void>(store, kStoreAddSegment, segment);
            keys.FreeBlock();
            if (hood) ReleaseObject(hood);
        }
        if (segment) ReleaseObject(segment);
        if (lotContext) ReleaseObject(lotContext);
    }
    if (context) ReleaseObject(context);
    return result;
}

// ---- the statistics report ----

namespace {
// A copy of a log text kept in the report: a cRZString (0x20 bytes) built by copying the text, destroyed through its virtual
// destructor like the log's entries.
struct LogCopy {
    alignas(4) uint8_t mBytes[0x20];
    explicit LogCopy(const FamilyLogEntry &source)
    {
        const auto *from = reinterpret_cast<const cRZString *>(source.mString);
        auto *to = reinterpret_cast<cRZString *>(mBytes);
        new (&to->mString) std::string(from->mString.data(), from->mString.size());
        to->mVTable = GameAddress(cRZString::kVTable);
        to->mRefCount = 0;
    }
    LogCopy(const LogCopy &source)
    {
        const auto *from = reinterpret_cast<const cRZString *>(source.mBytes);
        auto *to = reinterpret_cast<cRZString *>(mBytes);
        new (&to->mString) std::string(from->mString.data(), from->mString.size());
        to->mVTable = GameAddress(cRZString::kVTable);
        to->mRefCount = 0;
    }
    LogCopy &operator=(const LogCopy &) = delete;
    ~LogCopy() { Call<void *>(mBytes, 33, uint32_t(0)); }
};
static_assert(sizeof(LogCopy) == 0x20);

// What the report hands to the statistics sink (family slot 65's object, slot 6): 0x54 bytes. The words at +2.. count the
// members of the family and their kinds.
struct FamilyStatistics {
    uint16_t mHomeLot;                  // +0x00 the family's slot 42
    uint16_t mMembers;                  // +0x02 neighbours found for the member ids
    uint16_t mKinds[4];                 // +0x04 members by (flag 0x41 set) | (value 0x3a below 0x12) * 2
    uint16_t mFlagged;                  // +0x0c members of kind 0 or 1 with flag 0x38
    uint16_t mHolderValueA;             // +0x0e
    uint32_t mHouseholdA;               // +0x10 household slot 35
    uint32_t mHouseholdB;               // +0x14 household slot 36
    uint16_t mHolderValueB;             // +0x18
    uint16_t mHolderValueC;             // +0x1a
    uint32_t mCreatedValue[8];          // +0x1c created object's slot 4 with 1, 4, 3, 5, 6, 0 + 2, 7; then household slot 29
    int16_t mMax;                       // +0x3c largest value 0xa0 among the members
    int16_t mMin;                       // +0x3e smallest
    uint8_t mHouseholdFlagA;            // +0x40 household slot 15 with 0x2a
    uint8_t mHouseholdFlagB;            // +0x41 with 0x1e
    uint8_t mFamilyFromLot;             // +0x42 the family came from the current lot, not from the lot state
    uint8_t mPad43;                     // +0x43 (left unwritten by the original)
    std::vector<LogCopy, GZServiceAllocator<LogCopy>> mLog; // +0x44 copies of the manager's log texts
    uint16_t mFamilyValue;              // +0x50 family slot 61
    uint16_t mPad52;
};
static_assert(sizeof(FamilyStatistics) == 0x54);
static_assert(offsetof(FamilyStatistics, mLog) == 0x44);
static_assert(offsetof(FamilyStatistics, mMax) == 0x3c);
} // namespace

void cTSFamilyManager::ReportStatistics()
{
    void *lotState = GetCachedSlot23();
    bool fromLot = false;
    void *family = Call<void *>(lotState, 16);
    if (!family) {
        void *lot = Call<void *>(lotState, 27);
        if (!lot) return;
        family = FindFamilyByLot(int32_t(reinterpret_cast<uintptr_t>(lot))); // the lot state's slot 27 word, passed on as is
        fromLot = true;
        if (!family) return;
    }
    FamilyStatistics stats{};
    stats.mHomeLot = Call<uint16_t>(family, kFamHomeLot);
    bool haveRange = false;
    void *household = LiveHousehold();
    void *app = GetCachedSingleton();
    void *neighbors = Call<void *>(app, 26);
    const int32_t members = Call<int32_t>(family, kFamMemberCount, uint32_t(3));
    for (int32_t i = 0; i < members; ++i) {
        const uint32_t *id = Call<const uint32_t *>(family, kFamMemberIdAt, i);
        void *neighbor = Call<void *>(neighbors, 43, *id);
        if (!neighbor) continue;
        ++stats.mMembers;
        const uint32_t flagSet = Call<uint16_t>(neighbor, 10, uint32_t(0x41), uint32_t(1)) != 0;
        const uint32_t young = int16_t(Call<uint16_t>(neighbor, 10, uint32_t(0x3a), uint32_t(1))) < 0x12 ? 2 : 0;
        const uint32_t kind = flagSet | young;
        ++stats.mKinds[kind];
        if (kind < 2 && Call<uint16_t>(neighbor, 10, uint32_t(0x38), uint32_t(1)) != 0) ++stats.mFlagged;
        const int16_t value = int16_t(Call<uint16_t>(neighbor, 10, uint32_t(0xa0), uint32_t(1)));
        if (haveRange) {
            if (value > stats.mMax) stats.mMax = value;
            else if (value < stats.mMin) stats.mMin = value;
        } else {
            stats.mMin = value;
            haveRange = true;
            stats.mMax = value;
        }
    }
    void *holder = nullptr;
    Call<void>(Call<void *>(app, 13), 10, &holder);
    stats.mHolderValueA = Call<uint16_t>(holder, 8);
    stats.mHouseholdA = Call<uint32_t>(household, 35);
    stats.mHouseholdB = Call<uint32_t>(household, 36);
    stats.mHolderValueB = Call<uint16_t>(holder, 3);
    stats.mHolderValueC = Call<uint16_t>(holder, 7);
    void *created = nullptr;
    Call<void>(GetDirectorFrameworkObject(), 3, uint32_t(0x7a3ce443), uint32_t(0x7a3dd334), &created);
    Call<void>(household, 45, created);
    stats.mCreatedValue[0] = Call<uint32_t>(created, 4, uint32_t(1));
    stats.mCreatedValue[1] = Call<uint32_t>(created, 4, uint32_t(4));
    stats.mCreatedValue[2] = Call<uint32_t>(created, 4, uint32_t(3));
    stats.mCreatedValue[3] = Call<uint32_t>(created, 4, uint32_t(5));
    stats.mCreatedValue[4] = Call<uint32_t>(created, 4, uint32_t(6));
    const uint32_t first = Call<uint32_t>(created, 4, uint32_t(0));
    stats.mCreatedValue[5] = Call<uint32_t>(created, 4, uint32_t(2)) + first;
    stats.mCreatedValue[6] = Call<uint32_t>(created, 4, uint32_t(7));
    stats.mCreatedValue[7] = Call<uint32_t>(household, 29);
    stats.mFamilyValue = Call<uint16_t>(family, 61);
    stats.mHouseholdFlagA = Call<uint16_t>(household, 15, uint32_t(0x2a)) != 0;
    stats.mHouseholdFlagB = Call<uint16_t>(household, 15, uint32_t(0x1e)) != 0;
    stats.mFamilyFromLot = fromLot;
    for (const FamilyLogEntry &entry : mLog) stats.mLog.emplace_back(entry);
    mLog.clear();
    if (void *sink = Call<void *>(family, 65)) Call<void>(sink, 6, &stats);
    if (created) ReleaseObject(created);
    if (holder) ReleaseObject(holder);
}

// ---- the RVA hooks' adapters (one statement each: matchcheck v7 ties them to the member in our vtable) ----

using Adapters = cTSFamilyManagerAdapters;

// 0x4b4550: the deleting destructor (game callers: none but the vtable; a game-built manager reaches it through slot 26).
bool __fastcall Adapters::QueryInterface(Self *self, void *, uint32_t id, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::QueryInterface(id, out);
}
uint32_t __fastcall Adapters::Release(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::Release();
}
uint8_t __fastcall Adapters::UnregisterMessages(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::UnregisterMessages();
}
