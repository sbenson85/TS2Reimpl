#include "simantics/PrimPerson.h"

#include "simantics/EdithObjectTestSim.h"
#include "simantics/PrimPersonCommon.h"

// Opcode 0x11, IdleForInput, 0x521b90 (1,404 bytes). Differential test: tests/simantics_prims_person_idle.cpp. Its helpers
// (the interaction queues, the check-tree runner, the person's bookkeeping members) are ours now: src/simantics/
// PersonInteractions.cpp, RefCountedDeque.cpp, EdithObjectTestSim.cpp.

uint32_t PrimPersonObject::IdleForInput(void *frame, const PrimParamView *param)
{
    const uint8_t *p = reinterpret_cast<const uint8_t *>(param);
    void *const sub = &mPersonVTable;
    const uint16_t inputWord = static_cast<uint16_t>(p[4] | (p[5] << 8));

    if (p[8] & 1) {
        // ---- the input arrived: take the next queued interaction -------------------------------------------------
        if (PCall<uint16_t>(sub, 127) == 0) return 1;

        // The front of the queue at +0x5a8; an empty queue throws a standard range error.
        if (QueueB().Size() == 0) {
            ThrowDequeRangeError();
        }
        {
            void *const frontPtr = QueueB().Front();

            PHeld front;
            front.mObject = frontPtr;
            if (frontPtr) PCall<void>(frontPtr, 1);
            front.mArmed = true;
            QueueB().PopFront();

            void *const tmp = PCall<void *>(frontPtr, 16);
            if (!tmp) {
                ClearActionQueue();
                front.ReleaseNow();
                return 1;
            }

            TestSimScope l1;
            const uint32_t flag = PCall<uint8_t>(mHolder48, 53);
            l1->Begin(sub, tmp, 0u, flag);
            void *const recorder = PCall<void *>(mHolder48, 63);
            l1->RunCheckTree(frontPtr, 2u, recorder, 1u, 0u, 0u);

            if (PCall<uint8_t>(frontPtr, 39) == 0) {
                ClearActionQueue();
                RejectInteraction(frontPtr);
                l1.Destroy();
                front.ReleaseNow();
                return 1;
            }
            const uint32_t count = PCall<uint16_t>(frontPtr, 13);
            if (count == 0) {
                ClearActionQueue();
                RejectInteraction(frontPtr);
                l1.Destroy();
                front.ReleaseNow();
                return 1;
            }
            if (mSelectedInteraction) PCall<void>(mSelectedInteraction, 67, 0u);
            SelectInteraction(frontPtr);
            CountInteractionUse(frontPtr);
            // Slot 15 takes no arguments; slot 248 (cTSObject::vf248, ret 0x14) takes five, the last three pushed before
            // slot 15 is called.
            const uint32_t chosen = PCall<uint32_t>(frontPtr, 15);
            if (PCall<uint8_t>(this, 248, tmp, chosen, count, 0u, 1u) == 0) {
                ClearActionQueue();
                RejectInteraction(frontPtr);
                l1.Destroy();
                front.ReleaseNow();
                return 1;
            }
            void *const target = PCall<void *>(&mTreeSimVTable, 20);
            const uint32_t value = PCall<uint16_t>(tmp, 47);
            PCall<void>(target, 13, value);
            l1.Destroy();
            front.ReleaseNow();
            return 3;
        }
    }

    // ---- polling: count the idle ticks down --------------------------------------------------------------------------
    const uint8_t polled = PCall<uint8_t>(sub, 138);
    if (static_cast<int16_t>(inputWord) < 0) PRaise(8);
    const uint32_t stackCount = PCall<uint8_t>(frame, 26);
    const int32_t index = static_cast<int16_t>(inputWord);
    if (index >= static_cast<int32_t>(stackCount)) PRaise(8);
    uint16_t *const counter = PCall<uint16_t *>(frame, 35, index);

    if (polled == 0) {
        const uint16_t value = *counter;
        if (value == 0) {
            const uint32_t flags = PCall<uint16_t>(this, 13, 8u) & 0xffbfu;
            PCall<void>(this, 14, 8u, flags);
            return 1;
        }
        *counter = static_cast<uint16_t>(value - 1);
        return 2;
    }

    {
        const uint32_t flags = PCall<uint16_t>(this, 13, 8u) | 0x40u;
        PCall<void>(this, 14, 8u, flags);
    }
    if (static_cast<uint16_t>(p[6] | (p[7] << 8)) == 0) return 1;
    if (QueueA().Size() == 0) return 1;

    if (PCall<uint32_t>(frame, 18) == 1) {
        const uint16_t state = PCall<uint16_t>(this, 17, 0x22u);
        void *const firstQueued = *QueueA().At(0);
        if (PCall<uint8_t>(firstQueued, 33) == 0) return 1;
        if (state != 0) return 1;
        if (PCall<uint8_t>(mPreviousInteraction, 35) != 0) return 1;
    }

    // The next entry of the queue at +0x594 (popped here).
    void *const entry = *QueueA().At(0);
    PHeld held;
    held.mObject = entry;
    if (entry) PCall<void>(entry, 1);
    held.mArmed = true;
    QueueA().PopFront();
    ClearActionQueue();
    void *const tmp = PCall<void *>(entry, 16); // (no null check on `entry`, as the original)
    if (!tmp) {
        held.ReleaseNow();
        return 2;
    }

    TestSimScope l2;
    const uint8_t sourceFlag = PCall<uint8_t>(entry, 49);
    const uint32_t flag = PCall<uint8_t>(mHolder48, 53);
    l2->Begin(sub, tmp, sourceFlag != 0 ? 1u : 0u, flag);
    SetUiManager(nullptr);
    void *const recorder = PCall<void *>(mHolder48, 63);
    l2->RunCheckTree(entry, 2u, recorder, 1u, 0u, 0u);
    if (PCall<uint8_t>(entry, 39) == 0) {
        RejectInteraction(entry);
        l2.Destroy();
        held.ReleaseNow();
        return 2;
    }
    const uint32_t count = PCall<uint16_t>(entry, 13);
    if (count == 0) {
        RejectInteraction(entry);
        l2.Destroy();
        held.ReleaseNow();
        return 2;
    }
    const uint32_t chosen = PCall<uint32_t>(entry, 15); // slot 248's arguments as above
    if (PCall<uint8_t>(this, 248, tmp, chosen, count, 0u, 1u) == 0) {
        RejectInteraction(entry);
        l2.Destroy();
        held.ReleaseNow();
        return 2;
    }

    if (PCall<uint8_t>(mCurrentInteraction, 6) == 0) History().PushBack(&mCurrentInteraction);
    StartInteraction(entry);
    CountInteractionUse(entry);
    *counter = 0;
    PCall<void>(frame, 19, 1u);
    void *const target = PCall<void *>(&mTreeSimVTable, 20);
    const uint32_t value = PCall<uint16_t>(tmp, 47);
    PCall<void>(target, 13, value);
    {
        const uint32_t flags = PCall<uint16_t>(this, 13, 8u) | 0x40u;
        PCall<void>(this, 14, 8u, flags);
    }
    l2.Destroy();
    held.ReleaseNow();
    return 3;
}
