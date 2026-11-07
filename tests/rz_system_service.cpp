// Differential tests for rz::cRZUnknownMT and rz::cRZSystemService in the standalone shape (src/rz/RZSystemServiceBase.*,
// lane EI). Our side: objects built by our constructors (through the constructor hooks' adapters), every slot called
// through the object's own vtable. Original side: objects built by the original constructors, slots called through the
// game's vtables (so the original thunks 0x1b9672 / 0x1b9648 / 0x1b9682 / 0x1b9612 run too). Only the vtable words are
// normalised: each is mapped to "the side's own vtable of face f" (or the side's cRZUnknownMT vtable, which the destructors
// leave at +4), anything else stays raw. Frees go to a recording stub on both sides (the game's sized delete 0x824624 and
// ours are redirected while an object may be deleted); objects that may be freed come from HeapAlloc.
#include "difftest.h"
#include "rz/RZSystemServiceBase.h"
#include "shape.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <new>
#include <random>

namespace {

using rz::cRZSystemService;
using rz::cRZUnknownMT;
using MTA = rz::cRZUnknownMTAdapters;
using SSA = rz::cRZSystemServiceAdapters;

constexpr uint32_t kGameVT0 = 0xda3058;  // cRZSystemService, +0 (13 slots)
constexpr uint32_t kGameVT4 = 0xda3090;  // cRZSystemService, +4 (6 slots)
constexpr uint32_t kGameMTVT = 0xda2fec; // cRZUnknownMT (6 slots)
constexpr uint32_t kGameSizedDelete = 0x824624;
constexpr size_t kSize = sizeof(cRZSystemService);

// ---- redirection of the two sized deletes to a recording stub

// The code stays writable after the restore: putting back the old protection VirtualProtect reported (the first page's)
// narrows both pages when the five bytes straddle a page boundary, and an earlier redirect on the second page then
// faults when it restores its bytes (lane MW, tests/app_fixture.h).
struct Redirect {
    uint8_t *site; uint8_t saved[5];
    Redirect(void *entry, void *target) : site(static_cast<uint8_t *>(entry))
    {
        std::memcpy(saved, site, 5);
        DWORD old; VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        int32_t relative = int32_t(reinterpret_cast<uintptr_t>(target) - reinterpret_cast<uintptr_t>(site) - 5);
        site[0] = 0xe9; std::memcpy(site + 1, &relative, 4);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    ~Redirect()
    {
        DWORD old; VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(site, saved, 5);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    Redirect(const Redirect &) = delete;
    Redirect &operator=(const Redirect &) = delete;
};

unsigned gFrees = 0; void *gFreed = nullptr; size_t gFreedSize = 0;
void __cdecl RecordingFree(void *p, size_t size)
{
    ++gFrees; gFreed = p; gFreedSize = size;
    HeapFree(GetProcessHeap(), 0, p);
}

struct FreeWatch {
    Redirect game, ours;
    explicit FreeWatch(const difftest::Context &ctx)
        : game(ctx.image.At(kGameSizedDelete), reinterpret_cast<void *>(&RecordingFree)),
          ours(reinterpret_cast<void *>(static_cast<void(__cdecl *)(void *, size_t)>(&::operator delete)),
               reinterpret_cast<void *>(&RecordingFree))
    {
        gFrees = 0; gFreed = nullptr; gFreedSize = 0;
    }
};

uint8_t *Alloc() { return static_cast<uint8_t *>(HeapAlloc(GetProcessHeap(), 0, kSize)); }
void Release(uint8_t *p) { HeapFree(GetProcessHeap(), 0, p); }

// ---- the two worlds' vtables

struct Side {
    const void *vt0, *vt4, *mt;          // the side's own vtables
    const void *copy0 = nullptr, *copy4 = nullptr; // patched copies a test installed (the same arrays on both sides)
};
Side GameSide(const difftest::Context &ctx) { return {ctx.image.At(kGameVT0), ctx.image.At(kGameVT4), ctx.image.At(kGameMTVT)}; }
Side OurSide()
{
    alignas(8) uint8_t service[kSize], mt[8];
    auto *s = SSA::Construct(service, nullptr, 1, 2);
    auto *m = MTA::Construct(mt, nullptr);
    Side side{*reinterpret_cast<void **>(service), *reinterpret_cast<void **>(service + 4), *reinterpret_cast<void **>(mt)};
    SSA::Destruct(s, nullptr);
    MTA::Destruct(m, nullptr);
    return side;
}

uint32_t Code(const void *word, const Side &s)
{
    if (word == s.vt0) return 0xc0de0000;
    if (word == s.vt4) return 0xc0de0004;
    if (word == s.mt) return 0xc0de00ff;
    if (s.copy0 && word == s.copy0) return 0xc0de1000;
    if (s.copy4 && word == s.copy4) return 0xc0de1004;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(word));
}

// The object's bytes with its two vtable words (one for a cRZUnknownMT) replaced by codes.
struct Image { uint32_t words[kSize / 4]; };
Image Norm(const uint8_t *object, const Side &s, size_t size = kSize)
{
    Image image{};
    std::memcpy(image.words, object, size);
    image.words[0] = Code(reinterpret_cast<const void *const *>(object)[0], s);
    if (size > 8) image.words[1] = Code(reinterpret_cast<const void *const *>(object)[1], s);
    return image;
}
bool Same(const Image &a, const Image &b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }

template <typename R, typename... A> R CallSlot(void *face, int slot, A... args)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*reinterpret_cast<void *const *const *>(face))[slot]);
    return fn(face, args...);
}

void Fill(uint8_t *p, std::mt19937 &rng, size_t size = kSize)
{
    for (size_t i = 0; i < size; ++i) p[i] = static_cast<uint8_t>(rng());
}

uint32_t Count(std::mt19937 &rng, unsigned i)
{
    static const uint32_t edges[] = {0, 1, 2, 3, 0x7fffffff, 0x80000000, 0xfffffffe, 0xffffffff};
    return i % 2 ? edges[(i / 2) % 8] : rng() % 5;
}

struct World {
    difftest::Context &ctx;
    Side game, ours;
    explicit World(difftest::Context &c) : ctx(c), game(GameSide(c)), ours(OurSide())
    {
        if (ours.vt0 == game.vt0 || ours.vt4 == game.vt4 || ours.mt == game.mt || ours.vt0 == ours.vt4)
            ctx.Fail("our vtables are not our own");
    }
    // A service on `memory` built by each side's own constructor.
    uint8_t *Original(uint8_t *memory, uint32_t id, int32_t priority)
    {
        ctx.Original<void *(__thiscall *)(void *, uint32_t, int32_t)>(0x1b9584)(memory, id, priority);
        return memory;
    }
    uint8_t *Ours(uint8_t *memory, uint32_t id, int32_t priority)
    {
        SSA::Construct(memory, nullptr, id, priority);
        return memory;
    }
};

} // namespace

// Both constructors and cRZUnknownMT's on the same random bytes: the whole object compared, the padding after the
// running byte included (untouched on both sides).
DIFF_TEST(RZSystemService_Construct, 0x1b9584)
{
    World w(ctx); std::mt19937 rng(0xe1a);
    auto ctor2 = ctx.Original<void *(__thiscall *)(void *, uint32_t, int32_t, int32_t)>(0x1b95bf);
    auto ctorMT = ctx.Original<void *(__thiscall *)(void *)>(0x1b9078);
    for (unsigned i = 0; i < 3000; ++i) {
        alignas(8) uint8_t a[kSize], b[kSize];
        Fill(a, rng); std::memcpy(b, a, kSize);
        uint32_t id = rng(); int32_t priority = static_cast<int32_t>(rng()), tick = static_cast<int32_t>(rng());
        void *ra, *rb; size_t size = kSize;
        switch (i % 3) {
        case 0: ra = ctx.Original<void *(__thiscall *)(void *, uint32_t, int32_t)>(0x1b9584)(a, id, priority);
                rb = SSA::Construct(b, nullptr, id, priority); break;
        case 1: ra = ctor2(a, id, priority, tick); rb = SSA::ConstructWithTickPriority(b, nullptr, id, priority, tick); break;
        default: ra = ctorMT(a); rb = MTA::Construct(b, nullptr); size = 8; break;
        }
        ++ctx.cases;
        Image na = Norm(a, w.game, size), nb = Norm(b, w.ours, size);
        if (size == 8) { std::memcpy(na.words + 2, a + 8, kSize - 8); std::memcpy(nb.words + 2, b + 8, kSize - 8); }
        if (ra != a || rb != b || !Same(na, nb)) ctx.Fail("constructor %u (kind %u) differs", i, i % 3);
    }
}

// The destructors through each side's own vtable: the +4 face's slot 3 (the original's thunk 0x1b9612 -> 0x1b961a ->
// 0x1b95fe -> 0x1b90ab; ours the compiler's) and a cRZUnknownMT's slot 3 (0x1b912e -> 0x1b90ab), every flags value.
DIFF_TEST(RZSystemService_Destructors, 0x1b961a)
{
    World w(ctx); std::mt19937 rng(0xe1b);
    for (unsigned i = 0; i < 400; ++i) {
        uint32_t flags = i % 4 == 3 ? rng() : i % 4;
        bool mt = (i / 4) % 2 != 0;
        uint8_t *a = Alloc(), *b = Alloc();
        Fill(a, rng); std::memcpy(b, a, kSize);
        uint32_t id = rng(); int32_t priority = static_cast<int32_t>(rng());
        if (mt) { ctx.Original<void *(__thiscall *)(void *)>(0x1b9078)(a); MTA::Construct(b, nullptr); }
        else { w.Original(a, id, priority); w.Ours(b, id, priority); }
        void *faceA = mt ? a : a + 4, *faceB = mt ? b : b + 4;
        void *ra, *rb; unsigned freesA, freesB; void *freedA, *freedB; size_t sizeA, sizeB;
        {
            FreeWatch watch(ctx);
            ra = CallSlot<void *>(faceA, 3, flags); freesA = gFrees; freedA = gFreed; sizeA = gFreedSize;
            gFrees = 0; gFreed = nullptr; gFreedSize = 0;
            rb = CallSlot<void *>(faceB, 3, flags); freesB = gFrees; freedB = gFreed; sizeB = gFreedSize;
        }
        ++ctx.cases;
        bool freed = (flags & 1) != 0;
        size_t expected = mt ? 8 : kSize;
        if (ra != a || rb != b || freesA != freesB || freesA != (freed ? 1u : 0u) || (freed && (freedA != a || freedB != b ||
            sizeA != sizeB || sizeA != expected)))
            ctx.Fail("deleting destructor %u (flags %x, %s): return / frees differ", i, flags, mt ? "MT" : "service");
        if (!freed) {
            if (!Same(Norm(a, w.game, mt ? 8 : kSize), Norm(b, w.ours, mt ? 8 : kSize)) || std::memcmp(a + 8, b + 8, kSize - 8))
                ctx.Fail("destructor %u (%s): object differs", i, mt ? "MT" : "service");
            Release(a); Release(b);
        }
    }
}

// The cIGZUnknown slots of both faces and cRZUnknownMT's 4 / 5 (and a plain cRZUnknownMT's six) through each side's own
// vtable, over counts that delete (a count of 0 or 1 at Release), decrement, and wrap. QueryInterface for the service id
// (this, +0), 1 (the cRZUnknownMT part, +4) and others (false, out untouched), counted through slot 1.
DIFF_TEST(RZSystemService_RefCount, 0x1b9650)
{
    World w(ctx); std::mt19937 rng(0xe1c);
    unsigned deleted = 0, kept = 0;
    // {object kind, face offset, slot}: kind 0 the service, 1 a plain cRZUnknownMT.
    struct Case { int kind, face, slot; };
    const Case cases[] = {{0, 0, 0}, {0, 0, 1}, {0, 0, 2}, {0, 4, 0}, {0, 4, 1}, {0, 4, 2}, {0, 4, 4}, {0, 4, 5},
                          {1, 0, 0}, {1, 0, 1}, {1, 0, 2}, {1, 0, 4}, {1, 0, 5}};
    for (unsigned i = 0; i < 6000; ++i) {
        const Case &c = cases[i % (sizeof(cases) / sizeof(cases[0]))];
        uint8_t *a = Alloc(), *b = Alloc();
        Fill(a, rng); std::memcpy(b, a, kSize);
        uint32_t id = rng(); int32_t priority = static_cast<int32_t>(rng());
        if (c.kind) { ctx.Original<void *(__thiscall *)(void *)>(0x1b9078)(a); MTA::Construct(b, nullptr); }
        else { w.Original(a, id, priority); w.Ours(b, id, priority); }
        uint32_t count = Count(rng, i / 13);
        uint8_t *countA = a + (c.kind ? 4 : 8), *countB = b + (c.kind ? 4 : 8);
        std::memcpy(countA, &count, 4); std::memcpy(countB, &count, 4);
        uint32_t riid = (i / 13) % 4 == 0 ? cIGZSystemService::kIID : (i / 13) % 4 == 1 ? 1 : (i / 13) % 4 == 2 ? 0 : rng();
        void *sentinel = &w, *outA = sentinel, *outB = sentinel;
        uint32_t ra, rb; unsigned freesA, freesB; void *freedA, *freedB;
        {
            FreeWatch watch(ctx);
            ra = c.slot == 0 ? CallSlot<uint8_t>(a + c.face, 0, riid, &outA) : CallSlot<uint32_t>(a + c.face, c.slot);
            freesA = gFrees; freedA = gFreed; gFrees = 0; gFreed = nullptr;
            rb = c.slot == 0 ? CallSlot<uint8_t>(b + c.face, 0, riid, &outB) : CallSlot<uint32_t>(b + c.face, c.slot);
            freesB = gFrees; freedB = gFreed;
        }
        ++ctx.cases;
        // QueryInterface answers a bool in al (the original's `mov al, 1` / `xor al, al`; eax's upper bytes are left over).
        if (c.slot == 0) { ra &= 0xff; rb &= 0xff; }
        auto where = [](void *out, uint8_t *object, void *s) -> intptr_t {
            return out == s ? -1 : static_cast<uint8_t *>(out) - object;
        };
        if (ra != rb || freesA != freesB || (freesA && (freedA != a || freedB != b)) || where(outA, a, sentinel) != where(outB, b, sentinel))
            ctx.Fail("refcount %u (kind %d, face +%d, slot %d, count %u, riid %08x): %u vs %u, frees %u vs %u", i, c.kind, c.face,
                     c.slot, count, riid, ra, rb, freesA, freesB);
        if (freesA != freesB) continue; // leak the survivor rather than double free
        if (freesA) { ++deleted; continue; }
        ++kept;
        size_t size = c.kind ? 8 : kSize;
        if (!Same(Norm(a, w.game, size), Norm(b, w.ours, size)) || std::memcmp(a + size, b + size, kSize - size))
            ctx.Fail("refcount %u (kind %d, face +%d, slot %d, count %u): object differs", i, c.kind, c.face, c.slot, count);
        Release(a); Release(b);
    }
    std::printf("  refcount: %u deleted, %u kept\n", deleted, kept);
}

// The service face's slots 3..12 (folded accessors and the defaults) through each side's own vtable: results (bool slots
// compared in al: 0x144910 / 0x148260 are `mov al, 1`, 0x156110 `mov al, [ecx + 0x18]`), and the objects after the
// setters (SetServiceRunning stores its argument byte raw: 0x1b9690 `mov al, [esp + 4]; mov [ecx + 0x18], al`).
DIFF_TEST(RZSystemService_Slots, 0x1b9690)
{
    World w(ctx); std::mt19937 rng(0xe1d);
    for (unsigned i = 0; i < 4000; ++i) {
        alignas(8) uint8_t a[kSize], b[kSize];
        Fill(a, rng); std::memcpy(b, a, kSize);
        uint32_t id = rng(); int32_t priority = static_cast<int32_t>(rng());
        if (i % 2) {
            int32_t tick = static_cast<int32_t>(rng());
            ctx.Original<void *(__thiscall *)(void *, uint32_t, int32_t, int32_t)>(0x1b95bf)(a, id, priority, tick);
            SSA::ConstructWithTickPriority(b, nullptr, id, priority, tick);
        } else {
            w.Original(a, id, priority); w.Ours(b, id, priority);
        }
        a[0x18] = b[0x18] = static_cast<uint8_t>(i % 3 == 0 ? 0x80 : rng() % 2);
        int slot = 3 + static_cast<int>(i % 10);
        uint32_t arg = i % 4 == 0 ? 0x80 : i % 4 == 1 ? 0x100 : rng();
        uint32_t ra, rb;
        switch (slot) {
        case 4: case 7: case 10: case 11:
            ra = CallSlot<uint32_t>(a, slot, arg); rb = CallSlot<uint32_t>(b, slot, arg); break;
        default:
            ra = CallSlot<uint32_t>(a, slot); rb = CallSlot<uint32_t>(b, slot); break;
        }
        bool byteResult = slot == 6 || slot == 8 || slot == 9 || slot == 10 || slot == 11;
        bool noResult = slot == 4 || slot == 7;
        if (byteResult) { ra &= 0xff; rb &= 0xff; }
        if (noResult) ra = rb = 0;
        ++ctx.cases;
        if (ra != rb || !Same(Norm(a, w.game), Norm(b, w.ours)))
            ctx.Fail("slot %d case %u (arg %08x): %08x vs %08x or object differs", slot, i, arg, ra, rb);
    }
}

// ---- the RVA hooks' entry points on objects built by the original constructors (game vtables), as the game reaches them.
// Each object carries a copy of the game's vtables whose forwarded slot is a trap of that slot's arity, so an adapter that
// dispatched virtually (in game: back into its own hook) fails. Slots a body calls legitimately keep the game's function
// (slot 1, AddRef, for QueryInterface) or a recording delete (slot 3, for a Release that deletes).
namespace {

unsigned gTrapped = 0;
uint32_t TrapHit(void *, unsigned) { ++gTrapped; return 0; }
void *Trap(unsigned words) { return shape::TrapFor<TrapHit>(words); }
unsigned gDeleted = 0; uint32_t gDeleteFlags = 0;
void *__fastcall RecordingDelete(void *self, void *, uint32_t flags) { ++gDeleted; gDeleteFlags = flags; return self; }

} // namespace

DIFF_TEST(RZSystemService_Adapters, 0x1b9517)
{
    World w(ctx); std::mt19937 rng(0xe1e);
    void *copy0[13], *copy4[6], *copyMT[6];
    enum Kind { kMTRelease, kMTRemoveRef, kMTDestruct, kMTDeleting, kMTConstruct, kSSConstruct, kSSConstruct3, kSSDestruct,
                kSSDeleting, kSSQuery, kKinds };
    static const char *const names[] = {"MT Release", "MT RemoveRef", "MT Destruct", "MT DeletingDestructor", "MT Construct",
                                        "Construct", "ConstructWithTickPriority", "Destruct", "DeletingDestructor", "QueryInterface"};
    for (unsigned i = 0; i < 5000; ++i) {
        Kind kind = static_cast<Kind>(i % kKinds);
        bool mt = kind <= kMTConstruct;
        uint8_t *a = Alloc(), *b = Alloc();
        Fill(a, rng); std::memcpy(b, a, kSize);
        uint32_t id = rng(); int32_t priority = static_cast<int32_t>(rng()), tick = static_cast<int32_t>(rng());
        uint32_t count = Count(rng, i / kKinds), flags = rng() % 4, riid = i % 3 == 0 ? cIGZSystemService::kIID : i % 3 == 1 ? 1 : rng();
        bool construct = kind == kMTConstruct || kind == kSSConstruct || kind == kSSConstruct3;
        Side gameSide = w.game, oursSide = w.ours;
        // Both objects built by the original constructor, carrying the trap copies.
        for (uint8_t *p : {a, b}) {
            if (construct) break;
            if (mt) ctx.Original<void *(__thiscall *)(void *)>(0x1b9078)(p);
            else w.Original(p, id, priority);
            std::memcpy(p + (mt ? 4 : 8), &count, 4);
        }
        std::memcpy(copy0, w.game.vt0, sizeof(copy0)); std::memcpy(copy4, w.game.vt4, sizeof(copy4));
        std::memcpy(copyMT, w.game.mt, sizeof(copyMT));
        copyMT[3] = copy4[3] = reinterpret_cast<void *>(&RecordingDelete);
        switch (kind) {
        case kMTRelease: copyMT[2] = Trap(0); break;
        case kMTRemoveRef: copyMT[4] = Trap(0); break;
        case kMTDestruct: case kMTDeleting: copyMT[3] = Trap(1); break;
        case kSSDestruct: case kSSDeleting: copy4[3] = Trap(1); break;
        case kSSQuery: copy0[0] = copy4[0] = Trap(2); break;
        default: break;
        }
        if (!construct) {
            for (uint8_t *p : {a, b}) {
                if (mt) *reinterpret_cast<void **>(p) = copyMT;
                else { *reinterpret_cast<void **>(p) = copy0; *reinterpret_cast<void **>(p + 4) = copy4; }
            }
        }
        gameSide.copy0 = oursSide.copy0 = mt ? static_cast<void *>(copyMT) : static_cast<void *>(copy0);
        gameSide.copy4 = oursSide.copy4 = copy4;
        void *sentinel = &w, *outA = sentinel, *outB = sentinel;
        uint32_t ra = 0, rb = 0; unsigned deletedA, deletedB, freesA, freesB; uint32_t delFlagsA, delFlagsB; size_t freedSizeA = 0, freedSizeB = 0; intptr_t freedAtA = 0, freedAtB = 0;
        auto mtPtr = [](uint8_t *p) { return reinterpret_cast<cRZUnknownMT *>(p); };
        auto ssPtr = [](uint8_t *p) { return reinterpret_cast<cRZSystemService *>(p); };
        {
            FreeWatch watch(ctx);
            for (int side = 0; side < 2; ++side) {
                uint8_t *p = side ? b : a;
                void **out = side ? &outB : &outA;
                gTrapped = 0; gDeleted = 0; gDeleteFlags = 0; gFrees = 0;
                uint32_t r = 0;
                if (side == 0) {
                    switch (kind) {
                    case kMTRelease: r = ctx.Original<uint32_t(__thiscall *)(void *)>(0x1b9517)(p); break;
                    case kMTRemoveRef: r = ctx.Original<uint32_t(__thiscall *)(void *)>(0x1b9544)(p); break;
                    case kMTDestruct: ctx.Original<void(__thiscall *)(void *)>(0x1b90ab)(p); break;
                    case kMTDeleting: r = uint32_t(ctx.Original<uint8_t *(__thiscall *)(void *, uint32_t)>(0x1b912e)(p, flags) - p); break;
                    case kMTConstruct: r = uint32_t(ctx.Original<uint8_t *(__thiscall *)(void *)>(0x1b9078)(p) - p); break;
                    case kSSConstruct: r = uint32_t(ctx.Original<uint8_t *(__thiscall *)(void *, uint32_t, int32_t)>(0x1b9584)(p, id, priority) - p); break;
                    case kSSConstruct3:
                        r = uint32_t(ctx.Original<uint8_t *(__thiscall *)(void *, uint32_t, int32_t, int32_t)>(0x1b95bf)(p, id, priority, tick) - p);
                        break;
                    case kSSDestruct: ctx.Original<void(__thiscall *)(void *)>(0x1b95fe)(p); break;
                    case kSSDeleting: r = uint32_t(ctx.Original<uint8_t *(__thiscall *)(void *, uint32_t)>(0x1b961a)(p, flags) - p); break;
                    case kSSQuery: r = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(0x1b9650)(p, riid, out); break;
                    default: break;
                    }
                } else {
                    switch (kind) {
                    case kMTRelease: r = MTA::Release(mtPtr(p), nullptr); break;
                    case kMTRemoveRef: r = MTA::RemoveRef(mtPtr(p), nullptr); break;
                    case kMTDestruct: MTA::Destruct(mtPtr(p), nullptr); break;
                    case kMTDeleting: r = uint32_t(reinterpret_cast<uint8_t *>(MTA::DeletingDestructor(mtPtr(p), nullptr, flags)) - p); break;
                    case kMTConstruct: r = uint32_t(reinterpret_cast<uint8_t *>(MTA::Construct(p, nullptr)) - p); break;
                    case kSSConstruct: r = uint32_t(reinterpret_cast<uint8_t *>(SSA::Construct(p, nullptr, id, priority)) - p); break;
                    case kSSConstruct3:
                        r = uint32_t(reinterpret_cast<uint8_t *>(SSA::ConstructWithTickPriority(p, nullptr, id, priority, tick)) - p);
                        break;
                    case kSSDestruct: SSA::Destruct(ssPtr(p), nullptr); break;
                    case kSSDeleting: r = uint32_t(reinterpret_cast<uint8_t *>(SSA::DeletingDestructor(ssPtr(p), nullptr, flags)) - p); break;
                    case kSSQuery: r = SSA::QueryInterface(ssPtr(p), nullptr, riid, out); break;
                    default: break;
                    }
                }
                if (gTrapped) ctx.Fail("adapters %u (%s): side %d reached the trapped slot", i, names[kind], side);
                if (kind == kSSQuery) r &= 0xff;
                (side ? rb : ra) = r;
                (side ? deletedB : deletedA) = gDeleted; (side ? delFlagsB : delFlagsA) = gDeleteFlags;
                (side ? freesB : freesA) = gFrees; (side ? freedSizeB : freedSizeA) = gFreedSize; (side ? freedAtB : freedAtA) = gFrees ? static_cast<uint8_t *>(gFreed) - p : -1;
            }
        }
        ++ctx.cases;
        auto where = [](void *out, uint8_t *object, void *s) -> intptr_t { return out == s ? -1 : static_cast<uint8_t *>(out) - object; };
        if (ra != rb || deletedA != deletedB || delFlagsA != delFlagsB || freesA != freesB || freedSizeA != freedSizeB || freedAtA != freedAtB || where(outA, a, sentinel) != where(outB, b, sentinel))
            ctx.Fail("adapters %u (%s, count %u, flags %u, riid %08x): %u vs %u, deletes %u/%u, frees %u/%u", i, names[kind], count, flags,
                     riid, ra, rb, deletedA, deletedB, freesA, freesB);
        if (freesA != freesB) continue;
        if (freesA) continue; // both freed by the recording stub
        if (!Same(Norm(a, gameSide, mt ? 8 : kSize), Norm(b, oursSide, mt ? 8 : kSize)) || std::memcmp(a + (mt ? 8 : kSize), b + (mt ? 8 : kSize), mt ? kSize - 8 : 0))
            ctx.Fail("adapters %u (%s, count %u): object differs", i, names[kind], count);
        Release(a); Release(b);
    }
}
