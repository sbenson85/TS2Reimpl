// Differential tests for the nGZGraphic4 pixel format classes (src/gz/PixelFormat.*).
//
// Vtables: the original's objects carry the image's vtables and ours carry the ones our build emits;
// the only normalisation of that is the explicit table in pixel_format_vtables.h, which asserts the exact
// pointer found for the class on each side. Slot-by-slot calls through our vtables are in
// pixel_format_vtable_slots.cpp.
//
// Layouts: every object is built in raw memory filled with the same random bytes on both sides, then
// constructed (original constructor vs ours) or set up field by field. Objects that make virtual calls
// get a vtable of recording fakes whose answers come from a per-object script, so the sequence of calls
// (slot, which object, interface id) is compared as well as the result. Names are compared by size,
// capacity and characters (the small-string buffer beyond the terminator and the heap pointer differ
// between sides by nature).
#include "difftest.h"
#include "free_recorder.h"
#include "game/vtable.h"
#include "gz/PixelFormat.h"
#include "pixel_format_vtables.h"

#include <windows.h>

#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace nGZGraphic4;

namespace {

constexpr uint32_t kMaxSize = 0x34;

using U32Fn0 = uint32_t(__thiscall *)(void *);
using U32Fn1 = uint32_t(__thiscall *)(void *, uint32_t);
using QiFn = uint32_t(__thiscall *)(void *, uint32_t, void **);
using LessFn = uint32_t(__thiscall *)(void *, const void *, uint32_t);
using DtorFn = void *(__thiscall *)(void *, uint32_t);

// ---------------------------------------------------------------------------------------------------
// Random helpers

uint32_t Pick(std::mt19937 &rng, std::initializer_list<uint32_t> values)
{
    return *(values.begin() + rng() % values.size());
}

// Channel widths and similar small counts, with the occasional wild 32-bit value.
uint32_t Bits(std::mt19937 &rng)
{
    switch (rng() % 8) {
    case 0: return 0;
    case 1: return 1 + rng() % 8;
    case 2: return Pick(rng, {1, 2, 4, 5, 6, 8, 10, 11, 16, 24, 32, 64});
    case 3: return rng() % 40;
    case 4: return Pick(rng, {0x7fffffffu, 0x80000000u, 0xffffffffu, 31, 33, 255, 256});
    default: return rng() % 33;
    }
}

uint32_t AnyU32(std::mt19937 &rng)
{
    return rng() % 3 == 0 ? static_cast<uint32_t>(rng()) : Bits(rng);
}

// A raw block of `size` bytes holding the same random content for `copies` sides.
struct Raw {
    uint8_t *p;
    explicit Raw(const std::vector<uint8_t> &image)
    {
        p = static_cast<uint8_t *>(::operator new(image.size()));
        std::memcpy(p, image.data(), image.size());
    }
};

std::vector<uint8_t> RandomImage(std::mt19937 &rng, size_t size)
{
    std::vector<uint8_t> v(size);
    for (auto &b : v) b = static_cast<uint8_t>(rng());
    return v;
}

// What a name looks like, independent of where the buffer lives.
struct NameState {
    std::string text;
    size_t size, capacity;
    bool operator==(const NameState &o) const { return text == o.text && size == o.size && capacity == o.capacity; }
};

NameState StateOf(const std::string &s)
{
    return {std::string(s.c_str(), s.size()), s.size(), s.capacity()};
}

// Compares two constructed objects of `size` bytes: the vtable pointers must be the mapped pair of the
// class (the only normalisation); everything else except the name's storage is compared byte for byte
// (the garbage fill must match in padding and untouched fields), the name by NameState.
bool SameObject(const difftest::Context &ctx, FormatClass cls, bool baseT, const uint8_t *a, const uint8_t *b, size_t size)
{
    if (!pftest::VTablesCorrespond(ctx, cls, baseT, pftest::VTableOf(a), pftest::VTableOf(b))) return false;
    if (std::memcmp(a + 4, b + 4, 4) != 0) return false;
    if (size > 0x20 && std::memcmp(a + 0x20, b + 0x20, size - 0x20) != 0) return false;
    auto &na = *reinterpret_cast<const std::string *>(a + 8);
    auto &nb = *reinterpret_cast<const std::string *>(b + 8);
    return StateOf(na) == StateOf(nb);
}

// ---------------------------------------------------------------------------------------------------
// Constructors

struct CtorSpec {
    const char *name;
    uint32_t rva;
    uint32_t size;
    FormatClass cls;
    int argCount; // arguments after the format id
};

template <typename OrigFn, typename Obj, typename... Args>
void CtorCase(difftest::Context &ctx, const CtorSpec &spec, OrigFn orig, std::mt19937 &rng, uint32_t id, Args... args)
{
    std::vector<uint8_t> image = RandomImage(rng, spec.size);
    Raw a(image), b(image);
    void *ra = orig(a.p, id, args...);
    Obj *rb = reinterpret_cast<Obj *>(b.p)->Construct(id, args...);
    ++ctx.cases;
    if (ra != a.p || static_cast<void *>(rb) != b.p) ctx.Fail("%s: returned object differs", spec.name);
    if (!SameObject(ctx, spec.cls, false, a.p, b.p, spec.size)) {
        auto &na = *reinterpret_cast<const std::string *>(a.p + 8);
        auto &nb = *reinterpret_cast<const std::string *>(b.p + 8);
        ctx.Fail("%s(id %u, args %u..): object differs; original name \"%s\" (%zu/%zu) ours \"%s\" (%zu/%zu)", spec.name, id,
                 static_cast<uint32_t>(sizeof...(Args)), na.c_str(), na.size(), na.capacity(), nb.c_str(), nb.size(), nb.capacity());
    }
    reinterpret_cast<std::string *>(a.p + 8)->~basic_string();
    reinterpret_cast<std::string *>(b.p + 8)->~basic_string();
    ::operator delete(a.p);
    ::operator delete(b.p);
}

uint32_t FormatId(std::mt19937 &rng)
{
    return rng() % 4 == 0 ? static_cast<uint32_t>(rng()) : Pick(rng, {0, 21, 22, 23, 26, 50, 51, 0x31545844, 0x32595559, 0x59565955});
}

DIFF_TEST(PixelFormat_Construct_Base, 0x15b840)
{
    using Fn = void *(__thiscall *)(void *, uint32_t);
    CtorSpec spec{"cPixelFormat::Construct", 0x15b840, 0x20, FormatClass::Base, 0};
    std::mt19937 rng(0x15b840);
    for (int i = 0; i < 3000; ++i) CtorCase<Fn, cPixelFormat>(ctx, spec, ctx.Original<Fn>(0x15b840), rng, FormatId(rng));
}

DIFF_TEST(PixelFormat_Construct_ABGRFloat, 0x15ba30)
{
    using Fn = void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    CtorSpec spec{"cPixelFormatABGRFloat::Construct", 0x15ba30, 0x30, FormatClass::ABGRFloat, 4};
    std::mt19937 rng(0x15ba30);
    for (int i = 0; i < 6000; ++i) {
        uint32_t a = Bits(rng), b = Bits(rng), g = Bits(rng), r = Bits(rng);
        if (i % 5 == 0) a = b = g = r = 32;
        if (i % 7 == 0) a = 0;
        CtorCase<Fn, cPixelFormatABGRFloat>(ctx, spec, ctx.Original<Fn>(0x15ba30), rng, FormatId(rng), a, b, g, r);
    }
}

DIFF_TEST(PixelFormat_Construct_ARGB, 0x15bf40)
{
    using Fn = void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    CtorSpec spec{"cPixelFormatARGB::Construct", 0x15bf40, 0x34, FormatClass::ARGB, 5};
    std::mt19937 rng(0x15bf40);
    for (int i = 0; i < 6000; ++i) {
        uint32_t total = Bits(rng), a = Bits(rng), r = Bits(rng), g = Bits(rng), b = Bits(rng);
        if (i % 4 == 0) {
            a = 8; r = 8; g = 8; b = 8; total = 32;
        }
        if (i % 6 == 0) a = 0;
        if (i % 9 == 0) b = 0;
        CtorCase<Fn, cPixelFormatARGB>(ctx, spec, ctx.Original<Fn>(0x15bf40), rng, FormatId(rng), total, a, r, g, b);
    }
}

DIFF_TEST(PixelFormat_Construct_AlphaLuminance, 0x15c5b0)
{
    using Fn = void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t);
    CtorSpec spec{"cPixelFormatAlphaLuminance::Construct", 0x15c5b0, 0x2c, FormatClass::AlphaLuminance, 3};
    std::mt19937 rng(0x15c5b0);
    for (int i = 0; i < 6000; ++i) {
        uint32_t total = Bits(rng), a = Bits(rng), l = Bits(rng);
        if (i % 5 == 0) a = 0;
        CtorCase<Fn, cPixelFormatAlphaLuminance>(ctx, spec, ctx.Original<Fn>(0x15c5b0), rng, FormatId(rng), total, a, l);
    }
}

DIFF_TEST(PixelFormat_Construct_DXT, 0x15c9c0)
{
    using Fn = void *(__thiscall *)(void *, uint32_t, uint32_t);
    CtorSpec spec{"cPixelFormatDXT::Construct", 0x15c9c0, 0x2c, FormatClass::DXT, 1};
    std::mt19937 rng(0x15c9c0);
    for (int i = 0; i < 4000; ++i) {
        uint32_t version = i < 40 ? static_cast<uint32_t>(i) : (rng() % 3 == 0 ? static_cast<uint32_t>(rng()) : 1 + rng() % 5);
        CtorCase<Fn, cPixelFormatDXT>(ctx, spec, ctx.Original<Fn>(0x15c9c0), rng, FormatId(rng), version);
    }
}

DIFF_TEST(PixelFormat_Construct_DepthStencil, 0x15cc10)
{
    using Fn = void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    CtorSpec spec{"cPixelFormatDepthStencil::Construct", 0x15cc10, 0x30, FormatClass::DepthStencil, 4};
    std::mt19937 rng(0x15cc10);
    for (int i = 0; i < 8000; ++i) {
        uint32_t total = Bits(rng), depth = Bits(rng), stencil = Bits(rng);
        if (i % 4 == 0) {
            depth = 24; stencil = 8; total = 32;
        }
        if (i % 6 == 0) stencil = 0;
        if (i % 8 == 0) total = depth + stencil;
        // The flag is a byte argument; the high bytes of the pushed dword are ignored (mov al, [arg]).
        uint32_t flagWord = static_cast<uint32_t>(rng());
        if (i % 2) flagWord &= 0xffffff00u;
        uint8_t flag = static_cast<uint8_t>(flagWord);
        // The original is called with the whole dword (junk above the low byte); ours takes the byte.
        std::vector<uint8_t> image = RandomImage(rng, spec.size);
        Raw a(image), b(image);
        uint32_t id = FormatId(rng);
        auto orig = ctx.Original<Fn>(0x15cc10);
        void *ra = orig(a.p, id, total, depth, stencil, flagWord);
        void *rb = reinterpret_cast<cPixelFormatDepthStencil *>(b.p)->Construct(id, total, depth, stencil, flag);
        ++ctx.cases;
        if (ra != a.p || rb != b.p || !SameObject(ctx, spec.cls, false, a.p, b.p, spec.size)) {
            auto &na = *reinterpret_cast<const std::string *>(a.p + 8);
            auto &nb = *reinterpret_cast<const std::string *>(b.p + 8);
            ctx.Fail("DepthStencil::Construct(%u,%u,%u,flag %02x): original \"%s\" ours \"%s\"", total, depth, stencil, flag, na.c_str(), nb.c_str());
        }
        reinterpret_cast<std::string *>(a.p + 8)->~basic_string();
        reinterpret_cast<std::string *>(b.p + 8)->~basic_string();
        ::operator delete(a.p);
        ::operator delete(b.p);
    }
}

DIFF_TEST(PixelFormat_Construct_YUV, 0x15d230)
{
    using Fn = void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    CtorSpec spec{"cPixelFormatYUV::Construct", 0x15d230, 0x28, FormatClass::YUV, 4};
    std::mt19937 rng(0x15d230);
    const char *known[] = {"YUY2", "UYVY", "YVYU", "NV12", "YV12"};
    for (int i = 0; i < 4000; ++i) {
        uint32_t c[4];
        if (i < 5) {
            for (int k = 0; k < 4; ++k) c[k] = static_cast<uint8_t>(known[i][k]);
        } else {
            for (int k = 0; k < 4; ++k) c[k] = rng() % 3 == 0 ? static_cast<uint32_t>(rng() & 0xff) : 'A' + rng() % 26;
        }
        // Characters other than printable ones too, including a NUL and bytes >= 0x80 (the first one
        // is sign-extended by the original's movsx, which must not leak into the name).
        if (i % 11 == 0) c[0] = static_cast<uint8_t>(rng());
        if (i % 13 == 0) c[2] = 0;
        // Only the low byte of each argument is used; the pushed dwords may carry junk above it.
        std::vector<uint8_t> image = RandomImage(rng, spec.size);
        Raw a(image), b(image);
        uint32_t id = FormatId(rng);
        uint32_t w[4];
        for (int k = 0; k < 4; ++k) w[k] = c[k] | (i % 3 == 0 ? (static_cast<uint32_t>(rng()) & 0xffffff00u) : 0u);
        auto orig = ctx.Original<Fn>(0x15d230);
        void *ra = orig(a.p, id, w[0], w[1], w[2], w[3]);
        void *rb = reinterpret_cast<cPixelFormatYUV *>(b.p)->Construct(id, w[0], w[1], w[2], w[3]);
        ++ctx.cases;
        if (ra != a.p || rb != b.p || !SameObject(ctx, spec.cls, false, a.p, b.p, spec.size)) {
            auto &na = *reinterpret_cast<const std::string *>(a.p + 8);
            auto &nb = *reinterpret_cast<const std::string *>(b.p + 8);
            ctx.Fail("YUV::Construct(%02x %02x %02x %02x): original \"%s\" (%zu) ours \"%s\" (%zu)", c[0], c[1], c[2], c[3], na.c_str(), na.size(), nb.c_str(), nb.size());
        }
        reinterpret_cast<std::string *>(a.p + 8)->~basic_string();
        reinterpret_cast<std::string *>(b.p + 8)->~basic_string();
        ::operator delete(a.p);
        ::operator delete(b.p);
    }
}

// ---------------------------------------------------------------------------------------------------
// Plain accessors: fields in, value out. The object's name isn't touched, so the fill stays raw.

struct Accessor {
    const char *name;
    uint32_t rva;
    bool takesArg;     // one stack argument (ret 4)
    uint32_t mask;     // bits of the result that are defined
    uint32_t (*ours)(uint8_t *self, uint32_t arg);
};

template <typename T> T *As(uint8_t *p) { return reinterpret_cast<T *>(p); }

const Accessor kAccessors[] = {
    {"ReturnOne", 0x15e510, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormat>(s)->ReturnOne(); }},
    {"GetFormatId", 0x147a30, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormat>(s)->GetFormatId(); }},
    {"GetClassId (base)", 0x15fd30, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormat>(s)->GetClassId(); }},
    {"GetClassId (ABGRFloat)", 0x15fd40, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatABGRFloat>(s)->GetClassId(); }},
    {"GetClassId (ARGB)", 0x15fd50, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatARGB>(s)->GetClassId(); }},
    {"GetClassId (AlphaLuminance)", 0x15fd60, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatAlphaLuminance>(s)->GetClassId(); }},
    {"GetClassId (DXT)", 0x15fd70, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatDXT>(s)->GetClassId(); }},
    {"GetClassId (DepthStencil)", 0x15fd80, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatDepthStencil>(s)->GetClassId(); }},
    {"GetClassId (Unknown)", 0x15fd90, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatUnknown>(s)->GetClassId(); }},
    {"GetClassId (YUV)", 0x15fda0, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatYUV>(s)->GetClassId(); }},
    {"base GetRowBytes", 0x147140, true, ~0u, [](uint8_t *s, uint32_t a) { return As<cPixelFormat>(s)->GetRowBytes(a); }},
    {"Unknown GetRowBytes", 0x12e40, true, ~0u, [](uint8_t *s, uint32_t a) { return As<cPixelFormatUnknown>(s)->GetRowBytes(a); }},
    {"Unknown ReturnZero", 0x6c6b0, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatUnknown>(s)->ReturnZero(); }},
    {"ARGB GetBitsPerPixel", 0x15e550, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatARGB>(s)->GetBitsPerPixel(); }},
    {"AlphaLuminance GetBitsPerPixel", 0x15e550, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatAlphaLuminance>(s)->GetBitsPerPixel(); }},
    {"DXT GetBitsPerPixel", 0x15e550, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatDXT>(s)->GetBitsPerPixel(); }},
    {"DepthStencil GetBitsPerPixel", 0x15e550, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatDepthStencil>(s)->GetBitsPerPixel(); }},
    {"YUV GetBitsPerPixel", 0x15e550, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatYUV>(s)->GetBitsPerPixel(); }},
    {"ABGRFloat GetAlphaBits", 0x15e550, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatABGRFloat>(s)->GetAlphaBits(); }},
    {"ARGB GetAlphaBits", 0x15e970, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatARGB>(s)->GetAlphaBits(); }},
    {"AlphaLuminance GetAlphaBits", 0x15e970, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatAlphaLuminance>(s)->GetAlphaBits(); }},
    {"DXT GetAlphaBits", 0x15e970, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatDXT>(s)->GetAlphaBits(); }},
    {"DepthStencil GetDepthBits", 0x15e970, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatDepthStencil>(s)->GetDepthBits(); }},
    {"YUV GetFourCC", 0x15e970, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatYUV>(s)->GetFourCC(); }},
    {"ABGRFloat GetBlueBits", 0x15e970, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatABGRFloat>(s)->GetBlueBits(); }},
    {"ABGRFloat GetGreenBits", 0x15f390, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatABGRFloat>(s)->GetGreenBits(); }},
    {"ARGB GetRedBits", 0x15f390, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatARGB>(s)->GetRedBits(); }},
    {"AlphaLuminance GetLuminanceBits", 0x15f390, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatAlphaLuminance>(s)->GetLuminanceBits(); }},
    {"DXT GetVersion", 0x15f390, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatDXT>(s)->GetVersion(); }},
    {"DepthStencil GetStencilBits", 0x15f390, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatDepthStencil>(s)->GetStencilBits(); }},
    {"ABGRFloat GetRedBits", 0x147770, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatABGRFloat>(s)->GetRedBits(); }},
    {"ARGB GetGreenBits", 0x147770, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatARGB>(s)->GetGreenBits(); }},
    {"ARGB GetBlueBits", 0x1457d0, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatARGB>(s)->GetBlueBits(); }},
    {"DepthStencil IsFloatDepth", 0x15e850, false, 0xff, [](uint8_t *s, uint32_t) -> uint32_t { return As<cPixelFormatDepthStencil>(s)->IsFloatDepth(); }},
    {"ABGRFloat GetBitsPerPixel", 0x15e540, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatABGRFloat>(s)->GetBitsPerPixel(); }},
    {"ABGRFloat GetChannelBitsOr", 0x15f900, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatABGRFloat>(s)->GetChannelBitsOr(); }},
    {"ARGB GetUnusedBits", 0x15fdb0, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatARGB>(s)->GetUnusedBits(); }},
    {"DXT GetBlockBytes", 0x15e560, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatDXT>(s)->GetBlockBytes(); }},
    {"YUV GetBlockWidth", 0x15e570, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatYUV>(s)->GetBlockWidth(); }},
    {"DXT GetBlockSize", 0x155a00, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatDXT>(s)->GetBlockSize(); }},
    {"DXT GetColorBits", 0x1559c0, false, ~0u, [](uint8_t *s, uint32_t) { return As<cPixelFormatDXT>(s)->GetColorBits(); }},
    {"ARGB GetRowBytes", 0x15e640, true, ~0u, [](uint8_t *s, uint32_t a) { return As<cPixelFormatARGB>(s)->GetRowBytes(a); }},
    {"AlphaLuminance GetRowBytes", 0x15e640, true, ~0u, [](uint8_t *s, uint32_t a) { return As<cPixelFormatAlphaLuminance>(s)->GetRowBytes(a); }},
    {"DepthStencil GetRowBytes", 0x15e640, true, ~0u, [](uint8_t *s, uint32_t a) { return As<cPixelFormatDepthStencil>(s)->GetRowBytes(a); }},
    {"DXT GetRowBytes", 0x15e660, true, ~0u, [](uint8_t *s, uint32_t a) { return As<cPixelFormatDXT>(s)->GetRowBytes(a); }},
    {"YUV GetRowBytes", 0x15e680, true, ~0u, [](uint8_t *s, uint32_t a) { return As<cPixelFormatYUV>(s)->GetRowBytes(a); }},
};

DIFF_TEST(PixelFormat_Accessors, 0x15e550)
{
    std::mt19937 rng(0xacce55);
    for (const Accessor &acc : kAccessors) {
        for (int i = 0; i < 600; ++i) {
            std::vector<uint8_t> image = RandomImage(rng, kMaxSize);
            // Field values likely to reach the interesting arithmetic (small counts, wraps).
            for (uint32_t off = 0x20; off < kMaxSize; off += 4) {
                uint32_t v = AnyU32(rng);
                std::memcpy(&image[off], &v, 4);
            }
            uint32_t arg = rng() % 4 == 0 ? static_cast<uint32_t>(rng()) : rng() % 70;
            uint32_t idv = static_cast<uint32_t>(rng());
            std::memcpy(&image[4], &idv, 4);
            Raw a(image), b(image);
            uint32_t ra = acc.takesArg ? ctx.Original<U32Fn1>(acc.rva)(a.p, arg) : ctx.Original<U32Fn0>(acc.rva)(a.p);
            uint32_t rb = acc.ours(b.p, arg);
            ++ctx.cases;
            if ((ra & acc.mask) != (rb & acc.mask)) ctx.Fail("%s (0x%x): original %08x ours %08x", acc.name, acc.rva, ra, rb);
            if (std::memcmp(a.p, b.p, kMaxSize) != 0) ctx.Fail("%s: object modified differently", acc.name);
            ::operator delete(a.p);
            ::operator delete(b.p);
        }
    }
}

// slot 4: the name's characters, wherever the buffer is.
DIFF_TEST(PixelFormat_GetName, 0x15e820)
{
    std::mt19937 rng(0x15e820);
    for (int i = 0; i < 2000; ++i) {
        size_t len = Pick(rng, {0, 1, 5, 14, 15, 16, 17, 31, 40, 200});
        std::string text;
        for (size_t k = 0; k < len; ++k) text.push_back(static_cast<char>('A' + rng() % 26));
        std::vector<uint8_t> image = RandomImage(rng, 0x20);
        Raw a(image), b(image);
        new (a.p + 8) std::string(text);
        new (b.p + 8) std::string(text);
        const char *ra = ctx.Original<const char *(__thiscall *)(void *)>(0x15e820)(a.p);
        const char *rb = As<cPixelFormat>(b.p)->GetName();
        ++ctx.cases;
        auto &sa = *reinterpret_cast<std::string *>(a.p + 8);
        auto &sb = *reinterpret_cast<std::string *>(b.p + 8);
        if (ra != sa.c_str() || rb != sb.c_str()) ctx.Fail("GetName: pointer is not the string's characters (len %zu)", len);
        sa.~basic_string();
        sb.~basic_string();
        ::operator delete(a.p);
        ::operator delete(b.p);
    }
}

// ---------------------------------------------------------------------------------------------------
// Fake vtables: slots answer from a script and log every call.

constexpr int kSlots = 24;
constexpr int kObjects = 3; // 0: the object under test, 1: the other argument, 2: what QueryInterface hands back

struct Script {
    const void *object[kObjects] = {};
    uint32_t value[kObjects][kSlots] = {};
    uint32_t qiIid = 0;   // the interface id QueryInterface on the other object answers
    int qiTarget = -1;    // the object it hands out (index into object[]), or -1 for none
};
Script gScript;
std::vector<uint32_t> gLog;

int IndexOf(const void *self)
{
    for (int i = 0; i < kObjects; ++i) {
        if (gScript.object[i] == self) return i;
    }
    return 7;
}

template <int N> uint32_t __fastcall FakeGet(void *self, void *)
{
    int idx = IndexOf(self);
    gLog.push_back(static_cast<uint32_t>(N * 8 + idx));
    return idx < kObjects ? gScript.value[idx][N] : 0;
}

uint32_t __fastcall FakeQueryInterface(void *self, void *, uint32_t iid, void **ppv)
{
    int idx = IndexOf(self);
    gLog.push_back(static_cast<uint32_t>(idx));
    gLog.push_back(iid);
    if (iid == gScript.qiIid && gScript.qiTarget >= 0) {
        *ppv = const_cast<void *>(gScript.object[gScript.qiTarget]);
        return 1;
    }
    return 0;
}

#define SLOT(n) reinterpret_cast<void *>(&FakeGet<n>)
void *gFakeVTable[kSlots] = {
    reinterpret_cast<void *>(&FakeQueryInterface), SLOT(1), SLOT(2), SLOT(3), SLOT(4), SLOT(5), SLOT(6), SLOT(7), SLOT(8), SLOT(9),
    SLOT(10), SLOT(11), SLOT(12), SLOT(13), SLOT(14), SLOT(15), SLOT(16), SLOT(17), SLOT(18), SLOT(19), SLOT(20), SLOT(21), SLOT(22), SLOT(23)};
#undef SLOT

// ---------------------------------------------------------------------------------------------------
// QueryInterface (slot 0) and its AddRef call.

struct QiSpec {
    const char *name;
    uint32_t rva;
    uint32_t size;
    uint32_t iid;       // the class's own interface id
    bool rgb;           // also answers the colour space id (unchecked ppv)
    bool (*ours)(uint8_t *self, uint32_t riid, void **ppv);
};

#define QI_LAMBDA(Type) [](uint8_t *s, uint32_t r, void **p) { return As<Type>(s)->QueryInterface(r, p); }
#define QI_TEMPLATE(iid) [](uint8_t *s, uint32_t r, void **p) { return As<cPixelFormat>(s)->QueryInterfaceT<iid>(r, p); }

const QiSpec kQis[] = {
    {"cPixelFormat", 0x15f980, 0x20, kIidPixelFormat, false, QI_LAMBDA(cPixelFormat)},
    {"BaseT<ABGRFloat>", 0x15f9b0, 0x20, kIidABGRFloat, false, QI_TEMPLATE(kIidABGRFloat)},
    {"BaseT<ARGB>", 0x15f9e0, 0x20, kIidARGB, false, QI_TEMPLATE(kIidARGB)},
    {"BaseT<AlphaLuminance>", 0x15fa10, 0x20, kIidAlphaLuminance, false, QI_TEMPLATE(kIidAlphaLuminance)},
    {"BaseT<DXT>", 0x15fa40, 0x20, kIidDXT, false, QI_TEMPLATE(kIidDXT)},
    {"cPixelFormatDepthStencil (BaseT)", 0x15fa70, 0x30, kIidDepthStencil, false, QI_LAMBDA(cPixelFormatDepthStencil)},
    {"cPixelFormatUnknown (BaseT)", 0x15faa0, 0x20, kIidUnknownFormat, false, QI_LAMBDA(cPixelFormatUnknown)},
    {"cPixelFormatYUV (BaseT)", 0x15fad0, 0x28, kIidYUV, false, QI_LAMBDA(cPixelFormatYUV)},
    {"cPixelFormatABGRFloat", 0x15fb00, 0x30, kIidABGRFloat, true, QI_LAMBDA(cPixelFormatABGRFloat)},
    {"cPixelFormatARGB", 0x15fb50, 0x34, kIidARGB, true, QI_LAMBDA(cPixelFormatARGB)},
    {"cPixelFormatAlphaLuminance", 0x15fba0, 0x2c, kIidAlphaLuminance, true, QI_LAMBDA(cPixelFormatAlphaLuminance)},
    {"cPixelFormatDXT", 0x15fbf0, 0x2c, kIidDXT, true, QI_LAMBDA(cPixelFormatDXT)},
};

DIFF_TEST(PixelFormat_QueryInterface, 0x15f980)
{
    std::mt19937 rng(0x9f980);
    const uint32_t known[] = {kIidUnknown, kIidPixelFormat, kIidRgbColorSpace, kIidABGRFloat, kIidARGB, kIidAlphaLuminance,
                              kIidDXT, kIidDepthStencil, kIidUnknownFormat, kIidYUV, 0, 2};
    for (const QiSpec &spec : kQis) {
        for (int i = 0; i < 400; ++i) {
            uint32_t riid;
            switch (i % 4) {
            case 0: riid = known[rng() % (sizeof(known) / sizeof(known[0]))]; break;
            case 1: riid = spec.iid ^ (1u << (rng() % 32)); break;
            case 2: riid = spec.iid; break;
            default: riid = static_cast<uint32_t>(rng()); break;
            }
            // A null ppv: the original dereferences it for the colour space id in the colour classes
            // (an unchecked write, so a contract violation); never offered there.
            bool nullOut = (i % 5 == 0) && !(spec.rgb && riid == kIidRgbColorSpace);
            std::vector<uint8_t> image = RandomImage(rng, spec.size);
            Raw a(image), b(image);
            for (uint8_t *p : {a.p, b.p}) {
                const void *vt = gFakeVTable;
                std::memcpy(p, &vt, 4);
            }
            void *outA = reinterpret_cast<void *>(0x1234abcd), *outB = outA;
            gScript = Script();
            gScript.object[0] = a.p;
            gLog.clear();
            uint8_t ra = static_cast<uint8_t>(ctx.Original<QiFn>(spec.rva)(a.p, riid, nullOut ? nullptr : &outA));
            std::vector<uint32_t> logA = gLog;
            gScript.object[0] = b.p;
            gLog.clear();
            uint8_t rb = spec.ours(b.p, riid, nullOut ? nullptr : &outB) ? 1 : 0;
            std::vector<uint32_t> logB = gLog;
            ++ctx.cases;
            bool writtenA = outA == a.p, writtenB = outB == b.p;
            bool expected = !nullOut ? ((spec.rgb && riid == kIidRgbColorSpace) || riid == spec.iid || riid == kIidUnknown || riid == kIidPixelFormat) : false;
            if (ra != rb) ctx.Fail("%s QI(%08x, null=%d): result original %u ours %u", spec.name, riid, nullOut, ra, rb);
            if (writtenA != writtenB || (expected && !writtenA)) ctx.Fail("%s QI(%08x): out pointer differs", spec.name, riid);
            if (logA != logB) ctx.Fail("%s QI(%08x): AddRef calls differ", spec.name, riid);
            if ((ra == 1) != expected) ctx.Fail("%s QI(%08x, null=%d): result %u does not match the id set", spec.name, riid, nullOut, ra);
            ::operator delete(a.p);
            ::operator delete(b.p);
        }
    }
}

// ---------------------------------------------------------------------------------------------------
// Less (slot 10). The object under test and its arguments are fakes answering from a script.

struct LessSpec {
    const char *name;
    uint32_t rva;
    uint32_t size;
    uint32_t iid;
    // Offsets (relative to the object) of the fields the original reads directly, by the slot that
    // reports the same quantity on the other object. Everything else the original reads through the vtable.
    std::vector<std::pair<int, uint32_t>> fieldSlots;
    bool (*ours)(uint8_t *self, const void *other, bool strict);
};

#define LESS_LAMBDA(Type) [](uint8_t *s, const void *o, bool st) { return As<Type>(s)->Less(static_cast<const cPixelFormat *>(o), st); }

DIFF_TEST(PixelFormat_Less, 0x15eba0)
{
    const LessSpec specs[] = {
        {"cPixelFormat", 0x15eba0, 0x20, kIidPixelFormat, {}, LESS_LAMBDA(cPixelFormat)},
        {"cPixelFormatABGRFloat", 0x15ec10, 0x30, kIidABGRFloat, {{11, 0x20}, {14, 0x24}, {13, 0x28}, {12, 0x2c}}, LESS_LAMBDA(cPixelFormatABGRFloat)},
        {"cPixelFormatARGB", 0x15ed40, 0x34, kIidARGB, {{6, 0x20}, {11, 0x24}, {12, 0x28}, {13, 0x2c}, {14, 0x30}}, LESS_LAMBDA(cPixelFormatARGB)},
        {"cPixelFormatAlphaLuminance", 0x15eee0, 0x2c, kIidAlphaLuminance, {{6, 0x20}, {11, 0x24}, {12, 0x28}}, LESS_LAMBDA(cPixelFormatAlphaLuminance)},
        {"cPixelFormatDXT", 0x15f000, 0x2c, kIidDXT, {{15, 0x28}}, LESS_LAMBDA(cPixelFormatDXT)},
        {"cPixelFormatDepthStencil", 0x15f0b0, 0x30, kIidDepthStencil, {{11, 0x24}, {13, 0x28}}, LESS_LAMBDA(cPixelFormatDepthStencil)},
        {"cPixelFormatUnknown", 0x15f210, 0x20, kIidUnknownFormat, {}, LESS_LAMBDA(cPixelFormatUnknown)},
        {"cPixelFormatYUV", 0x15f2d0, 0x28, kIidYUV, {{11, 0x24}}, LESS_LAMBDA(cPixelFormatYUV)},
    };
    std::mt19937 rng(0x15eba0);
    for (const LessSpec &spec : specs) {
        for (int i = 0; i < 12000; ++i) {
            std::vector<uint8_t> image = RandomImage(rng, spec.size);
            // Fields: mostly the small values the comparisons turn on.
            for (uint32_t off = 0x20; off + 4 <= spec.size; off += 4) {
                uint32_t v = Pick(rng, {0, 1, 2, 8, 16, 24, 32, 0x7fffffffu, 0x80000000u, 0xffffffffu});
                std::memcpy(&image[off], &v, 4);
            }
            Raw a(image), b(image);
            for (uint8_t *p : {a.p, b.p}) {
                const void *vt = gFakeVTable;
                std::memcpy(p, &vt, 4);
            }
            // The other object and the QueryInterface result: just a vtable pointer each.
            alignas(8) uint8_t other[16], found[16];
            const void *vt = gFakeVTable;
            std::memcpy(other, &vt, 4);
            std::memcpy(found, &vt, 4);

            Script s;
            s.object[1] = other;
            s.object[2] = found;
            for (int obj = 0; obj < kObjects; ++obj) {
                for (int slot = 0; slot < kSlots; ++slot) s.value[obj][slot] = Pick(rng, {0, 1, 2, 3, 0x7fffffffu, 0x80000000u, 0xffffffffu, 0xfffffff0u});
            }
            // Class ids and format ids: often equal so the later comparisons are reached.
            for (int obj = 0; obj < kObjects; ++obj) {
                if (rng() % 4) s.value[obj][5] = s.value[0][5];
                if (rng() % 3) s.value[obj][3] = s.value[0][3];
                s.value[obj][4] = static_cast<uint32_t>(rng());
            }
            // The other object reports quantities equal to our fields more often than chance.
            for (auto &fs : spec.fieldSlots) {
                uint32_t mine;
                std::memcpy(&mine, &image[fs.second], 4);
                if (rng() % 3) s.value[2][fs.first] = mine;
                if (rng() % 3) s.value[0][fs.first] = mine;
            }
            // The slots the original asks of `this` as well (the unused-bits slot of ARGB, slot 6 of DepthStencil).
            if (rng() % 2) s.value[0][15] = s.value[2][15];
            if (rng() % 2) s.value[0][6] = s.value[2][6];
            s.value[2][12] = (rng() % 2) ? 0 : static_cast<uint32_t>(rng() | 0x100); // DepthStencil's flag: only al counts
            s.value[0][12] = 0;
            s.qiIid = spec.iid;
            s.qiTarget = (rng() % 4 == 0) ? -1 : (rng() % 2 ? 2 : 1);
            bool strict = rng() % 2;

            gScript = s;
            gScript.object[0] = a.p;
            gLog.clear();
            uint8_t ra = static_cast<uint8_t>(ctx.Original<LessFn>(spec.rva)(a.p, other, strict ? (rng() | 1) & 0xff : 0));
            std::vector<uint32_t> logA = gLog;
            gScript = s;
            gScript.object[0] = b.p;
            gLog.clear();
            uint8_t rb = spec.ours(b.p, other, strict) ? 1 : 0;
            std::vector<uint32_t> logB = gLog;
            ++ctx.cases;
            if (ra != rb) ctx.Fail("%s::Less: strict %d, original %u ours %u", spec.name, strict, ra, rb);
            if (logA != logB) ctx.Fail("%s::Less: calls differ (%zu vs %zu)", spec.name, logA.size(), logB.size());
            if (std::memcmp(a.p + 4, b.p + 4, spec.size - 4) != 0) ctx.Fail("%s::Less: object modified", spec.name);
            ::operator delete(a.p);
            ::operator delete(b.p);
        }
    }
}

// The base ordering also runs through the other classes' vtables (derived Less delegates when the
// other object is not the same class); the derived cases above cover that via qiTarget == -1.

// ABGRFloat::GetRowBytes asks slot 6 of itself.
DIFF_TEST(PixelFormat_ABGRFloat_GetRowBytes, 0x15e620)
{
    std::mt19937 rng(0x15e620);
    for (int i = 0; i < 4000; ++i) {
        std::vector<uint8_t> image = RandomImage(rng, 0x30);
        Raw a(image), b(image);
        for (uint8_t *p : {a.p, b.p}) {
            const void *vt = gFakeVTable;
            std::memcpy(p, &vt, 4);
        }
        gScript = Script();
        gScript.value[0][6] = AnyU32(rng);
        uint32_t width = rng() % 3 ? rng() % 5000 : static_cast<uint32_t>(rng());
        gScript.object[0] = a.p;
        gLog.clear();
        uint32_t ra = ctx.Original<U32Fn1>(0x15e620)(a.p, width);
        std::vector<uint32_t> logA = gLog;
        gScript.object[0] = b.p;
        gLog.clear();
        uint32_t rb = As<cPixelFormatABGRFloat>(b.p)->GetRowBytes(width);
        ++ctx.cases;
        if (ra != rb || logA != gLog) ctx.Fail("ABGRFloat::GetRowBytes(%u): original %u ours %u", width, ra, rb);
        ::operator delete(a.p);
        ::operator delete(b.p);
    }
}

// ---------------------------------------------------------------------------------------------------
// ARGB pack/unpack

DIFF_TEST(PixelFormat_ARGB_PackColor, 0x15e6a0)
{
    std::mt19937 rng(0x15e6a0);
    for (int i = 0; i < 30000; ++i) {
        std::vector<uint8_t> image = RandomImage(rng, 0x34);
        uint32_t f[4];
        if (i % 3 == 0) {
            f[0] = 8; f[1] = 8; f[2] = 8; f[3] = 8; // A8R8G8B8
            if (i % 9 == 0) { f[0] = 0; }
            if (i % 15 == 0) { f[0] = 1; f[1] = 5; f[2] = 5; f[3] = 5; }
            if (i % 21 == 0) { f[0] = 0; f[1] = 5; f[2] = 6; f[3] = 5; }
        } else {
            for (auto &v : f) v = Bits(rng);
        }
        std::memcpy(&image[0x24], f, 16);
        uint8_t bgra[4];
        for (auto &c : bgra) c = rng() % 3 == 0 ? Pick(rng, {0, 1, 127, 128, 254, 255}) : static_cast<uint8_t>(rng());
        Raw a(image), b(image);
        uint32_t ra = ctx.Original<U32Fn1>(0x15e6a0)(a.p, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(bgra)));
        uint32_t rb = As<cPixelFormatARGB>(b.p)->PackColor(bgra);
        ++ctx.cases;
        if (ra != rb) ctx.Fail("PackColor(bits %u/%u/%u/%u, bgra %02x%02x%02x%02x): original %08x ours %08x", f[0], f[1], f[2], f[3], bgra[0], bgra[1], bgra[2], bgra[3], ra, rb);
        ::operator delete(a.p);
        ::operator delete(b.p);
    }
}

DIFF_TEST(PixelFormat_ARGB_UnpackColor, 0x15e790)
{
    using UnpackFn = uint8_t *(__thiscall *)(void *, uint8_t *, uint32_t);
    std::mt19937 rng(0x15e790);
    for (int i = 0; i < 30000; ++i) {
        std::vector<uint8_t> image = RandomImage(rng, 0x34);
        uint32_t f[4];
        if (i % 3 == 0) {
            f[0] = 8; f[1] = 8; f[2] = 8; f[3] = 8;
            if (i % 9 == 0) f[0] = 0;
            if (i % 15 == 0) { f[0] = 1; f[1] = 5; f[2] = 5; f[3] = 5; }
        } else {
            for (auto &v : f) v = Bits(rng);
        }
        std::memcpy(&image[0x24], f, 16);
        uint32_t pixel = rng() % 4 == 0 ? Pick(rng, {0, 0xffffffffu, 0x80000000u, 1}) : static_cast<uint32_t>(rng());
        uint8_t outA[8], outB[8];
        std::memset(outA, 0xcd, sizeof(outA));
        std::memset(outB, 0xcd, sizeof(outB));
        Raw a(image), b(image);
        uint8_t *ra = ctx.Original<UnpackFn>(0x15e790)(a.p, outA, pixel);
        uint8_t *rb = As<cPixelFormatARGB>(b.p)->UnpackColor(outB, pixel);
        ++ctx.cases;
        if (ra != outA || rb != outB) ctx.Fail("UnpackColor: return value is not the output pointer");
        if (std::memcmp(outA, outB, sizeof(outA)) != 0) {
            ctx.Fail("UnpackColor(bits %u/%u/%u/%u, pixel %08x): original %02x%02x%02x%02x ours %02x%02x%02x%02x", f[0], f[1], f[2], f[3], pixel, outA[0], outA[1],
                     outA[2], outA[3], outB[0], outB[1], outB[2], outB[3]);
        }
        ::operator delete(a.p);
        ::operator delete(b.p);
    }
}

// ---------------------------------------------------------------------------------------------------
// Deleting destructors

struct DtorSpec {
    const char *name;
    uint32_t rva;
    uint32_t size;   // bytes freed when flags & 1
    FormatClass cls; // whose cPixelFormatBaseT<> vtable it writes
    void *(*ours)(uint8_t *self, uint32_t flags);
};

#define DTOR_LAMBDA(Type) [](uint8_t *s, uint32_t f) { return As<Type>(s)->DeletingDestructor(f); }
#define DTOR_BASET(cls) [](uint8_t *s, uint32_t f) { return As<cPixelFormat>(s)->DeletingDestructorT<FormatClass::cls, 0x20>(f); }

DIFF_TEST(PixelFormat_DeletingDestructors, 0x15dcb0)
{
    const DtorSpec specs[] = {
        {"cPixelFormat", 0x15dcb0, 0x20, FormatClass::Base, DTOR_LAMBDA(cPixelFormat)},
        {"BaseT<ABGRFloat>", 0x15dd20, 0x20, FormatClass::ABGRFloat, DTOR_BASET(ABGRFloat)},
        {"BaseT<ARGB>", 0x15dd90, 0x20, FormatClass::ARGB, DTOR_BASET(ARGB)},
        {"BaseT<AlphaLuminance>", 0x15de00, 0x20, FormatClass::AlphaLuminance, DTOR_BASET(AlphaLuminance)},
        {"BaseT<DXT>", 0x15de70, 0x20, FormatClass::DXT, DTOR_BASET(DXT)},
        {"BaseT<DepthStencil>", 0x15dee0, 0x20, FormatClass::DepthStencil, DTOR_BASET(DepthStencil)},
        {"cPixelFormatUnknown", 0x15df50, 0x20, FormatClass::Unknown, DTOR_LAMBDA(cPixelFormatUnknown)},
        {"BaseT<YUV>", 0x15dfc0, 0x20, FormatClass::YUV, DTOR_BASET(YUV)},
        {"cPixelFormatABGRFloat", 0x15e060, 0x30, FormatClass::ABGRFloat, DTOR_LAMBDA(cPixelFormatABGRFloat)},
        {"cPixelFormatARGB", 0x15e0d0, 0x34, FormatClass::ARGB, DTOR_LAMBDA(cPixelFormatARGB)},
        {"cPixelFormatAlphaLuminance", 0x15e140, 0x2c, FormatClass::AlphaLuminance, DTOR_LAMBDA(cPixelFormatAlphaLuminance)},
        {"cPixelFormatDXT", 0x15e1b0, 0x2c, FormatClass::DXT, DTOR_LAMBDA(cPixelFormatDXT)},
        {"cPixelFormatDepthStencil", 0x15e220, 0x30, FormatClass::DepthStencil, DTOR_LAMBDA(cPixelFormatDepthStencil)},
        {"cPixelFormatYUV", 0x15e360, 0x28, FormatClass::YUV, DTOR_LAMBDA(cPixelFormatYUV)},
    };
    std::mt19937 rng(0x15dcb0);
    for (const DtorSpec &spec : specs) {
        for (int i = 0; i < 800; ++i) {
            uint32_t flags = i % 4 == 0 ? static_cast<uint32_t>(rng()) : rng() % 4;
            size_t len = Pick(rng, {0, 1, 7, 15, 16, 17, 33, 100});
            std::string text;
            for (size_t k = 0; k < len; ++k) text.push_back(static_cast<char>('a' + rng() % 26));
            std::vector<uint8_t> image = RandomImage(rng, spec.size);
            Raw a(image), b(image);
            new (a.p + 8) std::string(text);
            new (b.p + 8) std::string(text);
            const char *bufA = reinterpret_cast<std::string *>(a.p + 8)->capacity() > 15 ? reinterpret_cast<std::string *>(a.p + 8)->data() : nullptr;
            const char *bufB = reinterpret_cast<std::string *>(b.p + 8)->capacity() > 15 ? reinterpret_cast<std::string *>(b.p + 8)->data() : nullptr;
            difftest::FreeRecorder recA;
            void *ra = ctx.Original<DtorFn>(spec.rva)(a.p, flags);
            recA.Stop();
            bool objFreedA = (flags & 1) && recA.Freed(a.p);
            bool objGoneA = recA.Freed(a.p);
            bool bufFreedA = bufA == nullptr || recA.Freed(bufA);
            std::vector<uint8_t> afterA;
            if (!(flags & 1)) afterA.assign(a.p, a.p + spec.size);
            difftest::FreeRecorder recB;
            void *rb = spec.ours(b.p, flags);
            recB.Stop();
            bool objFreedB = (flags & 1) && recB.Freed(b.p);
            bool objGoneB = recB.Freed(b.p);
            bool bufFreedB = bufB == nullptr || recB.Freed(bufB);
            ++ctx.cases;
            if (ra != a.p || rb != b.p) ctx.Fail("%s: return value", spec.name);
            if ((flags & 1) && (!objFreedA || !objFreedB)) ctx.Fail("%s (flags %u): object freed original %d ours %d", spec.name, flags, objFreedA, objFreedB);
            if (!(flags & 1) && (objGoneA || objGoneB)) ctx.Fail("%s (flags %u): object freed without bit 0", spec.name, flags);
            if (!bufFreedA || !bufFreedB) ctx.Fail("%s: name buffer not freed (original %d ours %d)", spec.name, bufFreedA, bufFreedB);
            if (!(flags & 1)) {
                // vtable, id, fields identical; the name is the empty small string (size 0, capacity 15, first byte 0).
                const uint8_t *pb = b.p;
                const uint8_t *pa = afterA.data();
                auto sizeOf = [](const uint8_t *p) { return *reinterpret_cast<const uint32_t *>(p + 0x18); };
                auto capOf = [](const uint8_t *p) { return *reinterpret_cast<const uint32_t *>(p + 0x1c); };
                if (std::memcmp(pa + 4, pb + 4, 4) != 0) ctx.Fail("%s: id differs", spec.name);
                if (!pftest::VTablesCorrespond(ctx, spec.cls, true, pftest::VTableOf(pa), pftest::VTableOf(pb))) ctx.Fail("%s: vtable is not the mapped BaseT vtable", spec.name);
                if (spec.size > 0x20 && std::memcmp(pa + 0x20, pb + 0x20, spec.size - 0x20) != 0) ctx.Fail("%s: fields differ", spec.name);
                if (sizeOf(pa) != sizeOf(pb) || capOf(pa) != capOf(pb) || sizeOf(pb) != 0 || capOf(pb) != 15 || pa[8] != pb[8] || pb[8] != 0) {
                    ctx.Fail("%s: name not left empty (orig %u/%u first %02x, ours %u/%u first %02x)", spec.name, sizeOf(pa), capOf(pa), pa[8], sizeOf(pb), capOf(pb), pb[8]);
                }
                if (bufA == nullptr && std::memcmp(pa + 9, pb + 9, 15) != 0) ctx.Fail("%s: small-string bytes differ", spec.name);
            }
            if (!(flags & 1)) {
                ::operator delete(a.p);
                ::operator delete(b.p);
            }
        }
    }
}

} // namespace
