#include "hook/hooks.h"
#include "rz/cRZCmdLine.h"

namespace {

// Overloaded members need an explicit type to take their address.
using CtorText = cRZCmdLine *(cRZCmdLine::*)(const char *);
using CtorOther = cRZCmdLine *(cRZCmdLine::*)(cIGZCmdLine *);
using SwitchValue = bool (cRZCmdLine::*)(const cIGZString &, cIGZString &, bool) const;
using SwitchAny = bool (cRZCmdLine::*)(const cIGZString &) const;
using SwitchChar = bool (cRZCmdLine::*)(char, cIGZString &, bool) const;
using IndexName = int32_t (cRZCmdLine::*)(const cIGZString &, int32_t) const;
using IndexChar = int32_t (cRZCmdLine::*)(char, int32_t) const;
using Construct0 = cRZCmdLine *(cRZCmdLine::*)();

// Not hooked: slot 2 (Release, 0x1af4d8) and slot 5 (GetCommandLineText, 0x1aedba) are shared with
// two other classes' vtables (folded); the AddRef slot is the shared refcount one.
const HookSpec kHooks[] = {
    {"cRZCmdLine_ConstructDefault", 0x1ae5e5, 32, 0x9ee8ef15, MemberAddress(static_cast<Construct0>(&cRZCmdLine::Construct))},
    {"cRZCmdLine_ConstructText", 0x1ae5a3, 32, 0x349359ee, MemberAddress(static_cast<CtorText>(&cRZCmdLine::Construct))},
    {"cRZCmdLine_ConstructFromOther", 0x1ae4e8, 32, 0xf9f5aab6, MemberAddress(static_cast<CtorOther>(&cRZCmdLine::Construct))},
    {"cRZCmdLine_Destruct", 0x14201d, 32, 0xd344fb0b, MemberAddress(&cRZCmdLine::Destruct)},
    {"cRZCmdLine_DeletingDestructor", 0x142229, 31, 0xd93560a3, MemberAddress(&cRZCmdLine::DeletingDestructor)},
    {"cRZCmdLine_QueryInterface", 0x1af4a0, 32, 0xc8d1fd19, MemberAddress(&cRZCmdLine::QueryInterface)},
    {"cRZCmdLine_ArgC", 0x1af537, 10, 0x50dbaba1, MemberAddress(&cRZCmdLine::ArgC)},
    {"cRZCmdLine_ArgV", 0x1af541, 32, 0xb616e223, MemberAddress(&cRZCmdLine::ArgV)},
    {"cRZCmdLine_Find", 0x1ae9d3, 32, 0x2885f230, MemberAddress(&cRZCmdLine::Find)},
    {"cRZCmdLine_FindSubString", 0x1aea85, 32, 0x5361e7c0, MemberAddress(&cRZCmdLine::FindSubString)},
    {"cRZCmdLine_IsSwitchPresent_1", 0x1af0ee, 32, 0x0c79744d, MemberAddress(static_cast<SwitchValue>(&cRZCmdLine::IsSwitchPresent))},
    {"cRZCmdLine_IsSwitchPresent_2", 0x1af09e, 32, 0x703db7ec, MemberAddress(static_cast<SwitchAny>(&cRZCmdLine::IsSwitchPresent))},
    {"cRZCmdLine_IsSwitchPresent_3", 0x1af321, 32, 0xda4423de, MemberAddress(static_cast<SwitchChar>(&cRZCmdLine::IsSwitchPresent))},
    {"cRZCmdLine_GetIndexOfSwitch_1", 0x1aedd0, 32, 0xe3d79fb2, MemberAddress(static_cast<IndexName>(&cRZCmdLine::GetIndexOfSwitch))},
    {"cRZCmdLine_GetIndexOfSwitch_2", 0x1aef39, 32, 0xd90f9f95, MemberAddress(static_cast<IndexChar>(&cRZCmdLine::GetIndexOfSwitch))},
    {"cRZCmdLine_InsertArgument", 0x1af02a, 32, 0x16c34a62, MemberAddress(&cRZCmdLine::InsertArgument)},
    {"cRZCmdLine_EraseArgument", 0x1ae992, 32, 0xe561166d, MemberAddress(&cRZCmdLine::EraseArgument)},
    {"cRZCmdLine_GetArgument", 0x1ae610, 32, 0x2bfa284d, MemberAddress(&cRZCmdLine::GetArgument)},
    {"cRZCmdLine_Parse", 0x1ae6fe, 32, 0x9d4018e6, MemberAddress(&cRZCmdLine::Parse)},
    {"cRZCmdLine_RebuildText", 0x1ae632, 32, 0xfd48ef2e, MemberAddress(&cRZCmdLine::RebuildText)},

    // The vector<cRZString> helpers (also called from cTSStringSet and other classes). The
    // vector's clear (0x1adb0b), _Calculate_growth (0x143994), _Change_array (0x1ad8db) and _Tidy (0x1441eb) are library
    // matches (db/library-matches.jsonl): no entry and no hook; the bodies in cRZCmdLine.size.cpp stay as the vector's own
    // internals for the hooked members above until the vector becomes a std::vector (its elements are destroyed through
    // their vtable, so that is a retrofit of its own).
    {"cRZStringVector_PushBack", 0x1ac271, 32, 0xbc567382, MemberAddress(&cRZStringVector::PushBack)},
    {"cRZStringVector_EmplaceReallocate", 0x1ac291, 10, 0xc9054fe6, MemberAddress(&cRZStringVector::EmplaceReallocate)},
    {"cRZStringVector_Insert", 0x1ae427, 32, 0x59ea9a90, MemberAddress(&cRZStringVector::Insert)},
    {"cRZStringVector_Erase", 0x1af56e, 32, 0x6189d3f8, MemberAddress(&cRZStringVector::Erase)},
    {"cRZStringVector_DestroyRange", 0x141663, 30, 0x9e461af0, reinterpret_cast<void *>(&cRZStringVector::DestroyRange)},
    {"cRZStringVector_UninitializedCopy", 0x1ac987, 32, 0x0273e7b5, reinterpret_cast<void *>(&cRZStringVector::UninitializedCopy)},
    {"cRZStringVector_CopyN", 0x141633, 32, 0xb29ee25c, reinterpret_cast<void *>(&cRZStringVector::CopyN)},
    {"cRZStringVector_UninitializedCopyN", 0x141a91, 32, 0xcafaac73, reinterpret_cast<void *>(&cRZStringVector::UninitializedCopyN)},
    {"cRZStringVector_AssignCounted", 0x14155c, 32, 0x2640eb56, MemberAddress(&cRZStringVector::AssignCounted)},
    {"cRZStringVector_CopyBackward", 0x1ae350, 32, 0x1d43a9ab, reinterpret_cast<void *>(&cRZStringVector::CopyBackward)},
    {"cRZStringVector_CopyForward", 0x1ae37a, 32, 0xac463ffd, reinterpret_cast<void *>(&cRZStringVector::CopyForward)},
};
REGISTER_HOOKS(kHooks);

} // namespace
