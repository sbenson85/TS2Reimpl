#include "hook/hooks.h"
#include "rz/RZFastFixedAllocators.h"

namespace {

template <uint32_t N> void *Ctor()
{
    return reinterpret_cast<void *>(&rz::FastAllocatorAdapters<rz::cRZFastFixedAllocator2<N>>::Construct);
}

template <uint32_t N> void *Dtor()
{
    return reinterpret_cast<void *>(&rz::FastAllocatorAdapters<rz::cRZFastFixedAllocator2<N>>::Destruct);
}

// vf1 (Free) and vf2 (NewPage) of each instance. The 0x34, 0x38 and 0x1b0 Free entries are the jump
// targets of the instances' vf1 stubs (0x20febe, 0xb2867e, 0x22edd1), which stay as they are.
// vf3 (FreePage) is hooked only for the two instances whose copies aren't folded (shared_vf_* otherwise).
const HookSpec kHooks[] = {
    {"cRZFastFixedAllocator2_0x0c_Free", 0x17aa00, 32, 0xdb47f86a, MemberAddress(&RZFastFixed0xc::Free)},
    {"cRZFastFixedAllocator2_0x0c_NewPage", 0x17aa40, 32, 0x0483c98d, MemberAddress(&RZFastFixed0xc::NewPage)},
    {"cRZFastFixedAllocator2_0x10_Free", 0x177030, 32, 0xdb47f86a, MemberAddress(&RZFastFixed0x10::Free)},
    {"cRZFastFixedAllocator2_0x10_NewPage", 0x177070, 32, 0x62a8f09d, MemberAddress(&RZFastFixed0x10::NewPage)},
    {"cRZFastFixedAllocator2_0x14_Free", 0x15a9f0, 32, 0xdb47f86a, MemberAddress(&RZFastFixed0x14::Free)},
    {"cRZFastFixedAllocator2_0x14_NewPage", 0x15aa30, 32, 0x7c39617f, MemberAddress(&RZFastFixed0x14::NewPage)},
    {"cRZFastFixedAllocator2_0x18_Free", 0x1607c0, 32, 0xdb47f86a, MemberAddress(&RZFastFixed0x18::Free)},
    {"cRZFastFixedAllocator2_0x18_NewPage", 0x160800, 32, 0xb8118c50, MemberAddress(&RZFastFixed0x18::NewPage)},
    {"cRZFastFixedAllocator2_0x1c_Free", 0x160740, 32, 0xdb47f86a, MemberAddress(&RZFastFixed0x1c::Free)},
    {"cRZFastFixedAllocator2_0x1c_NewPage", 0x160780, 32, 0xa82dea78, MemberAddress(&RZFastFixed0x1c::NewPage)},
    {"cRZFastFixedAllocator2_0x20_Free", 0x14a300, 32, 0x6204de6a, MemberAddress(&RZFastFixed0x20::Free)},
    {"cRZFastFixedAllocator2_0x20_NewPage", 0x14a340, 32, 0x096038a8, MemberAddress(&RZFastFixed0x20::NewPage)},
    {"cRZFastFixedAllocator2_0x28_Free", 0x1657f0, 32, 0x6204de6a, MemberAddress(&RZFastFixed0x28::Free)},
    {"cRZFastFixedAllocator2_0x28_NewPage", 0x165830, 32, 0x5495b7a2, MemberAddress(&RZFastFixed0x28::NewPage)},
    {"cRZFastFixedAllocator2_0x34_Free", 0x2102b7, 32, 0x8c0abf46, MemberAddress(&RZFastFixed0x34::Free)},
    {"cRZFastFixedAllocator2_0x34_NewPage", 0x20fec3, 32, 0x7d9821fc, MemberAddress(&RZFastFixed0x34::NewPage)},
    {"cRZFastFixedAllocator2_0x38_Free", 0xb2883d, 32, 0x8c0abf46, MemberAddress(&RZFastFixed0x38::Free)},
    {"cRZFastFixedAllocator2_0x38_NewPage", 0xb28683, 32, 0x389c4200, MemberAddress(&RZFastFixed0x38::NewPage)},
    {"cRZFastFixedAllocator2_0x3c_Free", 0x54e1d0, 32, 0x6204de6a, MemberAddress(&RZFastFixed0x3c::Free)},
    {"cRZFastFixedAllocator2_0x3c_NewPage", 0x54e210, 32, 0x6cea0c26, MemberAddress(&RZFastFixed0x3c::NewPage)},
    {"cRZFastFixedAllocator2_0x54_Free", 0x8c44f0, 32, 0xcbf3942b, MemberAddress(&RZFastFixed0x54::Free)},
    {"cRZFastFixedAllocator2_0x54_NewPage", 0x8c4530, 32, 0xee560416, MemberAddress(&RZFastFixed0x54::NewPage)},
    {"cRZFastFixedAllocator2_0x58_Free", 0x16ddd0, 32, 0xcbf3942b, MemberAddress(&RZFastFixed0x58::Free)},
    {"cRZFastFixedAllocator2_0x58_NewPage", 0x16de10, 32, 0x8de9b12a, MemberAddress(&RZFastFixed0x58::NewPage)},
    {"cRZFastFixedAllocator2_0x5c_Free", 0x1630f0, 32, 0xcbf3942b, MemberAddress(&RZFastFixed0x5c::Free)},
    {"cRZFastFixedAllocator2_0x5c_NewPage", 0x163130, 32, 0x24f24837, MemberAddress(&RZFastFixed0x5c::NewPage)},
    {"cRZFastFixedAllocator2_0x74_Free", 0x205150, 32, 0xcbf3942b, MemberAddress(&RZFastFixed0x74::Free)},
    {"cRZFastFixedAllocator2_0x74_NewPage", 0x205190, 32, 0xdd625328, MemberAddress(&RZFastFixed0x74::NewPage)},
    {"cRZFastFixedAllocator2_0x98_Free", 0xaa3ca0, 32, 0x436c06e8, MemberAddress(&RZFastFixed0x98::Free)},
    {"cRZFastFixedAllocator2_0x98_NewPage", 0xaa3ce0, 32, 0x764be4c4, MemberAddress(&RZFastFixed0x98::NewPage)},
    {"cRZFastFixedAllocator2_0x9c_Free", 0xa34fd0, 32, 0x436c06e8, MemberAddress(&RZFastFixed0x9c::Free)},
    {"cRZFastFixedAllocator2_0x9c_NewPage", 0xa35010, 32, 0xe0efc1eb, MemberAddress(&RZFastFixed0x9c::NewPage)},
    {"cRZFastFixedAllocator2_0x19c_Free", 0x1645b0, 32, 0x8922252f, MemberAddress(&RZFastFixed0x19c::Free)},
    {"cRZFastFixedAllocator2_0x19c_NewPage", 0x1645f0, 32, 0x3e233709, MemberAddress(&RZFastFixed0x19c::NewPage)},
    {"cRZFastFixedAllocator2_0x19c_FreePage", 0x164630, 17, 0xfc5281c0, MemberAddress(&RZFastFixed0x19c::FreePage)},
    {"cRZFastFixedAllocator2_0x1b0_Free", 0x22f908, 32, 0x672c4403, MemberAddress(&RZFastFixed0x1b0::Free)},
    {"cRZFastFixedAllocator2_0x1b0_NewPage", 0x22edd6, 32, 0x7c062e66, MemberAddress(&RZFastFixed0x1b0::NewPage)},
    {"cRZFastFixedAllocator2_0x1b0_FreePage", 0x22ee06, 16, 0x360ae484, MemberAddress(&RZFastFixed0x1b0::FreePage)},
    // Slot 0 of every instance (the 5-byte jump 0x20feb9 leads here too), also called directly by the pooled
    // classes' operator new (0x210087, 0x22f2ea, 0xb2878d, 0xc0aa89). Named shared_vf_* by the tools, but only the
    // allocator family's vtables reference it (tools/xrefs.py), so it is not folded with unrelated classes.
    {"cRZFastFixedAllocator2_Alloc", 0x14a2d0, 32, 0x7494b53d, MemberAddress(&RZFixedAllocatorBase::Alloc)},
    // Slot 3, the copies folded by page size (only the allocator family's vtables reference each one).
    {"cRZFastFixedAllocator2_FreePage_2k", 0x15aa70, 17, 0x671b17e5, MemberAddress(&RZFastFixed0xc::FreePage)},
    {"cRZFastFixedAllocator2_FreePage_4k", 0x14a380, 17, 0x2de61b68, MemberAddress(&RZFastFixed0x20::FreePage)},
    {"cRZFastFixedAllocator2_FreePage_4k_b", 0x20fef0, 16, 0x64fb3782, MemberAddress(&RZFastFixed0x34::FreePage)},
    {"cRZFastFixedAllocator2_FreePage_8k", 0x163170, 17, 0x5e05e770, MemberAddress(&RZFastFixed0x54::FreePage)},
    {"cRZFastFixedAllocator2_FreePage_16k", 0xa35050, 17, 0x087908c3, MemberAddress(&RZFastFixed0x98::FreePage)},
    // The out-of-line constructors and destructors (each called only by XC initialisers and their atexit thunks).
    {"cRZFastFixedAllocator2_0x0c_Construct", 0x17acf0, 32, 0x67eb9f8b, Ctor<0x0c>()},
    {"cRZFastFixedAllocator2_0x10_Construct", 0x177350, 32, 0x6886cd88, Ctor<0x10>()},
    {"cRZFastFixedAllocator2_0x14_Construct", 0x15b7e0, 32, 0x1eb261af, Ctor<0x14>()},
    {"cRZFastFixedAllocator2_0x18_Construct", 0x160840, 32, 0x64880c67, Ctor<0x18>()},
    {"cRZFastFixedAllocator2_0x1c_Construct", 0x1608a0, 32, 0xccee4365, Ctor<0x1c>()},
    {"cRZFastFixedAllocator2_0x20_Construct", 0x14ba20, 32, 0x3370477d, Ctor<0x20>()},
    {"cRZFastFixedAllocator2_0x28_Construct", 0x165870, 32, 0xe1f9ca8b, Ctor<0x28>()},
    {"cRZFastFixedAllocator2_0x34_Construct", 0x20ff00, 32, 0xd9980a02, Ctor<0x34>()},
    {"cRZFastFixedAllocator2_0x38_Construct", 0xb286b0, 32, 0x9ac98b1c, Ctor<0x38>()},
    {"cRZFastFixedAllocator2_0x3c_Construct", 0x54e250, 32, 0xa4df9be8, Ctor<0x3c>()},
    {"cRZFastFixedAllocator2_0x58_Construct", 0x16de90, 32, 0x11e4de24, Ctor<0x58>()},
    {"cRZFastFixedAllocator2_0x74_Construct", 0x2051d0, 32, 0xf3f033c7, Ctor<0x74>()},
    {"cRZFastFixedAllocator2_0x98_Construct", 0xaa4930, 32, 0xfe370440, Ctor<0x98>()},
    {"cRZFastFixedAllocator2_0x9c_Construct", 0xa368d0, 32, 0xe9f57dfb, Ctor<0x9c>()},
    {"cRZFastFixedAllocator2_0x1b0_Construct", 0x22ee16, 32, 0x099b7ff9, Ctor<0x1b0>()},
    {"cRZFastFixedAllocator2_0x0c_Destruct", 0x17ae20, 32, 0x9d750087, Dtor<0x0c>()},
    {"cRZFastFixedAllocator2_0x10_Destruct", 0x177710, 32, 0x9d750087, Dtor<0x10>()},
    {"cRZFastFixedAllocator2_0x14_Destruct", 0x15dad0, 32, 0x9d750087, Dtor<0x14>()},
    {"cRZFastFixedAllocator2_0x18_Destruct", 0x160950, 32, 0x9d750087, Dtor<0x18>()},
    {"cRZFastFixedAllocator2_0x20_Destruct", 0x14be30, 32, 0x9d750087, Dtor<0x20>()},
    {"cRZFastFixedAllocator2_0x34_Destruct", 0x20ffea, 32, 0x9d750087, Dtor<0x34>()},
    {"cRZFastFixedAllocator2_0x38_Destruct", 0xb2875e, 29, 0x8e4ee1f5, Dtor<0x38>()},
    {"cRZFastFixedAllocator2_0x3c_Destruct", 0x54e520, 32, 0x9d750087, Dtor<0x3c>()},
    {"cRZFastFixedAllocator2_0x58_Destruct", 0x16def0, 32, 0x9d750087, Dtor<0x58>()},
    {"cRZFastFixedAllocator2_0x74_Destruct", 0x2052b0, 32, 0x9d750087, Dtor<0x74>()},
    {"cRZFastFixedAllocator2_0x98_Destruct", 0xaa4c80, 32, 0x9d750087, Dtor<0x98>()},
    {"cRZFastFixedAllocator2_0x9c_Destruct", 0xa36b50, 32, 0x9d750087, Dtor<0x9c>()},
    {"cRZFastFixedAllocator2_0x1b0_Destruct", 0x22f1ae, 32, 0x9d750087, Dtor<0x1b0>()},
};
REGISTER_HOOKS(kHooks);

} // namespace
