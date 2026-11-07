#include "hook/hooks.h"
#include "ts/cTSFamilyHistory.h"

// Standalone shape (docs/standalone-shape.md, lane FI): the game creates every history through the creator 0x49e1e0 (operator
// new 0x28, constructor inline; registered by address by the cTSSimCOMDirector constructor), now `new cTSFamilyHistory`: every
// history carries our vtables. The slot hooks are adapters for game-built objects and direct calls. Bisect switch: the Create
// hook (cTSFamilyHistory_Create).

namespace {

// The creator 0x49e1e0 (100 bytes: push ecx; push 0x28; call operator new; ...; after int3 padding) is only referenced by
// address, so it is not in db/functions.jsonl: CRC of its first 32 bytes computed by hand (tools/hookcrc.py).
const HookSpec kHooks[] = {
    {"cTSFamilyHistory_Create", 0x49e1e0, 32, 0x11fce2fc, reinterpret_cast<void *>(&cTSFamilyHistory::Create)},
    {"cTSFamilyHistory_QueryInterface", 0x49f5f0, 32, 0x603c2948, reinterpret_cast<void *>(&cTSFamilyHistoryAdapters::QueryInterface)},
    {"cTSFamilyHistory_Release", 0x49feb0, 23, 0x36eb767c, reinterpret_cast<void *>(&cTSFamilyHistoryAdapters::Release)},
    {"cTSFamilyHistory_Write", 0x4a1240, 32, 0x8bdb8d14, reinterpret_cast<void *>(&cTSFamilyHistoryAdapters::Write)},
    {"cTSFamilyHistory_Read", 0x49fce0, 32, 0x50cbf8b0, reinterpret_cast<void *>(&cTSFamilyHistoryAdapters::Read)},
    {"cTSFamilyHistory_GetGZCLSID", 0x49e260, 6, 0x527110a9, reinterpret_cast<void *>(&cTSFamilyHistoryAdapters::GetGZCLSID)},
    {"cTSFamilyHistory_vf6", 0x49d350, 32, 0x5248c866, reinterpret_cast<void *>(&cTSFamilyHistoryAdapters::AddEntry)},
    {"cTSFamilyHistory_vf7", 0x49eca0, 32, 0x834a10ba, reinterpret_cast<void *>(&cTSFamilyHistoryAdapters::GetEntries)},
    {"cTSFamilyHistory_vf8", 0x49d9c0, 32, 0xaa93060d, reinterpret_cast<void *>(&cTSFamilyHistoryAdapters::WriteText)},
    {"cTSFamilyHistory_vf10", 0x49ba80, 32, 0x7c57d501, reinterpret_cast<void *>(&cTSFamilyHistoryAdapters::DeletingDestructor)},
    {"FamilyHistoryEntry_Read", 0x49f650, 32, 0x2770d57e, MemberAddress(&FamilyHistoryEntry::Read)},
    {"FamilyHistoryEntry_Write", 0x4a0f50, 32, 0x5485e9e1, MemberAddress(&FamilyHistoryEntry::Write)},
};
REGISTER_HOOKS(kHooks);

} // namespace
