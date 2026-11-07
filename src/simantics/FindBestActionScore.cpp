#include "simantics/AutonomyHelpers.h"
#include "simantics/BconTable.h"
#include "simantics/FindBestAction.h"
#include "simantics/PrimPerson.h"
#include "simantics/PrimPersonCommon.h"

#include "game/game.h"
#include "gz/cIGZString.h"
#include "ifaces/InteractionTableEntry.h"
#include "ifaces/KindEntry.h"
#include "ifaces/KindList.h"
#include "ifaces/ObjectTracker.h"
#include "ifaces/ObjectTrackerHost.h"
#include "ifaces/PersonEnvironment.h"
#include "ifaces/PersonMotives.h"
#include "ifaces/PersonServices.h"
#include "ifaces/SimRowTable.h"
#include "ifaces/WorldClock.h"
#include "ifaces/cITSPerson.h"
#include "ifaces/cTSInteraction.h"
#include "ifaces/cTSObject.h"
#include "ifaces/cTSPerson.h"

#include <cstring>

// The scoring step of the person's FindBestAction: ScoreInteraction (0x516160) turns one interaction into an ActionRecord with a
// score (and keeps the reason in a ScoreRecord when score recording is on); ScorePending (0x516c40) walks the gathered
// interactions, drops the ones the person may not take now and scores the rest.

namespace {

// Tuning globals (game data, set from the ini).
constexpr uint32_t kGlobalPlainThreshold = 0x101450c;     // float: the lowest score kept (0.2)
constexpr uint32_t kGlobalLowThreshold = 0x1014510;       // float: the same for a person that picks the best only (0.05)
constexpr uint32_t kGlobalDefaultDistance = 0x1014404;    // float: the default distance handed to the distance scale (10.0)
constexpr uint32_t kGlobalProximityCap = 0x1014530;       // float: numerator of the capped proximity (2.0)
constexpr uint32_t kGlobalCappedProximity = 0x10dac40;    // int: use the capped proximity for gathered interactions
constexpr uint32_t kGlobalNoRecentDiscount = 0x10dac44;   // int: ignore the recent-action discount

constexpr uint32_t kPersonDataKind = 0x1f;                // Person Data: the Sim's kind (row of the preference tables)
constexpr uint32_t kPersonDataFlag = 1;
constexpr uint32_t kSimInterfaceId = 0x5a69d3d5;          // the cITSPerson interface of a Sim's stack object
constexpr uint32_t kObjectTrackerInterfaceId = 0x5a6216f6;
constexpr uint32_t kPreferenceSkillBonusFlag = 0x800;     // cell 1 of the kind's row: the Sim has the bonus
constexpr uint32_t kBconSkillBonus = 0x15b;               // BCON table and entry giving the bonus in percent
constexpr uint32_t kBconSkillBonusEntry = 0xb;
constexpr float kPercent = 0.01f;
constexpr uint32_t kTreeFlagsNeedCheck = 6;               // tree flag bits that make the check run before scoring
constexpr double kMinimumScore = 1.0e-5;                  // a score below it is rejected whatever the threshold

constexpr uint32_t kFlagFromOwnList = 1;                  // score record flags: the interaction came from the person's own list
constexpr uint32_t kFlagRecentlyDone = 2;                 // ... the action was started lately (discounted)
constexpr uint32_t kFlagRejected = 8;                     // ... the score was too low

constexpr int kSkillColumns = 4;

// The id of the main object behind a primitive-forwarding stack object (a part of a larger object), through its tracker
// interface. (The interface is taken without a test, as in the original.)
int32_t MainObjectId(ifaces::cTSObject *stackObject)
{
    PHeld tracker;
    tracker.mArmed = true;
    stackObject->QueryInterface(kObjectTrackerInterfaceId, &tracker.mObject);
    const int32_t objectId = static_cast<int16_t>(As<ifaces::ObjectTracker>(tracker.mObject)->GetHost()->GetMainObject()->GetObjectId());
    tracker.ReleaseNow();
    return objectId;
}

// Moves the vector `produced` (what a table entry filled in) into `target` (empty), as the original does: the three words are
// taken and the source forgets them.
template <typename V> void TakeVector(V &target, V *produced)
{
    if (&target != produced) target = std::move(*produced);
}

} // namespace

void PrimPersonObject::ScoreInteraction(void *interactionPtr, void *entryPtr, void *preferencesPtr, uint8_t gathered, uint8_t checked)
{
    ifaces::cTSInteraction *const interaction = As<ifaces::cTSInteraction>(interactionPtr);
    ifaces::InteractionTableEntry *const entry = As<ifaces::InteractionTableEntry>(entryPtr);
    ifaces::SimRowTable *const preferences = As<ifaces::SimRowTable>(preferencesPtr);
    ifaces::cITSPerson *const sub = As<ifaces::cITSPerson>(&mPersonVTable);

    if (mScoringObject == nullptr || As<ifaces::cTSObject>(mScoringObject)->IsAvailable() == 0) mScoringObject = this;
    const bool recording = As<ifaces::PersonEnvironment>(mEnvironment)->IsScoreRecordingOn() != 0;
    const float threshold = sub->PicksBestOnly() != 0 ? *GameAddress<float>(kGlobalLowThreshold) : *GameAddress<float>(kGlobalPlainThreshold);

    ifaces::cTSObject *const stackObject = interaction->GetStackObject();
    const float distanceScale = As<ifaces::cTSObject>(mScoringObject)->GetDistanceScale(stackObject, *GameAddress<float>(kGlobalDefaultDistance));
    const float objectBonus = sub->GetObjectBonus(stackObject);
    const float interactionFactor = sub->GetInteractionFactor(stackObject, interaction);

    // The multiplier a Sim stack object's skill bonus gives (a person that uses table ads has none).
    float multiplier = 1.0f;
    if (sub->UsesTableAds() == 0) {
        PHeld tracker;
        tracker.mArmed = true;
        if (stackObject->QueryInterface(kSimInterfaceId, &tracker.mObject) == 0) {
            tracker.ReleaseNow();
        } else {
            ifaces::cITSPerson *const simPerson = As<ifaces::cITSPerson>(tracker.mObject);
            const int32_t row = static_cast<int16_t>(simPerson->GetPersonData(kPersonDataKind, kPersonDataFlag));
            if (preferences->GetColumnCount(static_cast<uint32_t>(row)) > 1) {
                const int32_t row2 = static_cast<int16_t>(simPerson->GetPersonData(kPersonDataKind, kPersonDataFlag));
                if ((preferences->GetCell(static_cast<uint32_t>(row2), 1) & kPreferenceSkillBonusFlag) != 0) {
                    uint16_t percent = 0;
                    if (GetBconValue(kBconSkillBonus, kBconSkillBonusEntry, &percent) != 0)
                        multiplier = static_cast<float>(static_cast<int16_t>(percent)) * kPercent + 1.0f;
                }
            }
            tracker.ReleaseNow();
        }
    }

    const float personFactor = As<ifaces::cTSPerson>(this)->GetPersonFactor(stackObject);

    float proximity;
    float scoreFactor;
    if (gathered && *GameAddress<int32_t>(kGlobalCappedProximity) != 0) {
        const float distance = interaction->GetDistance();
        const float capped = *GameAddress<float>(kGlobalProximityCap) / (distance * distanceScale + 1.0f);
        proximity = (1.0f < capped) ? 1.0f : capped;
        scoreFactor = GetStackObjectScoreFactor(stackObject);
    } else {
        const float distance = interaction->GetDistance();
        proximity = 1.0f / (distance * distanceScale + 1.0f);
        scoreFactor = GetStackObjectScoreFactor(stackObject);
    }

    // A gathered interaction that was checked already is scored against the person's own entry.
    ifaces::InteractionTableEntry *const scoreEntry = (gathered && checked) ? As<ifaces::PersonServices>(mHolder48)->GetOwnEntry() : entry;
    const float motive = As<ifaces::PersonMotives>(&mObject60cVTable)->ScoreEntry(scoreEntry);
    uint32_t flags = 0;
    float recentFactor = 1.0f;
    if (!mRecentActions.empty()) {
        int32_t objectId = static_cast<int16_t>(stackObject->GetObjectId());
        if (stackObject->ForwardsPrimitives() != 0) objectId = MainObjectId(stackObject);
        for (const RecentAction &recent : mRecentActions) {
            // (The entry index is compared as the whole register the slot returns, against the sign-extended memory.)
            if (static_cast<int16_t>(recent.mStackObjectId) == objectId && recent.mExpiryTick > mSearchTick &&
                static_cast<int32_t>(entry->GetEntryIndex()) == static_cast<int16_t>(recent.mEntryIndex)) {
                recentFactor = entry->GetRecentFactor();
                flags = kFlagRecentlyDone;
                break;
            }
        }
    }
    if (*GameAddress<int32_t>(kGlobalNoRecentDiscount) != 0) recentFactor = 1.0f;

    const float time = As<ifaces::PersonMotives>(&mObject60cVTable)->GetMotiveTime();
    const int32_t category = stackObject->GetCategory();

    // The action record.
    ActionRecord action;
    action.mStackObjectId = interaction->GetStackObject()->GetObjectId();
    action.mEntryIndex = 0;
    if (interaction->GetGroupObject() != nullptr) action.mParameterC = static_cast<int16_t>(interaction->GetGroupObject()->GetObjectId());
    action.mFlag12 = interaction->ForcesChoice();
    action.mDistance = interaction->GetDistance();
    {
        void *const nameObject = interaction->GetNameString();
        cIGZString *const nameString = static_cast<cIGZString *>(nameObject);
        const uint32_t length = nameString->Strlen();
        const char *const data = nameString->Data();
        action.mName.mText.assign(data, length);
    }
    action.mInteraction.Hold(interactionPtr);
    interaction->GetParameters(&action.mParameterB, &action.mParameterA);
    std::memset(action.mArguments, 0, sizeof(action.mArguments));
    if (!gathered) {
        const uint16_t *const arguments = static_cast<const uint16_t *>(interaction->GetArguments());
        std::memcpy(action.mArguments, arguments, 8);
    } else {
        action.mArguments[3] = 0xffff;
    }

    // The score: the motive score above the time, times the discounts and bonuses, one factor at a time.
    float score = motive - time;
    score *= recentFactor;
    score *= proximity;
    score *= objectBonus + 1.0f;
    score *= interactionFactor;
    score *= multiplier;
    score *= scoreFactor;
    score *= personFactor;
    action.mScore = score;
    action.mMotiveScore = motive;
    action.mDistanceScale = distanceScale;
    action.mEntryIndex = static_cast<uint16_t>(entry->GetEntryIndex());
    action.mChecked = checked;

    ScoreRecord record;
    record.mScore = -1.0f;
    record.mFlags = 0;
    record.mObjectBonus = 0;
    record.mInteractionFactor = 0;
    record.mInput = 0;
    record.mObjectCategory = 0;
    record.mProximityFactor = -1.0f;
    for (uint32_t &skill : record.mSkillValues) skill = 0xffffffffu;
    if (recording) {
        record.mObjectBonus = objectBonus;
        record.mInteractionFactor = interactionFactor;
        record.mObjectCategory = category;
        record.mInput = mResult830;
        record.mScore = score;
        record.mProximityFactor = proximity;
        record.mFlags = flags;
        if (!gathered) record.mFlags |= kFlagFromOwnList;
    }

    const bool tooLow = threshold > score || kMinimumScore > static_cast<double>(score);
    if (tooLow) {
        if (recording) {
            record.mFlags |= kFlagRejected;
            mRejected.push_back(action);
            mRejectedScores.push_back(record);
        }
        return;
    }

    if (recording) {
        // What the entry advertises for this Sim's life stage: its table ads or its trait ads.
        const bool tableAds = sub->UsesTableAds() != 0;
        const uint32_t lifeStage = sub->GetLifeStage();
        if (tableAds) {
            std::vector<TreeTableAd> produced;
            auto *const result = static_cast<std::vector<TreeTableAd> *>(scoreEntry->GetTableAds(&produced, lifeStage));
            TakeVector(record.mTableAds, result);
        } else {
            std::vector<std::vector<TraitAdValue>> produced;
            auto *const result = static_cast<std::vector<std::vector<TraitAdValue>> *>(scoreEntry->GetTraitAds(&produced, lifeStage));
            TakeVector(record.mTraitAds, result);
        }

        // The Sim's skill columns, when the stack object is a Sim.
        PHeld tracker;
        tracker.mArmed = true;
        if (stackObject->QueryInterface(kSimInterfaceId, &tracker.mObject) != 0) {
            ifaces::cITSPerson *const simPerson = As<ifaces::cITSPerson>(tracker.mObject);
            const int32_t countRow = static_cast<int16_t>(simPerson->GetPersonData(kPersonDataKind, kPersonDataFlag));
            const uint32_t columns = preferences->GetColumnCount(static_cast<uint32_t>(countRow));
            // Each column re-asks the Sim for its row; the columns land in the record in this order: 0 -> [1], 1 -> [2],
            // 2 -> [0], 3 -> [3].
            static const int kRecordSlotOfColumn[kSkillColumns] = {1, 2, 0, 3};
            for (uint32_t column = 0; column < kSkillColumns && columns > column; ++column) {
                const int32_t row = static_cast<int16_t>(simPerson->GetPersonData(kPersonDataKind, kPersonDataFlag));
                record.mSkillValues[kRecordSlotOfColumn[column]] = preferences->GetCell(static_cast<uint32_t>(row), column);
            }
        }
        tracker.ReleaseNow();
        mCandidateScores.push_back(record);
    }
    mCandidates.push_back(action);
    mChoices.push_back(action);
}

void PrimPersonObject::ScorePending()
{
    ifaces::cITSPerson *const sub = As<ifaces::cITSPerson>(&mPersonVTable);
    const int32_t actorId = static_cast<int16_t>(sub->GetSimObject()->GetObjectId());
    const int32_t tick = As<ifaces::WorldClock>(mClock)->GetTick();
    ifaces::KindList *const kinds = As<ifaces::PersonEnvironment>(mEnvironment)->GetKindList();
    const int32_t kind = static_cast<int16_t>(sub->GetPersonData(kPersonDataKind, kPersonDataFlag));
    ifaces::SimRowTable *const preferences = kinds->GetKind(kind, 1)->GetPreferences();

    // (The loop runs over the list as it was when it started.)
    void *const *const end = mPending.mLast;
    for (void *const *pending = mPending.mFirst; pending != end; ++pending) {
        ifaces::cTSInteraction *const interaction = As<ifaces::cTSInteraction>(*pending);
        ifaces::cTSObject *const stackObject = interaction->GetStackObject();
        ifaces::InteractionTableEntry *const entry = interaction->GetTableEntry();

        // An interaction on the person itself needs an entry that allows it.
        if (static_cast<void *>(stackObject) == static_cast<void *>(this) && entry->IsSelfUsable() == 0) continue;

        // While the person is busy with an interaction that allows interruption, only interrupts may start, and (when asked)
        // only on the same object.
        if (mRespectCurrentInteraction != 0) {
            ifaces::cTSInteraction *const current = As<ifaces::cTSInteraction>(mCurrentInteraction);
            if (current != nullptr && current->AllowsInterruption() != 0) {
                if (interaction->IsInterrupt() == 0) continue;
                if (mSameObjectAsCurrent != 0 && stackObject != current->GetStackObject()) continue;
            }
        }
        if (interaction->IsInterrupt() != 0) {
            ifaces::cTSInteraction *const current = As<ifaces::cTSInteraction>(mCurrentInteraction);
            if (current == nullptr || current->AllowsInterruption() == 0) continue;
        }

        const int32_t objectId = static_cast<int16_t>(stackObject->GetObjectId());
        if (entry->WasRecentlyUsed(tick, actorId, objectId) != 0) continue;
        entry->RecordUse(tick, actorId, objectId);
        ++mAcceptedCount;

        // Trees flagged for it are checked now (and only scored when they pass); the others are scored unchecked.
        const bool needsCheck = InteractionTreeHasFlags(interaction, kTreeFlagsNeedCheck);
        if (!needsCheck || CheckInteraction(*pending)) ScoreInteraction(*pending, entry, preferences, 1, needsCheck ? 1 : 0);
    }
}
