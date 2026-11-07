#include "ts/cTSSequenceEffect.h"

#include "game/game.h"
#include "game/vtable.h"
#include "rz/cRZRandom.h"
#include "ts/cTSCompDescription.h"

#include <bit>
#include <cstring>
#include <new>

namespace {

// The slots of the effects a sequence starts (named from their uses here; the objects are the effects manager's).
using StopFn = void(__thiscall *)(void *, uint32_t);
using IsRunningFn = bool(__thiscall *)(void *);
using FlagFn = void(__thiscall *)(void *, bool);
using PlacementFn = void(__thiscall *)(void *, const fx::EffectsTransform *);
using MakeFn = bool(__thiscall *)(void *, const char *, void **);

constexpr uint32_t kStart = 3, kStop = 4, kIsRunning = 5, kSetFlag = 8, kSetParent = 9, kSetLocal = 10, kMake = 14;

bool Running(void *effect) { return VirtualCall<IsRunningFn, kIsRunning>(effect); }

// The game's random generator, the one cRZRandom's static initialiser (slot 703) makes.
cRZRandom &Random() { return *GameAddress<cRZRandom>(0x10fd138); }

} // namespace

cTSSequenceEffect::cTSSequenceEffect(const cTSSequenceDescription *description)
    : mDescription(description), mRunning(false), mRestart(true), mElapsed(0), mIndex(0), mNext(0), mWaitForEffect(false),
      mHidden(true)
{
    // The two placements start as the identity; the padding byte after each one's flags stays as it was.
    for (fx::EffectsTransform *t : {&mLocal, &mParent}) {
        t->flags = 0;
        t->pad[1] = 0;
        t->pad[2] = 0;
        std::memset(t->m, 0, sizeof(t->m));
        t->m[0][0] = t->m[1][1] = t->m[2][2] = 1.0f;
        t->scale = 1.0f;
        t->translation = fx::Vector3{0, 0, 0};
    }
    mLoop = (description->mFlags >> 1) & 1;
    mStopOld = (description->mFlags >> 2) & 1;
}

cTSSequenceEffect::~cTSSequenceEffect()
{
    cTSSequenceEffect::Stop(1);
}

cITSComponentEffect *__cdecl cTSSequenceEffect::Create(const cTSSequenceDescription *description)
{
    return new (std::nothrow) cTSSequenceEffect(description);
}

void cTSSequenceEffect::Attach(cIGZUnknown *factory) { mFactory.Assign(factory); }

void cTSSequenceEffect::Start(uint32_t a)
{
    if (mDescription->mEntries.begin() == mDescription->mEntries.end()) {
        mRunning = false;
        return;
    }
    mRunning = true;
    // ORIGINAL-BUG: a nonzero a without mRestart keeps mIndex, which can be past the end of mEntries after the
    // sequence ran out, and AddToScene then reads past the end.
    if (a == 0 || mRestart) {
        mElapsed = 0;
        mIndex = 0;
        mNext = 0;
        mWaitForEffect = false;
    }
    mRestart = false;
}

void cTSSequenceEffect::Stop(uint32_t how)
{
    if (mCurrent.Get()) {
        VirtualCall<StopFn, kStop>(mCurrent.Get(), how);
        mCurrent.Reset();
    }
    if (how == 0) {
        mRunning = false;
        return;
    }
    if (!mFinishing.empty()) {
        for (size_t i = 0; i < mFinishing.size(); ++i)
            if (Running(mFinishing[i].Get())) VirtualCall<StopFn, kStop>(mFinishing[i].Get(), how);
        mFinishing.clear();
    }
    mRunning = false;
}

bool cTSSequenceEffect::IsRunning() { return mRunning || !mFinishing.empty(); }

void cTSSequenceEffect::DropFinished()
{
    size_t keep = 0;
    for (size_t i = 0; i < mFinishing.size(); ++i) {
        if (!Running(mFinishing[i].Get())) continue;
        if (int(i) > int(keep)) mFinishing[keep].Assign(mFinishing[i].Get());
        ++keep;
    }
    mFinishing.erase(mFinishing.begin() + keep, mFinishing.end());
}

void cTSSequenceEffect::AddToScene(void *step, void *)
{
    if (!mRunning) {
        DropFinished();
        return;
    }
    const float elapsed = fx::Add(std::bit_cast<float>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(step))), mElapsed);
    mElapsed = elapsed;
    float next;
    bool due;
    if (!mWaitForEffect) {
        next = mNext;
        due = elapsed >= next;
    } else if (!mCurrent.Get() || !Running(mCurrent.Get())) {
        next = elapsed;
        mNext = elapsed;
        due = true;
    } else {
        mNext = elapsed;
        return;
    }
    if (!due) return;
    if (next > 0.0f) {
        ++mIndex;
        const auto &entries = mDescription->mEntries;
        if (mIndex >= entries.size()) {
            if (!(mDescription->mFlags & 1)) {
                Stop(mStopOld);
                return;
            }
            mIndex = 0;
        }
        if (mCurrent.Get() && Running(mCurrent.Get())) {
            bool keep = true;
            if (!((mDescription->mFlags >> 3) & 1)) {
                VirtualCall<StopFn, kStop>(mCurrent.Get(), mStopOld);
                keep = mStopOld == 0;
            }
            if (keep) {
                mFinishing.push_back(mCurrent);
                DropFinished();
            }
            mCurrent.Reset();
        }
    }
    const auto &entry = mDescription->mEntries[mIndex];
    const float wait = entry.mPosition.x;
    bool waits;
    if (wait >= 0.0f) {
        const float random = static_cast<float>(Random().RandomDoubleRangeUniform(wait, entry.mPosition.y));
        mNext = fx::Add(random, mNext);
        waits = false;
    } else {
        waits = true;
    }
    mWaitForEffect = waits;
    if (entry.mName.empty() || !mFactory.Get()) return;
    mCurrent.Reset();
    if (!VirtualCall<MakeFn, kMake>(mFactory.Get(), entry.mName.c_str(), reinterpret_cast<void **>(mCurrent.Address()))) return;
    void *effect = mCurrent.Get();
    VirtualCall<PlacementFn, kSetLocal>(effect, &mLocal);
    VirtualCall<PlacementFn, kSetParent>(effect, &mParent);
    VirtualCall<StopFn, kStart>(effect, mLoop);
    VirtualCall<FlagFn, kSetFlag>(mCurrent.Get(), !mHidden);
}

void cTSSequenceEffect::Update(const fx::EffectsTransform *local, const fx::EffectsTransform *parent)
{
    std::memcpy(&mLocal, local, sizeof(mLocal));
    std::memcpy(&mParent, parent, sizeof(mParent));
    if (mCurrent.Get()) {
        VirtualCall<PlacementFn, kSetLocal>(mCurrent.Get(), &mLocal);
        VirtualCall<PlacementFn, kSetParent>(mCurrent.Get(), &mParent);
    }
}

void cTSSequenceEffect::SetFlagged(bool value)
{
    mHidden = value;
    const bool shown = !value;
    if (mCurrent.Get()) VirtualCall<FlagFn, kSetFlag>(mCurrent.Get(), shown);
    for (size_t i = 0; i < mFinishing.size(); ++i) VirtualCall<FlagFn, kSetFlag>(mFinishing[i].Get(), shown);
}
