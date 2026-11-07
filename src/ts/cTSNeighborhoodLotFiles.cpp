// cTSNeighborhood: adding and removing lots (AddLot slot 61, RemoveLot slot 62; lane CQ).
#include "rz/RZMessageServer.h"
#include "app/AppInit.h"
#include "game/game.h"
#include "game/original.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/cIGZMessage2Standard.h"
#include "ifaces/cIGZMessageServer.h"
#include "ifaces/cIGZPersistResource.h"
#include "ifaces/cITSBaseLotInfo.h"
#include "ifaces/cITSGameStateController.h"
#include "ifaces/cITSLotInfo.h"
#include "ifaces/cITSNeighborhood.h"
#include "ifaces/cITSNeighborhoodGroupData.h"
#include "ifaces/cITSNeighborhoodInfo.h"
#include "ifaces/cITSPersistContext.h"
#include "ifaces/cITSPersistSystem.h"
#include "rz/RZMemory.h"
#include "rz/ServiceGetters.h"
#include "rz/StaticGetters.h"
#include "rz/StaticGetters2.h"
#include "rz/cRZMessage2Standard.h"
#include "simantics/ServiceRefVector.h"
#include "ts/cTSNeighborhood.h"
#include "ts/cTSNeighborhoodInternal.h"

#include <new>
#include <tuple>
#include <utility>

namespace {
using namespace nbhood;

constexpr uint32_t kMessageLotAdded = 0xae;
constexpr uint32_t kMessageLotRemoving = 0xaf;   // sent (synchronously) before the lot leaves the maps
constexpr uint32_t kMessageLotRemoved = 0xb0;    // posted after
constexpr uint32_t kLotRecordType = 0xbf999e7;       // the persist type of the lot records
constexpr uint32_t kLotResourceType = 0x6bf97e03;    // the resource type GetResource reads them as
constexpr uint32_t kLotFlagNotPlaced = 0x20;
constexpr uint32_t kLotFileOptionQuery = 0x6ced477c;
constexpr uint32_t kLotFileSegmentType = 0x3b2271bb;

// A message made the way the game does (0x1c03b7 allocates 0x2c bytes, 0x1c033d builds a cRZMessage2Standard) and counted once.
// When the allocation fails the pointer stays null and the caller's first virtual call dereferences it, as in the original.
struct Message {
    void *object = nullptr;
    Message()
    {
        auto *raw = static_cast<uint8_t *>(RZMemory::Malloc(cRZMessage2Standard::kSize));
        if (raw) {
            *reinterpret_cast<const void **>(raw) = GameAddress(cRZMessage2Standard::kVTable);
            *reinterpret_cast<uint32_t *>(raw + 4) = 0;
            raw[8] = 0;
            for (uint32_t offset = 0x0c; offset <= 0x28; offset += 4) *reinterpret_cast<uint32_t *>(raw + offset) = 0;
        }
        object = raw;
        if (object) As<ifaces::cIGZMessage2Standard>(object)->AddRef();
    }
    Message(const Message &) = delete;
    Message &operator=(const Message &) = delete;
    ~Message() noexcept(false)
    {
        if (object) As<ifaces::cIGZMessage2Standard>(object)->Release();
    }
    ifaces::cIGZMessage2Standard *operator->() { return As<ifaces::cIGZMessage2Standard>(object); }
};

ifaces::cIGZMessageServer *MessageServer()
{
    return As<ifaces::cIGZMessageServer>(GetCachedMessageServer());
}

// A cRZString copy of the base lot info's name (the base info answers a cIGZString).
void NameOf(cRZString &out, ifaces::cITSLotInfo *lot)
{
    out.Construct(*static_cast<cIGZString *>(lot->GetBaseLotInfo()->GetName()));
}

// Whether the persist system has a file with the lot's file name in the neighbourhood's directory.
bool LotFileExists(void *owner, ifaces::cITSLotInfo *lot)
{
    if (!lot || !owner) return false;
    cRZString directory{GameAddress<void>(cRZString::kVTable), std::string(), 0};
    // The path of the lot files' directory, from the neighbourhood's file prefix (an original function that only builds strings).
    OriginalFunction<void(__cdecl *)(void *, cRZString *, uint32_t, uint32_t), 0xc0b232>()(
        As<ifaces::cITSNeighborhoodInfo>(owner)->GetFilePrefix(), &directory, 1, 0xffffffffu);
    return PersistSystem()->HasSegmentForFile(&directory, lot->GetFileName(), 0);
}
} // namespace

uint8_t cTSNeighborhood::LoadLots()
{
    if (mByteC8) return 1;
    auto *info = As<ifaces::cITSNeighborhoodInfo>(mOwner);
    const uint32_t group = info->AsPersistResource()->GetGroup();
    mByteC8 = 1;
    ifaces::cITSPersistSystem *persist = PersistSystem();
    struct KeyList : ServiceBlockVector {
        KeyList() { mFirst = mLast = mEnd = nullptr; }
        ~KeyList() { FreeBlock(); }
    } keys;
    const uint32_t count = persist->ListKeys(&keys, group, kLotRecordType);
    for (uint32_t index = 0; index < count; ++index) {
        Ref loaded;
        const auto *key = reinterpret_cast<const ::Key16 *>(static_cast<const uint8_t *>(keys.mFirst) + index * 16);
        if (!persist->GetResource(key, kLotResourceType, &loaded.object, 0, 0)) continue;
        auto *lot = As<ifaces::cITSLotInfo>(loaded.object);
        const int32_t number = lot->GetLotNumber();
        if (number == 0) continue;
        if (static_cast<int32_t>(mField6C) <= number) mField6C = static_cast<uint32_t>(number) + 1;
        lot->LoadTextsFromResource(number, group);
        auto *baseName = static_cast<cIGZString *>(lot->GetBaseLotInfo()->GetName());
        cRZString path{GameAddress<void>(cRZString::kVTable), std::string(), 0};
        FormatNumberOf(&path, lot, info); // the lot file name (slot 70)
        lot->SetFileName(&path);
        bool exists = LotFileExists(mOwner, lot);
        if (!exists) {
            // Without a file under that name, the lot's file is "<prefix>_<name>".
            path.Assign(*static_cast<cIGZString *>(info->GetFilePrefix()));
            cRZString separator{GameAddress<void>(cRZString::kVTable), std::string("_"), 0};
            path.Append(*separator.AsInterface());
            path.Append(*baseName);
            lot->SetFileName(&path);
            exists = LotFileExists(mOwner, lot);
        }
        if (!exists) continue;
        lot->GetBaseLotInfo()->ClearFlags(kLotFlagNotPlaced);
        std::pair<NeighborhoodLotMap::iterator, bool> added;
        {
            std::pair<const int32_t, NeighborDataRef> entry(std::piecewise_construct, std::forward_as_tuple(number),
                                                            std::forward_as_tuple(loaded.object));
            added = mLots.insert(entry);
        }
        if (added.second) {
            bool named;
            {
                NeighborhoodTextEntry entry(*baseName, loaded.object);
                named = mTexts.insert(entry).second;
            }
            // A name that is taken already: the lot does not stay in the lot map either.
            if (!named) mLots.erase(added.first);
        }
    }
    return 1;
}

uint8_t cTSNeighborhood::AddLot(void *lotObject)
{
    auto *lot = As<ifaces::cITSLotInfo>(lotObject);
    int32_t number = lot->GetLotNumber();
    if (number == 0 || number == -1) {
        number = NextFreeLotNumber();
        lot->SetLotNumber(number);
    }
    {
        // The map entry counts the lot, and so does its copy in the tree.
        std::pair<const int32_t, NeighborDataRef> entry(std::piecewise_construct, std::forward_as_tuple(lot->GetLotNumber()),
                                                        std::forward_as_tuple(lotObject));
        mLots.insert(entry);
    }
    cRZString name{GameAddress<void>(cRZString::kVTable), std::string(), 0};
    NameOf(name, lot);
    {
        NeighborhoodTextEntry entry(*name.AsInterface(), lotObject);
        mTexts.insert(entry);
    }
    ifaces::cITSGameStateController *gsc = StateController();
    gsc->GetNeighborhoodInfo();   // the answer is not used
    cRZString path{GameAddress<void>(cRZString::kVTable), std::string(), 0};
    FormatNumberOf(&path, lot, mOwner); // the lot file name (slot 70)
    lot->SetFileName(&path);
    cRZString option{GameAddress<void>(cRZString::kVTable), std::string(), 0};
    appinit::CommonImplSlot4(kLotFileOptionQuery, reinterpret_cast<uint32_t>(&option), 1, 0, 0xffffffffu);
    ifaces::cITSPersistSystem *persist = PersistSystem();
    Ref segment;
    persist->OpenSegment(kLotFileSegmentType, &segment.object, 1, &option, lot->GetFileName(), 1, 1);
    ifaces::cIGZMessageServer *server = MessageServer();
    Message added;
    added->SetType(kMessageLotAdded);
    added->SetData1(static_cast<uint32_t>(number));
    // The message holds a new copy of the neighbourhood's file prefix.
    cRZString *prefix = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
    if (prefix) prefix->Construct(*static_cast<cIGZString *>(gsc->GetNeighborhoodInfo()->GetFilePrefix()));
    added->SetExtra(prefix);
    server->MessagePost(added.object, 0);
    ifaces::cITSPersistContext *context = gsc->GetPersistContext(kPersistContextBusiness);
    BusinessKey key;
    key.group = GroupOf(PersistFace());
    key.instance = static_cast<uint32_t>(lot->GetLotNumber());
    key.type = kBusinessRecordType;
    key.zero = 0;
    context->DeleteResource(&key);
    As<ifaces::cITSNeighborhoodGroupData>(mSecondOwner)->IndexLot(number);
    mByteC8 = 1;
    return 1;
}

uint8_t cTSNeighborhood::RemoveLot(int32_t lotNumber)
{
    auto found = mLots.lower_bound(lotNumber);
    if (found == mLots.end() || lotNumber < found->first) return 0;
    void *lotObject = found->second.Get();
    NeighborDataRef held(lotObject);
    auto *lot = As<ifaces::cITSLotInfo>(lotObject);
    StateController();   // fetched and dropped
    ifaces::cIGZMessageServer *server = MessageServer();
    Message removing;
    removing->SetType(kMessageLotRemoving);
    removing->SetData1(static_cast<uint32_t>(lotNumber));
    server->MessageSend(removing.object);
    // ORIGINAL-BUG: a null value in the map is dereferenced here (the hold above only guards its own count).
    PersistSystem()->ReleaseSegmentsForFile(lot->GetFileName(), 1);
    mLots.erase(found);
    {
        cRZString name{GameAddress<void>(cRZString::kVTable), std::string(), 0};
        NameOf(name, lot);
        auto indexed = mTexts.find(name);
        if (indexed != mTexts.end()) mTexts.erase(indexed);
    }
    auto inventory = mMembers1.find(static_cast<int16_t>(lotNumber));
    if (inventory != mMembers1.end()) mMembers1.erase(inventory);
    Message removed;
    removed->SetType(kMessageLotRemoved);
    server->MessagePost(removed.object, 0);
    if (mSecondOwner) As<ifaces::cITSNeighborhoodGroupData>(mSecondOwner)->RebuildLotIndex(lotNumber);
    return 1;
}

// ---- adapters (the RVA hooks; src/hook/tables/cTSNeighborhood.cpp): next to the bodies they inline ----

using Adapters = cTSNeighborhoodAdapters;

uint8_t __fastcall Adapters::AddLot(Self *self, void *, void *lot)
{
    [[msvc::forceinline_calls]] return self->Self::AddLot(lot);
}

uint8_t __fastcall Adapters::RemoveLot(Self *self, void *, int32_t lotNumber)
{
    [[msvc::forceinline_calls]] return self->Self::RemoveLot(lotNumber);
}

uint8_t __fastcall Adapters::LoadLots(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::LoadLots();
}
