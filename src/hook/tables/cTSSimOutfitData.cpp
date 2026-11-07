#include "hook/hooks.h"
#include "cas/cTSSimOutfitData.h"

namespace {

using Obj = nTSCASCore::cTSSimOutfitData;

const HookSpec kHooks[] = {
    {"nTSCASCore_cTSSimOutfitData_vf56", 0xba6be0, 14, 0x4a07c3b3, MemberAddress(&Obj::StoreBlock6C)},
    {"nTSCASCore_cTSSimOutfitData_vf57", 0xb81c80, 14, 0x1a1e942c, MemberAddress(&Obj::StoreBlock7C)},
    {"nTSCASCore_cTSSimOutfitData_vf58", 0xb81970, 17, 0x600bf74a, MemberAddress(&Obj::StoreBlock8C)},
    {"nTSCASCore_cTSSimOutfitData_vf15", 0xba6f60, 13, 0x6526b982, MemberAddress(&Obj::StoreByte14C)},
    {"nTSCASCore_cTSSimOutfitData_vf109", 0xba6f00, 13, 0x704cea08, MemberAddress(&Obj::StoreDword134)},
    {"nTSCASCore_cTSSimOutfitData_vf54", 0xba6f30, 14, 0x402ec359, MemberAddress(&Obj::StoreFloatBits58)},
    {"nTSCASCore_cTSSimOutfitData_vf17", 0xb94ce0, 7, 0x8ac19419, MemberAddress(&Obj::Address138)},
    {"nTSCASCore_cTSSimOutfitData_vf37", 0xb9be90, 7, 0x7e786124, MemberAddress(&Obj::Address0E4)},
    {"nTSCASCore_cTSSimOutfitData_vf24", 0xba9f80, 14, 0xe8881e81, MemberAddress(&Obj::Dword50OrOne)},
    {"nTSCASCore_cTSSimOutfitData_vf46", 0xb99410, 32, 0xf8e1454d, MemberAddress(&Obj::SlotForLowestBit)},
    {"nTSCASCore_cTSSimOutfitData_vf47", 0xbaa250, 20, 0x484c9f73, MemberAddress(&Obj::FlagsContain)},
    {"nTSCASCore_cTSSimOutfitData_vf111", 0xba6a20, 32, 0x00f8cfd7, MemberAddress(&Obj::SetMaskBits150)},
    {"nTSCASCore_cTSSimOutfitData_vf110", 0xba6b50, 32, 0x00fc6374, MemberAddress(&Obj::FillSlots154)},
    {"nTSCASCore_cTSSimOutfitData_vf52", 0xba6f10, 23, 0x0242603b, MemberAddress(&Obj::SetDword50Notify)},
    {"nTSCASCore_cTSSimOutfitData_vf18", 0xb9aa80, 12, 0x3566d69f, MemberAddress(&Obj::LowWordIsZero)},
    {"nTSCASCore_cTSSimOutfitData_vf104", 0xb9c110, 32, 0x2260c73a, MemberAddress(&Obj::PreloadParts)},
};
REGISTER_HOOKS(kHooks);

} // namespace
