// Differential tests for cDisplayList's view slots and their helpers (src/sg/cDisplayListDraw.cpp) and the views'
// frame cache (src/sg/cFrameCache.cpp): the draw item builder 0xaeae00, AddGeometry (slot 29 0xa9f460), the pass reset
// 0xaa2540, the strategy call 0xaa24c0, the frame cache's Flip 0xaa2e10, destructor 0xa9e3f0 and deleting destructor
// 0xa9e5b0, DrawPass (slot 24 0xaa0920) and DrawViewer (slot 23 0xa9fb40).
//
// The world (tests/sg_display_list_world.h) plus: a viewer on a hand-made vtable (its slots reach 122, past the svcfx
// fakes' 80) that logs every call; nodes on another (slot 83 the draw, slot 96 the light records' SetWord78); the frame
// cache a real cFrameCache layout with the game's vtable (the objects are the game's) whose holder (a fake) answers the
// query; the material manager, shaders, geometries, the format object and the strategies as svcfx fakes. The item log
// (0xaa1640, ours: tests/sg_display_list_log.cpp) runs for real: the log fake records slots 7, 8, 9, 12 and 14 (texts
// by their characters), the material manager is also GetCachedQueriedObject's (cell 0x10f8fcc; GetInfo 33 a name),
// a formats manager in cell 0x10caa24 answers the format object (Describe 16 a name), the viewer and the nodes answer
// slot 88. Items logged have a record with a node: the seeded items a node's record, built items their pass's viewer
// record (a pass off the stack, which the game never adds to, is given a node before a logged DrawPass).
//
// Compared: results (DrawViewer's whole eax; the builder's al); the object, its passes and the stack (Capture); the
// draw items (pointers into the passes as offsets); the light records; the frame cache's words, blobs and held lists;
// the fakes' reference counts; every call in order with its arguments.
//
// Words the original leaves as its stack held, masked: a built item's word 0 (the builder never writes it; AddGeometry
// and DrawPass build on the stack, ours on a zeroed item), and a new frame cache blob's words 1-4 (0 and 5-7 are zeroed).
#include "free_recorder.h"
#include "sg_display_list_world.h"

#include "gz/cIGZString.h"
#include "sg/cFrameCache.h"

#include <limits>
#include <type_traits>

#include <windows.h>

using nGZSceneGraph::cFrameCache;
using nGZSceneGraph::LightRecord;

namespace {

// ---- the fakes ----------------------------------------------------------------------------------------------------------
constexpr unsigned kFormats = 4, kState = 10, kLog = 11, kHolder = 12, kStrategy = 13, kViewStrategy = 14, kMaterials = 15,
                   kShader0 = 16, kGeometry0 = 19, kFormat = 22, kTexture = 23;
constexpr unsigned kShaders = 3, kGeometries = 3;
constexpr uint32_t evViewer = 0x7600, evNode = 0x6e00, evNote = 0x5100;
constexpr uint32_t kRaised = 0xe0dd1234; // the strategy's exception

// What the test keeps for a world's draw: the shader infos, the geometries' draw infos, the strategy's plan.
struct DrawInfo {
    uint32_t words[2];
    int32_t layer;
    void *format;
    uint8_t between[0x24];
    uint8_t flag; // +0x34
    uint8_t pad[3];
};
static_assert(offsetof(DrawInfo, flag) == 0x34, "the builder's reads");
uint32_t gShaderInfo[kShaders][5]; // nGZSceneGraph::ShaderInfo: pass, sort key, count, flags, byte
DrawInfo gDrawInfo[kGeometries];
uint32_t gStrategyCalls;  // calls so far (the strategy's stats and its exception are drawn from it)
uint32_t gRaiseAt;        // the strategy call that raises (0: none)
uint32_t gStatsSeed;

uint32_t NormPassPointer(uint32_t p)
{
    const std::vector<Pass> &passes = Self()->mPasses;
    const uint32_t base = svcfx::P(passes.data());
    if (!passes.empty() && p >= base && p < base + passes.size() * sizeof(Pass)) return 0x0ff00000u | (p - base);
    return svcfx::Norm(p);
}

// The material manager's GetVariant (22): the index, the key (the item's state words), the rest; a shader by index.
uint32_t GetVariant(svcfx::Obj *, uint32_t *args)
{
    const uint32_t *key = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(args[1]));
    for (unsigned i = 0; i < 3; ++i) svcfx::Ev(key[i]);
    return svcfx::P(&svcfx::F(kShader0 + args[0] % kShaders));
}

// The strategies' slots 3 (BeginPass) and 4 (Render): the pass, the items (as an offset into the list) and the count;
// Render adds to the pass's draw stats. The planned call raises.
void StrategyCall(uint32_t pass)
{
    Pass *const p = reinterpret_cast<Pass *>(static_cast<uintptr_t>(pass));
    svcfx::Ev(NormPassPointer(pass));
    ++gStrategyCalls;
    if (p && Self()->mPasses.data() <= p && p < Self()->mPasses.data() + Self()->mPasses.size()) {
        p->mDrawStats[0] += gStatsSeed * gStrategyCalls;
        p->mDrawStats[1] += gStatsSeed ^ gStrategyCalls;
        p->mDrawStats[2] += gStrategyCalls * 7;
    }
    if (gStrategyCalls == gRaiseAt) RaiseException(kRaised, 0, 0, nullptr);
}
uint32_t StrategyBegin(svcfx::Obj *, uint32_t *args)
{
    StrategyCall(args[0]);
    return 0;
}
uint32_t StrategyRender(svcfx::Obj *, uint32_t *args)
{
    svcfx::Ev(args[1] - svcfx::P(Self()->mItems.data()));
    svcfx::Ev(args[2]);
    StrategyCall(args[0]);
    return 0;
}

// The holder's query (slot 0): the frame cache, counted.
cFrameCache gCache;
uint32_t HolderQuery(svcfx::Obj *, uint32_t *args)
{
    svcfx::Ev(args[0]);
    svcfx::Ev(svcfx::Norm(args[1]));
    *reinterpret_cast<void **>(args[1]) = &gCache;
    ++gCache.mRefCount;
    return 1;
}

uint32_t LogText(svcfx::Obj *, uint32_t *args)
{
    for (const char *p = reinterpret_cast<const char *>(static_cast<uintptr_t>(args[0])); *p; ++p)
        svcfx::Ev(static_cast<uint8_t>(*p));
    svcfx::Ev(0x7e7e);
    return 1;
}

// The log's SetUint16 (7): the upper half of the argument is the caller's stack.
uint32_t LogUint16(svcfx::Obj *, uint32_t *args)
{
    svcfx::Ev(args[0] & 0xffff);
    return 1;
}

// The material manager's GetInfo (33, by hidden pointer): the info's first word the name text (a name per index).
uint32_t MaterialInfo(svcfx::Obj *, uint32_t *args)
{
    static const char *const kNames[] = {"", "floorTile_0042", "a_material_name_longer_than_sixteen", "%d"};
    auto *const info = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(args[0]));
    info[0] = svcfx::P(kNames[args[1] % 4]);
    info[1] = info[2] = info[3] = info[4] = 0;
    return args[0];
}

// The format's Describe (16): a name into the string.
uint32_t FormatDescribe(svcfx::Obj *, uint32_t *args)
{
    reinterpret_cast<cIGZString *>(static_cast<uintptr_t>(args[0]))->FromChar("id:5{pos norm tc}");
    return 0;
}

// The svcfx side of a world: everything but the viewer and the nodes.
void DrawWorld(std::mt19937 &gen)
{
    svcfx::F(kDevice).Set(20, "", &svcfx::F(kState));
    svcfx::F(kLog).Set(7, "x", 1, &LogUint16);
    svcfx::F(kLog).Set(8, "d", 1);
    svcfx::F(kLog).Set(9, "d", 1);
    svcfx::F(kLog).Set(12, "d", 1);
    svcfx::F(kLog).Set(14, "x", 1, &LogText);
    svcfx::F(kMaterials).Set(33, "xd", 0, &MaterialInfo);
    svcfx::F(kFormats).Set(8, "d", &svcfx::F(kFormat));
    svcfx::F(kFormat).Set(16, "x", 0, &FormatDescribe);
    svcfx::F(kHolder).Set(0, "dx", 1, &HolderQuery);
    for (unsigned s : {kStrategy, kViewStrategy}) {
        svcfx::F(s).Set(3, "x", 0, &StrategyBegin);
        svcfx::F(s).Set(4, "xxx", 0, &StrategyRender);
    }
    svcfx::F(kMaterials).Set(22, "dxddd", 0, &GetVariant);
    for (unsigned i = 0; i < kShaders; ++i) {
        uint32_t *const info = gShaderInfo[i];
        info[0] = 0;
        info[1] = gen() % 6;                   // the sort key
        info[2] = gen() % 4 == 0 ? 0 : 1 + i;  // the pass count
        info[3] = gen();                       // the flags (the geometry is asked about byte 0)
        info[4] = gen();
        svcfx::F(kShader0 + i).Set(9, "", info);
    }
    for (unsigned i = 0; i < kGeometries; ++i) {
        DrawInfo &info = gDrawInfo[i];
        std::memset(&info, 0x5e, sizeof info);
        info.layer = static_cast<int32_t>(gen() % 9) - 4;
        info.format = &svcfx::F(kFormat);
        info.flag = static_cast<uint8_t>(gen() % 3 == 0 ? 0 : gen());
        svcfx::Obj &geometry = svcfx::F(kGeometry0 + i);
        geometry.Set(3, "d", gen() % 4 != 0);
        geometry.Set(4, "", 0u);
        geometry.Set(5, "d", &info);
    }
    svcfx::F(kFormat).Set(3, "", gen());
    gStrategyCalls = 0;
    gRaiseAt = 0; // (sg_display_list_strategy_exception raises: the original cannot catch in the harness)
    gStatsSeed = gen();
}

// ---- the viewer and the nodes (hand-made vtables) -------------------------------------------------------------------
struct FakeViewer {
    void **vt;
    uint32_t renderType, layer[2], byte87, textureMode, textureCount, textureRef94, handleTexture, color, depth;
    uint32_t width, height;
    float scale;
    uint32_t box[6];
    void *strategy;
};
FakeViewer gViewers[2];
void *gViewerVTable[123];

struct FakeNode {
    void **vt;
    int32_t word78;
};
FakeNode gNodes[4];
void *gNodeVTable[100];

uint32_t ViewerIndex(const FakeViewer *v) { return static_cast<uint32_t>(v - gViewers); }
void ViewerEv(const FakeViewer *v, uint32_t slot)
{
    svcfx::Ev(evViewer + slot);
    svcfx::Ev(ViewerIndex(v));
}
uint32_t *__fastcall ViewerLayerState(FakeViewer *self, void *, uint32_t *out)
{
    ViewerEv(self, 86);
    svcfx::Ev(svcfx::Norm(svcfx::P(out)));
    out[0] = self->layer[0];
    out[1] = self->layer[1];
    return out;
}
uint32_t __fastcall ViewerByte87(FakeViewer *self, void *) { return ViewerEv(self, 87), 0xabcdef00u | self->byte87; }
uint32_t __fastcall ViewerTextureCount(FakeViewer *self, void *) { return ViewerEv(self, 105), self->textureCount; }
uint32_t __fastcall ViewerTexture(FakeViewer *self, void *, int32_t index)
{
    ViewerEv(self, 106);
    svcfx::Ev(static_cast<uint32_t>(index));
    return index == 1 ? svcfx::P(&svcfx::F(kTexture)) : 0x7e700000u + static_cast<uint32_t>(index) * 16;
}
uint32_t __fastcall ViewerTextureRef94(FakeViewer *self, void *) { return ViewerEv(self, 107), self->textureRef94; }
uint32_t __fastcall ViewerHandleTexture(FakeViewer *self, void *) { return ViewerEv(self, 109), self->handleTexture; }
uint32_t *__fastcall ViewerTargetSize(FakeViewer *self, void *, uint32_t *out)
{
    ViewerEv(self, 110);
    svcfx::Ev(svcfx::Norm(svcfx::P(out)));
    out[0] = self->width;
    out[1] = self->height;
    std::memcpy(&out[2], &self->scale, 4);
    return out;
}
uint32_t __fastcall ViewerColor(FakeViewer *self, void *) { return ViewerEv(self, 111), self->color; }
uint32_t __fastcall ViewerDepth(FakeViewer *self, void *) { return ViewerEv(self, 112), self->depth; }
uint32_t __fastcall ViewerRenderType(FakeViewer *self, void *) { return ViewerEv(self, 115), self->renderType; }
void *__fastcall ViewerStrategy(FakeViewer *self, void *) { return ViewerEv(self, 117), self->strategy; }
void *__fastcall ViewerHolder(FakeViewer *self, void *) { return ViewerEv(self, 121), &svcfx::F(kHolder); }
uint32_t __fastcall ViewerTextureMode(FakeViewer *self, void *) { return ViewerEv(self, 122), self->textureMode; }
uint32_t __fastcall ViewerDetail(FakeViewer *self, void *) { return ViewerEv(self, 88), self->depth ^ 0x88; }
// 71 (by hidden pointer): the box, after logging the frame (a pointer into a pass) and its words.
uint32_t *__fastcall ViewerBox(FakeViewer *self, void *, uint32_t *out, const uint32_t *frame)
{
    ViewerEv(self, 71);
    svcfx::Ev(svcfx::Norm(svcfx::P(out)));
    svcfx::Ev(NormPassPointer(svcfx::P(frame)));
    for (unsigned i = 0; i < 13; ++i) svcfx::Ev(frame[i]);
    std::memcpy(out, self->box, sizeof self->box);
    return out;
}

uint32_t NodeIndex(const FakeNode *node) { return static_cast<uint32_t>(node - gNodes); }
void __fastcall NodeDraw(FakeNode *self, void *, uint32_t pass, uint32_t flag)
{
    svcfx::Ev(evNode + 83);
    svcfx::Ev(NodeIndex(self));
    svcfx::Ev(NormPassPointer(pass));
    svcfx::Ev(flag);
}
uint32_t __fastcall NodeDetail(FakeNode *self, void *)
{
    svcfx::Ev(evNode + 88);
    svcfx::Ev(NodeIndex(self));
    return static_cast<uint32_t>(self->word78) * 3;
}
void __fastcall NodeSetWord78(FakeNode *self, void *, int32_t value)
{
    svcfx::Ev(evNode + 96);
    svcfx::Ev(NodeIndex(self));
    svcfx::Ev(static_cast<uint32_t>(value));
    self->word78 = value;
}

void FillVTables()
{
    gViewerVTable[71] = reinterpret_cast<void *>(&ViewerBox);
    gViewerVTable[86] = reinterpret_cast<void *>(&ViewerLayerState);
    gViewerVTable[88] = reinterpret_cast<void *>(&ViewerDetail);
    gViewerVTable[87] = reinterpret_cast<void *>(&ViewerByte87);
    gViewerVTable[105] = reinterpret_cast<void *>(&ViewerTextureCount);
    gViewerVTable[106] = reinterpret_cast<void *>(&ViewerTexture);
    gViewerVTable[107] = reinterpret_cast<void *>(&ViewerTextureRef94);
    gViewerVTable[109] = reinterpret_cast<void *>(&ViewerHandleTexture);
    gViewerVTable[110] = reinterpret_cast<void *>(&ViewerTargetSize);
    gViewerVTable[111] = reinterpret_cast<void *>(&ViewerColor);
    gViewerVTable[112] = reinterpret_cast<void *>(&ViewerDepth);
    gViewerVTable[115] = reinterpret_cast<void *>(&ViewerRenderType);
    gViewerVTable[117] = reinterpret_cast<void *>(&ViewerStrategy);
    gViewerVTable[121] = reinterpret_cast<void *>(&ViewerHolder);
    gViewerVTable[122] = reinterpret_cast<void *>(&ViewerTextureMode);
    gNodeVTable[83] = reinterpret_cast<void *>(&NodeDraw);
    gNodeVTable[88] = reinterpret_cast<void *>(&NodeDetail);
    gNodeVTable[96] = reinterpret_cast<void *>(&NodeSetWord78);
}

// A viewport fraction for `dimension`: plain ones, ties (an odd number of halves when the dimension is a power of two),
// out of range, NaN and infinity.
float Fraction(std::mt19937 &gen, uint32_t dimension)
{
    switch (gen() % 9) {
    case 0: return 0.0f;
    case 1: return 1.0f;
    case 2:
    case 3: return static_cast<float>(2 * (gen() % (dimension + 1)) + 1) / static_cast<float>(2 * dimension);
    case 4: return -static_cast<float>(gen() % 1000) / 997.0f;
    case 5: return gen() % 2 ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
    case 6: return static_cast<float>(gen() % 3000) / 1000.0f;
    default: return static_cast<float>(gen() % 100001) / 100000.0f;
    }
}

float Real(std::mt19937 &gen, float range) { return (static_cast<float>(gen() % 20001) / 10000.0f - 1.0f) * range; }

void DrawViewerData(std::mt19937 &gen, FakeViewer &v)
{
    static const uint32_t kDims[] = {1024, 512, 640, 800, 3, 1};
    v.vt = gViewerVTable;
    v.renderType = gen() % 8;
    v.layer[0] = gen();
    v.layer[1] = gen();
    v.byte87 = gen() % 256;
    v.textureMode = gen() % 5;
    v.textureCount = gen() % 4 == 0 ? gen() % 10 : gen() % 5; // past four: the original's overrun
    v.textureRef94 = 0x7e940000u + gen() % 16;
    v.handleTexture = 0x7eb00000u + gen() % 16;
    v.color = gen() % 3 ? 0x7ec00000u + gen() % 16 : 0;
    v.depth = gen() % 3 ? 0x7ed00000u + gen() % 16 : 0;
    v.width = gen() % 3 ? kDims[gen() % 6] : gen() % 5000;
    v.height = gen() % 3 ? kDims[gen() % 6] : gen() % 5000;
    v.scale = Real(gen, 4.0f);
    for (uint32_t &w : v.box) w = gen();
    v.strategy = gen() % 3 == 0 ? &svcfx::F(kViewStrategy) : nullptr;
}

// ---- the frame cache --------------------------------------------------------------------------------------------------

// The cache with its lists filled: blob word 0 marks the test's blobs (never 0), held objects are fakes.
void FillCache(std::mt19937 &gen, cFrameCache &cache)
{
    cache.mVTable = shape::GameVTable(cFrameCache::kVTable);
    cache.mRefCount = 5 + gen() % 3;
    cache.mFrame = gen() % 6;
    cache.mFlips = gen() % 9;
    uint32_t tag = 0;
    for (std::vector<cFrameCache::Blob> *blobs : {&cache.mBlobsA, &cache.mBlobsB}) {
        blobs->clear();
        blobs->reserve(gen() % 12);
        const uint32_t n = gen() % 9;
        for (uint32_t i = 0; i < n; ++i) {
            cFrameCache::Blob blob;
            for (uint32_t &w : blob.mWords) w = gen() | 1;
            blob.mWords[0] = 0xb10b0000u | ++tag;
            blobs->push_back(blob);
        }
    }
    cache.mBlobCountA = gen() % 9;
    cache.mBlobCountB = gen() % 9;
    for (std::vector<cFrameCache::Held> *held : {&cache.mHeld, &cache.mHeldLast}) {
        held->clear();
        const uint32_t n = gen() % 5;
        for (uint32_t i = 0; i < n; ++i) held->push_back({reinterpret_cast<cIGZUnknown *>(&svcfx::F(16 + gen() % 8)), gen()});
    }
    cache.mHeldCount = static_cast<uint32_t>(gen() % (cache.mHeld.size() + 1));
    cache.mHeldLastCount = static_cast<uint32_t>(gen() % (cache.mHeldLast.size() + 1));
}

void CaptureCache(Blob &o, const cFrameCache &cache)
{
    o.push_back(cache.mVTable == shape::GameVTable(cFrameCache::kVTable) ? 1u : svcfx::P(cache.mVTable));
    o.push_back(cache.mRefCount);
    o.push_back(cache.mFrame);
    o.push_back(cache.mFlips);
    for (const std::vector<cFrameCache::Blob> *blobs : {&cache.mBlobsA, &cache.mBlobsB}) {
        VectorShape(o, *blobs);
        for (const cFrameCache::Blob &blob : *blobs) {
            const bool fresh = blob.mWords[0] == 0; // a new blob: words 1-4 are the original's stack
            for (unsigned i = 0; i < 8; ++i)
                if (!fresh || i == 0 || i >= 5) o.push_back(blob.mWords[i]);
        }
    }
    o.push_back(cache.mBlobCountA);
    o.push_back(cache.mBlobCountB);
    o.push_back(cache.mHeldCount);
    o.push_back(cache.mHeldLastCount);
    for (const std::vector<cFrameCache::Held> *held : {&cache.mHeld, &cache.mHeldLast}) {
        VectorShape(o, *held);
        for (const cFrameCache::Held &h : *held) {
            o.push_back(svcfx::Norm(svcfx::P(h.mObject)));
            o.push_back(h.mWord);
        }
    }
}

void FreeCache(cFrameCache &cache)
{
    Free(cache.mBlobsA);
    Free(cache.mBlobsB);
    Free(cache.mHeld);
    Free(cache.mHeldLast);
}

void CaptureRefs(Blob &o)
{
    for (unsigned i = 0; i < svcfx::kFakes; ++i) o.push_back(static_cast<uint32_t>(svcfx::F(i).refs));
}

// ---- the object -----------------------------------------------------------------------------------------------------------

// A built item's word 0 (the original's stack); the test's own items carry 0xa0a0 in the high half.
uint32_t ItemWord0(uint32_t w) { return (w >> 16) == 0xa0a0 ? w : 0; }

// The object (Capture, with the items and light records taken out), then the items and the records.
void CaptureDraw(Blob &o)
{
    cDisplayList *const self = Self();
    std::vector<DrawItem> items;
    std::vector<LightRecord> records;
    items.swap(self->mItems);
    records.swap(self->mRecords58);
    Capture(o);
    items.swap(self->mItems);
    records.swap(self->mRecords58);
    VectorShape(o, self->mItems);
    for (const DrawItem &item : self->mItems) {
        uint32_t words[sizeof(DrawItem) / 4];
        std::memcpy(words, &item, sizeof item);
        words[0] = ItemWord0(words[0]);
        words[offsetof(DrawItem, mRecord) / 4] = NormPassPointer(words[offsetof(DrawItem, mRecord) / 4]);
        o.insert(o.end(), std::begin(words), std::end(words));
    }
    VectorShape(o, self->mRecords58);
    for (const LightRecord &record : self->mRecords58) {
        o.push_back(svcfx::P(record.mLight));
        o.push_back(svcfx::P(record.mNode) -
                    svcfx::P(gNodes));
    }
    for (const FakeNode &node : gNodes) o.push_back(static_cast<uint32_t>(node.word78));
    CaptureRefs(o);
}

// A display list for the draw tests: `passes` passes, slots allocated, the members the draw slots read drawn.
void DrawObject(std::mt19937 &gen, int side, uint32_t passes)
{
    Fresh(side);
    Slot<void(__thiscall *)(void *, uint32_t)>(31)(gObject, passes);
    if (gen() % 3) Slot<int32_t(__thiscall *)(void *, uint32_t)>(18)(gObject, gen() % 12);
    cDisplayList *const self = Self();
    self->mMaterialManager = reinterpret_cast<nGZSceneGraph::cIMaterialManager *>(&svcfx::F(kMaterials));
    self->mScreenRect.p = reinterpret_cast<nGZSceneGraph::cIGeometryRender *>(&svcfx::F(kGeometry0 + gen() % 3));
    self->mStrategy.p = reinterpret_cast<nGZSceneGraph::cIDisplayListRenderStrategy *>(&svcfx::F(kStrategy));
    using Array = nGZGraphic4::cIDynamicVertexArray;
    self->mVertices1.p = static_cast<Array *>(World::Array(0));
    self->mVertices2.p = static_cast<Array *>(World::Array(1));
    self->mIndices1.p = static_cast<Array *>(World::Array(2));
    self->mIndices2.p = static_cast<Array *>(World::Array(3));
    self->mFrame = gen() % 8;
    self->mTime = Real(gen, 100.0f);
    for (uint32_t &w : self->mStats) w = gen() % 1000;
    for (uint32_t &w : self->mWords84) w = gen();
    self->mCount54 = gen();
    self->mCount64 = gen();
    self->mCount74 = gen();
    self->mWordA0 = gen();
    self->mWordE4 = gen();
}

// The test's own sorted items (word 0 tagged), the sorted count at most their number.
NodeRecord gSeedRecords[2]; // the seeded items' records (their nodes, nodes of the world)

void SeedItems(std::mt19937 &gen)
{
    for (NodeRecord &record : gSeedRecords) {
        std::memset(&record, 0, sizeof record);
        record.mNode = reinterpret_cast<nGZSceneGraph::cIRenderableNode *>(&gNodes[gen() % 4]);
    }
    cDisplayList *const self = Self();
    const uint32_t n = gen() % 6;
    for (uint32_t i = 0; i < n; ++i) {
        DrawItem item{};
        item.mWord00 = 0xa0a00000u | i;
        item.mMaterial = &svcfx::F(kShader0 + gen() % kShaders);
        item.mSortKey = static_cast<int32_t>(gen() % 4);
        item.mGeometry = &svcfx::F(kGeometry0 + gen() % kGeometries);
        item.mUserData = gen();
        item.mSubKey = gen() % 4;
        item.mLayer = static_cast<int32_t>(gen() % 3);
        item.mDepth = Real(gen, 10.0f);
        item.mMaterialIndex = static_cast<int32_t>(gen() % 5);
        item.mRecord = &gSeedRecords[i % 2];
        self->mItems.push_back(item);
    }
    self->mSortedCount = n ? gen() % (n + 1) : 0;
}

void FreeDraw()
{
    cDisplayList *const self = Self();
    self->mLog.p = nullptr;
    self->mStrategy.p = nullptr;
    self->mScreenRect.p = nullptr;
    self->mVertices1.p = self->mVertices2.p = self->mIndices1.p = self->mIndices2.p = nullptr;
    Free(self->mRecords58);
    FreeAll();
}

struct DrawFixture : Fixture {
    svcfx::Globals cells;
    DrawFixture()
    {
        FillVTables();
        cells.Set(0x10f8fcc, &svcfx::F(kMaterials)); // GetCachedQueriedObject's cache
        cells.Set(0x10caa24, &svcfx::F(kFormats));   // the cached vertex formats manager
    }
};

using BuildFn = bool(__cdecl *)(DrawItem *, void *, void *, int32_t, Pass *);
using SetupFn = void(__thiscall *)(void *);
using StrategyFn = void(__thiscall *)(void *, void *, Pass *);

} // namespace

// 0xaeae00: items built for random geometries, materials (-1 refused) and passes (null refused) with random render
// types, formats, flags, shaders (no passes refused) and geometry answers. The result's al, the item (pre-filled, so
// the words it leaves are compared too), every call.
DIFF_TEST(sg_display_list_build_item, 0xaeae00)
{
    DrawFixture fixture;
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 gen(seed * 7919u + 0xaeae00u);
            World world(gen);
            DrawWorld(gen);
            DrawObject(gen, side, 1 + gen() % 2);
            ctx.Mark(side ? "ours" : "original");
            Pass *const pass = gen() % 8 == 0 ? nullptr : &Self()->mPasses[gen() % Self()->mPasses.size()];
            if (pass) pass->mRenderType = gen() % 3 ? gen() % 16 : gen();
            DrawItem item;
            uint32_t fill = gen();
            std::memset(&item, static_cast<int>(fill & 0xff), sizeof item);
            const int32_t material = gen() % 6 == 0 ? -1 : static_cast<int32_t>(gen() % 7);
            void *const geometry = &svcfx::F(kGeometry0 + gen() % kGeometries);
            void *const manager = &svcfx::F(kMaterials);
            const bool built = side == 0 ? reinterpret_cast<BuildFn>(GameAddress<void>(0xaeae00))(
                                               &item, geometry, manager, material, pass)
                                         : cDisplayList::BuildDrawItem(
                                               &item, static_cast<nGZSceneGraph::cIGeometryRender *>(geometry),
                                               static_cast<nGZSceneGraph::cIMaterialManager *>(manager), material, pass);
            Blob &o = outcome[side];
            o.push_back(built);
            uint32_t words[sizeof(DrawItem) / 4];
            std::memcpy(words, &item, sizeof item);
            uint32_t &record = words[offsetof(DrawItem, mRecord) / 4];
            if (record != (fill & 0xff) * 0x01010101u) record = NormPassPointer(record); // the fill: left alone
            o.insert(o.end(), std::begin(words), std::end(words));
            o.insert(o.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            CaptureRefs(o);
            FreeDraw();
        }
        Same(ctx, outcome[0], outcome[1], "build item", seed);
    }
}

// 0xaa2540 and 0xaa24c0: the top pass reset on objects with random members and a pass made dirty first; the strategy
// called on a random pass with random sorted items. The object and its passes, the calls. (An exception from the
// strategy: sg_display_list_strategy_exception.)
DIFF_TEST(sg_display_list_pass_helpers, 0xaa2540)
{
    DrawFixture fixture;
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 gen(seed * 7919u + 0xaa2540u);
            World world(gen);
            DrawWorld(gen);
            const uint32_t passes = 1 + gen() % 3;
            DrawObject(gen, side, passes);
            cDisplayList *const self = Self();
            ctx.Mark(side ? "ours" : "original");
            Blob &o = outcome[side];
            const unsigned steps = 1 + gen() % 6;
            for (unsigned step = 0; step < steps; ++step) {
                if (gen() % 2) {
                    Pass *const pass = &self->mPasses[gen() % passes];
                    if (self->mPassStack.empty() || gen() % 2) self->mPassStack.push_back(pass);
                    // Dirty the top pass: every word but its lists and links.
                    uint8_t *const bytes = reinterpret_cast<uint8_t *>(self->mPassStack.back());
                    for (uint32_t off = 0; off < offsetof(Pass, mPrevious); off += 4) {
                        const uint32_t w = gen();
                        std::memcpy(bytes + off, &w, 4);
                    }
                    for (uint32_t off = offsetof(Pass, mLastShader); off < offsetof(Pass, mEntryCountA); off += 4) {
                        const uint32_t w = gen();
                        std::memcpy(bytes + off, &w, 4);
                    }
                    for (uint32_t off = offsetof(Pass, mViewerRecord); off < sizeof(Pass); off += 4) {
                        const uint32_t w = gen();
                        std::memcpy(bytes + off, &w, 4);
                    }
                    self->mPassStack.back()->mEntryCountA = gen();
                    self->mPassStack.back()->mEntryCountB = gen();
                    self->mPassStack.back()->mNodeCount = gen();
                    self->mCount54 = gen();
                    self->mSortedCount = gen();
                    o.push_back(0x5e7);
                    if (side == 0) reinterpret_cast<SetupFn>(GameAddress<void>(0xaa2540))(gObject);
                    else self->SetupTopPass();
                } else {
                    Free(self->mItems);
                    SeedItems(gen);
                    Pass *const pass = gen() % 6 == 0 ? nullptr : &self->mPasses[gen() % passes];
                    void *const strategy = &svcfx::F(gen() % 2 ? kStrategy : kViewStrategy);
                    o.push_back(0x57a);
                    if (side == 0) reinterpret_cast<StrategyFn>(GameAddress<void>(0xaa24c0))(gObject, strategy, pass);
                    else
                        self->RenderWithStrategy(static_cast<nGZSceneGraph::cIDisplayListRenderStrategy *>(strategy),
                                                 pass);
                }
                CaptureDraw(o);
            }
            o.insert(o.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            FreeDraw();
        }
        Same(ctx, outcome[0], outcome[1], "pass helpers", seed);
    }
}

// 0xaa24c0's handler, ours alone: the harness maps the original without registering it as a module, so the system
// never dispatches to the original's frame handler and its __except cannot be run here (an exception from its callee
// ends the process). From the disassembly: the try covers both slots and the filter is 1 (every exception, swallowed).
// Ours, raising in BeginPass or in Render, against the strategy's slots called by the test up to the raising one (the
// first run): the call returns, nothing after the raise is called; the object, the passes, the calls.
DIFF_TEST(sg_display_list_strategy_exception, 0xaa24c0)
{
    DrawFixture fixture;
    for (uint32_t seed = 0; seed < 200; ++seed) {
        for (uint32_t raiseAt = 1; raiseAt <= 2; ++raiseAt) {
            Blob outcome[2];
            for (int run = 0; run < 2; ++run) {
                std::mt19937 gen(seed * 7919u + 0xaa24c0u);
                World world(gen);
                DrawWorld(gen);
                DrawObject(gen, 1, 1 + gen() % 2);
                SeedItems(gen);
                ctx.Mark(run ? "ours, raising" : "ours");
                Pass *const pass = &Self()->mPasses[gen() % Self()->mPasses.size()];
                auto *const strategy =
                    reinterpret_cast<nGZSceneGraph::cIDisplayListRenderStrategy *>(&svcfx::F(kStrategy));
                if (run == 0) {
                    gRaiseAt = 0;
                    strategy->BeginPass(pass);
                    if (raiseAt == 2) strategy->Render(pass, Self()->mItems.data(), Self()->mSortedCount);
                } else {
                    gRaiseAt = raiseAt;
                    Self()->RenderWithStrategy(strategy, pass);
                }
                Blob &o = outcome[run];
                CaptureDraw(o);
                o.insert(o.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                o.push_back(gStrategyCalls);
                gRaiseAt = 0;
                FreeDraw();
            }
            Same(ctx, outcome[0], outcome[1], raiseAt == 1 ? "exception in BeginPass" : "exception in Render", seed);
        }
    }
}

// 0xaa2e10, 0xa9e3f0 and 0xa9e5b0: flips with random slot counts and frames on caches with random lists (blobs to
// grow or not, held objects released), then the destructor, or the deleting destructor with flags 0 or 1 (the memory
// freed or not). The cache after each step, the result, the fakes' counts and every release.
DIFF_TEST(sg_frame_cache, 0xaa2e10)
{
    DrawFixture fixture;
    using FlipFn = void(__thiscall *)(void *, uint32_t, uint32_t);
    using DestructFn = void(__thiscall *)(void *);
    using DeletingFn = void *(__thiscall *)(void *, uint32_t);
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 gen(seed * 7919u + 0xaa2e10u);
            World world(gen);
            ctx.Mark(side ? "ours" : "original");
            void *const memory = ::operator new(sizeof(cFrameCache));
            cFrameCache *const cache = new (memory) cFrameCache{};
            FillCache(gen, *cache);
            Blob &o = outcome[side];
            const unsigned flips = gen() % 4;
            for (unsigned i = 0; i < flips; ++i) {
                const uint32_t slots = gen() % 3 == 0 ? 0 : gen() % 14;
                const uint32_t frame = gen();
                if (side == 0) reinterpret_cast<FlipFn>(GameAddress<void>(0xaa2e10))(cache, slots, frame);
                else cache->Flip(slots, frame);
                CaptureCache(o, *cache);
                // New objects held this frame, as a renderer would add them.
                if (gen() % 2 && cache->mHeldCount < cache->mHeld.size())
                    cache->mHeld[cache->mHeldCount++] = {reinterpret_cast<cIGZUnknown *>(&svcfx::F(16 + gen() % 8)), gen()};
            }
            const uint32_t end = gen() % 3;
            bool freed = false;
            if (end == 0) {
                if (side == 0) reinterpret_cast<DestructFn>(GameAddress<void>(0xa9e3f0))(cache);
                else cache->Destruct();
            } else {
                const uint32_t flags = end - 1;
                difftest::FreeRecorder frees;
                void *const result = side == 0 ? reinterpret_cast<DeletingFn>(GameAddress<void>(0xa9e5b0))(cache, flags)
                                               : cache->DeletingDestructor(flags);
                frees.Stop();
                o.push_back(result == cache);
                // Whether the object's block went back to the heap (the vectors' blocks are compared by the releases).
                o.push_back(0xf2ee0000u | (frees.Freed(memory) ? 1u : 0u));
                freed = frees.Freed(memory);
            }
            if (!freed) {
                CaptureCache(o, *cache);
                FreeCache(*cache);
                ::operator delete(memory);
            }
            o.insert(o.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            CaptureRefs(o);
        }
        Same(ctx, outcome[0], outcome[1], "frame cache", seed);
    }
}

// Slots 23 (DrawViewer 0xa9fb40), 24 (DrawPass 0xaa0920) and 29 (AddGeometry 0xa9f460): runs of up to 30 steps on 1-3
// passes: views pushed for two viewers (their fields, textures past four, targets and viewport fractions with ties, NaN
// and infinity, random view frames and matrices, a frame cache behind by 0-3 frames), geometry added to the top pass, a
// null one or another, entries, node records and light records given to the top pass, the buffers' readiness, the
// draw switch, the log and the frame changed, views drawn (strategies raising sometimes). After each step the result,
// the object with its passes, items and records, and the frame cache; at the end every call in order.
DIFF_TEST(sg_display_list_draw, 0xa9fb40)
{
    DrawFixture fixture;
    using DrawViewerFn = uint32_t(__thiscall *)(void *, void *, const float *, const float *, const float *,
                                                const void *, const float *, const float *);
    using DrawPassFn = void(__thiscall *)(void *, uint32_t, const int32_t *, uint32_t);
    using AddGeometryFn = void(__thiscall *)(void *, void *, int32_t, Pass *, uint32_t);
    for (uint32_t seed = 0; seed < 2500; ++seed) {
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 gen(seed * 7919u + 0xa9fb40u);
            World world(gen);
            DrawWorld(gen);
            svcfx::F(kLog).refs = 1;
            for (FakeViewer &v : gViewers) DrawViewerData(gen, v);
            for (FakeNode &node : gNodes) node = {gNodeVTable, static_cast<int32_t>(gen() % 7)};
            const uint32_t passes = 1 + gen() % 3;
            DrawObject(gen, side, passes);
            FillCache(gen, gCache);
            cDisplayList *const self = Self();
            self->mBuffersReady = gen() % 5 != 0;
            self->mSwitches[1] = gen() % 4 != 0;
            self->mLog.p = gen() % 3 == 0 ? reinterpret_cast<cIGZUnknown *>(&svcfx::F(kLog)) : nullptr;
            SeedItems(gen);
            ctx.Mark(side ? "ours" : "original");
            Blob &o = outcome[side];
            const unsigned steps = 1 + gen() % 30;
            for (unsigned step = 0; step < steps; ++step) {
                const uint32_t op = gen() % 10;
                const bool stacked = !self->mPassStack.empty();
                Pass *const top = stacked ? self->mPassStack.back() : nullptr;
                o.push_back(0x100 + op);
                switch (op) {
                case 0:
                case 1:
                    if (self->mPassStack.size() < passes) {
                        FakeViewer &viewer = gViewers[gen() % 2];
                        float viewport[6];
                        viewport[0] = Fraction(gen, viewer.width);
                        viewport[1] = Fraction(gen, viewer.height);
                        viewport[2] = Fraction(gen, viewer.width);
                        viewport[3] = Fraction(gen, viewer.height);
                        viewport[4] = Real(gen, 1.0f);
                        viewport[5] = Real(gen, 1.0f);
                        uint32_t view[13];
                        view[0] = gen() % 8;
                        for (unsigned i = 1; i < 13; ++i) {
                            const float f = i < 10 && gen() % 3 == 0 ? ((i - 1) % 4 == 0 ? 1.0f : 0.0f)
                                                                     : Real(gen, i < 10 ? 2.0f : 50.0f);
                            std::memcpy(&view[i], &f, 4);
                        }
                        float matrices[2][16];
                        for (auto &m : matrices)
                            for (float &f : m) f = Real(gen, 3.0f);
                        gCache.mFrame = self->mFrame - gen() % 4;
                        const uint32_t result = Slot<DrawViewerFn>(23)(gObject, &viewer, viewport, viewport + 2,
                                                                       viewport + 4, view, matrices[0], matrices[1]);
                        o.push_back(NormPassPointer(result));
                    }
                    break;
                case 2:
                case 3: {
                    Pass *const pass = gen() % 5 == 0   ? nullptr
                                       : gen() % 4 == 0 ? &self->mPasses[gen() % passes]
                                                        : top;
                    const int32_t material = gen() % 6 == 0 ? -1 : static_cast<int32_t>(gen() % 7);
                    Slot<AddGeometryFn>(29)(gObject, &svcfx::F(kGeometry0 + gen() % kGeometries), material, pass, gen());
                    break;
                }
                case 4:
                    if (top) {
                        const uint32_t n = gen() % 4;
                        for (uint32_t i = 0; i < n; ++i) {
                            PassEntry entry{&gNodes[gen() % 4], static_cast<uint8_t>(gen()), {0x11, 0x22, 0x33}};
                            if (gen() % 2) top->mEntriesA.push_back(entry);
                            else top->mEntriesB.push_back(entry);
                        }
                        top->mEntryCountA = static_cast<uint32_t>(gen() % (top->mEntriesA.size() + 1));
                        top->mEntryCountB = static_cast<uint32_t>(gen() % (top->mEntriesB.size() + 1));
                        const uint32_t m = gen() % 3;
                        for (uint32_t i = 0; i < m; ++i) {
                            NodeRecord record;
                            std::memset(&record, 0, sizeof record);
                            record.mNode = reinterpret_cast<nGZSceneGraph::cIRenderableNode *>(&gNodes[gen() % 4]);
                            record.mPlaneMask = static_cast<uint8_t>(gen());
                            top->mNodes.push_back(record);
                        }
                        top->mNodeCount = static_cast<uint32_t>(gen() % (top->mNodes.size() + 1));
                    }
                    break;
                case 5: {
                    const uint32_t n = gen() % 4;
                    for (uint32_t i = 0; i < n; ++i) {
                        LightRecord record{};
                        record.mLight = reinterpret_cast<nGZSceneGraph::cILight *>(static_cast<uintptr_t>(gen()));
                        record.mNode = reinterpret_cast<nGZSceneGraph::cILightRefNode *>(&gNodes[gen() % 4]);
                        self->mRecords58.push_back(record);
                    }
                    self->mCount54 = static_cast<uint32_t>(gen() % (self->mRecords58.size() + 1));
                    break;
                }
                case 6:
                case 7:
                    if (stacked) {
                        if (self->mLog.p)
                            for (Pass &p : self->mPasses)
                                if (!p.mViewerRecord.mNode)
                                    p.mViewerRecord.mNode =
                                        reinterpret_cast<nGZSceneGraph::cIRenderableNode *>(&gNodes[gen() % 4]);
                        int32_t materials[5];
                        for (int32_t &m : materials) m = gen() % 5 == 0 ? -1 : static_cast<int32_t>(gen() % 7);
                        const uint32_t count = gen() % 6;
                        Slot<DrawPassFn>(24)(gObject, count, materials, gen() % 1000);
                    }
                    break;
                case 8:
                    self->mBuffersReady = gen() % 5 != 0;
                    self->mSwitches[1] = gen() % 4 != 0;
                    self->mLog.p = gen() % 3 == 0 ? reinterpret_cast<cIGZUnknown *>(&svcfx::F(kLog)) : nullptr;
                    break;
                default: self->mFrame += gen() % 3; break;
                }
                CaptureDraw(o);
                CaptureCache(o, gCache);
            }
            o.insert(o.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            FreeDraw();
            FreeCache(gCache);
        }
        Same(ctx, outcome[0], outcome[1], "draw", seed);
    }
}
