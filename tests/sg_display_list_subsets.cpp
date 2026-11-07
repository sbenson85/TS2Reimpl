// cDisplayList's AddGeometrySubsets (slot 28, 0xa9e700) and its helpers (src/sg/cDisplayListSubsets.cpp): the bone
// frames' growth 0xaa2920 and the two depth measures 0xaf2200 (the box's centre) and 0xaf2340 (its nearest corner),
// original vs ours on the shared world (tests/sg_display_list_world.h).
//
// Slot 28's fakes: two geometries (svcfx: IsSupported 3 with noise above al, GetFlags 4 the test's GeometryInfo,
// GetDrawInfo 5 its GeometrySubsets), two format objects (3), the material manager (GetVariant 22 logs the key words,
// GetInfo 33 through its hidden return pointer), three shaders (9: their ShaderInfo). Each side's light selection
// (0xaa0130, ours: src/sg/cDisplayListLights.cpp) runs on the light world (tests/sg_display_list_lights.h) with light
// types 1-6 (the pass's node records have no colour event source), so the subset's or the record's box and the
// placement reach the lights' strengths in the call log, and the items' light words and blocks are compared.
//
// Compared after each call: the object, its passes and their node records and the stack (Capture), the items (pointers
// into the passes' records, the bone frames and the light bytes as offsets), the bone frames and the depth list, the
// light state; at the end every call in order with its arguments.
#include "sg_display_list_world.h"

#include "sg_display_list_lights.h"

#include "compgeom/CompGeom.h"
#include "sg/cIGeometryRender.h"
#include "sg/cMaterialManager.h"
#include "sg/cShader.h"

#include <cmath>
#include <utility>

using nGZCompGeom::BoundBox;
using nGZCompGeom::FramePlacement;
using nGZCompGeom::RigidTransform3;
using nGZSceneGraph::GeometryInfo;
using nGZSceneGraph::GeometrySubset;
using nGZSceneGraph::MaterialInfo;
using nGZSceneGraph::ShaderInfo;

namespace {

constexpr unsigned kMaterials = 10, kShader0 = 11, kGeometry0 = 14, kFormat0 = 16;
constexpr unsigned kShaders = 3, kGeometries = 2, kMaxSubsets = 6, kMaxBones = 4;

float Real(std::mt19937 &gen, float range) { return (static_cast<float>(gen() % 20001) / 10000.0f - 1.0f) * range; }

// A frame: flags, a matrix (sometimes the identity's entries) and an offset.
template <class Frame> void MakeFrame(std::mt19937 &gen, Frame &frame)
{
    uint32_t words[13];
    words[0] = gen() % 8;
    for (unsigned i = 1; i < 13; ++i) {
        const float f = i < 10 && gen() % 3 == 0 ? ((i - 1) % 4 == 0 ? 1.0f : 0.0f) : Real(gen, i < 10 ? 2.0f : 20.0f);
        std::memcpy(&words[i], &f, 4);
    }
    static_assert(sizeof(Frame) == sizeof words);
    std::memcpy(&frame, words, sizeof words);
}

// A box: min below max mostly, sometimes inverted or flat.
void MakeBox(std::mt19937 &gen, BoundBox &box)
{
    float *const lo = &box.min.x, *const hi = &box.max.x;
    for (unsigned a = 0; a < 3; ++a) {
        lo[a] = Real(gen, 20.0f);
        const uint32_t kind = gen() % 8;
        hi[a] = kind == 0 ? lo[a] : kind == 1 ? lo[a] - Real(gen, 3.0f) : lo[a] + std::abs(Real(gen, 10.0f));
    }
}

// A 4x4 for the depths: random rows; the fourth row sometimes near or below the w threshold.
void MakeMatrix(std::mt19937 &gen, float m[16])
{
    for (unsigned i = 0; i < 16; ++i) m[i] = Real(gen, 2.0f);
    const uint32_t kind = gen() % 5;
    if (kind == 0) m[12] = m[13] = m[14] = 0.0f, m[15] = gen() % 2 ? 1e-5f : 2e-5f;
    if (kind == 1) m[12] = m[13] = m[14] = 0.0f, m[15] = 1.0f;
}

// A value the depth measures must treat as the original does: +-0, +-inf, NaN, huge. An infinite box coordinate with a
// positive w weight makes NaN depths at some corners only (inf / inf), so the nearest corner's NaN rule is seen.
float Special(std::mt19937 &gen)
{
    static const float kValues[] = {0.0f, -0.0f, INFINITY, -INFINITY, NAN, 1e30f, -1e30f, 1e-30f};
    return kValues[gen() % (sizeof kValues / sizeof kValues[0])];
}

// Some of a box's and a matrix's words replaced by special values.
void AddSpecials(std::mt19937 &gen, BoundBox &box, float m[16])
{
    float *const words = &box.min.x;
    const uint32_t boxCount = gen() % 3;
    for (uint32_t k = 0; k < boxCount; ++k) words[gen() % 6] = Special(gen);
    if (gen() % 4 == 0) m[gen() % 16] = Special(gen);
}

// ---- slot 28's world ----------------------------------------------------------------------------------------------------
GeometryInfo gInfo[kGeometries];
GeometrySubset gSubsets[kGeometries][kMaxSubsets];
RigidTransform3 gBind[kGeometries][kMaxBones];
int32_t gBoneIndex[kGeometries][kMaxSubsets];
ShaderInfo gShaderInfo[kShaders];
uint32_t gSupportSalt;
std::vector<std::pair<uint32_t, uint32_t>> gBlocks; // the bone frames' blocks seen in the run, in order
void NoteBlock();

// A pointer the items or the light selection hold: into a pass's node records, into a pass, into the bone frames, or
// as it is (the fakes and the test's globals are the same on both sides).
uint32_t NormPtr(const void *pointer)
{
    NoteBlock();
    const uint32_t p = svcfx::P(pointer);
    if (!p) return 0;
    cDisplayList *const self = Self();
    for (uint32_t i = 0; i < self->mPasses.size(); ++i) {
        const Pass &pass = self->mPasses[i];
        const uint32_t nodes = svcfx::P(pass.mNodes.data());
        if (!pass.mNodes.empty() && p >= nodes && p < nodes + pass.mNodes.size() * sizeof(NodeRecord))
            return 0x0e000000u | i << 20 | (p - nodes);
        const uint32_t base = svcfx::P(&pass);
        if (p >= base && p < base + sizeof(Pass)) return 0x0f000000u | i << 20 | (p - base);
    }
    // The bone frames, or a block they had before a growth moved them (items past the sorted count keep pointing there).
    for (size_t i = gBlocks.size(); i-- > 0;) {
        const uint32_t base = gBlocks[i].first;
        if (p >= base && p < base + gBlocks[i].second) return 0x0b000000u | static_cast<uint32_t>(i) << 16 | (p - base);
    }
    return p;
}

// Notes the bone frames' block (by its first address and bytes) when it is new.
void NoteBlock()
{
    const std::vector<FramePlacement> &frames = Self()->mBoneFrames;
    if (frames.empty()) return;
    const std::pair<uint32_t, uint32_t> block{svcfx::P(frames.data()),
                                              static_cast<uint32_t>(frames.size() * sizeof(FramePlacement))};
    if (gBlocks.empty() || gBlocks.back() != block) gBlocks.push_back(block);
}

uint32_t Mix(uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x9e3779b1u ^ b * 0x85ebca77u;
    h ^= h >> 15;
    return h * 0xc2b2ae3du ^ h >> 13;
}

// The material manager's GetVariant (22): the index, the key words, the rest; a shader by the index and the key.
uint32_t GetVariant(svcfx::Obj *, uint32_t *args)
{
    const uint32_t *key = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(args[1]));
    for (unsigned i = 0; i < 3; ++i) svcfx::Ev(key[i]);
    return svcfx::P(&svcfx::F(kShader0 + (args[0] + key[2] + key[0]) % kShaders));
}

// GetInfo (33): the info through the hidden return pointer (the name hash from the index).
uint32_t GetInfo(svcfx::Obj *, uint32_t *args)
{
    auto *const out = reinterpret_cast<MaterialInfo *>(static_cast<uintptr_t>(args[0]));
    out->nameHash = Mix(args[1], 0x33);
    out->material = nullptr;
    out->parsed = nullptr;
    out->references = 1;
    out->variants = args[1] % 3;
    return args[0];
}

// The geometries' IsSupported (3): drawn from the format word, in al (noise above).
uint32_t IsSupported(svcfx::Obj *, uint32_t *args)
{
    return 0x5a5a5a00u | (Mix(args[0], gSupportSalt) % 5 != 0 ? 1u : 0u);
}

void SubsetsWorld(std::mt19937 &gen)
{
    svcfx::F(kMaterials).Set(22, "dxddd", 0, &GetVariant);
    svcfx::F(kMaterials).Set(33, "xd", 0, &GetInfo);
    for (unsigned i = 0; i < kShaders; ++i) {
        ShaderInfo &info = gShaderInfo[i];
        std::memset(&info, 0, sizeof info);
        info.mFirstPass = gen() % 5 == 0 ? nullptr : reinterpret_cast<nGZSceneGraph::ShaderPass *>(0x1000u + i);
        info.m84 = gen() % 3 == 0 ? gen() : gen() % 6;
        for (uint8_t &b : info.mFlags) b = static_cast<uint8_t>(gen() % 3 == 0 ? 0 : gen());
        svcfx::F(kShader0 + i).Set(9, "", &info);
    }
    for (unsigned f = 0; f < 2; ++f) svcfx::F(kFormat0 + f).Set(3, "", gen());
    gSupportSalt = gen();
    for (unsigned g = 0; g < kGeometries; ++g) {
        GeometryInfo &info = gInfo[g];
        info.mSubsets = gen() % (kMaxSubsets + 1);
        info.mWord04 = gen();
        const bool boned = gen() % 4 != 0;
        info.mBones = boned ? 1 + gen() % kMaxBones : 0;
        info.mBoneFrames = gBind[g];
        for (RigidTransform3 &bind : gBind[g]) {
            bind.translation = {Real(gen, 5.0f), Real(gen, 5.0f), Real(gen, 5.0f)};
            for (float &q : bind.rotation) q = Real(gen, 1.0f);
        }
        for (unsigned s = 0; s < kMaxSubsets; ++s) {
            GeometrySubset &subset = gSubsets[g][s];
            std::memset(&subset, 0x5e, sizeof subset);
            subset.mWords[0] = gen();
            subset.mWords[1] = gen();
            subset.mLayer = static_cast<int32_t>(gen() % 7) - 3;
            subset.mFormat = reinterpret_cast<nGZSceneGraph::cIGeometryFormat *>(&svcfx::F(kFormat0 + gen() % 2));
            subset.mBoneCount = !boned || gen() % 3 == 0 ? 0 : gen() % 6 == 0 ? gen() : 1 + gen() % 4;
            gBoneIndex[g][s] = boned ? static_cast<int32_t>(gen() % info.mBones) : 0;
            subset.mBoneIndices = &gBoneIndex[g][s];
            subset.mBlendCount = gen() % 3 == 0 ? 0 : gen() % 6 == 0 ? gen() : 1 + gen() % 4;
            MakeBox(gen, subset.mBounds);
            subset.mFlag = static_cast<uint8_t>(gen() % 2 ? 0 : gen());
            subset.mSubKeyFlag = static_cast<uint8_t>(gen() % 2 ? 0 : gen());
        }
        svcfx::Obj &geometry = svcfx::F(kGeometry0 + g);
        geometry.Set(3, "d", 0, &IsSupported);
        geometry.Set(4, "", &gInfo[g]);
        geometry.Set(5, "d", 0, [](svcfx::Obj *self, uint32_t *args) {
            const unsigned g = static_cast<unsigned>(self - &svcfx::F(kGeometry0));
            return svcfx::P(&gSubsets[g][args[0]]);
        });
    }
}

// A node record: random layer state, current flag, plane mask (all planes often), placement and box.
void MakeRecord(std::mt19937 &gen, NodeRecord &record)
{
    std::memset(&record, 0, sizeof record);
    record.mNode = reinterpret_cast<nGZSceneGraph::cIRenderableNode *>(static_cast<uintptr_t>(0x2000u + gen() % 16));
    record.mLayerState[0] = gen() % 3 == 0 ? 0 : 1u << (gen() % 4);
    record.mLayerState[1] = gen() % 3;
    record.mCurrent = static_cast<uint8_t>(gen() % 3 == 0 ? gen() : gen() % 2);
    record.mPlaneMask = static_cast<uint8_t>(gen() % 3 == 0 ? 0x3f : gen() % 0x40);
    MakeFrame(gen, record.mPlacement);
    MakeBox(gen, record.mBounds);
}

// A pass for slot 28: render type, layer state, viewer byte, frustum planes, view inverse, matrix 3, node records.
void MakePass(std::mt19937 &gen, Pass &pass)
{
    pass.mRenderType = gen() % 3 ? gen() % 16 : gen();
    pass.mLayerState[0] = gen() % 4 == 0 ? 0xffffffffu : 1u << (gen() % 4) | 1u << (gen() % 4);
    pass.mLayerState[1] = gen() % 3;
    pass.mLayerTagMatch = static_cast<uint8_t>(gen() % 3);
    for (auto &plane : pass.mFrustum.plane) {
        for (unsigned a = 0; a < 3; ++a) plane[a] = Real(gen, 1.0f);
        plane[3] = Real(gen, 30.0f);
    }
    MakeFrame(gen, pass.mViewInverse);
    MakeMatrix(gen, pass.mMatrices[3]);
    const uint32_t records = 1 + gen() % 3;
    pass.mNodes.resize(records);
    for (NodeRecord &record : pass.mNodes) MakeRecord(gen, record);
}

// The object for slot 28: 1-2 passes on the stack, the switches, prior items (some sorted), the light item index.
void SubsetsObject(std::mt19937 &gen, int side)
{
    Fresh(side);
    gBlocks.clear();
    cDisplayList *const self = Self();
    self->mMaterialManager = reinterpret_cast<nGZSceneGraph::cIMaterialManager *>(&svcfx::F(kMaterials));
    const uint32_t passes = 1 + gen() % 2;
    self->mPasses.resize(passes);
    for (Pass &pass : self->mPasses) {
        pass.mRecord = &pass.mViewerRecord;
        pass.mFrameCache = &gCache;
        MakePass(gen, pass);
        self->mPassStack.push_back(&pass);
    }
    for (uint8_t &s : self->mSwitches) s = gen() % 4 != 0;
    for (uint32_t &w : self->mWords84) w = gen();
    self->mWords84[2] = gen() % self->mPassStack.back()->mNodes.size();
    self->mWords84[3] = 0;
    self->mFrame = gen() % 8;
    const uint32_t n = gen() % 5;
    for (uint32_t i = 0; i < n; ++i) {
        DrawItem item{};
        uint32_t words[sizeof(DrawItem) / 4];
        for (uint32_t &w : words) w = gen();
        std::memcpy(&item, words, sizeof item);
        item.mMaterial = &svcfx::F(kShader0 + gen() % kShaders);
        item.mGeometry = &svcfx::F(kGeometry0 + gen() % kGeometries);
        item.mPlacement = reinterpret_cast<const void *>(static_cast<uintptr_t>(0x3000u + gen() % 64));
        item.mRecord = nullptr;
        self->mItems.push_back(item);
    }
    if (gen() % 3 == 0) self->mItems.reserve(n + gen() % 8);
    self->mSortedCount = n ? gen() % (n + 1) : 0;
    self->mWordE4 = self->mSortedCount && gen() % 2 ? gen() % self->mSortedCount : self->mSortedCount;
    LightRecords(gen, self, 6);
    LightBytes(gen, self);
    LightCache(gen, 6);
}

// The items (pointers normalised), the bone frames and the depths, then the rest of the object (Capture, with the three
// vectors it expects empty set aside).
void CaptureSubsets(Blob &o, const LightPointers &pointers)
{
    cDisplayList *const self = Self();
    VectorShape(o, self->mItems);
    for (const DrawItem &item : self->mItems) {
        uint32_t words[sizeof(DrawItem) / 4];
        std::memcpy(words, &item, sizeof item);
        words[offsetof(DrawItem, mPlacement) / 4] = NormPtr(item.mPlacement);
        words[offsetof(DrawItem, mRecord) / 4] = NormPtr(item.mRecord);
        words[offsetof(DrawItem, mWord3c) / 4] = pointers.Item(self, item);
        o.insert(o.end(), std::begin(words), std::end(words));
    }
    VectorShape(o, self->mBoneFrames);
    for (const FramePlacement &frame : self->mBoneFrames) {
        uint32_t words[sizeof(FramePlacement) / 4];
        std::memcpy(words, &frame, sizeof frame);
        o.insert(o.end(), std::begin(words), std::end(words));
    }
    VectorShape(o, self->mDepths);
    for (float depth : self->mDepths) {
        uint32_t word;
        std::memcpy(&word, &depth, 4);
        o.push_back(word);
    }
    CaptureLights(o, pointers);
    std::vector<DrawItem> items;
    std::vector<FramePlacement> frames;
    std::vector<float> depths;
    LightVectors lights;
    items.swap(self->mItems);
    frames.swap(self->mBoneFrames);
    depths.swap(self->mDepths);
    lights.Swap(self);
    Capture(o);
    items.swap(self->mItems);
    frames.swap(self->mBoneFrames);
    depths.swap(self->mDepths);
    lights.Swap(self);
}

void FreeSubsets()
{
    cDisplayList *const self = Self();
    for (Pass &pass : self->mPasses) Free(pass.mNodes);
    Free(self->mBoneFrames);
    Free(self->mDepths);
    FreeLights();
    FreeAll();
}

using SubsetsFn = void(__thiscall *)(void *, void *, uint32_t, const int32_t *, const uint32_t *const *,
                                     const uint32_t *, const uint32_t (*)[2], const RigidTransform3 *, uint32_t);
using GrowFn = void(__thiscall *)(void *, uint32_t);
using CentreFn = float *(__cdecl *)(float *, const BoundBox *, const float *);
using CornerFn = void(__cdecl *)(float *, const BoundBox *, const float *);

} // namespace

// Slot 28: runs of 1-6 calls on 1-2 passes for the two geometries with random flags (every bit the slot reads, and
// others), materials (-1 sometimes), subset words (none sometimes), the word at +0x24, layer states (the record's
// sometimes), bone poses (none sometimes) and user data bases; between calls the current record, the light item, the
// switches and the counts in use change (the bone frames' count back to 0 as a new frame, the sorted count kept or
// cut), and a light record made current or not. After each call the object, items, bone frames, depths and the light
// state; at the end every call.
DIFF_TEST(sg_display_list_subsets, 0xa9e700)
{
    Fixture fixture;
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            HeldFrees held; // no address handed out twice in the side (LightPointers)
            std::mt19937 gen(seed * 7919u + 0xa9e700u);
            World world(gen);
            SubsetsWorld(gen);
            LightFakes(gen);
            SubsetsObject(gen, side);
            cDisplayList *const self = Self();
            ctx.Mark(side ? "ours" : "original");
            Blob &o = outcome[side];
            const unsigned calls = 1 + gen() % 6;
            LightPointers pointers;
            for (unsigned call = 0; call < calls; ++call) {
                if (call) {
                    const uint32_t change = gen() % 7;
                    if (change == 0) self->mWords84[3] = 0;
                    if (change == 1) self->mSortedCount = gen() % (self->mSortedCount + 1);
                    if (change == 2) self->mWords84[2] = gen() % self->mPassStack.back()->mNodes.size();
                    if (change == 3) self->mWordE4 = self->mSortedCount ? gen() % self->mSortedCount : 0;
                    if (change == 4) self->mSwitches[3 + gen() % 2] ^= 1;
                    if (change == 5 && self->mPassStack.size() > 1) self->mPassStack.pop_back();
                    if (change == 6 && self->mCount54) {
                        self->mRecords58[gen() % self->mCount54].mCurrent ^= 1;
                        CurrentLights(self);
                    }
                    if (self->mWordE4 > self->mSortedCount) self->mWordE4 = self->mSortedCount;
                    // The current record is always one of the top pass's (the game's +0x8c is DrawNode's index).
                    self->mWords84[2] %= self->mPassStack.back()->mNodes.size();
                }
                const unsigned g = gen() % kGeometries;
                int32_t materials[kMaxSubsets];
                for (int32_t &m : materials) m = gen() % 6 == 0 ? -1 : static_cast<int32_t>(gen() % 7);
                uint32_t words[kMaxSubsets];
                for (uint32_t &w : words) w = gen();
                const uint32_t *const subsetWords = gen() % 4 == 0 ? nullptr : words;
                const uint32_t word24 = gen() % 3 == 0 ? 0 : gen();
                uint32_t layerStates[kMaxSubsets][2];
                for (auto &state : layerStates) {
                    state[0] = gen() % 4 == 0 ? 0 : 1u << (gen() % 4);
                    state[1] = gen() % 3;
                }
                RigidTransform3 poses[kMaxBones];
                for (RigidTransform3 &pose : poses) {
                    pose.translation = {Real(gen, 5.0f), Real(gen, 5.0f), Real(gen, 5.0f)};
                    for (float &q : pose.rotation) q = Real(gen, 1.0f);
                }
                uint32_t flags = gen() & 0x1f3u;
                if (gen() % 4 == 0) flags |= gen() & 0xfffffe0cu;
                // The user data base: the items' draw slots, so their frame cache blobs (small ones meet last frame's).
                const uint32_t base = gen() % 2 ? gen() % 1000 : gen() % 4;
                pointers.Note(self);
                Slot<SubsetsFn>(28)(gObject, &svcfx::F(kGeometry0 + g), flags, materials, &subsetWords, &word24,
                                    gen() % 3 == 0 ? nullptr : layerStates, gen() % 4 == 0 ? nullptr : poses, base);
                o.push_back(0x2800 + call);
                CaptureSubsets(o, pointers);
            }
            o.insert(o.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            FreeSubsets();
        }
        Same(ctx, outcome[0], outcome[1], "subsets", seed);
        if (HeldFrees::Overflowed()) ctx.Fail("%s seed %u: more frees than HeldFrees holds", "subsets", seed);
    }
}

// 0xaa2920 on its own: any count (shrinking, the same, growing within or past the capacity) with 0-5 frames in use; the
// sorted items' placements rebased when the block moves (rounded down to whole frames), those outside the frames in use
// (or past the sorted count) left.
DIFF_TEST(sg_display_list_grow_bone_frames, 0xaa2920)
{
    Fixture fixture;
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            HeldFrees held; // "the block moved" is an address compare
            std::mt19937 gen(seed * 7919u + 0xaa2920u);
            svcfx::ResetAll();
            Fresh(side);
            cDisplayList *const self = Self();
            const uint32_t size = gen() % 6;
            self->mBoneFrames.resize(size);
            for (FramePlacement &frame : self->mBoneFrames) MakeFrame(gen, frame);
            if (gen() % 3 == 0) self->mBoneFrames.reserve(size + gen() % 6);
            self->mWords84[3] = size ? gen() % (size + 1) : 0;
            const uint32_t data = svcfx::P(self->mBoneFrames.data());
            const uint32_t bytes = static_cast<uint32_t>(self->mBoneFrames.capacity() * sizeof(FramePlacement));
            const uint32_t n = gen() % 7;
            uint32_t before[7];
            for (uint32_t i = 0; i < n; ++i) {
                DrawItem item{};
                item.mWord00 = 0xa0a00000u | i;
                uint32_t p = 0x3000u + gen() % 64;
                if (bytes && gen() % 4 != 0) p = data + gen() % (bytes + 0x40);
                if (bytes && gen() % 8 == 0) p = data - 1 - gen() % 0x40;
                before[i] = p;
                item.mPlacement = reinterpret_cast<const void *>(static_cast<uintptr_t>(p));
                self->mItems.push_back(item);
            }
            self->mSortedCount = n ? gen() % (n + 1) : 0;
            const uint32_t count = gen() % 4 == 0 ? size : gen() % 12;
            ctx.Mark(side ? "ours" : "original");
            if (side == 0)
                ctx.Original<GrowFn>(0xaa2920)(gObject, count);
            else
                self->GrowBoneFrames(count);
            // The placements: as they were (their values are the same draws on both sides, relative to the old block),
            // or moved into the new block (as offsets), or anything else as it is.
            Blob &o = outcome[side];
            const uint32_t now = svcfx::P(self->mBoneFrames.data());
            o.push_back(now == data);
            for (uint32_t i = 0; i < n; ++i) {
                const uint32_t p = svcfx::P(self->mItems[i].mPlacement);
                const uint32_t span = static_cast<uint32_t>(self->mBoneFrames.capacity() * sizeof(FramePlacement));
                if (p == before[i]) o.push_back(0x0a000000u);
                else if (span && p >= now && p < now + span) o.push_back(0x0b000000u | (p - now));
                else o.push_back(p);
            }
            for (DrawItem &item : self->mItems) item.mPlacement = nullptr; // compared above
            CaptureSubsets(o, LightPointers{});
            Free(self->mBoneFrames);
            FreeAll();
        }
        Same(ctx, outcome[0], outcome[1], "grow bone frames", seed);
        if (HeldFrees::Overflowed()) ctx.Fail("%s seed %u: more frees than HeldFrees holds", "grow bone frames", seed);
    }
}

// 0xaf2200 and 0xaf2340: random boxes (inverted and flat ones too) through random matrices (w at, near and below the
// threshold). The depth's bits; 0xaf2200's whole eax (the out pointer).
DIFF_TEST(sg_display_list_depths, 0xaf2200)
{
    static float out[2];
    for (uint32_t seed = 0; seed < 20000; ++seed) {
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 gen(seed * 7919u + 0xaf2200u);
            BoundBox box;
            MakeBox(gen, box);
            float matrix[16];
            MakeMatrix(gen, matrix);
            if (seed % 2) AddSpecials(gen, box, matrix);
            out[0] = out[1] = Real(gen, 9.0f);
            ctx.Mark(side ? "ours" : "original");
            const float *result;
            if (side == 0) {
                result = ctx.Original<CentreFn>(0xaf2200)(&out[0], &box, matrix);
                ctx.Original<CornerFn>(0xaf2340)(&out[1], &box, matrix);
            } else {
                result = cDisplayList::CentreDepth(&out[0], &box, matrix);
                cDisplayList::NearestCornerDepth(&out[1], &box, matrix);
            }
            Blob &o = outcome[side];
            o.push_back(svcfx::P(result) - svcfx::P(out));
            uint32_t words[2];
            std::memcpy(words, out, sizeof words);
            o.insert(o.end(), std::begin(words), std::end(words));
        }
        Same(ctx, outcome[0], outcome[1], "depths", seed);
    }
}
