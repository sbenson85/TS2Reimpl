#include "simantics/FindBestAction.h"
#include "simantics/PrimPerson.h"
#include "simantics/SimTraceLog.h"

#include "game/game.h"
#include "ifaces/PersonEnvironment.h"
#include "ifaces/PersonMotives.h"
#include "ifaces/PersonServices.h"
#include "ifaces/WorldClock.h"
#include "ifaces/cITSPerson.h"
#include "ifaces/cTSObject.h"

#include <algorithm>

// The start of the person's FindBestAction search (BeginSearch, 0x515940).

namespace {

constexpr uint32_t kPersonDataChosenStackObject = 0x35;
constexpr uint32_t kAttributeChosenEntry = 0x14;
constexpr uint32_t kPersonDataFlag = 1;
constexpr uint32_t kClockOptionAutonomy = 0x1e;
constexpr uint32_t kGlobalRecentActionLimit = 0x1014524; // int: how many recent actions are remembered (7)
constexpr int32_t kExpired = -1;

} // namespace

bool PrimPersonObject::BeginSearch()
{
    ifaces::cITSPerson *const sub = As<ifaces::cITSPerson>(&mPersonVTable);
    ifaces::WorldClock *const clock = As<ifaces::WorldClock>(mClock);

    sub->SetPersonData(kPersonDataChosenStackObject, 0xffffffffu, kPersonDataFlag);
    As<ifaces::cTSObject>(this)->SetAttribute(kAttributeChosenEntry, 0xffffffffu);
    const int16_t autonomyOption = clock->GetOption(kClockOptionAutonomy);

    // The search starts when any of these holds (they are tested in this order, each only if the ones before did not decide).
    const bool mayStart = sub->PicksBestOnly() != 0 || sub->IsAutonomyEnabled() == 0 || autonomyOption != 0 || sub->UsesTableAds() == 0 ||
                          sub->IsAutonomyForced() != 0;
    if (!mayStart) return false;
    if (As<ifaces::PersonServices>(mHolder48)->IsInputBlocked() != 0) return false;
    if (mSearchTick == clock->GetTick()) return false;

    const bool recording = As<ifaces::PersonEnvironment>(mEnvironment)->IsScoreRecordingOn() != 0;
    const float time = As<ifaces::PersonMotives>(&mObject60cVTable)->GetMotiveTime();
    const int32_t tick = clock->GetTick();

    mCandidates.clear();
    mSearchTick = tick;
    mSearchTime = time;
    mHasChoice = 0;
    mChosenInput = 0;

    if (recording) {
        mRejected.clear();
        mRejectedScores.clear();
        mCandidateScores.clear();
        mSnapshotA.Assign(mSnapshotSource);
        mLifeStageAtSearch = sub->GetSimObject()->GetCategory();
        As<ifaces::PersonMotives>(&mObject60cVTable)->CopySnapshot(&mSnapshotB);
        if (*GameAddress<uint8_t>(SimTraceLog::kGlobal) != 0) {
            const int32_t personId = static_cast<int16_t>(As<ifaces::cTSObject>(this)->GetObjectId());
            const int32_t logTick = clock->GetTick();
            const char *const name = reinterpret_cast<const char *>(As<ifaces::cTSObject>(this)->GetName());
            SimTraceLog::Get()->LogSearchStart(name, logTick, personId);
        }
    }

    // The recent-action memory: entries that ran out are marked, the rest is sorted newest expiry first and cut to the limit.
    int32_t valid = 0;
    for (RecentAction &recent : mRecentActions) {
        if (recent.mExpiryTick < mSearchTick) {
            recent.mExpiryTick = kExpired;
        } else {
            ++valid;
        }
    }
    const int32_t limit = *GameAddress<int32_t>(kGlobalRecentActionLimit);
    const int32_t keep = (limit < valid) ? limit : valid;
    std::sort(mRecentActions.begin(), mRecentActions.end(), [](const RecentAction &a, const RecentAction &b) { return a.mExpiryTick > b.mExpiryTick; });
    for (int32_t surplus = static_cast<int32_t>(mRecentActions.size()) - keep; surplus > 0; --surplus) mRecentActions.pop_back();

    mPending.Clear();
    mChoices.clear();
    mChosen = nullptr;
    mRefsA.Clear();
    mRefsB.Clear();
    mRefsAInUse = &mRefsA;
    mRefsBInUse = &mRefsB;
    mCheckTreeRuns = 0;
    mAcceptedCount = 0;
    mSearchFlag18 = 0;
    sub->SetSearching(1);
    return true;
}
