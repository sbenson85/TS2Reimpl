#include "simantics/PrimExpression.h"

#include "game/vtable.h"
#include "rz/RZFrameworkHelpers.h"

#include <immintrin.h>

// ---------------------------------------------------------------------------------------------------------
// SimDataRef
// ---------------------------------------------------------------------------------------------------------

SimDataRef *SimDataRef::Reset()
{
    mLength = 0;
    mIndex = 0;
    mPtr = nullptr;
    mMin = INT16_MIN;
    mMax = INT16_MAX;
    return this;
}

bool SimDataRef::HasData() const
{
    return mPtr != nullptr;
}

int32_t SimDataRef::Get() const
{
    int32_t value = mPtr ? static_cast<int32_t>(*mPtr) : 0;
    if (value < mMin) value = mMin;
    if (value > mMax) value = mMax;
    return value;
}

bool SimDataRef::Set(int32_t value)
{
    if (!mPtr) return false;
    const int16_t v = static_cast<int16_t>(value);
    if (v < mMin || v > mMax) return false;
    *mPtr = v;
    return true;
}

bool SimDataRef::HasNext() const
{
    return mPtr && mIndex + 1 < mLength;
}

SimDataRef *SimDataRef::Advance()
{
    if (HasNext()) {
        mMin = INT16_MIN;
        mMax = INT16_MAX;
        mIndex = static_cast<int16_t>(mIndex + 1);
        mPtr += 1;
    }
    return this;
}

uint8_t *ExpressionParam::OperatorPtr()
{
    return &mOperator;
}

uint8_t *ExpressionParam::RhsOwnerPtr()
{
    return &mRhsOwner;
}

void *ParamIdentity(void *p)
{
    return p;
}

// ---------------------------------------------------------------------------------------------------------
// The handler
// ---------------------------------------------------------------------------------------------------------

namespace {

// The secondary vtable's slot 7 (cTSObject::vf4_7 0x531700, ret 0x1c): reads one data value. Fills `ref` with
// where the value lives; `fpOut` (may be null) gets the owner's float for float-backed data (else null), `out5`
// (may be null) is zeroed. Returns the value read.
using ReadDataFn = int16_t(__thiscall *)(void *self, uint32_t owner, uint32_t data, SimDataRef *ref, float **fpOut,
                                         uint32_t *out5, uint32_t byteArg, uint32_t flagArg);
constexpr int kReadDataSlot = 7;
constexpr int kSecondaryNewestSlot = 20; // secondary slot 20: returns an object whose slot 12 gives a 16-bit id
constexpr int kFrameByteSlot = 16;       // the stack frame's slot 16: a byte for the read
constexpr int kContextSlotPeople = 26;   // object at +0x40, slot 26: returns the table object
constexpr int kContextSlotCurrent = 24;  // object at +0x40, slot 24: returns the object whose slot 56 is polled
constexpr int kTableLookupSlot = 29;     // table object, slot 29(id): returns the object to notify
constexpr int kNotifySlot = 56;          // the slot called on the notified object / polled object

// Operators that store into the left operand (1) or only compare/test (0); the table the original switches on.
constexpr uint8_t kStoresResult[0x16] = {0, 0, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1};

// The original's error throw: a C++ exception of type `short` (RTTI name ".F").
[[noreturn]] void Raise(int16_t code)
{
    throw code;
}

// The smart pointer the original keeps while it notifies an object: it is released when the scope ends, also
// when the call in between throws (the original's one unwind region).
struct HeldObject {
    RZUnknownSlot slot{nullptr};
    ~HeldObject()
    {
        if (slot.mObject) VirtualCall<uint32_t(__thiscall *)(void *), 2>(slot.mObject);
    }
};

// 1 << (bit - 1) with the x86 shift's count masking (a bit number of 0 shifts by 31). Kept out of line: the
// optimiser otherwise narrows `x | mask` to a 16-bit bts when only the low 16 bits are stored, which makes
// bit 31 wrap to bit 15.
__declspec(noinline) uint32_t BitMask(int16_t bit)
{
    return 1u << ((bit - 1) & 31);
}

int32_t Abs32(int32_t v)
{
    const int32_t sign = v >> 31;
    return (v ^ sign) - sign;
}

} // namespace

int32_t ExpressionPrimitive::Execute(void *frame, ExpressionParam *param)
{
    uint8_t *const self = reinterpret_cast<uint8_t *>(this);
    void *const secondary = self + 4;

    SimDataRef lhsRef;
    SimDataRef rhsRef;
    lhsRef.Reset();
    rhsRef.Reset();

    const uint8_t frameByte = VirtualCall<uint8_t(__thiscall *)(void *), kFrameByteSlot>(frame);
    bool lessCheck = false; // set only by operator 1 (see the end)
    const uint32_t op = *param->OperatorPtr();
    const bool stores = op < 0x16 && kStoresResult[op] != 0;

    // Right operand first, then the left (its call also gets the float slot and the frame byte).
    int16_t rhs = VirtualCall<ReadDataFn, kReadDataSlot>(secondary, *param->RhsOwnerPtr(), param->mRhsData,
                                                                    &rhsRef, nullptr, nullptr, 0, 1);
    // The float pointer and the scratch word are written by the callee (it zeroes both when it has no float).
    float *fp = nullptr;
    uint32_t scratch = 0;
    int16_t lhs = VirtualCall<ReadDataFn, kReadDataSlot>(secondary, param->mLhsOwner, param->mLhsData,
                                                                    &lhsRef, &fp, &scratch, frameByte, stores ? 0 : 1);

    int32_t result = -1;
    switch (op) {
    case kExprGreater: result = lhs > rhs; break;
    case kExprLess:
        lessCheck = true;
        result = lhs < rhs;
        break;
    case kExprEqual: result = lhs == rhs; break;
    case kExprGreaterEqual: result = lhs >= rhs; break;
    case kExprLessEqual: result = lhs <= rhs; break;
    case kExprNotEqual: result = lhs != rhs; break;
    case kExprIncrementLess:
        if (lhsRef.HasData()) {
            if (!lhsRef.Set(lhsRef.Get() + 1)) Raise(0x14);
            lhs = static_cast<int16_t>(lhsRef.Get());
        } else if (fp) {
            *fp = *fp + 1.0f;
            lhs = static_cast<int16_t>(_mm_cvttss_si32(_mm_set_ss(*fp)));
        } else {
            Raise(0x13);
        }
        result = lhs < rhs;
        break;
    case kExprDecrementGreater:
        if (lhsRef.HasData()) {
            if (!lhsRef.Set(lhsRef.Get() - 1)) Raise(0x14);
            lhs = static_cast<int16_t>(lhsRef.Get());
        } else if (fp) {
            *fp = *fp - 1.0f;
            lhs = static_cast<int16_t>(_mm_cvttss_si32(_mm_set_ss(*fp)));
        } else {
            Raise(0x13);
        }
        result = lhs > rhs;
        break;
    case kExprAdd:
        if (lhsRef.HasData()) {
            if (!lhsRef.Set(lhsRef.Get() + rhs)) Raise(0x14);
        } else if (fp) {
            *fp = static_cast<float>(rhs) + *fp;
        } else {
            Raise(0x13);
        }
        result = 1;
        break;
    case kExprSubtract:
        if (lhsRef.HasData()) {
            if (!lhsRef.Set(lhsRef.Get() - rhs)) Raise(0x14);
        } else if (fp) {
            *fp = *fp - static_cast<float>(rhs);
        } else {
            Raise(0x13);
        }
        result = 1;
        break;
    case kExprAssign:
        if (lhsRef.HasData()) {
            if (!lhsRef.Set(static_cast<uint16_t>(rhs))) Raise(0x14);
        } else if (fp) {
            *fp = static_cast<float>(rhs);
        } else {
            Raise(0x13);
        }
        result = 1;
        break;
    case kExprMultiply:
        if (lhsRef.HasData()) {
            if (!lhsRef.Set(lhsRef.Get() * rhs)) Raise(0x14);
        } else if (fp) {
            *fp = static_cast<float>(rhs) * *fp;
        } else {
            Raise(0x13);
        }
        result = 1;
        break;
    case kExprDivide:
        if (rhs == 0) Raise(0x26);
        if (lhsRef.HasData()) {
            if (!lhsRef.Set(lhsRef.Get() / rhs)) Raise(0x14);
        } else if (fp) {
            *fp = *fp / static_cast<float>(rhs);
        } else {
            Raise(0x13);
        }
        result = 1;
        break;
    case kExprTestBit:
        if (static_cast<uint32_t>(static_cast<int32_t>(rhs)) > 0x10) Raise(0xd);
        // A bit number of 0 shifts by 31 (the x86 shift masks its count).
        result = (static_cast<int32_t>(lhs) & BitMask(rhs)) != 0;
        break;
    case kExprSetBit:
        if (static_cast<uint32_t>(static_cast<int32_t>(rhs)) > 0x10) Raise(0xd);
        if (lhsRef.HasData()) {
            if (!lhsRef.Set(lhsRef.Get() | static_cast<int32_t>(BitMask(rhs)))) Raise(0x14);
        } else {
            Raise(0x13);
        }
        result = 1;
        break;
    case kExprClearBit:
        if (static_cast<uint32_t>(static_cast<int32_t>(rhs)) > 0x10) Raise(0xd);
        if (lhsRef.HasData()) {
            if (!lhsRef.Set(lhsRef.Get() & ~static_cast<int32_t>(BitMask(rhs)))) Raise(0x14);
        } else {
            Raise(0x13);
        }
        result = 1;
        break;
    case kExprAnd:
        if (!lhsRef.HasData()) Raise(0x13);
        if (!lhsRef.Set(lhsRef.Get() & rhs)) Raise(0x14);
        result = 1;
        break;
    case kExprOr:
        if (!lhsRef.HasData()) Raise(0x13);
        if (!lhsRef.Set(lhsRef.Get() | rhs)) Raise(0x14);
        result = 1;
        break;
    case kExprXor:
        if (!lhsRef.HasData()) Raise(0x13);
        if (!lhsRef.Set(lhsRef.Get() ^ rhs)) Raise(0x14);
        result = 1;
        break;
    case kExprModulo: {
        if (!lhsRef.HasData()) Raise(0x13);
        // The magnitude is kept in 16 bits (|-32768| becomes -32768 again), as is the negated divisor.
        const int16_t magnitude = static_cast<int16_t>(Abs32(lhsRef.Get()));
        if (rhs == 0) Raise(0x26);
        if (rhs < 0) rhs = static_cast<int16_t>(-rhs);
        if (!lhsRef.Set(static_cast<int32_t>(magnitude) % static_cast<int32_t>(rhs))) Raise(0x14);
        result = 1;
        break;
    }
    case kExprAssignAbs:
        if (!lhsRef.HasData()) Raise(0x13);
        if (!lhsRef.Set(Abs32(rhs))) Raise(0x14);
        result = 1;
        break;
    case kExprCopyTwo:
        if (!lhsRef.HasData()) Raise(0x13);
        if (!lhsRef.HasNext()) Raise(0x13);
        if (!rhsRef.HasData()) Raise(0x78);
        if (!rhsRef.HasNext()) Raise(0x78);
        lhsRef.Set(static_cast<uint16_t>(rhsRef.Get())); // the stores' results are not checked
        lhsRef.Advance();
        rhsRef.Advance();
        lhsRef.Set(static_cast<uint16_t>(rhsRef.Get()));
        result = 1;
        break;
    default:
        Raise(1);
    }

    // Operator 1 asks the object when the left value is 0x89 (the flag is dropped for any other value).
    if (lessCheck && lhs != 0x89) lessCheck = false;

    if (stores) {
        // After a store, by the left operand's owner: owners 3 and 4 with data index 0x56 notify an object;
        // owners 14, 15 and 17 keep their float within +-100.
        const uint32_t kind = static_cast<uint32_t>(param->mLhsOwner) - 3;
        int action = 2;
        if (kind <= 14) {
            static const uint8_t kActions[15] = {0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 2, 1};
            action = kActions[kind];
        }
        if (action == 0) {
            if (static_cast<int16_t>(param->mLhsData) == 0x56) {
                void *const context = *reinterpret_cast<void **>(self + 0x40);
                void *const table = VirtualCall<void *(__thiscall *)(void *), kContextSlotPeople>(context);
                HeldObject held;
                if (param->mLhsOwner == 3) {
                    held.slot.Assign(this);
                } else {
                    void *const newest = VirtualCall<void *(__thiscall *)(void *), kSecondaryNewestSlot>(secondary);
                    const int16_t id = VirtualCall<int16_t(__thiscall *)(void *), 12>(newest);
                    void *const target = VirtualCall<void *(__thiscall *)(void *, int32_t), kTableLookupSlot>(table, id);
                    held.slot.Assign(target);
                    if (!held.slot.mObject) Raise(8);
                }
                if (held.slot.mObject)
                    VirtualCall<uint32_t(__thiscall *)(void *), kNotifySlot>(held.slot.mObject);
            }
        } else if (action == 1) {
            if (fp) {
                if (-100.0f > *fp)
                    *fp = -100.0f;
                else if (*fp > 100.0f)
                    *fp = 100.0f;
            }
        }
    }

    if (lessCheck) {
        void *const context = *reinterpret_cast<void **>(self + 0x40);
        void *const polled = VirtualCall<void *(__thiscall *)(void *), kContextSlotCurrent>(context);
        if (VirtualCall<uint32_t(__thiscall *)(void *), kNotifySlot>(polled)) result = 0;
    }
    return result;
}
