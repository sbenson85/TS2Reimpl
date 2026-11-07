// Differential tests for cGZClipboardManager in the standalone shape (lane KD), and the constructor of the clipboard
// object it creates. The original side is a manager from the original constructor 0xc6d222 (the game's vtables), ours one
// from our constructor (the hook's adapter: our vtables); every operation runs through the original's RVA on the first and
// through our object's own vtable slot on the second. Managers are filled with fake clipboard objects whose vtable slots
// record every call. Compared: results, the sequence of clipboard calls with their arguments, the map (contents and tree
// shape), the lock count and the reference count; construction and destruction word by word with the vtable words and each
// side's heap pointers as codes (tests/service_shape.h).
//
// RVAs: the constructor 0xc6d222, the destructor 0xc6d38e, the deleting destructor 0xc6d418 and its +8 thunk 0xc6d410;
// QueryInterface 0xc6dcda (thunks 0xc6dd25 +4, 0xc6dd2d +8), AddRef 0xc6d4da (0xc6d4fd, 0xc6d505), Release 0xc6e150
// (0xc6e190, 0xc6e198), Init 0xc6db04 (+4 thunk 0xc6db78), Shutdown 0xc6e654 (+4 thunk 0xc6e681), slots 5..20 (below);
// the clipboard object's own tests are in tests/gz_clipboard.cpp.
#include "difftest.h"
#include "free_scope.h"
#include "service_shape.h"
#include "gz/cGZClipboardManager.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <random>
#include <vector>

namespace {

using Clip = cIGZClipboard;
using RealClip = cGZClipboardManagerClipboard;
using Manager = cGZClipboardManager;

constexpr uint32_t kCtor = 0xc6d222, kDtor = 0xc6d38e, kDeleting = 0xc6d418, kDeletingThunk = 0xc6d410;
constexpr uint32_t kQI = 0xc6dcda, kQIFromSystem = 0xc6dd25, kQIFromUnknown = 0xc6dd2d;
constexpr uint32_t kAddRef = 0xc6d4da, kAddRefFromSystem = 0xc6d4fd, kAddRefFromUnknown = 0xc6d505;
constexpr uint32_t kRelease = 0xc6e150, kReleaseFromSystem = 0xc6e190, kReleaseFromUnknown = 0xc6e198;
constexpr uint32_t kInit = 0xc6db04, kInitThunk = 0xc6db78, kShutdown = 0xc6e654, kShutdownThunk = 0xc6e681;
constexpr uint32_t kGetCount = 0xc6d797, kGetClipboard = 0xc6d762, kGetClipboards = 0xc6da71, kAdd = 0xc6d475, kRemove = 0xc6e1df, kDetach = 0xc6e271;

constexpr uint32_t kFaceOffset[3] = {0, 4, 8};
constexpr uint32_t kGameVt[3] = {Manager::kVTable, Manager::kSystemServiceVTable, Manager::kUnknownVTable};
constexpr uint32_t kClipVTable = 0xe9eda4; // the game's cGZClipboardManagerClipboard vtable
constexpr unsigned kSlotCount[3] = {21, 13, 6};

// Our side's slot `slot` of the face at `face` (its own vtable), as a function pointer of type Fn.
template <class Fn> Fn Ours(const Manager *m, uint32_t face, int slot) { return reinterpret_cast<Fn>(svc::VTableAt(m, face)[slot]); }
void *FaceOf(Manager *m, uint32_t face) { return reinterpret_cast<char *>(m) + face; }

// ---- Fake clipboard objects ----------------------------------------------------------------------

struct Event {
    int clip;
    int slot;
    uint32_t a[4];
    bool operator==(const Event &o) const { return clip == o.clip && slot == o.slot && std::memcmp(a, o.a, sizeof(a)) == 0; }
};

std::vector<Event> gEvents;
uint32_t *gArrayBase = nullptr; // the caller's output array, so the fakes log offsets into it

struct FakeClip {
    void *vtable;
    int index;
    uint32_t id;
    uint32_t ret[24];
};

void Log(FakeClip *c, int slot, uint32_t a = 0, uint32_t b = 0, uint32_t d = 0, uint32_t e = 0) { gEvents.push_back({c->index, slot, {a, b, d, e}}); }

uint32_t __fastcall F1(FakeClip *c, void *) { Log(c, 1); return 7; }
uint32_t __fastcall F2(FakeClip *c, void *) { Log(c, 2); return 3; }
void __fastcall F3(FakeClip *c, void *) { Log(c, 3); }
void __fastcall F4(FakeClip *c, void *) { Log(c, 4); }
uint32_t __fastcall F5(FakeClip *c, void *) { Log(c, 5); return c->id; }
void __fastcall F6(FakeClip *c, void *, uint32_t a) { Log(c, 6, a); }
bool __fastcall F10(FakeClip *c, void *, uint32_t a) { Log(c, 10, a); return c->ret[10] != 0; }
uint32_t __fastcall F11(FakeClip *c, void *, uint32_t *array, uint32_t *count)
{
    Log(c, 11, static_cast<uint32_t>(array - gArrayBase), *count);
    uint32_t n = c->ret[11] < *count ? c->ret[11] : *count;
    for (uint32_t i = 0; i < n; ++i) array[i] = c->id * 100 + i;
    *count = n + 1; // not looked at by the caller
    return n;
}
bool __fastcall F12(FakeClip *c, void *, uint32_t a, uint32_t b, uint32_t d) { Log(c, 12, a, b, d); return c->ret[12] != 0; }
bool __fastcall F14(FakeClip *c, void *, uint32_t a, uint32_t b) { Log(c, 14, a, b); return c->ret[14] != 0; }
void __fastcall F15(FakeClip *c, void *, uint32_t a) { Log(c, 15, a); }
bool __fastcall F16(FakeClip *c, void *, uint32_t a, uint32_t b, uint32_t d) { Log(c, 16, a, b, d); return c->ret[16] != 0; }
bool __fastcall F17(FakeClip *c, void *, uint32_t a) { Log(c, 17, a); return c->ret[17] != 0; }
bool __fastcall F18(FakeClip *c, void *, uint32_t a, uint32_t b, uint32_t d, uint32_t e) { Log(c, 18, a, b, d, e); return c->ret[18] != 0; }
bool __fastcall F19(FakeClip *c, void *, uint32_t a, uint32_t b) { Log(c, 19, a, b); return c->ret[19] != 0; }

void *gClipVTable[24];

void InitClipVTable()
{
    gClipVTable[1] = (void *)&F1;
    gClipVTable[2] = (void *)&F2;
    gClipVTable[3] = (void *)&F3;
    gClipVTable[4] = (void *)&F4;
    gClipVTable[5] = (void *)&F5;
    gClipVTable[6] = (void *)&F6;
    gClipVTable[10] = (void *)&F10;
    gClipVTable[11] = (void *)&F11;
    gClipVTable[12] = (void *)&F12;
    gClipVTable[14] = (void *)&F14;
    gClipVTable[15] = (void *)&F15;
    gClipVTable[16] = (void *)&F16;
    gClipVTable[17] = (void *)&F17;
    gClipVTable[18] = (void *)&F18;
    gClipVTable[19] = (void *)&F19;
}

// ---- Manager pairs --------------------------------------------------------------------------------

struct Pair {
    Manager *a;
    Manager *b;
};

Pair MakePair(difftest::Context &ctx)
{
    Pair p{static_cast<Manager *>(::operator new(sizeof(Manager))), static_cast<Manager *>(::operator new(sizeof(Manager)))};
    std::memset(p.a, 0xCD, sizeof(*p.a));
    std::memset(p.b, 0xCD, sizeof(*p.b));
    ctx.Original<Manager *(__thiscall *)(void *)>(kCtor)(p.a);
    cGZClipboardManagerAdapters::Construct(p.b, nullptr);
    return p;
}

// Deletes each side through its own +8 face's slot 3 (the deleting destructor), flags 1.
void FreeOne(Manager *m)
{
    const uint32_t flags[1] = {1};
    svc::CallWords(svc::VTableAt(m, 8)[3], FaceOf(m, 8), 1, flags);
}

void FreePair(Pair &p)
{
    FreeOne(p.a);
    FreeOne(p.b);
}

// Fake clipboards with the given ids; the same objects go into both managers' maps. The vectors
// live as long as the caller keeps them.
struct Fakes {
    std::vector<FakeClip *> clips;
    ~Fakes()
    {
        for (FakeClip *c : clips) delete c;
    }
    FakeClip *Add(uint32_t id, std::mt19937 &rng)
    {
        InitClipVTable();
        auto *c = new FakeClip();
        c->vtable = gClipVTable;
        c->index = static_cast<int>(clips.size());
        c->id = id;
        for (auto &r : c->ret) r = rng() % 3 == 0 ? 0 : 1 + rng() % 3;
        clips.push_back(c);
        return c;
    }
};

void Populate(Pair &p, Fakes &fakes, std::mt19937 &rng, const std::vector<uint32_t> &ids)
{
    for (uint32_t id : ids) {
        FakeClip *c = fakes.Add(id, rng);
        p.a->mClipboards.insert({id, reinterpret_cast<Clip *>(c)});
        p.b->mClipboards.insert({id, reinterpret_cast<Clip *>(c)});
    }
}

// Up to 8 ids from the pool: 0 (the empty map) to 8 entries.
std::vector<uint32_t> RandomIds(std::mt19937 &rng)
{
    std::vector<uint32_t> pool{0, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 1000, 70000};
    std::shuffle(pool.begin(), pool.end(), rng);
    pool.resize(rng() % 9);
    return pool;
}

struct RawNode {
    RawNode *left, *parent, *right;
    uint8_t color, isnil, pad[2];
    uint32_t key;
    void *value;
};

// The map's head node (the first word of the map).
const RawNode *Head(const Manager *m) { return *reinterpret_cast<RawNode *const *>(&m->mClipboards); }

// The clipboard with the given id, read from the map (no call).
Clip *ClipAt(Manager *m, uint32_t id)
{
    auto it = m->mClipboards.find(id);
    return it == m->mClipboards.end() ? nullptr : it->second;
}

bool SameShape(const RawNode *a, const RawNode *b, const RawNode *ha, const RawNode *hb)
{
    if ((a == ha) != (b == hb)) return false;
    if (a == ha) return true;
    if (a->color != b->color || a->isnil != b->isnil || a->key != b->key || a->value != b->value) return false;
    return SameShape(a->left, b->left, ha, hb) && SameShape(a->right, b->right, ha, hb);
}

bool SameManager(const Manager &a, const Manager &b)
{
    if (a.mInitialised != b.mInitialised || a.mLockedRefCount != b.mLockedRefCount || a.mClipboards.size() != b.mClipboards.size()) return false;
    if (a.mLock.mBlock->lockCount != b.mLock.mBlock->lockCount) return false;
    const RawNode *ha = Head(&a), *hb = Head(&b);
    if (ha->color != hb->color || ha->isnil != hb->isnil) return false;
    return SameShape(ha->parent, hb->parent, ha, hb);
}

// Runs `callA` on the first manager and `callB` on the second (each returns the result as 64 bits)
// and compares everything observable.
template <typename CallA, typename CallB>
void Compare(difftest::Context &ctx, Pair &p, const char *what, CallA callA, CallB callB)
{
    gEvents.clear();
    uint64_t ra = callA(p.a);
    std::vector<Event> eventsA = gEvents;
    gEvents.clear();
    uint64_t rb = callB(p.b);
    ++ctx.cases;
    if (ra != rb) ctx.Fail("%s: result %llx/%llx", what, ra, rb);
    if (eventsA != gEvents) ctx.Fail("%s: clipboard calls differ (%zu/%zu events)", what, eventsA.size(), gEvents.size());
    if (!SameManager(*p.a, *p.b)) ctx.Fail("%s: manager state differs (count %zu/%zu, refs %u/%u)", what, p.a->mClipboards.size(), p.b->mClipboards.size(), p.a->mLockedRefCount, p.b->mLockedRefCount);
}

uint32_t RandomId(std::mt19937 &rng, const std::vector<uint32_t> &ids, bool allowAll, bool allowAny)
{
    switch (rng() % 8) {
    case 0: return allowAll ? Manager::kAll : 12345;
    case 1: return allowAny ? Manager::kAny : 54321;
    case 2: return 99999; // not present
    default: return ids.empty() ? 7 : ids[rng() % ids.size()];
    }
}

// Each side's code map: the game's and our vtable of each face as one code per face, the destroyed service part's.
struct Maps {
    svc::CodeMap a, b;
    Maps()
    {
        auto *ours = static_cast<Manager *>(::operator new(sizeof(Manager)));
        cGZClipboardManagerAdapters::Construct(ours, nullptr);
        for (uint32_t f = 0; f < 3; ++f) {
            a.AddRva(kGameVt[f], f);
            b.AddRva(kGameVt[f], f);
            b.Add(svc::VTableAt(ours, kFaceOffset[f]), f);
        }
        FreeOne(ours);
        svc::AddServiceBaseCodes(a, b, 0x10);
    }
    // Word by word; each side's map head and lock block (heap addresses) as codes.
    std::string Compare(const Manager *x, const Manager *y, const void *headX, const void *headY, const void *blockX, const void *blockY) const
    {
        svc::CodeMap ma = a, mb = b;
        ma.Add(headX, 0x100);
        mb.Add(headY, 0x100);
        ma.Add(blockX, 0x101);
        mb.Add(blockY, 0x101);
        return svc::Diff(svc::Words(x, sizeof(Manager), ma), svc::Words(y, sizeof(Manager), mb));
    }
};

} // namespace

// ---- Construction and destruction -----------------------------------------------------------------

DIFF_TEST(cGZClipboardManager_Construct, 0xc6d222)
{
    auto orig = ctx.Original<Manager *(__thiscall *)(void *)>(kCtor);
    Maps maps;
    for (int fill : {0xCD, 0x00, 0xFF}) {
        auto *a = static_cast<Manager *>(::operator new(sizeof(Manager)));
        auto *b = static_cast<Manager *>(::operator new(sizeof(Manager)));
        std::memset(a, fill, sizeof(*a));
        std::memset(b, fill, sizeof(*b));
        Manager *ra = orig(a), *rb = cGZClipboardManagerAdapters::Construct(b, nullptr);
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("fill %02x: return value is not this", fill);
        // Every word, the vtable words, the map's head pointer and the lock's block pointer as codes (the padding after
        // the flag byte included: the original writes the byte only, as ours does).
        const std::string diff = maps.Compare(a, b, Head(a), Head(b), a->mLock.mBlock, b->mLock.mBlock);
        if (!diff.empty()) ctx.Fail("fill %02x: object differs at %s", fill, diff.c_str());
        for (uint32_t f = 0; f < 3; ++f) {
            if (svc::VTableAt(a, kFaceOffset[f]) != GameAddress<void *>(kGameVt[f])) ctx.Fail("fill %02x: original's face %u vtable", fill, f);
            if (svc::VTableAt(b, kFaceOffset[f]) == GameAddress<void *>(kGameVt[f])) ctx.Fail("fill %02x: our face %u has the game's vtable", fill, f);
        }
        if (a->mServiceId != Manager::kServiceId || a->mPriority != Manager::kPriority || a->mInitialised || a->mLockedRefCount != 0) ctx.Fail("fill %02x: original fields unexpected", fill);
        const RawNode *ha = Head(a), *hb = Head(b);
        if (ha->left != ha || ha->parent != ha || ha->right != ha || ha->color != 1 || ha->isnil != 1) ctx.Fail("fill %02x: original head unexpected", fill);
        if (hb->left != hb || hb->parent != hb || hb->right != hb || hb->color != ha->color || hb->isnil != ha->isnil) ctx.Fail("fill %02x: our head differs", fill);
        if (a->mLock.mBlock->lockCount != b->mLock.mBlock->lockCount) ctx.Fail("fill %02x: lock count differs", fill);
        FreeOne(a);
        FreeOne(b);
    }
}

namespace {

bool Live(const void *p) { return !difftest::FreedSinceBegin(p); } // the call under test ran after difftest::BeginFrees()

struct Blocks {
    const void *ptrs[40];
    uint32_t count = 0;
    void Add(const void *p)
    {
        if (count < 40) ptrs[count++] = p;
    }
    bool AnyLive() const
    {
        for (uint32_t i = 0; i < count; ++i) if (Live(ptrs[i])) return true;
        return false;
    }
};

// The blocks a manager owns: the map's nodes (head included) and the lock's block.
void CollectBlocks(Manager *m, Blocks &blocks)
{
    blocks.Add(Head(m));
    for (auto it = m->mClipboards.begin(); it != m->mClipboards.end(); ++it) blocks.Add(reinterpret_cast<const char *>(&*it) - 0x10);
    blocks.Add(m->mLock.mBlock);
}

} // namespace

// The destructor 0xc6d38e (called directly; ours: our destructor, no dispatch), the deleting destructor 0xc6d418 (on the
// primary pointer) and its +8 thunk 0xc6d410 (ours: our +8 vtable's slot 3 for both), flags 0..3, maps of 0..8 entries
// and random service fields; the fakes are not called (the map's destructor does not touch its values).
DIFF_TEST(cGZClipboardManager_Destruct, 0xc6d38e)
{
    Maps maps;
    auto dtor = ctx.Original<void(__thiscall *)(void *)>(kDtor);
    auto deleting = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kDeleting);
    auto thunk = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kDeletingThunk);
    std::mt19937 rng(0xC11);
    for (int run = 0; run < 96; ++run) {
        const int route = run % 3;
        const uint32_t flags = route == 0 ? 0 : (run / 3) % 4;
        Pair p = MakePair(ctx);
        Fakes fakes;
        Populate(p, fakes, rng, RandomIds(rng));
        const uint32_t seed[5] = {rng(), rng(), rng(), rng() % 5, rng() % 3};
        for (Manager *m : {p.a, p.b}) {
            m->mServiceId = seed[0];
            m->mPriority = static_cast<int32_t>(seed[1]);
            m->mTickPriority = static_cast<int32_t>(seed[2]);
            m->mLockedRefCount = seed[3];
            m->mInitialised = seed[4] != 0;
        }
        Blocks blocksA, blocksB;
        CollectBlocks(p.a, blocksA);
        CollectBlocks(p.b, blocksB);
        const void *headA = Head(p.a), *headB = Head(p.b), *blockA = p.a->mLock.mBlock, *blockB = p.b->mLock.mBlock;
        gEvents.clear();
        difftest::BeginFrees();
        void *ra = route == 0 ? (dtor(p.a), p.a) : route == 1 ? deleting(p.a, flags) : thunk(FaceOf(p.a, 8), flags);
        const bool freedA = !Live(p.a), leakA = blocksA.AnyLive();
        difftest::BeginFrees();
        void *rb = p.b;
        if (route == 0) {
            p.b->cGZClipboardManager::~cGZClipboardManager();
        } else {
            const uint32_t args[1] = {flags};
            rb = reinterpret_cast<void *>(svc::CallWords(svc::VTableAt(p.b, 8)[3], FaceOf(p.b, 8), 1, args));
        }
        const bool freedB = !Live(p.b), leakB = blocksB.AnyLive();
        ++ctx.cases;
        if (ra != p.a || rb != p.b) ctx.Fail("route %d flags %u: return value is not the object", route, flags);
        if (freedA != freedB || freedA != ((flags & 1) != 0) || leakA || leakB) ctx.Fail("route %d flags %u: object freed %d/%d, blocks left %d/%d", route, flags, freedA, freedB, leakA, leakB);
        if (!gEvents.empty()) ctx.Fail("route %d: a clipboard was called", route);
        if (!freedA && !freedB) {
            const std::string diff = maps.Compare(p.a, p.b, headA, headB, blockA, blockB);
            if (!diff.empty()) ctx.Fail("route %d: state after destruction differs at %s", route, diff.c_str());
        }
        if (!freedA) ::operator delete(p.a);
        if (!freedB) ::operator delete(p.b);
    }
}

// ---- QueryInterface, AddRef, Release --------------------------------------------------------------

namespace {

std::vector<int64_t> gRecorded;
unsigned char *gBase = nullptr;

uint32_t __fastcall RecAddRef(void *self, void *)
{
    gRecorded.push_back(1000 + (static_cast<unsigned char *>(self) - gBase));
    return 0x4242;
}

uint32_t __fastcall RecShutdown(void *self, void *)
{
    gRecorded.push_back(2000 + (static_cast<unsigned char *>(self) - gBase));
    return 1;
}

void *__fastcall RecDelete(void *self, void *, uint32_t flags)
{
    gRecorded.push_back(3000 + (static_cast<unsigned char *>(self) - gBase) * 16 + flags);
    return self;
}

// A side's recording vtables: copies of its own three vtables with the primary AddRef (slot 1) and Shutdown (slot 4) and
// the +8 deleting destructor (slot 3) replaced by recorders; Restore puts the side's own vtables back.
struct Recording {
    void *primary[21], *system[13], *unknown[6];
    void **saved[3];
    void Use(Manager *m)
    {
        void **const own[3] = {svc::VTableAt(m, 0), svc::VTableAt(m, 4), svc::VTableAt(m, 8)};
        std::copy(own, own + 3, saved);
        std::memcpy(primary, own[0], sizeof(primary));
        std::memcpy(system, own[1], sizeof(system));
        std::memcpy(unknown, own[2], sizeof(unknown));
        primary[1] = (void *)&RecAddRef;
        primary[4] = (void *)&RecShutdown;
        unknown[3] = (void *)&RecDelete;
        void **const rec[3] = {primary, system, unknown};
        for (uint32_t f = 0; f < 3; ++f) *reinterpret_cast<void ***>(FaceOf(m, kFaceOffset[f])) = rec[f];
    }
    void Restore(Manager *m)
    {
        for (uint32_t f = 0; f < 3; ++f) *reinterpret_cast<void ***>(FaceOf(m, kFaceOffset[f])) = saved[f];
    }
};

} // namespace

// Through each face's slot 0, 1 and 2 (the original's RVAs on one side, our vtables on the other): QueryInterface with
// recorders in place of AddRef, the id table on every face.
DIFF_TEST(cGZClipboardManager_QueryInterface, 0xc6dcda)
{
    const uint32_t origRva[3] = {kQI, kQIFromSystem, kQIFromUnknown};
    const uint32_t ids[] = {Manager::kInterfaceId, 0x287fb697, 1, 0, 0x7ec000f, 0x7ec0011, 0xffffffff, 0x089b7dc8, Manager::kServiceId};
    for (uint32_t id : ids) {
        for (uint32_t face = 0; face < 3; ++face) {
            Pair p = MakePair(ctx);
            Recording recA, recB;
            recA.Use(p.a);
            recB.Use(p.b);
            void *sentinel = reinterpret_cast<void *>(0x5a5a5a5a);
            void *outA = sentinel, *outB = sentinel;
            gRecorded.clear();
            gBase = reinterpret_cast<unsigned char *>(p.a);
            uint8_t ra = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(origRva[face])(FaceOf(p.a, kFaceOffset[face]), id, &outA);
            std::vector<int64_t> logA = gRecorded;
            gRecorded.clear();
            gBase = reinterpret_cast<unsigned char *>(p.b);
            uint8_t rb = Ours<uint8_t(__thiscall *)(void *, uint32_t, void **)>(p.b, kFaceOffset[face], 0)(FaceOf(p.b, kFaceOffset[face]), id, &outB);
            ++ctx.cases;
            ptrdiff_t offA = outA == sentinel ? -1 : reinterpret_cast<char *>(outA) - reinterpret_cast<char *>(p.a);
            ptrdiff_t offB = outB == sentinel ? -1 : reinterpret_cast<char *>(outB) - reinterpret_cast<char *>(p.b);
            if (ra != rb || logA != gRecorded || offA != offB || !SameManager(*p.a, *p.b)) {
                ctx.Fail("id 0x%x via face %u: result %u/%u, out offset %td/%td, AddRef calls %zu/%zu", id, face, ra, rb, offA, offB, logA.size(), gRecorded.size());
            }
            if (id == Manager::kInterfaceId && (ra != 1 || offA != 0)) ctx.Fail("manager id: result %u offset %td", ra, offA);
            if (id == 0x287fb697 && (ra != 1 || offA != 4)) ctx.Fail("system service id: result %u offset %td", ra, offA);
            recA.Restore(p.a);
            recB.Restore(p.b);
            FreePair(p);
        }
    }
}

DIFF_TEST(cGZClipboardManager_AddRef, 0xc6d4da)
{
    const uint32_t origRva[3] = {kAddRef, kAddRefFromSystem, kAddRefFromUnknown};
    for (uint32_t face = 0; face < 3; ++face) {
        Pair p = MakePair(ctx);
        for (uint32_t start : {0u, 1u, 5u, 0xfffffffeu, 0xffffffffu}) {
            p.a->mLockedRefCount = p.b->mLockedRefCount = start;
            uint32_t ra = ctx.Original<uint32_t(__thiscall *)(void *)>(origRva[face])(FaceOf(p.a, kFaceOffset[face]));
            uint32_t rb = Ours<uint32_t(__thiscall *)(void *)>(p.b, kFaceOffset[face], 1)(FaceOf(p.b, kFaceOffset[face]));
            ++ctx.cases;
            if (ra != rb || ra != start + 1 || !SameManager(*p.a, *p.b)) ctx.Fail("via face %u from %u: %u/%u", face, start, ra, rb);
        }
        p.a->mLockedRefCount = p.b->mLockedRefCount = 0;
        FreePair(p);
    }
}

// Counts on both sides of the boundary at 1 (0 and 1 shut down and delete, 2 and up count down).
DIFF_TEST(cGZClipboardManager_Release, 0xc6e150)
{
    const uint32_t origRva[3] = {kRelease, kReleaseFromSystem, kReleaseFromUnknown};
    for (uint32_t face = 0; face < 3; ++face) {
        for (uint32_t start : {0u, 1u, 2u, 3u, 100u, 0xffffffffu}) {
            Pair p = MakePair(ctx);
            Recording recA, recB;
            recA.Use(p.a);
            recB.Use(p.b);
            p.a->mLockedRefCount = p.b->mLockedRefCount = start;
            gRecorded.clear();
            gBase = reinterpret_cast<unsigned char *>(p.a);
            uint32_t ra = ctx.Original<uint32_t(__thiscall *)(void *)>(origRva[face])(FaceOf(p.a, kFaceOffset[face]));
            std::vector<int64_t> logA = gRecorded;
            gRecorded.clear();
            gBase = reinterpret_cast<unsigned char *>(p.b);
            uint32_t rb = Ours<uint32_t(__thiscall *)(void *)>(p.b, kFaceOffset[face], 2)(FaceOf(p.b, kFaceOffset[face]));
            ++ctx.cases;
            if (ra != rb || logA != gRecorded || !SameManager(*p.a, *p.b)) ctx.Fail("via face %u from %u: result %u/%u, calls %zu/%zu", face, start, ra, rb, logA.size(), gRecorded.size());
            if (start > 1 && (ra != start - 1 || !logA.empty())) ctx.Fail("via face %u from %u: original result %u, %zu calls", face, start, ra, logA.size());
            if (start <= 1 && (ra != 0 || logA.size() != 2)) ctx.Fail("via face %u from %u: original result %u, %zu calls (expected shutdown and delete)", face, start, ra, logA.size());
            recA.Restore(p.a);
            recB.Restore(p.b);
            FreePair(p);
        }
    }
}

// ---- Init, Shutdown -------------------------------------------------------------------------------

namespace {

// Init creates a real clipboard object (vtable 0xe9eda4) and calls its slots 1, 6 and 3, whose
// bodies aren't isolated in the harness (they reach GetVersion). For this test the image's vtable
// slots are replaced by recorders, so both sides run the same fakes.
uint32_t __fastcall RealClipF1(void *, void *) { gEvents.push_back({-1, 1, {0, 0, 0, 0}}); return 1; }
void __fastcall RealClipF3(void *, void *) { gEvents.push_back({-1, 3, {0, 0, 0, 0}}); }
void __fastcall RealClipF6(void *, void *, uint32_t a) { gEvents.push_back({-1, 6, {a, 0, 0, 0}}); }

} // namespace

DIFF_TEST(cGZClipboardManager_Init, 0xc6db04)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *)>(kInit);
    auto origThunk = ctx.Original<uint8_t(__thiscall *)(void *)>(kInitThunk);
    // The original Init builds the game's clipboard (vtable 0xe9eda4), ours our class: the recorders go into both tables.
    RealClip *scratch = new RealClip;
    void **ourVtable = *reinterpret_cast<void ***>(scratch);
    delete scratch;
    void **vtables[2] = {ctx.image.At<void *>(kClipVTable), ourVtable};
    DWORD oldProtect[2] = {};
    void *saved[2][3] = {};
    for (int t = 0; t < 2; ++t) {
        void **vtable = vtables[t];
        VirtualProtect(vtable, 0x60, PAGE_READWRITE, &oldProtect[t]);
        saved[t][0] = vtable[1];
        saved[t][1] = vtable[3];
        saved[t][2] = vtable[6];
        vtable[1] = (void *)&RealClipF1;
        vtable[3] = (void *)&RealClipF3;
        vtable[6] = (void *)&RealClipF6;
    }
    std::mt19937 rng(0xC13);
    for (int run = 0; run < 12; ++run) {
        Pair p = MakePair(ctx);
        Fakes fakes;
        if (run % 3 == 1) Populate(p, fakes, rng, {5, 9}); // not initialised yet, with other clipboards
        bool thunk = run % 2 == 1;
        // Twice: the second call finds it initialised and does nothing.
        std::vector<Event> eventsA, eventsB;
        for (int times = 0; times < 2; ++times) {
            gEvents.clear();
            uint8_t ra = thunk ? origThunk(reinterpret_cast<char *>(p.a) + 4) : orig(p.a);
            eventsA.insert(eventsA.end(), gEvents.begin(), gEvents.end());
            gEvents.clear();
            uint8_t rb = thunk ? Ours<uint8_t(__thiscall *)(void *)>(p.b, 4, 8)(FaceOf(p.b, 4)) : Ours<uint8_t(__thiscall *)(void *)>(p.b, 0, 3)(p.b);
            eventsB.insert(eventsB.end(), gEvents.begin(), gEvents.end());
            ++ctx.cases;
            if (ra != static_cast<uint8_t>(rb) || ra != 1) ctx.Fail("run %d: result %u/%u", run, ra, rb);
            if (p.a->mInitialised != p.b->mInitialised || p.a->mClipboards.size() != p.b->mClipboards.size() || p.a->mLock.mBlock->lockCount != p.b->mLock.mBlock->lockCount) ctx.Fail("run %d: state differs after Init #%d", run, times);
        }
        if (eventsA != eventsB || eventsA.size() != 3) ctx.Fail("run %d: clipboard calls differ or are not AddRef, 6, 3: %zu/%zu", run, eventsA.size(), eventsB.size());
        // The clipboard created for id 0 matches field by field (the original's own methods ran on
        // both: reference count, id, init).
        // Word 0 is each side's vtable; bytes 6 and 7 are never written.
        const auto *ca = reinterpret_cast<const unsigned char *>(ClipAt(p.a, 0));
        const auto *cb = reinterpret_cast<const unsigned char *>(ClipAt(p.b, 0));
        if (!ca || !cb) {
            ctx.Fail("run %d: no clipboard 0 (%p/%p)", run, ca, cb);
        } else {
            const auto *la = reinterpret_cast<const cRZCriticalSection *>(ca + 0x24);
            const auto *lb = reinterpret_cast<const cRZCriticalSection *>(cb + 0x24);
            if (*reinterpret_cast<void *const *>(ca) != vtables[0] || *reinterpret_cast<void *const *>(cb) != vtables[1] ||
                std::memcmp(ca + 4, cb + 4, 2) != 0 || std::memcmp(ca + 8, cb + 8, 0x14) != 0 ||
                *reinterpret_cast<const uint32_t *>(ca + 0x20) != *reinterpret_cast<const uint32_t *>(cb + 0x20) ||
                la->mBlock->lockCount != lb->mBlock->lockCount)
                ctx.Fail("run %d: clipboard 0 differs", run);
        }
        FreePair(p);
    }
    for (int t = 0; t < 2; ++t) {
        void **vtable = vtables[t];
        vtable[1] = saved[t][0];
        vtable[3] = saved[t][1];
        vtable[6] = saved[t][2];
        VirtualProtect(vtable, 0x60, oldProtect[t], &oldProtect[t]);
    }
}

DIFF_TEST(cGZClipboardManager_Shutdown, 0xc6e654)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *)>(kShutdown);
    auto origThunk = ctx.Original<uint8_t(__thiscall *)(void *)>(kShutdownThunk);
    std::mt19937 rng(0xC14);
    for (int run = 0; run < 24; ++run) {
        Pair p = MakePair(ctx);
        Fakes fakes;
        Populate(p, fakes, rng, RandomIds(rng));
        p.a->mInitialised = p.b->mInitialised = run % 3 != 0;
        bool thunk = run % 2 == 1;
        Compare(ctx, p, "Shutdown", [&](Manager *m) -> uint64_t { return thunk ? origThunk(reinterpret_cast<char *>(m) + 4) : orig(m); },
            [&](Manager *m) -> uint64_t { return thunk ? Ours<uint8_t(__thiscall *)(void *)>(m, 4, 9)(FaceOf(m, 4)) : Ours<uint8_t(__thiscall *)(void *)>(m, 0, 4)(m); });
        if (p.a->mInitialised) ctx.Fail("run %d: shutdown left the flag set", run);
        FreePair(p);
    }
}

// ---- Map operations -------------------------------------------------------------------------------

DIFF_TEST(cGZClipboardManager_GetCount, 0xc6d797)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(kGetCount);
    std::mt19937 rng(0xC15);
    for (int run = 0; run < 20; ++run) {
        Pair p = MakePair(ctx);
        Fakes fakes;
        auto ids = RandomIds(rng);
        Populate(p, fakes, rng, ids);
        Compare(ctx, p, "GetCount", [&](Manager *m) -> uint64_t { return orig(m); }, [&](Manager *m) -> uint64_t { return Ours<uint32_t(__thiscall *)(void *)>(m, 0, 5)(m); });
        FreePair(p);
    }
}

DIFF_TEST(cGZClipboardManager_GetClipboard, 0xc6d762)
{
    auto orig = ctx.Original<Clip *(__thiscall *)(void *, uint32_t)>(kGetClipboard);
    std::mt19937 rng(0xC16);
    for (int run = 0; run < 30; ++run) {
        Pair p = MakePair(ctx);
        Fakes fakes;
        auto ids = RandomIds(rng);
        Populate(p, fakes, rng, ids);
        for (int q = 0; q < 6; ++q) {
            uint32_t id = RandomId(rng, ids, true, true);
            Compare(ctx, p, "GetClipboard", [&](Manager *m) -> uint64_t { return reinterpret_cast<uintptr_t>(orig(m, id)); }, [&](Manager *m) -> uint64_t { return reinterpret_cast<uintptr_t>(Ours<Clip *(__thiscall *)(void *, uint32_t)>(m, 0, 6)(m, id)); });
        }
        FreePair(p);
    }
}

DIFF_TEST(cGZClipboardManager_GetClipboards, 0xc6da71)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, Clip **, uint32_t *)>(kGetClipboards);
    std::mt19937 rng(0xC17);
    for (int run = 0; run < 40; ++run) {
        Pair p = MakePair(ctx);
        Fakes fakes;
        auto ids = RandomIds(rng);
        Populate(p, fakes, rng, ids);
        uint32_t want = rng() % (ids.size() + 3);
        bool nullArray = run % 4 == 0;
        Clip *arrA[16], *arrB[16];
        uint32_t countA = want, countB = want;
        std::memset(arrA, 0x5A, sizeof(arrA));
        std::memset(arrB, 0x5A, sizeof(arrB));
        Compare(ctx, p, "GetClipboards", [&](Manager *m) -> uint64_t { return orig(m, nullArray ? nullptr : arrA, &countA); },
            [&](Manager *m) -> uint64_t { return Ours<uint32_t(__thiscall *)(void *, Clip **, uint32_t *)>(m, 0, 7)(m, nullArray ? nullptr : arrB, &countB); });
        if (countA != countB || std::memcmp(arrA, arrB, sizeof(arrA)) != 0) ctx.Fail("run %d: count %u/%u or array differs", run, countA, countB);
        FreePair(p);
    }
}

DIFF_TEST(cGZClipboardManager_AddClipboard, 0xc6d475)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, Clip *)>(kAdd);
    std::mt19937 rng(0xC18);
    for (int run = 0; run < 40; ++run) {
        Pair p = MakePair(ctx);
        Fakes fakes;
        auto ids = RandomIds(rng);
        Populate(p, fakes, rng, ids);
        // A new clipboard, or one replacing an existing id (the old one is removed first).
        uint32_t id = !ids.empty() && run % 2 == 0 ? ids[rng() % ids.size()] : 424242 + run;
        FakeClip *fresh = fakes.Add(id, rng);
        Compare(ctx, p, "AddClipboard", [&](Manager *m) -> uint64_t { return orig(m, reinterpret_cast<Clip *>(fresh)); },
            [&](Manager *m) -> uint64_t { return Ours<uint8_t(__thiscall *)(void *, Clip *)>(m, 0, 8)(m, reinterpret_cast<Clip *>(fresh)); });
        FreePair(p);
    }
}

DIFF_TEST(cGZClipboardManager_RemoveClipboard, 0xc6e1df)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t)>(kRemove);
    std::mt19937 rng(0xC19);
    for (int run = 0; run < 50; ++run) {
        Pair p = MakePair(ctx);
        Fakes fakes;
        auto ids = RandomIds(rng);
        Populate(p, fakes, rng, ids);
        for (int q = 0; q < 3; ++q) {
            uint32_t id = RandomId(rng, ids, true, false);
            Compare(ctx, p, "RemoveClipboard", [&](Manager *m) -> uint64_t { return orig(m, id); }, [&](Manager *m) -> uint64_t { return Ours<uint8_t(__thiscall *)(void *, uint32_t)>(m, 0, 9)(m, id); });
        }
        FreePair(p);
    }
}

DIFF_TEST(cGZClipboardManager_DetachClipboard, 0xc6e271)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, Clip *)>(kDetach);
    std::mt19937 rng(0xC1A);
    for (int run = 0; run < 40; ++run) {
        Pair p = MakePair(ctx);
        Fakes fakes;
        auto ids = RandomIds(rng);
        Populate(p, fakes, rng, ids);
        FakeClip *stranger = fakes.Add(777, rng); // never inserted
        for (int q = 0; q < 3; ++q) {
            FakeClip *target = fakes.clips.size() > 1 && rng() % 4 != 0 ? fakes.clips[rng() % (fakes.clips.size() - 1)] : stranger;
            Compare(ctx, p, "DetachClipboard", [&](Manager *m) -> uint64_t { return orig(m, reinterpret_cast<Clip *>(target)); },
                [&](Manager *m) -> uint64_t { return Ours<uint8_t(__thiscall *)(void *, Clip *)>(m, 0, 10)(m, reinterpret_cast<Clip *>(target)); });
        }
        FreePair(p);
    }
}

// ---- The forwarding operations (slots 11..20) -----------------------------------------------------

namespace {

constexpr uint32_t kS11 = 0xc6dbf0, kS12 = 0xc6d646, kS13 = 0xc6da1e, kS14 = 0xc6d7b4, kS15 = 0xc6e329, kS16 = 0xc6d5dc, kS17 = 0xc6d91e, kS18 = 0xc6d8ac, kS19 = 0xc6e51c, kS20 = 0xc6e4ac;

// Runs a forwarding operation on many managers; `body` receives the pair, the ids present and an rng.
template <typename Body> void ForEachManager(difftest::Context &ctx, uint32_t seed, int runs, Body body)
{
    std::mt19937 rng(seed);
    for (int run = 0; run < runs; ++run) {
        Pair p = MakePair(ctx);
        Fakes fakes;
        auto ids = RandomIds(rng);
        Populate(p, fakes, rng, ids);
        for (int q = 0; q < 6; ++q) body(p, ids, rng);
        FreePair(p);
    }
}

} // namespace

DIFF_TEST(cGZClipboardManager_Slot11, 0xc6dbf0)
{
    InitClipVTable();
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t)>(kS11);
    ForEachManager(ctx, 0xC21, 40, [&](Pair &p, const std::vector<uint32_t> &ids, std::mt19937 &rng) {
        uint32_t a1 = rng(), id = RandomId(rng, ids, false, true);
        Compare(ctx, p, "Slot11", [&](Manager *m) -> uint64_t { return orig(m, a1, id); }, [&](Manager *m) -> uint64_t { return Ours<uint8_t(__thiscall *)(void *, uint32_t, uint32_t)>(m, 0, 11)(m, a1, id); });
    });
}

DIFF_TEST(cGZClipboardManager_Slot12, 0xc6d646)
{
    InitClipVTable();
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, void *, uint32_t *, uint32_t)>(kS12);
    ForEachManager(ctx, 0xC22, 60, [&](Pair &p, const std::vector<uint32_t> &ids, std::mt19937 &rng) {
        uint32_t want = rng() % 14, id = RandomId(rng, ids, true, false);
        uint32_t bufA[16], bufB[16];
        std::memset(bufA, 0x5A, sizeof(bufA));
        std::memset(bufB, 0x5A, sizeof(bufB));
        uint32_t countA = want, countB = want;
        // The fake records the low bits of the array address, so offsets into the buffer show up.
        Compare(ctx, p, "Slot12", [&](Manager *m) -> uint64_t { gArrayBase = bufA; return orig(m, bufA, &countA, id); },
            [&](Manager *m) -> uint64_t { gArrayBase = bufB; return Ours<uint32_t(__thiscall *)(void *, void *, uint32_t *, uint32_t)>(m, 0, 12)(m, bufB, &countB, id); });
        if (countA != countB || std::memcmp(bufA, bufB, sizeof(bufA)) != 0) ctx.Fail("Slot12 want %u id %x: count %u/%u or buffer differs", want, id, countA, countB);
    });
}

DIFF_TEST(cGZClipboardManager_Slot13, 0xc6da1e)
{
    InitClipVTable();
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(kS13);
    ForEachManager(ctx, 0xC23, 40, [&](Pair &p, const std::vector<uint32_t> &, std::mt19937 &rng) {
        uint32_t a1 = rng();
        Compare(ctx, p, "Slot13", [&](Manager *m) -> uint64_t { return orig(m, a1); }, [&](Manager *m) -> uint64_t { return Ours<uint32_t(__thiscall *)(void *, uint32_t)>(m, 0, 13)(m, a1); });
    });
}

DIFF_TEST(cGZClipboardManager_Slot14, 0xc6d7b4)
{
    InitClipVTable();
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t)>(kS14);
    ForEachManager(ctx, 0xC24, 40, [&](Pair &p, const std::vector<uint32_t> &ids, std::mt19937 &rng) {
        uint32_t a1 = rng(), a2 = rng(), a3 = rng(), id = RandomId(rng, ids, false, true);
        Compare(ctx, p, "Slot14", [&](Manager *m) -> uint64_t { return orig(m, a1, a2, a3, id); }, [&](Manager *m) -> uint64_t { return Ours<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t)>(m, 0, 14)(m, a1, a2, a3, id); });
    });
}

DIFF_TEST(cGZClipboardManager_Slot15, 0xc6e329)
{
    InitClipVTable();
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(kS15);
    ForEachManager(ctx, 0xC25, 40, [&](Pair &p, const std::vector<uint32_t> &ids, std::mt19937 &rng) {
        uint32_t a1 = rng(), a2 = rng(), id = RandomId(rng, ids, true, false);
        Compare(ctx, p, "Slot15", [&](Manager *m) -> uint64_t { return orig(m, a1, a2, id); }, [&](Manager *m) -> uint64_t { return Ours<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(m, 0, 15)(m, a1, a2, id); });
    });
}

DIFF_TEST(cGZClipboardManager_Slot16, 0xc6d5dc)
{
    InitClipVTable();
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t)>(kS16);
    ForEachManager(ctx, 0xC26, 40, [&](Pair &p, const std::vector<uint32_t> &ids, std::mt19937 &rng) {
        uint32_t a1 = rng(), id = RandomId(rng, ids, true, false);
        Compare(ctx, p, "Slot16", [&](Manager *m) -> uint64_t { return orig(m, a1, id); }, [&](Manager *m) -> uint64_t { return Ours<uint8_t(__thiscall *)(void *, uint32_t, uint32_t)>(m, 0, 16)(m, a1, id); });
    });
}

DIFF_TEST(cGZClipboardManager_Slot17, 0xc6d91e)
{
    InitClipVTable();
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t)>(kS17);
    ForEachManager(ctx, 0xC27, 40, [&](Pair &p, const std::vector<uint32_t> &ids, std::mt19937 &rng) {
        uint32_t a1 = rng(), a2 = rng(), a3 = rng(), id = RandomId(rng, ids, false, true);
        Compare(ctx, p, "Slot17", [&](Manager *m) -> uint64_t { return orig(m, a1, a2, a3, id); }, [&](Manager *m) -> uint64_t { return Ours<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t)>(m, 0, 17)(m, a1, a2, a3, id); });
    });
}

DIFF_TEST(cGZClipboardManager_Slot18, 0xc6d8ac)
{
    InitClipVTable();
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t)>(kS18);
    ForEachManager(ctx, 0xC28, 40, [&](Pair &p, const std::vector<uint32_t> &ids, std::mt19937 &rng) {
        uint32_t a1 = rng(), id = RandomId(rng, ids, false, true);
        Compare(ctx, p, "Slot18", [&](Manager *m) -> uint64_t { return orig(m, a1, id); }, [&](Manager *m) -> uint64_t { return Ours<uint8_t(__thiscall *)(void *, uint32_t, uint32_t)>(m, 0, 18)(m, a1, id); });
    });
}

DIFF_TEST(cGZClipboardManager_Slot19, 0xc6e51c)
{
    InitClipVTable();
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(kS19);
    ForEachManager(ctx, 0xC29, 40, [&](Pair &p, const std::vector<uint32_t> &ids, std::mt19937 &rng) {
        uint32_t a1 = rng(), a2 = rng(), a3 = rng(), a4 = rng(), id = RandomId(rng, ids, true, false);
        Compare(ctx, p, "Slot19", [&](Manager *m) -> uint64_t { return orig(m, a1, a2, a3, a4, id); }, [&](Manager *m) -> uint64_t { return Ours<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(m, 0, 19)(m, a1, a2, a3, a4, id); });
    });
}

DIFF_TEST(cGZClipboardManager_Slot20, 0xc6e4ac)
{
    InitClipVTable();
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(kS20);
    ForEachManager(ctx, 0xC2A, 40, [&](Pair &p, const std::vector<uint32_t> &ids, std::mt19937 &rng) {
        uint32_t a1 = rng(), a2 = rng(), id = RandomId(rng, ids, true, false);
        Compare(ctx, p, "Slot20", [&](Manager *m) -> uint64_t { return orig(m, a1, a2, id); }, [&](Manager *m) -> uint64_t { return Ours<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(m, 0, 20)(m, a1, a2, id); });
    });
}

// ---- The service part's slots ---------------------------------------------------------------------

namespace {

struct ServiceSlot {
    uint32_t face, slot, words;
    svc::Ret ret;
    const char *name;
};

// The inherited cIGZSystemService and cRZUnknownMT slots (folded 0x145770, 0x149c60, 0x149cf0, 0x156110, 0x1b9690,
// 0x148260, 0x149d00, 0x1b9544, 0x147a30): the same bodies in both vtables, checked to sit in the same slots.
const ServiceSlot kServiceSlots[] = {
    {4, 3, 0, svc::kWord, "GetServiceID"},       {4, 4, 1, svc::kVoid, "SetServiceID"},
    {4, 5, 0, svc::kWord, "GetServicePriority"}, {4, 6, 0, svc::kByte, "IsServiceRunning"},
    {4, 7, 1, svc::kVoid, "SetServiceRunning"},  {4, 10, 1, svc::kByte, "OnTick"},
    {4, 11, 1, svc::kByte, "OnIdle"},            {4, 12, 0, svc::kWord, "GetServiceTickPriority"},
    {8, 4, 0, svc::kWord, "RemoveRef"},          {8, 5, 0, svc::kWord, "RefCount"},
};

} // namespace

DIFF_TEST(cGZClipboardManager_ServiceSlots, 0xc6d222)
{
    Maps maps;
    std::mt19937 rng(0xC2B);
    for (const ServiceSlot &s : kServiceSlots) {
        for (int run = 0; run < 12; ++run) {
            Pair p = MakePair(ctx);
            // The same random service fields on both sides (the service part's count 0..5 or random, the running byte
            // 0, 0x80 or random).
            for (uint32_t offset = 0xc; offset < 0x20; offset += 4) {
                uint32_t value = rng();
                if (offset == 0xc && run % 4 < 3) value = static_cast<uint32_t>(run % 6);
                if (offset == 0x1c && run % 3 == 0) value &= 0xffffff00;
                if (offset == 0x1c && run % 3 == 1) value = (value & 0xffffff00) | 0x80;
                std::memcpy(reinterpret_cast<uint8_t *>(p.a) + offset, &value, 4);
                std::memcpy(reinterpret_cast<uint8_t *>(p.b) + offset, &value, 4);
            }
            const uint32_t args[1] = {rng()};
            const uint32_t ra = svc::CallWords(GameAddress<void *>(kGameVt[s.face / 4])[s.slot], FaceOf(p.a, s.face), s.words, args);
            const uint32_t rb = svc::CallWords(svc::VTableAt(p.b, s.face)[s.slot], FaceOf(p.b, s.face), s.words, args);
            ++ctx.cases;
            if (s.slot >= kSlotCount[s.face / 4]) ctx.Fail("%s: slot out of range", s.name);
            if (svc::Masked(s.ret, ra) != svc::Masked(s.ret, rb)) ctx.Fail("%s run %d: result 0x%x/0x%x", s.name, run, ra, rb);
            const std::string diff = maps.Compare(p.a, p.b, Head(p.a), Head(p.b), p.a->mLock.mBlock, p.b->mLock.mBlock);
            if (!diff.empty()) ctx.Fail("%s run %d: object differs at %s", s.name, run, diff.c_str());
            FreePair(p);
        }
    }
}
