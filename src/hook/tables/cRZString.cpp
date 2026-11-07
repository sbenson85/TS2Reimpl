#include "hook/hooks.h"
#include "rz/cRZString.h"

namespace {

// Overloaded members need an explicit type to take their address.
using IsEqualLen = bool (cRZString::*)(const char *, uint32_t, bool) const;
using IsEqualStr = bool (cRZString::*)(const char *, bool) const;
using IsEqualObj = bool (cRZString::*)(const cIGZString &, bool) const;
using CompareLen = int32_t (cRZString::*)(const char *, uint32_t, bool) const;
using CompareStr = int32_t (cRZString::*)(const char *, bool) const;
using CompareObj = int32_t (cRZString::*)(const cIGZString &, bool) const;
using FromCharLen = void (cRZString::*)(const char *, uint32_t);
using FromCharStr = void (cRZString::*)(const char *);
using AppendObj = cRZString *(cRZString::*)(const cIGZString &);
using AppendLen = cRZString *(cRZString::*)(const char *, uint32_t);
using AppendStr = cRZString *(cRZString::*)(const char *);
using InsertObj = cRZString *(cRZString::*)(uint32_t, const cIGZString &);
using InsertLen = cRZString *(cRZString::*)(uint32_t, const char *, uint32_t);
using ReplaceObj = cRZString *(cRZString::*)(uint32_t, const cIGZString &);
using ReplaceLen = cRZString *(cRZString::*)(uint32_t, const char *, uint32_t);
using FindObj = int32_t (cRZString::*)(const cIGZString &, uint32_t, bool) const;
using FindStr = int32_t (cRZString::*)(const char *, uint32_t, bool) const;
using CtorDefault = cRZString *(cRZString::*)();
using CtorChar = cRZString *(cRZString::*)(const char *);
using CtorString = cRZString *(cRZString::*)(const cIGZString &);
using CtorSub = cRZString *(cRZString::*)(const cRZString &, uint32_t, uint32_t);
using StartsWithLen = bool (cRZString::*)(const char *, uint32_t, bool) const;
using StartsWithObj = bool (cRZString::*)(const cIGZString &, bool) const;

const HookSpec kHooks[] = {
    {"cRZString_Construct", 0x144f5b, 32, 0x8d480d9f, MemberAddress(static_cast<CtorDefault>(&cRZString::Construct))},
    {"cRZString_Construct_Char", 0x14116d, 32, 0xf188dde4, MemberAddress(static_cast<CtorChar>(&cRZString::Construct))},
    {"cRZString_Construct_String", 0x144f2a, 32, 0x0da28be5, MemberAddress(static_cast<CtorString>(&cRZString::Construct))},
    {"cRZString_Construct_Substring", 0x141eb3, 32, 0x45e25815, MemberAddress(static_cast<CtorSub>(&cRZString::Construct))},
    {"cRZString_Destruct", 0x141193, 14, 0xbf8d4416, MemberAddress(&cRZString::Destruct)},
    {"cRZString_IsEmpty", 0x1ad026, 8, 0x32cfcc1b, MemberAddress(&cRZString::IsEmpty)},
    {"cRZString_IsEqual_CharLen", 0x1ad035, 32, 0xfc133706, MemberAddress(static_cast<IsEqualLen>(&cRZString::IsEqual))},
    {"cRZString_IsEqual_Char", 0x1ad096, 32, 0x9cfd78fc, MemberAddress(static_cast<IsEqualStr>(&cRZString::IsEqual))},
    {"cRZString_IsEqual_String", 0x1ad0b9, 32, 0xd730bd67, MemberAddress(static_cast<IsEqualObj>(&cRZString::IsEqual))},
    {"cRZString_CompareTo_CharLen", 0x1acc74, 32, 0xe52ef09e, MemberAddress(static_cast<CompareLen>(&cRZString::CompareTo))},
    {"cRZString_CompareTo_Char", 0x1accdb, 32, 0xd63f6aad, MemberAddress(static_cast<CompareStr>(&cRZString::CompareTo))},
    {"cRZString_CompareTo_String", 0x1acc0c, 32, 0xfc85593f, MemberAddress(static_cast<CompareObj>(&cRZString::CompareTo))},
    {"cRZString_QueryInterface", 0x1ad1c0, 32, 0x59d4fcbc, MemberAddress(&cRZString::QueryInterface)},
    {"cRZString_Release", 0x1ad37a, 26, 0x3786d89a, MemberAddress(&cRZString::Release)},
    {"cRZString_FromChar_CharLen", 0x1acfbe, 32, 0x21b81e71, MemberAddress(static_cast<FromCharLen>(&cRZString::FromChar))},
    // TS2-Extender hooks this one too (CRZStringFromChar); with both installed, whichever patches first wins.
    {"cRZString_FromChar_Char", 0x1acf84, 32, 0x6f2ca41d, MemberAddress(static_cast<FromCharStr>(&cRZString::FromChar))},
    {"cRZString_Assign", 0x1ac9d0, 18, 0x7d8b81c4, MemberAddress(&cRZString::Assign)},
    {"cRZString_Copy", 0x1accfe, 32, 0xe2cd4c3b, MemberAddress(&cRZString::Copy)},
    {"cRZString_Resize", 0x1ad3dc, 17, 0xd57385f5, MemberAddress(&cRZString::Resize)},
    {"cRZString_Append_String", 0x1acabf, 32, 0x04f2bfdc, MemberAddress(static_cast<AppendObj>(&cRZString::Append))},
    {"cRZString_Append_CharLen", 0x1acb13, 25, 0xa5016a99, MemberAddress(static_cast<AppendLen>(&cRZString::Append))},
    {"cRZString_Append_Char", 0x1acae6, 32, 0x8424959f, MemberAddress(static_cast<AppendStr>(&cRZString::Append))},
    {"cRZString_Insert_String", 0x1acfde, 32, 0x7b41a6cc, MemberAddress(static_cast<InsertObj>(&cRZString::Insert))},
    {"cRZString_Insert_CharLen", 0x1ad009, 29, 0xa6366845, MemberAddress(static_cast<InsertLen>(&cRZString::Insert))},
    {"cRZString_Replace_String", 0x1ad394, 32, 0x7fc74834, MemberAddress(static_cast<ReplaceObj>(&cRZString::Replace))},
    {"cRZString_Replace_CharCount", 0x1ad3bf, 29, 0x6a9b91eb, MemberAddress(static_cast<ReplaceLen>(&cRZString::Replace))},
    {"cRZString_Erase", 0x1ace48, 25, 0x8dda73b4, MemberAddress(&cRZString::Erase)},
    {"cRZString_Find_String", 0x1ace61, 32, 0x1e98625b, MemberAddress(static_cast<FindObj>(&cRZString::Find))},
    {"cRZString_Find_Char", 0x1aceeb, 32, 0x0653c7fb, MemberAddress(static_cast<FindStr>(&cRZString::Find))},
    {"cRZString_RFind_String", 0x1ad1e4, 32, 0x07f4542a, MemberAddress(static_cast<FindObj>(&cRZString::RFind))},
    {"cRZString_RFind_Char", 0x1ad26e, 32, 0xc30d9f2f, MemberAddress(static_cast<FindStr>(&cRZString::RFind))},
    {"cRZString_Sprintf", 0x1ad6f7, 28, 0xd10b8fb9, MemberAddress(&cRZString::Sprintf)},
    {"cRZString_SprintfVaList", 0x1ad713, 32, 0x89345cca, MemberAddress(&cRZString::SprintfVaList)},
    // 0x1acb2c is byte-identical to the EndsWith object helper at 0x1acd5b (same CRC, different site).
    {"cRZString_StartsWith_Char", 0x1acb54, 32, 0xe0d91475, MemberAddress(static_cast<StartsWithLen>(&cRZString::StartsWith))},
    {"cRZString_StartsWith_String", 0x1acb2c, 32, 0xab1426e6, MemberAddress(static_cast<StartsWithObj>(&cRZString::StartsWith))},
    {"cRZString_DeletingDestruct", 0x1cbea8, 32, 0x7d98c149, MemberAddress(&cRZString::DeletingDestruct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
