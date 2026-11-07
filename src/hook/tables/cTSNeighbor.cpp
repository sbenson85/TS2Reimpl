#include "hook/hooks.h"
#include "ts/cTSNeighbor.h"

// Standalone shape (docs/standalone-shape.md, lane EJ): the creator 0x4b73c0 (the only creation site, constructor inline) is
// the class's switch: with it on, every neighbour carries our vtables. The other entries are adapters for direct calls and for
// neighbours built with the game's vtables. Bisect: turn off FUN_a04b73c0.
// Slots only the game's vtables reach have no entry (lane GP): their originals are trapped under Hooks.FUN_a04b73c0=1
// ("in_effect": "callers"). The adapters left are byte matches through the adapter, or GetGZCLSID (original code calls it).
namespace {

const HookSpec kHooks[] = {
    {"FUN_a04b73c0", 0x4b73c0, 32, 0x4402ae28, reinterpret_cast<void *>(&cTSNeighbor::Create)},
    {"cTSNeighbor_Release", 0x4b8cf0, 26, 0xb7239cc6, reinterpret_cast<void *>(&cTSNeighborAdapters::Release)},
    {"cTSNeighbor_GetGZCLSID", 0x4b74d0, 6, 0xf5e6f5d9, reinterpret_cast<void *>(&cTSNeighborAdapters::GetGZCLSID)},
    {"cTSNeighbor_Write", 0x4b8ed0, 32, 0xc10242f4, reinterpret_cast<void *>(&cTSNeighborAdapters::Write)},
    {"cTSNeighbor_vf7", 0x4b8e80, 32, 0x6f8122df, reinterpret_cast<void *>(&cTSNeighborAdapters::Detach)},
    {"cTSNeighbor_vf17", 0x4b8e10, 32, 0xe72a0051, reinterpret_cast<void *>(&cTSNeighborAdapters::SetAttribute)},
    {"cTSNeighbor_vf18", 0x4b80c0, 15, 0x4dc50c0c, reinterpret_cast<void *>(&cTSNeighborAdapters::GetAttributes)},
    {"cTSNeighbor_vf20", 0x4b8130, 14, 0x79cb8049, reinterpret_cast<void *>(&cTSNeighborAdapters::GetGenderMask)},
    {"cTSNeighbor_vf21", 0x4b80d0, 32, 0x9ad718f9, reinterpret_cast<void *>(&cTSNeighborAdapters::GetAgeMask)},
    {"cTSNeighbor_vf22", 0x4b8690, 12, 0x63ba3aa6, reinterpret_cast<void *>(&cTSNeighborAdapters::IsHuman)},
    {"cTSNeighbor_vf23", 0x4b8490, 12, 0x62785091, reinterpret_cast<void *>(&cTSNeighborAdapters::IsPet)},
    {"cTSNeighbor_vf24", 0x4b86a0, 12, 0x67d2e1aa, reinterpret_cast<void *>(&cTSNeighborAdapters::IsGender0)},
    {"cTSNeighbor_vf25", 0x4b8550, 12, 0x5ab2c81a, reinterpret_cast<void *>(&cTSNeighborAdapters::IsGender1)},
    {"cTSNeighbor_vf26", 0x4b84a0, 26, 0x7042ce55, reinterpret_cast<void *>(&cTSNeighborAdapters::IsBaby)},
    {"cTSNeighbor_vf28", 0x4b84e0, 32, 0x1dd15855, reinterpret_cast<void *>(&cTSNeighborAdapters::IsChild)},
    {"cTSNeighbor_vf29", 0x4b86b0, 32, 0x73dd756b, reinterpret_cast<void *>(&cTSNeighborAdapters::IsTeen)},
    {"cTSNeighbor_vf30", 0x4b8740, 32, 0x4513b136, reinterpret_cast<void *>(&cTSNeighborAdapters::IsAdultWithFlag)},
    {"cTSNeighbor_vf31", 0x4b8460, 32, 0xe3fd8361, reinterpret_cast<void *>(&cTSNeighborAdapters::IsAdult)},
    {"cTSNeighbor_vf32", 0x4b8530, 26, 0x78b12969, reinterpret_cast<void *>(&cTSNeighborAdapters::IsElder)},
    {"cTSNeighbor_vf35", 0x4b84c0, 27, 0xa3852762, reinterpret_cast<void *>(&cTSNeighborAdapters::IsPersonDataFlagged)},
    {"cTSNeighbor_vf51", 0x4b7f90, 32, 0x6fde5312, reinterpret_cast<void *>(&cTSNeighborAdapters::GetExtraWord)},
    {"cTSNeighbor_vf52", 0x4b8de0, 32, 0xed213acb, reinterpret_cast<void *>(&cTSNeighborAdapters::SetExtraWord)},
    {"cTSNeighbor_GetKey", 0x4b7f10, 17, 0xf4c5c1a1, reinterpret_cast<void *>(&cTSNeighborAdapters::GetKey)},
    {"cTSNeighbor_SetKey", 0x4b8d90, 32, 0x4c86ec2c, reinterpret_cast<void *>(&cTSNeighborAdapters::SetKey)},
    {"cTSNeighbor_GetType", 0x4b82c0, 7, 0x000449f8, reinterpret_cast<void *>(&cTSNeighborAdapters::GetType)},
    {"cTSNeighbor_GetGroup", 0x4b7ef0, 7, 0xf584ef38, reinterpret_cast<void *>(&cTSNeighborAdapters::GetGroup)},
    {"cTSNeighbor_GetInstance", 0x4b7f00, 7, 0x6af5fd8a, reinterpret_cast<void *>(&cTSNeighborAdapters::GetInstance)},
    {"cTSNeighbor_SetType", 0x4b8e70, 15, 0xd5a7d736, reinterpret_cast<void *>(&cTSNeighborAdapters::SetType)},
    {"cTSNeighbor_SetGroup", 0x4b8d70, 15, 0x884b863a, reinterpret_cast<void *>(&cTSNeighborAdapters::SetGroup)},
    {"cTSNeighbor_SetInstance", 0x4b8d80, 15, 0x7606b157, reinterpret_cast<void *>(&cTSNeighborAdapters::SetInstance)},
};
REGISTER_HOOKS(kHooks);

} // namespace
