#include "simantics/PrimGenericSimCallOriginals.h"
#include "rz/StaticGetters2.h"
#include "simantics/PrimGenericSimCall.h"
#include "simantics/PrimXCommon.h"

using primX::Call;
using primX::HeldRef;

// GenericSimCall (opcode 0x01) cases, group A: short bodies that are calls on the object, its context holder and the
// objects those return. Case numbers are the param word +4; every case ends with the value the original leaves in eax
// (the local at ebp-0x18, which the prologue sets to 1 unless a case says otherwise).

namespace {
constexpr uint32_t kDefaultResult = 1;

} // namespace

// Cases 1 and 3 (identical bodies): slot 260 of the object with 0.
uint32_t GenericSimCall::Case01_03()
{
    Call<uint32_t>(mSelf, 260, 0u);
    return kDefaultResult;
}

// Case 2: the stack object (throws 0xa without one); asks the object for its interface 0x5a69d3d5 (throws 0x1c without),
// and when that interface's helper (slot 132) says yes (slot 29, al) hands it the stack object (slot 65) and a new
// 0xcc-byte block obtained from the stack object's slot-64 object (slot 3) (slot 72, first argument 0).
uint32_t GenericSimCall::Case02()
{
    void *const obj = StackObject();
    HeldRef iface;
    Call<uint32_t>(mSelf, 0, 0x5a69d3d5u, &iface.mObject);
    if (!iface.mObject) Throw(0x1c);
    void *const helper = Call<void *>(iface.mObject, 132);
    if (Call<uint8_t>(helper, 29) != 0) {
        Call<void>(helper, 65, obj);
        void *const owner = Call<void *>(obj, 64);
        void *const block = Call<void *>(owner, 3, 0xccu);
        Call<void>(helper, 72, 0u, block);
    }
    return kDefaultResult;
}

// Case 5: result 0 unless the object found for argument 0 (slot 11 of the entry list from the prologue) exists; then its
// slot-60 value, negated, goes to slot 44 of the context's slot-3 object with selector 2. The context's slot 6 is
// called and its result dropped.
uint32_t GenericSimCall::Case05()
{
    uint32_t result = 0;
    ContextSlot(6);
    void *const entry = Call<void *>(mL2c, 11, static_cast<int32_t>(Arg(0)));
    if (entry) {
        void *const target = ContextSlot(3);
        const int32_t value = Call<int32_t>(entry, 60);
        Call<void>(target, 44, 2u, static_cast<uint32_t>(-value));
        result = 1;
    }
    return result;
}

// Case 10: slot 67 of the object's slot-63 object, with the object.
uint32_t GenericSimCall::Case10()
{
    void *const owner = Call<void *>(mSelf, 63);
    Call<void>(owner, 67, mSelf);
    return kDefaultResult;
}

// Case 11: as case 10 with slot 68.
uint32_t GenericSimCall::Case11()
{
    void *const owner = Call<void *>(mSelf, 63);
    Call<void>(owner, 68, mSelf);
    return kDefaultResult;
}

// Case 12: argument 0 = -(first byte) - (second byte) of the pair the stack object's slot 101 fills in.
uint32_t GenericSimCall::Case12()
{
    void *const obj = StackObject();
    // The pair is two signed bytes (read at +0 and +1); the local's real size is not known, so give the callee room.
    alignas(4) int8_t pair[16] = {};
    Call<void>(obj, 101, pair);
    SetArg(0, static_cast<uint32_t>((-static_cast<int32_t>(pair[0])) - static_cast<int32_t>(pair[1])));
    return kDefaultResult;
}

// Case 13: slot 50 of the object's slot-63 object with the stack object (looked up first).
uint32_t GenericSimCall::Case13()
{
    void *const obj = StackObject();
    void *const owner = Call<void *>(mSelf, 63);
    Call<void>(owner, 50, obj);
    return kDefaultResult;
}

// Case 14: slot 16 of the context's slot-3 object with (0x1f, argument 0 as an unsigned word).
uint32_t GenericSimCall::Case14()
{
    void *const target = ContextSlot(3);
    const uint32_t a0 = static_cast<uint16_t>(Arg(0));
    Call<void>(target, 16, 0x1fu, a0);
    return kDefaultResult;
}

// Case 17: argument 2 = -1; when the context's slot-23 object says yes (slot 24, al), argument 2 = 1 if the attribute
// check (0x57ee10) accepts (argument 0, the word the +0x2c8 object's slot 15 returns for 0x28).
uint32_t GenericSimCall::Case17()
{
    SetArg(2, 0xffffffffu);
    void *const holder = ContextSlot(23);
    if (Call<uint8_t>(holder, 24) == 0) return kDefaultResult;
    const int32_t a0 = Arg(0);
    const int32_t attr = Call<int16_t>(mSelf->mField2c8, 15, 0x28u);
    if (CheckAttribute(a0, attr)) SetArg(2, 1);
    return kDefaultResult;
}

// Case 18: ends at once with 2 when the cached singleton B's slot 23 says 0xa. Otherwise on the context's slot-3 object:
// argument 0 above 1: slot 20 (argument 0, 0); equal to 1: slot 25 (1); else slot 24 (1).
uint32_t GenericSimCall::Case18()
{
    void *const target = ContextSlot(3);
    void *const singleton = GetCachedSingletonB();
    if (Call<int32_t>(singleton, 23) == 0xa) return 2;
    if (Arg(0) > 1) {
        Call<void>(target, 20, static_cast<int32_t>(Arg(0)), 0u);
    } else if (Arg(0) == 1) {
        Call<void>(target, 25, 1u);
    } else {
        Call<void>(target, 24, 1u);
    }
    return kDefaultResult;
}

// Case 20: argument 2 = -1; when the context's slot-23 object says yes (slot 24): the attribute tail (see AttributeTail)
// for the entry number in argument 0.
uint32_t GenericSimCall::Case20()
{
    SetArg(2, 0xffffffffu);
    void *const holder = ContextSlot(23);
    if (Call<uint8_t>(holder, 24) == 0) return kDefaultResult;
    AttributeTail(holder, Arg(0), false);
    return kDefaultResult;
}

// Cases 26 and 80: argument 2 = -1; when the cached slot-23 object says yes (slot 24): argument 0 (0 means the +0x2c8
// object's slot 15 for 0x28) and argument 1 go to the attribute check; argument 2 = 1 on a match.
uint32_t GenericSimCall::Case26_80()
{
    SetArg(2, 0xffffffffu);
    void *const holder = GetCachedSlot23();
    if (Call<uint8_t>(holder, 24) == 0) return kDefaultResult;
    int16_t a0 = Arg(0);
    const int16_t a1 = Arg(1);
    if (a0 == 0) a0 = Call<int16_t>(mSelf->mField2c8, 15, 0x28u);
    if (CheckAttribute(a1, a0)) SetArg(2, 1);
    return kDefaultResult;
}

// Case 33: slot 221 of the stack object.
uint32_t GenericSimCall::Case33()
{
    void *const obj = StackObject();
    Call<void>(obj, 221);
    return kDefaultResult;
}

// Case 35: argument 1 = the word slot 8 of (slot 4 of the thing slot 18 of the context's slot-6 object's slot-13 object finds
// for argument 0), or 0xffff when there is none.
uint32_t GenericSimCall::Case35()
{
    uint16_t result = 0xffff;
    const int32_t a0 = Arg(0);
    void *const list = Call<void *>(ContextSlot(6), 13);
    void *const found = Call<void *>(list, 18, a0);
    if (found) {
        void *const inner = Call<void *>(found, 4);
        result = Call<uint16_t>(inner, 8);
    }
    SetArg(1, result);
    return kDefaultResult;
}

// Case 36: argument 1 = the low word of slot 41 of the object that slot 10 of the context's slot-9 object finds for
// argument 0, or 0xffff when there is none.
uint32_t GenericSimCall::Case36()
{
    uint32_t value = 0xffffffffu;
    const int32_t a0 = Arg(0);
    void *const list = ContextSlot(9);
    void *const found = Call<void *>(list, 10, a0);
    if (found) value = Call<uint32_t>(found, 41);
    SetArg(1, value & 0xffff);
    return kDefaultResult;
}

// Case 38: with the context's slot-10 object: the item slot 16 finds for (argument 0, 1), when there is one, goes to
// slot 22 with 1.
uint32_t GenericSimCall::Case38()
{
    const int32_t a0 = Arg(0);
    void *const list = ContextSlot(10);
    if (list) {
        void *const item = Call<void *>(list, 16, a0, 1u);
        if (item) Call<void>(list, 22, item, 1u);
    }
    return kDefaultResult;
}

// Case 63: slot 30 of the cached object 0x211da3 with argument 0.
uint32_t GenericSimCall::Case63()
{
    const int32_t a0 = Arg(0);
    void *const svc = gscorig::CachedObject211da3();
    Call<void>(svc, 30, a0);
    return kDefaultResult;
}

// Case 64: the six argument words go (as unsigned words) to slot 41 of the +0x2c8 object.
uint32_t GenericSimCall::Case64()
{
    uint16_t a[6];
    for (int i = 0; i < 6; ++i) a[i] = static_cast<uint16_t>(Arg(i));
    Call<void>(mSelf->mField2c8, 41, uint32_t(a[0]), uint32_t(a[1]), uint32_t(a[2]), uint32_t(a[3]), uint32_t(a[4]),
               uint32_t(a[5]));
    return kDefaultResult;
}

// Case 65: slot 350 of the object.
uint32_t GenericSimCall::Case65()
{
    Call<void>(mSelf, 350);
    return kDefaultResult;
}

// Case 69: 0x57d860 with 4 (the original's result is the result).
uint32_t GenericSimCall::Case69()
{
    return gscorig::ObjectFlags(mSelf, 4);
}

// Case 71: the object the slot-63 object finds for argument 0 (none throws 0x17); 0 when its slot 121 says yes, else
// whether its slot 88 accepts (argument 1 and 2 as bytes, 1).
uint32_t GenericSimCall::Case71()
{
    const int16_t a0 = Arg(0);
    const int16_t a1 = Arg(1);
    const int16_t a2 = Arg(2);
    void *const owner = Call<void *>(mSelf, 63);
    void *const obj = Call<void *>(owner, 29, static_cast<int32_t>(a0));
    if (!obj) Throw(0x17);
    if (Call<uint8_t>(obj, 121) != 0) return 0;
    const uint8_t ok = Call<uint8_t>(obj, 88, uint32_t(uint8_t(a1)), uint32_t(uint8_t(a2)), 1u);
    return ok != 0 ? 1 : 0;
}

// Case 73: the object the context's slot-26 list finds for argument 0 gets slot 193 with 1 (when there is one).
uint32_t GenericSimCall::Case73()
{
    const int32_t a0 = Arg(0);
    void *const list = ContextSlot(26);
    void *const obj = Call<void *>(list, 29, a0);
    if (obj) Call<void>(obj, 193, 1u);
    return kDefaultResult;
}

// Case 77: 0x57d860 with 0x40.
uint32_t GenericSimCall::Case77()
{
    return gscorig::ObjectFlags(mSelf, 0x40);
}
