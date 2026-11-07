#include "hook/hooks.h"
#include "ts/cTSStringSet.h"
namespace {
const HookSpec kHooks[] = {
{"cTSStringSet_Release", 0x1ee330, 26, 0x8d54b034, MemberAddress(&cTSStringSet::Release)},
{"cTSStringSet_QueryInterface", 0x1ee280, 32, 0x8e9455a8, MemberAddress(&cTSStringSet::QueryInterface)},
{"cTSStringSet_vf32", 0x1ee360, 32, 0x82e9887c, MemberAddress(&cTSStringSet::Clear)},
{"cTSStringSet_vf27", 0x1eeb90, 11, 0xf89ffc21, MemberAddress(&cTSStringSet::SetInstance)},
{"cTSStringSet_vf17", 0x1eeba0, 32, 0x999d3ae0, MemberAddress(&cTSStringSet::SetKey)},
{"cTSStringSet_vf20", 0x1ec790, 30, 0xd59dbc46, MemberAddress(&cTSStringSet::LoadFromFile)},
{"cTSStringSet_vf22", 0x1ee4f0, 30, 0x850d6005, MemberAddress(&cTSStringSet::SaveToFile)},
{"cTSStringSet_Destruct", 0x1eae50, 32, 0x991c9096, MemberAddress(&cTSStringSet::Destruct)},
{"cTSStringSet_vf35", 0x1eb230, 32, 0x03ec43ac, MemberAddress(&cTSStringSet::DeletingDestructor)},
// The string-set factory (cTSResourceFactoryStringSet). The creator 0x1eba40 is not in db/functions.jsonl:
// CRC over its first 32 bytes, computed by hand (tools/hookcrc.py checks it).
{"cTSResourceFactoryStringSet_Create", 0x1eba40, 32, 0x55eed1f6, reinterpret_cast<void *>(&cTSResourceFactoryStringSet::Create)},
{"cTSResourceFactoryStringSet_vf7", 0x1eb200, 32, 0xbcb5fb50, MemberAddress(&cTSResourceFactoryStringSet::DeletingDestructor)},
    {"IsValidUtf8", 0x1b285f, 32, 0x749472fa, reinterpret_cast<void *>(&cTSStringSet::IsValidUtf8)},
    {"cTSStringSet_OpenResourceRecord", 0x1ec1c0, 32, 0xd0187a0c, reinterpret_cast<void *>(&cTSStringSet::OpenResourceRecord)},
    {"cTSStringSet_GetStrings", 0x1ecf60, 32, 0xa4bf4711, MemberAddress(&cTSStringSet::GetStrings)},
    {"cTSStringSet_GetComments", 0x1ec7e0, 32, 0x97464f58, MemberAddress(&cTSStringSet::GetComments)},
    {"cTSStringSet_GetOrAddStrings", 0x1eb860, 32, 0xb7c3e940, MemberAddress(&cTSStringSet::GetOrAddStrings)},
    {"cTSStringSet_LoadAllLanguages", 0x1ec7b0, 32, 0xfcdfbfe9, MemberAddress(&cTSStringSet::LoadAllLanguages)},
    {"cTSStringSet_vf23", 0x1eb8f0, 32, 0x918701ee, MemberAddress(&cTSStringSet::LoadAll)},
    {"cTSStringSet_WriteStrings", 0x1ef4e0, 32, 0x165db233, MemberAddress(&cTSStringSet::WriteStrings)},
    {"cTSStringSet_vf21", 0x1ee510, 32, 0x25bddcb9, MemberAddress(&cTSStringSet::Commit)},
    {"cTSStringSet_Construct", 0x1eace0, 32, 0xb16966dd, MemberAddress(&cTSStringSet::Construct)},
    {"FUN_a01eba70", 0x1eba70, 32, 0xbca8f83a, reinterpret_cast<void *>(&cTSStringSet::Create)},
    {"cTSStringSet_vf5", 0x1eb6e0, 32, 0x5c6568e7, MemberAddress(&cTSStringSet::GetStringCount)},
    {"cTSStringSet_vf6", 0x1ebfb0, 32, 0x3b62e8be, MemberAddress(&cTSStringSet::GetString)},
    {"cTSStringSet_vf7", 0x1ec080, 32, 0xf736c0c7, MemberAddress(&cTSStringSet::GetStringInto)},
    {"cTSStringSet_vf8", 0x1ebb70, 32, 0x7b65364d, MemberAddress(&cTSStringSet::GetStringExact)},
    {"cTSStringSet_vf15", 0x1ebb20, 32, 0x1cca0001, MemberAddress(&cTSStringSet::GetComment)},
    {"cTSStringSet_vf24", 0x1ebbe0, 29, 0x84654f36, MemberAddress(&cTSStringSet::GetName)},
    {"cTSStringSet_vf25", 0x1eec60, 32, 0xb1beb6c5, MemberAddress(&cTSStringSet::SetName)},
    {"cTSStringSet_vf28", 0x1eb580, 32, 0xf58dd083, MemberAddress(&cTSStringSet::IsWritable)},
    {"cTSStringSet_vf4", 0x1eb620, 32, 0x5d0e3f54, MemberAddress(&cTSStringSet::CopyFrom)},
    {"cTSStringSet_vf12", 0x1eb510, 32, 0x63801137, MemberAddress(&cTSStringSet::AppendStringFrom)},
    {"cTSStringSet_vf13", 0x1eb550, 32, 0xa4f15374, MemberAddress(&cTSStringSet::AppendString)},
    {"cTSStringSet_vf18", 0x1eebf0, 32, 0x70e78858, MemberAddress(&cTSStringSet::SetKeyFromFile)},
    {"cTSStringSet_vf30", 0x1ef250, 32, 0xe4d41f19, MemberAddress(&cTSStringSet::ResetSortOrder)},
    {"cTSStringSet_vf31", 0x1ebf20, 32, 0x61a3df88, MemberAddress(&cTSStringSet::GetSortedIndex)},
    {"cTSResourceFactoryStringSet_CreateInstance_1", 0x1eb710, 32, 0xba121bdf, MemberAddress(&cTSResourceFactoryStringSet::CreateInstance)},
    {"cTSResourceFactoryStringSet_CreateInstance_2", 0x1eb7c0, 32, 0xa592bc3a, MemberAddress(&cTSResourceFactoryStringSet::CreateInstanceByType)},
    {"cTSResourceFactoryStringSet_Write", 0x1ef2c0, 32, 0x040f5e1d, MemberAddress(&cTSResourceFactoryStringSet::Write)},
};
// Shared folded functions are not hooked.
REGISTER_HOOKS(kHooks);
}
