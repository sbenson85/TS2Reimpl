// Differential tests for cPixelFormatsManagerDX (src/gz/PixelFormatsManager.*). The original manager is
// built on raw memory by the original constructor over the game's 56 static formats in the mapped image
// (whose objects the original's own static initialisers construct, with the original vtables); ours is
// built by our constructor over our own 56 static formats (StandaloneStore, our vtables). The two worlds
// have different object addresses, so results that are pointers to a format are compared as the format's
// index in that world's table; the vtable pointers are compared through the explicit map in
// pixel_format_vtables.h. The sort and binary search helpers are tested on fake formats whose Less slot
// answers from a rank table and logs every call, so the sequence of comparisons has to match too.
#include "difftest.h"
#include "free_recorder.h"
#include "game/vtable.h"
#include "gz/PixelFormatStore.h"
#include "gz/PixelFormatVTables.h"
#include "gz/PixelFormatsManager.h"
#include "pixel_format_fixture.h"
#include "pixel_format_vtables.h"

#include <windows.h>

#include <algorithm>
#include <climits>
#include <cstring>
#include <random>
#include <vector>

using namespace nGZGraphic4;

namespace {

using Mgr = cPixelFormatsManagerDX;
using CtorFn = void *(__thiscall *)(void *);

uint32_t Pick(std::mt19937 &rng, std::initializer_list<uint32_t> values)
{
    return *(values.begin() + rng() % values.size());
}

// Makes the manager use our own static formats for the duration of a test (the hook tables linked into
// this runner select the image store, as in-game).
struct StoreScope {
    PixelFormatStoreProvider saved;
    explicit StoreScope(PixelFormatStoreProvider provider) : saved(CurrentPixelFormatStoreProvider()) { SetPixelFormatStoreProvider(provider); }
    ~StoreScope() { SetPixelFormatStoreProvider(saved); }
};

// ---------------------------------------------------------------------------------------------------
// The original's 56 static formats

using pftest::EnsureStatics;
using pftest::kOriginalCreateIterator;
using pftest::OriginalTable;
using pftest::OriginalUnknown;

// A format pointer as its position in a world's table: 0..55, kFormatCount for the unknown format,
// -1 for null, -2 for anything else.
int IndexIn(const std::vector<cPixelFormat *> &table, const cPixelFormat *unknown, const void *p)
{
    if (!p) return -1;
    if (p == unknown) return static_cast<int>(kFormatCount);
    for (size_t i = 0; i < table.size(); ++i) {
        if (table[i] == p) return static_cast<int>(i);
    }
    return -2;
}

// Both worlds' tables for index conversion.
struct Worlds {
    std::vector<cPixelFormat *> origTable, oursTable;
    cPixelFormat *origUnknown, *oursUnknown;
    explicit Worlds(const difftest::Context &ctx)
    {
        EnsureStatics(ctx);
        origTable = OriginalTable(ctx);
        origUnknown = OriginalUnknown(ctx);
        const PixelFormatStore &store = StandaloneStore();
        oursTable.assign(store.table, store.table + kFormatCount);
        oursUnknown = store.unknown;
    }
    int Orig(const void *p) const { return IndexIn(origTable, origUnknown, p); }
    int Ours(const void *p) const { return IndexIn(oursTable, oursUnknown, p); }
};

FormatClass ClassOf(StaticKind kind)
{
    switch (kind) {
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

// One format of each world compared: vtables through the map, id and fields byte for byte, name by
// text, size and capacity. Returns an empty string when they agree.
std::string CompareFormats(const difftest::Context &ctx, FormatClass cls, const cPixelFormat *orig, const cPixelFormat *ours)
{
    const uint8_t *a = reinterpret_cast<const uint8_t *>(orig), *b = reinterpret_cast<const uint8_t *>(ours);
    if (!pftest::VTablesCorrespond(ctx, cls, false, pftest::VTableOf(a), pftest::VTableOf(b))) return "vtable";
    if (std::memcmp(a + 4, b + 4, 4) != 0) return "format id";
    uint32_t size = SizeOf(cls);
    if (size > 0x20 && std::memcmp(a + 0x20, b + 0x20, size - 0x20) != 0) return "fields";
    if (orig->mName.size() != ours->mName.size() || orig->mName.capacity() != ours->mName.capacity() ||
        std::memcmp(orig->mName.c_str(), ours->mName.c_str(), orig->mName.size() + 1) != 0)
        return "name \"" + std::string(orig->mName.c_str()) + "\" vs \"" + std::string(ours->mName.c_str()) + "\"";
    return "";
}

uint8_t *NewManager(const std::vector<uint8_t> &image)
{
    auto *p = static_cast<uint8_t *>(::operator new(Mgr::kSize));
    std::memcpy(p, image.data(), Mgr::kSize);
    return p;
}

std::vector<uint8_t> RandomImage(std::mt19937 &rng, size_t size)
{
    std::vector<uint8_t> v(size);
    for (auto &b : v) b = static_cast<uint8_t>(rng());
    return v;
}

// A manager's observable content, with format pointers as table indexes of the manager's own world.
struct MgrState {
    float maxLoad;
    size_t buckets, size;
    std::vector<std::pair<uint32_t, int>> entries; // in iteration order
    std::vector<int> sorted;
    size_t sortedCapacity;
    bool operator==(const MgrState &o) const
    {
        return maxLoad == o.maxLoad && buckets == o.buckets && size == o.size && entries == o.entries && sorted == o.sorted && sortedCapacity == o.sortedCapacity;
    }
};

template <typename IndexFn> MgrState StateOf(const Mgr *m, IndexFn index)
{
    MgrState s;
    s.maxLoad = m->mById.max_load_factor();
    s.buckets = m->mById.bucket_count();
    s.size = m->mById.size();
    for (auto &kv : m->mById) s.entries.emplace_back(kv.first, index(kv.second));
    for (cPixelFormat *f : m->mSorted) s.sorted.push_back(index(f));
    s.sortedCapacity = m->mSorted.capacity();
    return s;
}

Mgr *AsMgr(uint8_t *p) { return reinterpret_cast<Mgr *>(p); }

// A manager of each world, built by its own constructor: the original over the image's formats, ours
// over our own.
struct Pair {
    Worlds w;
    uint8_t *orig, *ours;
    StoreScope scope;
    explicit Pair(difftest::Context &ctx) : w(ctx), scope(&StandaloneStore)
    {
        orig = static_cast<uint8_t *>(::operator new(Mgr::kSize));
        ours = static_cast<uint8_t *>(::operator new(Mgr::kSize));
        std::memset(orig, 0, Mgr::kSize);
        std::memset(ours, 0, Mgr::kSize);
        ctx.Original<CtorFn>(0x15d4b0)(orig);
        AsMgr(ours)->Construct();
    }
    ~Pair()
    {
        AsMgr(orig)->Destruct(); // the destructor body has no world-specific part beyond the vtable it writes
        AsMgr(ours)->Destruct();
        ::operator delete(orig);
        ::operator delete(ours);
    }
};

// The static formats themselves: every one of the 56 (and the unknown format) that our code builds
// equals the one the original's static initialisers built. This is also what ties kStaticFormatSpecs to
// the image.
DIFF_TEST(PixelFormatsManager_StaticFormats, 0x15d4b0)
{
    Worlds w(ctx);
    for (uint32_t i = 0; i < kFormatCount; ++i) {
        FormatClass cls = ClassOf(kStaticFormatSpecs[i].kind);
        ++ctx.cases;
        std::string diff = CompareFormats(ctx, cls, w.origTable[i], w.oursTable[i]);
        if (!diff.empty()) ctx.Fail("static format %u (id %08x, spec kind %d): %s differs", i, kStaticFormatSpecs[i].id, int(kStaticFormatSpecs[i].kind), diff.c_str());
    }
    ++ctx.cases;
    std::string diff = CompareFormats(ctx, FormatClass::Unknown, w.origUnknown, w.oursUnknown);
    if (!diff.empty()) ctx.Fail("static unknown format: %s differs", diff.c_str());
    // The image's table agrees with the order of our specs, object by object.
    for (uint32_t i = 0; i < kFormatCount; ++i) {
        uint32_t id;
        std::memcpy(&id, reinterpret_cast<const uint8_t *>(w.origTable[i]) + 4, 4);
        ++ctx.cases;
        if (id != kStaticFormatSpecs[i].id) ctx.Fail("table entry %u has id %08x in the image, %08x in kStaticFormatSpecs", i, id, kStaticFormatSpecs[i].id);
    }
}

DIFF_TEST(PixelFormatsManager_Construct, 0x15d4b0)
{
    std::mt19937 rng(0x15d4b0);
    Worlds w(ctx);
    StoreScope scope(&StandaloneStore);
    for (int i = 0; i < 40; ++i) {
        std::vector<uint8_t> image = RandomImage(rng, Mgr::kSize);
        uint8_t *a = NewManager(image), *b = NewManager(image);
        void *ra = ctx.Original<CtorFn>(0x15d4b0)(a);
        void *rb = AsMgr(b)->Construct();
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("Construct: return value");
        if (!pftest::VTablesCorrespond(ctx, pftest::ManagerEntry(), pftest::VTableOf(a), pftest::VTableOf(b))) ctx.Fail("Construct: manager vtable is not the mapped pair");
        MgrState sa = StateOf(AsMgr(a), [&](const void *p) { return w.Orig(p); });
        MgrState sb = StateOf(AsMgr(b), [&](const void *p) { return w.Ours(p); });
        if (!(sa == sb)) ctx.Fail("Construct: state differs (size %zu/%zu, buckets %zu/%zu, order equal %d)", sa.size, sb.size, sa.buckets, sb.buckets, sa.sorted == sb.sorted);
        if (sa.sorted.size() != Mgr::kFormatCount) ctx.Fail("Construct: %zu sorted entries", sa.sorted.size());
        for (int idx : sb.sorted) {
            if (idx < 0 || idx >= int(kFormatCount)) ctx.Fail("Construct: sorted entry %d is not one of our 56 formats", idx);
        }
        // Bytes outside the vtable, the map and the vector (the object is 0x30 bytes, all of it covered): none.
        AsMgr(a)->Destruct();
        AsMgr(b)->Destruct();
        ::operator delete(a);
        ::operator delete(b);
    }
}

// The getter 0x15eb10 (lane RB). The original with its static's guard set to "initialised" (INT_MIN: the thread-safe
// static's fast path) hands out its static 0x10ca018, every time; what that static holds is what the original constructor
// builds on it, so ours is compared with a manager the original constructor builds: ours hands out one object, every
// time, not the image's, with our vtable and the same contents (formats as table indexes of their world: our instance
// is built over our own store).
DIFF_TEST(PixelFormatsManager_Instance, 0x15eb10)
{
    constexpr uint32_t kStatic = 0x10ca018, kGuard = 0x10ca048, kImageVTable = 0xd9d0c4;
    using Getter = void *(__cdecl *)();
    Worlds w(ctx);
    int32_t *const guard = ctx.image.At<int32_t>(kGuard);
    const int32_t savedGuard = *guard;
    *guard = INT_MIN;
    const Getter getter = ctx.Original<Getter>(0x15eb10);
    void *const a1 = getter(), *const a2 = getter();
    *guard = savedGuard;
    ++ctx.cases;
    if (a1 != a2 || a1 != static_cast<void *>(ctx.image.At<uint8_t>(kStatic))) ctx.Fail("original getter: %p %p", a1, a2);

    uint8_t *const orig = static_cast<uint8_t *>(::operator new(Mgr::kSize));
    std::memset(orig, 0, Mgr::kSize);
    ctx.Original<CtorFn>(0x15d4b0)(orig);
    // Built (on the first call in this runner) over our own formats: the image store would adopt the image's DXT
    // objects, which PixelFormatsManager_ImageStore expects to find as the image left them.
    Mgr *ours;
    {
        StoreScope scope(&StandaloneStore);
        ours = Mgr::Instance();
    }
    ++ctx.cases;
    if (ours != Mgr::Instance()) ctx.Fail("Instance: a different object on the second call");
    if (reinterpret_cast<uint8_t *>(ours) == ctx.image.At<uint8_t>(kStatic)) ctx.Fail("Instance: the image's static");
    if (ours->mVTable != ManagerVTable() || ours->mVTable == static_cast<const void *>(ctx.image.At<uint8_t>(kImageVTable))) ctx.Fail("Instance: not our vtable");
    const MgrState sa = StateOf(AsMgr(orig), [&](const void *p) { return w.Orig(p); });
    const MgrState sb = StateOf(ours, [&](const void *p) {
        const int i = w.Orig(p);
        return i >= 0 ? i : w.Ours(p);
    });
    if (!(sa == sb))
        ctx.Fail("Instance: contents differ (size %zu/%zu, buckets %zu/%zu, order equal %d)", sa.size, sb.size, sa.buckets,
                 sb.buckets, sa.sorted == sb.sorted);
    AsMgr(orig)->Destruct();
    ::operator delete(orig);
}

DIFF_TEST(PixelFormatsManager_Destruct, 0x15db70)
{
    std::mt19937 rng(0x15db70);
    Worlds w(ctx);
    StoreScope scope(&StandaloneStore);
    for (int i = 0; i < 40; ++i) {
        std::vector<uint8_t> image = RandomImage(rng, Mgr::kSize);
        uint8_t *a = NewManager(image), *b = NewManager(image);
        ctx.Original<CtorFn>(0x15d4b0)(a);
        AsMgr(b)->Construct();
        // The destructor writes the vtable pointer itself (derived classes run it as a base part).
        const uint32_t scribble = 0xdeadbeef;
        std::memcpy(a, &scribble, 4);
        std::memcpy(b, &scribble, 4);
        // The blocks the destructor frees: the vector's buffer and the map's list head and bucket vector.
        auto blocks = [](uint8_t *m) {
            std::vector<void *> v;
            v.push_back(*reinterpret_cast<void **>(m + 0x24));
            v.push_back(*reinterpret_cast<void **>(m + 8));    // list head
            v.push_back(*reinterpret_cast<void **>(m + 0x10)); // bucket vector
            return v;
        };
        std::vector<void *> blocksA = blocks(a), blocksB = blocks(b);
        difftest::FreeRecorder recA;
        ctx.Original<void(__thiscall *)(void *)>(0x15db70)(a);
        recA.Stop();
        bool freedA = true;
        for (void *p : blocksA) freedA = freedA && recA.Freed(p);
        difftest::FreeRecorder recB;
        AsMgr(b)->Destruct();
        recB.Stop();
        bool freedB = true;
        for (void *p : blocksB) freedB = freedB && recB.Freed(p);
        ++ctx.cases;
        if (!freedA || !freedB) ctx.Fail("Destruct: blocks freed original %d ours %d", freedA, freedB);
        if (!pftest::VTablesCorrespond(ctx, pftest::ManagerEntry(), pftest::VTableOf(a), pftest::VTableOf(b))) ctx.Fail("Destruct: vtable is not the mapped pair");
        // The vector is emptied (all three pointers zero).
        if (std::memcmp(a + 0x24, b + 0x24, 12) != 0 || *reinterpret_cast<uint32_t *>(b + 0x24) != 0) ctx.Fail("Destruct: vector pointers not cleared");
        ::operator delete(a);
        ::operator delete(b);
    }
}

// Fake vtable with a recording AddRef for QueryInterface.
int gAddRefs = 0;
uint32_t __fastcall FakeAddRef(void *, void *)
{
    ++gAddRefs;
    return 7;
}
uint32_t __fastcall FakeOther(void *, void *) { return 0; }

DIFF_TEST(PixelFormatsManager_QueryInterface, 0x15fc70)
{
    using QiFn = uint32_t(__thiscall *)(void *, uint32_t, void **);
    void *vtable[13];
    for (auto &s : vtable) s = reinterpret_cast<void *>(&FakeOther);
    vtable[1] = reinterpret_cast<void *>(&FakeAddRef);
    std::mt19937 rng(0x15fc70);
    for (int i = 0; i < 3000; ++i) {
        uint32_t riid = i % 3 == 0 ? Mgr::kSize * 0 + (i % 2 ? 1u : kIidPixelFormatsManager) : (i % 3 == 1 ? kIidPixelFormatsManager ^ (1u << (rng() % 32)) : static_cast<uint32_t>(rng()));
        bool nullOut = i % 5 == 0;
        uint8_t a[Mgr::kSize], b[Mgr::kSize];
        const void *vt = vtable;
        std::memcpy(a, &vt, 4);
        std::memcpy(b, &vt, 4);
        void *outA = reinterpret_cast<void *>(0x1234), *outB = outA;
        gAddRefs = 0;
        uint8_t ra = static_cast<uint8_t>(ctx.Original<QiFn>(0x15fc70)(a, riid, nullOut ? nullptr : &outA));
        int refsA = gAddRefs;
        gAddRefs = 0;
        uint8_t rb = AsMgr(b)->QueryInterface(riid, nullOut ? nullptr : &outB) ? 1 : 0;
        int refsB = gAddRefs;
        ++ctx.cases;
        if (ra != rb || refsA != refsB) ctx.Fail("Manager QI(%08x): result %u/%u, AddRef %d/%d", riid, ra, rb, refsA, refsB);
        if ((outA == a) != (outB == b) || (outA != reinterpret_cast<void *>(0x1234)) != (outB != reinterpret_cast<void *>(0x1234))) ctx.Fail("Manager QI(%08x): out pointer", riid);
    }
}

DIFF_TEST(PixelFormatsManager_GetFormat, 0x15e980)
{
    Pair m(ctx);
    auto orig = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x15e980);
    std::mt19937 rng(0x15e980);
    std::vector<uint32_t> ids;
    for (auto &kv : AsMgr(m.orig)->mById) ids.push_back(kv.first);
    for (int i = 0; i < 6000; ++i) {
        uint32_t id = i % 2 == 0 ? ids[rng() % ids.size()] : (i % 4 == 1 ? ids[rng() % ids.size()] + static_cast<uint32_t>(rng() % 3) : static_cast<uint32_t>(rng()));
        if (i < 5) id = i;
        int ra = m.w.Orig(orig(m.orig, id));
        int rb = m.w.Ours(AsMgr(m.ours)->GetFormat(id));
        ++ctx.cases;
        if (ra != rb) ctx.Fail("GetFormat(%08x): original format #%d ours #%d", id, ra, rb);
        if (rb < 0) ctx.Fail("GetFormat(%08x): ours returned a pointer outside our formats", id);
    }
    // Every registered id finds its own format; others find the unknown format.
    if (ids.size() < 40) ctx.Fail("expected at least 40 distinct ids, got %zu", ids.size());
    if (m.w.Ours(AsMgr(m.ours)->GetUnknownFormat()) != m.w.Orig(ctx.Original<void *(__thiscall *)(void *)>(0x15f800)(m.orig))) ctx.Fail("GetUnknownFormat differs");
    if (m.w.Ours(AsMgr(m.ours)->GetUnknownFormat()) != int(kFormatCount)) ctx.Fail("GetUnknownFormat is not our unknown format");
}

DIFF_TEST(PixelFormatsManager_Lookups, 0x15f480)
{
    Pair m(ctx);
    Mgr *mgr = AsMgr(m.ours);
    std::mt19937 rng(0x15f480);
    using F5 = void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    using F4 = void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t);
    using F3 = void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
    using F1 = void *(__thiscall *)(void *, uint32_t);
    using FP = void *(__thiscall *)(void *, const uint8_t *);
    auto idx = [&](void *a, void *b, int &ia, int &ib) {
        ia = m.w.Orig(a);
        ib = m.w.Ours(b);
    };
    int hits[6] = {};
    for (cPixelFormat *fmt : mgr->mSorted) {
        const uint8_t *o = reinterpret_cast<const uint8_t *>(fmt);
        uint32_t w[5];
        for (int k = 0; k < 5; ++k) std::memcpy(&w[k], o + 0x20 + 4 * k, 4);
        const void *v = pftest::VTableOf(o);
        for (int variant = 0; variant < 40; ++variant) {
            uint32_t p[5] = {w[0], w[1], w[2], w[3], w[4]};
            if (variant) {
                int k = rng() % 5;
                p[k] = variant % 3 == 0 ? static_cast<uint32_t>(rng()) : p[k] + static_cast<uint32_t>(rng() % 3) - 1;
            }
            void *ra, *rb;
            ++ctx.cases;
            if (v == ClassVTable(FormatClass::ARGB)) {
                ra = ctx.Original<F5>(0x15f480)(m.orig, p[0], p[1], p[2], p[3], p[4]);
                rb = mgr->GetARGB(p[0], p[1], p[2], p[3], p[4]);
                hits[0] += ra != nullptr;
            } else if (v == ClassVTable(FormatClass::AlphaLuminance)) {
                ra = ctx.Original<F3>(0x15f560)(m.orig, p[0], p[1], p[2]);
                rb = mgr->GetAlphaLuminance(p[0], p[1], p[2]);
                hits[1] += ra != nullptr;
            } else if (v == ClassVTable(FormatClass::ABGRFloat)) {
                ra = ctx.Original<F4>(0x15f3a0)(m.orig, p[0], p[1], p[2], p[3]);
                rb = mgr->GetABGRFloat(p[0], p[1], p[2], p[3]);
                hits[2] += ra != nullptr;
            } else if (v == ClassVTable(FormatClass::YUV)) {
                uint8_t chars[4];
                const std::string &name = fmt->mName;
                for (int k = 0; k < 4; ++k) chars[k] = k < static_cast<int>(name.size()) ? static_cast<uint8_t>(name[k]) : 0;
                if (variant) chars[rng() % 4] ^= static_cast<uint8_t>(rng());
                ra = ctx.Original<FP>(0x15f810)(m.orig, chars);
                rb = mgr->GetYUV(chars);
                hits[3] += ra != nullptr;
            } else if (v == ClassVTable(FormatClass::DXT)) {
                ra = ctx.Original<F1>(0x15f640)(m.orig, p[2]);
                rb = mgr->GetDXT(p[2]);
                hits[4] += ra != nullptr;
            } else if (v == ClassVTable(FormatClass::DepthStencil)) {
                uint32_t flag = (p[3] & 0xff) | (rng() % 2 ? static_cast<uint32_t>(rng()) & 0xffffff00u : 0);
                ra = ctx.Original<F4>(0x15f720)(m.orig, p[0], p[1], p[2], flag);
                rb = mgr->GetDepthStencil(p[0], p[1], p[2], flag);
                hits[5] += ra != nullptr;
            } else {
                continue; // the Unknown format has no lookup
            }
            int ia, ib;
            idx(ra, rb, ia, ib);
            if (ia != ib) ctx.Fail("lookup of format %s (variant %d): original #%d ours #%d", fmt->mName.c_str(), variant, ia, ib);
        }
    }
    for (int k = 0; k < 6; ++k) {
        if (hits[k] == 0) ctx.Fail("lookup kind %d never found a format", k);
    }
    // Random parameters, mostly misses.
    for (int i = 0; i < 4000; ++i) {
        uint32_t p[5];
        for (auto &x : p) x = rng() % 3 ? rng() % 40 : static_cast<uint32_t>(rng());
        ++ctx.cases;
        int ia, ib;
        idx(ctx.Original<F5>(0x15f480)(m.orig, p[0], p[1], p[2], p[3], p[4]), mgr->GetARGB(p[0], p[1], p[2], p[3], p[4]), ia, ib);
        if (ia != ib) ctx.Fail("GetARGB miss differs");
        idx(ctx.Original<F3>(0x15f560)(m.orig, p[0], p[1], p[2]), mgr->GetAlphaLuminance(p[0], p[1], p[2]), ia, ib);
        if (ia != ib) ctx.Fail("GetAlphaLuminance miss differs");
        idx(ctx.Original<F4>(0x15f3a0)(m.orig, p[0], p[1], p[2], p[3]), mgr->GetABGRFloat(p[0], p[1], p[2], p[3]), ia, ib);
        if (ia != ib) ctx.Fail("GetABGRFloat miss differs");
        idx(ctx.Original<F1>(0x15f640)(m.orig, p[0] % 8), mgr->GetDXT(p[0] % 8), ia, ib);
        if (ia != ib) ctx.Fail("GetDXT differs");
        uint8_t chars[4] = {uint8_t(rng()), uint8_t(rng()), uint8_t(rng()), uint8_t(rng())};
        idx(ctx.Original<FP>(0x15f810)(m.orig, chars), mgr->GetYUV(chars), ia, ib);
        if (ia != ib) ctx.Fail("GetYUV miss differs");
        idx(ctx.Original<F4>(0x15f720)(m.orig, p[0], p[1], p[2], p[3]), mgr->GetDepthStencil(p[0], p[1], p[2], p[3]), ia, ib);
        if (ia != ib) ctx.Fail("GetDepthStencil miss differs");
    }
}

// The in-game configuration: the formats live in the image's storage and carry our vtables (the image's
// static initialisers call our constructors on them; the six constant-initialised ones are adopted by
// ImageStore). Here the image's objects are rebuilt that way, then the original manager code and ours both
// run over those very objects, so pointers compare directly. The image's own bytes are restored afterwards.
DIFF_TEST(PixelFormatsManager_ImageStore, 0x15d4b0)
{
    Worlds w(ctx);
    struct Saved {
        void *where;
        uint8_t bytes[kStaticFormatStorage];
    };
    std::vector<Saved> saved;
    auto save = [&](cPixelFormat *f) {
        Saved s;
        s.where = f;
        std::memcpy(s.bytes, f, kStaticFormatStorage);
        saved.push_back(s);
    };
    for (cPixelFormat *f : w.origTable) save(f);
    save(w.origUnknown);

    // The image store's table is the original's table, pointer for pointer.
    ResetImageStore();
    SetPixelFormatStoreProvider(&ImageStore);
    const PixelFormatStore &store = ImageStore();
    for (uint32_t i = 0; i < kFormatCount; ++i) {
        ++ctx.cases;
        if (store.table[i] != w.origTable[i]) ctx.Fail("ImageStore table[%u] is %p, the image's table has %p", i, static_cast<void *>(store.table[i]), static_cast<void *>(w.origTable[i]));
    }
    if (store.unknown != w.origUnknown) ctx.Fail("ImageStore unknown format is not the image's");
    if (store.createIterator != ctx.Original<decltype(store.createIterator)>(kOriginalCreateIterator)) ctx.Fail("ImageStore createIterator is not the image's CreateIterator");

    // As in-game: our constructors on the 50 non-DXT objects (the image's static initialisers call our
    // hooked constructors), the DXT objects and the unknown format keep the image's constant data and
    // get adopted by ImageStore (reset so it runs again).
    for (uint32_t i = 0; i < kFormatCount; ++i) {
        if (kStaticFormatSpecs[i].kind != StaticKind::DXT) ConstructStaticFormat(w.origTable[i], kStaticFormatSpecs[i]);
    }
    ResetImageStore();
    ImageStore();
    for (uint32_t i = 0; i < kFormatCount; ++i) {
        FormatClass cls = ClassOf(kStaticFormatSpecs[i].kind);
        ++ctx.cases;
        if (pftest::VTableOf(w.origTable[i]) != ClassVTable(cls)) ctx.Fail("image object %u does not carry our %d vtable", i, int(cls));
    }
    if (pftest::VTableOf(w.origUnknown) != ClassVTable(FormatClass::Unknown)) ctx.Fail("the image's unknown format was not adopted");
    // The adopted objects keep what the image's initialisers gave them: ids, fields and names.
    for (uint32_t i = 32; i < 37; ++i) {
        const uint8_t *s = saved[i].bytes, *n = reinterpret_cast<const uint8_t *>(w.origTable[i]);
        ++ctx.cases;
        if (std::memcmp(s + 4, n + 4, 4) != 0 || std::memcmp(s + 0x20, n + 0x20, 12) != 0) ctx.Fail("adoption changed DXT object %u", i);
        if (!pftest::VTablesCorrespond(ctx, FormatClass::DXT, false, pftest::VTableOf(s), pftest::VTableOf(n))) ctx.Fail("DXT object %u: vtable pair", i);
    }

    // The original manager code and ours over the same objects.
    {
        uint8_t *a = static_cast<uint8_t *>(::operator new(Mgr::kSize)), *b = static_cast<uint8_t *>(::operator new(Mgr::kSize));
        std::memset(a, 0, Mgr::kSize);
        std::memset(b, 0, Mgr::kSize);
        ctx.Original<CtorFn>(0x15d4b0)(a);
        AsMgr(b)->Construct();
        ++ctx.cases;
        auto same = [](const void *p) { return static_cast<int>(reinterpret_cast<intptr_t>(p)); }; // one world: pointers compare directly
        MgrState sa = StateOf(AsMgr(a), same);
        MgrState sb = StateOf(AsMgr(b), same);
        if (!(sa == sb)) ctx.Fail("manager over the image's objects: original and ours differ");
        if (AsMgr(b)->mSorted != AsMgr(a)->mSorted) ctx.Fail("manager over the image's objects: sorted order differs");
        for (auto &kv : AsMgr(b)->mById) {
            if (AsMgr(b)->GetFormat(kv.first) != kv.second) ctx.Fail("GetFormat over the image's objects");
        }
        if (AsMgr(b)->GetFormat(0xdeadbeef) != w.origUnknown || AsMgr(b)->GetUnknownFormat() != w.origUnknown) ctx.Fail("unknown format over the image's objects");
        // Lookups: ours and the original's on each object's own parameters.
        using F5 = void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
        for (cPixelFormat *f : w.origTable) {
            if (pftest::VTableOf(f) != ClassVTable(FormatClass::ARGB)) continue;
            uint32_t p[5];
            std::memcpy(p, reinterpret_cast<const uint8_t *>(f) + 0x20, sizeof(p));
            ++ctx.cases;
            void *ra = ctx.Original<F5>(0x15f480)(a, p[0], p[1], p[2], p[3], p[4]);
            void *rb = AsMgr(b)->GetARGB(p[0], p[1], p[2], p[3], p[4]);
            if (ra != rb || ra != f) ctx.Fail("GetARGB over the image's objects: original %p ours %p expected %p", ra, rb, static_cast<void *>(f));
        }
        AsMgr(a)->Destruct();
        AsMgr(b)->Destruct();
        ::operator delete(a);
        ::operator delete(b);
    }

    // Restore the image's own objects and the default store.
    for (const Saved &s : saved) std::memcpy(s.where, s.bytes, kStaticFormatStorage);
    ResetImageStore();
    SetPixelFormatStoreProvider(&StandaloneStore);
}

// ---------------------------------------------------------------------------------------------------
// Sort and equal_range on fake formats

constexpr int kMaxFakes = 80;
struct FakeFormat {
    const void *vptr;
};
FakeFormat gFakes[kMaxFakes + 2];
uint32_t gRank[kMaxFakes + 2];
std::vector<uint32_t> gCompareLog;

int FakeIndex(const void *p)
{
    return static_cast<int>(static_cast<const FakeFormat *>(p) - gFakes);
}

// Less(other, strict): ranks decide; the upper bits of eax carry junk (callers read al).
uint32_t __fastcall FakeLess(void *self, void *, const void *other, uint32_t strict)
{
    int a = FakeIndex(self), b = FakeIndex(other);
    gCompareLog.push_back(static_cast<uint32_t>(a << 16 | b << 4 | (strict & 1)));
    return (gRank[a] < gRank[b] ? 1u : 0u) | 0x5a5a5a00u;
}
void *gFakeVTable[16] = {};

void SetupFakes(std::mt19937 &rng, int n, int rankSpread)
{
    for (int i = 0; i < kMaxFakes + 2; ++i) {
        gFakes[i].vptr = gFakeVTable;
        gRank[i] = rng() % rankSpread;
    }
    (void)n;
}

// The lookups on a manager whose vector holds fake formats: the matching element is asked for the
// class interface (QueryInterface, recorded, answering with itself or nothing) and the result is
// released again; the temporary key's own Less (original code) also reads the fakes' slots. Compares the
// returned pointer and the whole call log.
bool gQiHit[kMaxFakes];
uint32_t __fastcall FakeQi(void *self, void *, uint32_t iid, void **ppv)
{
    int i = FakeIndex(self);
    gCompareLog.push_back(0x10000000u | static_cast<uint32_t>(i));
    gCompareLog.push_back(iid);
    *ppv = gQiHit[i] ? self : nullptr;
    return 0x77777700u;
}
uint32_t __fastcall FakeRelease(void *self, void *)
{
    gCompareLog.push_back(0x20000000u | static_cast<uint32_t>(FakeIndex(self)));
    return 0x12345678;
}
uint32_t __fastcall FakeGetSlot3(void *self, void *)
{
    gCompareLog.push_back(0x30000000u | static_cast<uint32_t>(FakeIndex(self)));
    return gRank[FakeIndex(self)];
}
uint32_t __fastcall FakeGetSlot5(void *self, void *)
{
    gCompareLog.push_back(0x50000000u | static_cast<uint32_t>(FakeIndex(self)));
    return gRank[FakeIndex(self)] & 3;
}
// elem->Less(key, strict): decided by the element and one of the key's fields (the key is a real
// format object on the stack, so its address differs between the sides).
uint32_t __fastcall FakeLessVsKey(void *self, void *, const void *other, uint32_t strict)
{
    int a = FakeIndex(self);
    uint32_t keyField;
    std::memcpy(&keyField, static_cast<const uint8_t *>(other) + 0x20, 4);
    gCompareLog.push_back(0x40000000u | static_cast<uint32_t>(a) << 8 | (strict & 1));
    gCompareLog.push_back(keyField);
    return ((gRank[a] + keyField * 7 + a) % 3 == 0 ? 1u : 0u) | 0x33333300u;
}

DIFF_TEST(PixelFormatsManager_LookupCalls, 0x15f640)
{
    void *vtable[16] = {};
    for (auto &s : vtable) s = reinterpret_cast<void *>(&FakeRelease);
    vtable[0] = reinterpret_cast<void *>(&FakeQi);
    vtable[2] = reinterpret_cast<void *>(&FakeRelease);
    vtable[3] = reinterpret_cast<void *>(&FakeGetSlot3);
    vtable[5] = reinterpret_cast<void *>(&FakeGetSlot5);
    vtable[10] = reinterpret_cast<void *>(&FakeLessVsKey);
    std::mt19937 rng(0x15f641);
    using F5 = void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    using F4 = void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t);
    using F3 = void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
    using F1 = void *(__thiscall *)(void *, uint32_t);
    using FP = void *(__thiscall *)(void *, const uint8_t *);
    for (int i = 0; i < 6000; ++i) {
        int n = rng() % 14;
        SetupFakes(rng, n, 5);
        for (int k = 0; k < kMaxFakes + 2; ++k) gFakes[k].vptr = vtable;
        for (auto &h : gQiHit) h = rng() % 3 != 0;
        uint8_t raw[Mgr::kSize] = {};
        Mgr *mgr = AsMgr(raw);
        new (&mgr->mSorted) std::vector<cPixelFormat *>(n);
        for (int k = 0; k < n; ++k) mgr->mSorted[k] = reinterpret_cast<cPixelFormat *>(&gFakes[k]);
        uint32_t p[5];
        for (auto &x : p) x = rng() % 4 == 0 ? static_cast<uint32_t>(rng()) : rng() % 20;
        uint8_t chars[4] = {uint8_t(rng()), uint8_t(rng()), uint8_t(rng()), uint8_t(rng())};
        int kind = i % 6;
        void *ra = nullptr, *rb = nullptr;
        gCompareLog.clear();
        switch (kind) {
        case 0: ra = ctx.Original<F5>(0x15f480)(raw, p[0], p[1], p[2], p[3], p[4]); break;
        case 1: ra = ctx.Original<F3>(0x15f560)(raw, p[0], p[1], p[2]); break;
        case 2: ra = ctx.Original<F4>(0x15f3a0)(raw, p[0], p[1], p[2], p[3]); break;
        case 3: ra = ctx.Original<FP>(0x15f810)(raw, chars); break;
        case 4: ra = ctx.Original<F1>(0x15f640)(raw, p[0]); break;
        default: ra = ctx.Original<F4>(0x15f720)(raw, p[0], p[1], p[2], p[3] | 0x100); break;
        }
        std::vector<uint32_t> logA = gCompareLog;
        gCompareLog.clear();
        switch (kind) {
        case 0: rb = mgr->GetARGB(p[0], p[1], p[2], p[3], p[4]); break;
        case 1: rb = mgr->GetAlphaLuminance(p[0], p[1], p[2]); break;
        case 2: rb = mgr->GetABGRFloat(p[0], p[1], p[2], p[3]); break;
        case 3: rb = mgr->GetYUV(chars); break;
        case 4: rb = mgr->GetDXT(p[0]); break;
        default: rb = mgr->GetDepthStencil(p[0], p[1], p[2], p[3] | 0x100); break;
        }
        ++ctx.cases;
        if (ra != rb) ctx.Fail("lookup kind %d (n=%d): original %p ours %p", kind, n, ra, rb);
        if (logA != gCompareLog) ctx.Fail("lookup kind %d (n=%d): call logs differ (%zu vs %zu)", kind, n, logA.size(), gCompareLog.size());
        mgr->mSorted.~vector();
    }
}

DIFF_TEST(PixelFormatsManager_EqualRange, 0x15b4a0)
{
    using RangeFn = Mgr::Range *(__cdecl *)(Mgr::Range *, cPixelFormat *const *, cPixelFormat *const *, cPixelFormat *const *, uint32_t);
    gFakeVTable[10] = reinterpret_cast<void *>(&FakeLess);
    std::mt19937 rng(0x15b4a0);
    for (int i = 0; i < 6000; ++i) {
        int n = i < 90 ? i % 70 : static_cast<int>(rng() % (kMaxFakes + 1));
        int spread = Pick(rng, {1, 2, 3, 10, 40, 1000});
        SetupFakes(rng, n, spread);
        std::vector<cPixelFormat *> v(n);
        for (int k = 0; k < n; ++k) v[k] = reinterpret_cast<cPixelFormat *>(&gFakes[k]);
        std::stable_sort(v.begin(), v.end(), [](cPixelFormat *x, cPixelFormat *y) { return gRank[FakeIndex(x)] < gRank[FakeIndex(y)]; });
        cPixelFormat *key = reinterpret_cast<cPixelFormat *>(&gFakes[kMaxFakes + (i & 1)]); // outside the array
        if (n && i % 3 == 0) key = v[rng() % n];
        Mgr::Range ra{}, rb{};
        gCompareLog.clear();
        auto *retA = ctx.Original<RangeFn>(0x15b4a0)(&ra, v.data(), v.data() + n, &key, static_cast<uint32_t>(rng()));
        std::vector<uint32_t> logA = gCompareLog;
        gCompareLog.clear();
        auto *retB = Mgr::EqualRange(&rb, v.data(), v.data() + n, &key, 0);
        ++ctx.cases;
        if (retA != &ra || retB != &rb) ctx.Fail("EqualRange: returned pointer");
        if (ra.first - v.data() != rb.first - v.data() || ra.last - v.data() != rb.last - v.data()) ctx.Fail("EqualRange(n=%d, spread %d): range differs", n, spread);
        if (logA != gCompareLog) ctx.Fail("EqualRange(n=%d, spread %d): comparisons differ", n, spread);
    }
}

} // namespace
