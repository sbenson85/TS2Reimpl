#include "simantics/FindBestAction.h"

#include "game/original.h"
#include "ifaces/PersonEnvironment.h"
#include "ifaces/ObjectTracker.h"
#include "ifaces/ObjectTrackerHost.h"
#include "ifaces/PersonServices.h"
#include "ifaces/cITSPerson.h"
#include "ifaces/cTSObject.h"
#include "ifaces/cTSTreeStackElem.h"
#include "rz/cRZRandom.h"
#include "simantics/PrimPerson.h"
#include "simantics/PrimPersonCommon.h"
#include "simantics/SimTraceLog.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace {

// The score comparisons of the sorts below treat scores closer than this as equal.
constexpr float kScoreTolerance = 1.0e-7f;

// The tuning globals the choice reads (game data, set from the ini): how many of the best actions the random pick may take
// (0x1014518), and after how many check-tree runs the walk stops once it has something (0x1014520).
constexpr uint32_t kGlobalChoiceWidth = 0x1014518;
constexpr uint32_t kGlobalCheckLimit = 0x1014520;

// Orders by score, best first: "a comes after b" when b's score is higher by more than the tolerance, "equal" when they
// differ by less (the original's chain of the two compares; an unordered difference, a NaN score, counts as equal).
int CompareScoresDescending(float a, float b)
{
    const float difference = b - a;
    if (difference > kScoreTolerance) return 1;
    return (difference < -kScoreTolerance) ? -1 : 0;
}

constexpr uint32_t kGlobalMinimumScore = 0x1014514;      // float: the lowest score a checked action may start with
constexpr uint32_t kGlobalRecentActionTicks = 0x1014528; // int: how long a started action stays in the recent-action memory
constexpr uint32_t kPersonDataActionStackObject = 0x35;
constexpr uint32_t kAttributeChosenEntry = 0x14;
constexpr uint32_t kPersonDataFlag = 1;
constexpr uint32_t kObjectTrackerInterface = 0x5a6216f6;

} // namespace

// 0x512d70 (__cdecl): the sort order of ActionRecords by score, best first.
int __cdecl CompareActionRecords(const void *a, const void *b)
{
    return CompareScoresDescending(static_cast<const ActionRecord *>(a)->mScore, static_cast<const ActionRecord *>(b)->mScore);
}

// 0x512db0 (__cdecl): the same order for ScoreRecords.
int __cdecl CompareScoreRecords(const void *a, const void *b)
{
    return CompareScoresDescending(static_cast<const ScoreRecord *>(a)->mScore, static_cast<const ScoreRecord *>(b)->mScore);
}

SearchSnapshot *SearchSnapshot::Assign(const SearchSnapshot *other)
{
    for (int i = 0; i < 32; ++i) mData[i] = other->mData[i];
    if (&other->mReference != &mReference) {
        void *const incoming = other->mReference;
        void *const outgoing = mReference;
        if (incoming != outgoing) {
            if (incoming) VirtualCall<uint32_t(__thiscall *)(void *), 1>(incoming);
            mReference = incoming;
            if (outgoing) VirtualCall<uint32_t(__thiscall *)(void *), 2>(outgoing);
        }
    }
    return this;
}

uint32_t PrimPersonObject::StartChosenAction()
{
    ActionRecord *const chosen = mChosen;
    if (!chosen) return 0;
    ifaces::cITSPerson *const sub = As<ifaces::cITSPerson>(&mPersonVTable);
    // With a minimum-score mode on, an action scoring below the tuning value (or no number at all) is not started.
    if (sub->RequiresMinimumScore() != 0 && !(chosen->mScore >= *GameAddress<float>(kGlobalMinimumScore))) return 0;

    As<ifaces::cTSObject>(this)->SetAttribute(kAttributeChosenEntry, chosen->mEntryIndex);
    As<ifaces::cTSTreeStackElem>(mFrame)->SetStackObjectId(chosen->mStackObjectId);
    sub->SetPersonData(kPersonDataActionStackObject, chosen->mStackObjectId, kPersonDataFlag);
    if (chosen->mArguments[3] == 0xffff) {
        std::memset(mActionArguments, 0, sizeof(mActionArguments));
    } else {
        std::memcpy(mActionArguments, chosen->mArguments, sizeof(mActionArguments));
    }
    mHasChoice = 1;

    RecentAction recent;
    recent.mExpiryTick = mSearchTick + *GameAddress<int32_t>(kGlobalRecentActionTicks);

    // The id the memory files the action under: the main object of the stack object's owner when that owner forwards
    // primitives (a part of a larger object), else the stack object itself.
    // ORIGINAL-BUG: a stack object that no longer exists (or has no owner object) is dereferenced without a test, and so is
    // a missing tracker interface; the original crashes there.
    ifaces::cTSObject *const stackObject = static_cast<ifaces::cTSObject *>(
        mObjectRegistry->Find(static_cast<int16_t>(chosen->mStackObjectId)));
    ifaces::cTSObject *const owner = stackObject ? stackObject->GetResolvedObject() : nullptr;
    if (owner->ForwardsPrimitives() != 0) {
        PHeld tracker;
        tracker.mArmed = true;
        owner->QueryInterface(kObjectTrackerInterface, &tracker.mObject);
        recent.mStackObjectId = As<ifaces::ObjectTracker>(tracker.mObject)->GetHost()->GetMainObject()->GetObjectId();
        tracker.ReleaseNow();
    } else {
        recent.mStackObjectId = chosen->mStackObjectId;
    }
    recent.mEntryIndex = chosen->mEntryIndex;
    recent.mName.mText.assign(chosen->mName.mText.data(), chosen->mName.mText.size());

    mActionName.mText.assign(chosen->mName.mText.data(), chosen->mName.mText.size());
    mActionParameterA = chosen->mParameterA;
    mActionParameterB = chosen->mParameterB;
    mActionParameterC = chosen->mParameterC;
    mRecentActions.push_back(recent);
    return 1;
}

void PrimPersonObject::ChooseAction()
{
    const size_t choiceCount = mChoices.size();
    if (choiceCount == 0) return;

    std::qsort(mChoices.data(), choiceCount, sizeof(ActionRecord), &CompareActionRecords);
    const bool recordingScores = As<ifaces::PersonEnvironment>(mEnvironment)->IsScoreRecordingOn() != 0;
    const ActionRecord *const best = mChoices.data();
    if (recordingScores) {
        // (Both sorts use the candidate count, as the original does.)
        const size_t candidateCount = mCandidates.size();
        std::qsort(mCandidates.data(), candidateCount, sizeof(ActionRecord), &CompareActionRecords);
        std::qsort(mCandidateScores.data(), candidateCount, sizeof(ScoreRecord), &CompareScoreRecords);
    }

    // The rank to take: the best, unless the person does not insist on it and the best is not flagged; then a random one among
    // the first few.
    uint32_t wantedRank = 0;
    if (As<ifaces::cITSPerson>(&mPersonVTable)->PicksBestOnly() == 0 && best->mFlag12 == 0) {
        int32_t width = *GameAddress<int32_t>(kGlobalChoiceWidth);
        if (static_cast<int32_t>(choiceCount) < width) width = static_cast<int32_t>(choiceCount);
        cRZRandom *const random = static_cast<cRZRandom *>(As<ifaces::PersonServices>(mHolder48)->GetRandom());
        wantedRank = random->RandomUint32Uniform(static_cast<uint32_t>(width));
    }

    mChosen = nullptr;
    int32_t rank = 0;
    for (ActionRecord *record = mChoices.data(); record != mChoices.data() + mChoices.size(); ++record) {
        const int32_t checkLimit = *GameAddress<int32_t>(kGlobalCheckLimit);
        if (checkLimit != 0 && checkLimit < mCheckedCount && mLoopFlag == 0 && mChosen != nullptr) break;

        bool accepted;
        if (record->mChecked == 0) {
            // Interactions from the gathered list pass their check tree first, and only while the walk still needs one.
            accepted = rank <= static_cast<int32_t>(wantedRank) && CheckInteraction(record->mInteraction.Get());
        } else {
            accepted = rank <= static_cast<int32_t>(wantedRank);
        }
        if (!accepted) continue;

        mChosen = record;
        mHasChoice = 1;
        if (rank == static_cast<int32_t>(wantedRank)) {
            mChosenInput = static_cast<int32_t>(mCheckTreeRuns);
            if (!recordingScores) return;
        }
        ++rank;
    }
    if (recordingScores && *GameAddress<uint8_t>(SimTraceLog::kGlobal) != 0) SimTraceLog::Get()->CloseSink();
}
