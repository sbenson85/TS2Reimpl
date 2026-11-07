#pragma once
#include <cstdint>

// cRZRefCount as a real base class, for classes in the standalone shape (docs/standalone-shape.md) that derive from it: a
// vtable and a reference count at +4 (the game's vtable 0xda2fa8, 5 slots). As rz::cRZUnknown (rz/RZUnknownBase.h), it lives
// in namespace rz because the hook-shaped struct of the same name in rz/cRZRefCount.h holds the global name; nothing in the
// image reads RTTI. No QueryInterface: the count's own interface has no cIGZUnknown.
//
// The bodies are the game's shared ones (out of line, so an override that forwards to one compiles to the original's
// `add ecx, N; jmp`); none is hooked here: their entries belong to the cRZRefCount / RZFrameworkHelpers groups.
// First user: nGZCommandParser::cFileSource (lane IM).
namespace rz {

class cRZRefCount {
public:
    cRZRefCount() : mRefCount(0) {}

    // slot 0: the scalar deleting destructor (the game's 0x1b90b2; the destructor 0x1b9088 only stores the vtable).
    virtual ~cRZRefCount();
    // slot 1, 0x1b94af: at a count of 1 or below, delete this (slot 0 with 1) and 0; else --count.
    virtual uint32_t Release();
    // slot 2, 0x148fb0: ++count, the new count.
    virtual uint32_t AddRef();
    // slot 3, 0x1b9538: --count unless already 0; the count.
    virtual uint32_t RemoveRef();
    // slot 4, 0x147a30: the count.
    virtual uint32_t RefCount();

    cRZRefCount(const cRZRefCount &) = delete;
    cRZRefCount &operator=(const cRZRefCount &) = delete;

    uint32_t mRefCount; // +4
};
static_assert(sizeof(cRZRefCount) == 8, "cRZRefCount: vtable, count");

} // namespace rz
