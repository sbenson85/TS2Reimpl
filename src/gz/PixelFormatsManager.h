#pragma once
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "gz/PixelFormat.h"
#include "gz/PixelFormatStore.h"

// nGZGraphic4::cPixelFormatsManagerDX (cIPixelFormatsManager, original vtable 0xd9d0c4, 13 slots, 0x30
// bytes): the registry of the 56 static pixel formats. The constructor reads the format pointers from
// the current store (gz/PixelFormatStore.h: the original's table at RVA 0xd9cf78 holds the same 56
// pointers, in the same order, each a static object), indexes them by format id in an
// unordered_map and keeps them sorted (by Less, non-strict) in a vector, which the lookups
// binary-search with a temporary format built from the wanted parameters. RVAs are 1.18.0.187.
//
// Layout: +0 vtable; +4 std::unordered_map<format id, format> (0x20 bytes); +0x24 std::vector of the
// formats, sorted (first/last/end, 0x0c bytes).
//
// Slots: 0 QueryInterface, 1/2 AddRef/Release (0x15e510, ReturnOne), 3 CreateIterator (0x15e860, ret 8,
// not modelled: the store lends the image's function in-game and standalone has none), 4 GetFormat(id),
// 5 GetUnknownFormat, 6 GetARGB, 7 GetAlphaLuminance, 8 GetABGRFloat, 9 GetYUV, 10 GetDXT,
// 11 GetDepthStencil, 12 deleting destructor (0x15e3d0, game code (lane WI; once noted as a library match): Destruct then operator
// delete(p, 0x30); hooked). The vtable is ours
// (ManagerVTable(), gz/PixelFormatVTables.h).
namespace nGZGraphic4 {

constexpr uint32_t kIidPixelFormatsManager = 0xdac38a6f;

class cPixelFormatsManagerDX {
public:
    static constexpr uint32_t kSize = 0x30;
    static constexpr uint32_t kFormatCount = nGZGraphic4::kFormatCount;

    // A half-open range of the sorted vector, as std::equal_range returns it.
    struct Range {
        cPixelFormat *const *first;
        cPixelFormat *const *last;
    };

    // 0x15eb10 (cdecl; lane RB): the one manager, built by Construct on the first call and destroyed (Destruct) at exit.
    // Hooked: the switch (the image's function-local static 0x10ca018 and its guard 0x10ca048 are no longer used).
    static cPixelFormatsManagerDX *Instance();

    cPixelFormatsManagerDX *Construct(); // 0x15d4b0
    void Destruct();                     // 0x15db70 (the destructor body; the deleting version is slot 12)

    bool QueryInterface(uint32_t riid, void **ppv); // slot 0, 0x15fc70: 1 and 0xdac38a6f
    uint32_t ReturnOne() { return 1; }              // slots 1, 2 (AddRef, Release), 0x15e510 (shared)
    // slot 3, 0x15e860, ret 8: not reimplemented; runs the store's createIterator, aborting when it has none.
    void *CreateIterator(void *out, uint32_t flags);
    // slot 4, 0x15e980, ret 4: the format with this id, or the unknown format (never null).
    cPixelFormat *GetFormat(uint32_t formatId);
    cPixelFormat *GetUnknownFormat();               // slot 5, 0x15f800
    // Slots 6-11: find the format with exactly these parameters (a temporary format is built from them
    // and binary-searched; the match is asked for the class's interface and released again), or null.
    cPixelFormat *GetARGB(uint32_t totalBits, uint32_t alphaBits, uint32_t redBits, uint32_t greenBits, uint32_t blueBits); // slot 6, 0x15f480, ret 0x14
    cPixelFormat *GetAlphaLuminance(uint32_t totalBits, uint32_t alphaBits, uint32_t luminanceBits);                         // slot 7, 0x15f560, ret 0xc
    cPixelFormat *GetABGRFloat(uint32_t alphaBits, uint32_t blueBits, uint32_t greenBits, uint32_t redBits);                // slot 8, 0x15f3a0, ret 0x10
    cPixelFormat *GetYUV(const uint8_t *fourcc);                                                                             // slot 9, 0x15f810, ret 4: four characters
    cPixelFormat *GetDXT(uint32_t version);                                                                                  // slot 10, 0x15f640, ret 4
    cPixelFormat *GetDepthStencil(uint32_t totalBits, uint32_t depthBits, uint32_t stencilBits, uint32_t floatDepth);       // slot 11, 0x15f720, ret 0x10
    void *DeletingDestructor(uint32_t flags);       // slot 12, 0x15e3d0 (hooked), ret 4: Destruct, then operator delete(this, 0x30) if flags & 1

    // 0x15b030 (cdecl): std::sort of formats by a->Less(b, false) (class id, then format id: a total order). `ideal` is last - first.
    // 0x15b4a0 (cdecl): std::equal_range of *key in [first, last) by a->Less(b, true) (the strict ordering by contents). `unused` is a
    // stack slot the caller fills with junk.
    static Range *EqualRange(Range *result, cPixelFormat *const *first, cPixelFormat *const *last, cPixelFormat *const *key, uint32_t unused);

    const void *mVTable;                                // +0x00
    std::unordered_map<uint32_t, cPixelFormat *> mById; // +0x04
    std::vector<cPixelFormat *> mSorted;                // +0x24
};

static_assert(offsetof(cPixelFormatsManagerDX, mById) == 4, "manager: map at +4");
static_assert(offsetof(cPixelFormatsManagerDX, mSorted) == 0x24, "manager: vector at +0x24");
static_assert(sizeof(cPixelFormatsManagerDX) == cPixelFormatsManagerDX::kSize, "manager: 0x30 bytes");

} // namespace nGZGraphic4
