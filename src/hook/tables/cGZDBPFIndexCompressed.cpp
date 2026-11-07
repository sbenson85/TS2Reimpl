#include "hook/hooks.h"
#include "gz/cGZDBPFIndexCompressed.h"

// Standalone shape (docs/standalone-shape.md): the constructor hook (0x9a6ca0, the only constructor call: the packed file's
// index load 0x9af590) gives those indexes our vtable. The slot entries are adapters (cGZDBPFIndexCompressedAdapters) for the
// indexes cGZDBSegmentPackedFile 0x9ae5c0 builds inline with the game's vtable (its cached-index load, game code) and for
// direct calls (none known). FindEntry is a non-virtual member, hooked directly. Folded slots (1, 2, 12, 13, 14, 23) are
// shared with other classes and not hooked; our vtable has its own.
namespace {

using A = cGZDBPFIndexCompressedAdapters;

const HookSpec kHooks[] = {
    {"cGZDBPFIndexCompressed_Construct", 0x9a6ca0, 32, 0xe3158749, reinterpret_cast<void *>(&A::Construct)},
    {"cGZDBPFIndexCompressed_vf0", 0x9a7720, 32, 0xe1acf5d9, reinterpret_cast<void *>(&A::DeletingDestructor)},
    {"cGZDBPFIndexCompressed_vf3", 0x9acb40, 10, 0x087a791b, reinterpret_cast<void *>(&A::GetCount)},
    {"cGZDBPFIndexCompressed_vf4", 0x9acbc0, 32, 0x2d96930b, reinterpret_cast<void *>(&A::GetTotalSize)},
    {"cGZDBPFIndexCompressed_vf5", 0x9acc50, 32, 0x889c581e, reinterpret_cast<void *>(&A::GetTotalMemSize)},
    {"cGZDBPFIndexCompressed_vf6", 0x9abbc0, 32, 0xa50c8d25, reinterpret_cast<void *>(&A::GetDataEnd)},
    {"cGZDBPFIndexCompressed_vf7", 0x9ac6f0, 32, 0xd394b50e, reinterpret_cast<void *>(&A::GetSerializedSize)},
    {"cGZDBPFIndexCompressed_vf8", 0x9acd00, 32, 0xa97ff150, reinterpret_cast<void *>(&A::CollectTypes)},
    {"cGZDBPFIndexCompressed_vf9", 0x9abe70, 32, 0xbaf58526, reinterpret_cast<void *>(&A::CollectGroups)},
    {"cGZDBPFIndexCompressed_vf10", 0x9ac040, 32, 0x9fb84653, reinterpret_cast<void *>(&A::GetResourceKeyList)},
    {"cGZDBPFIndexCompressed_vf11", 0x9ab1a0, 32, 0x41ead888, reinterpret_cast<void *>(&A::FindRecord)},
    {"cGZDBPFIndexCompressed_FindEntry", 0x9ab2e0, 32, 0x577adbdd, MemberAddress(&cGZDBPFIndexCompressed::FindEntry)},
    {"cGZDBPFIndexCompressed_vf15", 0x9afd00, 32, 0x1ffd4e98, reinterpret_cast<void *>(&A::SetRecordMemSize)},
    {"cGZDBPFIndexCompressed_vf16", 0x9b0a40, 32, 0xf01b08ae, reinterpret_cast<void *>(&A::AllEntriesWithin)},
    {"cGZDBPFIndexCompressed_vf17", 0x9b0b60, 32, 0x5731f795, reinterpret_cast<void *>(&A::IsRangeFree)},
    {"cGZDBPFIndexCompressed_vf18", 0x9adb20, 32, 0xba46fbe5, reinterpret_cast<void *>(&A::SetEntries)},
    {"cGZDBPFIndexCompressed_vf19", 0x9b0e70, 32, 0xd2039968, reinterpret_cast<void *>(&A::CopyEntries)},
    {"cGZDBPFIndexCompressed_vf20", 0x9aec00, 32, 0x024fbe71, reinterpret_cast<void *>(&A::SetEntriesFromIndex)},
    {"cGZDBPFIndexCompressed_vf21", 0x9b1540, 32, 0x818ddf4d, reinterpret_cast<void *>(&A::WriteIndexEntries)},
    {"cGZDBPFIndexCompressed_vf22", 0x9b11a0, 32, 0x53b81082, reinterpret_cast<void *>(&A::WriteCompressionDirectory)},
};
REGISTER_HOOKS(kHooks);

} // namespace
