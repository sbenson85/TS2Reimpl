#include "simantics/AutonomyHelpers.h"
#include "simantics/EdithObjectTestSim.h"
#include "simantics/FindBestAction.h"
#include "simantics/PrimPerson.h"
#include "simantics/PrimPersonCommon.h"

#include "game/game.h"
#include "gz/cIGZString.h"
#include "ifaces/InteractionTableEntry.h"
#include "ifaces/PersonEnvironment.h"
#include "ifaces/PersonServices.h"
#include "ifaces/cITSPerson.h"
#include "ifaces/cTSInteraction.h"
#include "ifaces/cTSObject.h"

// The check step of the person's FindBestAction (CheckInteraction, 0x515c80).

namespace {

// The call mode the search gives the check tree (the check runner's second argument).
constexpr uint32_t kCheckCallMode = 7;
// Score record flags set for a rejected interaction: the check tree failed or vetoed it (4), or it made an action string (0x10).
constexpr uint32_t kRejectedByCheck = 4;
constexpr uint32_t kRejectedByActionString = 0x10;
// The tuning float the distance scale is asked for (game global 0x1014404, 10.0).
constexpr uint32_t kGlobalDefaultDistance = 0x1014404;

} // namespace

bool PrimPersonObject::CheckInteraction(void *interactionPtr)
{
    ifaces::cTSInteraction *const interaction = As<ifaces::cTSInteraction>(interactionPtr);
    const bool recording = As<ifaces::PersonEnvironment>(mEnvironment)->IsScoreRecordingOn() != 0;
    ifaces::cTSObject *const stackObject = interaction->GetStackObject();
    SetUiManager(interactionPtr);
    SetActionStringShown(0);
    ifaces::InteractionTableEntry *const recorder = As<ifaces::PersonServices>(mHolder48)->GetOwnEntry();

    TestSimScope sim;
    sim->SetPerson(&mPersonVTable, 1, 0);
    sim->SetObjectKey(stackObject->GetObjectKey());
    sim->RunCheckTree(interactionPtr, kCheckCallMode, recorder, 1, 0, 0);
    ++mCheckTreeRuns;

    bool rejected = interaction->HasPassedCheck() == 0 || interaction->IsVetoed() != 0;
    const bool actionStringShown = WasActionStringShown() != 0;
    ++mCheckedCount;
    if (actionStringShown) {
        rejected = true;
    } else if (!rejected) {
        return true;
    }
    if (!recording) return !rejected;
    ifaces::cTSObject *const recordedObject = interaction->GetStackObject();
    if (!recordedObject) return !rejected;

    // Why it was rejected: an action record (without the interaction) and a score record, kept for the log.
    // (Fields the original leaves as stack leftovers are zero here.)
    ifaces::InteractionTableEntry *const entry = As<ifaces::PersonServices>(mHolder48)->GetOwnEntry();
    ActionRecord action;
    action.mStackObjectId = recordedObject->GetObjectId();
    action.mEntryIndex = static_cast<uint16_t>(entry->GetEntryIndex());
    action.mScore = -1.0f;
    *reinterpret_cast<cIGZString *>(&action.mName) = *static_cast<cIGZString *>(entry->GetNameString());

    ScoreRecord score;
    score.mObjectCategory = stackObject->GetCategory();
    ifaces::cITSPerson *const sub = As<ifaces::cITSPerson>(&mPersonVTable);
    const float objectBonus = sub->GetObjectBonus(stackObject);
    const float interactionFactor = sub->GetInteractionFactor(stackObject, interaction);
    const float distanceScale = As<ifaces::cTSObject>(this)->GetDistanceScale(stackObject, *GameAddress<float>(kGlobalDefaultDistance));
    score.mScore = -1.0f;
    score.mFlags = actionStringShown ? kRejectedByActionString : kRejectedByCheck;
    score.mObjectBonus = objectBonus;
    score.mInteractionFactor = interactionFactor;
    score.mInput = mResult830;
    score.mProximityFactor = -1.0f;
    for (uint32_t &skill : score.mSkillValues) skill = 0xffffffffu;
    action.mDistanceScale = distanceScale;
    action.mDistance = interaction->GetDistance();

    mRejected.push_back(action);
    mRejectedScores.push_back(score);
    return !rejected;
}
