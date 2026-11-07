#include "game/vtable.h"
#include "simantics/PrimHandlers.h"

uint32_t PrimObject::Prim07(void *frame, const PrimParamView *param)
{
    using GetSlot = void *(__thiscall *)(void *);
    void *result = nullptr;
    void *owner = mSubobject40;
    // Called whatever the selector says.
    void *finder = VirtualCall<GetSlot, 26>(owner);

    const int sel = static_cast<int16_t>(param->mWord4);
    if (sel == 0) {
        result = this;
    } else if (sel == 1) {
        // The frame's slot 12 returns a word (ax), sign-extended into the argument.
        const int32_t word = static_cast<int16_t>(VirtualCall<int32_t(__thiscall *)(void *), 12>(frame));
        result = VirtualCall<void *(__thiscall *)(void *, int32_t), 29>(finder, word);
        if (!result) throw static_cast<short>(0x15);
    } else {
        throw static_cast<short>(3);
    }
    if (!result) throw static_cast<short>(3);

    const int mode = static_cast<int16_t>(param->mWord6);
    if (mode != 0 && mode != 1 && mode != 2) throw static_cast<short>(3);
    return 1;
}

uint32_t PrimObject::Prim0F(void * /*frame*/, const PrimParamView *param)
{
    const int16_t value = static_cast<int16_t>(ReadOperand(param->mWord6, param->mWord4, nullptr, nullptr));
    // Bit 0 of the byte at +8 (the original calls 0x57e910, which returns that bit; it is verified, but is
    // a method of an unrelated class, so the one-line test is written out here).
    const bool flag = (param->mWord8 & 1) != 0;
    if (!flag && value != 0) throw static_cast<short>(0x3e);
    return 1;
}
