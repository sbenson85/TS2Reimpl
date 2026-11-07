#pragma once
#include <cstddef>
#include <cstdint>

// SimAntics primitive 2, Expression (1.18.0.187): the handler 0x5890c0 that cTSObject::vf332 (0x534650) runs for
// opcode 2, and the small types it uses. The operand block is an ExpressionParam (an XPrimParam, see
// SimAnticsParams.h): two data references (owner, index) and an operator byte. The handler reads both through
// the object's secondary vtable (this + 4), slot 7, which fills a SimDataRef.

// A reference to a run of 16-bit values (the 12-byte cursor the data-owner switch cTSObject::vf4_7 0x531700
// fills): the handler's local "lhs" and "rhs". Literal and read-only owners fill nothing but the range.
//   +0 mLength  how many 16-bit values the run holds
//   +2 mIndex   which one mPtr currently points at
//   +4 mPtr     the value itself, null when the owner has no 16-bit storage (then the handler may use a float)
//   +8 mMin, +0xa mMax  the range a Set may store (reset: the whole int16 range)
struct SimDataRef {
    int16_t mLength;
    int16_t mIndex;
    int16_t *mPtr;
    int16_t mMin;
    int16_t mMax;

    SimDataRef *Reset();       // 0x57c8d0 (__thiscall): length, index, pointer = 0; range = full; returns this
    bool HasData() const;      // 0x57cc90 (shared with cTSBadge::vf8_7): mPtr != null (al only)
    int32_t Get() const;       // 0x512030: the value (0 without storage) clamped into [mMin, mMax] (sign-extended)
    bool Set(int32_t value);   // 0x57ed80 (ret 4): stores the low 16 bits when in range; false without storage/out of range
    bool HasNext() const;      // 0x57d6b0: storage and mIndex + 1 < mLength (al only)
    SimDataRef *Advance();     // 0x57cdc0: when HasNext-like (storage and index + 1 < length): range = full, index++, mPtr += 2 bytes
};
static_assert(sizeof(SimDataRef) == 12, "SimDataRef layout");
static_assert(offsetof(SimDataRef, mPtr) == 4, "SimDataRef layout");
static_assert(offsetof(SimDataRef, mMin) == 8, "SimDataRef layout");
static_assert(offsetof(SimDataRef, mMax) == 0xa, "SimDataRef layout");

// The operand block of the Expression primitive (same 0x18 bytes as every *Param; names for this primitive).
struct ExpressionParam {
    const void *mVTable;  // +0x00
    uint16_t mLhsData;    // +0x04 data index of the left operand (the destination)
    uint16_t mRhsData;    // +0x06 data index of the right operand
    uint8_t mByte8;       // +0x08 not used by the handler
    uint8_t mOperator;    // +0x09 0..0x15, see ExpressionOperator
    uint8_t mLhsOwner;    // +0x0a data owner of the left operand
    uint8_t mRhsOwner;    // +0x0b data owner of the right operand (7: literal)
    uint8_t mRest[8];     // +0x0c .. +0x13
    uint8_t mVersion;     // +0x14
    uint8_t mSpare[3];    // +0x15

    uint8_t *OperatorPtr();  // 0x57e4c0 (4 bytes, not hooked): &mOperator
    uint8_t *RhsOwnerPtr();  // 0x57e950 (4 bytes, not hooked): &mRhsOwner
};
static_assert(sizeof(ExpressionParam) == 0x18, "ExpressionParam layout");
static_assert(offsetof(ExpressionParam, mOperator) == 9, "ExpressionParam layout");
static_assert(offsetof(ExpressionParam, mRhsOwner) == 0xb, "ExpressionParam layout");

// 0x141120 (__cdecl, 5 bytes, hot, not hooked): returns its argument. The compiler used it as a cast between the
// frame/param pointers and the base the accessors take.
void *ParamIdentity(void *p);

// The operators of the Expression primitive (ExpressionParam::mOperator). "lhs" is the value read from the left
// operand, "rhs" from the right; writes go to the left operand's storage (or its float, see Execute).
enum ExpressionOperator : uint8_t {
    kExprGreater = 0,        // lhs > rhs
    kExprLess = 1,           // lhs < rhs (and a check on lhs == 0x89, see Execute)
    kExprEqual = 2,          // lhs == rhs
    kExprAdd = 3,            // lhs += rhs
    kExprSubtract = 4,       // lhs -= rhs
    kExprAssign = 5,         // lhs = rhs
    kExprMultiply = 6,       // lhs *= rhs
    kExprDivide = 7,         // lhs /= rhs (error 0x26 for rhs == 0)
    kExprTestBit = 8,        // lhs has bit rhs (1-based, rhs <= 16, else error 0xd)
    kExprSetBit = 9,         // lhs |= 1 << (rhs - 1)
    kExprClearBit = 10,      // lhs &= ~(1 << (rhs - 1))
    kExprIncrementLess = 11, // ++lhs, then lhs < rhs
    kExprModulo = 12,        // lhs = |lhs| % |rhs| (error 0x26 for rhs == 0)
    kExprAnd = 13,           // lhs &= rhs
    kExprGreaterEqual = 14,  // lhs >= rhs
    kExprLessEqual = 15,     // lhs <= rhs
    kExprNotEqual = 16,      // lhs != rhs
    kExprDecrementGreater = 17, // --lhs, then lhs > rhs
    kExprOr = 18,            // lhs |= rhs
    kExprXor = 19,           // lhs ^= rhs
    kExprAssignAbs = 20,     // lhs = |rhs|
    kExprCopyTwo = 21,       // copy two consecutive 16-bit values from rhs's run to lhs's run
};

// The object the handler runs on (a cTSObject; the layout is not modelled: it reads the pointer at +0x40 and the
// secondary subobject at +4).
struct ExpressionPrimitive {
    // 0x5890c0 (__thiscall, ret 8): `frame` is the cTSTreeStackElem (slot 16 gives a byte the data switch gets),
    // `param` the node's ExpressionParam. Returns 1 or 0 (the branch taken: 1 true target, 0 false target).
    // Errors are thrown as C++ `short` exceptions: 1 unknown operator, 8 no object for the person/owner pair,
    // 0xd bit number out of range, 0x13 operand has no storage, 0x14 the store was refused (out of range),
    // 0x26 division by zero, 0x78 copy without storage on the right side.
    int32_t Execute(void *frame, ExpressionParam *param);
};
