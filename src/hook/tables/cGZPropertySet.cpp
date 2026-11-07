#include "gz/cGZPropertySet.h"
#include "hook/hooks.h"

namespace {

using U32Adapters = cGZPropertySetUint32Adapters;
using StrAdapters = cGZPropertySetStringAdapters;

// The property sets in the standalone shape (src/gz/cGZPropertySet.size.cpp, lane EQ). Not hooked: the shared_vf_ slots AddRef
// 0x148fb0, Release 0x1bf485, Clear 0x1bee23, GetCount 0x1bf41f, KeyToString 0x1bf336 (folded with other classes).
const HookSpec kHooks[] = {
    // cGZPropertySetUint32: the constructor hook gives every set the game builds our vtable (the class's switch, bisect:
    // _Construct); the rest are adapters for direct calls and sets with the game's vtable.
    {"cGZPropertySetUint32_Construct", 0x1bec5f, 26, 0xc16e6f49, reinterpret_cast<void *>(&U32Adapters::Construct)},
    {"cGZPropertySetUint32_Destruct", 0x1bed39, 23, 0x24bc7d13, reinterpret_cast<void *>(&U32Adapters::Destruct)},
    {"cGZPropertySetUint32_QueryInterface", 0x1bf44d, 32, 0xd1c1b28d, reinterpret_cast<void *>(&U32Adapters::QueryInterface)},
    {"cGZPropertySetUint32_SetProperty", 0x1bf49c, 32, 0xdd9f121c, reinterpret_cast<void *>(&U32Adapters::SetProperty)},
    {"cGZPropertySetUint32_RemoveProperty", 0x1bee49, 32, 0x5397574c, reinterpret_cast<void *>(&U32Adapters::RemoveProperty)},
    {"cGZPropertySetUint32_GetFirst", 0x1bf239, 32, 0x701b397c, reinterpret_cast<void *>(&U32Adapters::GetFirst)},
    {"cGZPropertySetUint32_GetNext", 0x1bf389, 32, 0x5fd0552e, reinterpret_cast<void *>(&U32Adapters::GetNext)},
    {"cGZPropertySetUint32_GetProperty", 0x1bf5e9, 32, 0x4cf4d786, reinterpret_cast<void *>(&U32Adapters::GetProperty)},
    {"cGZPropertySetUint32_GetPropertyValue", 0x1bf626, 32, 0x134178f3, reinterpret_cast<void *>(&U32Adapters::GetPropertyValue)},
    {"cGZPropertySetUint32_HasProperty", 0x1bf2cc, 32, 0xa536586b, reinterpret_cast<void *>(&U32Adapters::HasProperty)},
    {"cGZPropertySetUint32_CopyFrom", 0x1bf148, 32, 0xa0e96136, reinterpret_cast<void *>(&U32Adapters::CopyFrom)},
    {"cGZPropertySetUint32_FormatKey", 0x1bf349, 32, 0xd57faef0, reinterpret_cast<void *>(&cGZPropertySetUint32::FormatKey)},
    {"cGZPropertyEntry_Construct", 0x1bec9b, 32, 0x7451a6a4, MemberAddress(&cGZPropertyEntry::Construct)},
    {"cGZPropertyEntry_Value", 0x1bee0a, 25, 0x2c37c745, MemberAddress(&cGZPropertyEntry::Value)},
    {"cGZPropertyEntry_Destroy", 0x1bf210, 32, 0xecf9d15e, MemberAddress(&cGZPropertyEntry::Destroy)},
    // cGZPropertySetString: the constructor hook (moved from AppInit) builds ours; sets the game builds inline (base constructor
    // 0x1bec45, then its own store of 0xd9f718) keep the game's vtable and reach these adapters. The class's switch (bisect):
    // _Construct.
    {"cGZPropertySetString_Construct", 0x22b157, 32, 0xd5fbbfeb, reinterpret_cast<void *>(&StrAdapters::Construct)},
    {"cGZPropertySetString_SetProperty", 0x1bf515, 32, 0xd431f769, reinterpret_cast<void *>(&StrAdapters::SetProperty)},
    {"cGZPropertySetString_RemoveProperty", 0x1bee9c, 32, 0xacf13833, reinterpret_cast<void *>(&StrAdapters::RemoveProperty)},
    {"cGZPropertySetString_GetFirst", 0x1bf26d, 32, 0xe36bc9f6, reinterpret_cast<void *>(&StrAdapters::GetFirst)},
    {"cGZPropertySetString_GetNext", 0x1bf3c0, 32, 0xe12d77af, reinterpret_cast<void *>(&StrAdapters::GetNext)},
    {"cGZPropertySetString_GetProperty", 0x1bf670, 32, 0x6ac460a0, reinterpret_cast<void *>(&StrAdapters::GetProperty)},
    {"cGZPropertySetString_GetPropertyValue", 0x1bf69a, 32, 0x6c41ec4d, reinterpret_cast<void *>(&StrAdapters::GetPropertyValue)},
    {"cGZPropertySetString_HasProperty", 0x1bf306, 32, 0x73b9b64f, reinterpret_cast<void *>(&StrAdapters::HasProperty)},
    {"cGZPropertySetString_KeyToString", 0x1bf36a, 31, 0x10514463, reinterpret_cast<void *>(&StrAdapters::KeyToString)},
    {"cGZPropertySetString_CopyFrom", 0x1bf0d1, 32, 0x5021d87d, reinterpret_cast<void *>(&StrAdapters::CopyFrom)},
    {"cGZPropertySetString_QueryInterface", 0x1bf429, 32, 0x6d82a157, reinterpret_cast<void *>(&StrAdapters::QueryInterface)},
    {"cGZPropertySetString_InternKey", 0x1bf191, 32, 0xc8593303, reinterpret_cast<void *>(&StrAdapters::InternKey)},
    {"cGZPropertySetString_FindKey", 0x1befff, 32, 0xd7d33d6e, reinterpret_cast<void *>(&StrAdapters::FindKey)},
    {"cGZPropertySetString_Destruct", 0x1777e0, 32, 0xd5e54385, reinterpret_cast<void *>(&StrAdapters::Destruct)},
    {"cGZPropertySetString_DeletingDestructor", 0x177a70, 32, 0x6a02b92a, reinterpret_cast<void *>(&StrAdapters::DeletingDestructor)},
    {"cGZPropertySetString_DestructBase", 0x1bed22, 23, 0x7c1aa134, reinterpret_cast<void *>(&StrAdapters::DestroyEntries)},
    {"cGZPropertySetUint32_DeletingDestructor", 0x1bedac, 31, 0xc7854492, reinterpret_cast<void *>(&U32Adapters::DeletingDestructor)},
    {"cGZPropertySetString_DeletingDestructorBase", 0x1bed8d, 31, 0xd637dd9f, reinterpret_cast<void *>(&StrAdapters::DeletingDestructorBase)},
};
REGISTER_HOOKS(kHooks);

} // namespace
