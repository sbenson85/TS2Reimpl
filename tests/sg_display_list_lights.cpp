// cDisplayList's AddLight (slot 30, 0xa9f140) and its light list growth (0xaa2890), original vs ours on the shared world
// (tests/sg_display_list_world.h). The result is the node and light calls (arguments, order) and the state left: the
// object, its pass, the light records, the current list and the ambient records.
#include "sg_display_list_world.h"

#include "sg/SGLightInterfaces.h"

using nGZSceneGraph::AmbientRecord;
using nGZSceneGraph::LightRecord;

namespace {

constexpr uint32_t evNode = 0x4e000, evLight = 0x11000;
constexpr uintptr_t kNear = 0x10000; // how far past the records a current pointer is still taken as one

// The light node (a cILightRefNode): its layer state (86), light flag (87), detail limit (88) and record index (96).
struct FakeLightNode {
    void **vt;
    uint32_t layer[2];
    uint8_t flag;
    uint32_t detail;
    int32_t word78;
};
FakeLightNode gNodes[4];
void *gNodeVTable[100];

struct FakeLight {
    void **vt;
    uint32_t type;
};
FakeLight gLights[4];
void *gLightVTable[40];

uint32_t NodeIndex(const FakeLightNode *node) { return static_cast<uint32_t>(node - gNodes); }
void NodeEv(const FakeLightNode *node, uint32_t slot)
{
    svcfx::Ev(evNode + slot);
    svcfx::Ev(NodeIndex(node));
}
uint32_t *__fastcall NodeLayerState(FakeLightNode *self, void *, uint32_t *out)
{
    NodeEv(self, 86);
    svcfx::Ev(svcfx::Norm(svcfx::P(out)));
    out[0] = self->layer[0];
    out[1] = self->layer[1];
    return out;
}
// The flag in al only: the upper bytes are noise the caller must not use.
uint32_t __fastcall NodeFlag(FakeLightNode *self, void *) { return NodeEv(self, 87), 0x5a5a5a00u | self->flag; }
uint32_t __fastcall NodeDetail(FakeLightNode *self, void *) { return NodeEv(self, 88), self->detail; }
void __fastcall NodeSetWord78(FakeLightNode *self, void *, int32_t value)
{
    NodeEv(self, 96);
    svcfx::Ev(static_cast<uint32_t>(value));
    self->word78 = value;
}
uint32_t __fastcall LightType(FakeLight *self, void *)
{
    svcfx::Ev(evLight + 36);
    svcfx::Ev(static_cast<uint32_t>(self - gLights));
    return self->type;
}

struct SceneFixture : Fixture {
    SceneFixture()
    {
        gNodeVTable[86] = reinterpret_cast<void *>(&NodeLayerState);
        gNodeVTable[87] = reinterpret_cast<void *>(&NodeFlag);
        gNodeVTable[88] = reinterpret_cast<void *>(&NodeDetail);
        gNodeVTable[96] = reinterpret_cast<void *>(&NodeSetWord78);
        gLightVTable[36] = reinterpret_cast<void *>(&LightType);
    }
};

// A frame: flags, a matrix (sometimes the identity's entries) and an offset.
void MakeFrame(std::mt19937 &gen, uint32_t frame[13])
{
    frame[0] = gen() % 8;
    std::uniform_real_distribution<float> real(-3.0f, 3.0f);
    for (unsigned i = 1; i < 13; ++i) {
        const float f = i < 10 && gen() % 3 == 0 ? ((i - 1) % 4 == 0 ? 1.0f : 0.0f) : real(gen);
        std::memcpy(&frame[i], &f, 4);
    }
}

// The state before the calls: a pass (layer state, viewer byte, entries naming the nodes), the light records (random
// words, a count up to their size), the current list (pointers anywhere into the records while some are in use, else
// small numbers; numbers past its count) and the ambient records.
void Seed(std::mt19937 &gen, int side, bool numbersInUse)
{
    Fresh(side);
    cDisplayList *const self = Self();
    self->mFrame = gen() % 4;
    self->mPasses.emplace_back();
    Pass &pass = self->mPasses.back();
    self->mPassStack.push_back(&pass);
    pass.mRecord = &pass.mViewerRecord; // as SetPassCount leaves it
    pass.mLayerState[0] = gen() % 4 == 0 ? 0xffffffffu : 1u << (gen() % 4);
    pass.mLayerState[1] = gen() % 3;
    pass.mLayerTagMatch = static_cast<uint8_t>(gen() % 3);
    const uint32_t entries = 1 + gen() % 5;
    for (uint32_t i = 0; i < entries; ++i)
        pass.mEntriesB.push_back(PassEntry{&gNodes[gen() % 4], static_cast<uint8_t>(gen()), {1, 2, 3}});
    pass.mEntryCountB = entries;
    for (FakeLightNode &node : gNodes) {
        node.vt = gNodeVTable;
        node.layer[0] = gen() % 3 == 0 ? 0 : 1u << (gen() % 4);
        node.layer[1] = gen() % 3;
        node.flag = static_cast<uint8_t>(gen() % 3);
        node.detail = gen() % 4;
        node.word78 = static_cast<int32_t>(gen() % 9) - 1;
    }
    for (FakeLight &light : gLights) light = {gLightVTable, gen() % 2 ? 0u : gen() % 5};

    const uint32_t records = gen() % 4 == 0 ? 0 : gen() % 6;
    self->mRecords58.resize(records);
    for (LightRecord &record : self->mRecords58) {
        uint32_t words[sizeof(LightRecord) / 4];
        for (uint32_t &w : words) w = gen();
        std::memcpy(&record, words, sizeof record);
    }
    if (gen() % 3 == 0) self->mRecords58.reserve(records + gen() % 4);
    self->mCount54 = records ? gen() % (records + 1) : 0;
    const uint32_t current = gen() % 4;
    for (uint32_t i = 0; i < current + gen() % 3; ++i) {
        uint32_t value = 0x100u + (gen() % 0x300u);
        if (i < current && self->mCount54)
            value = svcfx::P(self->mRecords58.data()) + gen() % (records * sizeof(LightRecord));
        self->mWords68.push_back(reinterpret_cast<LightRecord *>(static_cast<uintptr_t>(value)));
    }
    // Numbers in use are rebased by a growth as if they pointed into a block at 0 (no light in use): only the growth
    // test keeps them; slot 30's seeds have no current light while no light is in use, as the game always has.
    self->mCount64 = numbersInUse || self->mCount54 ? current : 0;
    if (gen() % 3 == 0) self->mWords68.reserve(self->mWords68.size() + gen() % 3);

    const uint32_t ambient = gen() % 4;
    for (uint32_t i = 0; i < ambient; ++i) {
        AmbientRecord record;
        uint32_t words[4] = {gen(), gen(), gen(), gen()};
        std::memcpy(&record, words, sizeof record);
        self->mRecords78.push_back(record);
    }
    self->mCount74 = gen() % (ambient + 1);
    self->mWords84[0] = gen() % 3;
}

// The light records (those from `seeded` on new: written by slot 30 when `newWritten`, else only grown), the current
// list and the ambient records (each a vector, taken out of the object's capture), then the nodes and the calls.
void CaptureLights(Blob &o, size_t seeded, bool newWritten)
{
    cDisplayList *const self = Self();
    std::vector<LightRecord> records;
    std::vector<LightRecord *> current;
    std::vector<AmbientRecord> ambient;
    records.swap(self->mRecords58);
    current.swap(self->mWords68);
    ambient.swap(self->mRecords78);
    Capture(o);
    records.swap(self->mRecords58);
    current.swap(self->mWords68);
    ambient.swap(self->mRecords78);

    VectorShape(o, self->mRecords58);
    for (size_t i = 0; i < self->mRecords58.size(); ++i) {
        uint32_t words[sizeof(LightRecord) / 4];
        std::memcpy(words, &self->mRecords58[i], sizeof words);
        const uint32_t node = words[1];
        if (node >= svcfx::P(gNodes) && node < svcfx::P(gNodes + 4))
            words[1] = 0x40de0000u + (node - svcfx::P(gNodes)) / sizeof(FakeLightNode);
        if (i >= seeded) {
            // A new record: the original's constructor (0xa9d320) sets only the frames' first words, the rest is the
            // heap's. Slot 30 then writes every byte but the two pads after the current flag; growth alone, nothing.
            if (newWritten) {
                words[offsetof(LightRecord, mPad16) / 4] &= 0xffffu;
            } else {
                o.push_back(words[offsetof(LightRecord, mFrame) / 4]);
                o.push_back(words[offsetof(LightRecord, mInverse) / 4]);
                continue;
            }
        }
        o.insert(o.end(), std::begin(words), std::end(words));
    }
    VectorShape(o, self->mWords68);
    const uintptr_t base = svcfx::P(self->mRecords58.data());
    for (const LightRecord *record : self->mWords68) {
        const uintptr_t p = svcfx::P(record);
        // A pointer into (or just past) the records by its offset; a number (never near the heap) as it is.
        if (base && p >= base && p - base < kNear)
            o.push_back(0x0ff00000u + static_cast<uint32_t>(p - base));
        else
            o.push_back(static_cast<uint32_t>(p));
    }
    VectorShape(o, self->mRecords78);
    for (const AmbientRecord &record : self->mRecords78) {
        uint32_t words[4];
        std::memcpy(words, &record, sizeof record);
        o.insert(o.end(), std::begin(words), std::end(words));
    }
    for (const FakeLightNode &node : gNodes) o.push_back(static_cast<uint32_t>(node.word78));
    o.insert(o.end(), svcfx::gEv.begin(), svcfx::gEv.end());
}

void FreeLights()
{
    cDisplayList *const self = Self();
    Free(self->mRecords58);
    Free(self->mWords68);
    Free(self->mRecords78);
    FreeAll();
}

using AddLightFn = void(__thiscall *)(void *, void *, const void *);
using GrowFn = void(__thiscall *)(void *, uint32_t);

} // namespace

// Slot 30: several lights in turn (any entry, ambient or typed, current or not, shown or not), the records growing and
// the current list following them.
DIFF_TEST(sg_display_list_add_light, 0xa9f140)
{
    SceneFixture fixture;
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 gen(seed * 7919u + 0xa9f140u);
            svcfx::ResetAll();
            Seed(gen, side, false);
            cDisplayList *const self = Self();
            const size_t seeded = self->mRecords58.size();
            ctx.Mark(side ? "ours" : "original");
            const unsigned calls = 1 + gen() % 8;
            for (unsigned call = 0; call < calls; ++call) {
                const Pass &pass = *self->mPassStack.back();
                self->mWords84[1] = gen() % pass.mEntryCountB;
                uint32_t frame[13];
                MakeFrame(gen, frame);
                Slot<AddLightFn>(30)(gObject, &gLights[gen() % 4], frame);
            }
            CaptureLights(outcome[side], seeded, true);
            FreeLights();
        }
        if (!Same(ctx, outcome[0], outcome[1], "add light", seed)) return;
    }
}

// 0xaa2890 on its own: any count (shrinking, the same, growing within or past the capacity), the current list's
// pointers rebased when the block moves (rounded down to whole records; numbers while no light is in use).
DIFF_TEST(sg_display_list_grow_lights, 0xaa2890)
{
    SceneFixture fixture;
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 gen(seed * 7919u + 0xaa2890u);
            svcfx::ResetAll();
            Seed(gen, side, true);
            cDisplayList *const self = Self();
            const size_t seeded = self->mRecords58.size();
            const uint32_t count = static_cast<uint32_t>(self->mRecords58.size()) + gen() % 6 - 2;
            const uint32_t clamped = static_cast<int32_t>(count) < 0 ? 0 : count;
            ctx.Mark(side ? "ours" : "original");
            if (side == 0)
                ctx.Original<GrowFn>(0xaa2890)(gObject, clamped);
            else
                self->GrowLights(clamped);
            CaptureLights(outcome[side], seeded, false);
            FreeLights();
        }
        if (!Same(ctx, outcome[0], outcome[1], "grow lights", seed)) return;
    }
}
