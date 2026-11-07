#include "hook/hooks.h"
#include "gz/cGZDBPFIndexModifiable.h"

// Standalone shape (docs/standalone-shape.md): the constructor hook (0x9a6cf0; the game's caller is 0x9af590, ours build the
// class with `new`) is the class's switch: no object carries the game's vtable 0xe5ff40, so the slot functions are in effect
// through their callers (trapped on this hook, src/hook/trap_table.cpp). The byte-matched slots keep their adapters.
namespace {

using A = cGZDBPFIndexModifiableAdapters;

const HookSpec kHooks[] = {
    {"cGZDBPFIndexModifiable_Construct", 0x9a6cf0, 32, 0x66d4e0d6, reinterpret_cast<void *>(&A::Construct)},
    {"cGZDBPFIndexModifiable_vf4", 0x9acc10, 26, 0xd2f94513, reinterpret_cast<void *>(&A::GetTotalSize)},
    {"cGZDBPFIndexModifiable_vf7", 0x9ac720, 10, 0x4a21657f, reinterpret_cast<void *>(&A::GetSerializedSize)},
};
REGISTER_HOOKS(kHooks);

} // namespace
