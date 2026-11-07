// cGZCOMLibrary's folded slots (see gz_com_support.h), each side through its own vtable: AddRef (slot 1, 0x145260),
// GetDirector (slot 4, 0x145770), GetModule (slot 5, 0x1457d0) and the initialised-flag getter (slot 6, 0x1458b0:
// `mov al, [ecx + 4]`). Folded with other classes' bodies, so not hooked.
#include "gz_com_support.h"

DIFF_TEST(GZCOMLibrary_IsInitialized, 0x1458b0)
{
    for (int value : {0, 1, 2, 0x7f, 0x80, 0xff}) {
        uint8_t r[2];
        for (int side = 0; side < 2; ++side) {
            Lib lib("flag.dll", side);
            Word<uint8_t>(lib.p, 4) = static_cast<uint8_t>(value);
            // The raw byte in al (the upper bits of eax are undefined).
            r[side] = CallLib<uint8_t>(ctx, side, lib.p, 6, kLibIsInit);
        }
        ++ctx.cases;
        if (r[0] != value || r[1] != r[0]) ctx.Fail("IsInitialized(%d): original %d ours %d", value, r[0], r[1]);
    }
}

DIFF_TEST(GZCOMLibrary_Slots, 0x145260)
{
    std::mt19937 rng(17);
    for (int i = 0; i < 40; ++i) {
        int count = i == 0 ? -1 : i == 1 ? INT32_MAX : static_cast<int>(rng());
        void *director = reinterpret_cast<void *>(static_cast<uintptr_t>(rng())), *module = reinterpret_cast<void *>(static_cast<uintptr_t>(rng()));
        uint32_t r[2][3];
        int after[2];
        for (int side = 0; side < 2; ++side) {
            Lib lib("slots.dll", side, false, director, module, count);
            r[side][0] = CallLib<uint32_t>(ctx, side, lib.p, 1, kLibAddRef);
            r[side][1] = CallLib<uint32_t>(ctx, side, lib.p, 4, kLibGetDirector);
            r[side][2] = CallLib<uint32_t>(ctx, side, lib.p, 5, kLibGetModule);
            after[side] = lib->mRefCount;
        }
        ++ctx.cases;
        if (std::memcmp(r[0], r[1], sizeof(r[0])) != 0 || after[0] != after[1])
            ctx.Fail("library slots (%d): AddRef %x/%x, director %x/%x, module %x/%x, count %d/%d", count, r[0][0], r[1][0], r[0][1], r[1][1], r[0][2], r[1][2], after[0], after[1]);
    }
}
