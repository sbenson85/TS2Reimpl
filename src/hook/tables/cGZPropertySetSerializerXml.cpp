#include "hook/hooks.h"
#include "gz/cGZPropertySetSerializer.h"

namespace {

using A = cGZPropertySetSerializerXmlStringAdapters;
using U = cGZPropertySetSerializerXmlUint32Adapters;

// The XML delegate of the string-keyed property-set serializer (lane DV). Write 0x556a30: lane DY. Standalone shape (lane EF,
// docs/standalone-shape.md): the dispatcher's `new` and the constructor hook (0x551a60, called by the game's 0xcb1cf0 /
// 0xcb28b0) give every delegate our vtables; the entries are adapters for direct calls and for objects with the game's
// vtables (the want-tree loader's derived class, built inline by cTSWantTreeLoader::vf3). The class's switch (bisect): the
// constructor hook plus the dispatcher's _ReadWith / _Write. Not hooked: the +4 face's QueryInterface thunk 0x555479
// (8 bytes) and the folded AddRef / Release bodies and thunks (0x145260, 0x242830, 0x1448e9, 0x242845).
const HookSpec kHooks[] = {
    {"cGZPropertySetSerializerXmlString_Construct", 0x551a60, 32, 0x78341479, reinterpret_cast<void *>(&A::Construct)},
    {"cGZPropertySetSerializerXmlString_QueryInterface", 0x555430, 32, 0xb40b8a1b, reinterpret_cast<void *>(&A::QueryInterface)},
    {"cGZPropertySetSerializerXmlString_Read", 0x555d50, 32, 0x345dcf69, reinterpret_cast<void *>(&A::Read)},
    {"cGZPropertySetSerializerXmlString_Destruct", 0x551e20, 32, 0x6675f6b1, reinterpret_cast<void *>(&A::Destruct)},
    {"cGZPropertySetSerializerXmlString_DeletingDestructor", 0x552200, 32, 0x397ba835, reinterpret_cast<void *>(&A::DeletingDestructor)},
    {"cGZPropertySetSerializerXmlString_KeyFromString", 0x556540, 32, 0x7d72b7f8, reinterpret_cast<void *>(&A::KeyFromString)},
    {"cGZPropertySetSerializerXmlString_StartElement", 0x552f30, 32, 0xd5739b69, reinterpret_cast<void *>(&A::StartElement)},
    {"cGZPropertySetSerializerXmlString_EndElement", 0x552630, 32, 0x9a26c052, reinterpret_cast<void *>(&A::EndElement)},
    {"cGZPropertySetSerializerXmlString_CharacterData", 0x5525f0, 32, 0x7f4409d9, reinterpret_cast<void *>(&A::CharacterData)},
    {"cGZPropertySetSerializerXmlString_Write", 0x556a30, 32, 0x2cf46d93, reinterpret_cast<void *>(&A::Write)},
    // The uint32 XML delegate in the standalone shape (lane EU; the same template, src/gz/cGZPropertySetSerializerUint32Delegates
    // .size.cpp). Built only by the uint32 dispatcher's ReadWith / Write (ours: `new`; the constructor 0x241ea6 has no other
    // caller, hooked anyway). The class's switch (bisect): the uint32 dispatcher's _ReadWith / _Write plus _Construct here. Not
    // hooked: the +4 face's QI thunk 0x2424f2 (8 bytes, ours in the vtable) and the folded AddRef / Release bodies and thunks.
    {"cGZPropertySetSerializerXmlUint32_Construct", 0x241ea6, 32, 0x8ff5f0df, reinterpret_cast<void *>(&U::Construct)},
    {"cGZPropertySetSerializerXmlUint32_Destruct", 0x241f82, 32, 0xd7f703c9, reinterpret_cast<void *>(&U::Destruct)},
    {"cGZPropertySetSerializerXmlUint32_DeletingDestructor", 0x241fe8, 31, 0xefb680e9, reinterpret_cast<void *>(&U::DeletingDestructor)},
    {"cGZPropertySetSerializerXmlUint32_QueryInterface", 0x24249f, 32, 0x37792a56, reinterpret_cast<void *>(&U::QueryInterface)},
    {"cGZPropertySetSerializerXmlUint32_Read", 0x242641, 32, 0xaaece47d, reinterpret_cast<void *>(&U::Read)},
    {"cGZPropertySetSerializerXmlUint32_Write", 0x242901, 32, 0x86dc614e, reinterpret_cast<void *>(&U::Write)},
    {"cGZPropertySetSerializerXmlUint32_KeyFromString", 0x24287e, 27, 0x3a693b15, reinterpret_cast<void *>(&U::KeyFromString)},
    {"cGZPropertySetSerializerXmlUint32_AssignRootName", 0x24204a, 21, 0xc890755c, reinterpret_cast<void *>(&U::AssignRootName)},
    {"cGZPropertySetSerializerXmlUint32_KeyToText", 0x242482, 29, 0x20d4f2ed, reinterpret_cast<void *>(&U::KeyToText)},
    {"cGZPropertySetSerializerXmlUint32_StartElement", 0x2422ae, 32, 0x20292523, reinterpret_cast<void *>(&U::StartElement)},
    {"cGZPropertySetSerializerXmlUint32_EndElement", 0x242090, 32, 0xe438e01a, reinterpret_cast<void *>(&U::EndElement)},
    {"cGZPropertySetSerializerXmlUint32_CharacterData", 0x242064, 32, 0x4d390672, reinterpret_cast<void *>(&U::CharacterData)},
};
REGISTER_HOOKS(kHooks);

} // namespace
