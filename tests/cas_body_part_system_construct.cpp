// Differential test for nTSCASCore::cTSCASBodyPartSystem, lanes KB and KC: the constructor (0xcddf40) and the creator
// (0xcebc20); since lane KC ours is the real class (our vtables on its three faces). The allocator service is a recording
// one (each allocation's size, in order); the locked-object manager's constructor (0xcfdf50) and the filters' constructors
// (0xb64d00, 0xb64e00) are recording fakes; +0x158's (0xd0a970, a null store) runs as it is on the original side;
// InitCategoryLists, InitTargetSurfaceNames, the face's slots 10 and 9 and ResetKeys are stood in on both sides. The
// objects are compared word by word, every heap pointer by what it points to (block size, the map heads' links and flags,
// the filters' bytes); the vtable words by whether each is the side's own vtable of that face (ours never the game's).
#include "cas_body_part_system.h"
#include "code_patch.h"
#include "free_recorder.h"

#include <malloc.h>

#include <array>
#include <map>
#include <set>

namespace bpt_construct {

using namespace bpt;
using System = nTSCASCore::cTSCASBodyPartSystem;
using Vt = std::array<const void *, 8>;

// ---- a recording allocator service ------------------------------------------------------------------------------------

std::map<const void *, uint32_t> gServiceBlocks; // block -> size asked
void *__fastcall ServiceAllocate(void *, void *, uint32_t size)
{
    gEvents.push_back({0xa110c, size});
    auto *const p = static_cast<uint8_t *>(::operator new(size));
    std::memset(p, 0xa5, size);
    gServiceBlocks[p] = size;
    return p;
}
void __fastcall ServiceFree(void *, void *, void *p)
{
    gEvents.push_back({0xf3ee, gServiceBlocks.count(p) ? gServiceBlocks[p] : 0xeeu});
}
Vt gServiceVt;

// ---- fakes ---------------------------------------------------------------------------------------------------------------

void *__fastcall F_cfdf50(uint8_t *self, void *)
{
    gEvents.push_back({0xcfdf50, Rel(self)});
    const uint32_t words[3] = {0xcfdf5000, 0xcfdf5001, 0xcfdf5002};
    std::memcpy(self, words, sizeof(words)); // the manager is 0xc bytes
    return self;
}
// The filters' base constructor: words through +0x54 (the derived constructors store the vtables after it), a count at +4
// for the image's AddRef.
void *__fastcall F_b64d00(uint8_t *self, void *)
{
    gEvents.push_back({0xb64d00, self[0x10]});
    for (uint32_t k = 1; k < 0x15; ++k) {
        const uint32_t word = 0xb64d0000 | k;
        std::memcpy(self + 4 * k, &word, 4);
    }
    std::memset(self + 4, 0, 4);
    return self;
}
// The skin filter: a recording vtable of its own.
struct SkinFilter {
    const void *const *vt;
    uint32_t refs;
    uint8_t rest[0x7c - 8];
};
uint32_t __fastcall SkinAddRef(SkinFilter *self, void *)
{
    gEvents.push_back({0x5f01, self->refs});
    return ++self->refs;
}
uint32_t __fastcall SkinRelease(SkinFilter *self, void *)
{
    gEvents.push_back({0x5f02, self->refs});
    return --self->refs;
}
Vt gSkinVt;
void *__fastcall F_b64e00(SkinFilter *self, void *, uint32_t flag)
{
    gEvents.push_back({0xb64e00, flag});
    self->vt = gSkinVt.data();
    self->refs = 0;
    std::memset(self->rest, 0x5e, sizeof(self->rest));
    return self;
}
void __fastcall F_cf8fd0(void *self, void *) { gEvents.push_back({0xcf8fd0, Rel(self)}); }
void __fastcall F_cf9340(void *self, void *) { gEvents.push_back({0xcf9340, Rel(self)}); }
void __fastcall F_cf71f0(void *self, void *) { gEvents.push_back({0xcf71f0, Rel(self)}); }
void __fastcall F_cf7110(void *self, void *) { gEvents.push_back({0xcf7110, Rel(self)}); }
void __fastcall F_ce9690(void *self, void *) { gEvents.push_back({0xce9690, Rel(self)}); }
void *__cdecl ImageNew(size_t size) { return ::operator new(size); }
void __cdecl ImageSizedDelete(void *p, size_t size) { ::operator delete(p, size); }

// ---- the object described ------------------------------------------------------------------------------------------------

uint32_t Word(const void *p, size_t offset)
{
    uint32_t w;
    std::memcpy(&w, static_cast<const uint8_t *>(p) + offset, 4);
    return w;
}
void *Ptr(const void *p, size_t offset) { return reinterpret_cast<void *>(static_cast<uintptr_t>(Word(p, offset))); }

// A map head: its size as asked of the service, linked to itself, colour and nil flags.
void PushHead(std::vector<uint32_t> &out, const void *head)
{
    out.push_back(gServiceBlocks.count(head) ? gServiceBlocks[head] : 0xbad);
    out.push_back(Ptr(head, 0) == head && Ptr(head, 4) == head && Ptr(head, 8) == head);
    out.push_back(static_cast<const uint8_t *>(head)[0xc] | static_cast<const uint8_t *>(head)[0xd] << 8);
}
void PushBlock(std::vector<uint32_t> &out, const void *p, size_t bytes)
{
    out.push_back(p ? static_cast<uint32_t>(_msize(const_cast<void *>(p))) : 0);
    for (size_t k = 0; p && k < bytes; k += 4) out.push_back(Word(p, k));
}

// Words the constructor leaves alone (pads, +0x124), masked in the creator's test where the heap block's old contents
// show through; the constructor's test checks they are kept.
std::map<size_t, uint32_t> gUntouched; // offset -> the bits left alone

// The three vtable words (+0, +4, +0x1c) are the only bytes coded: kFaceCode | face when the word is the side's own vtable of
// that face (the game's on the original side; ours, which must not be the game's, on our side), else the word as is.
constexpr uint32_t kFaceOffsets[3] = {0, 4, 0x1c};
constexpr uint32_t kGameVTables[3] = {System::kVTable, System::kVTable4, System::kIfaceVTable};
constexpr uint32_t kFaceCode = 0xd0000000u;
struct SideTables {
    const void *vt[3];
};
SideTables GameTables()
{
    SideTables t;
    for (int f = 0; f < 3; ++f) t.vt[f] = GameAddress<void>(kGameVTables[f]);
    return t;
}
// Ours: what our constructor stores (read from an object it built on a scratch buffer; the test's stand-ins and allocator in
// place, its heap blocks left as the compared objects' are).
SideTables gOurs{};
void ReadOurs()
{
    alignas(8) static uint8_t scratch[0x168];
    gBase = scratch;
    nTSCASCore::cTSCASBodyPartSystemAdapters::Construct(scratch, nullptr);
    for (int f = 0; f < 3; ++f) gOurs.vt[f] = Ptr(scratch, kFaceOffsets[f]);
}

std::vector<uint32_t> Describe(const uint8_t *s, bool ours)
{
    std::vector<uint32_t> out;
    const SideTables game = GameTables();
    const SideTables &tables = ours ? gOurs : game;
    for (size_t off = 0; off < sizeof(System); off += 4) {
        out.push_back(static_cast<uint32_t>(off));
        int face = -1;
        for (int f = 0; f < 3; ++f) face = off == kFaceOffsets[f] ? f : face;
        if (face >= 0) {
            const void *const word = Ptr(s, off);
            const bool own = word == tables.vt[face] && (!ours || word != game.vt[face]);
            out.push_back(own ? kFaceCode | static_cast<uint32_t>(face) : Word(s, off));
            continue;
        }
        switch (off) {
        case 0x20: case 0x28: case 0x2c: case 0x30: case 0x3c: case 0x44: case 0x48: { // heap maps
            const void *tree = Ptr(s, off);
            out.push_back(static_cast<uint32_t>(_msize(const_cast<void *>(tree))));
            out.push_back(Word(tree, 4));
            PushHead(out, Ptr(tree, 0));
            break;
        }
        case 0x24: case 0x34: case 0x38: PushBlock(out, Ptr(s, off), 0xc); break; // vectors
        case 0x40: {                                                           // the float name map
            const void *map = Ptr(s, off);
            out.push_back(static_cast<uint32_t>(_msize(const_cast<void *>(map))));
            out.push_back(Word(map, 0));
            PushHead(out, Ptr(map, 4));
            out.push_back(Word(map, 8));
            break;
        }
        case 0x4c: case 0x54: case 0x5c: case 0x64: case 0x6c: case 0x74: case 0xa0: PushHead(out, Ptr(s, off)); break;
        case 0x15c: PushBlock(out, Ptr(s, off), 0x58); break;
        case 0x160: PushBlock(out, Ptr(s, off), 0x7c); break;
        case 0x164: PushBlock(out, Ptr(s, off), 0x78); break;
        default: out.push_back(Word(s, off) & ~(gUntouched.count(off) ? gUntouched[off] : 0u));
        }
    }
    return out;
}

struct Stands {
    shape::StandIn s1, s2, s3, s4, s5;
    Stands()
        : s1(0xcf8fd0, &System::InitCategoryLists, &F_cf8fd0), s2(0xcf9340, &System::InitTargetSurfaceNames, &F_cf9340),
          s3(0xcf71f0, &Iface::ClearTreeAt14, &F_cf71f0), s4(0xcf7110, &Iface::ResetState, &F_cf7110),
          s5(0xce9690, &System::ResetKeys, &F_ce9690)
    {
    }
};

void Install(difftest::Context &)
{
    gServiceVt = {};
    gServiceVt[3] = reinterpret_cast<const void *>(&ServiceAllocate);
    gServiceVt[4] = reinterpret_cast<const void *>(&ServiceFree);
    const void *vt = gServiceVt.data();
    std::memcpy(Env::Current()->object, &vt, 4);
    gSkinVt = {};
    gSkinVt[1] = reinterpret_cast<const void *>(&SkinAddRef);
    gSkinVt[2] = reinterpret_cast<const void *>(&SkinRelease);
}

void CompareSides(difftest::Context &ctx, const char *name, const uint8_t *a, const std::vector<Event> &ea, const uint8_t *b,
                  const std::vector<Event> &eb)
{
    ++ctx.cases;
    if (ea != eb) {
        size_t k = 0;
        while (k < ea.size() && k < eb.size() && ea[k] == eb[k]) ++k;
        if (k < ea.size() && k < eb.size()) {
            ctx.Fail("%s: calls differ at %zu: original %x %x %x, ours %x %x %x", name, k, ea[k][0], ea[k][1], ea[k][2], eb[k][0],
                     eb[k][1], eb[k][2]);
        } else {
            ctx.Fail("%s: calls differ (original %zu, ours %zu)", name, ea.size(), eb.size());
        }
    }
    const std::vector<uint32_t> da = Describe(a, false), db = Describe(b, true);
    for (size_t k = 0; k < da.size() && k < db.size(); ++k) {
        if (da[k] != db[k]) {
            uint32_t at = 0;
            for (size_t j = 0; j <= k; ++j)
                if (da[j] < sizeof(System) && (j == 0 || da[j] == da[j - 1] + 4 || true)) at = da[j];
            ctx.Fail("%s: object differs at item %zu (near +0x%x): original %x, ours %x", name, k, at, da[k], db[k]);
            break;
        }
    }
    if (da.size() != db.size()) ctx.Fail("%s: description sizes differ", name);
}

} // namespace bpt_construct

// Buffers filled with random bytes (bytes the constructor leaves alone, +0x124 and the pads, must stay) on both sides.
DIFF_TEST(cTSCASBodyPartSystem_Construct, 0xcddf40)
{
    using namespace bpt_construct;
    std::mt19937 rng(0xcddf40);
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xcddf40);
    Env env(ctx);
    Install(ctx);
    Redirect r1(ctx, 0xcfdf50, reinterpret_cast<const void *>(&F_cfdf50));
    // 0xd0a970 (+0x158 = null, 9 bytes) runs as it is: ours stores the null inline.
    Redirect r3(ctx, 0xb64d00, reinterpret_cast<const void *>(&F_b64d00));
    Redirect r4(ctx, 0xb64e00, reinterpret_cast<const void *>(&F_b64e00));
    Redirect newStand(ctx, 0x824632, reinterpret_cast<const void *>(&ImageNew));
    Redirect deleteStand(ctx, 0x824624, reinterpret_cast<const void *>(&ImageSizedDelete));
    Stands stands;
    ReadOurs();
    static uint8_t a[sizeof(System)], b[sizeof(System)];
    for (int i = 0; i < 200; ++i) {
        for (size_t k = 0; k < sizeof(a); ++k) a[k] = static_cast<uint8_t>(rng());
        std::memcpy(b, a, sizeof(a));
        gEvents.clear();
        gBase = a;
        void *const ra = orig(a);
        const std::vector<Event> ea = gEvents;
        gEvents.clear();
        gBase = b;
        void *const rb = nTSCASCore::cTSCASBodyPartSystemAdapters::Construct(b, nullptr); // our class placed (the hook's entry)
        const std::vector<Event> eb = gEvents;
        if (ra != a || rb != b) ctx.Fail("Construct: result is not this");
        CompareSides(ctx, "Construct", a, ea, b, eb);
    }
}

// The creator: the object on the heap from each side's operator new.
DIFF_TEST(cTSCASBodyPartSystem_Create, 0xcebc20)
{
    using namespace bpt_construct;
    auto orig = ctx.Original<System *(__cdecl *)()>(0xcebc20);
    Env env(ctx);
    Install(ctx);
    Redirect r1(ctx, 0xcfdf50, reinterpret_cast<const void *>(&F_cfdf50));
    // 0xd0a970 (+0x158 = null, 9 bytes) runs as it is: ours stores the null inline.
    Redirect r3(ctx, 0xb64d00, reinterpret_cast<const void *>(&F_b64d00));
    Redirect r4(ctx, 0xb64e00, reinterpret_cast<const void *>(&F_b64e00));
    Redirect newStand(ctx, 0x824632, reinterpret_cast<const void *>(&ImageNew));
    Redirect deleteStand(ctx, 0x824624, reinterpret_cast<const void *>(&ImageSizedDelete));
    Stands stands;
    ReadOurs();
    {
        // The bits the original constructor leaves alone: those that keep a fill of zeros and of ones.
        auto ctor = ctx.Original<void *(__thiscall *)(void *)>(0xcddf40);
        static uint8_t z[sizeof(System)], o[sizeof(System)];
        std::memset(z, 0, sizeof(z));
        std::memset(o, 0xff, sizeof(o));
        gBase = z;
        ctor(z);
        gBase = o;
        ctor(o);
        gUntouched.clear();
        for (size_t off = 0; off < sizeof(System); off += 4) {
            const uint32_t mask = ~Word(z, off) & Word(o, off);
            if (mask) gUntouched[off] = mask;
        }
    }
    for (int i = 0; i < 20; ++i) {
        gEvents.clear();
        gBase = nullptr;
        System *const a = orig();
        const std::vector<Event> ea = gEvents;
        gEvents.clear();
        System *const b = System::Create();
        const std::vector<Event> eb = gEvents;
        if (_msize(a) != sizeof(System) || _msize(b) != sizeof(System)) ctx.Fail("Create: block sizes %zu / %zu", _msize(a), _msize(b));
        // The fakes record offsets from gBase (null here): make them comparable by recording again relative to each object.
        std::vector<Event> ra = ea, rb = eb;
        for (auto &e : ra)
            if (e[0] == 0xcfdf50 || e[0] == 0xcf8fd0 || e[0] == 0xcf9340 || e[0] == 0xcf71f0 || e[0] == 0xcf7110 || e[0] == 0xce9690)
                e[1] -= static_cast<uint32_t>(reinterpret_cast<uintptr_t>(a));
        for (auto &e : rb)
            if (e[0] == 0xcfdf50 || e[0] == 0xcf8fd0 || e[0] == 0xcf9340 || e[0] == 0xcf71f0 || e[0] == 0xcf7110 || e[0] == 0xce9690)
                e[1] -= static_cast<uint32_t>(reinterpret_cast<uintptr_t>(b));
        CompareSides(ctx, "Create", reinterpret_cast<const uint8_t *>(a), ra, reinterpret_cast<const uint8_t *>(b), rb);
    }
}

// ==== Lane KC: the destructor and every slot of the class's three vtables =================================================
//
// - cTSCASBodyPartSystem_Destruct (0xcdfb50, deleting 0xce0970, +4 thunk 0xce08c2): both sides built by their constructor
//   (the fakes above), then filled alike: the category lists and the surface names and sets for a random age (our
//   InitCategoryLists / InitTargetSurfaceNames, verified, on both), up to three records of four strings (short or long) at
//   +0x24, the name (empty, short or long), +0x158 a heap map or null, fake counted filters (or null), the float name map's
//   vtable a recording one, and random heap members nulled. The image's erases, the stacks' destructor 0xcdf230 and the
//   locked-object manager's destructor 0xcfe450 are recording fakes on both sides (both call the image's); the face's slots
//   10 and 9 are stood in on both. Destroyed by the original (0xcdfb50, 0xce0970 or the thunk 0xce08c2, flags 0-3) and ours
//   through our +4 vtable's slot 3 (the compiler's thunk to our deleting destructor; nothing else reaches it). Compared:
//   the calls in order (allocator service frees with the block's size), the heap blocks freed (sorted sizes), whether the
//   object was freed, and with flags 0 the bytes left (the vtable words coded: the side's own base vtables).
// - cTSCASBodyPartSystem_Slots: every slot of our three vtables. The cITSCASBodyPartSystem slots that forward to a
//   hook-shaped body must reach that body (patched with an arity-matched recorder) once, with this = the +0x1c face and the
//   arguments unchanged, and the game's vtable must hold, in that slot, the RVA hooked to that body. The folded getters and
//   setters (55, 57, 61, 68, 69, 73, 75, 79), the three faces' QueryInterface (0xcf5f00 and the thunks 0xcf5f31, 0xcf5f39),
//   AddRef / Release, and the service's own slots are called on both sides' objects (the game's slot on the original's)
//   and compared. Init, Shutdown and the class's slots 13 / 14 (adapter-hooked, tested by their own difftests through the
//   adapters) are checked to sit where the game's vtable holds their RVA.

namespace bpt_construct {

constexpr uint32_t kDtor = 0xcdfb50, kDeleting = 0xce0970, kDeletingThunk = 0xce08c2;

uint32_t P(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// What a pointer the destructor's callees get is: an offset in the object, or 0x1000 | the offset of the member holding it.
uint32_t Holder(const void *p)
{
    if (p == nullptr) return 0xeee0;
    const uintptr_t d = reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(gBase);
    if (d < sizeof(System)) return static_cast<uint32_t>(d);
    for (uint32_t off = 0x20; off < sizeof(System); off += 4)
        if (Ptr(gBase, off) == p) return 0x1000 | off;
    return 0xeee1;
}

// The image's map erases (this, the tree, the root): logged, nothing erased (the maps are empty but for +0x158's fake).
template <uint32_t Rva> void __fastcall EraseRecord(void *self, void *, void *tree, void *root)
{
    const void *head = Ptr(tree, 0);
    gEvents.push_back({Rva, Holder(self), Holder(tree), root == head ? 1u : 2u});
}
void __fastcall StackDestroyRecord(void *self, void *) { gEvents.push_back({0xcdf230, Holder(self)}); }
void __fastcall LockedDestroyRecord(void *self, void *) { gEvents.push_back({0xcfe450, Holder(self)}); }

// The float name map's deleting destructor (slot 0 of a recording vtable).
void *__fastcall FloatMapDelete(void *self, void *, uint32_t flags)
{
    gEvents.push_back({0xf10a7, Holder(self), flags});
    return self;
}
Vt gFloatMapVt;

// Counted fakes for the three filters.
struct FakeCounted {
    const void *const *vt;
    uint32_t label;
};
uint32_t __fastcall FakeAddRef(FakeCounted *self, void *)
{
    gEvents.push_back({0xfe0, self->label});
    return 2;
}
uint32_t __fastcall FakeRelease(FakeCounted *self, void *)
{
    gEvents.push_back({0xfe1, self->label});
    return 1;
}
Vt gCountedVt;
FakeCounted gFilters[2][3];

// The rz base's vtable words our destructor leaves at +0 and +4 (an rz::cRZSystemService built and destroyed on a buffer).
SideTables OurBaseTables()
{
    alignas(8) static uint8_t buffer[sizeof(rz::cRZSystemService)];
    auto *s = ::new (buffer) rz::cRZSystemService(1, 2);
    s->rz::cRZSystemService::~cRZSystemService();
    SideTables t{};
    t.vt[0] = Ptr(buffer, 0);
    t.vt[1] = Ptr(buffer, 4);
    return t;
}

struct DestroyWorld {
    uint32_t age;
    unsigned records;
    unsigned nameKind; // 0 empty, 1 short, 2 long
    uint32_t nullMask; // bits: the heap members below, then the three filters
    bool tree158;
    unsigned route;    // 0 the destructor, 1 the deleting destructor, 2 its +4 thunk
    uint32_t flags;
};
constexpr uint32_t kHeapMembers[] = {0x20, 0x24, 0x28, 0x2c, 0x30, 0x34, 0x38, 0x3c, 0x40, 0x44, 0x48};
constexpr uint32_t kFilterMembers[] = {0x15c, 0x160, 0x164};

std::string RecordText(std::mt19937 &rng)
{
    std::string s = "rec" + std::to_string(rng() % 100);
    if (rng() & 1) s += " a text long enough for a heap buffer " + std::to_string(rng() % 1000);
    return s;
}

void Fill(System *sys, int side, const DestroyWorld &w, uint32_t seed)
{
    gBase = reinterpret_cast<const uint8_t *>(sys);
    sys->mAge = w.age;
    sys->InitCategoryLists();
    sys->InitTargetSurfaceNames();
    std::mt19937 rng(seed);
    for (unsigned i = 0; i < w.records; ++i) {
        nTSCASCore::SurfaceRecord r;
        for (auto &t : r.mText) t = RecordText(rng);
        sys->mRecords->push_back(r);
    }
    if (w.nameKind == 1) sys->mName.mText.mString.assign("face");
    if (w.nameKind == 2) sys->mName.mText.mString.assign("a system name long enough to live on the heap");
    if (w.tree158) sys->mTree158 = nTSCASCore::NewHeapTree(0x20);
    auto *const floatMap = static_cast<uint8_t *>(sys->mFloatNameMap);
    const void *fvt = gFloatMapVt.data();
    std::memcpy(floatMap, &fvt, 4);
    void **filters[3] = {&sys->mSkinBaseFilter, &sys->mSkinFilter, &sys->mOverlayFilter};
    for (int f = 0; f < 3; ++f) {
        gFilters[side][f] = {gCountedVt.data(), 0xf00u + static_cast<uint32_t>(f)};
        *filters[f] = &gFilters[side][f];
    }
    auto *const bytes = reinterpret_cast<uint8_t *>(sys);
    for (unsigned i = 0; i < 11; ++i)
        if (w.nullMask & (1u << i)) std::memset(bytes + kHeapMembers[i], 0, 4);
    for (unsigned i = 0; i < 3; ++i)
        if (w.nullMask & (1u << (11 + i))) std::memset(bytes + kFilterMembers[i], 0, 4);
}

std::vector<uint32_t> Words(const void *p)
{
    std::vector<uint32_t> out;
    for (uint32_t off = 0; off < sizeof(System); off += 4) out.push_back(Word(p, off));
    return out;
}

// The bytes left after destruction: the vtable words coded (the side's expected vtable of that face), every other word as
// "unchanged" (0xe1), "zero" (0xe0), or, where the two sides held different values before (heap addresses), "changed"
// (0xe2); else the word itself.
std::vector<uint32_t> Remains(const std::vector<uint32_t> &before, const std::vector<uint32_t> &after,
                              const std::vector<uint32_t> &otherBefore, const SideTables &expect)
{
    std::vector<uint32_t> out;
    for (size_t k = 0; k < after.size(); ++k) {
        const uint32_t off = static_cast<uint32_t>(4 * k);
        int face = -1;
        for (int f = 0; f < 3; ++f) face = off == kFaceOffsets[f] ? f : face;
        if (face >= 0) {
            out.push_back(after[k] == P(expect.vt[face]) ? kFaceCode | static_cast<uint32_t>(face) : after[k]);
            continue;
        }
        if (off >= 0xb8 && off < 0xd0) {
            // The name's std::string (text or pointer, size, capacity): the original's inline destructor resets it to empty
            // after freeing; our compiler drops those stores to a member whose lifetime has ended (dead bytes).
            out.push_back(0xd5);
            continue;
        }
        if (after[k] == before[k]) out.push_back(0xe1);
        else if (after[k] == 0) out.push_back(0xe0);
        else if (before[k] != otherBefore[k]) out.push_back(0xe2);
        else out.push_back(after[k]);
    }
    return out;
}

std::vector<size_t> FreedSizes(const difftest::FreeRecorder &rec)
{
    std::vector<size_t> sizes(rec.Count() + 1);
    sizes.resize(rec.SortedSizes(sizes.data(), sizes.size()));
    return sizes;
}

void CompareEvents(difftest::Context &ctx, uint32_t seed, const std::vector<Event> &ea, const std::vector<Event> &eb)
{
    if (ea == eb) return;
    size_t k = 0;
    while (k < ea.size() && k < eb.size() && ea[k] == eb[k]) ++k;
    if (k < ea.size() && k < eb.size()) {
        ctx.Fail("Destruct seed %u: call %zu differs: original %x %x %x %x, ours %x %x %x %x", seed, k, ea[k][0], ea[k][1], ea[k][2],
                 ea[k][3], eb[k][0], eb[k][1], eb[k][2], eb[k][3]);
    } else {
        ctx.Fail("Destruct seed %u: calls differ (original %zu, ours %zu)", seed, ea.size(), eb.size());
    }
}

void *NewObjectBlock()
{
    void *p = ::operator new(sizeof(System));
    std::memset(p, 0xcd, sizeof(System));
    return p;
}

} // namespace bpt_construct

DIFF_TEST(cTSCASBodyPartSystem_Destruct, 0xcdfb50)
{
    // Also reaches 0xce0970 (the deleting destructor) and 0xce08c2 (its +4 thunk) on the original side.
    using namespace bpt_construct;
    auto ctor = ctx.Original<void *(__thiscall *)(void *)>(0xcddf40);
    auto dtor = ctx.Original<void(__thiscall *)(void *)>(kDtor);
    auto deleting = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kDeleting);
    auto thunk = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kDeletingThunk);
    Env env(ctx);
    Install(ctx);
    gFloatMapVt = {};
    gFloatMapVt[0] = reinterpret_cast<const void *>(&FloatMapDelete);
    gCountedVt = {};
    gCountedVt[1] = reinterpret_cast<const void *>(&FakeAddRef);
    gCountedVt[2] = reinterpret_cast<const void *>(&FakeRelease);
    Redirect r1(ctx, 0xcfdf50, reinterpret_cast<const void *>(&F_cfdf50));
    Redirect r3(ctx, 0xb64d00, reinterpret_cast<const void *>(&F_b64d00));
    Redirect r4(ctx, 0xb64e00, reinterpret_cast<const void *>(&F_b64e00));
    Redirect newStand(ctx, 0x824632, reinterpret_cast<const void *>(&ImageNew));
    Redirect deleteStand(ctx, 0x824624, reinterpret_cast<const void *>(&ImageSizedDelete));
    Redirect e1(ctx, 0xcdbf10, reinterpret_cast<const void *>(&EraseRecord<0xcdbf10>));
    Redirect e2(ctx, 0xcdb5e0, reinterpret_cast<const void *>(&EraseRecord<0xcdb5e0>));
    Redirect e3(ctx, 0xcdb4a0, reinterpret_cast<const void *>(&EraseRecord<0xcdb4a0>));
    Redirect e4(ctx, 0xcdb400, reinterpret_cast<const void *>(&EraseRecord<0xcdb400>));
    Redirect e5(ctx, 0xcdb970, reinterpret_cast<const void *>(&EraseRecord<0xcdb970>));
    Redirect e6(ctx, 0xcdb710, reinterpret_cast<const void *>(&EraseRecord<0xcdb710>));
    Redirect e7(ctx, 0xcdb540, reinterpret_cast<const void *>(&EraseRecord<0xcdb540>));
    Redirect e8(ctx, 0xd0a8e0, reinterpret_cast<const void *>(&EraseRecord<0xd0a8e0>));
    Redirect e9(ctx, 0xcdbc10, reinterpret_cast<const void *>(&EraseRecord<0xcdbc10>));
    Redirect e10(ctx, 0xcdc280, reinterpret_cast<const void *>(&EraseRecord<0xcdc280>));
    Redirect e11(ctx, 0xcdc300, reinterpret_cast<const void *>(&EraseRecord<0xcdc300>));
    Redirect stacks(ctx, 0xcdf230, reinterpret_cast<const void *>(&StackDestroyRecord));
    Redirect locked(ctx, 0xcfe450, reinterpret_cast<const void *>(&LockedDestroyRecord));
    {
        Stands stands;
        ReadOurs();
    }
    const SideTables ourBase = OurBaseTables();
    SideTables expectOriginal, expectOurs;
    expectOriginal.vt[0] = GameAddress<void>(0xda3058); // cRZSystemService's
    expectOriginal.vt[1] = GameAddress<void>(0xda2fec); // cRZUnknownMT's
    expectOriginal.vt[2] = GameAddress<void>(System::kIfaceVTable);
    expectOurs.vt[0] = ourBase.vt[0];
    expectOurs.vt[1] = ourBase.vt[1];
    expectOurs.vt[2] = gOurs.vt[2];
    if (expectOurs.vt[0] == expectOriginal.vt[0] || expectOurs.vt[1] == expectOriginal.vt[1] || expectOurs.vt[2] == expectOriginal.vt[2])
        ctx.Fail("Destruct: our base vtables are the game's");

    std::mt19937 rng(0xcdfb50);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        DestroyWorld w;
        w.age = rng() % 5;
        w.records = rng() % 4;
        w.nameKind = rng() % 3;
        w.nullMask = (rng() % 3 == 0) ? (rng() & 0x3fff) : 0;
        w.tree158 = (rng() & 1) != 0;
        w.route = seed % 3;
        w.flags = rng() % 4;
        if (w.route == 0) w.flags = 0;

        void *objects[2] = {NewObjectBlock(), NewObjectBlock()};
        {
            Stands stands;
            gBase = static_cast<uint8_t *>(objects[0]);
            ctor(objects[0]);
            gBase = static_cast<uint8_t *>(objects[1]);
            nTSCASCore::cTSCASBodyPartSystemAdapters::Construct(objects[1], nullptr);
        }
        for (int side = 0; side < 2; ++side) Fill(static_cast<System *>(objects[side]), side, w, seed);
        const std::vector<uint32_t> before[2] = {Words(objects[0]), Words(objects[1])};

        std::vector<Event> events[2];
        std::vector<size_t> freed[2];
        bool selfFreed[2] = {};
        std::vector<uint32_t> after[2];
        shape::StandIn s3(0xcf71f0, &Iface::ClearTreeAt14, &F_cf71f0), s4(0xcf7110, &Iface::ResetState, &F_cf7110);
        for (int side = 0; side < 2; ++side) {
            void *const o = objects[side];
            gBase = static_cast<uint8_t *>(o);
            gEvents.clear();
            gEvents.reserve(4096); // no allocation (nor free) inside the recorded call
            difftest::FreeRecorder rec;
            if (side == 0) {
                if (w.route == 0) dtor(o);
                else if (w.route == 1) deleting(o, w.flags);
                else thunk(static_cast<uint8_t *>(o) + 4, w.flags);
            } else {
                // Our +4 vtable's slot 3: the compiler's thunk to our deleting destructor (scalar, flags as given).
                void **vt = *reinterpret_cast<void ***>(static_cast<uint8_t *>(o) + 4);
                reinterpret_cast<void *(__thiscall *)(void *, uint32_t)>(vt[3])(static_cast<uint8_t *>(o) + 4, w.flags);
            }
            rec.Stop();
            if (rec.Overflowed()) ctx.Fail("Destruct seed %u: the free recorder overflowed", seed);
            events[side] = gEvents;
            freed[side] = FreedSizes(rec);
            selfFreed[side] = rec.Freed(o);
            if (!(w.flags & 1)) after[side] = Words(o);
        }
        ++ctx.cases;
        CompareEvents(ctx, seed, events[0], events[1]);
        if (freed[0] != freed[1])
            ctx.Fail("Destruct seed %u route %u flags %u: heap frees differ (%zu, %zu)", seed, w.route, w.flags, freed[0].size(),
                     freed[1].size());
        if (selfFreed[0] != selfFreed[1] || selfFreed[0] != ((w.flags & 1) != 0))
            ctx.Fail("Destruct seed %u: object freed %d / %d (flags %u)", seed, int(selfFreed[0]), int(selfFreed[1]), w.flags);
        if (!(w.flags & 1)) {
            const auto ra = Remains(before[0], after[0], before[1], expectOriginal);
            const auto rb = Remains(before[1], after[1], before[0], expectOurs);
            for (size_t k = 0; k < ra.size(); ++k) {
                if (ra[k] != rb[k]) {
                    ctx.Fail("Destruct seed %u route %u: left at +0x%zx: original %x, ours %x", seed, w.route, 4 * k, ra[k], rb[k]);
                    break;
                }
            }
            ::operator delete(objects[0]);
            ::operator delete(objects[1]);
        }
        if (!ctx.failures.empty()) return;
    }
}

namespace bpt_construct {

// ---- calls with N stack words, and recorders of them -------------------------------------------------------------------

using shape::StackWord;
constexpr unsigned kMaxWords = 10; // slot 24 (ApplyPart, ret 0x28)

template <unsigned... I> uint32_t CallWords(void *fn, void *self, const uint32_t *a, std::integer_sequence<unsigned, I...>)
{
    return reinterpret_cast<uint32_t(__thiscall *)(void *, StackWord<I>...)>(fn)(self, a[I]...);
}
template <unsigned... I> float CallWordsFloat(void *fn, void *self, const uint32_t *a, std::integer_sequence<unsigned, I...>)
{
    return reinterpret_cast<float(__thiscall *)(void *, StackWord<I>...)>(fn)(self, a[I]...);
}
template <unsigned N> uint32_t CallN(void *fn, void *self, const uint32_t *a)
{
    return CallWords(fn, self, a, std::make_integer_sequence<unsigned, N>());
}
template <unsigned N> uint32_t CallFloatN(void *fn, void *self, const uint32_t *a)
{
    const float f = CallWordsFloat(fn, self, a, std::make_integer_sequence<unsigned, N>());
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

int gHits = 0;
const void *gSelf = nullptr;
uint32_t gArgs[kMaxWords];
unsigned gWords = 0;
constexpr uint32_t kRecorded = 0x5a5a5a81;
constexpr float kRecordedFloat = 1.5f;

template <unsigned... I> void Note(void *self, StackWord<I>... w)
{
    ++gHits;
    gSelf = self;
    const uint32_t a[] = {0u, w...};
    for (unsigned i = 0; i < sizeof...(I); ++i) gArgs[i] = a[i + 1];
    gWords = sizeof...(I);
}
template <unsigned... I> uint32_t __fastcall RecordOf(void *self, void *, StackWord<I>... w)
{
    Note<I...>(self, w...);
    return kRecorded;
}
template <unsigned... I> float __fastcall RecordFloatOf(void *self, void *, StackWord<I>... w)
{
    Note<I...>(self, w...);
    return kRecordedFloat;
}
template <unsigned... I> void *RecordAddress(std::integer_sequence<unsigned, I...>) { return reinterpret_cast<void *>(&RecordOf<I...>); }
template <unsigned... I> void *RecordFloatAddress(std::integer_sequence<unsigned, I...>)
{
    return reinterpret_cast<void *>(&RecordFloatOf<I...>);
}

using Caller = uint32_t (*)(void *, void *, const uint32_t *);
template <unsigned... W> uint32_t Call(void *fn, void *self, unsigned words, bool isFloat, const uint32_t *a, std::integer_sequence<unsigned, W...>)
{
    const Caller ints[] = {&CallN<W>...};
    const Caller floats[] = {&CallFloatN<W>...};
    return (isFloat ? floats : ints)[words](fn, self, a);
}
uint32_t CallSlot(void *fn, void *self, unsigned words, bool isFloat, const uint32_t *a)
{
    return Call(fn, self, words, isFloat, a, std::make_integer_sequence<unsigned, kMaxWords + 1>());
}
template <unsigned... W> void *Recorder(unsigned words, bool isFloat, std::integer_sequence<unsigned, W...>)
{
    void *const ints[] = {RecordAddress(std::make_integer_sequence<unsigned, W>())...};
    void *const floats[] = {RecordFloatAddress(std::make_integer_sequence<unsigned, W>())...};
    return (isFloat ? floats : ints)[words];
}
void *RecorderFor(unsigned words, bool isFloat) { return Recorder(words, isFloat, std::make_integer_sequence<unsigned, kMaxWords + 1>()); }

// ---- the forwarding slots of cITSCASBodyPartSystem ---------------------------------------------------------------------

enum Kind : uint8_t { kWord, kByte, kFloat, kVoid };

struct Forward {
    unsigned slot;
    uint32_t rva;  // what the game's vtable holds in the slot (the RVA hooked to the body)
    void *body;    // the hook-shaped body
    unsigned words;
    Kind kind;
    uint32_t masks[kMaxWords]; // the bits of each argument word the callee reads (a byte or a whole word)
};
template <class A> constexpr uint32_t MaskOf()
{
    if constexpr (sizeof(A) >= 4) return ~0u;
    else return (1u << (8 * sizeof(A))) - 1;
}
template <class R> constexpr Kind KindOf()
{
    if constexpr (std::is_void_v<R>) return kVoid;
    else if constexpr (std::is_same_v<R, float>) return kFloat;
    else if constexpr (sizeof(R) == 1) return kByte;
    else return kWord;
}
template <class R, class... A> Forward F(unsigned slot, uint32_t rva, R (Iface::*m)(A...))
{
    static_assert(sizeof...(A) <= kMaxWords, "words");
    Forward f{slot, rva, shape::MemberCode(m), static_cast<unsigned>(sizeof...(A)), KindOf<R>(), {}};
    const uint32_t masks[] = {MaskOf<A>()..., 0u};
    for (unsigned i = 0; i < sizeof...(A); ++i) f.masks[i] = masks[i];
    return f;
}

std::vector<Forward> Forwards()
{
    return {
        F(3, 0xcf4f50, &Iface::ClearData), F(4, 0xcf3750, &Iface::LoadFaceData), F(5, 0xce09a0, &Iface::ImportPartData),
        F(6, 0xcf6ee0, &Iface::EraseAgeName), F(7, 0xcf3280, &Iface::HasAgeName), F(8, 0xcf3220, &Iface::HasActiveMorph),
        F(9, 0xcf7110, &Iface::ResetState), F(10, 0xcf71f0, &Iface::ClearTreeAt14), F(11, 0xceaae0, &Iface::GetText),
        F(12, 0xcf74c0, &Iface::SaveSnapshot), F(13, 0xcf73e0, &Iface::RestoreSnapshot), F(14, 0xcfab70, &Iface::PopEntry),
        F(15, 0xcf6820, &Iface::RedoEntry), F(16, 0xcf6e80, &Iface::ClearStacks), F(17, 0xcf7370, &Iface::ClearPartWeight),
        F(18, 0xcfab60, &Iface::StackCount), F(19, 0xcf6810, &Iface::RedoStackCount), F(20, 0xce32b0, &Iface::RefreshPart),
        F(21, 0xce3280, &Iface::VisitIds), F(22, 0xcfac20, &Iface::ApplyAll), F(23, 0xcfade0, &Iface::ApplyAllPasses),
        F(24, 0xcfaec0, &Iface::ApplyPart), F(25, 0xcfb830, &Iface::RebuildPartMaterials),
        F(26, 0xcea630, &Iface::CreateSkinMaterial), F(27, 0xcea2f0, &Iface::CreateHairMaterial),
        F(28, 0xcf9f30, &Iface::ApplyOutfitProperties), F(29, 0xcf8700, &Iface::CopyFrom),
        F(30, 0xce5a50, &Iface::ApplyToAllParts), F(31, 0xce7ee0, &Iface::RefreshGeometry),
        F(32, 0xcf59e0, &Iface::PushMarker), F(33, 0xcf5ce0, &Iface::PushKind5),
        F(34, 0xcf3360, &Iface::ItemsMatchNewestKind5), F(35, 0xcf5da0, &Iface::PushKind6),
        F(36, 0xcf3410, &Iface::MatchesNewestKind6), F(37, 0xcf5a80, &Iface::ApplyPartGuid),
        F(38, 0xcf5ca0, &Iface::ApplyGuidText), F(39, 0xcf7220, &Iface::ClearGuidPartWeights),
        F(40, 0xcf45e0, &Iface::IsGuidApplied), F(41, 0xcf81d0, &Iface::ApplyModifierTexts),
        F(42, 0xcf2180, &Iface::ExportModifierTexts), F(43, 0xcf80e0, &Iface::ApplyNamesValue0B),
        F(44, 0xcf1f70, &Iface::CollectNamesValue0B), F(45, 0xcf8070, &Iface::ApplyNamesValue5),
        F(46, 0xcf1d90, &Iface::CollectNamesValue5), F(47, 0xcf5e50, &Iface::PushKind0), F(48, 0xcf54e0, &Iface::AddMorph),
        F(49, 0xcf5770, &Iface::AddNamedWeight), F(50, 0xcf4310, &Iface::SumNamedWeights),
        F(51, 0xce1690, &Iface::TopMorphWeight), F(52, 0xcf43d0, &Iface::GetPart),
        F(53, 0xcf4130, &Iface::CollectLinkedParts), F(54, 0xb81a30, &Iface::StoreBlockB8),
        F(56, 0xcf80c0, &Iface::StoreBlockD8), F(58, 0xcf8140, &Iface::StoreKey), F(59, 0xcf2160, &Iface::GetKey),
        F(60, 0xcf81c0, &Iface::StoreDword108), F(62, 0xcf7f20, &Iface::StoreByte10C),
        F(63, 0xce1ff0, &Iface::CheckRegionPairs), F(64, 0xcf76a0, &Iface::VisitIdsF7),
        F(65, 0xcece60, &Iface::VisitIdsEA), F(66, 0xced0e0, &Iface::CollectRecordTexts),
        F(67, 0xcf7ef0, &Iface::StoreByte129), F(70, 0xcf3720, &Iface::ByteGate120), F(71, 0xcf85e0, &Iface::SetAge),
        F(72, 0xcf3210, &Iface::AgeMask), F(74, 0xcf7f00, &Iface::StoreBlockC8),
        F(76, 0xce9840, &Iface::ExportPointDeltas), F(77, 0xce9b30, &Iface::ExportPointPositions),
        F(78, 0xce9e20, &Iface::ExportPositionsByUv), F(80, 0xce16d0, &Iface::AverageWeightedOffset),
        F(81, 0xce0cc0, &Iface::ApplyToChildren), F(82, 0xce0e60, &Iface::ApplyBoneAdjusts),
        F(83, 0xce3180, &Iface::GetNodeTransform), F(84, 0xceac20, &Iface::ExportFaceObj),
        F(85, 0xcee3f0, &Iface::ReportVertexIds), F(86, 0xcee680, &Iface::ReportGroupVertexIds),
        F(87, 0xcee930, &Iface::ReportRegionMasks), F(88, 0xced540, &Iface::ReportBones),
    };
}

// ---- the slots called on both sides ------------------------------------------------------------------------------------

struct Direct {
    unsigned face; // index into kFaceOffsets
    unsigned slot;
    uint32_t rva;  // the game's slot
    unsigned words;
    Kind kind;
    bool pointer;  // the result is a pointer: compared as an offset from the face
};
// The folded ITS slots (lane KC): 55 0x418bd0, 57 0x60d700, 61 0x418b60, 68 0x4e7f40, 69 0x1cc650, 73 0x4a6cf0 (4 bytes),
// 75 0x931190, 79 0x16a930 (3 bytes); the service's own slots on the +0 face and the cRZUnknownMT slots on the +4 face;
// AddRef / Release on every face.
const Direct kDirect[] = {
    {2, 55, 0x418bd0, 0, kWord, true}, {2, 57, 0x60d700, 0, kWord, true}, {2, 61, 0x418b60, 0, kWord, false},
    {2, 68, 0x4e7f40, 1, kVoid, false}, {2, 69, 0x1cc650, 0, kByte, false}, {2, 73, 0x4a6cf0, 0, kWord, true},
    {2, 75, 0x931190, 0, kWord, true}, {2, 79, 0x16a930, 4, kVoid, false},
    {0, 1, 0, 0, kWord, false}, {0, 2, 0, 0, kWord, false}, {1, 1, 0, 0, kWord, false}, {1, 2, 0, 0, kWord, false},
    {2, 1, 0, 0, kWord, false}, {2, 2, 0, 0, kWord, false},
    {0, 3, 0, 0, kWord, false}, {0, 4, 0, 1, kVoid, false}, {0, 5, 0, 0, kWord, false}, {0, 6, 0, 0, kByte, false},
    {0, 7, 0, 1, kVoid, false}, {0, 10, 0, 1, kByte, false}, {0, 11, 0, 1, kByte, false}, {0, 12, 0, 0, kWord, false},
    {1, 4, 0, 0, kWord, false}, {1, 5, 0, 0, kWord, false},
};

void *Face(void *object, unsigned face) { return static_cast<uint8_t *>(object) + kFaceOffsets[face]; }
void **VtOf(void *object, unsigned face) { return *reinterpret_cast<void ***>(Face(object, face)); }

// The slot of the primary vtable a virtual member of ours sits in (read from its vcall thunk).
template <class M> unsigned SlotOfMember(M m)
{
    uint32_t offset = ~0u;
    return shape::VcallThunkOffset(shape::MemberCodeWord(m), &offset) ? offset / 4 : ~0u;
}

} // namespace bpt_construct

DIFF_TEST(cTSCASBodyPartSystem_Slots, 0xcf5f39)
{
    // Also covers the +4 QueryInterface thunk 0xcf5f31 and the folded slots 0x60d700, 0x4e7f40, 0x1cc650, 0x931190 (and
    // 0x418bd0, 0x418b60, 0x4a6cf0, 0x16a930).
    using namespace bpt_construct;
    Env env(ctx);
    Install(ctx);
    Redirect r1(ctx, 0xcfdf50, reinterpret_cast<const void *>(&F_cfdf50));
    Redirect r3(ctx, 0xb64d00, reinterpret_cast<const void *>(&F_b64d00));
    Redirect r4(ctx, 0xb64e00, reinterpret_cast<const void *>(&F_b64e00));
    Redirect newStand(ctx, 0x824632, reinterpret_cast<const void *>(&ImageNew));
    Redirect deleteStand(ctx, 0x824624, reinterpret_cast<const void *>(&ImageSizedDelete));
    void *a = NewObjectBlock(), *b = NewObjectBlock();
    {
        Stands stands;
        gBase = static_cast<uint8_t *>(a);
        ctx.Original<void *(__thiscall *)(void *)>(0xcddf40)(a);
        gBase = static_cast<uint8_t *>(b);
        nTSCASCore::cTSCASBodyPartSystemAdapters::Construct(b, nullptr);
    }
    const unsigned kSlotCounts[3] = {15, 6, 89};
    std::vector<bool> seen[3];
    for (unsigned f = 0; f < 3; ++f) seen[f].assign(kSlotCounts[f], false);
    for (unsigned f = 0; f < 3; ++f)
        if (VtOf(b, f) == GameAddress<void *>(kGameVTables[f])) ctx.Fail("face %u: our object carries the game's vtable", f);

    // The forwarding slots: reached once, this = the +0x1c face, the arguments unchanged.
    for (const Forward &fw : Forwards()) {
        ++ctx.cases;
        seen[2][fw.slot] = true;
        void **game = GameAddress<void *>(System::kIfaceVTable);
        if (game[fw.slot] != GameAddress<void>(fw.rva))
            ctx.Fail("ITS slot %u: the game's vtable holds %x, not %x", fw.slot, P(game[fw.slot]) - P(GameAddress<void>(0)), fw.rva);
        uint32_t args[kMaxWords];
        for (unsigned i = 0; i < kMaxWords; ++i) args[i] = 0x01020304u * (i + 1) + fw.slot * 0x101u;
        gHits = 0;
        gSelf = nullptr;
        uint32_t r;
        {
            shape::CodePatch patch(fw.body, RecorderFor(fw.words, fw.kind == kFloat));
            r = CallSlot(VtOf(b, 2)[fw.slot], Face(b, 2), fw.words, fw.kind == kFloat, args);
        }
        if (gHits != 1) {
            ctx.Fail("ITS slot %u: the body was reached %d times", fw.slot, gHits);
            continue;
        }
        if (gSelf != Face(b, 2)) ctx.Fail("ITS slot %u: the body got this = object + %x", fw.slot, P(gSelf) - P(b));
        bool same = gWords == fw.words;
        for (unsigned i = 0; same && i < fw.words; ++i) same = ((gArgs[i] ^ args[i]) & fw.masks[i]) == 0;
        if (!same) ctx.Fail("ITS slot %u: the arguments changed on the way", fw.slot);
        uint32_t expect = kRecorded;
        if (fw.kind == kFloat) std::memcpy(&expect, &kRecordedFloat, 4);
        if (fw.kind == kByte && (r & 0xff) != (expect & 0xff)) ctx.Fail("ITS slot %u: result %x", fw.slot, r);
        if ((fw.kind == kWord || fw.kind == kFloat) && r != expect) ctx.Fail("ITS slot %u: result %x", fw.slot, r);
    }

    // The slots called on both sides.
    std::mt19937 rng(0xcf5f39);
    void *objects[2] = {a, b};
    for (int round = 0; round < 4; ++round) {
        // The fields the folded slots and the service's read, the same on both sides.
        uint8_t fields[0x148 - 0xd4];
        for (auto &x : fields) x = static_cast<uint8_t>(rng());
        uint32_t service[4] = {static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng()),
                               static_cast<uint32_t>(rng())};
        for (void *o : objects) {
            std::memcpy(static_cast<uint8_t *>(o) + 0xd4, fields, sizeof(fields));
            std::memcpy(static_cast<uint8_t *>(o) + 0xc, service, sizeof(service));
            std::memset(static_cast<uint8_t *>(o) + 8, 0, 4);
            *(static_cast<uint8_t *>(o) + 8) = 5; // the count, well above zero
            if (round & 1) *(static_cast<uint8_t *>(o) + 0x13c) = 0x80; // slot 69 hands out the raw byte
        }
        for (const Direct &d : kDirect) {
            ++ctx.cases;
            seen[d.face][d.slot] = true;
            void **game = GameAddress<void *>(kGameVTables[d.face]);
            if (d.rva && game[d.slot] != GameAddress<void>(d.rva)) ctx.Fail("face %u slot %u: the game's vtable does not hold %x", d.face, d.slot, d.rva);
            uint32_t args[kMaxWords];
            for (unsigned i = 0; i < kMaxWords; ++i) args[i] = static_cast<uint32_t>(rng());
            uint32_t results[2];
            std::vector<uint32_t> words[2];
            for (int side = 0; side < 2; ++side) {
                void *o = objects[side];
                results[side] = CallSlot(VtOf(o, d.face)[d.slot], Face(o, d.face), d.words, false, args);
                if (d.pointer) results[side] -= P(Face(o, d.face));
                for (uint32_t off = 8; off < 0x1c; off += 4) words[side].push_back(Word(o, off));
                for (uint32_t off = 0xd4; off < 0x148; off += 4) words[side].push_back(Word(o, off));
            }
            if (d.kind == kWord && results[0] != results[1]) ctx.Fail("face %u slot %u: result %x / %x", d.face, d.slot, results[0], results[1]);
            if (d.kind == kByte && (results[0] & 0xff) != (results[1] & 0xff))
                ctx.Fail("face %u slot %u: result %x / %x", d.face, d.slot, results[0] & 0xff, results[1] & 0xff);
            if (words[0] != words[1]) ctx.Fail("face %u slot %u: the object's fields differ after the call", d.face, d.slot);
        }
        // QueryInterface on the three faces (0xcf5f00 and its thunks 0xcf5f31, 0xcf5f39 on the original side).
        const uint32_t kQueryRva[3] = {0xcf5f00, 0xcf5f31, 0xcf5f39};
        const uint32_t riids[] = {nTSCASCore::cITSCASBodyPartSystem::kIid, cIGZSystemService::kIID, 1u, static_cast<uint32_t>(rng())};
        for (unsigned f = 0; f < 3; ++f) {
            seen[f][0] = true;
            if (GameAddress<void *>(kGameVTables[f])[0] != GameAddress<void>(kQueryRva[f])) ctx.Fail("face %u: QueryInterface is not %x", f, kQueryRva[f]);
            for (uint32_t riid : riids) {
                ++ctx.cases;
                uint32_t results[2], outs[2], counts[2];
                for (int side = 0; side < 2; ++side) {
                    void *o = objects[side];
                    void *out = reinterpret_cast<void *>(uintptr_t(0xfeedface));
                    const uint32_t qargs[2] = {riid, P(&out)};
                    results[side] = CallSlot(VtOf(o, f)[0], Face(o, f), 2, false, qargs) & 0xff;
                    outs[side] = out == reinterpret_cast<void *>(uintptr_t(0xfeedface)) ? 0xfeedface : P(out) - P(o);
                    counts[side] = Word(o, 8);
                }
                if (results[0] != results[1] || outs[0] != outs[1] || counts[0] != counts[1])
                    ctx.Fail("face %u QueryInterface %x: original %x +%x count %u, ours %x +%x count %u", f, riid, results[0], outs[0],
                             counts[0], results[1], outs[1], counts[1]);
            }
        }
    }
    // The QueryInterface adapter (the hook's entry at 0xcf5f00) on the original's object whose vtables are copies with a trap
    // in slot 0 of each face (an adapter that dispatched would land there): the same as the original called directly.
    {
        static int traps = 0;
        std::vector<void *> copies[3];
        void **saved[3];
        for (unsigned f = 0; f < 3; ++f) {
            saved[f] = VtOf(a, f);
            copies[f].assign(saved[f], saved[f] + kSlotCounts[f]);
            copies[f][0] = shape::TrapFor<&shape::Count<traps>>(2);
            *reinterpret_cast<void ***>(Face(a, f)) = copies[f].data();
        }
        auto original = ctx.Original<bool(__thiscall *)(void *, uint32_t, void **)>(0xcf5f00);
        const uint32_t riids[] = {nTSCASCore::cITSCASBodyPartSystem::kIid, cIGZSystemService::kIID, 1u, 0x13572468u};
        for (uint32_t riid : riids) {
            ++ctx.cases;
            traps = 0;
            void *outs[2] = {nullptr, nullptr};
            uint32_t counts[2];
            std::memset(static_cast<uint8_t *>(a) + 8, 0, 4);
            const bool ra = original(a, riid, &outs[0]);
            counts[0] = Word(a, 8);
            std::memset(static_cast<uint8_t *>(a) + 8, 0, 4);
            const bool rb = nTSCASCore::cTSCASBodyPartSystemAdapters::QueryInterface(static_cast<System *>(a), nullptr, riid, &outs[1]);
            counts[1] = Word(a, 8);
            if (traps) ctx.Fail("QueryInterface adapter %x: %d trap hits", riid, traps);
            if (ra != rb || outs[0] != outs[1] || counts[0] != counts[1]) ctx.Fail("QueryInterface adapter %x differs", riid);
        }
        for (unsigned f = 0; f < 3; ++f) *reinterpret_cast<void ***>(Face(a, f)) = saved[f];
    }
    // Init, Shutdown and the class's slots 13 / 14: in the slot where the game's vtable holds their RVA (their bodies are
    // tested through their adapters, the hooks' entries, which call the same members qualified).
    struct Placed {
        unsigned slot;
        uint32_t rva;
    };
    const Placed placed[] = {{SlotOfMember(&System::Init), 0xcf3580}, {SlotOfMember(&System::Shutdown), 0xcfa200},
                             {SlotOfMember(&System::RemoveMorphRecords), 0xcf6ff0}, {SlotOfMember(&System::GetPartResource), 0xcf43f0}};
    for (const Placed &p : placed) {
        ++ctx.cases;
        if (p.slot >= 15 || GameAddress<void *>(System::kVTable)[p.slot] != GameAddress<void>(p.rva)) {
            ctx.Fail("our slot %u is not where the game's vtable holds %x", p.slot, p.rva);
            continue;
        }
        seen[0][p.slot] = true;
    }
    // The deleting destructor (+4 slot 3, the thunk 0xce08c2) is the Destruct test's.
    if (GameAddress<void *>(System::kVTable4)[3] != GameAddress<void>(kDeletingThunk)) ctx.Fail("+4 slot 3 is not 0xce08c2");
    seen[1][3] = true;
    for (unsigned f = 0; f < 3; ++f)
        for (unsigned s = 0; s < kSlotCounts[f]; ++s)
            if (!seen[f][s]) ctx.Fail("face %u slot %u is not covered", f, s);
}
