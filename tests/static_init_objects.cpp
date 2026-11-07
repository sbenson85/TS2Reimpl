// Differential tests for the owned XC entries that build statics of our classes (src/app/StaticInitObjects.cpp,
// lane GQ; docs/subsystems/startup.md "Stage 3: the static initialisers"): the 57 pixel format entries, the 10
// cRZCriticalSection entries and the 5 cRZRandom entries.
//
// Each entry against the image's initialiser in its slot (the slot checked to hold it), on the same start state
// (the object and 8 bytes on each side filled with junk; for the constant-initialised DXT and unknown formats
// also the old name's capacity chosen: inline, or a heap block the move-assignment must free):
//   * construction: the window after the initialiser, and the destructor registration (exactly one, or none);
//   * destruction: the registered thunk run (the image's, ours), the window after it and the heap blocks it freed
//     (difftest::FreeRecorder, sizes).
// The registration: the image's atexit (0x824cbe, library code working on the image CRT's exit table, which the
// harness never initialises; tests/rz_allocator_service.cpp) is redirected to a recorder for the test, and ours
// goes through staticinit::BindAtExit, the binding the host points at the image CRT's atexit under
// [Startup] OwnStart=1/2 (host/main.cpp, ownstart: ImageAtExit), bound here to the same recorder.
//
// What the comparison leaves out, and why:
//   * The vtable word of a constructed or destroyed pixel format: the image's objects carry the image's vtables,
//     ours ours; it is compared through the explicit map of tests/pixel_format_vtables.h (the original's must be
//     exactly the mapped original vtable, ours exactly ours).
//   * A pixel format name's 16-byte inline buffer is compared as a string (size, capacity, characters, the
//     terminator), not byte for byte: the image moves the name in from a temporary on its stack, all 16 bytes
//     (cPixelFormat::Construct 0x15b954 `movups [esi], xmm0` from [ebp-0x2c]; the DXT entries 0x1373ac from
//     [ebp-0x28]), so the bytes past the terminator are the original's stack contents. A heap name's pointer is
//     compared by its characters (each side allocates its own block). The unknown format's entry (0x137cf0) writes
//     only seven characters into the buffer, and that window is compared byte for byte.
//   * cRZCriticalSection's block pointer (+4): each side allocates its own block (0x1af65d), compared by contents
//     (the lock count and the CRITICAL_SECTION's counters, as tests/rz_critical_section.cpp), and by the freed size.
//   * cRZRandom's seed from the clock (seed -1): the image's Seed 0x1b8ecb reads _time64 (0x82fbf6) and clock
//     (0x83a46e), both redirected to stubs (time T, clock 0: seed T); ours takes the same reading through
//     gRZRandomClockSeedOverride (the seed-control seam), which must see this entry's call site.
#include "difftest.h"
#include "free_recorder.h"
#include "app/StaticInit.h"
#include "app/StaticInitAllocators.h"
#include "app/StaticInitAny.h"
#include "app/StaticInitAtExit.h"
#include "app/StaticInitConstructed.h"
#include "app/StaticInitContainers.h"
#include "app/StaticInitParser.h"
#include "app/StaticInitH5.h"
#include "app/StaticInitHT.h"
#include "app/WinMainStart.h"
#include "gz/PixelFormat.h"
#include "gz/PixelFormatStore.h"
#include "rz/cRZRandom.h"
#include "pixel_format_vtables.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

using nGZGraphic4::FormatClass;
using nGZGraphic4::StaticKind;

constexpr uint32_t kXcFirst = 0xd9a724, kXcLast = 0xd9b3a8;
constexpr uint32_t kAtExit = 0x824cbe, kTime64 = 0x82fbf6, kClock = 0x83a46e;
constexpr int kMargin = 8;

using Thunk = void(__cdecl *)();

std::vector<Thunk> gRegistered;
int __cdecl RecordAtExit(Thunk f)
{
    gRegistered.push_back(f);
    return 0;
}

// A 5-byte jmp over an image function's entry for the duration of a test.
struct Redirect {
    uint8_t *site;
    uint8_t saved[5];
    Redirect(const difftest::Context &ctx, uint32_t rva, const void *target) : site(ctx.image.At<uint8_t>(rva))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    ~Redirect() { std::memcpy(site, saved, 5); }
};

// Both sides' atexit registrations go to gRegistered (see the top of the file).
struct AtExitCapture {
    Redirect image;
    staticinit::RegisterAtExit saved;
    explicit AtExitCapture(const difftest::Context &ctx)
        : image(ctx, kAtExit, reinterpret_cast<const void *>(&RecordAtExit)), saved(staticinit::BoundAtExit())
    {
        staticinit::BindAtExit(&RecordAtExit);
        gRegistered.clear();
    }
    ~AtExitCapture() { staticinit::BindAtExit(saved); }
};

bool SlotHolds(difftest::Context &ctx, uint32_t slot, uint32_t rva)
{
    uint32_t held = *ctx.image.At<uint32_t>(kXcFirst + 4 * slot) - static_cast<uint32_t>(ctx.image.Base());
    if (held == rva) return true;
    ctx.Fail("slot %u holds %08x, our table says %08x", slot, held, rva);
    return false;
}

std::vector<uint8_t> Window(const difftest::Context &ctx, uint32_t storage, uint32_t size)
{
    const uint8_t *p = ctx.image.At<uint8_t>(storage - kMargin);
    return std::vector<uint8_t>(p, p + size + 2 * kMargin);
}

void Put(const difftest::Context &ctx, uint32_t storage, const std::vector<uint8_t> &w)
{
    std::memcpy(ctx.image.At<uint8_t>(storage - kMargin), w.data(), w.size());
}

std::vector<size_t> Sizes(const difftest::FreeRecorder &rec)
{
    std::vector<size_t> v(512); // the exit list frees ~130 blocks with the cRZString statics
    if (rec.Count() > v.size()) v.resize(rec.Count());
    v.resize(rec.SortedSizes(v.data(), v.size()));
    return v;
}

// ---- pixel formats ----

FormatClass ClassOf(const staticinit::PixelFormatStatic &e)
{
    if (e.spec == staticinit::kUnknownFormatSpec) return FormatClass::Unknown;
    switch (nGZGraphic4::kStaticFormatSpecs[e.spec].kind) {
    case StaticKind::Base: return FormatClass::Base;
    case StaticKind::ARGB: return FormatClass::ARGB;
    case StaticKind::AlphaLuminance: return FormatClass::AlphaLuminance;
    case StaticKind::YUV: return FormatClass::YUV;
    case StaticKind::DepthStencil: return FormatClass::DepthStencil;
    case StaticKind::ABGRFloat: return FormatClass::ABGRFloat;
    case StaticKind::DXT: return FormatClass::DXT;
    }
    return FormatClass::Base;
}

uint32_t SizeOf(FormatClass c)
{
    using namespace nGZGraphic4;
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

enum class VTableCheck { Raw, Class, BaseT };

// A pixel format's window as compared (see the top of the file).
struct FormatShot {
    std::vector<uint8_t> raw; // the window; the vtable word cleared unless Raw, the name buffer cleared when semantic
    const void *vtable = nullptr;
    bool semantic = false;
    uint32_t size = 0, capacity = 0;
    std::string text;
    bool terminated = false;
};

FormatShot TakeFormat(const difftest::Context &ctx, uint32_t storage, uint32_t size, VTableCheck vt, bool semantic)
{
    FormatShot s;
    s.raw = Window(ctx, storage, size);
    uint8_t *object = s.raw.data() + kMargin;
    s.vtable = pftest::VTableOf(object);
    if (vt != VTableCheck::Raw) std::memset(object, 0, 4);
    s.semantic = semantic;
    if (semantic) {
        uint8_t *name = object + 8;
        std::memcpy(&s.size, name + 0x10, 4);
        std::memcpy(&s.capacity, name + 0x14, 4);
        const char *chars = s.capacity > 15 ? *reinterpret_cast<char *const *>(ctx.image.At<uint8_t>(storage + 8))
                                            : reinterpret_cast<const char *>(ctx.image.At<uint8_t>(storage + 8));
        if (s.size <= s.capacity && s.size < 0x1000) {
            s.text.assign(chars, s.size);
            s.terminated = chars[s.size] == 0;
        }
        std::memset(name, 0, 16);
    }
    return s;
}

bool SameFormat(const difftest::Context &ctx, FormatClass cls, VTableCheck vt, const FormatShot &a, const FormatShot &b,
                std::string &why)
{
    if (a.raw != b.raw) {
        for (size_t i = 0; i < a.raw.size(); ++i) {
            if (a.raw[i] != b.raw[i]) {
                why = "byte +" + std::to_string(static_cast<int>(i) - kMargin);
                break;
            }
        }
        return false;
    }
    if (vt != VTableCheck::Raw && !pftest::VTablesCorrespond(ctx, cls, vt == VTableCheck::BaseT, a.vtable, b.vtable)) {
        why = "vtable";
        return false;
    }
    if (a.semantic && (a.size != b.size || a.capacity != b.capacity || a.text != b.text || a.terminated != b.terminated ||
                       !a.terminated)) {
        why = "name '" + a.text + "' / '" + b.text + "'";
        return false;
    }
    return true;
}

// The start state of one round (see the top of the file). For the DXT entries the old name's capacity decides
// whether the move-assignment frees it: a heap block (both sides their own, same contents) on odd rounds.
struct FormatStart {
    std::vector<uint8_t> window;
    uint32_t heapCapacity = 0; // 0: inline (capacity 15)
};

FormatStart MakeStart(const staticinit::PixelFormatStatic &e, uint32_t size, int round, std::mt19937 &rng)
{
    FormatStart s;
    s.window.resize(size + 2 * kMargin);
    for (auto &x : s.window) x = static_cast<uint8_t>(rng());
    FormatClass cls = ClassOf(e);
    uint8_t *name = s.window.data() + kMargin + 8;
    if (cls == FormatClass::Unknown || (cls == FormatClass::DXT && round % 2 == 0)) {
        uint32_t cap = 15; // the destructor frees a capacity above 15: the unknown format's is 15 in .data
        std::memcpy(name + 0x14, &cap, 4);
    } else if (cls == FormatClass::DXT) {
        s.heapCapacity = 16 + rng() % 200;
        std::memcpy(name + 0x14, &s.heapCapacity, 4);
    }
    return s;
}

void LoadStart(const difftest::Context &ctx, uint32_t storage, const FormatStart &s)
{
    Put(ctx, storage, s.window);
    if (s.heapCapacity) {
        auto *block = static_cast<char *>(::operator new(s.heapCapacity + 1));
        std::memset(block, 'q', s.heapCapacity + 1);
        std::memcpy(ctx.image.At<uint8_t>(storage + 8), &block, 4);
        uint32_t size = s.heapCapacity / 2;
        std::memcpy(ctx.image.At<uint8_t>(storage + 8 + 0x10), &size, 4);
    }
}

struct FormatRun {
    FormatShot built, destroyed;
    size_t registrations = 0;
    std::vector<size_t> freedByInit, freedByThunk;
};

template <typename Init>
FormatRun RunFormat(const difftest::Context &ctx, const staticinit::PixelFormatStatic &e, uint32_t size, Init init)
{
    FormatClass cls = ClassOf(e);
    bool constructed = cls != FormatClass::DXT && cls != FormatClass::Unknown;
    bool semantic = cls != FormatClass::Unknown;
    FormatRun r;
    gRegistered.clear();
    {
        difftest::FreeRecorder rec;
        init();
        rec.Stop();
        r.freedByInit = Sizes(rec);
    }
    r.built = TakeFormat(ctx, e.storage, size, constructed ? VTableCheck::Class : VTableCheck::Raw, semantic);
    r.registrations = gRegistered.size();
    if (r.registrations == 1) {
        difftest::FreeRecorder rec;
        gRegistered[0]();
        rec.Stop();
        r.freedByThunk = Sizes(rec);
        r.destroyed = TakeFormat(ctx, e.storage, size, VTableCheck::BaseT, true);
    }
    return r;
}

} // namespace

DIFF_TEST(StaticInit_PixelFormats, 0x137ca0)
{
    AtExitCapture capture(ctx);
    size_t n = 0;
    const staticinit::PixelFormatStatic *entries = staticinit::PixelFormatStatics(&n);
    if (n != 57) ctx.Fail("%u pixel format entries, expected 57 (slots 19-75)", static_cast<unsigned>(n));
    std::mt19937 rng(19);
    for (size_t i = 0; i < n; ++i) {
        const staticinit::PixelFormatStatic &e = entries[i];
        if (!SlotHolds(ctx, e.slot, e.imageRva)) continue;
        FormatClass cls = ClassOf(e);
        uint32_t size = SizeOf(cls);
        const std::vector<uint8_t> saved = Window(ctx, e.storage, size);
        auto image = ctx.Original<void(__cdecl *)()>(e.imageRva);
        for (int round = 0; round < 4; ++round) {
            FormatStart start = MakeStart(e, size, round, rng);
            LoadStart(ctx, e.storage, start);
            FormatRun a = RunFormat(ctx, e, size, [&] { image(); });
            LoadStart(ctx, e.storage, start);
            FormatRun b = RunFormat(ctx, e, size, [&] { staticinit::InitPixelFormat(&e); });
            ++ctx.cases;
            std::string why;
            if (a.registrations != 1 || b.registrations != 1) {
                ctx.Fail("slot %u: %u registrations by the image's entry, %u by ours", e.slot,
                         static_cast<unsigned>(a.registrations), static_cast<unsigned>(b.registrations));
                continue;
            }
            bool constructed = cls != FormatClass::DXT && cls != FormatClass::Unknown;
            if (!SameFormat(ctx, cls, constructed ? VTableCheck::Class : VTableCheck::Raw, a.built, b.built, why))
                ctx.Fail("slot %u round %d: built object differs (%s)", e.slot, round, why.c_str());
            if (!SameFormat(ctx, cls, VTableCheck::BaseT, a.destroyed, b.destroyed, why))
                ctx.Fail("slot %u round %d: destroyed object differs (%s)", e.slot, round, why.c_str());
            if (a.freedByInit != b.freedByInit) ctx.Fail("slot %u round %d: the initialisers free different blocks", e.slot, round);
            if (a.freedByThunk != b.freedByThunk) ctx.Fail("slot %u round %d: the destructors free different blocks", e.slot, round);
            if (start.heapCapacity && b.freedByInit.size() != 1) ctx.Fail("slot %u round %d: the old heap name was not freed", e.slot, round);
        }
        Put(ctx, e.storage, saved);
    }
}

// ---- cRZCriticalSection ----

namespace {

struct LockRun {
    std::vector<uint8_t> built, destroyed; // the windows, the block pointer cleared
    int32_t lockCount = 0;
    LONG csLockCount = 0, csRecursion = 0;
    HANDLE csOwner = nullptr;
    bool blockKept = false; // the destructor left the block pointer as the constructor set it
    size_t registrations = 0;
    std::vector<size_t> freed;
};

template <typename Init> LockRun RunLock(const difftest::Context &ctx, const staticinit::LockStatic &e, Init init)
{
    LockRun r;
    gRegistered.clear();
    init();
    r.built = Window(ctx, e.storage, 8);
    void *block;
    std::memcpy(&block, r.built.data() + kMargin + 4, 4);
    std::memset(r.built.data() + kMargin + 4, 0, 4);
    if (block) {
        auto *cs = static_cast<CRITICAL_SECTION *>(block);
        r.lockCount = *reinterpret_cast<int32_t *>(static_cast<uint8_t *>(block) + 0x18);
        r.csLockCount = cs->LockCount;
        r.csRecursion = cs->RecursionCount;
        r.csOwner = cs->OwningThread;
    }
    r.registrations = gRegistered.size();
    if (r.registrations == 1) {
        difftest::FreeRecorder rec;
        gRegistered[0]();
        rec.Stop();
        r.freed = Sizes(rec);
        r.blockKept = rec.Freed(block);
        r.destroyed = Window(ctx, e.storage, 8);
        void *after;
        std::memcpy(&after, r.destroyed.data() + kMargin + 4, 4);
        r.blockKept = r.blockKept && after == block;
        std::memset(r.destroyed.data() + kMargin + 4, 0, 4);
    }
    return r;
}

} // namespace

DIFF_TEST(StaticInit_Locks, 0x138342)
{
    AtExitCapture capture(ctx);
    size_t n = 0;
    const staticinit::LockStatic *entries = staticinit::LockStatics(&n);
    if (n != 10) ctx.Fail("%u lock entries, expected 10", static_cast<unsigned>(n));
    std::mt19937 rng(2);
    for (size_t i = 0; i < n; ++i) {
        const staticinit::LockStatic &e = entries[i];
        if (!SlotHolds(ctx, e.slot, e.imageRva)) continue;
        const std::vector<uint8_t> saved = Window(ctx, e.storage, 8);
        auto image = ctx.Original<void(__cdecl *)()>(e.imageRva);
        for (int round = 0; round < 4; ++round) {
            std::vector<uint8_t> junk(8 + 2 * kMargin);
            for (auto &x : junk) x = static_cast<uint8_t>(rng());
            Put(ctx, e.storage, junk);
            LockRun a = RunLock(ctx, e, [&] { image(); });
            Put(ctx, e.storage, junk);
            LockRun b = RunLock(ctx, e, [&] { staticinit::InitLock(&e); });
            ++ctx.cases;
            if (a.registrations != 1 || b.registrations != 1) {
                ctx.Fail("slot %u: %u registrations by the image's entry, %u by ours", e.slot,
                         static_cast<unsigned>(a.registrations), static_cast<unsigned>(b.registrations));
                continue;
            }
            if (a.built != b.built || a.lockCount != b.lockCount || a.csLockCount != b.csLockCount ||
                a.csRecursion != b.csRecursion || a.csOwner != b.csOwner)
                ctx.Fail("slot %u round %d: built lock differs", e.slot, round);
            if (a.destroyed != b.destroyed || a.blockKept != b.blockKept || !a.blockKept)
                ctx.Fail("slot %u round %d: destroyed lock differs", e.slot, round);
            if (a.freed != b.freed) ctx.Fail("slot %u round %d: the destructors free different blocks", e.slot, round);
        }
        Put(ctx, e.storage, saved);
    }
}

// ---- cRZRandom ----

namespace {

uint32_t gTime = 0;
int gOverrideCalls = 0, gOverrideKind = -1;
uint32_t gOverrideSite = 0;
uint64_t __cdecl StubTime64(void *) { return gTime; }
long __cdecl StubClock() { return 0; }
uint32_t SeedOverride(int kind, uint32_t site)
{
    ++gOverrideCalls;
    gOverrideKind = kind;
    gOverrideSite = site;
    return gTime;
}

} // namespace

DIFF_TEST(StaticInit_Randoms, 0x1382d8)
{
    AtExitCapture capture(ctx);
    Redirect time(ctx, kTime64, reinterpret_cast<const void *>(&StubTime64));
    Redirect clock(ctx, kClock, reinterpret_cast<const void *>(&StubClock));
    auto savedOverride = gRZRandomClockSeedOverride;
    gRZRandomClockSeedOverride = &SeedOverride;
    size_t n = 0;
    const staticinit::RandomStatic *entries = staticinit::RandomStatics(&n);
    if (n != 5) ctx.Fail("%u random entries, expected 5", static_cast<unsigned>(n));
    std::mt19937 rng(95);
    for (size_t i = 0; i < n; ++i) {
        const staticinit::RandomStatic &e = entries[i];
        if (!SlotHolds(ctx, e.slot, e.imageRva)) continue;
        // Each image entry is `push -1; mov ecx, object; call 0x1b8bc8; ret`: the call returns 12 bytes in.
        if (e.callSite != e.imageRva + 12) ctx.Fail("slot %u: call site %08x, expected %08x", e.slot, e.callSite, e.imageRva + 12);
        const std::vector<uint8_t> saved = Window(ctx, e.storage, 12);
        auto image = ctx.Original<void(__cdecl *)()>(e.imageRva);
        for (int round = 0; round < 6; ++round) {
            std::vector<uint8_t> junk(12 + 2 * kMargin);
            for (auto &x : junk) x = static_cast<uint8_t>(rng());
            const uint32_t edges[] = {0u, 1u, 0xfffffffeu, 0x80000000u};
            gTime = round < 4 ? edges[round] : static_cast<uint32_t>(rng());
            gRegistered.clear();
            Put(ctx, e.storage, junk);
            image();
            std::vector<uint8_t> a = Window(ctx, e.storage, 12);
            Put(ctx, e.storage, junk);
            gOverrideCalls = 0;
            staticinit::InitRandom(&e);
            std::vector<uint8_t> b = Window(ctx, e.storage, 12);
            ++ctx.cases;
            if (a != b) ctx.Fail("slot %u round %d: generator differs", e.slot, round);
            if (gOverrideCalls != 1 || gOverrideKind != 0 || gOverrideSite != e.callSite)
                ctx.Fail("slot %u: seed control saw %d calls, kind %d, site %08x", e.slot, gOverrideCalls, gOverrideKind, gOverrideSite);
            if (!gRegistered.empty()) ctx.Fail("slot %u: a destructor was registered", e.slot);
        }
        Put(ctx, e.storage, saved);
    }
    gRZRandomClockSeedOverride = savedOverride;
}

// ---- values ----

namespace {

uint32_t TargetSize(const staticinit::ValueStatic &e)
{
    using staticinit::ValueOp;
    switch (e.op) {
    case ValueOp::Copy:
    case ValueOp::Negate: return 4 * e.constant;
    case ValueOp::CopyAndNegateTriples: return 24 * e.constant;
    case ValueOp::Box: return 0x24;
    default: return 4;
    }
}

// What the image's entry reads: nothing (constants, an address), a string pointer (kept: it points into .rdata),
// or a source block (junk-filled, so every load shows).
uint32_t SourceSize(const staticinit::ValueStatic &e)
{
    using staticinit::ValueOp;
    switch (e.op) {
    case ValueOp::Copy:
    case ValueOp::Negate: return 4 * e.constant;
    case ValueOp::CopyAndNegateTriples: return 12 * e.constant;
    case ValueOp::ScaleBySqrt2: return 4;
    case ValueOp::Box: return 0x24;
    default: return 0;
    }
}

} // namespace

DIFF_TEST(StaticInit_Values, 0x13cd08)
{
    AtExitCapture capture(ctx);
    size_t n = 0;
    const staticinit::ValueStatic *entries = staticinit::ValueStatics(&n);
    if (n != 25) ctx.Fail("%u value entries, expected 25 (the kind's 26 but slot 104)", static_cast<unsigned>(n));
    std::mt19937 rng(26);
    for (size_t i = 0; i < n; ++i) {
        const staticinit::ValueStatic &e = entries[i];
        if (!SlotHolds(ctx, e.slot, e.imageRva)) continue;
        const uint32_t tsize = TargetSize(e), ssize = SourceSize(e);
        const std::vector<uint8_t> savedTarget = Window(ctx, e.target, tsize);
        const std::vector<uint8_t> savedSource = ssize ? Window(ctx, e.source, ssize) : std::vector<uint8_t>();
        auto image = ctx.Original<void(__cdecl *)()>(e.imageRva);
        for (int round = 0; round < 8; ++round) {
            // The target's window, then the source's (they may touch: slots 364 and 799 read the dword before).
            std::vector<uint8_t> t(tsize + 2 * kMargin);
            for (auto &x : t) x = static_cast<uint8_t>(rng());
            Put(ctx, e.target, t);
            if (ssize) {
                std::vector<uint8_t> s(ssize + 2 * kMargin);
                for (size_t k = 0; k < s.size(); k += 4) {
                    // Floats on some rounds (finite products), raw junk on the others.
                    float f = static_cast<float>(static_cast<int32_t>(rng())) / 65536.0f;
                    uint32_t v = round % 2 ? static_cast<uint32_t>(rng()) : 0;
                    if (round % 2 == 0) std::memcpy(&v, &f, 4);
                    std::memcpy(s.data() + k, &v, std::min<size_t>(4, s.size() - k));
                }
                Put(ctx, e.source, s);
            }
            const std::vector<uint8_t> startT = Window(ctx, e.target, tsize);
            const std::vector<uint8_t> startS = ssize ? Window(ctx, e.source, ssize) : std::vector<uint8_t>();
            gRegistered.clear();
            image();
            std::vector<uint8_t> aT = Window(ctx, e.target, tsize);
            std::vector<uint8_t> aS = ssize ? Window(ctx, e.source, ssize) : std::vector<uint8_t>();
            Put(ctx, e.target, startT);
            if (ssize) Put(ctx, e.source, startS);
            staticinit::InitValue(&e);
            std::vector<uint8_t> bT = Window(ctx, e.target, tsize);
            std::vector<uint8_t> bS = ssize ? Window(ctx, e.source, ssize) : std::vector<uint8_t>();
            ++ctx.cases;
            if (aT != bT || aS != bS) ctx.Fail("slot %u round %d: value differs", e.slot, round);
            if (aT == startT) ctx.Fail("slot %u round %d: the image's entry wrote nothing in the window", e.slot, round);
            if (!gRegistered.empty()) ctx.Fail("slot %u: a destructor was registered", e.slot);
        }
        if (ssize) Put(ctx, e.source, savedSource);
        Put(ctx, e.target, savedTarget);
    }
}

// ---- std::string constants ----
//
// Start state: the string and 8 bytes on each side junk, except a Characters entry's capacity, which .data holds
// at 15 (the entry does not write it, and the destructor would free a larger one). Compared byte for byte after the
// entry, except a Heap string's pointer (each side its own block: compared by its size + 1 characters), and after
// the thunk, except bytes 1-3 of a freed Heap pointer: the destructor writes only the first byte of the union
// (the terminator; e.g. the pixel format thunk's `mov byte ptr [name], 0` 0xd8fb08, the same inlined
// _Tidy_deallocate), so the rest of the old pointer stays, and it differs between the sides.

DIFF_TEST(StaticInit_Strings, 0x1384bb)
{
    AtExitCapture capture(ctx);
    size_t n = 0;
    const staticinit::StringStatic *entries = staticinit::StringStatics(&n);
    if (n != 85) ctx.Fail("%u string entries, expected 85", static_cast<unsigned>(n));
    std::mt19937 rng(85);
    for (size_t i = 0; i < n; ++i) {
        const staticinit::StringStatic &e = entries[i];
        if (!SlotHolds(ctx, e.slot, e.imageRva)) continue;
        const bool heap = e.form == staticinit::StringForm::Heap;
        const std::vector<uint8_t> saved = Window(ctx, e.storage, 0x18);
        auto image = ctx.Original<void(__cdecl *)()>(e.imageRva);
        for (int round = 0; round < 4; ++round) {
            std::vector<uint8_t> junk(0x18 + 2 * kMargin);
            for (auto &x : junk) x = static_cast<uint8_t>(rng());
            if (e.form == staticinit::StringForm::Characters) {
                uint32_t cap = 15;
                std::memcpy(junk.data() + kMargin + 0x14, &cap, 4);
            }
            struct Run {
                std::vector<uint8_t> built, destroyed;
                std::string heapText;
                size_t registrations = 0;
                std::vector<size_t> freed;
            };
            auto run = [&](auto init) {
                Run r;
                Put(ctx, e.storage, junk);
                gRegistered.clear();
                init();
                r.built = Window(ctx, e.storage, 0x18);
                if (heap) {
                    const char *p = *ctx.image.At<const char *>(e.storage);
                    r.heapText.assign(p, e.size + 1);
                    std::memset(r.built.data() + kMargin, 0, 4);
                }
                r.registrations = gRegistered.size();
                if (r.registrations == 1) {
                    difftest::FreeRecorder rec;
                    gRegistered[0]();
                    rec.Stop();
                    r.freed = Sizes(rec);
                    r.destroyed = Window(ctx, e.storage, 0x18);
                    if (heap) std::memset(r.destroyed.data() + kMargin + 1, 0, 3);
                }
                return r;
            };
            Run a = run([&] { image(); });
            Run b = run([&] { staticinit::InitString(&e); });
            ++ctx.cases;
            if (a.registrations != 1 || b.registrations != 1) {
                ctx.Fail("slot %u: %u registrations by the image's entry, %u by ours", e.slot,
                         static_cast<unsigned>(a.registrations), static_cast<unsigned>(b.registrations));
                continue;
            }
            if (a.built != b.built || a.heapText != b.heapText) ctx.Fail("slot %u round %d: built string differs", e.slot, round);
            if (a.destroyed != b.destroyed) ctx.Fail("slot %u round %d: destroyed string differs", e.slot, round);
            if (a.freed != b.freed || (heap && a.freed.size() != 1)) ctx.Fail("slot %u round %d: the destructors free different blocks", e.slot, round);
        }
        Put(ctx, e.storage, saved);
    }
}

// ---- the skip list ----

// Every kind's entries: one per slot, in slot order, each slot holding the image's initialiser our table names.
DIFF_TEST(StaticInit_OwnedList, 0x13d9ab)
{
    size_t total = 0, keys = 0, objects = 0, values = 0, strings = 0, rzStrings = 0;
    const winmainstart::OwnedInitialiser *all = staticinit::OwnedInitialisers(&total);
    staticinit::OwnedNameKeys(&keys);
    staticinit::OwnedObjects(&objects);
    staticinit::OwnedValues(&values);
    staticinit::OwnedStrings(&strings);
    size_t anys = 0; // lane GZ: the cIGZAny statics (tests/static_init_any.cpp)
    staticinit::OwnedAnys(&anys);
    staticinit::OwnedRZStrings(&rzStrings);
    size_t atExits = 0; // lane HC: the atexit-only entries (tests/static_init_atexit.cpp)
    staticinit::OwnedAtExits(&atExits);
    size_t constructed = 0; // lane HD: the object entries (tests/static_init_constructed.cpp)
    staticinit::OwnedConstructed(&constructed);
    size_t hf = 0; // lane HF: containers, registrations, computed, other, value slot 104 (tests/static_init_containers.cpp)
    staticinit::OwnedHF(&hf);
    size_t allocators = 0; // lane HE: the pool allocators (tests/static_init_allocators.cpp)
    staticinit::OwnedAllocators(&allocators);
    size_t parsers = 0; // lane HP: slots 675 and 773 (tests/static_init_parser.cpp)
    staticinit::OwnedParsers(&parsers);
    ++ctx.cases;
    if (parsers != 2) ctx.Fail("owned: %u lane HP entries (2)", static_cast<unsigned>(parsers));
    size_t h5 = 0; // lane HQ: package H5, and slot 98 from lane II (tests/static_init_h5.cpp)
    staticinit::OwnedH5(&h5);
    ++ctx.cases;
    if (h5 != 11) ctx.Fail("owned: %u lane HQ / II entries (11)", static_cast<unsigned>(h5));
    size_t ht = 0; // lane HT: the last image-run entries but the CRT's (tests/static_init_ht.cpp)
    staticinit::OwnedHT(&ht);
    ++ctx.cases;
    if (ht != 25) ctx.Fail("owned: %u lane HT entries (25)", static_cast<unsigned>(ht));
    if (constructed != 97) ctx.Fail("owned: %u object entries (97)", static_cast<unsigned>(constructed));
    if (anys != 36) ctx.Fail("owned: %u cIGZAny statics (36)", static_cast<unsigned>(anys));
    if (atExits != 109) ctx.Fail("owned: %u atexit-only entries (109)", static_cast<unsigned>(atExits));
    if (allocators != 50) ctx.Fail("owned: %u allocator entries (50)", static_cast<unsigned>(allocators));
    if (hf != 36) ctx.Fail("owned: %u lane HF entries (36)", static_cast<unsigned>(hf));
    if (keys != 111 || objects != 72 || values != 25 || strings != 85 || rzStrings != 135 ||
        total != keys + objects + values + strings + anys + rzStrings + atExits + constructed + hf + allocators + parsers + h5 + ht || total != 794)
        ctx.Fail("owned: %u in all (794), %u keys (111), %u objects (72), %u values (25), %u strings (85), %u cRZStrings (135)",
                 static_cast<unsigned>(total), static_cast<unsigned>(keys), static_cast<unsigned>(objects),
                 static_cast<unsigned>(values), static_cast<unsigned>(strings), static_cast<unsigned>(rzStrings));
    const uint32_t slots = (kXcLast - kXcFirst) / 4;
    for (size_t i = 0; i < total; ++i) {
        ++ctx.cases;
        if (i && all[i - 1].slot >= all[i].slot) ctx.Fail("owned list: slot %u after %u", all[i].slot, all[i - 1].slot);
        if (all[i].slot >= slots) ctx.Fail("owned list: slot %u out of the table", all[i].slot);
        else SlotHolds(ctx, all[i].slot, all[i].imageRva);
    }
}

// The runner on a copy of the image's XC table with only the object slots kept: ours runs in each, the same number
// of destructors is registered in the same order of slots, and running each side's list LIFO (as the CRT's exit
// does) frees the same blocks.
DIFF_TEST(StaticInit_ObjectsOnImageTable, 0x137ca0)
{
    AtExitCapture capture(ctx);
    Redirect time(ctx, kTime64, reinterpret_cast<const void *>(&StubTime64));
    Redirect clock(ctx, kClock, reinterpret_cast<const void *>(&StubClock));
    auto savedOverride = gRZRandomClockSeedOverride;
    gRZRandomClockSeedOverride = &SeedOverride;
    gTime = 0x12345678;

    std::vector<winmainstart::OwnedInitialiser> ownedList;
    for (auto kind : {&staticinit::OwnedObjects, &staticinit::OwnedValues, &staticinit::OwnedStrings, &staticinit::OwnedRZStrings}) {
        size_t k = 0;
        const winmainstart::OwnedInitialiser *p = kind(&k);
        ownedList.insert(ownedList.end(), p, p + k);
    }
    const size_t ownedCount = ownedList.size();
    const winmainstart::OwnedInitialiser *owned = ownedList.data();
    const uint32_t slots = (kXcLast - kXcFirst) / 4;
    std::vector<winmainstart::Initialiser> table(slots, nullptr);
    for (size_t i = 0; i < ownedCount; ++i) {
        if (owned[i].slot < slots) table[owned[i].slot] = ctx.image.At<winmainstart::Initialiser>(kXcFirst)[owned[i].slot];
    }
    // The objects' windows, kept to restore.
    struct Saved { uint32_t storage; std::vector<uint8_t> bytes; };
    std::vector<Saved> saved;
    size_t n = 0;
    for (const auto *p = staticinit::PixelFormatStatics(&n), *end = p + n; p != end; ++p)
        saved.push_back({p->storage, Window(ctx, p->storage, SizeOf(ClassOf(*p)))});
    for (const auto *p = staticinit::LockStatics(&n), *end = p + n; p != end; ++p) saved.push_back({p->storage, Window(ctx, p->storage, 8)});
    for (const auto *p = staticinit::RandomStatics(&n), *end = p + n; p != end; ++p) saved.push_back({p->storage, Window(ctx, p->storage, 12)});
    for (const auto *p = staticinit::ValueStatics(&n), *end = p + n; p != end; ++p) saved.push_back({p->target, Window(ctx, p->target, TargetSize(*p))});
    for (const auto *p = staticinit::StringStatics(&n), *end = p + n; p != end; ++p) saved.push_back({p->storage, Window(ctx, p->storage, 0x18)});
    for (const auto *p = staticinit::RZStringStatics(&n), *end = p + n; p != end; ++p) {
        saved.push_back({p->storage, Window(ctx, p->storage, p->count * p->stride)});
        if (p->alias) saved.push_back({p->alias, Window(ctx, p->alias, 4)});
    }
    // The constant-initialised formats start from the file's state (empty or "UNKNOWN"-sized names, capacity 15).
    auto restore = [&] {
        for (const Saved &s : saved) Put(ctx, s.storage, s.bytes);
        for (const auto *p = staticinit::PixelFormatStatics(&n), *end = p + n; p != end; ++p) {
            if (ClassOf(*p) != FormatClass::DXT && ClassOf(*p) != FormatClass::Unknown) continue;
            uint32_t size = ClassOf(*p) == FormatClass::Unknown ? 7 : 0, cap = 15;
            std::memcpy(ctx.image.At<uint8_t>(p->storage + 8 + 0x10), &size, 4);
            std::memcpy(ctx.image.At<uint8_t>(p->storage + 8 + 0x14), &cap, 4);
        }
    };

    auto lifo = [&](std::vector<size_t> &freed) {
        difftest::FreeRecorder rec;
        for (auto it = gRegistered.rbegin(); it != gRegistered.rend(); ++it) (*it)();
        rec.Stop();
        freed = Sizes(rec);
    };

    restore();
    gRegistered.clear();
    for (uint32_t s = 0; s < slots; ++s) {
        if (table[s] != nullptr) table[s]();
    }
    // The values (no pointers in them) after the whole run, compared byte for byte.
    auto valueWindows = [&] {
        std::vector<uint8_t> all;
        for (const auto *p = staticinit::ValueStatics(&n), *end = p + n; p != end; ++p) {
            std::vector<uint8_t> w = Window(ctx, p->target, TargetSize(*p));
            all.insert(all.end(), w.begin(), w.end());
        }
        return all;
    };
    size_t imageRegistrations = gRegistered.size();
    std::vector<uint8_t> valuesA = valueWindows();
    std::vector<size_t> freedA;
    lifo(freedA);

    restore();
    gRegistered.clear();
    winmainstart::XcRunCounts c = winmainstart::RunInitialisersOwned(table.data(), table.data() + slots, ctx.image.Base(),
                                                                      owned, ownedCount);
    size_t ourRegistrations = gRegistered.size();
    std::vector<uint8_t> valuesB = valueWindows();
    std::vector<size_t> freedB;
    lifo(freedB);
    ++ctx.cases;
    if (c.owned != static_cast<int>(ownedCount) || c.image || c.mismatched || c.unused)
        ctx.Fail("counts: %d ours, %d image, %d mismatched, %d unused (owned %u)", c.owned, c.image, c.mismatched, c.unused,
                 static_cast<unsigned>(ownedCount));
    // 57 pixel formats + 10 locks + 85 strings + 135 cRZStrings register a destructor; the randoms and values none.
    if (imageRegistrations != 287 || ourRegistrations != imageRegistrations)
        ctx.Fail("registrations: %u by the image's entries (287), %u by ours", static_cast<unsigned>(imageRegistrations),
                 static_cast<unsigned>(ourRegistrations));
    if (valuesA != valuesB) ctx.Fail("values differ after the run");
    if (freedA != freedB || freedA.size() < 10)
        ctx.Fail("exit list: %u blocks freed after the image's entries, %u after ours (the 10 lock blocks and the long names)",
                 static_cast<unsigned>(freedA.size()), static_cast<unsigned>(freedB.size()));
    for (const Saved &s : saved) Put(ctx, s.storage, s.bytes);
    gRZRandomClockSeedOverride = savedOverride;
}
