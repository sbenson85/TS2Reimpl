#include "hook/hooks.h"
#include "gz/cGZAny.h"

namespace {

using StringCopyCtor = AnyString *(AnyString::*)(const AnyString &);
using StringTextCtor = AnyString *(AnyString::*)(const cIGZString &);
using StringCharCtor = AnyString *(AnyString::*)(const char *);

// The cIGZAny values (lane DQ). The folded bodies 0x14c5f0 (AnyUint32 / AnyBoolean slot 0), 0x1bedcb (AnySint32 /
// AnyFloat32 / AnyAny slot 0) and 0x1bf068 (their slot 1) are shared only within this family (xrefs: these vtables
// only; docs/verification.md "shared_vf within one family"), so they are hooked (lane VX; CRCs by tools/hookcrc.py).
// AnyUint32, AnyBoolean and AnyFloat32 are in the standalone shape (lane GZ): their rows point at the adapters, which
// serve the objects the game builds with its vtables; objects built by our code carry our vtables.
const HookSpec kHooks[] = {
    {"cIGZAny_DeletingDestructor", 0x14c840, 32, 0x6d11f3ba, MemberAddress(&cGZAny::DeletingDestructor)},
    {"AnyValue_DeletingDestructor", 0x14c5f0, 32, 0x360f45d9, MemberAddress(&cGZAny::ValueDeletingDestructor)},
    {"AnySint32_DeletingDestructor", 0x1bedcb, 32, 0x69b49cd9, MemberAddress(&AnySint32::DeletingDestructor)},
    {"AnySint32_Assign", 0x1bf068, 32, 0x2be0baf5, MemberAddress(&AnySint32::Assign)},
    {"AnyUint32_Assign", 0x177b90, 32, 0xa4822da2, reinterpret_cast<void *>(&gz::AnyAdapters<gz::AnyUint32>::Assign)},
    {"AnyUint32_Compare", 0x177b70, 27, 0x4a5698df, reinterpret_cast<void *>(&gz::AnyAdapters<gz::AnyUint32>::Compare)},
    {"AnyUint32_Clone", 0x177ab0, 32, 0x4c340a5f, reinterpret_cast<void *>(&gz::AnyAdapters<gz::AnyUint32>::Clone)},
    {"AnyUint32_GetType", 0x179d00, 6, 0x12a072a3, reinterpret_cast<void *>(&gz::AnyAdapters<gz::AnyUint32>::GetType)},
    {"AnyUint32_GetTypeName", 0x179d10, 6, 0x45d2423a, reinterpret_cast<void *>(&gz::AnyAdapters<gz::AnyUint32>::GetTypeName)},
    {"AnyBoolean_Assign", 0x14e760, 32, 0xa5404795, reinterpret_cast<void *>(&gz::AnyAdapters<gz::AnyBoolean>::Assign)},
    {"AnyBoolean_Compare", 0x14e5b0, 29, 0xad56c8c8, reinterpret_cast<void *>(&gz::AnyAdapters<gz::AnyBoolean>::Compare)},
    {"AnyBoolean_Clone", 0x14df70, 32, 0x46ae7c0e, reinterpret_cast<void *>(&gz::AnyAdapters<gz::AnyBoolean>::Clone)},
    {"AnyBoolean_GetType", 0x1522b0, 6, 0x6b910d5b, reinterpret_cast<void *>(&gz::AnyAdapters<gz::AnyBoolean>::GetType)},
    {"AnyBoolean_GetTypeName", 0x1522c0, 6, 0xbbd95b6e, reinterpret_cast<void *>(&gz::AnyAdapters<gz::AnyBoolean>::GetTypeName)},
    {"AnySint32_Compare", 0x1befcf, 28, 0xec72c8c2, MemberAddress(&AnySint32::Compare)},
    {"AnySint32_Clone", 0x1bef56, 32, 0xae28e0ef, MemberAddress(&AnySint32::Clone)},
    {"AnySint32_GetType", 0x1bf5c5, 6, 0xc9a07289, MemberAddress(&AnySint32::GetType)},
    {"AnySint32_GetTypeName", 0x1bf5dd, 6, 0xf05cf88f, MemberAddress(&AnySint32::GetTypeName)},
    {"AnyFloat32_Compare", 0x1befac, 32, 0xccabac24, reinterpret_cast<void *>(&gz::AnyAdapters<gz::AnyFloat32>::Compare)},
    {"AnyFloat32_Clone", 0x1bef35, 32, 0xc5ce9b9f, reinterpret_cast<void *>(&gz::AnyAdapters<gz::AnyFloat32>::Clone)},
    {"AnyFloat32_GetType", 0x1bf5bf, 6, 0xdcdb8315, reinterpret_cast<void *>(&gz::AnyAdapters<gz::AnyFloat32>::GetType)},
    {"AnyFloat32_GetTypeName", 0x1bf5d7, 6, 0xe8f87491, reinterpret_cast<void *>(&gz::AnyAdapters<gz::AnyFloat32>::GetTypeName)},
    {"AnyAny_Compare", 0x1bef94, 24, 0x7b56fb64, MemberAddress(&AnyAny::Compare)},
    {"AnyAny_Clone", 0x1bef14, 32, 0xdb719055, MemberAddress(&AnyAny::Clone)},
    {"AnyAny_GetType", 0x1bf5b9, 6, 0x19d47e69, MemberAddress(&AnyAny::GetType)},
    {"AnyAny_GetTypeName", 0x1bf5d1, 6, 0x2b4bfc6f, MemberAddress(&AnyAny::GetTypeName)},
    {"AnyString_CopyConstruct", 0x1bec79, 32, 0xdaebbb40, MemberAddress(static_cast<StringCopyCtor>(&AnyString::Construct))},
    {"AnyString_ConstructFromString", 0x1ce6ab, 32, 0xb2678222, MemberAddress(static_cast<StringTextCtor>(&AnyString::Construct))},
    {"AnyString_ConstructFromChar", 0x213fbb, 32, 0xf3b09199, MemberAddress(static_cast<StringCharCtor>(&AnyString::Construct))},
    {"AnyString_Destruct", 0x1bed50, 23, 0x31de77c3, MemberAddress(&AnyString::Destruct)},
    {"AnyString_DeletingDestructor", 0x1bedeb, 31, 0x6479387c, MemberAddress(&AnyString::DeletingDestructor)},
    {"AnyString_Assign", 0x1bf097, 32, 0x2717818e, MemberAddress(&AnyString::Assign)},
    {"AnyString_Compare", 0x1befeb, 20, 0xe8b1b1c5, MemberAddress(&AnyString::Compare)},
    {"AnyString_Clone", 0x1bef77, 29, 0xd78058e1, MemberAddress(&AnyString::Clone)},
    {"AnyString_GetType", 0x1bf5cb, 6, 0xa2f0f304, MemberAddress(&AnyString::GetType)},
    {"AnyString_GetTypeName", 0x1bf5e3, 6, 0x79a987d6, MemberAddress(&AnyString::GetTypeName)},
    {"AnyStringSet_CopyConstruct", 0x236c35, 32, 0x19fa0865, MemberAddress(&AnyStringSet::Construct)},
    {"AnyStringSet_Destruct", 0x236f02, 27, 0xc8cdd4c9, MemberAddress(&AnyStringSet::Destruct)},
    {"AnyStringSet_Contains", 0x237c75, 32, 0x718c1b34, MemberAddress(&AnyStringSet::Contains)},
    {"AnyStringSet_DeletingDestructor", 0x237034, 31, 0x0d4ef9a8, MemberAddress(&AnyStringSet::DeletingDestructor)},
    {"AnyStringSet_Assign", 0x237679, 32, 0x10c971bc, MemberAddress(&AnyStringSet::Assign)},
    {"AnyStringSet_Compare", 0x237621, 32, 0x4c4b3a22, MemberAddress(&AnyStringSet::Compare)},
    {"AnyStringSet_Clone", 0x2375ea, 32, 0x151c1a72, MemberAddress(&AnyStringSet::Clone)},
    {"AnyStringSet_GetType", 0x238790, 6, 0xb9f99c37, MemberAddress(&AnyStringSet::GetType)},
    {"AnyStringSet_GetTypeName", 0x238796, 6, 0xc5ea3cae, MemberAddress(&AnyStringSet::GetTypeName)},
    {"GZStringSet_Construct", 0x551af0, 32, 0x3b79d138, MemberAddress(&GZStringSetOps::Construct)},
    {"GZStringSet_Destruct", 0x551d90, 32, 0xdd9690d9, MemberAddress(&GZStringSetOps::Destruct)},
    {"GZStringSet_Insert", 0x5573a0, 32, 0x55aa2d46, MemberAddress(&GZStringSetOps::Insert)},
    {"GZStringSet_InsertSize", 0x236a4b, 32, 0x6cde72b3, MemberAddress(&GZStringSetOps::InsertSize)},
    {"GZStringSet_Find", 0x236b0b, 32, 0xbed471f4, MemberAddress(&GZStringSetOps::Find)},
};
REGISTER_HOOKS(kHooks);

} // namespace
