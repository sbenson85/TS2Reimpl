// Differential tests for the vertex formats and declarations as whole classes (lane NJ, rendering R3 part 3):
// src/gz/cVertexFormat.h (cVertexFormat, cVertexFormatsManager, cVertexDeclarationDX, cVertexDeclarationsManagerDX).
//
// gz_dx_VertexFormats: per case, a formats manager on each side (the original's: our object's layout with the game's
// vtables on the manager and its builder, so every slot runs the original at its RVA; ours: our class) runs the same
// seeded sequence through its vtable: formats built (BeginFormat, AddElement in a shuffled order from a few recipes,
// so equal formats come back and the sort decides, EndFormat), formats released, every format slot (elements by value,
// searches, sizes, offsets, Describe into a string), the manager's queries, and finally the destructor. The RVAs it
// verifies: the getters 0x16db40 / 0x16e760 (their guards set initialised: the static's address each call, against
// one object of ours carrying our vtable), the usage names 0x18dbe0 (every usage id and others), the format's
// 0x16dcd0 0x16dd30 0x16dc60 0x16dc80 0x16dca0 0x16d380 0x16d3b0 0x16d440 0x16d7d0 0x16cfc0 0x16d590 0x16d4e0
// 0x16d510 0x16d550 0x16d000 0x16cde0, the manager's 0x16dd00 0x16cf70 0x16ce80 0x16d820 (with the sort key
// 0x16d5c0) 0x16db30 0x16db20 0x16db10 0x16ce10.
// gz_dx_VertexDeclarations: per case, formats made by each side's formats manager, then a declarations manager (the
// original's: our layout with the game's vtable, and the game's cached manager cell 0x10caa28 pointing at it, which
// the declarations' release reads through 0x18dd70; ours: our Instance(), which ours reads) runs a seeded sequence:
// GetDeclaration for the formats (device ready or not; CreateVertexDeclaration scripted with quiet and other failures,
// with or without an object), the declarations' slots (QueryInterface ids, ReleaseResource, GetMemorySize over a fake
// declaration's count and result, the getters), releases, the manager's queries and its semantic rename table, and the
// destructor. RVAs: 0x16e860 0x16e1d0 0x16e750 0x16e1b0 0x16df40 (the manager), 0x16e810 0x16e890 0x16e8b0 0x16e800
// 0x16e910 0x16e960 0x16e970 0x16e080 (the declaration).
//
// Compared: the log (every fake call with its arguments, the elements CreateVertexDeclaration is given, LogD3DError
// lines), each result (elements by value as all 16 bytes, strings by content), and after every step the managers'
// lists, every format's count, id and elements, every declaration's words after its vtable (device, Direct3D
// declaration, stream sizes, format by its id) and every fake's reference count; the declarations' pool state at the
// end. Left out, with the reason:
// - the objects' vtable words (each side its own);
// - the formats' addresses (each side's heap): formats are named by their place in the manager's list;
// - the vectors' capacities (an allocation detail, not state any caller reads);
// - element types past 5 in sizes and offsets: the original reads its six-entry size table past its end (the formats
//   manager's static object, not ours); cases with such types call only the slots that don't size them.
// The managers' constructors are not compared: both sides' objects are built by ours (the original's construction is
// the getters' first-call path, which needs the CRT's thread-safe static support the harness doesn't initialise).
#include "code_patch.h"
#include "d3dfx.h"
#include "difftest.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cVertexFormat.h"
#include "rz/RZFixedAllocator.h"
#include "rz/cRZString.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

using namespace nGZGraphic4;

namespace {

constexpr uint32_t kFormatVTable = 0xd9e8e0, kFormatsManagerVTable = 0xd9e92c;
constexpr uint32_t kDeclarationsManagerVTable = 0xd9e9f4;
constexpr uint32_t kDeclarationsCache = 0x10caa28; // 0x18dd70's cell
constexpr uint32_t kFormatsStatic = 0xfecfb4, kFormatsGuard = 0x10ca29c;
constexpr uint32_t kDeclarationsStatic = 0xfed22c, kDeclarationsGuard = 0x10ca2e8;

void *__cdecl GameNew(size_t size) { return ::operator new(size); }
void __cdecl GameDelete(void *p) { ::operator delete(p); }

// The original's element results (FindElement's "none") and its AddElement leave the padding bytes as whatever its
// stack held; ours writes zeros. The stack the original's frame will use is zeroed first, so its padding reads as zero
// too (the padding is compared).
__declspec(noinline) __declspec(safebuffers) void ZeroStack()
{
    volatile uint8_t pad[4096];
    for (size_t i = 0; i < sizeof(pad); ++i) pad[i] = 0;
}

uint32_t W(const void *p) { return drawfx::P(p); }
uint32_t Pick(std::mt19937 &g, std::initializer_list<uint32_t> v) { return v.begin()[g() % v.size()]; }
void NoteWords(const char *label, std::vector<uint32_t> words)
{
    drawfx::Call c;
    c.note = label;
    c.words = std::move(words);
    drawfx::gLog.push_back(std::move(c));
}
void NoteResult(uint32_t r) { drawfx::Note("result", r); }
void NoteText(const char *label, const char *text)
{
    drawfx::Call c;
    c.note = label;
    c.text = text ? text : "(null)";
    drawfx::gLog.push_back(std::move(c));
}

// Every usage id the engine names, and one it doesn't.
constexpr uint32_t kUsages[] = {0x5b830781, 0x3bd70105, 0xfbd70111, 0x3b83078b, 0xdb830795, 0xbb8307ab,
                                0x89d92ba0, 0x69d92b93, 0xdcf2cfdc, 0x5cf2cfe1, 0xcb6f3a6a, 0xeb720693,
                                0xcb7206a1, 0x114113c3, 0x114113cd, 0x12345678};
constexpr unsigned kUsageCount = sizeof(kUsages) / sizeof(kUsages[0]);

// ---- formats ----
// A format's state (the layout is the same on both sides): count, id, then per stream its size and elements.
void FormatWords(std::vector<uint32_t> &w, const void *p)
{
    const auto *f = static_cast<const cVertexFormat *>(p);
    w.push_back(f->mRefs);
    w.push_back(f->mId);
    w.push_back(uint32_t(f->mStreams.size()));
    for (const cVertexFormat::Stream &s : f->mStreams) {
        w.push_back(uint32_t(s.size()));
        for (const cVertexElement &e : s) {
            uint32_t words[4];
            std::memcpy(words, &e, sizeof(words));
            w.insert(w.end(), words, words + 4);
        }
    }
}

struct FormatsSide {
    alignas(16) uint8_t storage[sizeof(cVertexFormatsManager)];
    cVertexFormatsManager *m = nullptr;
    std::vector<void *> held; // formats the test holds (counted)

    void Make(bool original)
    {
        m = new (storage) cVertexFormatsManager();
        if (original) {
            *reinterpret_cast<void **>(m) = GameAddress<void>(kFormatsManagerVTable);
            *reinterpret_cast<void **>(m->mBuilder) = GameAddress<void>(kFormatVTable);
        }
        held.clear();
    }
    // A format by its place in the manager's list (0xf0000000 | place), the builder, or not there.
    uint32_t Name(const void *f) const
    {
        if (!f) return 0;
        for (size_t i = 0; i < m->mFormats.size(); ++i)
            if (m->mFormats[i] == f) return 0xf0000000u | uint32_t(i);
        if (f == m->mBuilder) return 0xfb000000u;
        return 0xfe000000u;
    }
    void Note() const
    {
        std::vector<uint32_t> w{m->mBuilding, uint32_t(m->mFormats.size())};
        FormatWords(w, m->mBuilder);
        for (const cIVertexFormat *f : m->mFormats) {
            if (f)
                FormatWords(w, f);
            else
                w.push_back(0xdeadu);
        }
        for (const void *f : held) w.push_back(Name(f));
        NoteWords("formats", std::move(w));
    }
};

// A recipe: elements (stream, type, usage, index), from a small seed space so formats repeat.
struct Element {
    uint32_t stream, type, usage, index;
};
std::vector<Element> Recipe(uint32_t recipe, bool oddTypes, uint32_t maxStream)
{
    std::mt19937 r(recipe * 977 + 13);
    const unsigned n = recipe == 7 ? 34 + r() % 10 : 1 + r() % 7;
    std::vector<Element> out;
    for (unsigned i = 0; i < n; ++i) {
        Element e;
        e.stream = recipe != 7 && r() % 3 == 0 ? r() % (maxStream + 1) : 0;
        e.type = oddTypes ? r() % 8 : r() % 6;
        e.usage = kUsages[r() % kUsageCount];
        e.index = r() % 4 == 0 ? r() % 10 : r() % 2;
        out.push_back(e);
    }
    return out;
}

// Builds a format through the manager's vtable (the same calls on both sides); answers it (counted) or null.
void *Build(std::mt19937 &g, FormatsSide &s, bool oddTypes, uint32_t maxStream)
{
    std::vector<Element> elements = Recipe(g() % 8, oddTypes, maxStream);
    // The order the elements come in (EndFormat sorts each stream).
    for (size_t i = elements.size(); i > 1; --i) std::swap(elements[i - 1], elements[g() % i]);
    if (g() % 4 != 0) RawSlot<3, void>(s.m);
    for (const Element &e : elements) {
        const uint32_t index = e.index | (g() % 2 ? (g() & 0xffffff00u) : 0); // only the low byte counts
        ZeroStack();
        RawSlot<4, void>(s.m, e.stream, e.type, e.usage, index);
    }
    void *out = reinterpret_cast<void *>(uintptr_t(0x0badf00d));
    ZeroStack();
    RawSlot<5, void>(s.m, &out);
    NoteResult(s.Name(out));
    return out;
}

void NoteElement(const char *label, const uint8_t *e, const void *answered)
{
    uint32_t words[4];
    std::memcpy(words, e, sizeof(words));
    NoteWords(label, {answered == e, words[0], words[1], words[2], words[3]});
}

// Describe into a string holding text already (Describe empties it first).
void Describe(void *f)
{
    alignas(16) uint8_t buffer[sizeof(cRZString) + 16];
    auto *text = reinterpret_cast<cRZString *>(buffer);
    text->Construct("left over");
    RawSlot<16, void>(f, text);
    NoteText("describe", reinterpret_cast<cIGZString *>(text)->ToChar());
    text->Destruct();
}

// One call of a format's slots (types past 5 only in the slots that don't size them).
void FormatOp(std::mt19937 &g, FormatsSide &s, void *f, bool oddTypes)
{
    const auto *v = static_cast<const cVertexFormat *>(f);
    const uint32_t streams = RawSlot<4>(f);
    const uint32_t stream = streams ? g() % streams : 0;
    const uint32_t count = streams ? RawSlot<5>(f, stream) : 0;
    // A usage and index the stream holds half the time.
    uint32_t usage = kUsages[g() % kUsageCount], index = g() % 3;
    if (count && g() % 2) {
        const cVertexElement &e = v->mStreams[stream][g() % count];
        usage = e.usage;
        index = e.index;
    }
    const uint32_t indexArg = index | (g() % 2 ? (g() & 0xffffff00u) : 0);
    alignas(16) uint8_t e[16];
    std::memset(e, 0xa5, sizeof(e));
    const unsigned op = g() % 13;
    if (oddTypes && op >= 7 && op <= 11) return;
    switch (op) {
    case 0: {
        const uint32_t iid = Pick(g, {1, kIidVertexFormat, kIidVertexFormatsManager, 0, 0x8b448b45});
        void *out = nullptr;
        const bool r = RawSlot<0, bool>(f, iid, g() % 6 == 0 ? nullptr : &out);
        NoteWords("QueryInterface", {iid, r, s.Name(out)});
        if (r && out) RawSlot<2>(out);
        break;
    }
    case 1: NoteWords("id, total", {RawSlot<3>(f), RawSlot<6>(f), streams}); break;
    case 2: NoteResult(count); break;
    case 3:
        if (!count) break;
        ZeroStack();
        NoteElement("GetElement", e, RawSlot<7, void *>(f, e, stream, uint32_t(g() % count)));
        break;
    case 4:
        if (!streams) break;
        ZeroStack();
        NoteElement("FindElement", e, RawSlot<8, void *>(f, e, stream, usage, indexArg));
        break;
    case 5:
        ZeroStack();
        NoteElement("FindElementAny", e, RawSlot<9, void *>(f, e, usage, indexArg));
        break;
    case 6: NoteResult(RawSlot<10>(f, usage)); break;
    case 7:
        if (streams) NoteResult(RawSlot<11>(f, stream));
        break;
    case 8:
        if (count) NoteResult(RawSlot<12>(f, stream, uint32_t(g() % count)));
        break;
    case 9:
        if (streams) NoteResult(RawSlot<13>(f, stream, int32_t(g() % (count + 2)) - 1)); // -1 to count
        break;
    case 10:
        if (!streams) break;
        ZeroStack();
        NoteResult(RawSlot<14>(f, stream, usage, indexArg));
        break;
    case 11:
        ZeroStack();
        NoteResult(RawSlot<15>(f, usage, indexArg));
        break;
    case 12: Describe(f); break;
    }
}

struct FormatsSeen {
    unsigned made = 0, reused = 0, dropped = 0, slotReuse = 0, describes = 0, long_ = 0;
};

void FormatsCase(uint32_t seed, bool original, FormatsSeen &seen)
{
    std::mt19937 g(seed * 31 + 7);
    const bool oddTypes = seed % 5 == 0;
    FormatsSide s;
    s.Make(original);
    s.Note();
    for (unsigned step = 0; step < 24; ++step) {
        const unsigned op = g() % 14;
        if (op <= 3) {
            const size_t before = s.m->mFormats.size();
            void *f = Build(g, s, oddTypes, 3);
            if (f) s.held.push_back(f);
            if (original && f) {
                const auto *v = static_cast<cVertexFormat *>(f);
                const bool fresh = v->mRefs == 2;
                (fresh ? seen.made : seen.reused)++;
                if (fresh && s.m->mFormats.size() == before) ++seen.slotReuse;
                for (const cVertexFormat::Stream &st : v->mStreams)
                    if (st.size() > 32) ++seen.long_; // past the sort's insertion sort
            }
        } else if (op <= 5) {
            if (s.held.empty()) continue;
            const size_t i = g() % s.held.size();
            NoteResult(RawSlot<2>(s.held[i]));
            s.held.erase(s.held.begin() + i);
        } else if (op == 6) {
            if (s.held.empty()) continue;
            void *f = s.held[g() % s.held.size()];
            NoteResult(RawSlot<1>(f));
            NoteResult(RawSlot<2>(f));
        } else if (op <= 10) {
            if (s.held.empty()) continue;
            if (original && s.held.size()) ++seen.describes;
            FormatOp(g, s, s.held[g() % s.held.size()], oddTypes);
        } else if (op == 11) {
            const uint32_t count = RawSlot<7>(s.m);
            const uint32_t i = g() % count;
            const uint32_t iid = Pick(g, {1, kIidVertexFormatsManager, kIidVertexFormat, 0});
            void *out = nullptr;
            const bool r = RawSlot<0, bool>(s.m, iid, g() % 5 == 0 ? nullptr : &out);
            NoteWords("manager", {RawSlot<6, bool>(s.m), count, s.Name(RawSlot<8, void *>(s.m, i)), iid, r,
                                  out == s.m ? 1u : W(out)});
            if (r && out) RawSlot<2>(out);
        } else if (op == 12) {
            const size_t before = std::count(s.m->mFormats.begin(), s.m->mFormats.end(), nullptr);
            RawSlot<3, void>(s.m); // BeginFormat alone: drops what only the manager holds
            if (original && size_t(std::count(s.m->mFormats.begin(), s.m->mFormats.end(), nullptr)) > before)
                ++seen.dropped;
        } else {
            // Elements left in the builder for the next format.
            const Element e{g() % 2, g() % (oddTypes ? 8 : 6), kUsages[g() % kUsageCount], g() % 3};
            ZeroStack();
            RawSlot<4, void>(s.m, e.stream, e.type, e.usage, e.index);
        }
        s.Note();
    }
    // The release: the test's references, then witnesses on what the manager holds, the destructor (not freeing), and
    // the witnesses' counts after it.
    for (void *f : s.held) NoteResult(RawSlot<2>(f));
    s.held.clear();
    std::vector<void *> witnesses;
    for (cIVertexFormat *f : s.m->mFormats)
        if (f) witnesses.push_back(f);
    witnesses.push_back(s.m->mBuilder);
    for (void *w : witnesses) RawSlot<1>(w);
    RawSlot<9, void>(s.m, 0u);
    for (void *w : witnesses) NoteResult(RawSlot<2>(w));
}

// ---- declarations: the engine side ----
constexpr drawfx::Slot U{0, drawfx::Ret::Int, false, "", ""};
constexpr drawfx::Slot kQI{2, drawfx::Ret::Int, true, "QueryInterface", "ui"};
constexpr drawfx::Slot kAddRef{0, drawfx::Ret::Int, true, "AddRef", ""};
constexpr drawfx::Slot kRelease{0, drawfx::Ret::Int, true, "Release", ""};

inline constexpr drawfx::Slot kDeviceSlots[] = {
    kQI, kAddRef, kRelease, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "GetResourceManager", ""}, // 18
    U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "IsDeviceReady", ""}, // 35
    U, U, U, U, U, U, U, U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "GetD3DDevice", ""}, // 48
};
inline constexpr drawfx::Iface kDevice{"cIDeviceDX", 49, kDeviceSlots};

inline constexpr drawfx::Slot kManagerSlots[] = {
    kQI, kAddRef, kRelease, U, U,
    {1, drawfx::Ret::Int, true, "Insert", "i"}, // 5
    U,
    {1, drawfx::Ret::Int, true, "Erase", "i"}, // 7
};
inline constexpr drawfx::Iface kManager{"cIDeviceResourceManager", 8, kManagerSlots};

constexpr unsigned kDeclarationFakes = 6;
constexpr uint32_t kIidDeviceDX = 0x553b7cf;
constexpr uint32_t kQuiet[] = {0x8876017c, 0x8007000e, 0x88760827};

struct DeclarationWorld {
    d3dfx::Declaration decl[kDeclarationFakes];
    uint32_t ready;
    struct Create {
        uint32_t hr;
        int fake; // -1: none
    };
    std::vector<Create> creates;
    unsigned next = 0;
} gWorld;

// Two devices the sequence asks with (a declaration is found by device and format), and a third only the last step
// uses (so its declaration is always made).
drawfx::Fake gDevice, gDevice2, gDevice3, gManager, gD3D, gDecl[kDeclarationFakes];

// CreateVertexDeclaration(elements, out): the elements (to the end mark) go into the logged call in place of the
// pointer to them (the manager's scratch vector, a heap address); then the scripted answer.
uint32_t CreateDeclaration(drawfx::Fake &, const uint32_t *a)
{
    drawfx::Call &c = drawfx::gLog.back();
    c.words[0] = a[0] != 0;
    const auto *e = reinterpret_cast<const D3DVERTEXELEMENT9 *>(uintptr_t(a[0]));
    for (unsigned i = 0; e && i < 64; ++i) {
        uint32_t words[2];
        std::memcpy(words, &e[i], sizeof(words));
        c.words.insert(c.words.end(), words, words + 2);
        if (e[i].Stream == 0xff) break;
    }
    void **out = reinterpret_cast<void **>(uintptr_t(a[1]));
    DeclarationWorld::Create cr{uint32_t(E_OUTOFMEMORY), -1};
    if (gWorld.next < gWorld.creates.size()) cr = gWorld.creates[gWorld.next++];
    drawfx::Fake *fake = cr.fake >= 0 ? &gDecl[cr.fake] : nullptr;
    if (fake) ++fake->refs;
    if (out) *out = fake;
    return cr.hr;
}

void SeedWorld(uint32_t seed)
{
    std::mt19937 g(seed ^ 0xdec1);
    for (d3dfx::Declaration &d : gWorld.decl) {
        d.count = Pick(g, {0, 1, 3, 7, 17});
        d.hr = g() % 6 == 0 ? Pick(g, {0x8876086c, 0x80004005, 1}) : 0;
    }
    gWorld.ready = g() % 8 != 0;
    gWorld.creates.clear();
    gWorld.next = 0;
    for (unsigned i = 0; i < 12; ++i) {
        uint32_t hr = 0;
        if (g() % 4 == 0) hr = Pick(g, {kQuiet[0], kQuiet[1], kQuiet[2], 0x80004005, 0x8876086c, 1});
        int fake = int(g() % kDeclarationFakes);
        // No object only with the quiet failures: a declaration made without a Direct3D object is never unregistered,
        // so its entry outlives it (ORIGINAL-BUG in GetDeclaration), and a later search would read the freed block.
        if (g() % 3 == 0 && (hr == kQuiet[0] || hr == kQuiet[1] || hr == kQuiet[2])) fake = -1;
        gWorld.creates.push_back({hr, fake});
    }
}

void BuildWorld()
{
    const uint32_t ids[3] = {0x6d001, 0x6d003, 0x6d004};
    drawfx::Fake *devices[3] = {&gDevice, &gDevice2, &gDevice3};
    for (unsigned i = 0; i < 3; ++i) {
        drawfx::Fake &d = *devices[i];
        d.Reset<kDevice>(ids[i]);
        d.AddFace(kIidDeviceDX, d);
        d.Returns(18, &gManager);
        d.Returns(35, i == 2 || gWorld.ready);
        d.Returns(48, &gD3D);
    }
    gManager.Reset<kManager>(0x6d002);
    gD3D.Reset<fx_d3d9::IDirect3DDevice9>(d3dfx::kDeviceId);
    gD3D.On(fx_d3d9::dev::CreateVertexDeclaration, &CreateDeclaration);
    for (unsigned i = 0; i < kDeclarationFakes; ++i) d3dfx::MakeDeclaration(gDecl[i], 0xdec10000 + i, gWorld.decl[i]);
}

// ---- the declarations' pool (0xfed220, RZFastFixed0x58: 0x2000-byte pages), fresh for each side ----
constexpr uint32_t kPoolRva = cVertexDeclarationDX::kPoolRva, kPageBytes = 0x2000, kBlockBytes = 0x58;
constexpr unsigned kPoolPages = 4;
struct Pool {
    uint8_t *region = nullptr;
    unsigned next = 0;
    uint32_t saved[3] = {};
    void *vt[4] = {};
    std::vector<uint32_t> freed;
} gPool;

RZFixedPage *InitPage()
{
    auto *page = reinterpret_cast<RZFixedPage *>(gPool.region + (gPool.next++ % kPoolPages) * kPageBytes);
    page->next = page;
    page->prev = page;
    page->used = 0;
    uint8_t *base = reinterpret_cast<uint8_t *>(page);
    const uint32_t blocks = (kPageBytes - 0x10) / kBlockBytes;
    for (uint32_t k = 0; k < blocks; ++k)
        *reinterpret_cast<void **>(base + 0x10 + k * kBlockBytes) =
            k + 1 < blocks ? base + 0x10 + (k + 1) * kBlockBytes : nullptr;
    page->freeList = base + 0x10;
    return page;
}
RZFixedPage *__fastcall FakeNewPage(void *, void *) { return InitPage(); }
void __fastcall FakeFreePage(void *, void *, RZFixedPage *page)
{
    gPool.freed.push_back(uint32_t(reinterpret_cast<uint8_t *>(page) - gPool.region));
}
// Slots 0 / 1 (Alloc, Free) are called directly by both sides, never through the vtable: a call here is an error.
void *__fastcall PoolAllocTrap(void *, void *)
{
    drawfx::gErrors.push_back("the pool's vtable slot 0 was called");
    return nullptr;
}
void __fastcall PoolFreeTrap(void *, void *, void *) { drawfx::gErrors.push_back("the pool's vtable slot 1 was called"); }
struct PoolScope {
    PoolScope()
    {
        gPool.vt[0] = reinterpret_cast<void *>(&PoolAllocTrap);
        gPool.vt[1] = reinterpret_cast<void *>(&PoolFreeTrap);
        gPool.vt[2] = reinterpret_cast<void *>(&FakeNewPage);
        gPool.vt[3] = reinterpret_cast<void *>(&FakeFreePage);
        gPool.region = static_cast<uint8_t *>(
            VirtualAlloc(nullptr, kPoolPages * kPageBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        std::memcpy(gPool.saved, GameAddress<void>(kPoolRva), sizeof(gPool.saved));
    }
    ~PoolScope()
    {
        std::memcpy(GameAddress<void>(kPoolRva), gPool.saved, sizeof(gPool.saved));
        VirtualFree(gPool.region, 0, MEM_RELEASE);
    }
    PoolScope(const PoolScope &) = delete;
    static void Reset()
    {
        gPool.next = 0;
        gPool.freed.clear();
        std::memset(gPool.region, 0xcd, kPoolPages * kPageBytes);
        uint32_t *words = GameAddress<uint32_t>(kPoolRva);
        words[0] = W(gPool.vt);
        words[1] = W(InitPage());
        words[2] = 0;
    }
    static void Note()
    {
        const uint32_t *words = GameAddress<uint32_t>(kPoolRva);
        std::vector<uint32_t> w{drawfx::Norm(words[1]), drawfx::Norm(words[2]), gPool.next};
        for (unsigned i = 0; i < gPool.next && i < kPoolPages; ++i) {
            const auto *page = reinterpret_cast<const RZFixedPage *>(gPool.region + i * kPageBytes);
            w.insert(w.end(), {drawfx::Norm(W(page->next)), drawfx::Norm(W(page->prev)), drawfx::Norm(W(page->freeList)),
                               page->used});
        }
        w.insert(w.end(), gPool.freed.begin(), gPool.freed.end());
        NoteWords("pool", std::move(w));
    }
};

// ---- declarations: one side ----
struct DeclarationsSide {
    FormatsSide formats;
    std::vector<void *> formatList; // the formats the case declares, held
    alignas(16) uint8_t storage[sizeof(cVertexDeclarationsManagerDX)];
    cVertexDeclarationsManagerDX *m = nullptr;
    bool original = false;
    std::vector<void *> held; // declarations the test holds (counted)

    uint32_t FormatName(const void *f) const { return formats.Name(f); }
    void Note() const
    {
        std::vector<uint32_t> w{uint32_t(m->mDeclarations.size()), uint32_t(m->mElements.size())};
        for (const cIVertexDeclarationDX *d : m->mDeclarations) w.push_back(drawfx::Norm(W(d)));
        auto words = [&](const void *p) {
            const auto *d = static_cast<const cVertexDeclarationDX *>(p);
            w.insert(w.end(), {drawfx::Norm(W(d)), d->mRefs, d->mId, drawfx::Norm(W(d->mDevice)),
                               drawfx::Norm(W(d->mDeclaration)), FormatName(d->mFormat)});
            if (d->mDeclaration) w.insert(w.end(), d->mStreamSizes, d->mStreamSizes + 16);
        };
        for (const cIVertexDeclarationDX *d : m->mDeclarations)
            if (d) words(d);
        for (const void *d : held) words(d);
        for (const void *f : formatList) w.push_back(static_cast<const cVertexFormat *>(f)->mRefs);
        NoteWords("declarations", std::move(w));
    }
};

struct DeclarationsSeen {
    unsigned made = 0, creates = 0, refused = 0, notReady = 0, releases = 0, reports = 0, memory = 0, noObject = 0;
};

void NoteRenames(const char *const *t)
{
    drawfx::Call c;
    c.note = "renames";
    unsigned n = 0;
    for (; t[n] || t[n + 1]; n += 2) {
        c.text += t[n];
        c.text += '\x1f';
        c.text += t[n + 1] ? t[n + 1] : "(null)";
        c.text += '\x1f';
    }
    c.words.push_back(n);
    drawfx::gLog.push_back(std::move(c));
}

void DeclarationOp(std::mt19937 &g, DeclarationsSide &s, DeclarationsSeen &seen)
{
    const unsigned op = g() % 16;
    if (op <= 5) {
        void *f = s.formatList[g() % s.formatList.size()];
        void *out = reinterpret_cast<void *>(uintptr_t(0x0badf00d));
        drawfx::Fake &device = g() % 3 == 0 ? gDevice2 : gDevice;
        const bool r = RawSlot<3, bool>(s.m, &device, f, &out);
        NoteWords("GetDeclaration", {s.FormatName(f), r, r ? drawfx::Norm(W(out)) : 0, &device == &gDevice2});
        if (r) s.held.push_back(out);
        if (s.original) {
            if (!gWorld.ready) ++seen.notReady;
        }
        return;
    }
    if (op == 6) {
        gWorld.ready = !gWorld.ready;
        gDevice.Returns(35, gWorld.ready);
        gDevice2.Returns(35, gWorld.ready);
        return;
    }
    if (op == 7) {
        const uint32_t count = RawSlot<4>(s.m);
        const uint32_t iid = Pick(g, {1, kIidVertexDeclarationsManager, kIidVertexDeclarationsManagerDX, 0, kIidVertexFormat});
        void *out = nullptr;
        const bool r = RawSlot<0, bool>(s.m, iid, g() % 5 == 0 ? nullptr : &out);
        NoteWords("manager", {count, drawfx::Norm(W(RawSlot<5, void *>(s.m, uint32_t(g() % count)))), iid, r,
                              out == s.m ? 1u : W(out)});
        if (r && out) RawSlot<2>(out);
        NoteRenames(RawSlot<6, const char *const *>(s.m));
        return;
    }
    if (s.held.empty()) return;
    const size_t at = g() % s.held.size();
    void *d = s.held[at];
    switch (op) {
    case 8:
    case 9:
        NoteResult(RawSlot<2>(d));
        s.held.erase(s.held.begin() + at);
        if (s.original) ++seen.releases;
        break;
    case 10: {
        const uint32_t iid =
            Pick(g, {1, kIidVertexDeclaration, kIidVertexDeclarationDX, 0xda02e09f, kIidD3DVertexDeclaration, 0x12345, 0});
        void *out = nullptr;
        const bool nullOut = iid != kIidD3DVertexDeclaration && g() % 6 == 0; // that id writes before any null check
        const bool r = RawSlot<0, bool>(d, iid, nullOut ? nullptr : &out);
        NoteWords("QueryInterface", {iid, r, drawfx::Norm(W(out))});
        if (r && out && iid != kIidD3DVertexDeclaration) RawSlot<2>(out);
        break;
    }
    case 11: RawSlot<3, void>(d); break; // ReleaseResource
    case 12:
        NoteWords("getters", {drawfx::Norm(RawSlot<4>(d)), drawfx::Norm(RawSlot<5>(d)), RawSlot<6, bool>(d),
                              RawSlot<8, bool>(d), RawSlot<9, bool>(d), RawSlot<10>(d), RawSlot<11>(d, uint32_t(g() % 16)),
                              s.FormatName(RawSlot<12, void *>(d)), drawfx::Norm(RawSlot<13>(d)),
                              drawfx::Norm(RawSlot<14>(d))});
        break;
    case 13:
        if (s.original) ++seen.memory;
        NoteResult(RawSlot<7>(d));
        break;
    default:
        NoteResult(RawSlot<1>(d));
        NoteResult(RawSlot<2>(d));
        break;
    }
}

} // namespace

DIFF_TEST(gz_dx_VertexFormats, 0x16d820)
{
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNew)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDelete)};

    // The getters: the originals with their statics' guards set to "initialised" (INT_MIN: the thread-safe static's
    // fast path, so the constructors and atexit registrations don't run) hand out their statics, every time; ours one
    // object each, every time, carrying our vtable.
    {
        using Getter = void *(__cdecl *)();
        const struct {
            uint32_t rva, guard, object;
            void *ours, *ours2;
        } getters[] = {
            {cVertexFormatsManager::kGetterRva, kFormatsGuard, kFormatsStatic, cVertexFormatsManager::Instance(),
             cVertexFormatsManager::Instance()},
            {cVertexDeclarationsManagerDX::kGetterRva, kDeclarationsGuard, kDeclarationsStatic,
             cVertexDeclarationsManagerDX::Instance(), cVertexDeclarationsManagerDX::Instance()},
        };
        for (const auto &x : getters) {
            int32_t *const guard = ctx.image.At<int32_t>(x.guard);
            const int32_t savedGuard = *guard;
            *guard = INT_MIN;
            const Getter getter = ctx.Original<Getter>(x.rva);
            void *const a = getter(), *const b = getter();
            *guard = savedGuard;
            ++ctx.cases;
            if (a != b || a != static_cast<void *>(ctx.image.At<uint8_t>(x.object)))
                ctx.Fail("original getter 0x%x: %p %p", x.rva, a, b);
            ++ctx.cases;
            if (x.ours != x.ours2 || !x.ours || *static_cast<void **>(x.ours) == GameAddress<void>(kFormatsManagerVTable) ||
                *static_cast<void **>(x.ours) == GameAddress<void>(kDeclarationsManagerVTable))
                ctx.Fail("Instance 0x%x: not one object with our vtable", x.rva);
        }
    }

    // The usage names, by content.
    {
        using Name = const char *(__cdecl *)(uint32_t);
        const Name original = ctx.Original<Name>(0x18dbe0);
        std::vector<uint32_t> ids(std::begin(kUsages), std::end(kUsages));
        std::mt19937 g(5);
        for (int i = 0; i < 200; ++i) ids.push_back(g());
        for (uint32_t u : kUsages) ids.insert(ids.end(), {u - 1, u + 1});
        for (uint32_t u : ids) {
            ++ctx.cases;
            const char *a = original(u), *b = VertexUsageName(u);
            if (std::strcmp(a, b) != 0) ctx.Fail("usage name 0x%08x: orig %s ours %s", u, a, b);
        }
    }

    // EndFormat's sort key 0x16d5c0 (cdecl, the element by address), compared directly: the recipes reach only some of
    // its special cases (two colours of type 5 in one stream, texture coordinates up to 7 against the usage bands), so
    // every usage id and its neighbours, indices 0-12 and 255 (it reads the index byte) and types 0-8.
    {
        using Key = int32_t(__cdecl *)(const cVertexElement *);
        const Key original = ctx.Original<Key>(0x16d5c0);
        std::vector<uint32_t> ids(std::begin(kUsages), std::end(kUsages));
        for (uint32_t u : kUsages) ids.insert(ids.end(), {u - 1, u + 1});
        ids.push_back(0);
        for (uint32_t u : ids)
            for (uint32_t index : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 255u})
                for (uint32_t type = 0; type < 9; ++type) {
                    cVertexElement e{};
                    e.usage = u;
                    e.index = static_cast<uint8_t>(index);
                    e.type = type;
                    ++ctx.cases;
                    const int32_t a = original(&e), b = format_detail::SortKey(e);
                    if (a != b) ctx.Fail("sort key 0x%08x index %u type %u: orig %d ours %d", u, index, type, a, b);
                }
    }

    FormatsSeen seen;
    for (uint32_t seed = 1; seed <= 1500; ++seed) {
        auto side = [&](bool original) {
            auto [log, errors] = drawfx::Side([] {}, [&] {
                drawfx::gLog.reserve(512);
                FormatsCase(seed, original, seen);
            });
            return std::make_pair(std::move(log), std::move(errors));
        };
        const auto a = side(true);
        const auto b = side(false);
        std::vector<std::string> errors = a.second;
        errors.insert(errors.end(), b.second.begin(), b.second.end());
        drawfx::Same(ctx, a.first, b.first, "VertexFormats", seed, errors);
    }
    if (!seen.made || !seen.reused || !seen.dropped || !seen.slotReuse || !seen.describes || !seen.long_)
        ctx.Fail("VertexFormats: a path never ran (made %u, reused %u, dropped %u, empty entries reused %u, format "
                 "slots %u, long streams %u)",
                 seen.made, seen.reused, seen.dropped, seen.slotReuse, seen.describes, seen.long_);
}

DIFF_TEST(gz_dx_VertexDeclarations, 0x16e1d0)
{
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNew)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDelete)};
    PoolScope pool;
    d3dfx::ErrorLog errors(ctx);
    DeclarationsSeen seen;
    for (uint32_t seed = 1; seed <= 1500; ++seed) {
        auto side = [&](bool original) {
            SeedWorld(seed);
            PoolScope::Reset();
            DeclarationsSide s;
            s.original = original;
            drawfx::Cells cells;
            auto [log, errs] = drawfx::Side([&] { BuildWorld(); }, [&] {
                drawfx::gLog.reserve(512);
                drawfx::AddRange(gPool.region, kPoolPages * kPageBytes, 0x7d000000);
                std::mt19937 g(seed * 17 + 3);
                // The formats: two to four, from the recipes (16 streams at most: the declaration keeps 16 sizes).
                s.formats.Make(original);
                const unsigned n = 2 + g() % 3;
                for (unsigned i = 0; i < n; ++i) {
                    void *f = Build(g, s.formats, false, g() % 4 == 0 ? 15 : 3);
                    if (f) s.formatList.push_back(f);
                }
                if (original) {
                    s.m = new (s.storage) cVertexDeclarationsManagerDX();
                    *reinterpret_cast<void **>(s.m) = GameAddress<void>(kDeclarationsManagerVTable);
                    cells.Set(kDeclarationsCache, s.m);
                } else {
                    s.m = cVertexDeclarationsManagerDX::Instance();
                }
                s.Note();
                for (unsigned step = 0; step < 24; ++step) {
                    DeclarationOp(g, s, seen);
                    s.Note();
                }
                // Half the cases end with a declaration made without a Direct3D object (a failure other than the
                // quiet codes, ORIGINAL-BUG in GetDeclaration): its slots (IsCreated answers false; ReleaseResource
                // then does nothing), never released, because its release would leave a dangling entry that the
                // teardown's notes would read. Its block stays used in the pool and its format keeps a reference
                // (both compared).
                if (g() % 2 == 0) {
                    gWorld.creates.insert(gWorld.creates.begin() + std::min<size_t>(gWorld.next, gWorld.creates.size()),
                                          {0x80004005u, -1});
                    void *f = s.formatList[g() % s.formatList.size()];
                    void *out = reinterpret_cast<void *>(uintptr_t(0x0badf00d));
                    const bool r = RawSlot<3, bool>(s.m, &gDevice3, f, &out);
                    NoteWords("GetDeclaration (no object)", {s.FormatName(f), r, r ? drawfx::Norm(W(out)) : 0});
                    if (r) {
                        if (original) ++seen.noObject;
                        NoteWords("no object", {drawfx::Norm(RawSlot<4>(out)), RawSlot<6, bool>(out), RawSlot<7>(out),
                                                s.FormatName(RawSlot<12, void *>(out)), drawfx::Norm(RawSlot<14>(out))});
                        RawSlot<3, void>(out); // ReleaseResource
                        s.Note();
                    }
                }
                // The release: the test's declarations, then the formats, the managers' destructors (not freeing).
                for (void *d : s.held) NoteResult(RawSlot<2>(d));
                s.held.clear();
                s.Note();
                RawSlot<7, void>(s.m, 0u);
                if (!original) new (s.m) cVertexDeclarationsManagerDX();
                for (void *f : s.formatList) NoteResult(RawSlot<2>(f));
                RawSlot<9, void>(s.formats.m, 0u);
                PoolScope::Note();
            });
            return std::make_pair(std::move(log), std::move(errs));
        };
        const auto a = side(true);
        const auto b = side(false);
        for (const drawfx::Call &c : a.first) {
            if (!c.iface && std::strcmp(c.note, "LogD3DError") == 0) ++seen.reports;
            if (!c.iface && std::strcmp(c.note, "GetDeclaration") == 0 && c.words[1]) ++seen.made;
            if (c.iface && c.slot == fx_d3d9::dev::CreateVertexDeclaration) ++seen.creates;
            if (!c.iface && std::strcmp(c.note, "GetDeclaration") == 0 && !c.words[1]) ++seen.refused;
        }
        std::vector<std::string> errs = a.second;
        errs.insert(errs.end(), b.second.begin(), b.second.end());
        drawfx::Same(ctx, a.first, b.first, "VertexDeclarations", seed, errs);
    }
    if (!seen.made || !seen.creates || !seen.refused || !seen.notReady || !seen.releases || !seen.reports ||
        !seen.memory || !seen.noObject || seen.made <= seen.creates)
        ctx.Fail("VertexDeclarations: a path never ran (answered %u, created %u, refused %u, not ready %u, releases %u, "
                 "reports %u, memory sizes %u, made without an object %u; some answers must be found ones)",
                 seen.made, seen.creates, seen.refused, seen.notReady, seen.releases, seen.reports, seen.memory,
                 seen.noObject);
}
