// cTSNeighborhood: the lot name index, lot inventories, lot references and the neighbourhood's message handler (lane CL).
#include "ts/cTSNeighborhood.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/ComFramework.h"
#include "ifaces/SGDebugDraw.h"
#include "ifaces/SGRenderIdRegistry.h"
#include "ifaces/cIGZMessage2Standard.h"
#include "ifaces/cIGZPersistResource.h"
#include "ifaces/cITSBaseLotInfo.h"
#include "ifaces/cITSGameStateController.h"
#include "ifaces/cITSInventory.h"
#include "ifaces/cITSInventoryToken.h"
#include "ifaces/cITSLotInfo.h"
#include "ifaces/cITSNeighborhood.h"
#include "ifaces/cITSNeighborhoodGroupData.h"
#include "ifaces/cITSNeighborhoodInfo.h"
#include "ifaces/cITSPersistContext.h"
#include "ifaces/cITSPersistSystem.h"
#include "ifaces/cTSSGSystem.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/StaticGetters2.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <string.h>

namespace {
constexpr uint32_t kMessage2StandardInterface = 0x65297976;
constexpr uint32_t kMessageFamilyLotsFreed = 0x9e;   // the lots of a family id lose their owner
constexpr uint32_t kMessageDropFamilyInventory = 0xa4;
constexpr uint32_t kMessageLotRemoved = 0xaf;        // RemoveLot posts it, DoMessage answers with RemoveLotReferences
constexpr uint32_t kLotFlagOwned = 0x40;
constexpr uint32_t kLotTypeWithSimInventories = 3;
constexpr uint32_t kTokenGuidA = 0xeaa2454;         // the inventory tokens that point at a lot
constexpr uint32_t kTokenGuidB = 0x2eaa244c;
constexpr uint32_t kTokenFlagMarked = 4;
constexpr uint32_t kPersistContextBusiness = 0xd0590041;
constexpr uint32_t kBusinessRecordType = 0x104f6a6e;
constexpr uint32_t kLotNumberPrefix = 0xda33f8;       // the marker that starts a "by number" lot name (game data)
constexpr uint32_t kLotNumberFormat = 0xd9b648;       // scanf format reading the number after the marker
constexpr uint32_t kLotFileFormat = 0xdca02c;         // sprintf format of a lot file name: prefix and number
constexpr uint32_t kSgSystemCache = 0x10cc458;
constexpr uint32_t kSgSystemIdA = 0x9a353ce8;
constexpr uint32_t kSgSystemIdB = 0xfa353cdb;
constexpr uint32_t kPersistSystemCache = 0x10faf6c;
constexpr uint32_t kPersistSystemIdA = 0x056b906e;
constexpr uint32_t kPersistSystemIdB = 0x9af5e924;
constexpr uint32_t kLotInventoryOwnerKind = 1;

// A counted pointer held by a local of the original: released when it goes out of scope.
struct Ref {
    void *object = nullptr;
    Ref() = default;
    Ref(const Ref &) = delete;
    Ref &operator=(const Ref &) = delete;
    ~Ref() noexcept(false) { if (object) As<cIGZUnknown>(object)->Release(); }
};

// The 16-byte resource key of a lot's business record.
struct BusinessKey {
    uint32_t type, group, instance, zero;
};

// 0x54c9e0: {type, the lot's group, the lot's number, 0}.
BusinessKey LotBusinessKey(ifaces::cITSLotInfo *lot)
{
    ifaces::cIGZPersistResource *resource = lot->AsPersistResource();
    const int32_t number = lot->GetLotNumber();
    BusinessKey key;
    key.group = resource->GetGroup();
    key.instance = static_cast<uint32_t>(number);
    key.type = kBusinessRecordType;
    key.zero = 0;
    return key;
}

ifaces::cITSPersistSystem *PersistSystem()
{
    return As<ifaces::cITSPersistSystem>(CachedServiceGetter<kPersistSystemCache, kPersistSystemIdA, kPersistSystemIdB>());
}

// The scene graph system the debug drawing and the render id registry belong to.
ifaces::cTSSGSystem *SgSystem()
{
    return As<ifaces::cTSSGSystem>(CachedServiceGetter<kSgSystemCache, kSgSystemIdA, kSgSystemIdB>());
}

ifaces::cITSGameStateController *StateController() { return As<ifaces::cITSGameStateController>(GetCachedSlot23()); }

uint32_t GroupOf(void *persistFace) { return As<ifaces::cIGZPersistResource>(persistFace)->GetGroup(); }
} // namespace

// ---- 0x476e80, RemoveLotReferences (slot 21), DoMessage (message face, slot 3) ----

uint8_t __cdecl FlagLotTokens(ifaces::cITSInventory *inventory, uint32_t guid, int32_t lot, int32_t propertyIndex)
{
    using TokenCursor = ifaces::cITSInventoryToken **;
    uint8_t flagged = 0;
    // The first cursor lives in the stack slot of the inventory argument in the original: a find that writes nothing leaves it.
    TokenCursor cursor = reinterpret_cast<TokenCursor>(inventory);
    TokenCursor end = nullptr;
    TokenCursor scratch = nullptr;
    // The property count is a 16-bit result that is sign-extended, then compared unsigned with the index plus one.
    const uint32_t needed = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(propertyIndex)) + 1);
    inventory->FindToken(&cursor, guid, 0, 0, -1, 0, 0);
    TokenCursor *endRef = static_cast<TokenCursor *>(inventory->TokensEnd(&end));
    while (cursor != *endRef) {
        ifaces::cITSInventoryToken *token = *cursor;
        if (static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(token->GetPropertyCount()))) >= needed &&
            static_cast<uint16_t>(lot) == token->GetProperty(propertyIndex)) {
            token->SetFlags(kTokenFlagMarked);
            flagged = 1;
        }
        TokenCursor begin = *static_cast<TokenCursor *>(inventory->TokensBegin(&scratch));
        const int32_t next = static_cast<int32_t>(cursor - begin) + 1;
        cursor = *static_cast<TokenCursor *>(inventory->FindToken(&scratch, guid, next, 0, -1, 0, 0));
        endRef = static_cast<TokenCursor *>(inventory->TokensEnd(&end));
    }
    return flagged;
}

uint8_t cTSNeighborhood::RemoveLotReferences(int32_t lot)
{
    auto found = mLots.lower_bound(lot);
    if (found == mLots.end() || lot < found->first) return 0;
    if (auto *record = As<ifaces::cITSLotInfo>(found->second.Get())) {
        if (record->GetLotType() == kLotTypeWithSimInventories) {
            NeighborhoodMemberMap::iterator it;
            NeighborhoodMemberMap::iterator end;
            MemberMap3Begin(reinterpret_cast<void **>(&it)); // the sim inventories (slots 48 / 49)
            MemberMap3End(reinterpret_cast<void **>(&end));
            for (; it != end; ++it) {
                if (auto *inventory = As<ifaces::cITSInventory>(it->second.Get())) {
                    const uint8_t first = FlagLotTokens(inventory, kTokenGuidA, lot, 0);
                    const uint8_t second = FlagLotTokens(inventory, kTokenGuidB, lot, 0);
                    // Both run (a bitwise or): either flagged something.
                    if (second | first) inventory->PurgeMarkedTokens();
                }
            }
        }
        ifaces::cITSPersistContext *context = StateController()->GetPersistContext(kPersistContextBusiness);
        BusinessKey key = LotBusinessKey(record);
        context->DeleteResource(&key);
    }
    return 1;
}

bool cTSNeighborhood::DoMessage(void *message)
{
    cTSNeighborhood *nb = this;
    bool handled = false;
    Ref held;
    if (As<cIGZUnknown>(message)->QueryInterface(kMessage2StandardInterface, &held.object)) {
        auto *standard = As<ifaces::cIGZMessage2Standard>(held.object);
        const uint32_t type = standard->GetType();
        if (type == kMessageFamilyLotsFreed) {
            if (standard->HasData1()) {
                const uint32_t family = standard->GetData1();
                for (auto &entry : nb->mLots) {
                    auto *lot = As<ifaces::cITSLotInfo>(entry.second.Get());
                    if (lot->GetOwnerFamily() == family) {
                        lot->SetOwnerFamily(0);
                        lot->GetBaseLotInfo()->ClearFlags(kLotFlagOwned);
                        // ORIGINAL-BUG: a neighbourhood without group data (mSecondOwner null) crashes here when a lot is owned
                        // by the family. Reproduced.
                        As<ifaces::cITSNeighborhoodGroupData>(nb->mSecondOwner)->MarkDirty(GroupOf(nb->PersistFace()));
                    }
                }
                handled = true;
            }
        } else if (type == kMessageDropFamilyInventory) {
            const int16_t key = static_cast<int16_t>(static_cast<uint16_t>(standard->GetData1()));
            auto found = nb->mMembers2.find(key);
            if (found != nb->mMembers2.end()) nb->mMembers2.erase(found);
            handled = true;
        } else if (type == kMessageLotRemoved) {
            if (standard->HasData1()) {
                RemoveLotReferences(static_cast<int32_t>(standard->GetData1()));
                handled = true;
            }
        }
    }
    return handled;
}

// ---- the lot name index ----

ifaces::cITSLotInfo *cTSNeighborhood::FindLotByName(const cIGZString *name)
{
    cRZString text{GameAddress<void>(cRZString::kVTable), std::string(), 0};
    text.Construct(*name);
    if (text.StartsWith(GameAddress<const char>(kLotNumberPrefix), 1, true)) {
        text.Assign(*text.Erase(0, 1)->AsInterface());
        int32_t number = 0;
        if (std::sscanf(text.Data(), GameAddress<const char>(kLotNumberFormat), &number) == 1)
            return static_cast<ifaces::cITSLotInfo *>(GetLot(number));
    }
    cRZString key{GameAddress<void>(cRZString::kVTable), std::string(), 0};
    key.Construct(*name);
    auto found = mTexts.lower_bound(key);
    if (found != mTexts.end() && !(_stricmp(key.Data(), found->mText.Data()) < 0))
        return static_cast<ifaces::cITSLotInfo *>(found->mHeld);
    return nullptr;
}

uint8_t cTSNeighborhood::RenameLot(ifaces::cITSLotInfo *lot, const cIGZString *name)
{
    if (!lot) return 1;
    auto *oldName = static_cast<cIGZString *>(lot->GetBaseLotInfo()->GetName());
    cRZString old{GameAddress<void>(cRZString::kVTable), std::string(), 0};
    old.Construct(*oldName);
    bool inserted;
    {
        NeighborhoodTextEntry entry(*name, lot);
        inserted = mTexts.insert(entry).second;
    }
    if (inserted) {
        lot->GetBaseLotInfo()->SetName(const_cast<cIGZString *>(name));
        auto stale = mTexts.find(old);
        if (stale != mTexts.end()) mTexts.erase(stale);
    }
    return inserted;
}

int32_t cTSNeighborhood::NextFreeLotNumber()
{
    int32_t number = static_cast<int32_t>(mField6C);
    auto *prefix = static_cast<cIGZString *>(StateController()->GetNeighborhoodInfo()->GetFilePrefix());
    cRZString name{GameAddress<void>(cRZString::kVTable), std::string(), 0};
    name.Construct(*prefix);
    ifaces::cITSPersistSystem *persist = PersistSystem();
    cRZString directory{GameAddress<void>(cRZString::kVTable), std::string(), 0};
    // The path of the lot files' directory, from the name (an original function that only builds strings).
    OriginalFunction<void(__cdecl *)(cRZString *, cRZString *, uint32_t, uint32_t), 0xc0b232>()(&name, &directory, 1, 0xffffffffu);
    cRZString file{GameAddress<void>(cRZString::kVTable), std::string(), 0};
    do {
        file.Sprintf(GameAddress<const char>(kLotFileFormat), name.Data(), number);
        if (!persist->HasSegmentForFile(&directory, &file, 0)) break;
        ++number;
    } while (number != 0);
    // ORIGINAL-BUG: when every number up to the wrap is taken, the loop leaves with number 0 (a lot number that means "none").
    mField6C = static_cast<uint32_t>(number) + 1;
    return number;
}

// ---- lot inventories and the render ids ----

ifaces::cITSInventory *cTSNeighborhood::GetLotInventory(int32_t lot)
{
    const int16_t key = static_cast<int16_t>(lot);
    auto found = mMembers1.find(key);
    if (found == mMembers1.end()) {
        if (key == 0) {
            if (mDelegate) return As<ifaces::cITSNeighborhood>(mDelegate)->GetLotInventory(0);
            Ref created;
            auto *framework = As<ifaces::ComFramework>(GetDirectorFrameworkObject());
            framework->GetClassObject(kHelperClass, kHelperInterface, &created.object);
            mMembers1[key].Assign(created.object);
            if (created.object) {
                As<ifaces::cITSInventory>(created.object)->SetOwner(kLotInventoryOwnerKind, static_cast<uint32_t>(lot));
                As<ifaces::cITSInventory>(created.object)->SetOwnerGroup(GroupOf(PersistFace()));
            }
            return As<ifaces::cITSInventory>(created.object);
        }
        // A lot of another neighbourhood: its group data knows the inventory.
        if (mLots.find(static_cast<int32_t>(key)) == mLots.end() && mSecondOwner)
            return As<ifaces::cITSNeighborhoodGroupData>(mSecondOwner)->GetLotInventory(static_cast<int32_t>(key));
        Ref created;
        auto *framework = As<ifaces::ComFramework>(GetDirectorFrameworkObject());
        framework->GetClassObject(kHelperClass, kHelperInterface, &created.object);
        mMembers1[key].Assign(created.object);
        if (created.object) {
            As<ifaces::cITSInventory>(created.object)->SetOwner(kLotInventoryOwnerKind, static_cast<uint32_t>(lot));
            As<ifaces::cITSInventory>(created.object)->SetOwnerGroup(GroupOf(PersistFace()));
        }
    }
    return As<ifaces::cITSInventory>(mMembers1[key].Get());
}

void cTSNeighborhood::UnregisterRenderIds()
{
    if (ifaces::SGDebugDraw *debugDraw = SgSystem()->GetDebugDraw()) debugDraw->Clear();
    if (ifaces::cTSSGSystem *sg = SgSystem()) {
        if (sg->GetRenderIdRegistry()) {
            for (auto &entry : mPlain) SgSystem()->GetRenderIdRegistry()->Unregister(entry.second);
        }
    }
    mPlain.clear();
}

// ---- adapters (the RVA hooks; src/hook/tables/cTSNeighborhood.cpp): next to the bodies they inline ----

using Adapters = cTSNeighborhoodAdapters;

uint8_t __fastcall Adapters::RemoveLotReferences(Self *self, void *, int32_t lot)
{
    [[msvc::forceinline_calls]] return self->Self::RemoveLotReferences(lot);
}

bool __fastcall Adapters::DoMessage(cIGZMessageTarget2 *face, void *, void *message)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::DoMessage(message);
}

ifaces::cITSLotInfo * __fastcall Adapters::FindLotByName(Self *self, void *, const cIGZString *name)
{
    [[msvc::forceinline_calls]] return self->Self::FindLotByName(name);
}

uint8_t __fastcall Adapters::RenameLot(Self *self, void *, ifaces::cITSLotInfo *lot, const cIGZString *name)
{
    [[msvc::forceinline_calls]] return self->Self::RenameLot(lot, name);
}

int32_t __fastcall Adapters::NextFreeLotNumber(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::NextFreeLotNumber();
}

ifaces::cITSInventory * __fastcall Adapters::GetLotInventory(Self *self, void *, int32_t lot)
{
    [[msvc::forceinline_calls]] return self->Self::GetLotInventory(lot);
}

void __fastcall Adapters::UnregisterRenderIds(Self *self, void *)
{
    [[msvc::forceinline_calls]] self->Self::UnregisterRenderIds();
}
