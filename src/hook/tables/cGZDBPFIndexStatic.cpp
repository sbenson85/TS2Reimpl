#include "hook/hooks.h"
#include "gz/cGZDBPFIndexStatic.h"

namespace {

using A = cGZDBPFIndexStaticAdapters;


// The constructor is the class's switch (every index 0x9af590 builds is ours); the slot adapters serve the indexes
// 0x9ae5c0 builds inline with the game's vtable. Folded slots (shared_vf_*) are not hooked.
const HookSpec kHooks[] = {
    {"cGZDBPFIndexStatic_Construct", 0x9a6da0, 32, 0xc2cd9fff, reinterpret_cast<void *>(&A::Construct)},
    {"cGZDBPFIndexStatic_vf0", 0x9a7860, 32, 0xda85a2b7, reinterpret_cast<void *>(&A::DeletingDestructor)},
    {"cGZDBPFIndexStatic_vf3", 0x9acb50, 28, 0x1d0dbe2f, reinterpret_cast<void *>(&A::GetCount)},
    {"cGZDBPFIndexStatic_vf4", 0x9acc30, 27, 0xb27bda57, reinterpret_cast<void *>(&A::GetTotalSize)},
    {"cGZDBPFIndexStatic_vf5", 0x9accd0, 32, 0x701a09fd, reinterpret_cast<void *>(&A::GetTotalMemSize)},
    {"cGZDBPFIndexStatic_vf6", 0x9abc40, 32, 0x59fedaca, reinterpret_cast<void *>(&A::GetDataEnd)},
    {"cGZDBPFIndexStatic_vf7", 0x9ac730, 32, 0x6949b0f6, reinterpret_cast<void *>(&A::GetSerializedSize)},
    {"cGZDBPFIndexStatic_vf8", 0x9ace20, 32, 0x81b41428, reinterpret_cast<void *>(&A::CollectTypes)},
    {"cGZDBPFIndexStatic_vf9", 0x9abfa0, 32, 0xc555c06c, reinterpret_cast<void *>(&A::CollectGroups)},
    {"cGZDBPFIndexStatic_vf10", 0x9ac4e0, 32, 0x3a797bdb, reinterpret_cast<void *>(&A::GetResourceKeyList)},
    {"cGZDBPFIndexStatic_vf12", 0x9ab430, 32, 0x1e6207e7, reinterpret_cast<void *>(&A::FindRecordForWrite)},
    {"cGZDBPFIndexStatic_vf16", 0x9b0b10, 32, 0x5f093c66, reinterpret_cast<void *>(&A::AllEntriesWithin)},
    {"cGZDBPFIndexStatic_vf17", 0x9b0c20, 32, 0xe275a087, reinterpret_cast<void *>(&A::IsRangeFree)},
    {"cGZDBPFIndexStatic_vf18", 0x9ae410, 32, 0x33730f59, reinterpret_cast<void *>(&A::SetEntries)},
    {"cGZDBPFIndexStatic_vf19", 0x9b10c0, 32, 0x92a49eba, reinterpret_cast<void *>(&A::CopyEntries)},
    {"cGZDBPFIndexStatic_vf20", 0x9aef50, 32, 0xb7a90522, reinterpret_cast<void *>(&A::SetEntriesFromIndex)},
    {"cGZDBPFIndexStatic_vf21", 0x9b16f0, 32, 0xd382918f, reinterpret_cast<void *>(&A::WriteIndexEntries)},
    {"cGZDBPFIndexStatic_vf22", 0x9b1430, 32, 0x4f959144, reinterpret_cast<void *>(&A::WriteCompressionDirectory)},
    {"cGZDBPFIndexStatic_BuildBuckets", 0x9ad220, 32, 0x61cb08d1, MemberAddress(&cGZDBPFIndexStatic::BuildBuckets)},
};
REGISTER_HOOKS(kHooks);

} // namespace
