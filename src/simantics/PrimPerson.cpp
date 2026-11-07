#include "simantics/PrimPerson.h"
#include "game/vtable.h"
#include "ifaces/KindEntry.h"
#include "ifaces/KindList.h"
#include "ifaces/ObjectModule.h"
#include "ifaces/PersonEnvironment.h"
#include "ifaces/SimRowTable.h"
#include "ifaces/cITSPerson.h"
#include "ifaces/cTSObject.h"

#include "game/game.h"
#include "game/original.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "simantics/PrimPersonCommon.h"
#include "simantics/cTSBehaviorTree.h"

#include <cstring>

namespace {

// The Person Data field of the Sim's kind (the row of the preference tables) and the flag its reads pass.
constexpr uint32_t kPersonDataKind = 0x1f;
constexpr uint32_t kPersonDataFlag = 1;

inline const uint8_t *Bytes(const PrimParamView *p)
{
    return reinterpret_cast<const uint8_t *>(p);
}


} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// 0x521860, opcode 3
// ---------------------------------------------------------------------------------------------------------------------

uint32_t PrimPersonObject::Prim03(void *frame, const PrimParamView *param)
{
    const uint8_t *p = Bytes(param);

    mFrame = frame;
    mFlag81c = (p[7] >> 1) & 1;
    mFlag81d = (p[7] >> 2) & 1;
    const uint32_t input = static_cast<uint16_t>(p[4] | (p[5] << 8));
    mInput820 = input;
    mRespectCurrentInteraction = (p[7] >> 3) & 1;
    mSameObjectAsCurrent = (p[7] >> 4) & 1;
    mResult830 = input;
    mScoringObject = this;
    mUseNodeObject = (p[7] >> 5) & 1;
    mCheckedCount = 0;
    mAcceptedCount = 0;

    void *const holder = PCall<void *>(mHolder48, 67);
    uint32_t result = 0;
    const uint8_t savedBusy = mBusy;
    const uint8_t holderFlag = PCall<uint8_t>(holder, 9);

    if (BeginSearch()) {
        mBusy = 1;
        PCall<void>(holder, 8, 1u);

        if (mUseNodeObject != 0) {
            if (void *const singleton = GetCachedSingleton()) {
                if (void *const lookup = PCall<void *>(singleton, 26)) {
                    const int16_t id = As<ifaces::cTSObject>(this)->GetArgument(1u);
                    mScoringObject = PCall<void *>(lookup, 29, static_cast<int32_t>(id));
                }
            }
        }

        // The table of the object functions of the Sim's life stage (16 dwords: the original's frame slots S+0x1c..S+0x5b, up to the
        // return address), which the input object reads. (A 60-byte block, as the decompiler suggested, is overrun by the callee:
        // the stack cookie check then ends the process.)
        uint32_t functionTable[16];
        As<ifaces::cTSObject>(this)->GetObjectModule()->GetFunctionTable(functionTable, As<ifaces::cITSPerson>(&mPersonVTable)->GetLifeStage());

        mLoopFlag = 1;
        uint32_t accumulated = 0;
        for (;;) {
            if (mInput820 == 0) {
                mResult830 = PCall<uint32_t>(&mObject60cVTable, 8, *GameAddress<uint32_t>(0x101451c), functionTable, accumulated);
            }
            const uint32_t value = mResult830;
            accumulated |= value;
            if (p[7] & 1) mInput820 = 0;

            if (value != 0) {
                GatherInteractions();
                ScorePending();
                // The interactions queued in the two reference vectors (an interaction and its table entry each) are scored against
                // the preference table of the Sim's kind, as ScorePending's own ones are, but as not gathered and already checked.
                ifaces::KindList *const kinds = As<ifaces::PersonEnvironment>(mEnvironment)->GetKindList();
                const int32_t kind = static_cast<int16_t>(As<ifaces::cITSPerson>(&mPersonVTable)->GetPersonData(kPersonDataKind, kPersonDataFlag));
                ifaces::SimRowTable *const preferences = kinds->GetKind(kind, 1)->GetPreferences();
                void **queuedInteraction = mRefsA.mFirst;
                void **queuedEntry = mRefsB.mFirst;
                while (queuedInteraction != mRefsA.mLast) {
                    ScoreInteraction(*queuedInteraction, *queuedEntry, preferences, 0, 1);
                    ++queuedInteraction;
                    ++queuedEntry;
                }
                ChooseAction();
            }

            const int32_t limit = *GameAddress<int32_t>(0x1014520);
            bool again;
            if (limit != 0 && mCheckedCount < limit && mResult830 != 0 && mInput820 == 0) {
                again = true;
            } else {
                again = mCandidates.empty() && mResult830 != 0 && mInput820 == 0;
            }
            mLoopFlag = 0;
            if (!again) break;
        }

        if (savedBusy == 0) PCall<void>(&mTreeSimVTable, 32, 8u);
        if (holderFlag == 0) PCall<void>(holder, 10);
        mBusy = savedBusy;
        // The original pushes the whole stack slot that held the flag byte; its upper three bytes are left over from an
        // earlier use of the slot. The callee takes a flag, so the byte is passed zero-extended.
        PCall<void>(holder, 8, static_cast<uint32_t>(holderFlag));
        result = StartChosenAction();
    }

    mRefsAInUse = nullptr;
    mRefsBInUse = nullptr;
    mRefsA.Clear();
    mRefsB.Clear();
    SetUiManager(nullptr);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// 0x522110, opcode 0x1d (vtable slot 361)
// ---------------------------------------------------------------------------------------------------------------------

uint32_t PrimPersonObject::Prim1D(void *frame, const PrimParamView *param)
{
    (void)frame;
    const uint8_t *p = Bytes(param);

    if (p[7] & 1) {
        if (mMotiveBegin != mMotiveEnd) mMotiveEnd = mMotiveBegin;
        return 1;
    }

    uint32_t motive;
    switch (p[6]) {
    case 0: motive = 0x64; break;
    case 1: motive = 0x6a; break;
    case 5: motive = 0x60; break;
    case 6: motive = 0x5e; break;
    case 7: motive = 0x5d; break;
    case 8: motive = 0x61; break;
    case 9: motive = 0x5f; break;
    case 14: motive = 0x63; break;
    case 15: motive = 0x65; break;
    default: PRaise(0x1a);
    }

    // The rate and the target: operands read through the tree sim subobject (slot 7, seven arguments, value in ax).
    const uint32_t first = PCall<uint16_t>(&mTreeSimVTable, 7, static_cast<uint32_t>(p[4]), static_cast<uint32_t>(p[8] | (p[9] << 8)),
                                           static_cast<void *>(nullptr), static_cast<void *>(nullptr), 0u, 0u, 0u);
    const uint32_t second = PCall<uint16_t>(&mTreeSimVTable, 7, static_cast<uint32_t>(p[5]), static_cast<uint32_t>(p[10] | (p[11] << 8)),
                                            static_cast<void *>(nullptr), static_cast<void *>(nullptr), 0u, 0u, 0u);

    MotiveInc element;
    element.mVTable = GameAddress<void>(0xdced28);
    element.mMotive = p[6];
    element.mRate = static_cast<float>(static_cast<int16_t>(first)) / 60.0f / 30.0f;
    element.mTarget = static_cast<float>(static_cast<int16_t>(second));

    if (p[7] & 2) PCall<void>(&mPersonVTable, 11, motive, 0u, 1u);

    if (mMotiveEnd != mMotiveLimit) {
        MotiveInc *slot = reinterpret_cast<MotiveInc *>(mMotiveEnd);
        slot->mMotive = element.mMotive;
        slot->mVTable = element.mVTable;
        slot->mRate = element.mRate;
        slot->mTarget = element.mTarget;
        mMotiveEnd += sizeof(MotiveInc);
        return 1;
    }
    // The vector is full: its reallocating insert (std::vector<cMotiveInc>::emplace at the end) takes the element.
    MotiveEmplaceReallocate(reinterpret_cast<MotiveIncVector *>(&mMotiveBegin), nullptr, reinterpret_cast<MotiveInc *>(mMotiveEnd), &element);
    return 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// 0x522300, opcode 0x25 (vtable slot 362)
// ---------------------------------------------------------------------------------------------------------------------

uint32_t PrimPersonObject::Prim25(void *frame, const PrimParamView *param)
{
    (void)param;
    const uint32_t stackObject = PCall<uint16_t>(frame, 12);
    return PCall<uint8_t>(&mPersonVTable, 137, stackObject) != 0 ? 1u : 0u;
}

// ---------------------------------------------------------------------------------------------------------------------
// 0x525520, opcode 0x1e selector 0
// ---------------------------------------------------------------------------------------------------------------------

uint32_t PrimPersonObject::Prim1E0(void *frame, const PrimParamView *param)
{
    (void)param;
    if (PCall<int16_t>(frame, 12) == 0) PRaise(0x15);

    void *const list = PCall<void *>(mEnvironment, 26);
    const int32_t stackId = PCall<int16_t>(frame, 12);
    void *const person = PCall<void *>(list, 29, stackId);
    if (!person) return 0;
    void *const sub = PCall<void *>(person, 87, 1u);
    if (!sub) return 0;

    const int32_t index = PCall<int16_t>(this, 13, 0x14u);
    if (index == -1) PRaise(0x91);
    void *const entry = PCall<void *>(sub, 13, index);
    if (!entry) PRaise(0x16);
    const uint32_t kind = PCall<uint16_t>(entry, 76);
    if (PCall<uint8_t>(this, 248, person, static_cast<void *>(mActionArguments), kind, 0u, 1u) == 0) PRaise(0x44f);

    // The new object lives in the frame from here on (unwind state 0).
    PHeld created;
    created.mArmed = true;
    void *const director = GetDirectorFrameworkObject();
    PCall<uint32_t>(director, 3, 0xfc5ecc3du, 0x5aca6692u, &created.mObject);

    // (The original passes null for the subobject when `this` is null: never the case for a handler.)
    PCall<void>(created.mObject, 8, static_cast<void *>(&mPersonVTable), person, index, 0x14u, 0u);
    PCall<void>(created.mObject, 27, static_cast<void *>(mActionArguments));
    PCall<void>(created.mObject, 26, static_cast<void *>(&mActionName));
    PCall<void>(created.mObject, 48, 1u);
    PCall<void>(created.mObject, 50, 1u);
    PCall<void>(created.mObject, 72, mActionParameterB, mActionParameterA);
    const uint32_t listed = PCall<uint32_t>(list, 29, static_cast<uint32_t>(mActionParameterC));
    PCall<void>(created.mObject, 65, listed);
    StartInteraction(created.mObject);

    if (void *const obj = created.mObject) {
        void *const p24 = PCall<void *>(obj, 24);
        if (p24 && PCall<uint8_t>(p24, 94) != 0) {
            void *const q = PCall<void *>(obj, 16);
            if (q && PCall<int32_t>(q, 98) != 2) {
                void *target = q;
                void *bits = PCall<void *>(q, 64);
                if (PCall<uint8_t>(q, 104) != 0) {
                    PHeld iface;
                    iface.mArmed = true; // unwind state 1
                    PCall<uint32_t>(q, 0, 0x5a6216f6u, &iface.mObject);
                    if (iface.mObject) {
                        void *const step = PCall<void *>(iface.mObject, 6);
                        target = PCall<void *>(step, 11);
                        void *const a = PCall<void *>(q, 61);
                        void *const b = PCall<void *>(a, 27);
                        bits = PCall<void *>(b, 4);
                    }
                    iface.ReleaseNow();
                }
                if (PCall<uint8_t>(bits, 3, 0x19u) & 1) {
                    if (void *const counters = PCall<void *>(target, 109)) {
                        const int32_t slot = static_cast<int16_t>(PCall<uint16_t>(&mPersonVTable, 57));
                        const uint32_t value = PCall<uint16_t>(target, 47);
                        bool proceed = true;
                        if (PCall<uint32_t>(counters, 5, slot) <= 1) {
                            PCall<void>(counters, 6, static_cast<int32_t>(static_cast<int16_t>(value)), slot, 2u);
                            if (PCall<uint32_t>(counters, 5, slot) <= 1) {
                                proceed = false;
                            } else {
                                PCall<void>(counters, 10, slot, 1u, 0u);
                            }
                        }
                        if (proceed) {
                            const uint32_t count = PCall<uint32_t>(counters, 9, slot, 1u);
                            if (count < 100) PCall<void>(counters, 10, slot, 1u, count + 1);
                        }
                    }
                }
            }
        }
    }
    created.ReleaseNow();
    return 3;
}
