#include "simantics/PrimPerson.h"

#include "game/game.h"
#include "ifaces/AttributeTable.h"
#include "ifaces/InteractionTableEntry.h"
#include "ifaces/SimRowTable.h"
#include "ifaces/cITSPerson.h"
#include "ifaces/cTSInteraction.h"
#include "ifaces/cTSObject.h"
#include "ifaces/cTSTreeSim.h"
#include "simantics/PersonData.h"
#include "simantics/PrimPersonCommon.h"
#include "simantics/TreeHeld.h"

// The person's interaction bookkeeping: the queues of cTSInteraction objects (+0x594 pending, +0x5a8 action queue), the
// current / previous / selected interaction (+0x5bc, +0x5c0, +0x5c4) and the Person Data fields that mirror them for the
// SimAntics trees. The functions are members of the person (cTSPerson, `this` in ecx); their callers are the person
// primitives (IdleForInput, opcode 0x1e) and the interaction code.

namespace {

using Interaction = ifaces::cTSInteraction;

constexpr uint32_t kPersonDataFlag = 1; // the third argument of every Person Data write, and the second of every read
constexpr uint32_t kObjectEventRejected = 4;
constexpr uint32_t kUseCounterColumn = 1;
constexpr uint32_t kUseCounterLimit = 100;
constexpr uint32_t kUseCounterRowColumns = 2;
constexpr uint32_t kAttributeUsesCounter = 0x19;
constexpr uint32_t kUseTrackingInterface = 0x5a6216f6;
constexpr int kQueryInterfaceSlot = 0;
constexpr int32_t kObjectKindSim = 2;

// Reference holders (the game's cRZAutoRef): assignment adds the new reference, stores it, then releases the old one,
// and does nothing when both are the same object.
void AssignReference(void **holder, void *object)
{
    void *const old = *holder;
    if (object == old) return;
    if (object) VirtualCall<uint32_t(__thiscall *)(void *), 1>(object);
    *holder = object;
    if (old) VirtualCall<uint32_t(__thiscall *)(void *), 2>(old);
}

void ReleaseReference(void *object)
{
    if (object) VirtualCall<uint32_t(__thiscall *)(void *), 2>(object);
}

// The number of 12-byte elements between two vector pointers (a signed division, as the compiler's reciprocal multiply does).
uint16_t CountTwelveByteElements(const uint8_t *begin, const uint8_t *end)
{
    return static_cast<uint16_t>(static_cast<int32_t>(end - begin) / 12);
}

void ClearMotiveVector(PrimPersonObject &person)
{
    if (person.mMotiveBegin != person.mMotiveEnd) person.mMotiveEnd = person.mMotiveBegin;
}

void WriteStackObjectId(ifaces::cITSPerson *sub, uint32_t field, ifaces::cTSObject *stackObject)
{
    sub->SetPersonData(field, stackObject->GetObjectId(), kPersonDataFlag);
}

constexpr uint32_t kUiManagerGlobal = 0x10da968;

} // namespace

void __cdecl SetUiManager(void *manager)
{
    *GameAddress<void *>(kUiManagerGlobal) = manager;
}

void PrimPersonObject::ClearActionQueue()
{
    ifaces::cITSPerson *const sub = As<ifaces::cITSPerson>(&mPersonVTable);
    RefCountedDeque &queue = QueueB();

    // Every queued interaction forgets this person. An interaction may remove itself from the queue while it is told, so
    // the index only advances when the size did not change.
    uint32_t size = queue.Size();
    if (size != 0) {
        uint32_t index = 0;
        do {
            const uint32_t sizeBefore = size;
            void *const queued = *queue.At(index);
            As<Interaction>(queued)->SetOwnerPerson(&mPersonVTable);
            size = queue.Size();
            if (sizeBefore == size) ++index;
        } while (index < size);
    }

    queue.Clear();
    sub->SetPersonData(PersonData::kQueueSize, static_cast<uint16_t>(queue.Size()), kPersonDataFlag);

    void *const selected = mSelectedInteraction;
    if (selected) {
        mSelectedInteraction = nullptr;
        ReleaseReference(selected);
    }
    mReactionCountSelected = 0;
    sub->SetPersonData(PersonData::kQueueHeadInteractionId, 0, kPersonDataFlag);
    sub->SetPersonData(PersonData::kQueueHeadStackObjectId, 0, kPersonDataFlag);
    sub->SetPersonData(PersonData::kActiveInteractionId, 0, kPersonDataFlag);
    sub->SetPersonData(PersonData::kActiveStackObjectId, 0, kPersonDataFlag);
    sub->SetPersonData(PersonData::kSelectedStackObjectId, 0, kPersonDataFlag);
}

void PrimPersonObject::RejectInteraction(void *interaction)
{
    Interaction *const entry = As<Interaction>(interaction);
    ifaces::cTSObject *const stackObject = entry->GetStackObject();
    entry->SetOwnerPerson(&mPersonVTable);
    if (stackObject) {
        const uint16_t personObjectId = As<ifaces::cTSObject>(this)->GetObjectId();
        stackObject->NotifyObjectEvent(kObjectEventRejected, personObjectId, 0, 0, 0);
    }
}

void PrimPersonObject::PublishActionQueue()
{
    ifaces::cITSPerson *const sub = As<ifaces::cITSPerson>(&mPersonVTable);
    RefCountedDeque &queue = QueueB();

    sub->SetPersonData(PersonData::kQueueSize, static_cast<uint16_t>(queue.Size()), kPersonDataFlag);
    if (queue.Size() != 0) {
        TreeHeld<Interaction> head(As<Interaction>(queue.Front()));
        sub->SetPersonData(PersonData::kQueueHeadInteractionId, head->GetInteractionId(), kPersonDataFlag);
        // (The head's stack object is used without a null check, as in the original.)
        WriteStackObjectId(sub, PersonData::kQueueHeadStackObjectId, head->GetStackObject());
        head.Reset();
    } else {
        sub->SetPersonData(PersonData::kQueueHeadInteractionId, 0, kPersonDataFlag);
        sub->SetPersonData(PersonData::kQueueHeadStackObjectId, 0, kPersonDataFlag);
    }

    Interaction *const current = sub->GetCurrentInteraction();
    if (current) {
        // (No null check on the stack object here either.)
        WriteStackObjectId(sub, PersonData::kActiveStackObjectId, current->GetStackObject());
        sub->SetPersonData(PersonData::kActiveInteractionId, current->GetInteractionId(), kPersonDataFlag);
    } else {
        sub->SetPersonData(PersonData::kActiveStackObjectId, 0, kPersonDataFlag);
        sub->SetPersonData(PersonData::kActiveInteractionId, 0, kPersonDataFlag);
    }

    Interaction *const selected = sub->GetSelectedInteraction();
    Interaction *const reported = selected ? selected : current;
    if (reported) {
        sub->SetPersonData(PersonData::kLastInteractionId, reported->GetInteractionId(), kPersonDataFlag);
        WriteStackObjectId(sub, PersonData::kLastStackObjectId, reported->GetStackObject());
    } else {
        sub->SetPersonData(PersonData::kLastInteractionId, 0, kPersonDataFlag);
        sub->SetPersonData(PersonData::kLastStackObjectId, 0, kPersonDataFlag);
    }
}

void PrimPersonObject::PublishPendingQueue()
{
    ifaces::cITSPerson *const sub = As<ifaces::cITSPerson>(&mPersonVTable);
    RefCountedDeque &queue = QueueA();

    if (queue.Size() != 0) {
        // (The head is read without a null check and held while the Person Data is written, as the original does.)
        TreeHeld<Interaction> head(As<Interaction>(queue.Front()));
        sub->SetPersonData(PersonData::kPendingHeadInteractionId, head->GetInteractionId(), kPersonDataFlag);
        sub->SetPersonData(PersonData::kPendingHeadFlag, PCall<uint8_t>(head.get(), 33), kPersonDataFlag);
        ifaces::cTSObject *const stackObject = head->GetStackObject();
        if (stackObject) {
            WriteStackObjectId(sub, PersonData::kPendingHeadStackObjectId, head->GetStackObject());
        } else {
            sub->SetPersonData(PersonData::kPendingHeadStackObjectId, 0, kPersonDataFlag);
        }
        head.Reset();
    } else {
        sub->SetPersonData(PersonData::kPendingHeadInteractionId, 0, kPersonDataFlag);
        sub->SetPersonData(PersonData::kPendingHeadStackObjectId, 0, kPersonDataFlag);
        sub->SetPersonData(PersonData::kPendingHeadFlag, 0, kPersonDataFlag);
    }
    sub->SetPersonData(PersonData::kFlag6d, PCall<uint16_t>(mCurrentInteraction, 68), kPersonDataFlag);
}

void PrimPersonObject::SelectInteraction(void *interaction)
{
    ifaces::cITSPerson *const sub = As<ifaces::cITSPerson>(&mPersonVTable);
    AssignReference(&mSelectedInteraction, interaction);

    Interaction *const selected = As<Interaction>(mSelectedInteraction);
    if (selected->GetStackObject()) {
        sub->SetPersonData(PersonData::kSelectedStackObjectId, As<Interaction>(mSelectedInteraction)->GetStackObject()->GetObjectId(),
                           kPersonDataFlag);
        sub->SetPersonData(PersonData::kInteractionInitialized, 0, kPersonDataFlag);
        if (!As<Interaction>(mSelectedInteraction)->IsInitialized()) As<Interaction>(mSelectedInteraction)->Initialize();
    } else {
        sub->SetPersonData(PersonData::kSelectedStackObjectId, 0, kPersonDataFlag);
    }

    if (As<Interaction>(interaction)->GetTableEntry()->IsInitialized()) ClearMotiveVector(*this);

    if (sub->GetPersonData(PersonData::kFlag49, kPersonDataFlag) != 0) {
        sub->SetPersonData(PersonData::kFlag49, 0, kPersonDataFlag);
        PCall<void>(this, 221);
    }
    sub->SetPersonData(PersonData::kFlag47, 0, kPersonDataFlag);
    sub->SetPersonData(PersonData::kFlag6e, 0, kPersonDataFlag);
    sub->SetPersonData(PersonData::kFlag6d, PCall<uint16_t>(mSelectedInteraction, 68), kPersonDataFlag);
    PublishActionQueue();
    mReactionCountSelected = CountTwelveByteElements(mReactionBegin, mReactionEnd);
}

void PrimPersonObject::StartInteraction(void *interaction)
{
    ifaces::cITSPerson *const sub = As<ifaces::cITSPerson>(&mPersonVTable);

    if (As<Interaction>(mCurrentInteraction)->GetStackObject()) {
        // The interaction being replaced becomes the previous one and is told the tree sim's last branch.
        AssignReference(&mPreviousInteraction, mCurrentInteraction);
        const uint8_t lastBranch = As<ifaces::cTSTreeSim>(&mTreeSimVTable)->GetLastBranch();
        As<Interaction>(mPreviousInteraction)->SetBranchFlag(lastBranch);
    }
    AssignReference(&mCurrentInteraction, interaction);

    Interaction *const current = As<Interaction>(mCurrentInteraction);
    sub->SetPersonData(PersonData::kCurrentInteractionId, current->GetInteractionId(), kPersonDataFlag);
    sub->SetPersonData(PersonData::kCurrentIsSimOnly, current->GetTableEntry() && current->GetTableEntry()->IsSimOnly() ? 1 : 0,
                       kPersonDataFlag);

    if (current->GetStackObject()) {
        sub->SetPersonData(PersonData::kCurrentStackObjectId, current->GetStackObject()->GetObjectId(), kPersonDataFlag);
    } else {
        sub->SetPersonData(PersonData::kCurrentStackObjectId, 0, kPersonDataFlag);
    }
    if (current->GetStackObject()) {
        sub->SetPersonData(PersonData::kInteractionInitialized, 0, kPersonDataFlag);
        if (!current->IsInitialized()) current->Initialize();
    }
    sub->SetPersonData(PersonData::kPriority, current->GetPriority(), kPersonDataFlag);
    sub->SetPersonData(PersonData::kLastInteractionId, sub->GetPersonData(PersonData::kCurrentInteractionId, kPersonDataFlag),
                       kPersonDataFlag);
    sub->SetPersonData(PersonData::kLastStackObjectId, sub->GetPersonData(PersonData::kCurrentStackObjectId, kPersonDataFlag),
                       kPersonDataFlag);
    sub->SetPersonData(PersonData::kCurrentFlagA6, PCall<uint8_t>(current, 47), kPersonDataFlag);
    if (PCall<uint16_t>(current, 68) == 0) PCall<void>(current, 70, PCall<uint16_t>(current, 66));

    ClearMotiveVector(*this);
    if (sub->GetPersonData(PersonData::kFlag49, kPersonDataFlag) != 0) {
        sub->SetPersonData(PersonData::kFlag49, 0, kPersonDataFlag);
        PCall<void>(this, 221);
    }
    sub->SetPersonData(PersonData::kFlag47, 0, kPersonDataFlag);
    sub->SetPersonData(PersonData::kFlag6e, 0, kPersonDataFlag);
    ClearActionQueue();
    PublishPendingQueue();
    mReactionCountCurrent = CountTwelveByteElements(mReactionBegin, mReactionEnd);
}

void PrimPersonObject::CountInteractionUse(void *interaction)
{
    if (!interaction) return;
    Interaction *const entry = As<Interaction>(interaction);
    ifaces::InteractionTableEntry *const tableEntry = entry->GetTableEntry();
    if (!tableEntry) return;
    if (!tableEntry->IsUseTracked()) return;
    ifaces::cTSObject *const stackObject = entry->GetStackObject();
    if (!stackObject) return;
    if (stackObject->ObjectKind() == kObjectKindSim) return;

    // The counters live on the stack object; an object that forwards its primitives keeps them on the object behind its
    // use-tracking interface instead (reached through a chain of accessors nobody has named yet).
    ifaces::cTSObject *counterOwner = stackObject;
    ifaces::AttributeTable *attributes = stackObject->GetAttributeTable();
    if (stackObject->ForwardsPrimitives()) {
        TreeHeld<void> tracker;
        PCall<uint32_t>(stackObject, kQueryInterfaceSlot, kUseTrackingInterface, tracker.Receive());
        if (tracker.get()) {
            void *const trackerHost = PCall<void *>(tracker.get(), 6);
            counterOwner = PCall<ifaces::cTSObject *>(trackerHost, 11);
            void *const first = PCall<void *>(stackObject, 61);
            void *const second = PCall<void *>(first, 27);
            attributes = PCall<ifaces::AttributeTable *>(second, 4);
        }
        tracker.Reset();
    }

    if ((attributes->GetFlags(kAttributeUsesCounter) & 1) == 0) return;
    ifaces::SimRowTable *const table = counterOwner->GetUseCounters();
    if (!table) return;

    const int32_t row = static_cast<int16_t>(As<ifaces::cITSPerson>(&mPersonVTable)->GetSimRow());
    const int32_t objectId = static_cast<int16_t>(counterOwner->GetObjectId());
    if (table->GetColumnCount(row) <= 1) {
        table->CreateRow(objectId, row, kUseCounterRowColumns);
        if (table->GetColumnCount(row) <= 1) return;
        table->SetCell(row, kUseCounterColumn, 0);
    }
    const uint32_t uses = table->GetCell(row, kUseCounterColumn);
    if (uses < kUseCounterLimit) table->SetCell(row, kUseCounterColumn, uses + 1);
}
