// Differential tests of nGZSceneGraph::cSceneGraphSystem and its scene node iterator (lane OM, scenegraph N1 step 4;
// src/sg/cSceneGraphSystem.h): the game's system (its factory 0xa4cb40, constructor 0xa4c6f0, vtables 0xe6c474 / 0xe6c4b4
// / 0xe6c4ec and the bodies behind them; the iterator's vtable 0xe6c508) against ours (Create, our vtables), each side
// building its own system with its own factory and driving it through its faces.
//
//   SG_SceneGraphSystem_Set      AddSceneNode / RemoveSceneNode (13 / 14: the std::set at +0x20, its tree compared node
//       by node: shape, colours, nil flags, parents, values) and GetSceneNodes (3) with its iterators walked, cloned,
//       asked for interfaces and released (the pool 0x10f617c stood up by the test: blocks and page headers compared).
//   SG_SceneGraphSystem_Service  the service's life: the constructor (the factory static), QueryInterface through each
//       face, the getters, Init (the factory registrations, the string pool, the COM objects, the material parser, the
//       file parser's set-up, the tick), OnTick, Shutdown (the tick, the resource manager's cache generations, the
//       releases), Init again, and the destructor (its releases, the blocks it frees).
//
// The original's bodies these reach through the game's vtables (each side's objects come from its own factory):
// QueryInterface 0xa4d060 with its +4 / +8 adjustor thunks 0xa4d09d / 0xa4d0a5 (QueryFaces), the getters
// GetMaterialManager 0xa4d030, GetTextureManager 0xa4d300 and GetStringPool 0xa4d2f0, Shutdown 0xa4d190 and OnTick
// 0xa4d040 (_Service); GetSceneNodes 0xa4d0c0 and RemoveSceneNode 0xa4d0b0 (_Set); the destructor 0xa4c840 with the
// deleting destructor 0xa4c9e0 and its +8 thunk 0xa4c9d8 (Destroy: cRZUnknownMT's last Release deletes through slot 3);
// the scene node iterator's constructor 0xa4c820, copy constructor 0xa4c800, Clone 0xa4cb10, destructor 0xa4c920,
// deleting destructor 0xa4ca00 and its pool operator new / delete 0xa4c930 / 0xa4c960 (_Set: built, cloned, released).
//
// The game code the system still calls stands in on both sides (the original's calls reach the image, ours reach the
// image through OriginalFunction or our replacement, each stood in over): the factory static 0xa4d660 (a factory object
// whose cISceneGraphFactory face and its cIGZUnknown are fakes, through vbtables as the real one's are), the material,
// texture and string pool singletons 0xacc040 / 0xade2e0 / 0xadf8b0 (the material manager's getter ours since lane RD,
// the pool's since lane OX), the material parser's constructor 0xacec90 (ours since lane RH; both are stood in; lays
// out the parser's vbptrs and faces in the block Init allocates), and the COM getter 0x1ae18d (ours,
// GetDirectorFrameworkObject; a COM fake answering the four class ids). The framework and the resource manager are
// sgfx's (gResources; the framework's AddToTick / RemoveFromTick and the manager's slot 45 log).
//
// Left out of the word comparison (NoteSystem): words 0-2 (the vtables, each side's own) and word 8, the set's head (a
// heap address: the tree behind it is compared by NoteSet instead); word 7 is the base's running flag, a byte the base
// constructor 0x1b9584 stores alone (`mov byte ptr [edi + 0x18], 0`), so only its low byte is compared.
#include "sgfx.h"

#include "code_patch.h"
#include "free_recorder.h"

#include "gz/cIGZSystemService.h"
#include "rz/RZFixedAllocator.h"
#include "rz/StaticGetters.h"
#include "sg/cMaterialManager.h"
#include "sg/cMaterialParser.h"
#include "sg/cSceneGraphSystem.h"
#include "sg/cStringPool.h"
#include "sg/cTextureManager.h"

#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <random>
#include <vector>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;
using drawfx::Ret;
using drawfx::Slot;

#define OM_UNKNOWN                                                                                                     \
    {2, Ret::Int, true, "QueryInterface", "ui"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""}
#define OM_UNSETTLED {0, Ret::Int, false, "", ""}
#define OM_UNSETTLED4 OM_UNSETTLED, OM_UNSETTLED, OM_UNSETTLED, OM_UNSETTLED

// ---- the fakes ----
constexpr Slot kUnknownSlots[] = {OM_UNKNOWN};
constexpr drawfx::Iface kUnknown{"Unknown", 3, kUnknownSlots};
constexpr Slot kFactoryFaceSlots[] = {{1, Ret::Int, true, "RegisterClass", "u"}};
constexpr drawfx::Iface kFactoryFace{"SceneGraphFactory", 1, kFactoryFaceSlots};
// The singletons: only the slots the system calls are settled, all without arguments (the callers push none).
constexpr Slot kMaterialsSlots[] = {OM_UNKNOWN, OM_UNSETTLED4, OM_UNSETTLED4, OM_UNSETTLED4, // 3-14
                                    {0, Ret::Int, true, "Slot15", ""}, {0, Ret::Int, true, "Slot16", ""},
                                    {0, Ret::Int, true, "Slot17", ""}};
constexpr drawfx::Iface kMaterials{"MaterialManager", 18, kMaterialsSlots};
constexpr Slot kTexturesSlots[] = {OM_UNKNOWN, OM_UNSETTLED4, OM_UNSETTLED4, OM_UNSETTLED4, // 3-14
                                   {0, Ret::Int, true, "Slot15", ""}, {0, Ret::Int, true, "Slot16", ""}};
constexpr drawfx::Iface kTextures{"TextureManager", 17, kTexturesSlots};
constexpr Slot kStringsSlots[] = {OM_UNKNOWN, {0, Ret::Int, true, "Slot3", ""}, {0, Ret::Int, true, "Slot4", ""}};
constexpr drawfx::Iface kStrings{"StringPool", 5, kStringsSlots};
constexpr Slot kComSlots[] = {OM_UNKNOWN, {3, Ret::Int, true, "GetClassObject", "uui"}};
constexpr drawfx::Iface kCom{"COM", 4, kComSlots};
constexpr Slot kDisplayListSlots[] = {OM_UNKNOWN, OM_UNSETTLED4, OM_UNSETTLED4, OM_UNSETTLED4, // 3-14
                                      {0, Ret::Int, true, "Slot15", ""}, {0, Ret::Int, true, "Slot16", ""}};
constexpr drawfx::Iface kDisplayList{"DisplayList", 17, kDisplayListSlots};
constexpr Slot kFileParserSlots[] = {OM_UNKNOWN, {0, Ret::Int, true, "Init", ""}, {0, Ret::Int, true, "Shutdown", ""},
                                     {2, Ret::Int, true, "SetParser", "iu"}};
constexpr drawfx::Iface kFileParser{"FileParser", 6, kFileParserSlots};
constexpr Slot kLodSlots[] = {OM_UNKNOWN, OM_UNSETTLED, OM_UNSETTLED, {0, Ret::Int, true, "Slot5", ""},
                              {0, Ret::Int, true, "Slot6", ""}};
constexpr drawfx::Iface kLod{"LODLevelsStore", 7, kLodSlots};

constexpr uint32_t kSystemTag = 0x71000000;
constexpr uint32_t kParserTag = 0x72000000;  // | the parser's index << 12
constexpr uint32_t kFactoryTag = 0x73000000;
constexpr uint32_t kValueTag = 0x74000000;   // the scene node values
constexpr uint32_t kPoolTag = 0x75000000;

constexpr uint32_t kParserSize = 0x304;
constexpr unsigned kMaxParsers = 3;

uint32_t W(const void *p, unsigned word) { return reinterpret_cast<const uint32_t *>(p)[word]; }

// ---- the iterator pool (0x10f617c, a cRZFastFixedAllocator2<0xc>: pages of 0x800 bytes, 169 blocks each) ----
// The test's pages in a region of its own (random bytes, page 0 the current ring), NewPage / FreePage through a fake
// vtable; the pool's three words (vtable, current ring, full ring) saved and restored.
struct Pool {
    static constexpr uint32_t kRva = cSceneNodeIterator::kPool;
    static constexpr uint32_t kPage = 0x800;
    static constexpr uint32_t kBlock = cSceneNodeIterator::kSize;
    static constexpr uint32_t kBlocks = (kPage - 16) / kBlock;
    static constexpr int kPages = 4;
    static_assert(kBlocks == 169);
    static inline uint8_t *region = nullptr;
    static inline int next = 0;
    static inline std::vector<uint32_t> freed;
    static inline void *vt[4] = {};
    static inline uint32_t saved[3] = {};

    static uint32_t *Words() { return GameAddress<uint32_t>(kRva); }
    static RZFixedPage *Page(int i) { return reinterpret_cast<RZFixedPage *>(region + uint32_t(i) * kPage); }
    static uint32_t IndexOf(uint32_t p)
    {
        if (!p) return 0xff;
        const uint32_t d = p - P(region);
        return d < kPages * kPage && d % kPage == 0 ? d / kPage : 0xfe;
    }
    static void InitPage(RZFixedPage *p)
    {
        p->next = p;
        p->prev = p;
        p->used = 0;
        uint8_t *base = reinterpret_cast<uint8_t *>(p);
        for (uint32_t k = 0; k < kBlocks; ++k) {
            void **block = reinterpret_cast<void **>(base + 16 + k * kBlock);
            *block = k + 1 < kBlocks ? base + 16 + (k + 1) * kBlock : nullptr;
        }
        p->freeList = base + 16;
    }
    static RZFixedPage *__fastcall NewPage(void *, void *)
    {
        if (next >= kPages) {
            drawfx::gErrors.emplace_back("iterator pool: out of pages");
            return nullptr;
        }
        RZFixedPage *p = Page(next++);
        InitPage(p);
        return p;
    }
    static void __fastcall FreePage(void *, void *, RZFixedPage *p) { freed.push_back(IndexOf(P(p))); }

    static void Open()
    {
        std::memcpy(saved, Words(), sizeof(saved));
        region = static_cast<uint8_t *>(VirtualAlloc(nullptr, kPages * kPage, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        vt[2] = reinterpret_cast<void *>(&NewPage);
        vt[3] = reinterpret_cast<void *>(&FreePage);
    }
    static void Close()
    {
        std::memcpy(Words(), saved, sizeof(saved));
        VirtualFree(region, 0, MEM_RELEASE);
        region = nullptr;
    }
    static void Reset(uint32_t seed)
    {
        std::mt19937 rng(seed ^ kRva);
        for (uint32_t i = 0; i < kPages * kPage; i += 4) *reinterpret_cast<uint32_t *>(region + i) = rng();
        next = 1;
        freed.clear();
        InitPage(Page(0));
        Words()[0] = P(vt);
        Words()[1] = P(Page(0));
        Words()[2] = 0;
        drawfx::AddRange(region, kPages * kPage, kPoolTag);
    }
    // The rings, the pages handed out and freed, and each page's header (its blocks hold each side's own vtables and
    // tree nodes: the iterators are compared through their slots instead).
    static void Note(const char *label)
    {
        drawfx::Call c;
        c.note = label;
        c.words = {IndexOf(Words()[1]), IndexOf(Words()[2]), uint32_t(next), uint32_t(freed.size())};
        for (uint32_t f : freed) c.words.push_back(f);
        for (int i = 0; i < next; ++i) {
            const uint32_t *page = reinterpret_cast<const uint32_t *>(Page(i));
            c.words.push_back(IndexOf(page[0]));
            c.words.push_back(IndexOf(page[1]));
            c.words.push_back(page[2] ? page[2] - P(region) : 0xffffffffu);
            c.words.push_back(page[3]);
        }
        drawfx::gLog.push_back(std::move(c));
    }
};

struct PoolScope {
    PoolScope() { Pool::Open(); }
    ~PoolScope() { Pool::Close(); }
    PoolScope(const PoolScope &) = delete;
    PoolScope &operator=(const PoolScope &) = delete;
};

// ---- the world ----
struct World {
    Fake factoryFace, factoryUnknown, materials, textures, strings, com, renderGroups, displayList, fileParser, lods;
    Fake parserUnknown[kMaxParsers];
    // The factory object the static hands out: its vbptr at +0x10 (entry 2: the face at +0x20), the face's vbptr at
    // +0x24 (entry 1: the cIGZUnknown at +0x30).
    alignas(16) uint8_t factory[0x40];
    int32_t factoryVbtable[3] = {0, 0, 0x10};
    int32_t factoryFaceVbtable[2] = {-4, 0xc};
    // The parser's vbptr at +8 (entry 3: the cIMaterialParser face at +0x2fc); the face's vbptr at +0x300 (entry 1: the
    // cIGZUnknown at +0x10, entry 2: the cIBaseParser at +0x20).
    int32_t parserVbtable[4] = {-8, 0, 0, 0x2f4};
    int32_t parserFaceVbtable[3] = {-4, -0x2f0, -0x2e0};
    uint8_t *parsers[kMaxParsers] = {};
    unsigned parserCount = 0;
    bool original = false;       // the side running (the parser blocks go back to its heap)
    bool noRenderGroups = false; // the COM answers no render group manager
    uint32_t values[64];         // the scene nodes the set holds (only their addresses matter)
};
World gw;

nGZSceneGraph::cSceneGraphFactory *__cdecl StandFactory()
{
    drawfx::Note("factory static", 0);
    return reinterpret_cast<nGZSceneGraph::cSceneGraphFactory *>(gw.factory);
}
void *__cdecl StandMaterials() { return &gw.materials; }
void *__cdecl StandTextures() { return &gw.textures; }
void *__cdecl StandStrings() { return &gw.strings; }
void *StandCom()
{
    drawfx::Note("COM", 0);
    return &gw.com;
}

// The parser's constructor (thiscall, the virtual-base flag): the vbptrs and the cIGZUnknown face (a fake) laid out.
void *__fastcall StandParser(void *memory, void *, uint32_t flag)
{
    drawfx::Note("parser constructor", flag);
    if (gw.parserCount >= kMaxParsers) {
        drawfx::gErrors.emplace_back("material parser: too many");
        return memory;
    }
    const unsigned index = gw.parserCount++;
    auto *const b = static_cast<uint8_t *>(memory);
    std::memset(b, 0x5c, kParserSize);
    *reinterpret_cast<const int32_t **>(b + 8) = gw.parserVbtable;
    *reinterpret_cast<const int32_t **>(b + 0x300) = gw.parserFaceVbtable;
    gw.parserUnknown[index].Embed(b + 0x10);
    drawfx::AddRange(b, kParserSize, kParserTag | (index << 12));
    gw.parsers[index] = b;
    return memory;
}

uint32_t GetClassObject(Fake &, const uint32_t *a)
{
    Fake *f = nullptr;
    switch (a[0]) {
    case 0x4973819d: f = gw.noRenderGroups ? nullptr : &gw.renderGroups; break;
    case 0x49d02f15: f = &gw.displayList; break;
    case 0x5cf9459a: f = &gw.fileParser; break;
    case 0x2c674b2c: f = &gw.lods; break;
    default: break;
    }
    if (!f) return 0;
    ++f->refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = f;
    return 1;
}

// The stand-ins, over the image's functions (both sides reach them there) and our COM getter.
struct Stands {
    shape::StandIn factory{0xa4d660, &nGZSceneGraph::cSceneGraphFactory::Get, &StandFactory}; // ours since lane RC
    shape::StandIn materials{0xacc040, &nGZSceneGraph::cMaterialManager::Get, &StandMaterials}; // ours since lane RD
    shape::StandIn textures{0xade2e0, &nGZSceneGraph::cTextureManager::Get, &StandTextures}; // ours since lane SE
    shape::StandIn strings{0xadf8b0, &cStringPool::Instance, &StandStrings}; // ours since lane OX
    shape::CodePatch parser{0xacec90, shape::CodeOf(&StandParser)};
    shape::CodePatch ourParser{shape::MemberCode(&nGZSceneGraph::cMaterialParser::Construct), shape::CodeOf(&StandParser)};
    shape::StandIn com{0x1ae18d, &GetDirectorFrameworkObject, &StandCom};
};

void Build(uint32_t seed, drawfx::Cells &cells)
{
    std::mt19937 rng(seed * 31u + 7u);
    drawfx::gLog.reserve(8192); // no reallocation (and so no free) while the destructor's frees are recorded
    gw.factoryFace.Reset<kFactoryFace>(500);
    gw.factoryUnknown.Reset<kUnknown>(501);
    gw.materials.Reset<kMaterials>(502);
    gw.textures.Reset<kTextures>(503);
    gw.strings.Reset<kStrings>(504);
    gw.com.Reset<kCom>(505);
    gw.com.On(3, &GetClassObject);
    gw.renderGroups.Reset<kUnknown>(506);
    gw.displayList.Reset<kDisplayList>(507);
    gw.fileParser.Reset<kFileParser>(508);
    gw.lods.Reset<kLod>(509);
    for (unsigned i = 0; i < kMaxParsers; ++i) gw.parserUnknown[i].Reset<kUnknown>(510 + i);
    std::memset(gw.factory, 0xfa, sizeof(gw.factory));
    *reinterpret_cast<const int32_t **>(gw.factory + 0x10) = gw.factoryVbtable;
    *reinterpret_cast<const int32_t **>(gw.factory + 0x24) = gw.factoryFaceVbtable;
    gw.factoryFace.Embed(gw.factory + 0x20);
    gw.factoryUnknown.Embed(gw.factory + 0x30);
    drawfx::AddRange(gw.factory, sizeof(gw.factory), kFactoryTag);
    drawfx::AddRange(gw.values, sizeof(gw.values), kValueTag);
    gw.parserCount = 0;
    gw.noRenderGroups = rng() % 4 == 0;
    sgfx::gResources.Reset(cells);
    Pool::Reset(seed);
}

// The parser blocks Init allocated (the stand-in's fakes hold no references to them): back to the heap they came from.
void FreeParsers(difftest::Context &ctx)
{
    for (unsigned i = 0; i < gw.parserCount; ++i) {
        if (gw.original) ctx.Original<void(__cdecl *)(void *, size_t)>(0x824624)(gw.parsers[i], kParserSize);
        else ::operator delete(gw.parsers[i]);
        gw.parsers[i] = nullptr;
    }
    gw.parserCount = 0;
}

cISceneGraphSystemPrivate *Make(difftest::Context &ctx, bool original)
{
    cISceneGraphSystemPrivate *s = original ? ctx.Original<cISceneGraphSystemPrivate *(__cdecl *)()>(0xa4cb40)()
                                            : static_cast<cISceneGraphSystemPrivate *>(cSceneGraphSystem::Create());
    drawfx::AddRange(s, cSceneGraphSystem::kSize, kSystemTag);
    return s;
}

cIGZSystemService *Service(cISceneGraphSystemPrivate *s)
{
    return reinterpret_cast<cIGZSystemService *>(reinterpret_cast<uint8_t *>(s) + 4);
}
cIGZUnknown *Face(cISceneGraphSystemPrivate *s, unsigned face)
{
    return reinterpret_cast<cIGZUnknown *>(reinterpret_cast<uint8_t *>(s) + 4 * face);
}

// The set's tree: each node (pre-order) its value, colour, nil flag and whether its parent link is right; the head's.
struct TreeNode {
    TreeNode *left, *parent, *right;
    uint8_t color, isnil;
    uint8_t pad[2];
    uint32_t value;
};
void WalkTree(drawfx::Call &c, const TreeNode *n, const TreeNode *head, const TreeNode *parent, unsigned depth)
{
    if (n == head) {
        c.words.push_back(0xfffffff0u);
        return;
    }
    if (depth > 64) {
        c.words.push_back(0xfffffff1u);
        return;
    }
    c.words.push_back(drawfx::Norm(n->value));
    c.words.push_back(n->color);
    c.words.push_back(n->isnil);
    c.words.push_back(n->parent == parent);
    WalkTree(c, n->left, head, n, depth + 1);
    WalkTree(c, n->right, head, n, depth + 1);
}
void NoteSet(const char *label, const cISceneGraphSystemPrivate *s)
{
    const auto *head = reinterpret_cast<const TreeNode *>(uintptr_t(W(s, 8)));
    drawfx::Call c;
    c.note = label;
    c.words = {W(s, 9), head->color, head->isnil, head->left == head ? 0xfffffff0u : drawfx::Norm(head->left->value),
               head->right == head ? 0xfffffff0u : drawfx::Norm(head->right->value)};
    WalkTree(c, head->parent, head, head, 0);
    drawfx::gLog.push_back(std::move(c));
}

// The object's words but the vtables and the set's head (see the top); the running flag's byte.
void NoteSystem(const char *label, const cISceneGraphSystemPrivate *s)
{
    drawfx::Call c;
    c.note = label;
    for (unsigned k = 3; k < cSceneGraphSystem::kSize / 4; ++k) {
        if (k == 8) continue;
        const uint32_t w = W(s, k);
        c.words.push_back(k == 7 ? (w & 0xff) : drawfx::Norm(w));
    }
    drawfx::gLog.push_back(std::move(c));
    NoteSet(label, s);
}

void NoteRefs()
{
    const Fake *fakes[] = {&gw.factoryUnknown, &gw.renderGroups, &gw.displayList, &gw.fileParser, &gw.lods,
                           &gw.parserUnknown[0], &gw.parserUnknown[1], &gw.parserUnknown[2], &sgfx::gResources.manager};
    drawfx::Call c;
    c.note = "refs";
    for (const Fake *f : fakes) c.words.push_back(uint32_t(f->refs));
    drawfx::gLog.push_back(std::move(c));
}

// ---- the operations ----
void ChangeSet(cISceneGraphSystemPrivate *s, std::mt19937 &rng)
{
    const unsigned n = 1 + rng() % 24;
    const unsigned range = 1 + rng() % 64;
    for (unsigned i = 0; i < n; ++i) {
        auto *node = reinterpret_cast<cISceneNode *>(&gw.values[rng() % range]);
        if (rng() % 3) s->AddSceneNode(node);
        else s->RemoveSceneNode(node);
    }
    NoteSet("set", s);
}

void Walk(cISceneGraphSystemPrivate *s, std::mt19937 &rng)
{
    const cSceneNodeRange r = s->GetSceneNodes();
    drawfx::Note("range", {drawfx::Norm(P(r.mCurrent)), drawfx::Norm(P(r.mBegin)), drawfx::Norm(P(r.mEnd)),
                           W(r.mCurrent, 1), W(r.mBegin, 1), W(r.mEnd, 1)});
    Pool::Note("pool after GetSceneNodes");
    std::vector<cISceneNodeIterator *> clones;
    cISceneNodeIterator *at = r.mCurrent;
    for (unsigned step = 0; step < 80; ++step) {
        const bool atEnd = at->IsEqual(r.mEnd);
        drawfx::Note("at", {atEnd, at->IsEqual(r.mBegin), atEnd ? 0u : drawfx::Norm(P(at->Get()))});
        if (atEnd) break;
        if (rng() % 5 == 0) {
            cISceneNodeIterator *clone = at->Clone();
            drawfx::Note("clone", {drawfx::Norm(P(clone)), W(clone, 1), clone->IsEqual(at), drawfx::Norm(P(clone->Get()))});
            clone->AddRef();
            clones.push_back(clone);
        }
        if (rng() % 7 == 0) {
            void *out = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
            const uint32_t iid = rng() % 2 ? 1u : uint32_t(rng());
            const bool withOut = rng() % 4 != 0;
            const bool got = at->QueryInterface(iid, withOut ? &out : nullptr);
            drawfx::Note("iterator QueryInterface", {got, withOut ? drawfx::Norm(P(out)) : 0u, W(at, 1)});
            if (got) drawfx::Note("release", at->Release());
        }
        at->Next();
    }
    for (cISceneNodeIterator *c : clones) {
        if (!c->IsEqual(r.mEnd)) c->Next();
        drawfx::Note("clone moved", {c->IsEqual(r.mEnd), c->IsEqual(r.mBegin)});
        drawfx::Note("release", c->Release());
    }
    drawfx::Note("releases", {r.mCurrent->Release(), r.mBegin->Release(), r.mEnd->Release()});
    Pool::Note("pool after releases");
}

void QueryFaces(cISceneGraphSystemPrivate *s, std::mt19937 &rng)
{
    static const uint32_t kIids[] = {cISceneGraphSystem::kIid, cISceneGraphSystemPrivate::kIid, cIGZSystemService::kIID,
                                     1u, 0xe5074b6bu, 0x12345678u};
    const unsigned face = rng() % 3;
    const uint32_t iid = kIids[rng() % 6];
    const bool withOut = rng() % 5 != 0;
    void *out = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
    const bool got = Face(s, face)->QueryInterface(iid, withOut ? &out : nullptr);
    drawfx::Note("QueryInterface", {face, iid, got, withOut ? drawfx::Norm(P(out)) : 0u, W(s, 3)});
    if (got) drawfx::Note("release", Face(s, rng() % 3)->Release());
}

void Getters(cISceneGraphSystemPrivate *s)
{
    drawfx::Note("getters", {drawfx::Norm(P(s->GetFactory())), drawfx::Norm(P(s->GetMaterialManager())),
                             drawfx::Norm(P(s->GetTextureManager())), drawfx::Norm(P(s->GetRenderGroups())),
                             drawfx::Norm(P(s->GetStringPool())), drawfx::Norm(P(s->GetDisplayList())),
                             drawfx::Norm(P(s->GetFileParser())), drawfx::Norm(P(s->GetMaterialParser())),
                             drawfx::Norm(P(s->GetLODLevels()))});
    cIGZSystemService *service = Service(s);
    drawfx::Note("service", {service->GetServiceID(), uint32_t(service->GetServicePriority()),
                             uint32_t(service->IsServiceRunning()), uint32_t(service->GetServiceTickPriority())});
}

// The final release: the destructor's releases (logged) and the blocks it frees (the set's nodes, 0x14 bytes, and the
// object, 0x40), counted by size.
void Destroy(cISceneGraphSystemPrivate *s)
{
    difftest::FreeRecorder rec;
    const uint32_t left = s->Release();
    rec.Stop();
    size_t sizes[256];
    const size_t n = rec.SortedSizes(sizes, 256);
    uint32_t nodes = 0, objects = 0;
    for (size_t i = 0; i < n && i < 256; ++i) {
        if (sizes[i] == 0x14) ++nodes;
        if (sizes[i] == cSceneGraphSystem::kSize) ++objects;
    }
    drawfx::Note("destroyed", {left, nodes, objects});
    NoteRefs();
}

template <class Ops>
void Run(difftest::Context &ctx, const char *what, unsigned seeds, Ops &&ops)
{
    Stands stands;
    PoolScope pool;
    sgfx::CheckSides(
        ctx, what, seeds, [&](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
        [&](bool side, std::mt19937 &rng) {
            gw.original = side;
            cISceneGraphSystemPrivate *s = Make(ctx, side);
            NoteSystem("built", s);
            NoteRefs();
            s->AddRef();
            ops(s, rng);
            Destroy(s);
            FreeParsers(ctx);
        });
}

} // namespace

DIFF_TEST(SG_SceneGraphSystem_Set, 0xa4ca40)
{
    Run(ctx, "Set", 48, [&](cISceneGraphSystemPrivate *s, std::mt19937 &rng) {
        const unsigned steps = 1 + rng() % 10;
        for (unsigned k = 0; k < steps; ++k) {
            char step[32];
            std::snprintf(step, sizeof(step), "step %u", k);
            sgfx::MarkStep(ctx, step);
            if (rng() % 3) ChangeSet(s, rng);
            else Walk(s, rng);
        }
        Walk(s, rng);
    });
}

DIFF_TEST(SG_SceneGraphSystem_Service, 0xa4cbb0)
{
    Run(ctx, "Service", 48, [&](cISceneGraphSystemPrivate *s, std::mt19937 &rng) {
        unsigned inits = 0;
        const unsigned steps = 1 + rng() % 12;
        for (unsigned k = 0; k < steps; ++k) {
            const unsigned op = rng() % 7;
            char step[32];
            std::snprintf(step, sizeof(step), "step %u, op %u", k, op);
            sgfx::MarkStep(ctx, step);
            drawfx::Note("op", op);
            switch (op) {
            case 0:
                if (inits < kMaxParsers - 1) {
                    ++inits;
                    drawfx::Note("Init", Service(s)->Init());
                }
                break;
            case 1: drawfx::Note("Shutdown", Service(s)->Shutdown()); break;
            case 2: drawfx::Note("OnTick", Service(s)->OnTick(rng())); break;
            case 3: QueryFaces(s, rng); break;
            case 4: Getters(s); break;
            case 5: ChangeSet(s, rng); break;
            default: drawfx::Note("count", {s->AddRef(), s->Release()}); break;
            }
            NoteSystem("after", s);
            NoteRefs();
        }
    });
}
