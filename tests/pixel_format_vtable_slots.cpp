// Every slot of every pixel format vtable, called through the object's own vtable pointer: the original's
// objects (original constructors, the image's vtables) and ours (our constructors, the vtables our build
// emits) are driven with the same inputs and the slot results and the objects afterwards are compared.
// Our code is reached only through our vtables here, so a wrong function, a swapped pair of slots or a
// wrong slot count in src/gz/PixelFormatVTables.cpp fails these tests even though the per-function tests
// (pixel_format.cpp, which call our members directly) would still pass.
//
// The vtable word itself is the one thing that differs between the sides; it is checked, never ignored:
// pixel_format_vtables.h asserts that each side holds exactly its mapped vtable of the class.
#include "difftest.h"
#include "free_recorder.h"
#include "game/vtable.h"
#include "gz/PixelFormat.h"
#include "gz/PixelFormatStore.h"
#include "gz/PixelFormatsManager.h"
#include "pixel_format_fixture.h"
#include "pixel_format_vtables.h"

#include <windows.h>

#include <cstring>
#include <random>
#include <string>
#include <vector>

extern "C" int __cdecl _purecall(void);

using namespace nGZGraphic4;

namespace {

constexpr uint32_t kPurecallRva = 0x8261ba; // the image's _purecall

uint32_t Pick(std::mt19937 &rng, std::initializer_list<uint32_t> values)
{
    return *(values.begin() + rng() % values.size());
}

uint32_t Bits(std::mt19937 &rng)
{
    switch (rng() % 6) {
    case 0: return 0;
    case 1: return 1 + rng() % 12;
    case 2: return Pick(rng, {1, 2, 4, 5, 6, 8, 10, 11, 16, 24, 32});
    case 3: return rng() % 40;
    case 4: return Pick(rng, {0x7fffffffu, 0x80000000u, 0xffffffffu, 31, 33, 255, 256});
    default: return rng() % 33;
    }
}

constexpr uint32_t Size(FormatClass c)
{
    switch (c) {
    case FormatClass::Base: return cPixelFormat::kSize;
    case FormatClass::ABGRFloat: return cPixelFormatABGRFloat::kSize;
    case FormatClass::ARGB: return cPixelFormatARGB::kSize;
    case FormatClass::AlphaLuminance: return cPixelFormatAlphaLuminance::kSize;
    case FormatClass::DXT: return cPixelFormatDXT::kSize;
    case FormatClass::DepthStencil: return cPixelFormatDepthStencil::kSize;
    case FormatClass::Unknown: return cPixelFormatUnknown::kSize;
    case FormatClass::YUV: return cPixelFormatYUV::kSize;
    }
    return 0;
}

const char *Name(FormatClass c)
{
    return pftest::EntryFor(c, false).name;
}

// ---------------------------------------------------------------------------------------------------
// An object of a class in both worlds

struct Obj {
    FormatClass cls;
    bool baseT = false; // the vtable word was switched to the class's cPixelFormatBaseT<> vtable
    uint8_t *orig = nullptr;
    uint8_t *ours = nullptr;
    uint32_t size = 0;
};

template <typename Fn, typename... Args> void *CallOrig(const difftest::Context &ctx, uint32_t rva, Args... args)
{
    return ctx.Original<Fn>(rva)(args...);
}

// Constructs one object of `cls` on each side from the same random fill and constructor arguments.
Obj Make(const difftest::Context &ctx, std::mt19937 &rng, FormatClass cls)
{
    Obj o;
    o.cls = cls;
    o.size = Size(cls);
    std::vector<uint8_t> fill(o.size);
    for (auto &b : fill) b = static_cast<uint8_t>(rng());
    o.orig = static_cast<uint8_t *>(::operator new(o.size));
    o.ours = static_cast<uint8_t *>(::operator new(o.size));
    std::memcpy(o.orig, fill.data(), o.size);
    std::memcpy(o.ours, fill.data(), o.size);
    uint32_t id = rng() % 4 == 0 ? static_cast<uint32_t>(rng()) : Pick(rng, {0, 21, 22, 23, 26, 50, 51, 0x31545844, 0x32595559});
    // Channel counts: keep them distinct more often than chance so swapped accessors differ.
    uint32_t a = Bits(rng), b = Bits(rng), c = Bits(rng), d = Bits(rng), e = Bits(rng);
    if (rng() % 2) {
        a = 1 + rng() % 7;
        b = 8 + rng() % 7;
        c = 15 + rng() % 7;
        d = 22 + rng() % 7;
        e = 29 + rng() % 7;
    }
    switch (cls) {
    case FormatClass::Base:
        if (rng() % 3 == 0) id = static_cast<uint32_t>(rng()) | 0x10000000; // a long name (heap)
        CallOrig<void *(__thiscall *)(void *, uint32_t)>(ctx, 0x15b840, o.orig, id);
        reinterpret_cast<cPixelFormat *>(o.ours)->Construct(id);
        break;
    case FormatClass::ABGRFloat:
        CallOrig<void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(ctx, 0x15ba30, o.orig, id, a, b, c, d);
        reinterpret_cast<cPixelFormatABGRFloat *>(o.ours)->Construct(id, a, b, c, d);
        break;
    case FormatClass::ARGB:
        CallOrig<void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(ctx, 0x15bf40, o.orig, id, e, a, b, c, d);
        reinterpret_cast<cPixelFormatARGB *>(o.ours)->Construct(id, e, a, b, c, d);
        break;
    case FormatClass::AlphaLuminance:
        CallOrig<void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t)>(ctx, 0x15c5b0, o.orig, id, e, a, b);
        reinterpret_cast<cPixelFormatAlphaLuminance *>(o.ours)->Construct(id, e, a, b);
        break;
    case FormatClass::DXT: {
        uint32_t version = rng() % 4 == 0 ? static_cast<uint32_t>(rng() % 9) : 1 + rng() % 5;
        CallOrig<void *(__thiscall *)(void *, uint32_t, uint32_t)>(ctx, 0x15c9c0, o.orig, id, version);
        reinterpret_cast<cPixelFormatDXT *>(o.ours)->Construct(id, version);
        break;
    }
    case FormatClass::DepthStencil: {
        uint32_t flag = rng() % 2;
        CallOrig<void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(ctx, 0x15cc10, o.orig, id, e, a, b, flag);
        reinterpret_cast<cPixelFormatDepthStencil *>(o.ours)->Construct(id, e, a, b, static_cast<uint8_t>(flag));
        break;
    }
    case FormatClass::Unknown: {
        // No constructor: the original has one static instance. Copy it for the original side; ours
        // is initialised the way the image's startup does it.
        pftest::EnsureStatics(ctx);
        std::memcpy(o.orig, ctx.image.At(pftest::kOriginalUnknown), o.size);
        reinterpret_cast<cPixelFormatUnknown *>(o.ours)->InitStatic();
        break;
    }
    case FormatClass::YUV: {
        const char *fourcc[] = {"YUY2", "UYVY", "NV12", "AbCd"};
        const char *f = fourcc[rng() % 4];
        CallOrig<void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(ctx, 0x15d230, o.orig, id, uint32_t(f[0]), uint32_t(f[1]), uint32_t(f[2]), uint32_t(f[3]));
        reinterpret_cast<cPixelFormatYUV *>(o.ours)->Construct(id, f[0], f[1], f[2], f[3]);
        break;
    }
    }
    return o;
}

void SwitchToBaseT(Obj &o, const difftest::Context &ctx)
{
    const pftest::VTableMapEntry &e = pftest::EntryFor(o.cls, true);
    const void *orig = ctx.image.At(e.originalRva);
    const void *ours = pftest::OursOf(e);
    std::memcpy(o.orig, &orig, 4);
    std::memcpy(o.ours, &ours, 4);
    o.baseT = true;
}

void Destroy(Obj &o)
{
    reinterpret_cast<std::string *>(o.orig + 8)->~basic_string();
    reinterpret_cast<std::string *>(o.ours + 8)->~basic_string();
    ::operator delete(o.orig);
    ::operator delete(o.ours);
}

// The two objects must agree everywhere except the vtable word, which must be the mapped pair.
std::string Differences(const difftest::Context &ctx, const Obj &o)
{
    if (!pftest::VTablesCorrespond(ctx, o.cls, o.baseT, pftest::VTableOf(o.orig), pftest::VTableOf(o.ours))) return "vtable pair";
    if (std::memcmp(o.orig + 4, o.ours + 4, 4) != 0) return "format id";
    if (o.size > 0x20 && std::memcmp(o.orig + 0x20, o.ours + 0x20, o.size - 0x20) != 0) return "fields";
    auto &na = *reinterpret_cast<const std::string *>(o.orig + 8);
    auto &nb = *reinterpret_cast<const std::string *>(o.ours + 8);
    if (na.size() != nb.size() || na.capacity() != nb.capacity() || std::memcmp(na.c_str(), nb.c_str(), na.size() + 1) != 0) return "name";
    return "";
}

// ---------------------------------------------------------------------------------------------------
// Slot calls through the object's own vtable

enum class Arg { None, Name, QueryInterface, Width, Less, Pack, Unpack, Dtor };

Arg KindOf(FormatClass cls, int slot)
{
    int last = kVTableSlots[int(cls)] - 1;
    if (slot == last) return Arg::Dtor;
    if (slot == 0) return Arg::QueryInterface;
    if (slot == 4) return Arg::Name;
    if (slot == 9) return Arg::Width;
    if (slot == 10) return Arg::Less;
    if (cls == FormatClass::ARGB && slot == 16) return Arg::Pack;
    if (cls == FormatClass::ARGB && slot == 17) return Arg::Unpack;
    return Arg::None;
}

// Slots whose result is a byte or bool (only al is defined).
bool ByteResult(FormatClass cls, int slot, Arg kind)
{
    return kind == Arg::QueryInterface || kind == Arg::Less || (cls == FormatClass::DepthStencil && slot == 12);
}

using Fn0 = uint32_t(__thiscall *)(void *);
using Fn1 = uint32_t(__thiscall *)(void *, uint32_t);

// Runs every non-destructor slot of one object pair (class vtable or BaseT vtable) and compares.
void RunSlots(difftest::Context &ctx, std::mt19937 &rng, const Obj &o, const Obj &other)
{
    const char *label = pftest::EntryFor(o.cls, o.baseT).name;
    int slots = kVTableSlots[int(o.cls)];
    const void *const *origTable = static_cast<const void *const *>(ctx.image.At(pftest::EntryFor(o.cls, o.baseT).originalRva));
    const void *origPurecall = ctx.image.At(kPurecallRva);
    for (int slot = 0; slot < slots - 1; ++slot) {
        Arg kind = KindOf(o.cls, slot);
        const void *oursSlot = static_cast<const void *const *>(pftest::VTableOf(o.ours))[slot];
        if (origTable[slot] == origPurecall) {
            // The original has _purecall here (BaseT vtables): ours must have the CRT's.
            ++ctx.cases;
            if (oursSlot != reinterpret_cast<const void *>(&_purecall)) ctx.Fail("%s slot %d: original has _purecall, ours has %p", label, slot, oursSlot);
            continue;
        }
        if (oursSlot == reinterpret_cast<const void *>(&_purecall)) {
            ++ctx.cases;
            ctx.Fail("%s slot %d: ours has _purecall where the original has a function", label, slot);
            continue;
        }
        for (int rep = 0; rep < 3; ++rep) {
            uint32_t ra = 0, rb = 0;
            std::string textA, textB;
            uint8_t outA[8], outB[8];
            std::memset(outA, 0xcd, 8);
            std::memset(outB, 0xcd, 8);
            uint32_t scalar = rng() % 4 == 0 ? static_cast<uint32_t>(rng()) : rng() % 70;
            switch (kind) {
            case Arg::Name: {
                const char *a = reinterpret_cast<const char *(__thiscall *)(void *)>(origTable[slot])(o.orig);
                const char *b = VirtualSlot<const char *(__thiscall *)(void *)>(o.ours, slot)(o.ours);
                // The slot answers the characters of the name inside the object.
                ra = a == reinterpret_cast<std::string *>(o.orig + 8)->c_str();
                rb = b == reinterpret_cast<std::string *>(o.ours + 8)->c_str();
                textA = a;
                textB = b;
                break;
            }
            case Arg::QueryInterface: {
                const uint32_t known[] = {kIidUnknown, kIidPixelFormat, kIidRgbColorSpace, kIidABGRFloat, kIidARGB, kIidAlphaLuminance, kIidDXT, kIidDepthStencil, kIidUnknownFormat, kIidYUV, 0, 2};
                uint32_t riid = rng() % 5 == 0 ? static_cast<uint32_t>(rng()) : known[rng() % (sizeof(known) / sizeof(known[0]))];
                void *pa = reinterpret_cast<void *>(0x1234abcd), *pb = pa;
                using QiFn = uint32_t(__thiscall *)(void *, uint32_t, void **);
                pa = pb = reinterpret_cast<void *>(0x1234abcd);
                ra = reinterpret_cast<QiFn>(origTable[slot])(o.orig, riid, &pa) & 0xff;
                rb = VirtualSlot<QiFn>(o.ours, slot)(o.ours, riid, &pb) & 0xff;
                // Compare what was written as "self" or "untouched".
                textA = pa == o.orig ? "self" : (pa == reinterpret_cast<void *>(0x1234abcd) ? "untouched" : "other");
                textB = pb == o.ours ? "self" : (pb == reinterpret_cast<void *>(0x1234abcd) ? "untouched" : "other");
                break;
            }
            case Arg::Width:
                ra = reinterpret_cast<Fn1>(origTable[slot])(o.orig, scalar);
                rb = VirtualSlot<Fn1>(o.ours, slot)(o.ours, scalar);
                break;
            case Arg::Less: {
                bool strict = rng() % 2;
                // The other object is of the same world; its class vtable (not BaseT) answers the nested calls.
                using LessFn = uint32_t(__thiscall *)(void *, const void *, uint32_t);
                uint32_t sw = strict ? (rng() | 1) & 0xff : 0;
                ra = reinterpret_cast<LessFn>(origTable[slot])(o.orig, other.orig, sw) & 0xff;
                rb = VirtualSlot<LessFn>(o.ours, slot)(o.ours, other.ours, strict ? 1 : 0) & 0xff;
                break;
            }
            case Arg::Pack: {
                uint8_t bgra[4];
                for (auto &c : bgra) c = rng() % 3 == 0 ? static_cast<uint8_t>(Pick(rng, {0, 1, 127, 128, 254, 255})) : static_cast<uint8_t>(rng());
                ra = reinterpret_cast<Fn1>(origTable[slot])(o.orig, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(bgra)));
                rb = VirtualSlot<Fn1>(o.ours, slot)(o.ours, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(bgra)));
                break;
            }
            case Arg::Unpack: {
                uint32_t pixel = rng() % 4 == 0 ? Pick(rng, {0, 0xffffffffu, 0x80000000u, 1}) : static_cast<uint32_t>(rng());
                using UnpackFn = uint8_t *(__thiscall *)(void *, uint8_t *, uint32_t);
                uint8_t *pa = reinterpret_cast<UnpackFn>(origTable[slot])(o.orig, outA, pixel);
                uint8_t *pb = VirtualSlot<UnpackFn>(o.ours, slot)(o.ours, outB, pixel);
                ra = pa == outA;
                rb = pb == outB;
                break;
            }
            default:
                ra = reinterpret_cast<Fn0>(origTable[slot])(o.orig);
                rb = VirtualSlot<Fn0>(o.ours, slot)(o.ours);
                if (ByteResult(o.cls, slot, kind)) {
                    ra &= 0xff;
                    rb &= 0xff;
                }
                break;
            }
            ++ctx.cases;
            if (ra != rb || textA != textB || std::memcmp(outA, outB, 8) != 0) {
                ctx.Fail("%s slot %d: original %08x \"%s\" ours %08x \"%s\"", label, slot, ra, textA.c_str(), rb, textB.c_str());
            }
            std::string diff = Differences(ctx, o);
            if (!diff.empty()) ctx.Fail("%s slot %d changed the object differently (%s)", label, slot, diff.c_str());
        }
    }
}

// ---------------------------------------------------------------------------------------------------

DIFF_TEST(PixelFormatVTables_Layout, 0x15f980)
{
    // The map lists 8 class vtables, 8 BaseT vtables and the manager; slot counts agree with ours.
    for (const pftest::VTableMapEntry &e : pftest::kVTableMap) {
        ++ctx.cases;
        int expected = e.cls < 0 ? kManagerVTableSlots : kVTableSlots[e.cls];
        if (e.slots != expected) ctx.Fail("%s: map says %d slots, ours has %d", e.name, e.slots, expected);
        const void *ours = pftest::OursOf(e);
        if (!ours) ctx.Fail("%s: no vtable", e.name);
        // The word before slot 0 (where the original keeps its RTTI locator) is null in ours.
        if (ours && static_cast<const void *const *>(ours)[-1] != nullptr) ctx.Fail("%s: word before slot 0 is not null", e.name);
        // No two of our vtables are the same table, and none lies inside the image.
        uintptr_t base = ctx.image.Base();
        if (reinterpret_cast<uintptr_t>(ours) >= base && reinterpret_cast<uintptr_t>(ours) < base + ctx.image.Size()) ctx.Fail("%s: our vtable is inside the image", e.name);
        for (const pftest::VTableMapEntry &f : pftest::kVTableMap) {
            if (&e != &f && pftest::OursOf(f) == ours) ctx.Fail("%s and %s share a vtable", e.name, f.name);
        }
        // Every slot of ours is a function in our module (a non-null pointer outside the image).
        for (int s = 0; s < e.slots && ours; ++s) {
            const void *fn = static_cast<const void *const *>(ours)[s];
            if (!fn || (reinterpret_cast<uintptr_t>(fn) >= base && reinterpret_cast<uintptr_t>(fn) < base + ctx.image.Size())) ctx.Fail("%s slot %d is null or inside the image", e.name, s);
        }
    }
    // The original's slots are all code in the image.
    for (const pftest::VTableMapEntry &e : pftest::kVTableMap) {
        const uint32_t *tbl = ctx.image.At<uint32_t>(e.originalRva);
        for (int s = 0; s < e.slots; ++s) {
            ++ctx.cases;
            uint32_t fn = tbl[s];
            if (fn < ctx.image.Base() || fn >= ctx.image.Base() + ctx.image.Size()) ctx.Fail("%s: original slot %d is not an image address", e.name, s);
        }
    }
}

DIFF_TEST(PixelFormatVTables_ClassSlots, 0x15fd30)
{
    std::mt19937 rng(0x5107);
    for (int c = 0; c < kFormatClassCount; ++c) {
        FormatClass cls = FormatClass(c);
        for (int i = 0; i < 400; ++i) {
            Obj o = Make(ctx, rng, cls);
            Obj other = Make(ctx, rng, FormatClass(rng() % kFormatClassCount));
            ++ctx.cases;
            std::string diff = Differences(ctx, o);
            if (!diff.empty()) ctx.Fail("%s constructed differently (%s)", Name(cls), diff.c_str());
            // Same class often, so the same-class branches of Less are reached.
            if (i % 2 == 0) {
                Destroy(other);
                other = Make(ctx, rng, cls);
            }
            RunSlots(ctx, rng, o, other);
            Destroy(o);
            Destroy(other);
        }
    }
}

DIFF_TEST(PixelFormatVTables_BaseTSlots, 0x15f9b0)
{
    std::mt19937 rng(0xba5e);
    for (int c = 0; c < kFormatClassCount; ++c) {
        FormatClass cls = FormatClass(c);
        for (int i = 0; i < 300; ++i) {
            Obj o = Make(ctx, rng, cls);
            Obj other = Make(ctx, rng, FormatClass(rng() % kFormatClassCount));
            if (i % 2 == 0) {
                Destroy(other);
                other = Make(ctx, rng, cls);
            }
            SwitchToBaseT(o, ctx);
            RunSlots(ctx, rng, o, other);
            Destroy(o);
            Destroy(other);
        }
    }
}

// The deleting destructor slot (the last one of each vtable), class and BaseT versions.
DIFF_TEST(PixelFormatVTables_DeletingDestructorSlots, 0x15dcb0)
{
    std::mt19937 rng(0xde1e7e);
    using DtorFn = void *(__thiscall *)(void *, uint32_t);
    for (int c = 0; c < kFormatClassCount; ++c) {
        FormatClass cls = FormatClass(c);
        for (int baseT = 0; baseT < 2; ++baseT) {
            const pftest::VTableMapEntry &e = pftest::EntryFor(cls, baseT != 0);
            int slot = e.slots - 1;
            const void *const *origTable = static_cast<const void *const *>(ctx.image.At(e.originalRva));
            for (int i = 0; i < 200; ++i) {
                Obj o = Make(ctx, rng, cls);
                if (baseT) SwitchToBaseT(o, ctx);
                uint32_t flags = i % 4 == 0 ? static_cast<uint32_t>(rng()) : rng() % 4;
                auto &sa = *reinterpret_cast<std::string *>(o.orig + 8);
                auto &sb = *reinterpret_cast<std::string *>(o.ours + 8);
                const char *bufA = sa.capacity() > 15 ? sa.data() : nullptr;
                const char *bufB = sb.capacity() > 15 ? sb.data() : nullptr;
                difftest::FreeRecorder recA;
                void *ra = reinterpret_cast<DtorFn>(origTable[slot])(o.orig, flags);
                recA.Stop();
                bool objFreedA = (flags & 1) && recA.Freed(o.orig);
                bool bufFreedA = bufA == nullptr || recA.Freed(bufA);
                difftest::FreeRecorder recB;
                void *rb = VirtualSlot<DtorFn>(o.ours, slot)(o.ours, flags);
                recB.Stop();
                bool objFreedB = (flags & 1) && recB.Freed(o.ours);
                bool bufFreedB = bufB == nullptr || recB.Freed(bufB);
                ++ctx.cases;
                if (ra != o.orig || rb != o.ours) ctx.Fail("%s slot %d: dtor returned another pointer", e.name, slot);
                if ((flags & 1) && (!objFreedA || !objFreedB)) ctx.Fail("%s slot %d (flags %u): object freed original %d ours %d", e.name, slot, flags, objFreedA, objFreedB);
                if (!bufFreedA || !bufFreedB) ctx.Fail("%s slot %d: name buffer not freed (original %d ours %d)", e.name, slot, bufFreedA, bufFreedB);
                if (!(flags & 1)) {
                    // What it leaves: the BaseT vtable (mapped pair), the same id and fields, an empty name.
                    o.baseT = true;
                    std::string diff = Differences(ctx, o);
                    if (!diff.empty()) ctx.Fail("%s slot %d (flags %u): objects differ afterwards (%s)", e.name, slot, flags, diff.c_str());
                    if (sa.size() != 0 || sb.size() != 0 || sa.capacity() != 15 || sb.capacity() != 15) ctx.Fail("%s slot %d: name not left empty", e.name, slot);
                    ::operator delete(o.orig);
                    ::operator delete(o.ours);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------
// The manager: every slot through its own vtable

struct StoreScope {
    PixelFormatStoreProvider saved;
    explicit StoreScope(PixelFormatStoreProvider p) : saved(CurrentPixelFormatStoreProvider()) { SetPixelFormatStoreProvider(p); }
    ~StoreScope() { SetPixelFormatStoreProvider(saved); }
};

// A store like the standalone one, but with a recording CreateIterator.
struct Recorded {
    cPixelFormatsManagerDX *self;
    void *out;
    uint32_t flags;
    int calls;
};
Recorded gRecorded;
void *__fastcall FakeCreateIterator(cPixelFormatsManagerDX *self, void *, void *out, uint32_t flags)
{
    gRecorded = {self, out, flags, gRecorded.calls + 1};
    return out;
}
const PixelFormatStore &StoreWithIterator()
{
    static PixelFormatStore store = [] {
        PixelFormatStore s = StandaloneStore();
        s.createIterator = reinterpret_cast<decltype(s.createIterator)>(&FakeCreateIterator);
        return s;
    }();
    return store;
}

DIFF_TEST(PixelFormatVTables_ManagerSlots, 0x15fc70)
{
    // The original manager over the image's formats (built by the image's static initialisers, run by
    // the PixelFormatsManager tests) and ours over our own; slot results compared as table indexes.
    std::mt19937 rng(0x3a9);
    pftest::EnsureStatics(ctx);
    StoreScope scope(&StandaloneStore);
    const PixelFormatStore &ours = StandaloneStore();
    // The image's table, with the formats built (the manager tests' fixture, here rebuilt by the same initialisers).
    std::vector<cPixelFormat *> origTable = pftest::OriginalTable(ctx);
    cPixelFormat *origUnknown = pftest::OriginalUnknown(ctx);
    auto indexOrig = [&](const void *p) {
        if (!p) return -1;
        if (p == origUnknown) return int(kFormatCount);
        for (size_t i = 0; i < origTable.size(); ++i)
            if (origTable[i] == p) return int(i);
        return -2;
    };
    auto indexOurs = [&](const void *p) {
        if (!p) return -1;
        if (p == ours.unknown) return int(kFormatCount);
        for (uint32_t i = 0; i < kFormatCount; ++i)
            if (ours.table[i] == p) return int(i);
        return -2;
    };

    const pftest::VTableMapEntry &e = pftest::ManagerEntry();
    const void *const *origVT = static_cast<const void *const *>(ctx.image.At(e.originalRva));
    // The original manager must exist already; build one with the original constructor.
    uint8_t *a = static_cast<uint8_t *>(::operator new(cPixelFormatsManagerDX::kSize));
    uint8_t *b = static_cast<uint8_t *>(::operator new(cPixelFormatsManagerDX::kSize));
    std::memset(a, 0, cPixelFormatsManagerDX::kSize);
    std::memset(b, 0, cPixelFormatsManagerDX::kSize);
    ctx.Original<void *(__thiscall *)(void *)>(0x15d4b0)(a);
    reinterpret_cast<cPixelFormatsManagerDX *>(b)->Construct();
    ++ctx.cases;
    if (!pftest::VTablesCorrespond(ctx, e, pftest::VTableOf(a), pftest::VTableOf(b))) ctx.Fail("manager vtable pair");
    const void *const *oursVT = static_cast<const void *const *>(pftest::VTableOf(b));
    // Slot 3 (CreateIterator): the original's is the image's function; ours is a forwarder (below).
    ++ctx.cases;
    if (origVT[3] != ctx.image.At(0x15e860) || !oursVT[3]) ctx.Fail("manager slot 3");

    using Q = uint32_t(__thiscall *)(void *, uint32_t, void **);
    using F0 = uint32_t(__thiscall *)(void *);
    using F1P = void *(__thiscall *)(void *);
    using F1 = void *(__thiscall *)(void *, uint32_t);
    using F5 = void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    using F4 = void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t);
    using F3 = void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
    using FP = void *(__thiscall *)(void *, const uint8_t *);
    for (int i = 0; i < 2000; ++i) {
        // Slot 0: QueryInterface
        uint32_t riid = Pick(rng, {1u, kIidPixelFormatsManager, kIidPixelFormat, 0u, static_cast<uint32_t>(rng())});
        void *pa = reinterpret_cast<void *>(0x77), *pb = pa;
        uint32_t qa = reinterpret_cast<Q>(origVT[0])(a, riid, &pa) & 0xff;
        uint32_t qb = VirtualSlot<Q>(b, 0)(b, riid, &pb) & 0xff;
        ++ctx.cases;
        if (qa != qb || (pa == a) != (pb == b) || (pa == reinterpret_cast<void *>(0x77)) != (pb == reinterpret_cast<void *>(0x77))) ctx.Fail("manager slot 0 (%08x)", riid);
        // Slots 1, 2
        if (reinterpret_cast<F0>(origVT[1])(a) != VirtualSlot<F0>(b, 1)(b) || reinterpret_cast<F0>(origVT[2])(a) != VirtualSlot<F0>(b, 2)(b)) ctx.Fail("manager slots 1/2");
        // Slots 4, 5: GetFormat, GetUnknownFormat
        uint32_t id = i % 3 == 0 ? static_cast<uint32_t>(rng()) : kStaticFormatSpecs[rng() % kFormatCount].id;
        if (indexOrig(reinterpret_cast<F1>(origVT[4])(a, id)) != indexOurs(VirtualSlot<F1>(b, 4)(b, id))) ctx.Fail("manager slot 4 (%08x)", id);
        if (indexOrig(reinterpret_cast<F1P>(origVT[5])(a)) != indexOurs(VirtualSlot<F1P>(b, 5)(b))) ctx.Fail("manager slot 5");
        // Slots 6-11: the lookups, with the parameters of a random format (often) or random ones
        const StaticFormatSpec &spec = kStaticFormatSpecs[rng() % kFormatCount];
        uint32_t p[5] = {spec.args[0], spec.args[1], spec.args[2], spec.args[3], spec.args[4]};
        if (i % 3 == 0) p[rng() % 5] += rng() % 3;
        auto cmp = [&](void *x, void *y, int slot) {
            ++ctx.cases;
            if (indexOrig(x) != indexOurs(y)) ctx.Fail("manager slot %d (%u %u %u %u %u): original #%d ours #%d", slot, p[0], p[1], p[2], p[3], p[4], indexOrig(x), indexOurs(y));
        };
        cmp(reinterpret_cast<F5>(origVT[6])(a, p[0], p[1], p[2], p[3], p[4]), VirtualSlot<F5>(b, 6)(b, p[0], p[1], p[2], p[3], p[4]), 6);
        cmp(reinterpret_cast<F3>(origVT[7])(a, p[0], p[1], p[2]), VirtualSlot<F3>(b, 7)(b, p[0], p[1], p[2]), 7);
        cmp(reinterpret_cast<F4>(origVT[8])(a, p[0], p[1], p[2], p[3]), VirtualSlot<F4>(b, 8)(b, p[0], p[1], p[2], p[3]), 8);
        uint8_t chars[4] = {uint8_t(p[0]), uint8_t(p[1]), uint8_t(p[2]), uint8_t(p[3])};
        cmp(reinterpret_cast<FP>(origVT[9])(a, chars), VirtualSlot<FP>(b, 9)(b, chars), 9);
        cmp(reinterpret_cast<F1>(origVT[10])(a, spec.kind == StaticKind::DXT ? spec.args[0] : p[0] % 7), VirtualSlot<F1>(b, 10)(b, spec.kind == StaticKind::DXT ? spec.args[0] : p[0] % 7), 10);
        cmp(reinterpret_cast<F4>(origVT[11])(a, p[0], p[1], p[2], p[3]), VirtualSlot<F4>(b, 11)(b, p[0], p[1], p[2], p[3]), 11);
    }

    // Slot 3 forwards to the store's CreateIterator with (self, out, flags) and returns its result.
    {
        StoreScope fake(&StoreWithIterator);
        gRecorded = {};
        uint8_t out[16];
        void *r = VirtualSlot<void *(__thiscall *)(void *, void *, uint32_t)>(b, 3)(b, out, 0x1234abcd);
        ++ctx.cases;
        if (r != out || gRecorded.calls != 1 || gRecorded.self != reinterpret_cast<cPixelFormatsManagerDX *>(b) || gRecorded.out != out || gRecorded.flags != 0x1234abcd) ctx.Fail("manager slot 3 does not forward (self, out, flags)");
    }

    reinterpret_cast<cPixelFormatsManagerDX *>(a)->Destruct();
    reinterpret_cast<cPixelFormatsManagerDX *>(b)->Destruct();
    ::operator delete(a);
    ::operator delete(b);

    // Slot 12: the deleting destructor of a freshly built manager of each world.
    using DtorFn = void *(__thiscall *)(void *, uint32_t);
    for (int i = 0; i < 100; ++i) {
        uint32_t flags = i % 4 == 0 ? static_cast<uint32_t>(rng()) : rng() % 4;
        uint8_t *ma = static_cast<uint8_t *>(::operator new(cPixelFormatsManagerDX::kSize));
        uint8_t *mb = static_cast<uint8_t *>(::operator new(cPixelFormatsManagerDX::kSize));
        std::memset(ma, 0, cPixelFormatsManagerDX::kSize);
        std::memset(mb, 0, cPixelFormatsManagerDX::kSize);
        ctx.Original<void *(__thiscall *)(void *)>(0x15d4b0)(ma);
        reinterpret_cast<cPixelFormatsManagerDX *>(mb)->Construct();
        void *blocksA[3] = {*reinterpret_cast<void **>(ma + 0x24), *reinterpret_cast<void **>(ma + 8), *reinterpret_cast<void **>(ma + 0x10)};
        void *blocksB[3] = {*reinterpret_cast<void **>(mb + 0x24), *reinterpret_cast<void **>(mb + 8), *reinterpret_cast<void **>(mb + 0x10)};
        bool freedA = true, freedB = true;
        difftest::FreeRecorder recA;
        void *ra = reinterpret_cast<DtorFn>(origVT[12])(ma, flags);
        recA.Stop();
        bool objA = (flags & 1) && recA.Freed(ma);
        for (void *q : blocksA) freedA = freedA && recA.Freed(q);
        difftest::FreeRecorder recB;
        void *rb = VirtualSlot<DtorFn>(mb, 12)(mb, flags);
        recB.Stop();
        bool objB = (flags & 1) && recB.Freed(mb);
        for (void *q : blocksB) freedB = freedB && recB.Freed(q);
        ++ctx.cases;
        if (ra != ma || rb != mb) ctx.Fail("manager slot 12: return value");
        if (!freedA || !freedB) ctx.Fail("manager slot 12: map or vector blocks not freed (original %d ours %d)", freedA, freedB);
        if ((flags & 1) && (!objA || !objB)) ctx.Fail("manager slot 12 (flags %u): object freed original %d ours %d", flags, objA, objB);
        if (!(flags & 1)) {
            if (!pftest::VTablesCorrespond(ctx, e, pftest::VTableOf(ma), pftest::VTableOf(mb))) ctx.Fail("manager slot 12: vtable pair");
            if (std::memcmp(ma + 0x24, mb + 0x24, 12) != 0) ctx.Fail("manager slot 12: vector not emptied the same way");
            ::operator delete(ma);
            ::operator delete(mb);
        }
    }
}

} // namespace
