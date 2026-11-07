// The pixel converter 0x8c6b10 and its 62 per-format loops (src/gz/GZConvertPixels.cpp, docs/subsystems/rendering.md R5b
// step 3). The converter's two callers (cGZDrawSystem::ConvertPixels, cGZBuffer::BltUnclipped) are ours and call it directly;
// the hooks keep the converter and every table-reached loop ours wherever else the image might reach them (the loops are
// reached by data pointers in the table at 0xe4f0d8, which callers.py cannot follow). The loops take one by-value
// argument block (GZConvertArgs, 0x58 bytes, cdecl).
#include "hook/hooks.h"
#include "gz/GZConvertPixels.h"

namespace {

const HookSpec kHooks[] = {
    {"GZConvertPixels", 0x8c6b10, 32, 0xee5b244a, reinterpret_cast<void *>(&GZConvertPixels)},
    {"GZConvertLoop_8c38e0", 0x8c38e0, 32, 0x0a10805c, reinterpret_cast<void *>(&GZConvertLoopHook<0>)},
    {"GZConvertLoop_8c3970", 0x8c3970, 32, 0x00242eb4, reinterpret_cast<void *>(&GZConvertLoopHook<1>)},
    {"GZConvertLoop_8c39c0", 0x8c39c0, 32, 0x6327529c, reinterpret_cast<void *>(&GZConvertLoopHook<2>)},
    {"GZConvertLoop_8c3a30", 0x8c3a30, 32, 0x10a41bbe, reinterpret_cast<void *>(&GZConvertLoopHook<3>)},
    {"GZConvertLoop_8c3a80", 0x8c3a80, 32, 0x4845519c, reinterpret_cast<void *>(&GZConvertLoopHook<4>)},
    {"GZConvertLoop_8c3b00", 0x8c3b00, 32, 0x00242eb4, reinterpret_cast<void *>(&GZConvertLoopHook<5>)},
    {"GZConvertLoop_8c3b40", 0x8c3b40, 32, 0x6327529c, reinterpret_cast<void *>(&GZConvertLoopHook<6>)},
    {"GZConvertLoop_8c3ba0", 0x8c3ba0, 32, 0xe00aab37, reinterpret_cast<void *>(&GZConvertLoopHook<7>)},
    {"GZConvertLoop_8c3bf0", 0x8c3bf0, 32, 0x4845519c, reinterpret_cast<void *>(&GZConvertLoopHook<8>)},
    {"GZConvertLoop_8c3c70", 0x8c3c70, 32, 0x6e544631, reinterpret_cast<void *>(&GZConvertLoopHook<9>)},
    {"GZConvertLoop_8c3cd0", 0x8c3cd0, 32, 0x050bd531, reinterpret_cast<void *>(&GZConvertLoopHook<10>)},
    {"GZConvertLoop_8c3d30", 0x8c3d30, 32, 0x2d5d9b49, reinterpret_cast<void *>(&GZConvertLoopHook<11>)},
    {"GZConvertLoop_8c3da0", 0x8c3da0, 32, 0x39499e6a, reinterpret_cast<void *>(&GZConvertLoopHook<12>)},
    {"GZConvertLoop_8c3e20", 0x8c3e20, 32, 0x00242eb4, reinterpret_cast<void *>(&GZConvertLoopHook<13>)},
    {"GZConvertLoop_8c3e70", 0x8c3e70, 32, 0xe00aab37, reinterpret_cast<void *>(&GZConvertLoopHook<14>)},
    {"GZConvertLoop_8c3ed0", 0x8c3ed0, 32, 0x2d5d9b49, reinterpret_cast<void *>(&GZConvertLoopHook<15>)},
    {"GZConvertLoop_8c3f50", 0x8c3f50, 32, 0x4f380812, reinterpret_cast<void *>(&GZConvertLoopHook<16>)},
    {"GZConvertLoop_8c3fe0", 0x8c3fe0, 32, 0xa515def6, reinterpret_cast<void *>(&GZConvertLoopHook<17>)},
    {"GZConvertLoop_8c4050", 0x8c4050, 32, 0x4845519c, reinterpret_cast<void *>(&GZConvertLoopHook<18>)},
    {"GZConvertLoop_8c40d0", 0x8c40d0, 32, 0x6327529c, reinterpret_cast<void *>(&GZConvertLoopHook<19>)},
    {"GZConvertLoop_8c4150", 0x8c4150, 32, 0x4845519c, reinterpret_cast<void *>(&GZConvertLoopHook<20>)},
    {"GZConvertLoop_8c41e0", 0x8c41e0, 32, 0x8f85fe62, reinterpret_cast<void *>(&GZConvertLoopHook<21>)},
    {"GZConvertLoop_8c4260", 0x8c4260, 32, 0x4f380812, reinterpret_cast<void *>(&GZConvertLoopHook<22>)},
    {"GZConvertLoop_8c42e0", 0x8c42e0, 32, 0x8f85fe62, reinterpret_cast<void *>(&GZConvertLoopHook<23>)},
    {"GZConvertLoop_8c4360", 0x8c4360, 32, 0x4f380812, reinterpret_cast<void *>(&GZConvertLoopHook<24>)},
    {"GZConvertLoop_8c43e0", 0x8c43e0, 32, 0x8f85fe62, reinterpret_cast<void *>(&GZConvertLoopHook<25>)},
    {"GZConvertLoop_8c4460", 0x8c4460, 32, 0x4f380812, reinterpret_cast<void *>(&GZConvertLoopHook<26>)},
    {"GZConvertLoop_8c4570", 0x8c4570, 32, 0xe00aab37, reinterpret_cast<void *>(&GZConvertLoopHook<27>)},
    {"GZConvertLoop_8c45d0", 0x8c45d0, 32, 0xa515def6, reinterpret_cast<void *>(&GZConvertLoopHook<28>)},
    {"GZConvertLoop_8c4640", 0x8c4640, 32, 0x4845519c, reinterpret_cast<void *>(&GZConvertLoopHook<29>)},
    {"GZConvertLoop_8c46c0", 0x8c46c0, 32, 0x6327529c, reinterpret_cast<void *>(&GZConvertLoopHook<30>)},
    {"GZConvertLoop_8c4740", 0x8c4740, 32, 0x4845519c, reinterpret_cast<void *>(&GZConvertLoopHook<31>)},
    {"GZConvertLoop_8c47d0", 0x8c47d0, 32, 0xb22c9066, reinterpret_cast<void *>(&GZConvertLoopHook<32>)},
    {"GZConvertLoop_8c4850", 0x8c4850, 32, 0x00242eb4, reinterpret_cast<void *>(&GZConvertLoopHook<33>)},
    {"GZConvertLoop_8c48a0", 0x8c48a0, 32, 0x4845519c, reinterpret_cast<void *>(&GZConvertLoopHook<34>)},
    {"GZConvertLoop_8c4930", 0x8c4930, 32, 0xbed37729, reinterpret_cast<void *>(&GZConvertLoopHook<35>)},
    {"GZConvertLoop_8c4990", 0x8c4990, 32, 0x66e1ed94, reinterpret_cast<void *>(&GZConvertLoopHook<36>)},
    {"GZConvertLoop_8c4a20", 0x8c4a20, 32, 0x2d5d9b49, reinterpret_cast<void *>(&GZConvertLoopHook<37>)},
    {"GZConvertLoop_8c4a90", 0x8c4a90, 32, 0x8ebfb447, reinterpret_cast<void *>(&GZConvertLoopHook<38>)},
    {"GZConvertLoop_8c4b30", 0x8c4b30, 32, 0x39499e6a, reinterpret_cast<void *>(&GZConvertLoopHook<39>)},
    {"GZConvertLoop_8c4bb0", 0x8c4bb0, 32, 0x8c79485c, reinterpret_cast<void *>(&GZConvertLoopHook<40>)},
    {"GZConvertLoop_8c4c20", 0x8c4c20, 32, 0x2d5d9b49, reinterpret_cast<void *>(&GZConvertLoopHook<41>)},
    {"GZConvertLoop_8c4ca0", 0x8c4ca0, 32, 0x4845519c, reinterpret_cast<void *>(&GZConvertLoopHook<42>)},
    {"GZConvertLoop_8c4d30", 0x8c4d30, 32, 0x0a10805c, reinterpret_cast<void *>(&GZConvertLoopHook<43>)},
    {"GZConvertLoop_8c4dc0", 0x8c4dc0, 32, 0x00242eb4, reinterpret_cast<void *>(&GZConvertLoopHook<44>)},
    {"GZConvertLoop_8c4e10", 0x8c4e10, 32, 0x6327529c, reinterpret_cast<void *>(&GZConvertLoopHook<45>)},
    {"GZConvertLoop_8c4e80", 0x8c4e80, 32, 0x10a41bbe, reinterpret_cast<void *>(&GZConvertLoopHook<46>)},
    {"GZConvertLoop_8c4ed0", 0x8c4ed0, 32, 0x4845519c, reinterpret_cast<void *>(&GZConvertLoopHook<47>)},
    {"GZConvertLoop_8c4f60", 0x8c4f60, 32, 0xb22c9066, reinterpret_cast<void *>(&GZConvertLoopHook<48>)},
    {"GZConvertLoop_8c4fe0", 0x8c4fe0, 32, 0x2d5d9b49, reinterpret_cast<void *>(&GZConvertLoopHook<49>)},
    {"GZConvertLoop_8c5060", 0x8c5060, 32, 0x4845519c, reinterpret_cast<void *>(&GZConvertLoopHook<50>)},
    {"GZConvertLoop_8c50f0", 0x8c50f0, 32, 0xb64e6a44, reinterpret_cast<void *>(&GZConvertLoopHook<51>)},
    {"GZConvertLoop_8c5170", 0x8c5170, 32, 0x66e1ed94, reinterpret_cast<void *>(&GZConvertLoopHook<52>)},
    {"GZConvertLoop_8c5200", 0x8c5200, 32, 0x8ebfb447, reinterpret_cast<void *>(&GZConvertLoopHook<53>)},
    {"GZConvertLoop_8c52a0", 0x8c52a0, 32, 0xe00aab37, reinterpret_cast<void *>(&GZConvertLoopHook<54>)},
    {"GZConvertLoop_8c52f0", 0x8c52f0, 32, 0xb22c9066, reinterpret_cast<void *>(&GZConvertLoopHook<55>)},
    {"GZConvertLoop_8c5370", 0x8c5370, 32, 0x0a10805c, reinterpret_cast<void *>(&GZConvertLoopHook<56>)},
    {"GZConvertLoop_8c53e0", 0x8c53e0, 32, 0x8ebfb447, reinterpret_cast<void *>(&GZConvertLoopHook<57>)},
    {"GZConvertLoop_8c5490", 0x8c5490, 32, 0xb64e6a44, reinterpret_cast<void *>(&GZConvertLoopHook<58>)},
    {"GZConvertLoop_8c5500", 0x8c5500, 32, 0x8c79485c, reinterpret_cast<void *>(&GZConvertLoopHook<59>)},
    {"GZConvertLoop_8c5580", 0x8c5580, 32, 0x2d5d9b49, reinterpret_cast<void *>(&GZConvertLoopHook<60>)},
    {"GZConvertLoop_8c5610", 0x8c5610, 32, 0x4845519c, reinterpret_cast<void *>(&GZConvertLoopHook<61>)},
};
REGISTER_HOOKS(kHooks);

} // namespace
