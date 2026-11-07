#include "simantics/PrimHandlers.h"

#include "game/vtable.h"

PrimShortDest *PrimShortDest::Construct()
{
    mUnknown0 = 0;
    mTarget = nullptr;
    mMin = -32768; // the dword at +8 is 0x7fff8000
    mMax = 32767;
    return this;
}

bool PrimShortDest::HasTarget() const
{
    return mTarget != nullptr;
}

bool PrimShortDest::Set(int32_t value)
{
    // The argument is a dword on the stack; only its low word is read, and the range is compared as
    // signed 16-bit values.
    int16_t v = static_cast<int16_t>(value);
    if (!mTarget || v < mMin || v > mMax) return false;
    *mTarget = v;
    return true;
}

int32_t PrimObject::ReadOperand(uint32_t scope, uint32_t id, PrimShortDest *dest, float **floatDest)
{
    using Fn = int32_t(__thiscall *)(void *, uint32_t, uint32_t, PrimShortDest *, float **, uint32_t, uint32_t, uint32_t);
    void *sub = &mTreeSimVTable;
    return VirtualCall<Fn, 7>(sub, scope, id, dest, floatDest, 0, 0, 0);
}
