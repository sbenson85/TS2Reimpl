#pragma once
#include <cstddef>
#include <cstdint>

#include "gz/PixelFormat.h"

// Where the manager (cPixelFormatsManagerDX) gets its 56 static formats, its static unknown format and
// the one function it still borrows. A "store" is chosen once at startup (SetPixelFormatStoreProvider);
// the manager asks CurrentStore() and nothing else in the pixel format code touches the game image.
//
// Two stores:
//   * StandaloneStore() (PixelFormatStore.cpp): our own static objects, built from kStaticFormatSpecs
//     by our constructors, carrying our vtables. No image involved; this is the default.
//   * ImageStore() (PixelFormatImageStore.cpp, uses GameAddress): the same 56 objects in the image's own
//     storage (.bss/.data), which is where the image's static initialisers construct them (by calling
//     our hooked constructors, so they carry our vtables) and where the image keeps pointers to them.
//     The six objects the image constant-initialises (the five DXT formats and the unknown format)
//     are given our vtable on first use. Selected by the hook table (src/hook/tables/PixelFormatsManager.cpp).
//
// Findings behind the in-game choice (wave A; capstone over the unpacked exe, tools/xrefs.py):
//   * The only code that refers to the individual static objects is, per object, its static initialiser
//     (push the constructor arguments, mov ecx, object, call the constructor, register the destructor
//     with atexit) and that destructor (writes the BaseT vtable, frees the name; runs at exit), plus the
//     table below. The table (56 pointers, .rdata 0xd9cf78) is read by the manager's constructor and by
//     CreateIterator (0x15e860); the unknown format (0xfec094) by GetFormat/GetUnknownFormat and its
//     own initialiser (0x137cf0) and destructor. So the statics are reached only through the manager;
//     keeping them in image storage but constructed by our constructors is enough.
//   * The class and BaseT vtables are written only by the constructors, the deleting destructors, the
//     per-object atexit destructors and (for the DXT/unknown statics) the file image itself; no code
//     compares, reads or copies a vtable pointer. There is no RTTI use (no dynamic_cast/typeid,
//     no reference to the type descriptors from code).
//   * The five DXT statics (0xfec0b4 + 0x2c*n) have no constructor: .data holds vtable, 'DXTn' id,
//     bits, alpha bits and version, and their initialisers (0x1372d0, 0x137460, 0x1375f0, 0x137780,
//     0x137910) only set the name ("DXT" + n). Same for the unknown format (initialiser 0x137cf0 copies
//     "UNKNOWN"). Those six therefore carry the image's vtable until ImageStore adopts them.
//   * CreateIterator (manager slot 3, 0x15e860, with the cPixelFormatIterator class and its node pool)
//     is not reimplemented: the image store lends the image's function, the standalone store has none
//     and the slot aborts with a message.
namespace nGZGraphic4 {

class cPixelFormatsManagerDX;

constexpr uint32_t kFormatCount = 56;

struct PixelFormatStore {
    cPixelFormat *const *table; // kFormatCount pointers, in the original table order
    cPixelFormat *unknown;      // the static cPixelFormatUnknown
    // Manager slot 3 (ret 8): fills `out`, returns it. Null when not available (standalone).
    void *(__thiscall *createIterator)(cPixelFormatsManagerDX *self, void *out, uint32_t flags);
};

using PixelFormatStoreProvider = const PixelFormatStore &(*)();

const PixelFormatStore &StandaloneStore();
const PixelFormatStore &ImageStore();
void ResetImageStore(); // forget ImageStore's cached table (tests that rebuild the image's objects); the next use adopts again

// The provider the manager uses; StandaloneStore until changed.
void SetPixelFormatStoreProvider(PixelFormatStoreProvider provider);
PixelFormatStoreProvider CurrentPixelFormatStoreProvider();
inline const PixelFormatStore &CurrentStore() { return CurrentPixelFormatStoreProvider()(); }

// ---------------------------------------------------------------------------------------------------
// The 56 static formats, in the order of the original table (RVA 0xd9cf78): which constructor builds
// each and with what. The five DXT entries (index 32-36) are constant-initialised in the image; here
// they are built by the DXT constructor, which gives the same fields and name.
enum class StaticKind : uint8_t { Base, ARGB, AlphaLuminance, YUV, DepthStencil, ABGRFloat, DXT };

struct StaticFormatSpec {
    StaticKind kind;
    uint32_t id;      // D3DFORMAT value or FOURCC
    uint32_t args[5]; // the constructor's arguments after the id (see each kind below), unused ones 0
};
// Arguments: ARGB (total, alpha, red, green, blue); AlphaLuminance (total, alpha, luminance);
// YUV (the four FOURCC characters); DepthStencil (total, depth, stencil, float flag);
// ABGRFloat (alpha, blue, green, red); DXT (version); Base (none).
extern const StaticFormatSpec kStaticFormatSpecs[kFormatCount];

constexpr size_t kStaticFormatStorage = 0x38; // the largest format is 0x34 bytes
// Runs the constructor `spec` names on raw, 8-byte aligned storage of kStaticFormatStorage bytes.
cPixelFormat *ConstructStaticFormat(void *storage, const StaticFormatSpec &spec);

} // namespace nGZGraphic4
