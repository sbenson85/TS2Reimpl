// Differential test for the vertex and index arrays as whole classes (lane NJ, rendering R3b item 1):
// src/gz/cVertexArrayDX.h (cStaticVertexComponentArrayDX, cDynamicVertexComponentArrayDX, cStaticVertexIndexArrayDX,
// cDynamicVertexIndexArrayDX).
//
// Each case makes two arrays with their creators (the original's at their RVAs, ours through New()), then runs a seeded
// sequence of slot calls through each object's vtables (the same calls on both sides: RawSlot on the primary or the +4
// face) and finally releases them. Our side reaches every slot through our vtables (the standalone shape's slots test);
// the RVAs this file verifies:
//   creators 0x165fa0 0x165ea0 0x166000 0x165f20;
//   QueryInterface 0x166bc0 0x166b00 0x166c10 0x166b60 and the shared parts 0x1669e0 0x166a70; AddRef 0x165e30;
//   Release 0x166c80 0x166c60; ReleaseResource 0x166e10 0x166db0; GetMemorySize 0x166f40 0x166f80; SurvivesReset
//   0x166ec0 0x166f00; SurvivesModeChange 0x1670c0 0x167120 and the usage / pool choice 0x167020; GetPool 0x165e40
//   0x165e70; GetVideoMemorySize 0x166800 0x166850; Lock 0x1668a0 0x1668f0 0x166940 0x166990; Unlock 0x166fc0
//   0x166ff0; GetSize 0x166e60 0x166e90; Reserve 0x166ca0 0x166d30; Create 0x1664c0 0x166660 0x1660a0 0x1662b0;
//   the destructors 0x165c10 0x1659d0 0x165d20 0x165af0; the faces' QueryInterface thunks 0x166c06 0x166b4d 0x166c56
//   0x166bad; the surfaces' bodies the arrays share (IsCreated 0x1624f0, SetPriority 0x162d10, PreLoad 0x1630d0,
//   GetPriority 0x1628d0).
//
// The world (built alike for each side from the case's seed):
// - The engine side: the device (cIDeviceDX: readiness, its resource manager, device enum and Direct3D device), the
//   manager (Add / Remove / RefreshAvailableMemory) and the device enum (the four vertex processing modes) are drawfx
//   fakes.
// - Direct3D: the device's CreateVertexBuffer / CreateIndexBuffer by d3dfx::Script (results per call, a failure can
//   still hand out an object); the buffers are d3dfx::Buffer fakes whose GetDesc / Lock write their out-parameters
//   whatever they answer (the original reads them regardless).
// - The pools: each class's game pool is set up fresh for each side on one fixed region, so both sides' objects sit at
//   the same addresses.
// Compared: the log (every call on every fake with its arguments, LogD3DError lines), each call's result (the locked
// addresses normalised), the objects' words after +4 (vtables excluded: each side has its own) after every call, every
// fake's reference count, the buffers' lock counts and priorities, the freed blocks' words after +4 (what the
// destructors left) and the pools' state after the release.
// The static arrays' Create refreshes the manager's memory only for the default pool, which no kind 0-3 chooses (the
// usage / pool table 0x167020 indexes holds the system memory and managed pools only): that branch never runs.
#include "d3dfx.h"
#include "difftest.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cVertexArrayDX.h"
#include "rz/RZFixedAllocator.h"

#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace nGZGraphic4;

namespace {

// ---- engine-side slot tables ----
constexpr drawfx::Slot U{0, drawfx::Ret::Int, false, "", ""};
constexpr drawfx::Slot kQI{2, drawfx::Ret::Int, true, "QueryInterface", "ui"};
constexpr drawfx::Slot kAddRef{0, drawfx::Ret::Int, true, "AddRef", ""};
constexpr drawfx::Slot kRelease{0, drawfx::Ret::Int, true, "Release", ""};

inline constexpr drawfx::Slot kDeviceSlots[] = {
    kQI, kAddRef, kRelease, U, U, U, U, U, U, U, U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "GetDeviceEnum", ""},       // 15
    {0, drawfx::Ret::Int, true, "GetDeviceEnumConst", ""},  // 16
    U,
    {0, drawfx::Ret::Int, true, "GetResourceManager", ""},  // 18
    U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "IsDeviceReady", ""},       // 35
    U, U, U, U, U, U, U, U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "GetD3DDevice", ""},        // 48
};
inline constexpr drawfx::Iface kDevice{"cIDeviceDX", 49, kDeviceSlots};

inline constexpr drawfx::Slot kManagerSlots[] = {
    kQI, kAddRef, kRelease, U, U, U,
    {1, drawfx::Ret::Int, true, "Add", "i"},
    U,
    {1, drawfx::Ret::Int, true, "Remove", "i"},
    U, U, U, U,
    {0, drawfx::Ret::Int, true, "RefreshAvailableMemory", ""},
};
inline constexpr drawfx::Iface kManager{"cIDeviceResourceManager", 14, kManagerSlots};

inline constexpr drawfx::Slot kEnumSlots[] = {
    kQI, kAddRef, kRelease, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {1, drawfx::Ret::Int, true, "HasVertexProcessing", "u"}, // 20
};
inline constexpr drawfx::Iface kEnum{"cIDeviceEnum", 21, kEnumSlots};

// ---- the world ----
constexpr unsigned kBuffers = 8; // per kind (vertex, index)
constexpr uint32_t kIidDeviceDX = 0x553b7cf;
constexpr uint32_t kQuiet[] = {0x8876017c, 0x8007000e, 0x88760827};

struct World {
    d3dfx::Buffer buffer[2][kBuffers];
    uint32_t ready;
    uint32_t modes[4];
    void *out;
} gWorld;

drawfx::Fake gDevice, gManager, gEnum, gD3D, gD3DBuf[2][kBuffers];

uint32_t W(const void *p) { return drawfx::P(p); }
uint32_t N(uint32_t w) { return drawfx::Norm(w); }
void NoteWords(const char *label, std::vector<uint32_t> words)
{
    drawfx::Call c;
    c.note = label;
    c.words = std::move(words);
    drawfx::gLog.push_back(std::move(c));
}
void NoteResult(uint32_t r) { drawfx::Note("result", r); }

uint32_t Pick(std::mt19937 &g, std::initializer_list<uint32_t> v) { return v.begin()[g() % v.size()]; }

uint32_t Modes(drawfx::Fake &, const uint32_t *a) { return gWorld.modes[a[0] & 3]; }

void Seed(uint32_t seed)
{
    std::mt19937 g(seed);
    World &w = gWorld;
    for (unsigned k = 0; k < 2; ++k)
        for (d3dfx::Buffer &b : w.buffer[k]) {
            b = d3dfx::Buffer{};
            b.index = k == 1;
            b.desc.Format = k == 1 ? D3DFMT_INDEX16 : D3DFMT_VERTEXDATA;
            b.desc.Type = k == 1 ? D3DRTYPE_INDEXBUFFER : D3DRTYPE_VERTEXBUFFER;
            b.desc.Usage = Pick(g, {0, 0x10, 0x208, 0x218, 8, 0});
            b.desc.Pool = D3DPOOL(Pick(g, {0, 1, 2, 3, 1, 2}));
            b.desc.Size = Pick(g, {0, 64, 960, 4096, 65536});
            b.desc.FVF = g() % 4 == 0 ? 0x142 : 0;
            b.descHr = g() % 8 == 0 ? d3dfx::kResults[2 + g() % 2] : 0;
            b.lockHr = g() % 8 == 0 ? d3dfx::kResults[2 + g() % 2] : 0;
            b.unlockHr = g() % 8 == 0 ? d3dfx::kResults[2 + g() % 2] : 0;
            b.priority = g() % 4;
        }
    w.ready = g() % 10 != 0;
    for (uint32_t &m : w.modes) m = g() % 2;
    w.out = nullptr;
}

// The Create* script: per slot, results and objects.
void Script(uint32_t seed, d3dfx::Script &s)
{
    std::mt19937 g(seed ^ 0x5eed);
    for (unsigned k = 0; k < 2; ++k) {
        const unsigned slot = k == 0 ? fx_d3d9::dev::CreateVertexBuffer : fx_d3d9::dev::CreateIndexBuffer;
        for (unsigned i = 0; i < 6; ++i) {
            uint32_t hr = 0;
            if (g() % 4 == 0) hr = Pick(g, {kQuiet[0], kQuiet[1], kQuiet[2], 0x80004005, 0x8876086c, 1});
            bool quiet = false;
            for (uint32_t q : kQuiet) quiet = quiet || q == hr;
            drawfx::Fake *fake = &gD3DBuf[k][g() % kBuffers];
            if (quiet || (int32_t(hr) < 0 && g() % 2)) fake = nullptr;
            s.Add(slot, hr, fake);
        }
    }
}

void Build(uint32_t seed)
{
    gDevice.Reset<kDevice>(0x6d001);
    gDevice.AddFace(kIidDeviceDX, gDevice);
    gDevice.Returns(15, &gEnum);
    gDevice.Returns(16, &gEnum);
    gDevice.Returns(18, &gManager);
    gDevice.Returns(35, gWorld.ready);
    gDevice.Returns(48, &gD3D);
    gManager.Reset<kManager>(0x6d002);
    gEnum.Reset<kEnum>(0x6d003);
    gEnum.On(20, &Modes);
    gD3D.Reset<fx_d3d9::IDirect3DDevice9>(d3dfx::kDeviceId);
    for (unsigned k = 0; k < 2; ++k)
        for (unsigned i = 0; i < kBuffers; ++i) {
            d3dfx::MakeBuffer(gD3DBuf[k][i], 0xd3d70000 + k * 0x100 + i, gWorld.buffer[k][i]);
            drawfx::AddRange(gWorld.buffer[k][i].bytes, d3dfx::kBufferBytes, 0x7c000000 + (k * kBuffers + i) * 0x10000);
        }
    static d3dfx::Script *script = nullptr;
    delete script;
    script = new d3dfx::Script(gD3D);
    Script(seed, *script);
    drawfx::AddRange(&gWorld.out, sizeof(gWorld.out), 0x7a000000);
}

// ---- the pools ----
struct Pool {
    uint32_t rva, pageBytes, blockBytes;
    uint8_t *region = nullptr;
    unsigned next = 0;
    uint32_t saved[3] = {};
    void *vt[4] = {};
    std::vector<uint32_t> freed;
};
constexpr unsigned kPoolPages = 8;
Pool gPools[] = {{0x10ca160, 0x800, 0x1c}, {0x10ca16c, 0x1000, 0x28}, {0x10ca178, 0x800, 0x1c}, {0x10ca184, 0x1000, 0x28}};
Pool *PoolOf(void *self)
{
    for (Pool &p : gPools)
        if (GameAddress<void>(p.rva) == self) return &p;
    return nullptr;
}
RZFixedPage *InitPage(Pool &p)
{
    auto *page = reinterpret_cast<RZFixedPage *>(p.region + (p.next++ % kPoolPages) * p.pageBytes);
    page->next = page;
    page->prev = page;
    page->used = 0;
    uint8_t *base = reinterpret_cast<uint8_t *>(page);
    const uint32_t blocks = (p.pageBytes - 0x10) / p.blockBytes;
    for (uint32_t k = 0; k < blocks; ++k)
        *reinterpret_cast<void **>(base + 0x10 + k * p.blockBytes) =
            k + 1 < blocks ? base + 0x10 + (k + 1) * p.blockBytes : nullptr;
    page->freeList = base + 0x10;
    return page;
}
RZFixedPage *__fastcall FakeNewPage(void *self, void *) { return InitPage(*PoolOf(self)); }
void __fastcall FakeFreePage(void *self, void *, RZFixedPage *page)
{
    Pool &p = *PoolOf(self);
    p.freed.push_back(uint32_t(reinterpret_cast<uint8_t *>(page) - p.region));
}
// Slots 0 / 1 (Alloc, Free) are called directly by both sides, never through the vtable: a call here is an error.
void *__fastcall PoolAllocTrap(void *, void *)
{
    drawfx::gErrors.push_back("a pool's vtable slot 0 was called");
    return nullptr;
}
void __fastcall PoolFreeTrap(void *, void *, void *) { drawfx::gErrors.push_back("a pool's vtable slot 1 was called"); }
struct Pools {
    Pools()
    {
        for (Pool &p : gPools) {
            p.vt[0] = reinterpret_cast<void *>(&PoolAllocTrap);
            p.vt[1] = reinterpret_cast<void *>(&PoolFreeTrap);
            p.vt[2] = reinterpret_cast<void *>(&FakeNewPage);
            p.vt[3] = reinterpret_cast<void *>(&FakeFreePage);
            p.region = static_cast<uint8_t *>(
                VirtualAlloc(nullptr, kPoolPages * p.pageBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            std::memcpy(p.saved, GameAddress<void>(p.rva), sizeof(p.saved));
        }
    }
    ~Pools()
    {
        for (Pool &p : gPools) {
            std::memcpy(GameAddress<void>(p.rva), p.saved, sizeof(p.saved));
            VirtualFree(p.region, 0, MEM_RELEASE);
        }
    }
    static void Reset()
    {
        for (Pool &p : gPools) {
            p.next = 0;
            p.freed.clear();
            std::memset(p.region, 0xcd, kPoolPages * p.pageBytes);
            uint32_t *words = GameAddress<uint32_t>(p.rva);
            words[0] = W(p.vt);
            words[1] = W(InitPage(p));
            words[2] = 0;
        }
    }
    static void Note()
    {
        for (Pool &p : gPools) {
            const uint32_t *words = GameAddress<uint32_t>(p.rva);
            std::vector<uint32_t> w{words[1], words[2], p.next};
            for (unsigned i = 0; i < p.next && i < kPoolPages; ++i) {
                const auto *page = reinterpret_cast<const RZFixedPage *>(p.region + i * p.pageBytes);
                w.insert(w.end(), {W(page->next), W(page->prev), W(page->freeList), page->used});
            }
            w.insert(w.end(), p.freed.begin(), p.freed.end());
            NoteWords("pool", {});
            drawfx::gLog.back().words = std::move(w);
        }
    }
};

// ---- objects under test ----
enum Kind { kStaticVertex, kDynamicVertex, kStaticIndex, kDynamicIndex };
constexpr uint32_t kCreator[] = {0x165fa0, 0x165ea0, 0x166000, 0x165f20};
constexpr uint32_t kObjectSize[] = {0x1c, 0x28, 0x1c, 0x28};
bool IsDynamic(Kind k) { return k == kDynamicVertex || k == kDynamicIndex; }
bool IsIndex(Kind k) { return k == kStaticIndex || k == kDynamicIndex; }

void *NewObject(difftest::Context &ctx, Kind k, bool original)
{
    if (original) return ctx.Original<void *(__cdecl *)()>(kCreator[k])();
    switch (k) {
    case kStaticVertex: return cStaticVertexComponentArrayDX::New();
    case kDynamicVertex: return cDynamicVertexComponentArrayDX::New();
    case kStaticIndex: return cStaticVertexIndexArrayDX::New();
    default: return cDynamicVertexIndexArrayDX::New();
    }
}

void NoteObject(const char *label, const void *p, Kind k)
{
    std::vector<uint32_t> w;
    const uint32_t *words = static_cast<const uint32_t *>(p);
    for (uint32_t off = 8; off < kObjectSize[k]; off += 4) w.push_back(N(words[off / 4]));
    NoteWords(label, {});
    drawfx::gLog.back().words = std::move(w);
}
void NoteRefs()
{
    std::vector<uint32_t> refs;
    for (drawfx::Fake *f : drawfx::gFakes) refs.push_back(uint32_t(f->refs));
    NoteWords("refs", {});
    drawfx::gLog.back().words = std::move(refs);
}

struct Seen {
    unsigned creates = 0, failedCreates = 0, locks = 0, discards = 0, reports = 0, modeChanges = 0, exactEnds = 0;
};

// IIDs asked of the objects (answered or not).
constexpr uint32_t kIids[] = {1,          0x973e60f,  0xda02e09f, 0xfb7a964e, 0x5b7a96c6, 0xbb813fed,
                              0x998d7e9,  0x4998d917, 0xcb60e61d, 0xfbffec42, 0xfbfffcd7, 0xdbfff0de,
                              0x3c0437f9, 0x7c0503f9, 0x9b71bf9d, 0x12345678};
bool Counted(uint32_t iid) { return iid != 0x998d7e9 && iid != 0x4998d917 && iid != 0xcb60e61d; }

// One QueryInterface (null out sometimes, except for the buffer ids, answered before the null check).
void Query(std::mt19937 &g, void *obj)
{
    const uint32_t iid = kIids[g() % std::size(kIids)];
    void **out = (g() % 8 == 0 && iid != 0x998d7e9 && iid != 0x4998d917) ? nullptr : &gWorld.out;
    gWorld.out = nullptr;
    const bool r = RawSlot<0, bool>(obj, iid, out);
    drawfx::Note("QueryInterface", {iid, r, N(W(gWorld.out))});
    if (r && out && Counted(iid) && gWorld.out) RawSlot<2>(gWorld.out);
}

bool Create(std::mt19937 &g, void *a, Kind k)
{
    const uint32_t size = Pick(g, {64, 960, 4096, 65536});
    const uint32_t kind = g() % 4;
    bool r = IsDynamic(k) ? RawSlot<23, bool>(a, &gDevice, size, kind) : RawSlot<20, bool>(a, &gDevice, size, kind);
    drawfx::Note("created", r);
    const bool held = RawSlot<6, bool>(a); // a non-quiet failure registers the array without a buffer
    NoteResult(held);
    return held;
}

void ArrayOp(std::mt19937 &g, void *a, Kind k, bool &created, Seen &seen)
{
    const unsigned op = g() % 22;
    const bool need = (op >= 8 && op <= 16) || op == 20;
    if (need && !created) return;
    switch (op) {
    case 0: Query(g, a); break;
    case 1: RawSlot<3, void>(a); created = false; break;
    case 2: NoteResult(N(RawSlot<4>(a))); NoteResult(N(RawSlot<5>(a))); break;
    case 3: NoteResult(RawSlot<6, bool>(a)); break;
    case 4: NoteResult(RawSlot<7>(a)); break;
    case 5: NoteResult(RawSlot<17>(a)); NoteResult(RawSlot<19>(a)); break;
    case 6:
        if (IsDynamic(k)) NoteResult(RawSlot<9, bool>(a)), NoteResult(RawSlot<18>(a));
        break;
    case 7:
        if (IsDynamic(k)) NoteResult(RawSlot<21>(a)), NoteResult(RawSlot<22>(a));
        break;
    case 8: NoteResult(RawSlot<8, bool>(a)); break;
    case 9:
        ++seen.modeChanges;
        NoteResult(RawSlot<9, bool>(a));
        if (!IsDynamic(k)) NoteResult(RawSlot<18>(a));
        break;
    case 10: NoteResult(RawSlot<10>(a, uint32_t(g() % 5))); RawSlot<11, void>(a); NoteResult(RawSlot<14>(a)); break;
    case 11: NoteResult(RawSlot<12>(a)); break;
    case 12: NoteResult(RawSlot<13>(a)); break;
    case 13: case 14: {
        ++seen.locks;
        const uint32_t write = Pick(g, {0, 1, 0x100, 0xffffff00});
        const uint32_t offset = Pick(g, {0, 4, 64, 1000, 5000});
        const uint32_t size = Pick(g, {0, 12, 960});
        NoteResult(N(W(RawSlot<15, void *>(a, write, offset, size))));
        NoteResult(RawSlot<19>(a));
        break;
    }
    case 15: RawSlot<16, void>(a); NoteResult(RawSlot<19>(a)); break;
    case 16: NoteResult(RawSlot<18>(a)); break;
    case 17: case 18: {
        // The +4 face: 3 / 4 the primary, 5 the device, 6 the buffer; QueryInterface, AddRef, Release through it.
        void *face = nullptr;
        const bool r = RawSlot<0, bool>(a, IsIndex(k) ? kIidVertexIndexArrayDX : kIidVertexComponentArrayDX, &face);
        NoteResult(r);
        if (!r || !face) break;
        NoteResult(N(RawSlot<3>(face)) == N(W(a)));
        NoteResult(N(RawSlot<4>(face)) == N(W(a)));
        NoteResult(N(RawSlot<5>(face)));
        NoteResult(N(RawSlot<6>(face)));
        Query(g, face);
        NoteResult(RawSlot<1>(face));
        NoteResult(RawSlot<2>(face));
        NoteResult(RawSlot<2>(face));
        break;
    }
    case 19: NoteResult(RawSlot<1>(a)); NoteResult(RawSlot<2>(a)); break;
    case 20:
        if (!IsDynamic(k)) break;
        {
            // Spans ending exactly at the buffer's end (32 x 2 or 16 x 4 of 64 bytes, 24 x 40 of 960) as well as
            // past it: the end itself still fits.
            const uint32_t x = Pick(g, {1, 2, 12, 24, 32, 7, 16}), n = Pick(g, {0, 1, 2, 3, 4, 40, 100, 3000});
            const uint32_t before = RawSlot<22>(a);
            NoteResult(RawSlot<20>(a, x, n));
            if (RawSlot<22>(a) < before || (RawSlot<22>(a) == 0 && before == 0)) ++seen.discards;
            // offset + span bytes == capacity, with a span (slots 22, 18, 21)
            if (RawSlot<18>(a) && RawSlot<22>(a) + RawSlot<18>(a) == RawSlot<21>(a)) ++seen.exactEnds;
        }
        break;
    case 21: break;
    }
}

struct After {
    drawfx::Log log;
    std::vector<std::string> errors;
    std::vector<uint32_t> buffers;
};

template <class Run> void Case(difftest::Context &ctx, uint32_t seed, Seen &seen, Run run)
{
    auto side = [&](bool original) {
        After a;
        Seed(seed);
        Pools::Reset();
        auto [log, errors] = drawfx::Side([&] { Build(seed); }, [&] {
            drawfx::gLog.reserve(256);
            run(original);
            NoteRefs();
            Pools::Note();
        });
        a.log = std::move(log);
        a.errors = std::move(errors);
        for (auto &kind : gWorld.buffer)
            for (const d3dfx::Buffer &b : kind) a.buffers.insert(a.buffers.end(), {uint32_t(b.locks), b.priority});
        return a;
    };
    const After a = side(true);
    const After b = side(false);
    for (const drawfx::Call &c : a.log) {
        if (!c.iface && std::strcmp(c.note, "LogD3DError") == 0) ++seen.reports;
        if (!c.iface && std::strcmp(c.note, "created") == 0) (c.words[0] ? seen.creates : seen.failedCreates)++;
    }
    std::vector<std::string> errors = a.errors;
    errors.insert(errors.end(), b.errors.begin(), b.errors.end());
    if (!drawfx::Same(ctx, a.log, b.log, "Arrays", seed, errors)) return;
    if (a.buffers != b.buffers) ctx.Fail("Arrays seed %u: buffer lock counts or priorities differ", seed);
}

} // namespace

DIFF_TEST(gz_dx_Arrays, 0x165fa0)
{
    Pools pools;
    d3dfx::ErrorLog errors(ctx);
    Seen seen;
    for (uint32_t seed = 1; seed <= 2000; ++seed) {
        Case(ctx, seed, seen, [&](bool original) {
            std::mt19937 g(seed * 31 + 7);
            const Kind ka = Kind(g() % 4), kb = Kind(g() % 4);
            void *a = NewObject(ctx, ka, original);
            void *b = NewObject(ctx, kb, original);
            NoteObject("new a", a, ka);
            NoteObject("new b", b, kb);
            RawSlot<1>(a);
            RawSlot<1>(b);
            bool ca = Create(g, a, ka), cb = Create(g, b, kb);
            NoteObject("a", a, ka);
            NoteObject("b", b, kb);
            for (unsigned step = 0; step < 16; ++step) {
                const bool onA = g() % 3 != 0;
                void *s = onA ? a : b;
                const Kind k = onA ? ka : kb;
                bool &created = onA ? ca : cb;
                if (g() % 10 == 0)
                    created = Create(g, s, k);
                else
                    ArrayOp(g, s, k, created, seen);
                NoteObject("a", a, ka);
                NoteObject("b", b, kb);
            }
            NoteResult(RawSlot<2>(a));
            NoteResult(RawSlot<2>(b));
            // The blocks after the destructors (back in their pools: word 0 the free list, +4 a vtable, the rest
            // what the destructors left).
            NoteObject("freed a", a, ka);
            NoteObject("freed b", b, kb);
        });
    }
    if (!seen.creates || !seen.failedCreates || !seen.locks || !seen.discards || !seen.reports || !seen.modeChanges ||
        !seen.exactEnds)
        ctx.Fail("Arrays: a path never ran (creates %u, failed %u, locks %u, discards %u, reports %u, mode changes %u, "
                 "spans ending at the buffer's end %u)",
                 seen.creates, seen.failedCreates, seen.locks, seen.discards, seen.reports, seen.modeChanges,
                 seen.exactEnds);
}
