#pragma once
#include "gz/cIGZUnknown.h"

#include <cstdint>

// cRZUnknown as a real base class, for classes in the standalone shape (docs/standalone-shape.md) that derive from it: a
// vtable and a reference count at +4 (the game's vtable 0xda2fd0, 6 slots). The hook-shaped struct of the same name in
// rz/cRZRefCount.h (non-polymorphic, `mVTable` member, used by 16 files and the cRZRefCount hooks) holds the global name, so
// this one lives in namespace rz until cRZRefCount's group is taken to the shape and the two merge. Its RTTI name is then
// "rz::cRZUnknown" instead of the original's "cRZUnknown": nothing in the image reads RTTI (docs/standalone-shape.md).
//
// The bodies are the game's shared ones (out of line, as the game's: an override that forwards to one compiles to the original's
// `add ecx, N; jmp`), none hooked here: their entries belong to the cRZRefCount / RZFrameworkHelpers groups.
// First user: cTSNeighborhoodInfo (lane EA).
namespace rz {

class cRZUnknown : public cIGZUnknown {
public:
    cRZUnknown() : mRefCount(0) {} // 0x1b9052 (the vtable, a zero count)

    // slot 0, 0x1b945c (ret 8): this for interface id 1 (counted through slot 1), false for anything else (out untouched).
    bool QueryInterface(uint32_t riid, void **out) override;
    // slot 1, 0x148fb0: ++count, the new count.
    uint32_t AddRef() override;
    // slot 2, 0x1b94e9: at a count of 1 or below, delete this (slot 3 with 1) and 0; else --count.
    uint32_t Release() override;
    // slot 3: the scalar deleting destructor (the game's 0x1b90f0; the destructor 0x1b9096 only stores the vtable).
    virtual ~cRZUnknown();
    // slot 4, 0x1b9538: --count unless already 0; the count.
    virtual uint32_t RemoveRef();
    // slot 5, 0x147a30: the count. (Not named GetRefCount: a class that declares GetRefCount for another interface, as
    // cTSNeighborhoodInfo for cIGZPersistResource, leaves this slot alone in the original.)
    virtual uint32_t RefCount();

    cRZUnknown(const cRZUnknown &) = delete;
    cRZUnknown &operator=(const cRZUnknown &) = delete;

    uint32_t mRefCount; // +4
};
static_assert(sizeof(cRZUnknown) == 8, "cRZUnknown: vtable, count");

} // namespace rz
