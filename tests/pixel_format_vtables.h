#pragma once
// The one place the pixel format tests relate our vtables to the original's.
//
// Our objects carry vtables emitted by our build (src/gz/PixelFormatVTables.*); the original's carry the
// image's. A comparison of an original object with ours therefore differs in exactly one word, the
// vtable pointer, and nowhere else. That word is normalised here, and only here: kVTableMap lists the
// 17 vtables (8 class, 8 cPixelFormatBaseT<>, the manager) with their original RVA, and
// VTablesCorrespond asserts that the pointer found in the original object is exactly the mapped
// original vtable of that class and the pointer found in ours is exactly our vtable of that class.
// Every other byte is compared as before.
#include "difftest.h"
#include "gz/PixelFormat.h"
#include "gz/PixelFormatVTables.h"
#include "gz/PixelFormatsManager.h"

#include <cstring>

namespace pftest {

using nGZGraphic4::FormatClass;

struct VTableMapEntry {
    const char *name;
    int cls;           // FormatClass, or -1 for the manager
    bool baseT;        // the cPixelFormatBaseT<> instantiation of the class
    uint32_t originalRva; // 1.18.0.187
    int slots;
};

inline constexpr VTableMapEntry kVTableMap[] = {
    {"cPixelFormat", int(FormatClass::Base), false, 0xd9d130, 12},
    {"cPixelFormatABGRFloat", int(FormatClass::ABGRFloat), false, 0xd9d344, 17},
    {"cPixelFormatARGB", int(FormatClass::ARGB), false, 0xd9d21c, 19},
    {"cPixelFormatAlphaLuminance", int(FormatClass::AlphaLuminance), false, 0xd9d2b4, 17},
    {"cPixelFormatDXT", int(FormatClass::DXT), false, 0xd9d3d8, 18},
    {"cPixelFormatDepthStencil", int(FormatClass::DepthStencil), false, 0xd9d4d4, 15},
    {"cPixelFormatUnknown", int(FormatClass::Unknown), false, 0xd9d198, 12},
    {"cPixelFormatYUV", int(FormatClass::YUV), false, 0xd9d45c, 13},
    {"BaseT<cIPixelFormat>", int(FormatClass::Base), true, 0xd9d0fc, 12},
    {"BaseT<ABGRFloat>", int(FormatClass::ABGRFloat), true, 0xd9d2fc, 17},
    {"BaseT<ARGB>", int(FormatClass::ARGB), true, 0xd9d1cc, 19},
    {"BaseT<AlphaLuminance>", int(FormatClass::AlphaLuminance), true, 0xd9d26c, 17},
    {"BaseT<DXT>", int(FormatClass::DXT), true, 0xd9d38c, 18},
    {"BaseT<DepthStencil>", int(FormatClass::DepthStencil), true, 0xd9d494, 15},
    {"BaseT<Unknown>", int(FormatClass::Unknown), true, 0xd9d164, 12},
    {"BaseT<YUV>", int(FormatClass::YUV), true, 0xd9d424, 13},
    {"cPixelFormatsManagerDX", -1, false, 0xd9d0c4, 13},
};
inline constexpr int kVTableMapSize = int(sizeof(kVTableMap) / sizeof(kVTableMap[0]));

// Our vtable of an entry.
inline const void *OursOf(const VTableMapEntry &e)
{
    if (e.cls < 0) return nGZGraphic4::ManagerVTable();
    return e.baseT ? nGZGraphic4::BaseTVTable(FormatClass(e.cls)) : nGZGraphic4::ClassVTable(FormatClass(e.cls));
}

inline const VTableMapEntry &EntryFor(FormatClass cls, bool baseT)
{
    for (const VTableMapEntry &e : kVTableMap) {
        if (e.cls == int(cls) && e.baseT == baseT) return e;
    }
    return kVTableMap[0];
}

inline const VTableMapEntry &ManagerEntry() { return kVTableMap[kVTableMapSize - 1]; }

// The original vtable address that corresponds to one of ours, or null when `ours` is none of the 17.
inline const void *OriginalFor(const difftest::Context &ctx, const void *ours)
{
    for (const VTableMapEntry &e : kVTableMap) {
        if (OursOf(e) == ours) return ctx.image.At(e.originalRva);
    }
    return nullptr;
}

// The pointer found in the original object must be exactly the mapped original vtable of the entry, and
// the pointer found in ours exactly our vtable of the entry.
inline bool VTablesCorrespond(const difftest::Context &ctx, const VTableMapEntry &e, const void *origFound, const void *oursFound)
{
    return origFound == ctx.image.At(e.originalRva) && oursFound == OursOf(e) && OriginalFor(ctx, oursFound) == origFound;
}

inline bool VTablesCorrespond(const difftest::Context &ctx, FormatClass cls, bool baseT, const void *origFound, const void *oursFound)
{
    return VTablesCorrespond(ctx, EntryFor(cls, baseT), origFound, oursFound);
}

// The vtable word of an object.
inline const void *VTableOf(const void *object)
{
    const void *v;
    std::memcpy(&v, object, sizeof(v));
    return v;
}

} // namespace pftest
