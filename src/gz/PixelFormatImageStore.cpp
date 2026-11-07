#include "game/game.h"
#include "gz/PixelFormatStore.h"

namespace nGZGraphic4 {

namespace {

// The 56 static formats' storage in the image (RVAs, 1.18.0.187), in the order of the original table
// at 0xd9cf78 (the order of kStaticFormatSpecs). The 50 in .bss are constructed at startup by the
// image's static initialisers, which call our hooked constructors; the five DXT objects (0xfec0b4 +
// 0x2c * n) and the unknown format are constant-initialised in .data and carry the image's vtables
// until Adopt() below.
constexpr uint32_t kImageFormatRva[kFormatCount] = {
    0x10c9790, 0x10c97c4, 0x10c97f8, 0x10c982c, 0x10c9860, 0x10c9894, 0x10c98c8, 0x10c98fc, 0x10c9930, 0x10c9964, 0x10c9998, 0x10c9e30, 0x10c9e50, 0x10c9e70,
    0x10c9e90, 0x10c99cc, 0x10c9eb0, 0x10c9d70, 0x10c9d90, 0x10c9a00, 0x10c9a58, 0x10c9a84, 0x10c9ed0, 0x10c9ef0, 0x10c9f10, 0x10c9f30, 0x10c9f50, 0x10c9f70,
    0x10c9bd0, 0x10c9f90, 0x10c9bf8, 0x10c9fb0, 0xfec0b4,  0xfec0e0,  0xfec10c,  0xfec138,  0xfec164,  0x10c9db0, 0x10c9c20, 0x10c9c50, 0x10c9c80, 0x10c9cb0,
    0x10c9ce0, 0x10c9d10, 0x10c9dd0, 0x10c9d40, 0x10c9a2c, 0x10c9fd0, 0x10c9df0, 0x10c9ab0, 0x10c9ae0, 0x10c9b10, 0x10c9b40, 0x10c9b70, 0x10c9ba0, 0x10c9e10,
};
constexpr uint32_t kImageUnknownRva = 0xfec094;
constexpr uint32_t kImageCreateIteratorRva = 0x15e860; // cPixelFormatsManagerDX slot 3 (not reimplemented)

struct ImageObjects {
    cPixelFormat *table[kFormatCount];
    PixelFormatStore store;
    bool built;
};
ImageObjects gImage;

} // namespace

const PixelFormatStore &ImageStore()
{
    ImageObjects &o = gImage;
    if (!o.built) {
        for (uint32_t i = 0; i < kFormatCount; ++i) {
            cPixelFormat *format = GameAddress<cPixelFormat>(kImageFormatRva[i]);
            o.table[i] = format;
            // Adopt the constant-initialised objects: they were never constructed, so only the vtable
            // (and nothing the image's initialisers set: names) needs to become ours.
            if (kStaticFormatSpecs[i].kind == StaticKind::DXT) format->mVTable = ClassVTable(FormatClass::DXT);
        }
        o.store.table = o.table;
        o.store.unknown = GameAddress<cPixelFormat>(kImageUnknownRva);
        o.store.unknown->mVTable = ClassVTable(FormatClass::Unknown);
        o.store.createIterator = reinterpret_cast<decltype(o.store.createIterator)>(GameAddress<void>(kImageCreateIteratorRva));
        o.built = true;
    }
    return o.store;
}

void ResetImageStore()
{
    gImage = ImageObjects();
}

} // namespace nGZGraphic4
