#include "simantics/EdithObjectTestSim.h"

#include "game/game.h"
#include "ifaces/CheckRecorder.h"
#include "ifaces/InteractionTableEntry.h"
#include "ifaces/cITSPerson.h"
#include "ifaces/cTSInteraction.h"
#include "ifaces/cTSObject.h"
#include "ifaces/cTSTreeSim.h"
#include "rz/Singleton_20ddfb.h"
#include "simantics/PrimPersonCommon.h"
#include "simantics/SimTraceLog.h"

namespace {

using Interaction = ifaces::cTSInteraction;

// Slots of the test sim itself.
constexpr int kSetObjectKeySlot = 5;

// Attributes of the actor's object the check tree reports through (cleared before a run, copied to the interaction after).
constexpr uint32_t kAttributeCheckFailed = 0x32;
constexpr uint32_t kAttributeCheckSecond = 6;
constexpr uint32_t kAttributeCheckThird = 0x65;
constexpr uint32_t kAttributeCheckFourth = 0x68;

constexpr uint32_t kSourceKindIsOne = 1;
constexpr uint32_t kTuningCheckRestricted = 0x23;
constexpr uint32_t kObjectFlagAutonomyVeto = 0x2000;
constexpr uint32_t kRecorderCounterKind = 0xd;
constexpr uint32_t kTableEntryCheckFlag = 1;

// Slots with no name yet. The singleton the settings come from (0x20ddfb) hands out a tuning object (slot 3) whose slot 15
// reads a value by id; the singleton's slot 31 is a boolean setting.
constexpr int kSingletonTuningSlot = 3;
constexpr int kTuningValueSlot = 15;
constexpr int kSingletonAutonomySlot = 31;
// The stack object's boolean slots 135 and 190 (the latter takes flag A) both veto the interaction.
constexpr int kStackObjectVetoSlot = 135;
constexpr int kStackObjectFlagVetoSlot = 190;
// The actor's boolean slots 30 and 38; slot 38 gates the counter record.
constexpr int kActorChecksEntrySlot = 30;
constexpr int kActorCountsSlot = 38;
// The interaction's setters for the four copied attributes: slot 42 (attribute 0x32 as a boolean), 56 (attribute 6 as a
// boolean), 75 (attribute 0x65) and 77 (attribute 0x68).
constexpr int kInteractionStoreSecondSlot = 56;
constexpr int kInteractionStoreFailedSlot = 42;
constexpr int kInteractionStoreThirdSlot = 75;
constexpr int kInteractionStoreFourthSlot = 77;

} // namespace

cEdithObjectTestSim *cEdithObjectTestSim::Construct()
{
    mVTable = GameAddress<void *>(kVTable);
    mRefCount = 0;
    mPerson = nullptr;
    mObjectKey = 0;
    mFlagA = 0;
    mFlagB = 0;
    mSavedFor = nullptr;
    return this;
}

void cEdithObjectTestSim::Destruct() noexcept
{
    mVTable = GameAddress<void *>(kVTable);
    if (mSavedFor) {
        for (int i = 0; i < kArgumentCount; ++i) {
            ifaces::cTSObject *const actorObject = As<ifaces::cITSPerson>(mSavedFor)->GetSimObject();
            actorObject->SetArgument(static_cast<uint32_t>(i), mSavedArguments[i]);
        }
        mSavedFor = nullptr;
    }
}

uint8_t cEdithObjectTestSim::Begin(void *personInterface, void *stackObject, uint32_t flagA, uint32_t flagB)
{
    void **const vtable = mVTable;
    mPerson = personInterface;
    mFlagA = static_cast<uint8_t>(flagA);
    mFlagB = static_cast<uint8_t>(flagB);
    mObjectKey = 0;
    mSavedFor = nullptr;
    const uint32_t key = As<ifaces::cTSObject>(stackObject)->GetObjectKey();
    reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t)>(vtable[kSetObjectKeySlot])(this, key);
    return 1;
}

uint8_t cEdithObjectTestSim::SetPerson(void *personInterface, uint32_t flagA, uint32_t flagB)
{
    mPerson = personInterface;
    mFlagA = static_cast<uint8_t>(flagA);
    mFlagB = static_cast<uint8_t>(flagB);
    mObjectKey = 0;
    mSavedFor = nullptr;
    return 1;
}

void cEdithObjectTestSim::SetObjectKey(uint32_t key)
{
    reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t)>(mVTable[kSetObjectKeySlot])(this, key);
}

void cEdithObjectTestSim::RunCheckTree(void *interactionPtr, uint32_t callMode, void *recorderPtr, uint32_t runTree, uint32_t extraArgument,
                                       uint32_t bypassVeto)
{
    Interaction *const interaction = As<Interaction>(interactionPtr);
    ifaces::CheckRecorder *const recorder = As<ifaces::CheckRecorder>(recorderPtr);

    if (interaction->GetInteractionId() <= -1) return;
    interaction->SetCheckPassed(0);
    interaction->SetCheckPending(1);
    ifaces::cITSPerson *const actor = interaction->GetActor();
    ifaces::InteractionTableEntry *const tableEntry = interaction->GetTableEntry();
    ifaces::cTSObject *const stackObject = interaction->GetStackObject();
    if (!actor || !stackObject || !tableEntry) return;

    ifaces::cTSObject *const actorObject = actor->GetSimObject();
    // The arguments the check tree gets: flag A, "the interaction's source kind is 1", the caller's word, 0.
    uint16_t treeArguments[4] = {0, 0, 0, 0};
    if (mFlagA) treeArguments[0] = 1;
    if (interaction->GetSourceKind() == kSourceKindIsOne) treeArguments[1] = 1;
    treeArguments[2] = static_cast<uint16_t>(extraArgument);
    actorObject->SetAttribute(kAttributeCheckFailed, 0);
    actorObject->SetAttribute(kAttributeCheckSecond, 0);
    actorObject->SetAttribute(kAttributeCheckThird, 0);
    actorObject->SetAttribute(kAttributeCheckFourth, 0);

    // The verdict is held in the low byte of the stack slot of the first argument (the interaction's address); the call
    // that stores it passes the whole slot, so the upper three bytes of the address travel along. The setter reads the low
    // byte only. The tree's answer is kept as it comes (a byte), not reduced to 0 / 1.
    uint8_t verdict;
    bool passed;
    {
        const uint32_t actorMask = actor->GetAgeMask();
        const uint32_t entryMask = tableEntry->GetAllowedAgeMask();
        passed = (actorMask & entryMask) != 0;
        verdict = passed ? 1 : 0;
    }
    if (passed) {
        // Vetoes: each can only clear the verdict.
        if (PCall<uint8_t>(actor, kActorChecksEntrySlot) != 0) passed = (tableEntry->CheckAllowed(kTableEntryCheckFlag) != 0) && passed;
        if (PCall<uint8_t>(stackObject, kStackObjectVetoSlot) != 0) {
            passed = false;
        } else if (PCall<uint8_t>(stackObject, kStackObjectFlagVetoSlot, static_cast<uint32_t>(mFlagA)) != 0) {
            passed = false;
        } else {
            void *const tuning = PCall<void *>(GetCachedSingleton(), kSingletonTuningSlot);
            if (PCall<int16_t>(tuning, kTuningValueSlot, kTuningCheckRestricted) != 0 && tableEntry->IsRestricted()) passed = false;
        }
        verdict = passed ? 1 : 0;

        void *const settings = GetCachedSingleton();
        if (mFlagA == 0 && stackObject->TestFlags(kObjectFlagAutonomyVeto) != 0 && PCall<uint8_t>(settings, kSingletonAutonomySlot) != 0 &&
            (bypassVeto & 0xff) == 0) {
            passed = (tableEntry->IsDebugOnly() != 0) && passed;
            verdict = passed ? 1 : 0;
        } else if (tableEntry->IsDebugOnly() != 0) {
            passed = false;
            verdict = 0;
        }
    }

    if (passed) {
        if (recorder) recorder->RecordTableEntry(tableEntry);
        const uint32_t checkTreeId = tableEntry->GetCheckTreeId();
        if (checkTreeId == 0 || (runTree & 0xff) == 0) {
            verdict = 1;
        } else {
            if (mSavedFor == nullptr) {
                mSavedFor = actor;
                for (int i = 0; i < kArgumentCount; ++i) mSavedArguments[i] = static_cast<uint16_t>(actorObject->GetArgument(static_cast<uint32_t>(i)));
            }
            SimTraceLog *const log = SimTraceLog::Get();
            if (log->IsActive()) {
                const int32_t interactionId = interaction->GetInteractionId();
                const char *const name = stackObject->GetName();
                const int32_t stackObjectId = static_cast<int16_t>(stackObject->GetObjectId());
                log->LogInteractionCheckStart(static_cast<int16_t>(checkTreeId), stackObjectId, name, interactionId);
            }
            ifaces::cTSTreeSim *const treeSim = actor->GetTreeSim();
            const uint32_t actorId = actorObject->GetObjectId();
            const uint32_t stackObjectId = stackObject->GetObjectId();
            ifaces::TreeOwner *const owner = stackObject->GetTreeOwner();
            verdict = treeSim->CallTree(owner, stackObjectId, checkTreeId, actorId, callMode, treeArguments, 0, 0);
            if (log->IsActive()) log->LogInteractionCheckComplete();
            if (mFlagB) {
                verdict = 1;
                actorObject->SetAttribute(kAttributeCheckFailed, 0);
            }
        }
        if (recorder) {
            // When actor and stack object have different owners, a Sim-aware recorder resets or negates its counter.
            const uint16_t actorOwner = actorObject->GetOwnerId();
            const uint16_t stackOwner = stackObject->GetOwnerId();
            if (stackOwner != actorOwner && PCall<uint8_t>(actor, kActorCountsSlot) != 0) {
                uint16_t *const record = recorder->GetCounterRecord(kRecorderCounterKind, actor->GetLifeStage(), 1, 1);
                if (record) {
                    const int16_t counter = static_cast<int16_t>(record[3]);
                    if (counter <= 0) {
                        record[4] = static_cast<uint16_t>(-counter);
                    } else {
                        record[3] = 0;
                        record[4] = 0;
                    }
                }
            }
        }
    }

    interaction->SetCheckPassed((static_cast<uint32_t>(reinterpret_cast<uintptr_t>(interactionPtr)) & ~0xffu) | verdict);
    PCall<void>(interaction, kInteractionStoreFailedSlot, static_cast<uint32_t>(actorObject->GetAttribute(kAttributeCheckFailed) != 0));
    PCall<void>(interaction, kInteractionStoreSecondSlot, static_cast<uint32_t>(actorObject->GetAttribute(kAttributeCheckSecond) != 0));
    PCall<void>(interaction, kInteractionStoreThirdSlot, static_cast<uint32_t>(actorObject->GetAttribute(kAttributeCheckThird)));
    PCall<void>(interaction, kInteractionStoreFourthSlot, static_cast<uint32_t>(actorObject->GetAttribute(kAttributeCheckFourth)));
}
