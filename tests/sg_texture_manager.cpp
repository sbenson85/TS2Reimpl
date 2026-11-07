// Differential tests of nGZSceneGraph::cTextureManager (lanes SA and SE, milestone O row M4a; src/sg/cTextureManager.h).
//
//   SG_TextureManagerSlots  the object's life on each side in a block of the test's: the original's constructor 0xadd170
//       (with a device or not: the default white texture made through the COM getter with its update strategy, and added
//       by Add; the graphics system's device event connected), then random calls through the interface: QueryInterface
//       0xade3c0, AddRef / Release, OnEvent 0xade1f0 (the device event through 0xaddd40; the connection-changed event;
//       others), Start 0xade290, Shutdown 0xade970, FindOrLoad 0xaddd60 (found; loaded from a resource of either kind,
//       from the name's group or the game's; missing: the 128x128 texture with its missing-texture strategy), AddRefId
//       0xaddc00, ReleaseId 0xade7e0, Add 0xade4e0 (new names, with ids reused from the free list, and names already
//       there), ReloadStale 0xade2a0, SetFilterParams 0xade930, GetTexture 0xadeb10, GetInfo 0xadeb20, Has 0xade230,
//       GetFilterParams 0xadeb70, SamplerStates 0xade9e0 (each filter mode, a texture ready or not, each kind) and Range
//       0xadd700 (with NewHandleIterator 0xadd130: the game's iterators in a test pool, walked through their own
//       slots). The whole state is noted after
//       every call; at the end the destructor 0xadd480 (or, one seed in four, a block on the heap and the last Release,
//       reaching the deleting destructor 0xadd6e0), the fakes' counts, the pool's handle counts and the heap (allocated
//       minus freed, blocks and bytes).
//   SG_TextureManager_Registry  the behaviour registry 0xade400 (cTextureManagerBehaviorTraits::RegistryFor) as
//       SG_MaterialManager_Registry does the material manager's: the original side starts from the state its first-use
//       build 0xadce80 leaves (built by the test, the device event record ending in 4 as the code stores it).
//   SG_TextureManager_Get  the getter 0xade2e0, one-sided on ours (the original's function-local static cannot be built in
//       the harness): the same object twice, ours (not the image's static, our vtable), with what the original constructor
//       builds in the same world (no device).
//
// Stood in on both sides: the device getter 0x18ae40 (a device fake or none), the COM getter 0x1ae18d (a COM fake handing
// out texture fakes), and the two sampler-state appliers 0x18cc40 and 0x18e320 (noting their arguments). The framework
// (cell 0x10c9378) answers the graphics system fake and the resource service fake; the string pool is sgfx's; the
// behaviour's event manager (cell 0x10f8c50) is our connection manager. The update strategies (0xaebd30 and 0xadd030)
// and the iterators are the image's on both sides.
#include "sgfx.h"

#include "code_patch.h"
#include "free_recorder.h"
#include "gz_registry_fx.h"

#include "gz/GraphicServices.h"
#include "gz/TextureSamplerCapsDX.h"
#include "rz/StaticGetters.h"
#include "sg/cTextureManager.h"

#include <array>
#include <bit>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <malloc.h>
#include <new>
#include <random>
#include <string>
#include <typeinfo>
#include <vector>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;
using drawfx::Ret;
using drawfx::Slot;
using TM = cTextureManager;

// ---- slot tables: unsettled but for the slots named ----
struct At {
    unsigned slot;
    Slot s;
};
template <size_t N, size_t K> constexpr std::array<Slot, N> Table(const At (&at)[K])
{
    std::array<Slot, N> t{};
    for (Slot &s : t) s = {0, Ret::Int, false, "", ""};
    t[0] = {2, Ret::Int, true, "QueryInterface", "ua"};
    t[1] = {0, Ret::Int, true, "AddRef", ""};
    t[2] = {0, Ret::Int, true, "Release", ""};
    for (const At &a : at) t[a.slot] = a.s;
    return t;
}

constexpr At kGraphicsAt[] = {{8, {3, Ret::Int, true, "Connect", "uau"}}, {9, {3, Ret::Int, true, "Disconnect", "uau"}}};
constexpr auto kGraphicsSlots = Table<10>(kGraphicsAt);
constexpr drawfx::Iface kGraphics{"GraphicSystem", 10, kGraphicsSlots.data()};

constexpr At kComAt[] = {{3, {3, Ret::Int, true, "GetClassObject", "uua"}}};
constexpr auto kComSlots = Table<4>(kComAt);
constexpr drawfx::Iface kCom{"COM", 4, kComSlots.data()};

// A texture (nGZSceneGraph::cITexture as the manager calls it).
constexpr At kTextureAt[] = {{3, {0, Ret::Int, true, "Slot3", ""}},
                             {4, {0, Ret::Int, true, "Slot4", ""}},
                             {5, {0, Ret::Int, true, "Kind", ""}},
                             {6, {0, Ret::Int, true, "Ready", ""}},
                             {8, {1, Ret::Int, true, "Level", "u"}},
                             {9, {2, Ret::Int, true, "Load", "au"}},
                             {10, {7, Ret::Int, true, "Create", "auuuuuf"}}};
constexpr auto kTextureSlots = Table<11>(kTextureAt);
constexpr drawfx::Iface kTexture{"Texture", 11, kTextureSlots.data()};

// A loaded "_txtr" resource: slot 25 its kind (6 a cube map).
constexpr At kTexResourceAt[] = {{25, {0, Ret::Int, true, "Kind", ""}}};
constexpr auto kTexResourceSlots = Table<26>(kTexResourceAt);
constexpr drawfx::Iface kTexResource{"TextureResource", 26, kTexResourceSlots.data()};

// The framework's resource service: slot 3 loads a key.
constexpr At kResourceAt[] = {{3, {5, Ret::Int, true, "Load", "W4uauu"}}};
constexpr auto kResourceSlots = Table<4>(kResourceAt);
constexpr drawfx::Iface kResource{"ResourceService", 4, kResourceSlots.data()};

// ---- the world ----
constexpr unsigned kMade = 40, kGiven = 4, kResources = 6;
constexpr uint32_t kMadeId = 600, kGivenId = 650, kResourceId = 670;
constexpr uint32_t kFrameworkCell = 0x10c9378;
constexpr uint32_t kBlockTag = 0x7a000000, kStateTag = 0x7a100000;
constexpr uint32_t kConnectionChanged = 0x5b51e806;
constexpr uint32_t kResourceService = 0x056b906e, kResourceIid = 0x656b8efc;
constexpr unsigned kStateBytes = 0x18;

// Plain names are in the game's group 0x1c0532fa; the "#num!" ones in their own (FindOrLoad then tries the game's).
const char *const kNames[] = {"tex0", "tex1", "tex2", "tex3", "tex4", "tex5", "Tex0", "", "#0x1234!tex6", "##7!tex1"};
constexpr unsigned kNameCount = sizeof kNames / sizeof kNames[0];

struct World {
    Fake framework, graphics, device, com, resource, connectionListener;
    Fake made[kMade], given[kGiven], resources[kResources];
    unsigned nextMade = 0, nextResource = 0;
    uint32_t lcg = 1;
    bool deviceUp = false;
    bool heapBlock = false, listen = false;
    alignas(4) uint8_t states[2][kStateBytes];
    sgfx::Manager events;
    uint32_t Next()
    {
        lcg = lcg * 1664525u + 1013904223u;
        return lcg >> 8;
    }
};
World gw;
alignas(16) uint8_t gBlock[TM::kSize];

uint32_t GetService(Fake &, const uint32_t *a)
{
    Fake *service = nullptr;
    if (a[0] == TM::kGraphicSystemId && a[1] == TM::kGraphicSystemIid) service = &gw.graphics;
    else if (a[0] == kResourceService && a[1] == kResourceIid) service = &gw.resource;
    if (!service) return 0;
    ++service->refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = service;
    return 1;
}
// The textures the director's COM makes, in turn, counted for the caller.
uint32_t GetClassObject(Fake &, const uint32_t *a)
{
    Fake *const f = &gw.made[gw.nextMade++ % kMade];
    ++f->refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = f;
    return 1;
}
// The resource service finds a key's resource two times in three (counted for the caller).
uint32_t ResourceLoad(Fake &, const uint32_t *a)
{
    const uint32_t *const key = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
    drawfx::Note("resource key", {key[0], key[1], key[2], key[3]});
    if (gw.Next() % 3 == 0) return 0;
    Fake *const r = &gw.resources[gw.nextResource++ % kResources];
    ++r->refs;
    *reinterpret_cast<Fake **>(uintptr_t(a[2])) = r;
    return 1;
}
uint32_t Word0(Fake &self, const uint32_t *) { return self.value[0]; }
uint32_t Word1(Fake &self, const uint32_t *) { return self.value[1]; }
uint32_t Word2(Fake &self, const uint32_t *) { return self.value[2]; }

void *__cdecl StandDevice() { return gw.deviceUp ? &gw.device : nullptr; }
void *StandCom() { return &gw.com; }
void __cdecl StandApplyA(void *a, void *b, uint32_t level) { drawfx::Note("apply 4/7", {drawfx::Ptr(a), drawfx::Ptr(b), level}); }
void __cdecl StandApplyB(void *a, void *b, uint32_t level) { drawfx::Note("apply 6", {drawfx::Ptr(a), drawfx::Ptr(b), level}); }

struct Stands {
    shape::StandIn device{0x18ae40, &nGZGraphic4::GetCurrentDeviceCached, &StandDevice};
    shape::StandIn com{0x1ae18d, &GetDirectorFrameworkObject, &StandCom};
    shape::StandIn applyA{0x18cc40, &nGZGraphic4::FitTextureSamplerStates, &StandApplyA};
    shape::StandIn applyB{0x18e320, &nGZGraphic4::FitCubeTextureSamplerStates, &StandApplyB};
};

// A texture's answers: its kind (slot 5), ready (slot 6), its level (slot 8).
void MakeTexture(Fake &t, uint32_t id, uint32_t seed)
{
    static constexpr uint32_t kKinds[] = {0, 4, 6, 7, 1};
    t.Reset<kTexture>(id);
    t.On(5, &Word0);
    t.On(6, &Word1);
    t.On(8, &Word2);
    t.value[0] = kKinds[(seed + id) % 5];
    t.value[1] = (seed + id) % 4 != 0;
    t.value[2] = 0x900 + id;
}

void Build(uint32_t seed, drawfx::Cells &cells)
{
    gw.framework.Reset<sgfx::kFramework>(460);
    gw.framework.On(5, &GetService);
    gw.graphics.Reset<kGraphics>(520);
    gw.device.Reset<sgfx::kUnknown>(521);
    gw.com.Reset<kCom>(522);
    gw.com.On(3, &GetClassObject);
    gw.resource.Reset<kResource>(523);
    gw.resource.On(3, &ResourceLoad);
    sgfx::MakeListener(gw.connectionListener, 524);
    for (unsigned i = 0; i < kMade; ++i) MakeTexture(gw.made[i], kMadeId + i, seed);
    for (unsigned i = 0; i < kGiven; ++i) MakeTexture(gw.given[i], kGivenId + i, seed);
    for (unsigned i = 0; i < kResources; ++i) {
        Fake &r = gw.resources[i];
        r.Reset<kTexResource>(kResourceId + i);
        r.On(25, &Word0);
        r.value[0] = (seed + i) % 3 == 0 ? 6 : (seed + i) % 3;
    }
    gw.nextMade = gw.nextResource = 0;
    gw.lcg = seed * 2654435761u + 7;
    gw.deviceUp = seed % 3 != 0;
    gw.heapBlock = seed % 4 == 3;
    gw.listen = seed % 3 == 1;
    cells.Set(kFrameworkCell, &gw.framework);
    sgfx::gPool.Reset(cells);
    gw.events.Use(cells, cTextureManagerBehaviorTraits::kEventManagerCell);
}

// The handle iterators' pool (cTextureHandleIterator's at 0x10f8bd4, its current page at +4) points at a test page:
// 0x800-aligned, header +8 the free list, +0xc the used count, set far from empty and from full so neither side reaches
// the page bookkeeping (game code not under test).
struct RangePool {
    static constexpr uint32_t kPoolPage = 0x10f8bd8, kBlock = 0x10, kPage = 0x800, kTag = 0x7b500000;
    uint8_t *page;
    uint32_t saved;
    RangePool()
    {
        page = static_cast<uint8_t *>(_aligned_malloc(kPage, kPage));
        saved = *GameAddress<uint32_t>(kPoolPage);
        *GameAddress<uint32_t>(kPoolPage) = P(page);
    }
    ~RangePool()
    {
        *GameAddress<uint32_t>(kPoolPage) = saved;
        _aligned_free(page);
    }
    RangePool(const RangePool &) = delete;
    RangePool &operator=(const RangePool &) = delete;
    void Reset()
    {
        std::memset(page, 0, kPage);
        uint32_t *header = reinterpret_cast<uint32_t *>(page);
        header[3] = 1000;
        uint32_t next = 0;
        for (uint32_t at = kPage - kBlock; at >= 0x10; at -= kBlock) {
            *reinterpret_cast<uint32_t *>(page + at) = next;
            next = P(page + at);
        }
        header[2] = next;
        drawfx::AddRange(page, kPage, kTag);
    }
    void Note() const
    {
        const uint32_t *header = reinterpret_cast<const uint32_t *>(page);
        drawfx::Note("pool", {drawfx::Ptr(header[2]), header[3]});
    }
};
RangePool *gRangePool = nullptr;
difftest::AllocationCounter *gCounter = nullptr; // the life's heap counter, noted with the state

// ---- noting ----
void NoteState(const TM *v)
{
    difftest::NotCounted bookkeeping;
    if (gCounter) drawfx::Note("heap so far", {uint32_t(gCounter->Blocks()), uint32_t(gCounter->Bytes()), uint32_t(gCounter->Allocations()), uint32_t(gCounter->Frees())});
    const uint32_t *w = reinterpret_cast<const uint32_t *>(v);
    drawfx::Note("refs, filter, started",
                 {w[1], v->mFilter.a, v->mFilter.mode, std::bit_cast<uint32_t>(v->mFilter.bias), uint32_t(v->mStarted)});
    for (const auto &[key, r] : v->mRecords) {
        drawfx::Note("record", {key.mValue, r.name.mValue, drawfx::Ptr(r.texture), uint32_t(r.references), r.id, r.flags});
    }
    drawfx::Call ids;
    ids.note = "by id";
    for (const TM::Record *r : v->mById) {
        uint32_t word = 0xffffffffu;
        if (r) {
            word = 0xbbbbbbbbu;
            for (const auto &[key, record] : v->mRecords)
                if (&record == r) word = key.mValue;
        }
        ids.words.push_back(word);
    }
    ids.words.push_back(0xcccc);
    ids.words.insert(ids.words.end(), v->mFreeIds.begin(), v->mFreeIds.end());
    drawfx::gLog.push_back(std::move(ids));
}

void NoteFakes()
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = "fake refs";
    for (Fake *f : {&gw.graphics, &gw.device, &gw.com, &gw.resource, &gw.connectionListener}) c.words.push_back(uint32_t(f->refs));
    for (const Fake &f : gw.made) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.given) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.resources) c.words.push_back(uint32_t(f.refs));
    drawfx::gLog.push_back(std::move(c));
    sgfx::gPool.NoteCounts();
}

// A live id, or -1 one time in six (or when there is none).
uint32_t PickId(const TM *v, std::mt19937 &rng)
{
    std::vector<uint32_t> live;
    for (size_t i = 0; i < v->mById.size(); ++i)
        if (v->mById[i]) live.push_back(uint32_t(i));
    if (live.empty() || rng() % 6 == 0) return cTextureHandle::kNone;
    return live[rng() % live.size()];
}

// The handle iterator (cTextureHandleIterator 0xe7c7f4): Clone, IsEqual, Get (the id), Next.
class cIHandleIterator : public cIGZUnknown {
public:
    virtual cIHandleIterator *Clone() = 0;                  // 3
    virtual bool IsEqual(const cIHandleIterator *other) = 0; // 4
    virtual uint32_t Get() = 0;                             // 5: the id
    virtual void Next() = 0;                                // 6
    virtual ~cIHandleIterator() = default;                  // 7
};

uint32_t WordOf(const cIGZUnknown *it, unsigned k) { return reinterpret_cast<const uint32_t *>(it)[k]; }

void NoteIterator(const char *label, const cIGZUnknown *it)
{
    if (!it) {
        drawfx::Note(label, 0u);
        return;
    }
    drawfx::Note(label, {WordOf(it, 1), WordOf(it, 2), drawfx::Ptr(WordOf(it, 3))});
}

void NoteRange(const TextureRange &r)
{
    drawfx::Note("range", {drawfx::Ptr(r.cursor), drawfx::Ptr(r.begin), drawfx::Ptr(r.end)});
    NoteIterator("cursor", r.cursor);
    NoteIterator("begin", r.begin);
    NoteIterator("end", r.end);
    auto *begin = reinterpret_cast<cIHandleIterator *>(r.begin);
    auto *end = reinterpret_cast<cIHandleIterator *>(r.end);
    if (begin && end) {
        cIHandleIterator *it = begin->Clone();
        NoteIterator("clone", it);
        if (it) {
            drawfx::Note("clone addref", it->AddRef());
            drawfx::Note("begin is end", uint32_t(begin->IsEqual(end)));
            for (int k = 0; k < 24 && !it->IsEqual(end); ++k) {
                drawfx::Note("item", it->Get());
                it->Next();
            }
            drawfx::Note("clone released", it->Release());
        }
    }
    for (cIGZUnknown *it : {r.cursor, r.begin, r.end})
        if (it) drawfx::Note("iterator released", it->Release());
    gRangePool->Note();
}

void NoteStates()
{
    for (unsigned s = 0; s < 2; ++s) {
        drawfx::Call c;
        c.note = "sampler state";
        for (unsigned k = 0; k < kStateBytes; k += 4) {
            uint32_t word;
            std::memcpy(&word, gw.states[s] + k, 4);
            c.words.push_back(word);
        }
        drawfx::gLog.push_back(std::move(c));
    }
}

Fake *PickTexture(std::mt19937 &rng)
{
    if (rng() % 3 == 0) return &gw.given[rng() % kGiven];
    return &gw.made[rng() % kMade];
}

void Step(cITextureManager *m, TM *v, std::mt19937 &rng)
{
    const unsigned op = rng() % 20;
    drawfx::Note("op", op);
    switch (op) {
    case 0:
        if (rng() % 3 == 0) m->Start();
        else m->Shutdown();
        break;
    case 1:
        m->Start();
        break;
    case 2:
    case 3:
        drawfx::Note("find or load", m->FindOrLoad(kNames[rng() % kNameCount]));
        break;
    case 4:
        if (rng() % 2) {
            m->AddRefId(PickId(v, rng));
            break;
        }
        // Every record not counted yet, counted and released: those with flag 1 dropped together, so several ids are
        // free when the next new names are added (the order they are reused in).
        for (size_t i = 0; i < v->mById.size(); ++i)
            if (v->mById[i] && v->mById[i]->references == 0) {
                m->AddRefId(uint32_t(i));
                m->ReleaseId(uint32_t(i));
            }
        break;
    case 5:
    case 6:
        m->ReleaseId(PickId(v, rng));
        break;
    case 7:
    case 8: {
        static constexpr uint32_t kFlags[] = {0, 1, 2, 4, 5};
        Fake *const texture = PickTexture(rng);
        drawfx::Note("add", m->Add(reinterpret_cast<cIGZUnknown *>(texture), kNames[rng() % kNameCount], kFlags[rng() % 5]));
        break;
    }
    case 9:
        m->ReloadStale();
        break;
    case 10: {
        static const float kBias[] = {-0.2f, 0.0f, -0.0f, 0.5f, 1.5f};
        const TextureFilterParams &p = v->mFilter;
        const uint32_t a = rng() % 3 ? p.a : rng() % 5;
        const uint32_t mode = rng() % 3 ? p.mode : rng() % 5;
        const float bias = rng() % 3 ? p.bias : kBias[rng() % 5];
        m->SetFilterParams(a, mode, bias);
        break;
    }
    case 11: {
        std::vector<uint32_t> live;
        for (size_t i = 0; i < v->mById.size(); ++i)
            if (v->mById[i]) live.push_back(uint32_t(i));
        if (live.empty()) break;
        drawfx::Note("texture", drawfx::Ptr(m->GetTexture(live[rng() % live.size()])));
        break;
    }
    case 12: {
        const TextureInfo info = m->GetInfo(PickId(v, rng));
        sgfx::NoteText("info name", info.name ? reinterpret_cast<const char *>(uintptr_t(info.name)) : nullptr);
        drawfx::Note("info references", uint32_t(info.references));
        break;
    }
    case 13:
        drawfx::Note("has", uint32_t(m->Has(kNames[rng() % kNameCount])));
        break;
    case 14: {
        const TextureFilterParams p = m->GetFilterParams();
        drawfx::Note("filter", {p.a, p.mode, std::bit_cast<uint32_t>(p.bias)});
        break;
    }
    case 15: {
        for (auto &state : gw.states)
            for (uint8_t &b : state) b = uint8_t(rng());
        if (rng() % 2) v->mFilter.mode = rng() % 5;
        m->SamplerStates(gw.states[0], gw.states[1], reinterpret_cast<cIGZUnknown *>(PickTexture(rng)));
        NoteStates();
        break;
    }
    case 16:
        NoteRange(m->Range());
        break;
    case 17: {
        uint32_t data[2] = {rng(), rng() % 3};
        const uint32_t type = rng() % 3 == 0 ? (rng() % 2 ? kConnectionChanged : rng()) : TM::kDeviceEvent;
        m->OnEvent(type, rng() % 2, data, 0);
        break;
    }
    case 18: {
        static constexpr uint32_t kIids[] = {cITextureManager::kIid, 1, TM::kIidBehavior2, TM::kIidBehaviorType,
                                             0xdeadbeef, kConnectionChanged};
        const uint32_t iid = kIids[rng() % 6];
        void *out = nullptr;
        const uint32_t before = reinterpret_cast<const uint32_t *>(v)[1];
        const bool got = m->QueryInterface(iid, rng() % 5 ? &out : nullptr);
        const uint32_t after = reinterpret_cast<const uint32_t *>(v)[1];
        drawfx::Note("query", {iid, uint32_t(got), drawfx::Ptr(out), after - before});
        if (after != before) m->Release();
        break;
    }
    default: {
        const uint32_t a = m->AddRef();
        const uint32_t b = m->Release();
        drawfx::Note("addref, release", {a, b});
        break;
    }
    }
    NoteState(v);
}

void Life(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    difftest::AllocationCounter counter;
    gCounter = &counter;
    void *block = gw.heapBlock ? ::operator new(TM::kSize) : gBlock;
    drawfx::AddRange(block, TM::kSize, kBlockTag);
    drawfx::AddRange(gw.states, sizeof gw.states, kStateTag);
    std::memset(block, 0xcd, TM::kSize);
    if (original)
        ctx.Original<void *(__thiscall *)(void *)>(0xadd170)(block);
    else
        ::new (block) TM();
    TM *v = reinterpret_cast<TM *>(block);
    cITextureManager *m = static_cast<cITextureManager *>(v);
    NoteState(v);
    if (gw.listen) gw.events.Listen(block, TM::kDeviceEvent, gw.connectionListener, 0x1234);
    if (rng() % 4 != 0) m->Start();
    const unsigned steps = 6 + rng() % 30;
    for (unsigned s = 0; s < steps; ++s) Step(m, v, rng);
    sgfx::MarkStep(ctx, "destroying");
    if (gw.heapBlock) {
        drawfx::Note("last release", m->Release());
    } else if (original) {
        ctx.Original<void(__thiscall *)(void *)>(0xadd480)(block);
    } else {
        v->~TM();
    }
    NoteFakes();
    gCounter = nullptr;
    counter.Stop();
    drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
    gw.events.Drop();
}

} // namespace

// Verifies: the constructor 0xadd170, destructor 0xadd480, deleting destructor 0xadd6e0, QueryInterface 0xade3c0, OnEvent
// 0xade1f0 with 0xaddd40, Start 0xade290, Shutdown 0xade970, FindOrLoad 0xaddd60, AddRefId 0xaddc00, ReleaseId 0xade7e0,
// Add 0xade4e0, ReloadStale 0xade2a0, SetFilterParams 0xade930, GetTexture 0xadeb10, GetInfo 0xadeb20, Has 0xade230,
// GetFilterParams 0xadeb70, SamplerStates 0xade9e0, Range 0xadd700.
DIFF_TEST(SG_TextureManagerSlots, 0xade7e0)
{
    Stands stands;
    RangePool pool;
    gRangePool = &pool;
    sgfx::CheckSides(
        ctx, "texture manager", 500,
        [&](uint32_t seed, drawfx::Cells &cells) {
            Build(seed, cells);
            pool.Reset();
        },
        [&](bool original, std::mt19937 &rng) {
            Life(ctx, original, rng);
            ++ctx.cases;
        });
    gRangePool = nullptr;
}

// ---- the registry ----
namespace {

using nGZEvent::cEventAttributeMap;

constexpr uint32_t kBaseMap = 0x10c94f4, kBaseMapGuard = 0x10c94fc, kBaseOnce = 0x10c9510, kBaseHolderGuard = 0x10c9518;
constexpr uint32_t kMap = 0x10f8c44, kMapGuard = 0x10f8c4c, kOnce = 0x10f8c54, kHolderGuard = 0x10f8c5c;
constexpr uint32_t kRecord = 0x10700a0; // the device event's record in the image: id, name, three words (and 8)
constexpr uint32_t kBaseEventA = 0x5b51e806, kBaseEventB = 0xe9dd42cf;
char gNames[8][12];

struct Saved {
    uint32_t rva;
    unsigned size;
    uint8_t bytes[8];
};

class RegistryScope {
public:
    RegistryScope()
    {
        for (const auto &[rva, size] : {std::pair{kBaseMap, 8u}, {kBaseMapGuard, 4u}, {kBaseOnce, 1u}, {kBaseHolderGuard, 4u},
                                        {kMap, 8u}, {kMapGuard, 4u}, {kOnce, 1u}, {kHolderGuard, 4u}}) {
            Saved s{rva, size, {}};
            std::memcpy(s.bytes, GameAddress<void>(rva), size);
            saved_.push_back(s);
        }
        base_ = ::new (GameAddress<void>(kBaseMap)) nGZEvent::cEventAttributeMap;
        *GameAddress<int32_t>(kBaseMapGuard) = INT_MIN;
        *GameAddress<uint8_t>(kBaseOnce) = 1;
        *GameAddress<int32_t>(kBaseHolderGuard) = INT_MIN;
        for (unsigned i = 0; i < 3; ++i) {
            std::snprintf(gNames[i], sizeof gNames[i], "base%u", i);
            (*base_)[0x10 + i] = {0x10 + i, gNames[i], i, 0x100u + i, 0x200u + i, 0x300u + i};
        }
    }
    // The original side: the state its first-use build leaves (the base events stored, the base copied, the device
    // event's record with 4 in its last word), the guards and the holder's flag marked done.
    void PlantOriginal(difftest::Context &ctx)
    {
        ctx.Original<void(__thiscall *)(void *)>(0x1464f0)(nullptr);
        mine_ = ::new (GameAddress<void>(kMap)) nGZEvent::cEventAttributeMap(*base_);
        const uint32_t *record = GameAddress<uint32_t>(kRecord);
        (*mine_)[record[0]] = {record[0], reinterpret_cast<const char *>(uintptr_t(record[1])), record[2], record[3],
                               record[4], 4};
        *GameAddress<uint8_t>(kOnce) = 1;
        *GameAddress<int32_t>(kHolderGuard) = INT_MIN;
        *GameAddress<int32_t>(kMapGuard) = INT_MIN;
    }
    ~RegistryScope()
    {
        if (mine_) mine_->~cEventAttributeMap();
        base_->~cEventAttributeMap();
        for (auto it = saved_.rbegin(); it != saved_.rend(); ++it) std::memcpy(GameAddress<void>(it->rva), it->bytes, it->size);
    }
    RegistryScope(const RegistryScope &) = delete;
    RegistryScope &operator=(const RegistryScope &) = delete;

private:
    nGZEvent::cEventAttributeMap *base_ = nullptr, *mine_ = nullptr;
    std::vector<Saved> saved_;
};

std::string Snapshot(const nGZEvent::cEventAttributeMap *map)
{
    std::string out;
    char line[160];
    for (const auto &[key, a] : *map) {
        std::snprintf(line, sizeof line, "%08x:%08x:%s:%u:%u:%u:%u;", key, a.mId, a.mName ? a.mName : "<null>", a.mWord08,
                      a.mWord0C, a.mWord10, a.mWord14);
        out += line;
    }
    return out;
}

using RegistryFn = nGZEvent::cEventAttributeMap *(__cdecl *)(uint32_t, const nGZEvent::cEventAttribute *);

std::vector<std::string> RunRegistry(difftest::Context &ctx, uint32_t seed, bool ours, bool &stable)
{
    std::mt19937 rng(seed * 1103515245u + 12345u);
    RegistryScope scope;
    shape::StandIn baseStand{0x1485f0, &nGZEvent::cAbstractBehaviorType::RegistryFor, &pdfx::StandRegistry<kBaseMap>};
    if (ours)
        cTextureManagerBehaviorTraits::ResetRegistryForTest();
    else
        scope.PlantOriginal(ctx);
    const uint32_t ids[] = {0, 0, 0x10, 0x11, 0x12, kBaseEventA, kBaseEventB, TM::kDeviceEvent, 0x77, 0x1000, 0xfffffff0u,
                            rng(), rng()};
    for (unsigned i = 0; i < 8; ++i) std::snprintf(gNames[i], sizeof gNames[i], "n%u_%u", seed % 100, i);
    std::vector<std::string> out;
    nGZEvent::cEventAttributeMap *first = nullptr;
    stable = true;
    const unsigned calls = 1 + rng() % 6;
    for (unsigned c = 0; c < calls; ++c) {
        const uint32_t id = ids[rng() % (sizeof ids / sizeof ids[0])];
        nGZEvent::cEventAttribute attribute{rng() % 5 == 0 ? id : rng(), gNames[rng() % 8], rng() % 4, rng(), rng() % 3,
                                            rng() % 16};
        nGZEvent::cEventAttributeMap *map = ours ? cTextureManagerBehaviorTraits::RegistryFor(id, &attribute)
                                                 : ctx.Original<RegistryFn>(0xade400)(id, &attribute);
        if (!first) first = map;
        stable = stable && map == first;
        out.push_back(Snapshot(map));
    }
    if (ours) {
        out.push_back(Snapshot(cTextureManagerBehaviorTraits::Registry()));
    } else {
        nGZEvent::cEventAttribute defaults{0, "", 0, 0, 0, 0};
        out.push_back(Snapshot(ctx.Original<RegistryFn>(0xade400)(0, &defaults)));
    }
    if (ours) cTextureManagerBehaviorTraits::ResetRegistryForTest(); // ours copied the planted base: not kept
    return out;
}

} // namespace

// Verifies the registry 0xade400 with its holder's first-use build 0xadce80 (whose result the original side is planted
// with: the harness can't run the static's guard).
DIFF_TEST(SG_TextureManager_Registry, 0xade400)
{
    for (uint32_t seed = 1; seed <= 600; ++seed) {
        bool stableOriginal = false, stableOurs = false;
        const std::vector<std::string> original = RunRegistry(ctx, seed, false, stableOriginal);
        const std::vector<std::string> ours = RunRegistry(ctx, seed, true, stableOurs);
        ++ctx.cases;
        if (!stableOriginal || !stableOurs || original.size() != ours.size()) {
            ctx.Fail("seed %u: the returned map moved (original %d, ours %d) or the call counts differ (%zu / %zu)", seed,
                     stableOriginal, stableOurs, original.size(), ours.size());
            return;
        }
        for (size_t i = 0; i < original.size(); ++i)
            if (original[i] != ours[i]) {
                ctx.Fail("seed %u call %zu:\n  original %s\n  ours     %s", seed, i, original[i].c_str(), ours[i].c_str());
                return;
            }
    }
}

// The getter 0xade2e0 (as SG_MaterialManager_Get). The original with its guard 0x10f8c40 set to "initialised" hands out
// its static 0x10f8c08 every time; ours is compared with a manager the original constructor 0xadd170 builds in the same
// world (no device: the graphics system's device event connected, nothing else): ours hands out one object every time,
// not the image's, with our vtable, and the same contents and the same calls while building (when this runner builds it
// here; the object outlives the test, a function-local static of ours).
namespace {

void GetWorld(drawfx::Cells &cells)
{
    drawfx::ResetAll();
    gw.framework.Reset<sgfx::kFramework>(460);
    gw.framework.On(5, &GetService);
    gw.graphics.Reset<kGraphics>(520);
    gw.deviceUp = false;
    cells.Set(kFrameworkCell, &gw.framework);
    sgfx::gPool.Reset(cells);
}

void NoteFresh(const TM *v)
{
    NoteState(v);
    difftest::NotCounted bookkeeping;
    drawfx::Note("fresh", {uint32_t(v->mRecords.size()), uint32_t(v->mById.capacity()), uint32_t(v->mFreeIds.capacity())});
}

// The calls while building log the object's own address as an unregistered pointer's record: it reads "self" on both
// sides.
std::vector<std::string> TakeLog(const void *self)
{
    uint32_t record = 0;
    if (!normwords::Pointer(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self)), drawfx::gRanges, record)) record = 0;
    char hex[16];
    std::snprintf(hex, sizeof hex, "0x%08x", record);
    std::vector<std::string> out;
    for (const drawfx::Call &c : drawfx::gLog) {
        std::string line = drawfx::Describe(c);
        for (size_t at; (at = line.find(hex)) != std::string::npos;) line.replace(at, std::strlen(hex), "self");
        out.push_back(line);
    }
    drawfx::gLog.clear();
    return out;
}

} // namespace

DIFF_TEST(SG_TextureManager_Get, 0xade2e0)
{
    constexpr uint32_t kStatic = 0x10f8c08, kGuard = 0x10f8c40, kImageVTable = 0xe7c778;
    static bool built = false;
    Stands stands;

    int32_t *const guard = ctx.image.At<int32_t>(kGuard);
    const int32_t savedGuard = *guard;
    *guard = INT_MIN;
    const auto getter = ctx.Original<void *(__cdecl *)()>(0xade2e0);
    void *const a1 = getter(), *const a2 = getter();
    *guard = savedGuard;
    ++ctx.cases;
    if (a1 != a2 || a1 != static_cast<void *>(ctx.image.At<uint8_t>(kStatic))) ctx.Fail("original getter: %p %p", a1, a2);

    std::vector<std::string> original, ours;
    {
        drawfx::Cells cells;
        GetWorld(cells);
        std::memset(gBlock, 0xcd, TM::kSize);
        ctx.Original<void *(__thiscall *)(void *)>(0xadd170)(gBlock);
        if (built) drawfx::gLog.clear(); // ours is built already: the contents only
        NoteFresh(reinterpret_cast<TM *>(gBlock));
        original = TakeLog(gBlock);
        ctx.Original<void(__thiscall *)(void *)>(0xadd480)(gBlock);
    }
    {
        drawfx::Cells cells;
        GetWorld(cells);
        TM *const a = TM::Get();
        TM *const b = TM::Get();
        ++ctx.cases;
        if (!a || a != b) ctx.Fail("Get answered %p then %p", static_cast<void *>(a), static_cast<void *>(b));
        else if (static_cast<void *>(a) == static_cast<void *>(ctx.image.At<uint8_t>(kStatic))) ctx.Fail("Get: the image's static");
        else if (typeid(*a) != typeid(TM) || *reinterpret_cast<void *const *>(a) == static_cast<void *>(ctx.image.At<uint8_t>(kImageVTable)))
            ctx.Fail("Get's object does not have our vtable");
        else {
            NoteFresh(a);
            ours = TakeLog(a);
            if (gw.graphics.refs != 1000) ctx.Fail("Get left the graphics system counted (%d)", gw.graphics.refs);
        }
        built = true;
    }
    ++ctx.cases;
    if (ours.size() != original.size()) ctx.Fail("Get: %zu log lines, the original constructor's %zu", ours.size(), original.size());
    for (size_t i = 0; i < ours.size() && i < original.size(); ++i)
        if (ours[i] != original[i]) {
            ctx.Fail("Get: line %zu: original [%s], ours [%s]", i, original[i].c_str(), ours[i].c_str());
            break;
        }
    drawfx::ResetAll();
}
