#pragma once
#include <cstddef>
#include <cstdint>
#include <new>
#include <string>

#include "game/vtable.h"
#include "gz/PixelFormatVTables.h"

// The nGZGraphic4 pixel format objects: cPixelFormat (the generic base) and its seven concrete
// descriptions. Each format is a small object that names a texel layout and answers questions about it
// (bits per pixel, bytes per row, per-channel bit counts); the manager (cPixelFormatsManagerDX, not
// modelled here) keeps them in a sorted vector ordered by Less and looks formats up by building a
// temporary object on the stack. RVAs are 1.18.0.187.
//
// Every class carries the same first 0x20 bytes (the cPixelFormatBaseT<> part): the vtable at +0, the
// format id at +4 (a D3DFORMAT value or FOURCC; 0 for lookup keys) and the name, a std::string, at +8.
// The derived classes append plain 32-bit fields from +0x20. Interface ids (the "class id" of slot 5)
// are the ids QueryInterface answers to besides cIGZUnknown (1) and the base id.
//
// Slot map (the same for all classes; cIPixelFormat is slots 0-10, the derived interfaces add more):
//    0 QueryInterface(riid, ppv)      5 GetClassId()                    10 Less(other, strict)
//    1 AddRef, 2 Release (both 1)     6 GetBitsPerPixel()               11.. per class (see each class)
//    3 GetFormatId()                  7 GetBlockHeight()  (guess)
//    4 GetName()                      8 GetBlockWidth()   (guess)
//                                     9 GetRowBytes(width)
// The objects are static, so AddRef and Release return 1 and keep no count. The vtables are ours
// (gz/PixelFormatVTables.h): explicit constant tables in the original slot order, one for each class
// and one for each class's cPixelFormatBaseT<> instantiation (the constructor/destructor-time vtable
// the deleting destructors write); the BaseT ones share the functions below, parameterised by
// interface id (the *T templates), and put _purecall in the slots only the derived class fills.
//
// Slots shared by several classes are implemented once per class (the member that reads that class's
// field) and the RVA's status entry lists the other users.
namespace nGZGraphic4 {

// Interface ids accepted by QueryInterface (compared directly).
constexpr uint32_t kIidUnknown = 1;               // cIGZUnknown
constexpr uint32_t kIidPixelFormat = 0xbac3896f;  // cIPixelFormat (also the base class id)
constexpr uint32_t kIidRgbColorSpace = 0x4c96818d; // cIPixelFormatRGBColorSpace (the four colour classes)
constexpr uint32_t kIidABGRFloat = 0x2b3f3457;
constexpr uint32_t kIidARGB = 0x9ac38991;
constexpr uint32_t kIidAlphaLuminance = 0x2cb4c095;
constexpr uint32_t kIidDXT = 0xbac3f24f;
constexpr uint32_t kIidDepthStencil = 0x5ac389b6;
constexpr uint32_t kIidUnknownFormat = 0x5b672736; // cIPixelFormatUnknown
constexpr uint32_t kIidYUV = 0x9ac3899e;

// FormatClass (the generic base and the seven concrete ones), ClassVTable() and BaseTVTable() come
// from gz/PixelFormatVTables.h.

// The generic format and the common first 0x20 bytes of every format.
class cPixelFormat {
public:
    static constexpr FormatClass kClass = FormatClass::Base;
    static constexpr uint32_t kSize = 0x20;

    // Constructor: records the id and names the format "Unimplemented:<id>". 0x15b840, ret 4.
    cPixelFormat *Construct(uint32_t formatId);

    bool QueryInterface(uint32_t riid, void **ppv); // slot 0, 0x15f980: answers 1 and the base id
    uint32_t ReturnOne();                           // slots 1, 2 (AddRef/Release), and 6-8 of the base; 0x15e510 (shared by all formats)
    uint32_t GetFormatId();                         // slot 3, 0x147a30 (shared)
    const char *GetName();                          // slot 4, 0x15e820 (shared): the string's characters
    uint32_t GetClassId();                          // slot 5, 0x15fd30: the base id
    uint32_t GetRowBytes(uint32_t width);           // slot 9, 0x147140 (shared): the width itself, ret 4
    // slot 10, 0x15eba0 (shared by every cPixelFormatBaseT<> vtable): strict ordering by class id, then
    // (unless `strict`) by format id. `strict` makes equal class ids compare as not-less.
    bool Less(const cPixelFormat *other, bool strict);
    void *DeletingDestructor(uint32_t flags);       // slot 11, 0x15dcb0: base vtable, frees the name; flags & 1 frees the object (0x20)

    // The pieces the derived classes share.
    //  * QueryInterfaceT<Iid>: the base-style QueryInterface of a format with interface id Iid (also
    //    the cPixelFormatBaseT<> versions).
    //  * QueryInterfaceRgbT<Iid>: the same, but the colour classes also answer kIidRgbColorSpace, first
    //    and without a null check on ppv (a contract violation, so only a comment).
    //  * ClassIdT<Iid>: slot 5.
    //  * DeletingDestructorT<Class, Size>: writes the class's BaseT vtable, empties the name, frees Size bytes.
    template <uint32_t Iid> bool QueryInterfaceT(uint32_t riid, void **ppv);
    template <uint32_t Iid> bool QueryInterfaceRgbT(uint32_t riid, void **ppv);
    template <uint32_t Iid> uint32_t ClassIdT() { return Iid; }
    template <FormatClass Class, uint32_t Size> void *DeletingDestructorT(uint32_t flags);

    // Helpers for the derived constructors (not original functions).
    void InitHeader(uint32_t formatId);     // id and an empty name
    void AppendChannel(const char *letter, uint32_t bits); // name += <letter><bits>

    const void *mVTable;  // +0x00
    uint32_t mFormatId;   // +0x04
    std::string mName;    // +0x08 (0x18 bytes: buffer, size, capacity)
};

static_assert(offsetof(cPixelFormat, mVTable) == 0, "cPixelFormat: vtable at +0");
static_assert(offsetof(cPixelFormat, mFormatId) == 4, "cPixelFormat: format id at +4");
static_assert(offsetof(cPixelFormat, mName) == 8, "cPixelFormat: name at +8");
static_assert(sizeof(cPixelFormat) == cPixelFormat::kSize, "cPixelFormat: 0x20 bytes");

// 0x30 bytes. Channels in memory order A, B, G, R as floats ("A32B32G32R32F"). Slots 11-14 are the
// colour space channel bit counts: 11 alpha, 12 red, 13 green, 14 blue. Vtable 0xd9d344.
class cPixelFormatABGRFloat : public cPixelFormat {
public:
    static constexpr FormatClass kClass = FormatClass::ABGRFloat;
    static constexpr uint32_t kSize = 0x30;

    // Constructor: 0x15ba30, ret 0x14. The name is the non-zero channels "A<n>B<n>G<n>R<n>" then "F".
    cPixelFormatABGRFloat *Construct(uint32_t formatId, uint32_t alphaBits, uint32_t blueBits, uint32_t greenBits, uint32_t redBits);

    bool QueryInterface(uint32_t riid, void **ppv); // slot 0, 0x15fb00
    uint32_t GetClassId();                          // slot 5, 0x15fd40
    uint32_t GetBitsPerPixel();                     // slot 6, 0x15e540: the sum of the four channels
    uint32_t GetRowBytes(uint32_t width);           // slot 9, 0x15e620: (slot 6 * width + 7) / 8
    bool Less(const cPixelFormat *other, bool strict); // slot 10, 0x15ec10
    uint32_t GetAlphaBits() { return mAlphaBits; }  // slot 11, 0x15e550 (shared)
    uint32_t GetRedBits() { return mRedBits; }      // slot 12, 0x147770 (shared)
    uint32_t GetGreenBits() { return mGreenBits; }  // slot 13, 0x15f390 (shared)
    uint32_t GetBlueBits() { return mBlueBits; }    // slot 14, 0x15e970 (shared)
    uint32_t GetChannelBitsOr();                    // slot 15, 0x15f900: the four counts OR-ed
    void *DeletingDestructor(uint32_t flags);       // slot 16, 0x15e060 (0x30)

    uint32_t mAlphaBits; // +0x20
    uint32_t mBlueBits;  // +0x24
    uint32_t mGreenBits; // +0x28
    uint32_t mRedBits;   // +0x2c
};
static_assert(offsetof(cPixelFormatABGRFloat, mAlphaBits) == 0x20, "ABGRFloat: alpha at +0x20");
static_assert(offsetof(cPixelFormatABGRFloat, mRedBits) == 0x2c, "ABGRFloat: red at +0x2c");
static_assert(sizeof(cPixelFormatABGRFloat) == cPixelFormatABGRFloat::kSize, "ABGRFloat: 0x30 bytes");

// 0x34 bytes. Integer channels packed A (top), R, G, B (bottom). Vtable 0xd9d21c.
class cPixelFormatARGB : public cPixelFormat {
public:
    static constexpr FormatClass kClass = FormatClass::ARGB;
    static constexpr uint32_t kSize = 0x34;

    // Constructor: 0x15bf40, ret 0x18. The name is "BPP<total>" then "A<n>R<n>G<n>B<n>" for the non-zero channels.
    cPixelFormatARGB *Construct(uint32_t formatId, uint32_t totalBits, uint32_t alphaBits, uint32_t redBits, uint32_t greenBits, uint32_t blueBits);

    bool QueryInterface(uint32_t riid, void **ppv); // slot 0, 0x15fb50
    uint32_t GetClassId();                          // slot 5, 0x15fd50
    uint32_t GetBitsPerPixel() { return mTotalBits; } // slot 6, 0x15e550 (shared)
    uint32_t GetRowBytes(uint32_t width);           // slot 9, 0x15e640 (shared): (bits * width + 7) / 8
    bool Less(const cPixelFormat *other, bool strict); // slot 10, 0x15ed40
    uint32_t GetAlphaBits() { return mAlphaBits; }  // slot 11, 0x15e970 (shared)
    uint32_t GetRedBits() { return mRedBits; }      // slot 12, 0x15f390 (shared)
    uint32_t GetGreenBits() { return mGreenBits; }  // slot 13, 0x147770 (shared)
    uint32_t GetBlueBits() { return mBlueBits; }    // slot 14, 0x1457d0 (shared)
    uint32_t GetUnusedBits();                       // slot 15, 0x15fdb0: total minus the four channels
    // slot 16, 0x15e6a0: packs B, G, R, A bytes (bgra[0..3]) scaled to the channel widths: round(v * (2^n - 1) / 255).
    uint32_t PackColor(const uint8_t *bgra);
    // slot 17, 0x15e790, ret 8: splits a pixel into B, G, R, A at bgra[0..3] (channel bits, not rescaled); returns bgra.
    uint8_t *UnpackColor(uint8_t *bgra, uint32_t pixel);
    void *DeletingDestructor(uint32_t flags);       // slot 18, 0x15e0d0 (0x34)

    uint32_t mTotalBits; // +0x20
    uint32_t mAlphaBits; // +0x24
    uint32_t mRedBits;   // +0x28
    uint32_t mGreenBits; // +0x2c
    uint32_t mBlueBits;  // +0x30
};
static_assert(offsetof(cPixelFormatARGB, mTotalBits) == 0x20, "ARGB: total at +0x20");
static_assert(offsetof(cPixelFormatARGB, mBlueBits) == 0x30, "ARGB: blue at +0x30");
static_assert(sizeof(cPixelFormatARGB) == cPixelFormatARGB::kSize, "ARGB: 0x34 bytes");

// 0x2c bytes. Alpha plus one luminance channel (slots 12-15 all report the luminance bits). Vtable 0xd9d2b4.
class cPixelFormatAlphaLuminance : public cPixelFormat {
public:
    static constexpr FormatClass kClass = FormatClass::AlphaLuminance;
    static constexpr uint32_t kSize = 0x2c;

    // Constructor: 0x15c5b0, ret 0x10. The name is "BPP<total>", "A<n>" if alpha is non-zero, then "L<n>".
    cPixelFormatAlphaLuminance *Construct(uint32_t formatId, uint32_t totalBits, uint32_t alphaBits, uint32_t luminanceBits);

    bool QueryInterface(uint32_t riid, void **ppv); // slot 0, 0x15fba0
    uint32_t GetClassId();                          // slot 5, 0x15fd60
    uint32_t GetBitsPerPixel() { return mTotalBits; } // slot 6, 0x15e550 (shared)
    uint32_t GetRowBytes(uint32_t width);           // slot 9, 0x15e640 (shared)
    bool Less(const cPixelFormat *other, bool strict); // slot 10, 0x15eee0
    uint32_t GetAlphaBits() { return mAlphaBits; }  // slot 11, 0x15e970 (shared)
    uint32_t GetLuminanceBits() { return mLuminanceBits; } // slots 12-15, 0x15f390 (shared)
    void *DeletingDestructor(uint32_t flags);       // slot 16, 0x15e140 (0x2c)

    uint32_t mTotalBits;     // +0x20
    uint32_t mAlphaBits;     // +0x24
    uint32_t mLuminanceBits; // +0x28
};
static_assert(offsetof(cPixelFormatAlphaLuminance, mTotalBits) == 0x20, "AlphaLuminance: total at +0x20");
static_assert(sizeof(cPixelFormatAlphaLuminance) == cPixelFormatAlphaLuminance::kSize, "AlphaLuminance: 0x2c bytes");

// 0x2c bytes. Block-compressed formats: "DXT<version>". Vtable 0xd9d3d8.
class cPixelFormatDXT : public cPixelFormat {
public:
    static constexpr FormatClass kClass = FormatClass::DXT;
    static constexpr uint32_t kSize = 0x2c;

    // Constructor: 0x15c9c0, ret 8. Versions 1-5 set the bits per pixel (4 for DXT1, else 8) and alpha
    // bits (1, 4 for DXT2/3, 8 for DXT4/5); anything else leaves 1 and 0. The name is "DXT<version>".
    cPixelFormatDXT *Construct(uint32_t formatId, uint32_t version);

    bool QueryInterface(uint32_t riid, void **ppv); // slot 0, 0x15fbf0
    uint32_t GetClassId();                          // slot 5, 0x15fd70
    uint32_t GetBitsPerPixel() { return mBitsPerPixel; } // slot 6, 0x15e550 (shared)
    uint32_t GetBlockSize() { return 4; }           // slots 7, 8, 0x155a00 (shared): block height/width in pixels
    uint32_t GetRowBytes(uint32_t width);           // slot 9, 0x15e660: whole 4-pixel blocks * bits * 2, ret 4
    bool Less(const cPixelFormat *other, bool strict); // slot 10, 0x15f000
    uint32_t GetAlphaBits() { return mAlphaBits; }  // slot 11, 0x15e970 (shared)
    uint32_t GetColorBits() { return 8; }           // slots 12-14, 0x1559c0 (shared)
    uint32_t GetVersion() { return mVersion; }      // slot 15, 0x15f390 (shared)
    uint32_t GetBlockBytes();                       // slot 16, 0x15e560: bits per pixel * 2
    void *DeletingDestructor(uint32_t flags);       // slot 17, 0x15e1b0 (0x2c)

    uint32_t mBitsPerPixel; // +0x20
    uint32_t mAlphaBits;    // +0x24
    uint32_t mVersion;      // +0x28
};
static_assert(offsetof(cPixelFormatDXT, mBitsPerPixel) == 0x20, "DXT: bits at +0x20");
static_assert(sizeof(cPixelFormatDXT) == cPixelFormatDXT::kSize, "DXT: 0x2c bytes");

// 0x30 bytes. Depth and stencil. Vtable 0xd9d4d4.
class cPixelFormatDepthStencil : public cPixelFormat {
public:
    static constexpr FormatClass kClass = FormatClass::DepthStencil;
    static constexpr uint32_t kSize = 0x30;

    // Constructor: 0x15cc10, ret 0x14 (the flag is the low byte of its argument). The name is "D<depth>",
    // "F" if the depth is floating point, "S<stencil>" if non-zero, "X<unused>" if the total exceeds both.
    cPixelFormatDepthStencil *Construct(uint32_t formatId, uint32_t totalBits, uint32_t depthBits, uint32_t stencilBits, uint8_t floatDepth);

    bool QueryInterface(uint32_t riid, void **ppv); // slot 0, 0x15fa70 (the BaseT version)
    uint32_t GetClassId();                          // slot 5, 0x15fd80
    uint32_t GetBitsPerPixel() { return mTotalBits; } // slot 6, 0x15e550 (shared)
    uint32_t GetRowBytes(uint32_t width);           // slot 9, 0x15e640 (shared)
    bool Less(const cPixelFormat *other, bool strict); // slot 10, 0x15f0b0
    uint32_t GetDepthBits() { return mDepthBits; }  // slot 11, 0x15e970 (shared)
    uint8_t IsFloatDepth() { return mFloatDepth; }  // slot 12, 0x15e850 (shared): the raw byte; only al is defined
    uint32_t GetStencilBits() { return mStencilBits; } // slot 13, 0x15f390 (shared)
    void *DeletingDestructor(uint32_t flags);       // slot 14, 0x15e220 (0x30)

    uint32_t mTotalBits;   // +0x20
    uint32_t mDepthBits;   // +0x24
    uint32_t mStencilBits; // +0x28
    uint8_t mFloatDepth;   // +0x2c (+0x2d..0x2f padding, never written)
};
static_assert(offsetof(cPixelFormatDepthStencil, mTotalBits) == 0x20, "DepthStencil: total at +0x20");
static_assert(offsetof(cPixelFormatDepthStencil, mFloatDepth) == 0x2c, "DepthStencil: float flag at +0x2c");
static_assert(sizeof(cPixelFormatDepthStencil) == cPixelFormatDepthStencil::kSize, "DepthStencil: 0x30 bytes");

// 0x20 bytes, only the common part. The game has one static instance (set up by startup code, no
// constructor of its own). Vtable 0xd9d198.
class cPixelFormatUnknown : public cPixelFormat {
public:
    static constexpr FormatClass kClass = FormatClass::Unknown;
    static constexpr uint32_t kSize = 0x20;

    // What the image does at startup for its one static instance (no constructor: the object is
    // constant-initialised in .data with the vtable and id 0, and the initialiser 0x137cf0 copies the 7
    // characters "UNKNOWN" into the name). Writes our vtable, id 0 and the name.
    cPixelFormatUnknown *InitStatic();

    bool QueryInterface(uint32_t riid, void **ppv); // slot 0, 0x15faa0 (the BaseT version)
    uint32_t GetClassId();                          // slot 5, 0x15fd90
    uint32_t ReturnZero() { return 0; }             // slots 6-8, 0x6c6b0 (shared)
    uint32_t GetRowBytes(uint32_t width);           // slot 9, 0x12e40 (shared): 0, ret 4
    bool Less(const cPixelFormat *other, bool strict); // slot 10, 0x15f210
    void *DeletingDestructor(uint32_t flags);       // slot 11, 0x15df50 (0x20)
};
static_assert(sizeof(cPixelFormatUnknown) == cPixelFormatUnknown::kSize, "Unknown: 0x20 bytes");

// 0x28 bytes. Packed YUV formats named by their FOURCC. Vtable 0xd9d45c.
class cPixelFormatYUV : public cPixelFormat {
public:
    static constexpr FormatClass kClass = FormatClass::YUV;
    static constexpr uint32_t kSize = 0x28;

    // Constructor: 0x15d230, ret 0x14. The four arguments are the FOURCC characters (low byte used),
    // first character in the lowest byte; the name is the four characters. 16 bits per pixel for YUY2
    // and UYVY, otherwise 1.
    cPixelFormatYUV *Construct(uint32_t formatId, uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3);

    bool QueryInterface(uint32_t riid, void **ppv); // slot 0, 0x15fad0 (the BaseT version)
    uint32_t GetClassId();                          // slot 5, 0x15fda0
    uint32_t GetBitsPerPixel() { return mBitsPerPixel; } // slot 6, 0x15e550 (shared)
    uint32_t GetBlockWidth() { return 2; }          // slot 8, 0x15e570 (shared)
    uint32_t GetRowBytes(uint32_t width);           // slot 9, 0x15e680: width rounded up to even * bits / 8, ret 4
    bool Less(const cPixelFormat *other, bool strict); // slot 10, 0x15f2d0
    uint32_t GetFourCC() { return mFourCC; }        // slot 11, 0x15e970 (shared)
    void *DeletingDestructor(uint32_t flags);       // slot 12, 0x15e360 (0x28)

    uint32_t mBitsPerPixel; // +0x20
    uint32_t mFourCC;       // +0x24
};
static_assert(offsetof(cPixelFormatYUV, mFourCC) == 0x24, "YUV: FOURCC at +0x24");
static_assert(sizeof(cPixelFormatYUV) == cPixelFormatYUV::kSize, "YUV: 0x28 bytes");

template <uint32_t Iid> bool cPixelFormat::QueryInterfaceT(uint32_t riid, void **ppv)
{
    if (!ppv) return false;
    if (riid != Iid && riid != kIidUnknown && riid != kIidPixelFormat) return false;
    *ppv = this;
    VirtualCall<uint32_t(__thiscall *)(void *), 1>(this); // AddRef
    return true;
}

template <uint32_t Iid> bool cPixelFormat::QueryInterfaceRgbT(uint32_t riid, void **ppv)
{
    if (riid == kIidRgbColorSpace) { // no null check on ppv here, unlike the other ids
        *ppv = this;
        VirtualCall<uint32_t(__thiscall *)(void *), 1>(this); // AddRef
        return true;
    }
    return QueryInterfaceT<Iid>(riid, ppv);
}

template <FormatClass Class, uint32_t Size> void *cPixelFormat::DeletingDestructorT(uint32_t flags)
{
    mVTable = BaseTVTable(Class);
    mName.~basic_string(); // frees a heap buffer and leaves the empty small string (size 0, capacity 15)
    if (flags & 1) ::operator delete(this, Size);
    return this;
}

} // namespace nGZGraphic4
