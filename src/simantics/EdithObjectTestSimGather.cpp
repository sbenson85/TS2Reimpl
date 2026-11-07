#include "simantics/EdithObjectTestSim.h"

#include "game/game.h"
#include "game/vtable.h"
#include "ifaces/ComFramework.h"
#include "ifaces/InteractionTable.h"
#include "ifaces/InteractionTableEntry.h"
#include "ifaces/SimServiceA.h"
#include "ifaces/SimServiceC.h"
#include "ifaces/cITSPerson.h"
#include "ifaces/cTSInteraction.h"
#include "ifaces/cTSObject.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/StaticGetters2.h"
#include "simantics/FindBestAction.h"
#include "simantics/PrimPerson.h"
#include "simantics/PrimPersonCommon.h"
#include "simantics/ServiceRefVector.h"

#include <cstring>

// The gather side of the interaction test sim: the interactions a Sim may start on a stack object, collected for the autonomy
// search (PrimPersonObject::GatherInteractions) and for the pie menu. Lane AH.

namespace {

// A new cTSInteraction is made by the director's framework with these ids.
constexpr uint32_t kInteractionClassId = 0xfc5ecc3d;
constexpr uint32_t kInteractionInterfaceId = 0x5aca6692;
// The interface id under which a stack object that is a Sim gives out its cITSPerson.
constexpr uint32_t kPersonInterfaceId = 0x5a69d3d5;
// cTSObject::ObjectKind() of a Sim.
constexpr int32_t kObjectKindPerson = 2;
// The source kind of the interactions of a menu (and of the cheat interactions).
constexpr uint32_t kSourceKindMenu = 0x32;
// The Person Data field that holds the autonomy level the table entries' thresholds are compared with.
constexpr uint32_t kPersonDataAutonomyLevel = 0x24;
constexpr uint32_t kPersonDataFlag = 1;
// The stack object flag that allows the debug cheat interactions in its menu.
constexpr uint32_t kObjectFlagDebugMenu = 0x2000;
// The call mode of the check tree run a listed interaction goes through.
constexpr uint32_t kListCheckCallMode = 2;
// The priority below which a pending interaction lets a new menu interaction through (0x32 is also the menu's source kind).
constexpr int32_t kPendingPriorityLimit = 0x32;
constexpr int32_t kPendingGroupedLimit = 7;
constexpr int32_t kActionQueueLimit = 5;
constexpr int32_t kNoEntry = -2; // the entry index of a cheat interaction
constexpr uint32_t kFunctionCount = 16;
// The interaction text ids of the cheat interactions; the first has two spellings.
constexpr uint32_t kTextDumpHappyLogPlain = 0x209d;
constexpr uint32_t kTextDumpHappyLogTracked = 0x2089;
constexpr uint32_t kTextForceError = 0x2da;
// The globals the menu gather publishes itself and its list in while it runs (0x4526a0 reads them), and the UI manager global.
constexpr uint32_t kGlobalMenuSim = 0x10da960;
constexpr uint32_t kGlobalMenuList = 0x10da964;

ifaces::cTSObject *StackObjectOf(const cEdithObjectTestSim *sim)
{
    return reinterpret_cast<ifaces::cTSObject *>(static_cast<uintptr_t>(sim->mObjectKey));
}

// Makes a new interaction (counted once, the holder releases it). The holder is empty.
void CreateInteraction(PHeld &holder)
{
    holder.mArmed = true;
    ifaces::ComFramework *const framework = As<ifaces::ComFramework>(GetDirectorFrameworkObject());
    framework->GetClassObject(kInteractionClassId, kInteractionInterfaceId, &holder.mObject);
}

} // namespace

void cEdithObjectTestSim::AppendInteractions(ServiceRefVector *list, uint32_t functionMask, uint32_t stopAtFirst)
{
    if (mFlagA != 0) {
        AppendAutonomousInteractions(list, functionMask);
    } else {
        AppendInteractionsForMenu(list, stopAtFirst);
    }
}

void cEdithObjectTestSim::AppendAutonomousInteractions(ServiceRefVector *list, uint32_t functionMask)
{
    ifaces::cTSObject *const stackObject = StackObjectOf(this);
    ifaces::InteractionTable *const table = stackObject->GetInteractionTable(1);
    if (!table) return;

    ifaces::cITSPerson *const person = As<ifaces::cITSPerson>(mPerson);
    int32_t remaining = static_cast<int32_t>(mEntryIndexes->size());
    const uint32_t autonomyLevel = person->GetPersonData(kPersonDataAutonomyLevel, kPersonDataFlag) & 0xffffu;

    // The entries are walked from the last index to the first.
    while (--remaining >= 0) {
        ifaces::InteractionTableEntry *const entry = table->GetEntryAt(static_cast<int32_t>((*mEntryIndexes)[remaining]));
        if (!entry) {
            // ORIGINAL-BUG: an index the table has no entry for only has the table's name fetched (into a string nothing reads
            // any more), and the code that follows uses the missing entry: the original crashes there.
            EmbeddedString tableName;
            table->GetName(&tableName);
        }
        const uint32_t ageMask = person->GetAgeMask();
        if ((ageMask & entry->GetAllowedAgeMask()) == 0) continue;
        if (entry->IsDebugOnly() != 0) continue;
        if (static_cast<int16_t>(autonomyLevel) < entry->GetAutonomyThreshold()) continue;

        PHeld created;
        CreateInteraction(created);
        ifaces::cTSInteraction *const interaction = As<ifaces::cTSInteraction>(created.mObject);
        interaction->SetUp(mPerson, stackObject, entry->GetEntryIndex(), 0, 0);
        interaction->SetFlag400(1);
        if (interaction->GetMenuStringId() > 0) list->PushBack(created.mObject);
        created.ReleaseNow();
    }

    // For a stack object that is a Sim: the interaction its Sim's current interaction (a Sim-only table entry) implies.
    if (stackObject->ObjectKind() != kObjectKindPerson) return;
    PHeld otherPerson;
    otherPerson.mArmed = true;
    stackObject->QueryInterface(kPersonInterfaceId, &otherPerson.mObject); // (the answer is not looked at)
    ifaces::cITSPerson *const other = As<ifaces::cITSPerson>(otherPerson.mObject);
    ifaces::InteractionTableEntry *const otherEntry = other->GetCurrentInteraction()->GetTableEntry();
    if (otherEntry) {
        bool provided = functionMask == 0;
        for (uint32_t function = 0, bit = 1; !provided && function < kFunctionCount; ++function, bit <<= 1) {
            if ((functionMask & bit) != 0) provided = otherEntry->ProvidesFunction(function, person->GetLifeStage()) != 0;
        }
        if (provided && otherEntry->IsDebugOnly() == 0 && otherEntry->IsSimOnly() != 0) {
            PHeld created;
            CreateInteraction(created);
            ifaces::cTSInteraction *const interaction = As<ifaces::cTSInteraction>(created.mObject);
            interaction->SetUpBetween(mPerson, other);
            interaction->SetBranchFlag(1);
            interaction->SetFlag400(1);
            if (interaction->GetStackObject() != nullptr) list->PushBack(created.mObject);
            created.ReleaseNow();
        }
    }
    otherPerson.ReleaseNow();
}

void cEdithObjectTestSim::AppendInteractionsForMenu(ServiceRefVector *list, uint32_t stopAtFirst)
{
    ifaces::cTSObject *const stackObject = StackObjectOf(this);
    ifaces::InteractionTable *const table = stackObject->GetInteractionTable(1);
    ifaces::InteractionTableEntry *const recorder = As<ifaces::SimServiceC>(GetCachedSingletonC())->GetCheckRecorder();
    *GameAddress<void *>(kGlobalMenuSim) = this;
    *GameAddress<void *>(kGlobalMenuList) = list;
    ifaces::cITSPerson *const person = As<ifaces::cITSPerson>(mPerson);

    // (The original keeps an empty reference vector in its frame here and destroys it with 0x38f140 on the way out; nothing
    // is ever put into it.)
    if (table) {
        // How many of the Sim's pending interactions belong to a group.
        int32_t groupedPending = 0;
        const int32_t pendingCount = person->GetPendingCount();
        if (pendingCount != 0) {
            uint32_t index = 0;
            do {
                if (person->GetPendingAt(index)->HasGroup() != 0) ++groupedPending;
                ++index;
            } while (index < static_cast<uint32_t>(pendingCount));
        }

        const int32_t entryCount = table->GetEntryCount();
        for (int32_t position = 0; position < entryCount; ++position) {
            ifaces::InteractionTableEntry *const entry = table->GetEntryAt(position);
            PHeld created;
            CreateInteraction(created);
            ifaces::cTSInteraction *const interaction = As<ifaces::cTSInteraction>(created.mObject);
            interaction->SetUp(mPerson, stackObject, entry->GetEntryIndex(), kSourceKindMenu, 0);
            if (interaction->IsInterrupt() == 0) {
                // A plain interaction: while something pending is below the priority limit, or few pending interactions have a
                // group.
                if (person->HasPendingBelowPriority(kPendingPriorityLimit) != 0 || groupedPending < kPendingGroupedLimit) {
                    AppendIfPasses(list, created.mObject, recorder, 0);
                }
            } else if (person->GetCurrentInteraction()->AllowsInterruption() != 0) {
                // An interrupt: only when the current interaction allows it and not many actions are queued.
                if (person->GetActionQueueCount() < kActionQueueLimit) AppendIfPasses(list, created.mObject, recorder, 0);
            }
            if ((stopAtFirst & 0xffu) != 0 && list->mFirst != list->mLast) {
                *GameAddress<void *>(kGlobalMenuSim) = nullptr;
                created.ReleaseNow();
                return;
            }
            created.ReleaseNow();
        }
    }

    // The debug cheat interactions.
    if (As<ifaces::SimServiceA>(GetCachedSingleton())->AreDebugInteractionsEnabled() != 0 && stackObject->TestFlags(kObjectFlagDebugMenu) != 0 &&
        mFlagA == 0) {
        if (stackObject->ObjectKind() == kObjectKindPerson) {
            PHeld personInterface;
            personInterface.mArmed = true;
            stackObject->QueryInterface(kPersonInterfaceId, &personInterface.mObject);
            uint32_t text = kTextDumpHappyLogPlain;
            if (personInterface.mObject && As<ifaces::cITSPerson>(personInterface.mObject)->UsesTableAds() != 0) text = kTextDumpHappyLogTracked;
            AddCheatInteraction(list, mPerson, stackObject, kNoEntry, kSourceKindMenu, "Dump Happy Log", text);
            personInterface.ReleaseNow();
        }
        AddCheatInteraction(list, mPerson, stackObject, kNoEntry, kSourceKindMenu, "Force Error", kTextForceError);
    }

    if (table) {
        // Interactions between this Sim and a Sim stack object. First the ones the stack object's Sim starts on this Sim, when
        // this Sim's current interaction is Sim-only.
        if (mPerson != nullptr) {
            // ORIGINAL-BUG: a Sim without a current interaction is dereferenced (here and for the other Sim below).
            ifaces::InteractionTableEntry *const currentEntry = person->GetCurrentInteraction()->GetTableEntry();
            if (currentEntry != nullptr && currentEntry->IsSimOnly() != 0) {
                PHeld otherPerson;
                otherPerson.mArmed = true;
                if (stackObject != nullptr && stackObject->QueryInterface(kPersonInterfaceId, &otherPerson.mObject) != 0 &&
                    otherPerson.mObject != mPerson) {
                    PHeld created;
                    CreateInteraction(created);
                    ifaces::cTSInteraction *const interaction = As<ifaces::cTSInteraction>(created.mObject);
                    interaction->SetUpBetween(otherPerson.mObject, mPerson);
                    if (interaction->GetStackObject() != nullptr) AppendIfPasses(list, created.mObject, recorder, 0);
                    created.ReleaseNow();
                }
                otherPerson.ReleaseNow();
            }
        }
        // Then the ones this Sim starts on the stack object's Sim, when that Sim's current interaction is Sim-only.
        if (stackObject->ObjectKind() == kObjectKindPerson) {
            PHeld otherPerson;
            otherPerson.mArmed = true;
            if (stackObject != nullptr && stackObject->QueryInterface(kPersonInterfaceId, &otherPerson.mObject) != 0 &&
                otherPerson.mObject != mPerson) {
                ifaces::InteractionTableEntry *const otherEntry =
                    As<ifaces::cITSPerson>(otherPerson.mObject)->GetCurrentInteraction()->GetTableEntry();
                if (otherEntry != nullptr && otherEntry->IsSimOnly() != 0) {
                    PHeld created;
                    CreateInteraction(created);
                    ifaces::cTSInteraction *const interaction = As<ifaces::cTSInteraction>(created.mObject);
                    interaction->SetUpBetween(mPerson, otherPerson.mObject);
                    if (interaction->GetStackObject() != nullptr) {
                        AppendIfPasses(list, created.mObject, recorder, static_cast<int16_t>(stackObject->GetObjectId()));
                    }
                    created.ReleaseNow();
                }
            }
            otherPerson.ReleaseNow();
        }
    }
    *GameAddress<void *>(kGlobalMenuSim) = nullptr;
}

void cEdithObjectTestSim::AppendIfPasses(ServiceRefVector *list, void *interactionPtr, void *recorder, uint32_t extraArgument)
{
    ifaces::cTSInteraction *const interaction = As<ifaces::cTSInteraction>(interactionPtr);
    SetUiManager(interactionPtr);
    const int32_t sizeBefore = static_cast<int32_t>(list->mLast - list->mFirst);
    RunCheckTree(interactionPtr, kListCheckCallMode, recorder, 1, extraArgument, 0);
    // The check tree may list the interaction itself; otherwise it is listed when it is not flag 0x10 and passed.
    if (sizeBefore == static_cast<int32_t>(list->mLast - list->mFirst) && interaction->IsFlag10Set() == 0 && interaction->HasPassedCheck() != 0) {
        HeldReference held(interactionPtr);
        list->PushBack(held.Get());
    }
    SetUiManager(nullptr);
}

void __cdecl AddCheatInteraction(ServiceRefVector *list, void *person, void *stackObject, int16_t entryIndex, uint32_t sourceKind,
                                 const char *name, uint32_t menuStringId)
{
    PHeld created;
    CreateInteraction(created);
    ifaces::cTSInteraction *const interaction = As<ifaces::cTSInteraction>(created.mObject);
    interaction->SetUp(person, stackObject, entryIndex, sourceKind, 0);
    {
        EmbeddedString text;
        text.mText.assign(name, std::strlen(name));
        interaction->SetName(&text);
    }
    interaction->SetMenuStringId(menuStringId);
    interaction->SetCheckPassed(1);
    list->PushBack(created.mObject);
    created.ReleaseNow();
}
