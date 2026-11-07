#include "simantics/EdithObjectTestSim.h"
#include "simantics/FindBestAction.h"
#include "simantics/PrimPerson.h"
#include "simantics/ServiceRefVector.h"

#include "ifaces/ObjectModule.h"
#include "ifaces/cITSPerson.h"
#include "ifaces/cTSObject.h"

// The gather step of the person's FindBestAction (GatherInteractions, 0x5155b0). Lane AH.

namespace {

// One interaction group of the object module (a node of the std::list the module keeps per object function): a stack object and
// the indexes of its interaction table entries that apply.
struct InteractionGroupNode {
    InteractionGroupNode *mNext;               // +0x00
    InteractionGroupNode *mPrev;               // +0x04
    ifaces::cTSObject *mStackObject;           // +0x08
    std::vector<uint32_t> mEntryIndexes;       // +0x0c
};
static_assert(sizeof(InteractionGroupNode) == 0x18, "InteractionGroupNode layout");

// A stretch [mFirst, mLast) of the module's group list.
struct InteractionGroupRange {
    InteractionGroupNode *mFirst;
    InteractionGroupNode *mLast;
};

// The vector of group ranges the module fills (storage from the allocator service), freed when the gather ends (0x195720).
struct GroupRanges : ServiceBlockVector {
    GroupRanges() : ServiceBlockVector{nullptr, nullptr, nullptr} {}
    GroupRanges(const GroupRanges &) = delete;
    ~GroupRanges() { FreeBlock(); }
};

// Stack object flags and attributes the candidates are tested with.
constexpr uint32_t kObjectFlagHidden = 0x10000;
constexpr uint32_t kObjectAttributeState = 0x38;
constexpr uint32_t kObjectAttributeStateBlocked = 5;
// The first argument of the search's tree frame is the object the search is limited to.
constexpr uint32_t kArgumentTargetObject = 0;

} // namespace

void PrimPersonObject::GatherInteractions()
{
    ifaces::cITSPerson *const person = As<ifaces::cITSPerson>(&mPersonVTable);

    // Unless the search is limited to one object: the ids of the objects the Sim may use, which the module's candidates are
    // filtered by.
    OwnerIdSet allowedOwnerIds;
    bool filterByOwnerIds = false;
    if (mFlag81c == 0) {
        const int16_t ownerId = As<ifaces::cTSObject>(this)->GetOwnerId();
        filterByOwnerIds = person->FillAllowedOwnerIds(&allowedOwnerIds, ownerId) != 0;
    }
    const int16_t targetObjectId = As<ifaces::cTSObject>(this)->GetArgument(kArgumentTargetObject);

    TestSimScope sim;
    sim->SetPerson(&mPersonVTable, 1, 0);

    // The interaction groups of the objects that offer the wanted function to a Sim of this life stage.
    GroupRanges ranges;
    ifaces::ObjectModule *const objectModule = As<ifaces::cTSObject>(this)->GetObjectModule();
    objectModule->GetInteractionGroups(mResult830, person->GetLifeStage(), &ranges);

    const InteractionGroupRange *const rangesEnd = static_cast<const InteractionGroupRange *>(ranges.mLast);
    for (const InteractionGroupRange *range = static_cast<const InteractionGroupRange *>(ranges.mFirst); range != rangesEnd; ++range) {
        for (InteractionGroupNode *group = range->mFirst; group != range->mLast; group = group->mNext) {
            // The group's stack object, looked up again in the object table by its number.
            const int32_t objectNumber = static_cast<int16_t>(group->mStackObject->GetObjectId());
            ifaces::cTSObject *const candidate = static_cast<ifaces::cTSObject *>(mObjectRegistry->Find(objectNumber));
            if (!candidate) continue;
            ifaces::cTSObject *const stackObject = candidate->GetResolvedObject();
            if (!stackObject) continue;
            if (stackObject->IsAvailable() == 0) continue;
            if (stackObject->TestFlags(kObjectFlagHidden) != 0) continue;
            if (stackObject->IsBusy(1) != 0) continue;
            if ((stackObject->GetAttribute(kObjectAttributeState) & kObjectAttributeStateBlocked) != 0) continue;

            bool wanted;
            if (mFlag81c == 0) {
                wanted = true;
                if (filterByOwnerIds) {
                    const int32_t ownerId = static_cast<int16_t>(stackObject->GetOwnerId());
                    wanted = allowedOwnerIds.find(static_cast<uint32_t>(ownerId)) != allowedOwnerIds.end();
                }
            } else if (stackObject->GetOwnerId() == static_cast<uint16_t>(targetObjectId)) {
                wanted = true;
            } else {
                // (The object was available a moment ago: this takes the other candidates only if the answer changed.)
                wanted = mFlag81d != 0 && stackObject->IsAvailable() == 0;
            }
            if (!wanted) continue;

            sim->SetObjectKey(stackObject->GetObjectKey());
            sim->mEntryIndexes = &group->mEntryIndexes;
            sim->AppendInteractions(&mPending, mResult830, 0);
        }
    }
}
