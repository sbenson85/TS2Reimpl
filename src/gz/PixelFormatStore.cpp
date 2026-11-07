#include "gz/PixelFormatStore.h"

#include <new>

namespace nGZGraphic4 {

namespace {

constexpr StaticFormatSpec Base(uint32_t id) { return {StaticKind::Base, id, {0, 0, 0, 0, 0}}; }
constexpr StaticFormatSpec Argb(uint32_t id, uint32_t total, uint32_t a, uint32_t r, uint32_t g, uint32_t b) { return {StaticKind::ARGB, id, {total, a, r, g, b}}; }
constexpr StaticFormatSpec AlphaLum(uint32_t id, uint32_t total, uint32_t a, uint32_t l) { return {StaticKind::AlphaLuminance, id, {total, a, l, 0, 0}}; }
constexpr StaticFormatSpec Yuv(uint32_t id, char c0, char c1, char c2, char c3)
{
    return {StaticKind::YUV, id, {static_cast<uint8_t>(c0), static_cast<uint8_t>(c1), static_cast<uint8_t>(c2), static_cast<uint8_t>(c3), 0}};
}
constexpr StaticFormatSpec Depth(uint32_t id, uint32_t total, uint32_t depth, uint32_t stencil, uint32_t isFloat) { return {StaticKind::DepthStencil, id, {total, depth, stencil, isFloat, 0}}; }
constexpr StaticFormatSpec AbgrFloat(uint32_t id, uint32_t a, uint32_t b, uint32_t g, uint32_t r) { return {StaticKind::ABGRFloat, id, {a, b, g, r, 0}}; }
constexpr StaticFormatSpec Dxt(uint32_t version) { return {StaticKind::DXT, 0x31545844u + ((version - 1) << 24), {version, 0, 0, 0, 0}}; }

} // namespace

// In the order of the table at 0xd9cf78. Derived from the push sequences of the 51 static initialisers
// (0x136f50-0x137e90 and the three before them) and the five constant-initialised DXT objects; the
// difftest builds the original's 56 objects by running those initialisers and compares every one of
// these entries against them.
const StaticFormatSpec kStaticFormatSpecs[kFormatCount] = {
    Argb(0x14, 0x18, 0x0, 0x8, 0x8, 0x8), // 0  R8G8B8
    Argb(0x15, 0x20, 0x8, 0x8, 0x8, 0x8), // 1  A8R8G8B8
    Argb(0x16, 0x20, 0x0, 0x8, 0x8, 0x8), // 2  X8R8G8B8
    Argb(0x17, 0x10, 0x0, 0x5, 0x6, 0x5), // 3  R5G6B5
    Argb(0x18, 0x10, 0x0, 0x5, 0x5, 0x5), // 4  X1R5G5B5
    Argb(0x19, 0x10, 0x1, 0x5, 0x5, 0x5), // 5  A1R5G5B5
    Argb(0x1a, 0x10, 0x4, 0x4, 0x4, 0x4), // 6  A4R4G4B4
    Argb(0x1b, 0x8, 0x0, 0x3, 0x3, 0x2),  // 7  R3G3B2
    Argb(0x1c, 0x8, 0x8, 0x0, 0x0, 0x0),  // 8  A8
    Argb(0x1d, 0x10, 0x8, 0x3, 0x3, 0x2), // 9  A8R3G3B2
    Argb(0x1e, 0x10, 0x0, 0x4, 0x4, 0x4), // 10 X4R4G4B4
    Base(0x1f),                           // 11
    Base(0x20),                           // 12
    Base(0x21),                           // 13
    Base(0x22),                           // 14
    Argb(0x23, 0x20, 0x2, 0xa, 0xa, 0xa), // 15 A2R10G10B10
    Base(0x24),                           // 16
    Base(0x28),                           // 17
    Base(0x29),                           // 18
    AlphaLum(0x32, 0x8, 0x0, 0x8),        // 19 L8
    AlphaLum(0x33, 0x10, 0x8, 0x8),       // 20 A8L8
    AlphaLum(0x34, 0x8, 0x4, 0x4),        // 21 A4L4
    Base(0x3c),                           // 22
    Base(0x3d),                           // 23
    Base(0x3e),                           // 24
    Base(0x3f),                           // 25
    Base(0x40),                           // 26
    Base(0x43),                           // 27
    Yuv(0x59565955, 'U', 'Y', 'V', 'Y'),  // 28 UYVY
    Base(0x47424752),                     // 29 RGBG
    Yuv(0x32595559, 'Y', 'U', 'Y', '2'),  // 30 YUY2
    Base(0x42475247),                     // 31 GRGB
    Dxt(1),                               // 32 DXT1
    Dxt(2),                               // 33 DXT2
    Dxt(3),                               // 34 DXT3
    Dxt(4),                               // 35 DXT4
    Dxt(5),                               // 36 DXT5
    Base(0x46),                           // 37
    Depth(0x47, 0x20, 0x20, 0x0, 0x0),    // 38
    Depth(0x49, 0x10, 0xf, 0x1, 0x0),     // 39
    Depth(0x4b, 0x20, 0x18, 0x8, 0x0),    // 40
    Depth(0x4d, 0x20, 0x18, 0x0, 0x0),    // 41
    Depth(0x4f, 0x20, 0x18, 0x4, 0x0),    // 42
    Depth(0x50, 0x10, 0x10, 0x0, 0x0),    // 43
    Base(0x52),                           // 44
    Depth(0x53, 0x20, 0x18, 0x8, 0x1),    // 45
    AlphaLum(0x51, 0x10, 0x0, 0x10),      // 46 L16
    Base(0x6e),                           // 47
    Base(0x3154454d),                     // 48 MET1
    AbgrFloat(0x6f, 0x0, 0x0, 0x0, 0x10), // 49 R16F
    AbgrFloat(0x70, 0x0, 0x0, 0x10, 0x10), // 50 G16R16F
    AbgrFloat(0x71, 0x10, 0x10, 0x10, 0x10), // 51 A16B16G16R16F
    AbgrFloat(0x72, 0x0, 0x0, 0x0, 0x20), // 52 R32F
    AbgrFloat(0x73, 0x0, 0x0, 0x20, 0x20), // 53 G32R32F
    AbgrFloat(0x74, 0x20, 0x20, 0x20, 0x20), // 54 A32B32G32R32F
    Base(0x75),                           // 55
};

cPixelFormat *ConstructStaticFormat(void *storage, const StaticFormatSpec &spec)
{
    const uint32_t *a = spec.args;
    switch (spec.kind) {
    case StaticKind::Base: return (new (storage) cPixelFormat)->Construct(spec.id);
    case StaticKind::ARGB: return (new (storage) cPixelFormatARGB)->Construct(spec.id, a[0], a[1], a[2], a[3], a[4]);
    case StaticKind::AlphaLuminance: return (new (storage) cPixelFormatAlphaLuminance)->Construct(spec.id, a[0], a[1], a[2]);
    case StaticKind::YUV: return (new (storage) cPixelFormatYUV)->Construct(spec.id, a[0], a[1], a[2], a[3]);
    case StaticKind::DepthStencil: return (new (storage) cPixelFormatDepthStencil)->Construct(spec.id, a[0], a[1], a[2], static_cast<uint8_t>(a[3]));
    case StaticKind::ABGRFloat: return (new (storage) cPixelFormatABGRFloat)->Construct(spec.id, a[0], a[1], a[2], a[3]);
    case StaticKind::DXT: return (new (storage) cPixelFormatDXT)->Construct(spec.id, a[0]);
    }
    return nullptr;
}

// ---------------------------------------------------------------------------------------------------
// Standalone store

namespace {

struct alignas(8) Storage {
    unsigned char bytes[kStaticFormatStorage];
};

// Plain zero-initialised globals, filled in on first use (startup is single-threaded: the first user
// is the manager's constructor, and nothing needs a lock or a magic static).
struct StandaloneObjects {
    Storage formats[kFormatCount];
    Storage unknown;
    cPixelFormat *table[kFormatCount];
    PixelFormatStore store;
    bool built;
};
StandaloneObjects gStandalone;

} // namespace

const PixelFormatStore &StandaloneStore()
{
    StandaloneObjects &o = gStandalone;
    if (!o.built) {
        for (uint32_t i = 0; i < kFormatCount; ++i) o.table[i] = ConstructStaticFormat(o.formats[i].bytes, kStaticFormatSpecs[i]);
        o.store.table = o.table;
        o.store.unknown = (new (o.unknown.bytes) cPixelFormatUnknown)->InitStatic();
        o.store.createIterator = nullptr;
        o.built = true;
    }
    return o.store;
}

namespace {
// constinit: the provider is set before any dynamic initialiser runs, so the hook table's startup selection
// (a dynamic initialiser in another translation unit) can never be overwritten by this one.
constinit PixelFormatStoreProvider gProvider = &StandaloneStore;
}

void SetPixelFormatStoreProvider(PixelFormatStoreProvider provider)
{
    gProvider = provider ? provider : &StandaloneStore;
}

PixelFormatStoreProvider CurrentPixelFormatStoreProvider()
{
    return gProvider;
}

} // namespace nGZGraphic4
