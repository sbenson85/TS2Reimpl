// cTSNeighborhood::ValidateInventories (slot 35; lane CQ): drops the inventories of lots, families and sims that are gone, and
// has the others validate their tokens against the object ids that are known.
#include "game/game.h"
#include "game/original.h"
#include "gz/cGZDBSegmentPackedFile.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/NbrKindInfo.h"
#include "ifaces/NbrOwnerInfo.h"
#include "ifaces/NbrPersonData.h"
#include "ifaces/cIGZPersistResource.h"
#include "ifaces/cITSFamily.h"
#include "ifaces/cITSFamilyManager.h"
#include "ifaces/cITSGlobals.h"
#include "ifaces/cITSInventory.h"
#include "ifaces/cITSLotInfo.h"
#include "ifaces/cITSNeighbor.h"
#include "ifaces/cITSNeighborManager.h"
#include "ifaces/cITSNeighborhood.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StopwatchMore.h"
#include "rz/cRZStopwatch.h"
#include "simantics/ServiceRefVector.h"
#include "ts/cTSNeighborhood.h"
#include "ts/cTSNeighborhoodInternal.h"

#include <functional>
#include <set>

namespace {
using namespace nbhood;

// A set of object ids whose nodes come from the allocator service, as the inventories' token validation fills them.
using KnownIds = std::set<uint32_t, std::less<uint32_t>, GZServiceAllocator<uint32_t>>;

constexpr int32_t kStopwatchMode = 5;
constexpr uint32_t kKindGroupInfo = 9;       // GetKind argument; the answer 0x10 marks a neighbour whose inventory is dropped
constexpr uint32_t kKindToDrop = 0x10;

// The inventories take the three counters of a validation as out-values (zeroed before each call).
void Validate(ifaces::cITSInventory *inventory, KnownIds *known)
{
    int32_t countA = 0, countB = 0, countC = 0;
    inventory->ValidateTokens(known, &countA, &countB, &countC);
}

// 0x4eaa70 (cdecl, an original function): removes from the group's resources the objects that are not in the set.
void RemoveUnknown(KnownIds *known, uint32_t group)
{
    OriginalFunction<int32_t(__cdecl *)(void *, uint32_t, uint32_t), 0x4eaa70>()(known, group, 0);
}
} // namespace

void cTSNeighborhood::ValidateInventories()
{
    cRZStopwatch stopwatch;
    stopwatch.Construct(kStopwatchMode);
    stopwatch.Start();
    if (mDelegate) As<ifaces::cITSNeighborhood>(mDelegate)->ValidateInventories();
    auto *app = As<ifaces::cITSGlobals>(GetCachedSingleton());
    if (!app) return;
    ifaces::cITSFamilyManager *families = app->GetFamilyManager();
    ifaces::cITSNeighborManager *neighbors = app->GetNeighborManager();
    const uint32_t neighborGroup = neighbors ? neighbors->AsPersistResource()->GetGroup() : 0;
    const uint32_t helperGroup = mDelegate ? As<ifaces::cITSNeighborhood>(mDelegate)->AsPersistResource()->GetGroup()
                                           : GroupOf(PersistFace());
    KnownIds knownOther, knownHelper, knownNeighbor;   // for the lot inventories of another group, the helper group, the neighbours
    if (app->GetGlobalInventory() != nullptr) {
        ifaces::cITSInventory *global = app->GetGlobalInventory();
        if (global) As<cIGZUnknown>(global)->AddRef();
        Validate(global, &knownNeighbor);
        if (global) As<cIGZUnknown>(global)->Release();
    }
    EnsureStateHelper(this);
    if (mStateHelper) Validate(As<ifaces::cITSInventory>(mStateHelper), &knownHelper);
    if (mByteC8) {
        // The lot inventories whose lot is gone are dropped (the walk then starts again from the first).
        auto member = mMembers1.begin();
        while (member != mMembers1.end()) {
            const int32_t key = member->first;
            auto lot = mLots.lower_bound(key);
            if (lot != mLots.end() && !(key < lot->first)) {
                if (auto *inventory = As<ifaces::cITSInventory>(member->second.Get())) {
                    inventory->SetOwnerGroup(GroupOf(PersistFace()));
                    Validate(inventory, GroupOf(PersistFace()) == helperGroup ? &knownHelper : &knownOther);
                }
                ++member;
            } else {
                mMembers1.erase(member);
                member = mMembers1.begin();
            }
        }
        // Every lot with a number gets its inventory (made if missing).
        for (auto &entry : mLots) {
            auto *lot = As<ifaces::cITSLotInfo>(entry.second.Get());
            if (lot->GetLotNumber() != 0) GetLotInventory(lot->GetLotNumber());
        }
    }
    if (!mDelegate) {
        auto family = mMembers2.begin();
        while (family != mMembers2.end()) {
            if (families->GetFamily(static_cast<uint32_t>(static_cast<int32_t>(family->first))) == nullptr) {
                mMembers2.erase(family);
                family = mMembers2.begin();
            } else {
                if (auto *inventory = As<ifaces::cITSInventory>(family->second.Get())) {
                    inventory->SetOwnerGroup(helperGroup);
                    Validate(inventory, &knownHelper);
                }
                ++family;
            }
        }
        ServiceBlockVector ids = {nullptr, nullptr, nullptr};   // 16-bit family ids
        families->GetFamilyIds(&ids);
        for (auto *id = static_cast<int16_t *>(ids.mFirst); id != static_cast<int16_t *>(ids.mLast); ++id) {
            if (ifaces::cITSFamily *found = families->GetFamily(static_cast<uint32_t>(static_cast<int32_t>(*id))))
                GetOrCreateMember2(found->GetFamilyId()); // the family inventory (slot 38)
        }
        auto sim = mMembers3.begin();
        while (sim != mMembers3.end()) {
            const int32_t key = sim->first;
            ifaces::cITSNeighbor *neighbor = neighbors->GetNeighbor(key, 1);
            const bool stillThere = neighbors->GetNeighbor(key, 1) != nullptr;   // asked twice, as the original does
            bool drop = !stillThere;
            if (stillThere && neighbor && neighbor->GetPersonData()) {
                if (neighbor->GetPersonData()->GetKindInfo() &&
                    neighbor->GetPersonData()->GetKindInfo()->GetKind(kKindGroupInfo) == kKindToDrop)
                    drop = true;
            }
            if (drop) {
                mMembers3.erase(sim);
                sim = mMembers3.begin();
                continue;
            }
            auto *inventory = As<ifaces::cITSInventory>(sim->second.Get());
            if (neighbor && inventory) {
                uint32_t owner = 0;
                if (neighbor->GetPersonData() && neighbor->GetPersonData()->GetOwnerInfo()) {
                    owner = neighbor->GetPersonData()->GetOwnerInfo()->GetValue();
                    inventory->SetOwnerGroup(owner);
                }
                KnownIds known;
                Validate(inventory, &known);
                RemoveUnknown(&known, owner);
            }
            ++sim;
        }
        neighbors->EnsureSimInventories();
        if (ids.mFirst) ids.FreeBlock();
    }
    if (!knownOther.empty()) RemoveUnknown(&knownOther, GroupOf(PersistFace()));
    if (!knownHelper.empty()) RemoveUnknown(&knownHelper, helperGroup);
    if (!knownNeighbor.empty()) RemoveUnknown(&knownNeighbor, neighborGroup);
    float elapsed = 0.0f;
    stopwatch_more::ElapsedAsFloat(&stopwatch, nullptr, &elapsed);   // the answer is not used
}

// ---- adapters (the RVA hooks; src/hook/tables/cTSNeighborhood.cpp): next to the bodies they inline ----

using Adapters = cTSNeighborhoodAdapters;

void __fastcall Adapters::ValidateInventories(Self *self, void *)
{
    [[msvc::forceinline_calls]] self->Self::ValidateInventories();
}
