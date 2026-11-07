#include "simantics/PrimR.h"
#include "game/vtable.h"
#include "ifaces/cTSObject.h"

#include "simantics/EdithObjectTestSim.h"
#include "rz/StaticGetters.h"
#include "simantics/PrimRCommon.h"

using namespace primr;

// ---------------------------------------------------------------------------------------------------------
// Opcode 0x0d
// ---------------------------------------------------------------------------------------------------------

uint32_t PrimRObject::Prim0D(void *frame, const PrimParamView *param)
{
    const uint8_t flags5 = Byte(param, 5);
    void *table = Call<void *>(mContext, 26);
    Call<void>(this, 14, 0x34u, 0u);
    // Records why the push failed (this's slot 14 with 0x34 and a reason code).
    auto fail = [&](uint32_t reason) { Call<void>(this, 14, 0x34u, reason); };

    Held callerInterface;
    void *caller = Call<void *>(table, 29, static_cast<int32_t>(Call<int16_t>(frame, 12)));
    if (!caller) Raise(0x15);
    Call<uint32_t>(caller, 0, 0x5a69d3d5u, &callerInterface.mRef.mObject);
    if (Call<int32_t>(caller, 98) != 2 || !callerInterface.Get()) Raise(0x1c);

    // The object the interaction goes to.
    int16_t targetId;
    if (Byte(param, 7) & 2) {
        if (flags5 >= Call<uint8_t>(frame, 27)) Raise(0x33);
        targetId = Call<int16_t>(frame, 31, static_cast<uint32_t>(flags5));
    } else if (Byte(param, 0x12) & 2) {
        targetId = Call<int16_t>(callerInterface.Get(), 10, 0x35u, 1u);
    } else {
        if (flags5 >= Call<uint8_t>(frame, 26)) Raise(8);
        targetId = Call<int16_t>(frame, 30, static_cast<uint32_t>(flags5));
    }
    void *target = Call<void *>(table, 29, static_cast<int32_t>(targetId));
    if (!target) Raise(0x17);
    if (!Call<void *>(target, 61)) return 0;

    // Which interaction (an index into the target's list).
    int32_t index;
    if (Byte(param, 7) & 0x10) {
        index = Read(this, Byte(param, 9), Word(param, 0xa), nullptr, nullptr);
    } else if (Byte(param, 0x12) & 2) {
        index = Call<int16_t>(caller, 13, 0x14u);
    } else {
        index = Byte(param, 4);
    }
    void *list = Call<void *>(target, 87, 1u);
    if (!list) Raise(0x1f);
    if (!Call<uint32_t>(list, 13, static_cast<uint32_t>(index))) Raise(0x16);

    // The priority.
    int32_t priority = 1;
    switch (Byte(param, 6)) {
    case 0:
        if (As<ifaces::cTSObject>(this)->ObjectKind() == 2) {
            Held own;
            Call<uint32_t>(this, 0, 0x5a69d3d5u, &own.mRef.mObject);
            if (own.Get()) {
                if (Call<int16_t>(own.Get(), 10, 0x21u, 1u) == 0) {
                    fail(1);
                    return 0;
                }
                priority = Call<int16_t>(own.Get(), 10, 0x21u, 1u);
            }
        }
        break;
    case 1: priority = 100; break;
    case 2: priority = 0x14; break;
    case 3: priority = 0x32; break;
    default: Raise(0x2d);
    }

    // The object that takes the new interaction (the director hands it out).
    Held notifier;
    {
        void *director = GetDirectorFrameworkObject();
        notifier.mRef.Reset();
        Call<uint8_t>(director, 3, 0xfc5ecc3du, 0x5aca6692u, &notifier.mRef.mObject);
    }
    Call<void>(notifier.Get(), 8, callerInterface.Get(), target, static_cast<uint32_t>(index), static_cast<uint32_t>(priority),
               static_cast<void *>(this));
    Call<void>(notifier.Get(), 46, static_cast<uint32_t>((Byte(param, 7) >> 3) & 1));

    if (Byte(param, 0x12) & 1) {
        // Up to four argument words from the frame; the others stay unset, as in the original.
        uint16_t words[4];
        uint32_t count = 4;
        if (Call<uint8_t>(frame, 26) < 4) count = Call<uint8_t>(frame, 26);
        for (uint32_t i = 0; i < count; ++i) words[i] = static_cast<uint16_t>(Call<int16_t>(frame, 30, i));
        Call<void>(notifier.Get(), 27, words);
    }

    if (Call<int16_t>(notifier.Get(), 20) != 0) {
        if (!Call<void *>(callerInterface.Get(), 132)) Raise(0x6f);
        void *inner = Call<void *>(callerInterface.Get(), 132);
        if (Call<int16_t>(inner, 19) == 0) Raise(0x6f);
    }

    TestSimScope sim;
    {
        void *ownerObject = mOwner;
        const uint32_t ownerValue = Call<uint8_t>(ownerObject, 53);
        sim->Begin(callerInterface.Get(), target, static_cast<uint32_t>((Byte(param, 0x12) >> 1) & 1), ownerValue);
    }
    // The original keeps a dword here of which only the low byte is set; its upper bytes are the target pointer's
    // (stale stack) and are passed on as the check runner's `runTree` argument and left in the slot-147 output below.
    const uint8_t bit5 = (Byte(param, 7) >> 5) & 1;
    uint32_t stale34 = (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target)) & ~0xffu) | bit5;
    {
        // Slot 63 takes no arguments: the three values the original pushes before the call (stale34, 0, 1) are the last
        // three arguments of the check runner's call that follows.
        void *ownerObject = mOwner;
        void *const recorder = Call<void *>(ownerObject, 63);
        sim->RunCheckTree(notifier.Get(), 2, recorder, stale34, 0, 1);
    }
    if (!Call<uint8_t>(notifier.Get(), 39)) {
        fail(2);
        return 0;
    }

    uint8_t resumable = 0;
    if (Call<int32_t>(notifier.Get(), 18) > -1) {
        void *record = Call<void *>(notifier.Get(), 24);
        if (!record) {
            fail(5);
            return 0;
        }
        resumable = Call<uint8_t>(record, 16);
    }
    Call<void>(notifier.Get(), 34, static_cast<uint32_t>((Byte(param, 7) >> 2) & 1));
    Call<void>(notifier.Get(), 36, static_cast<uint32_t>((Byte(param, 0x12) >> 4) & 1));
    if (Call<uint8_t>(notifier.Get(), 33) == 0 && resumable == 0) {
        if (Call<int16_t>(notifier.Get(), 20) != 0) {
            if (Call<int16_t>(callerInterface.Get(), 127) >= 5) {
                fail(4);
                return 0;
            }
        } else if (Call<int16_t>(callerInterface.Get(), 126) >= 7) {
            if (!Call<uint8_t>(callerInterface.Get(), 128, static_cast<uint32_t>(priority))) {
                fail(3);
                return 0;
            }
        }
    }

    if (Byte(param, 7) & 1) {
        const uint32_t slot = Byte(param, 8);
        stale34 = slot;
        if (slot >= Call<uint8_t>(frame, 27)) Raise(0x33);
        const int16_t id = Call<int16_t>(frame, 31, slot);
        void *object = Call<void *>(table, 29, static_cast<int32_t>(id));
        Call<void>(notifier.Get(), 65, object);
    }

    uint32_t pair = 0;
    if (Byte(param, 0x12) & 4) {
        const int32_t high = As<ifaces::cTSObject>(this)->GetArgument(5u);
        const uint32_t low = static_cast<uint16_t>(As<ifaces::cTSObject>(this)->GetArgument(4u));
        pair = (static_cast<uint32_t>(high) << 16) | low;
    }
    uint32_t value = Byte(param, 0x13);
    if (Byte(param, 0x12) & 8) value = static_cast<uint32_t>(static_cast<int32_t>(As<ifaces::cTSObject>(this)->GetArgument(6u)));

    if (Byte(param, 0x12) & 2) {
        Held own;
        Call<uint32_t>(this, 0, 0x5a69d3d5u, &own.mRef.mObject);
        if (own.Get()) {
            // The interface hands back three values through pointers (the new value, the pair and an object number).
            Call<void>(own.Get(), 147, &value, &pair, &stale34);
            void *object = Call<void *>(table, 29, stale34);
            Call<void>(notifier.Get(), 65, object);
            Call<void>(notifier.Get(), 27, Call<void *>(own.Get(), 148));
            Call<void>(notifier.Get(), 48, 1u);
            Call<void>(notifier.Get(), 50, 1u);
        }
    }
    Call<void>(notifier.Get(), 72, pair, value);

    if (Byte(param, 7) & 0x40) {
        const uint16_t number = static_cast<uint16_t>(Read(this, Byte(param, 0xc), Word(param, 0xd), nullptr, nullptr));
        void *entry = Call<void *>(notifier.Get(), 71, static_cast<uint32_t>(number));
        if (entry) {
            const uint16_t a = static_cast<uint16_t>(Call<int16_t>(entry, 69));
            Call<void>(notifier.Get(), 70, static_cast<uint32_t>(a));
            const uint16_t b = static_cast<uint16_t>(Call<int16_t>(notifier.Get(), 66));
            Call<void>(entry, 70, static_cast<uint32_t>(b));
        }
    }

    if (static_cast<int8_t>(Byte(param, 7)) < 0) {
        SimDataRef dest;
        dest.mLength = 0;
        dest.mIndex = 0;
        dest.mPtr = nullptr;
        dest.mMin = -32768;
        dest.mMax = 32767;
        Read(this, Byte(param, 0xf), Word(param, 0x10), &dest, nullptr);
        uint16_t result = static_cast<uint16_t>(Call<int16_t>(notifier.Get(), 68));
        if (result == 0) {
            result = static_cast<uint16_t>(Call<int16_t>(notifier.Get(), 66));
            Call<void>(notifier.Get(), 70, static_cast<uint32_t>(result));
        }
        if (dest.mPtr) {
            if (static_cast<int16_t>(result) < dest.mMin || static_cast<int16_t>(result) > dest.mMax) Raise(0x14);
            *dest.mPtr = static_cast<int16_t>(result);
        }
    }

    if (As<ifaces::cTSObject>(this)->ObjectKind() == 2 && !(Byte(param, 0x12) & 2)) {
        Held own;
        Call<uint32_t>(this, 0, 0x5a69d3d5u, &own.mRef.mObject);
        // ORIGINAL-BUG: the result of the interface query is not checked; an object without it is read through a null
        // pointer here.
        if (Call<void *>(own.Get(), 132)) {
            void *inner = Call<void *>(own.Get(), 132);
            const uint8_t id = Call<uint8_t>(inner, 47);
            Call<void>(notifier.Get(), 48, static_cast<uint32_t>(id));
        }
    }

    if (Call<uint8_t>(callerInterface.Get(), 122, notifier.Get())) return 1;
    fail(6);
    return 0;
}
