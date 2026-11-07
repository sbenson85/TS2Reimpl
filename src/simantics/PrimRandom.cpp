#include "game/vtable.h"
#include "rz/cRZRandom.h"
#include "simantics/PrimHandlers.h"

uint32_t PrimObject::Random(void * /*frame*/, const PrimParamView *param)
{
    PrimShortDest dest;
    dest.Construct();
    // The reader zeroes the float destination pointer itself; the original leaves the local unset until then.
    float *floatDest = nullptr;

    // Operand 1: the destination. The reader's return value is not used.
    ReadOperand(param->mWord6, param->mWord4, &dest, &floatDest);
    // Operand 2: the bound, a signed 16-bit value.
    const int16_t bound = static_cast<int16_t>(ReadOperand(param->mWordA, param->mWord8, nullptr, nullptr));

    auto draw = [&]() {
        // The owner's slot 55 hands back the generator; the bound is sign-extended to 32 bits.
        auto *rng = VirtualCall<cRZRandom *(__thiscall *)(void *), 55>(mRandomOwner);
        return rng->RandomUint32Uniform(static_cast<uint32_t>(static_cast<int32_t>(bound)));
    };

    if (bound == 0) {
        if (dest.HasTarget() && !dest.Set(0)) throw static_cast<short>(0x14);
        return 1;
    }
    if (dest.HasTarget()) {
        if (!dest.Set(static_cast<int32_t>(draw()))) throw static_cast<short>(0x14);
    } else if (floatDest) {
        uint32_t v = draw();
        *floatDest = static_cast<float>(static_cast<double>(v));
    } else {
        throw static_cast<short>(0x13);
    }
    return 1;
}
