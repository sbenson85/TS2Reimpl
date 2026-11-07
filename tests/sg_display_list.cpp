// Differential tests for nGZSceneGraph::cDisplayList (src/sg/cDisplayList.cpp): the slot allocator (17-20), the passes
// (27, 31), the scene slots (21, 22, 25, 26), the configuration slots and getters (33, 35-47), the query, the sorted
// insertion 0xaa1290, and the life cycle (factory 0xaa0e00, constructor 0xa9d460, deleting destructor 0xa9e580 with 0xa9e090).
//
// The world: the object is one 0x160-byte buffer both sides run on in turn, zeroed (an empty vector is three null
// words) with the members set per case; each side's slot is called through its own vtable at +0: the game's
// (0xe75404) on the original's side, ours (read from an object our constructor built) on ours, so a slot that
// dispatches virtually (19 calls 18 and 20; the query calls AddRef) stays on its side. The lifecycle test builds the
// object itself instead (the original constructor on a 0xcd-filled block against ours by placement, or the two
// factories). The services are svcfx fakes (ts_services_fixture.h): the framework, the graphic system's device (ready
// or not), the COM director handing out four fake dynamic arrays, the graphics behaviour's Connect. The game's operator
// new and sized delete (0x824632 / 0x824624, which the original's vector growth, pass destruction and destructor use)
// are patched to ours, so both sides share one heap and either side's buffers can be freed by the other.
//
// Compared: results (whole eax, except slots 39 / 40, which set only al); the object's words outside its vectors;
// each vector's size and capacity (the growth policy) and contents (pointers into the object or the passes as
// offsets or indices); the passes' bytes with their links; every fake call in order.
#include "sg_display_list_world.h"

namespace {


// ---- the slot allocator ---------------------------------------------------------------------------------------------

struct Block {
    int32_t start;
    uint32_t count;
};

enum Op : uint32_t { opAlloc, opFree, opRealloc, opFrame, opBadFree };

struct Step {
    Op op;
    uint32_t a, b, c;
};

} // namespace

// 0xaa1570 BeginFrame, 0xa9f6a0 AllocSlots, 0xaa0db0 FreeSlots, 0xaa21d0 ReallocSlots: runs of up to 60 operations from an
// empty table, the same run on both sides. The test keeps the blocks allocated (from the original's results; a block
// whose results differ fails the case anyway) and frees only those, whole, or out of range (start past the end, a count
// running past it, a negative start): freeing a slot twice would loop the free list. Counts 0-7 (0 rarely), so runs of
// free slots are found, missed, and extended at the end of the table. The slot table's growth 0xa9c570 (AllocSlots' only
// callee) is covered through it: size, capacity and contents compared.
DIFF_TEST(sg_display_list_slots, 0xa9f6a0)
{
    Fixture fixture;
    using BeginFrameFn = void(__thiscall *)(void *, float);
    using AllocSlotsFn = int32_t(__thiscall *)(void *, uint32_t);
    using FreeSlotsFn = void(__thiscall *)(void *, int32_t, uint32_t);
    using ReallocSlotsFn = int32_t(__thiscall *)(void *, int32_t, uint32_t, uint32_t);
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        std::mt19937 gen(seed * 7919u + 0xa9f6a0u);
        const unsigned steps = 1 + gen() % 60;
        Blob outcome[2];
        std::vector<Step> plan;
        for (int side = 0; side < 2; ++side) {
            Fresh(side);
            ctx.Mark(side ? "ours" : "original");
            std::mt19937 run(seed);
            std::vector<Block> live;
            Blob &o = outcome[side];
            for (unsigned i = 0; i < steps; ++i) {
                // The plan is drawn on the original's side, from its live blocks; ours replays it.
                Step step;
                if (side == 0) {
                    const uint32_t r = gen() % 16;
                    Op op = r < 6 ? opAlloc : r < 10 ? opFree : r < 12 ? opRealloc : r < 15 ? opFrame : opBadFree;
                    if ((op == opFree || op == opRealloc) && live.empty()) op = opAlloc;
                    step.op = op;
                    step.a = gen() % 64 == 0 ? 0 : 1 + gen() % 7;
                    step.b = live.empty() ? 0 : static_cast<uint32_t>(gen() % live.size());
                    step.c = gen();
                    plan.push_back(step);
                } else {
                    step = plan[i];
                }
                const uint32_t size = static_cast<uint32_t>(Self()->mSlots.size());
                int32_t result = 0;
                switch (step.op) {
                case opAlloc:
                    result = Slot<AllocSlotsFn>(18)(gObject, step.a);
                    live.push_back({result, step.a});
                    break;
                case opFree: {
                    const Block block = live[step.b];
                    live.erase(live.begin() + step.b);
                    Slot<FreeSlotsFn>(20)(gObject, block.start, block.count);
                    break;
                }
                case opRealloc: {
                    const Block block = live[step.b];
                    live.erase(live.begin() + step.b);
                    // A zero count frees nothing (the block stays live); a zero new count allocates nothing.
                    const uint32_t count = step.c % 5 == 0 ? 0 : block.count;
                    const uint32_t newCount = step.c % 7 == 0 ? 0 : step.a;
                    if (!count) live.push_back(block);
                    result = Slot<ReallocSlotsFn>(19)(gObject, block.start, count, newCount);
                    if (newCount) live.push_back({result, newCount});
                    break;
                }
                case opFrame: {
                    const float time = static_cast<float>(step.c % 1000) / 8.0f;
                    Slot<BeginFrameFn>(17)(gObject, time);
                    break;
                }
                case opBadFree: {
                    // Out of range: nothing is freed.
                    const uint32_t kind = step.c % 3;
                    const int32_t start = kind == 0 ? static_cast<int32_t>(size + step.c % 4)
                                          : kind == 1 ? static_cast<int32_t>(size ? (step.c >> 8) % size : 0)
                                                      : -1 - static_cast<int32_t>(step.c % 3);
                    const uint32_t count = kind == 1 ? size + 1 : 1 + step.c % 3;
                    Slot<FreeSlotsFn>(20)(gObject, start, count);
                    break;
                }
                }
                o.push_back(0x100 + step.op);
                o.push_back(static_cast<uint32_t>(result));
                Capture(o);
            }
            FreeAll();
        }
        Same(ctx, outcome[0], outcome[1], "slots", seed);
    }
}

// 0xaa2b90 SetPassCount and 0xa9f600 AddPassEntry: runs of pass counts 0-6 (growing past the capacity, shrinking, the
// same count again) with entries added to a pass on the pass stack between them (the stack is the drawing slots'; the
// test pushes one of the passes after each count). SetPassCount's callees are covered through it: the resize
// 0xa9c680 and the default-pass fill 0xa9d350 (size, capacity and contents compared).
DIFF_TEST(sg_display_list_passes, 0xaa2b90)
{
    Fixture fixture;
    using SetPassCountFn = void(__thiscall *)(void *, uint32_t);
    using AddPassEntryFn = void(__thiscall *)(void *, void *, uint8_t);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 gen(seed * 7919u + 0xaa2b90u);
            Fresh(side);
            ctx.Mark(side ? "ours" : "original");
            Blob &o = outcome[side];
            const unsigned steps = 1 + gen() % 12;
            for (unsigned i = 0; i < steps; ++i) {
                const uint32_t count = gen() % 7;
                Slot<SetPassCountFn>(31)(gObject, count);
                Self()->mPassStack.clear();
                if (count) {
                    Self()->mPassStack.push_back(&Self()->mPasses[gen() % count]);
                    // The entry count is the slot's own; some passes start with a count past their list.
                    Pass *const pass = Self()->mPassStack.back();
                    if (gen() % 4 == 0) pass->mEntryCountA = gen() % 3;
                    const unsigned entries = gen() % 12;
                    for (unsigned e = 0; e < entries; ++e) {
                        void *const object = reinterpret_cast<void *>(static_cast<uintptr_t>(gen()));
                        const uint8_t flag = static_cast<uint8_t>(gen());
                        Slot<AddPassEntryFn>(27)(gObject, object, flag);
                    }
                }
                Capture(o);
            }
            FreeAll();
        }
        Same(ctx, outcome[0], outcome[1], "passes", seed);
    }
}

// 0xaa1290 InsertDrawItem: items sorted in DrawItemOrder up to a drawn sorted count (then unsorted ones), the new item
// drawn from the same pools (equal to some, between others, before or after all), capacity exact or with room.
DIFF_TEST(sg_display_list_insert_draw_item, 0xaa1290)
{
    Fixture fixture;
    auto insert = ctx.Original<void(__thiscall *)(void *, const DrawItem *)>(0xaa1290);
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        std::mt19937 gen(seed * 7919u + 0xaa1290u);
        static const int32_t kKeys[] = {0, 1, 2, 3, -1, -2};
        static const uintptr_t kPointers[] = {0x1000, 0x2000, 0x80000000, 0xfffffff0};
        auto make = [&](uint32_t tag) {
            DrawItem item;
            std::memset(&item, 0, sizeof(item));
            item.mWord00 = tag;
            item.mUserData = gen();
            item.mSortKey = kKeys[gen() % (seed % 2 ? 2 : 6)];
            item.mMaterial = reinterpret_cast<void *>(kPointers[gen() % 4]);
            item.mGeometry = reinterpret_cast<void *>(kPointers[gen() % (seed % 3 ? 1 : 4)]);
            item.mSubKey = (gen() % 3) | (gen() % 2 ? 0x10000u : 0);
            item.mLayer = static_cast<int32_t>(gen() % 3);
            item.mDepth = static_cast<float>(gen() % 5) - 2.0f;
            return item;
        };
        const unsigned size = seed < 20 ? seed % 4 : gen() % 40;
        std::vector<DrawItem> items;
        for (unsigned i = 0; i < size; ++i) items.push_back(make(i));
        const unsigned sorted = size ? (gen() % 4 == 0 ? static_cast<unsigned>(gen() % (size + 1)) : size) : 0;
        std::sort(items.begin(), items.begin() + sorted, DrawItemOrder());
        const unsigned room = gen() % 3 == 0 ? 0 : gen() % 4;
        const DrawItem value = make(0x1000);
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            Fresh(side);
            Self()->mItems.reserve(size + room);
            Self()->mItems = items;
            Self()->mSortedCount = sorted;
            ctx.Mark(side ? "ours" : "original");
            if (side) Self()->InsertDrawItem(&value);
            else insert(gObject, &value);
            Capture(outcome[side]);
            FreeAll();
        }
        Same(ctx, outcome[0], outcome[1], "insert", seed);
    }
}

// The configuration slots and getters, through each side's vtable: 33 SetBufferSizes (through 0xaa11f0: the buffers released and
// created again on a ready device, each Create answering drawn; or only marked not ready), 35 TakeStats (with and without the
// reset), 36-38 the matrix and vectors, and the getters 39-47 on random members. mDrawing (+0x32) is 0 or 1, the values
// the drawing slots store (IsDrawing answers the byte: the original returns it whole in eax, so another byte would
// differ in eax's bits but not as the bool its callers test). Compared: the result, the out words, the object, and every
// fake call in order.
DIFF_TEST(sg_display_list_config, 0xaa2d90)
{
    Fixture fixture;
    using Getter = uint32_t(__thiscall *)(void *);
    using SetFloats = void(__thiscall *)(void *, const float *);
    for (uint32_t seed = 0; seed < 2000; ++seed) {
        std::mt19937 gen(seed * 7919u + 0xaa2d90u);
        uint8_t start[kObjectSize];
        for (uint8_t &b : start) b = static_cast<uint8_t>(gen());
        // The vectors empty (the getters read the pass stack's and the slot table's sizes); the arrays the fakes.
        static const uint32_t kVectors[] = {0x3c, 0x48, 0x58, 0x68, 0x78, 0x94, 0xa4, 0xb4, 0xc0, 0xcc};
        for (uint32_t v : kVectors) std::memset(start + v, 0, 12);
        start[0x32] = static_cast<uint8_t>(gen() % 2);
        const unsigned slot = 33 + gen() % 15;
        uint32_t args[16], out[8];
        for (uint32_t &a : args) a = gen();
        const uint8_t reset = static_cast<uint8_t>(gen() % 3 == 0 ? gen() : gen() % 2);
        const unsigned stackSize = gen() % 3, slotCount = gen() % 4;
        const uint32_t worldSeed = gen();
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 worldGen(worldSeed);
            World world(worldGen, worldSeed % 5 != 0);
            std::memcpy(gObject, start, kObjectSize);
            for (unsigned i = 0; i < 4; ++i) {
                void *const array = World::Array(i);
                std::memcpy(gObject + 0x18 + 4 * i, &array, 4);
            }
            shape::VTableAt(gObject, 0) = side == 0 ? shape::GameVTable(kVTable) : gOurVTable;
            for (unsigned i = 0; i < stackSize; ++i) Self()->mPassStack.push_back(nullptr);
            Self()->mSlots.resize(slotCount);
            for (uint32_t &w : out) w = 0x5e5e5e5e;
            ctx.Mark(side ? "ours" : "original");
            uint32_t result = 0;
            const float *floats = reinterpret_cast<const float *>(args);
            switch (slot) {
            case 33:
                Slot<void(__thiscall *)(void *, uint32_t, uint32_t)>(33)(gObject, args[0] % 0x400000, args[1] % 0x400000);
                break;
            case 34: // OpenLog makes a file: TakeStats instead.
            case 35: // eax is left holding out (compared as an offset)
                result = static_cast<uint32_t>(Slot<uint32_t *(__thiscall *)(void *, uint32_t *, uint8_t)>(35)(gObject, out, reset) - out);
                break;
            case 36:
            case 37:
            case 38: Slot<SetFloats>(slot)(gObject, floats); break;
            case 39:
            case 40: result = static_cast<uint8_t>(Slot<Getter>(slot)(gObject)); break;
            case 41:
            case 42:
            case 43: result = Slot<Getter>(slot)(gObject); break;
            case 44: // eax is left holding indexKB (compared as an offset)
                result = static_cast<uint32_t>(Slot<uint32_t *(__thiscall *)(void *, uint32_t *, uint32_t *)>(44)(gObject, &out[0], &out[1]) - out);
                break;
            case 45:
            case 46:
            case 47: {
                const float *got = Slot<const float *(__thiscall *)(void *)>(slot)(gObject);
                result = static_cast<uint32_t>(reinterpret_cast<const uint8_t *>(got) - gObject);
                break;
            }
            }
            Blob &o = outcome[side];
            o.push_back(slot);
            o.push_back(result);
            o.insert(o.end(), std::begin(out), std::end(out));
            Capture(o);
            o.insert(o.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            FreeAll();
        }
        Same(ctx, outcome[0], outcome[1], "config", seed);
    }
}

// 0xaa2160 QueryInterface: the display list's interface, 1 and the behaviour's two (one counted, one not), others
// refused, a null out; the reference count is the behaviour's (+4), read back through the vtable's AddRef.
DIFF_TEST(sg_display_list_query, 0xaa2160)
{
    Fixture fixture;
    using QueryFn = bool(__thiscall *)(void *, uint32_t, void **);
    static const uint32_t kIids[] = {nGZSceneGraph::cIDisplayList::kIid, 1, cDisplayList::kIidBehavior2,
                                     cDisplayList::kIidBehaviorType, nGZSceneGraph::cIDisplayList::kClassId, 0,
                                     cDisplayList::kMessageIid, 0x12345678};
    for (uint32_t seed = 0; seed < 200; ++seed) {
        std::mt19937 gen(seed * 7919u + 0xaa2160u);
        const uint32_t iid = seed < 16 ? kIids[seed % 8] : gen() % 2 ? kIids[gen() % 8] : gen();
        const bool nullOut = seed % 9 == 4;
        const uint32_t refs = gen() % 100;
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            Fresh(side);
            std::memcpy(gObject + 4, &refs, 4);
            void *out = reinterpret_cast<void *>(0x5e5e5e5e);
            ctx.Mark(side ? "ours" : "original");
            const bool ok = Slot<QueryFn>(0)(gObject, iid, nullOut ? nullptr : &out);
            Blob &o = outcome[side];
            o.push_back(ok);
            o.push_back(out == gObject ? 1u : shape::P(out));
            Capture(o);
        }
        Same(ctx, outcome[0], outcome[1], "query", seed);
    }
}

// 0xa9d460 the constructor and 0xa9e580 the deleting destructor (0xa9e090), with runs of slots through each side's vtable
// between them: the original's constructor on a block of 0x160 bytes filled with 0xcd, ours (placement) on another, in
// the fakes' world (a device or none; readiness and the arrays' answers drawn again before each SetBufferSizes). The
// steps: 33 SetBufferSizes, 31 SetPassCount, 18 AllocSlots / 20 FreeSlots, 17 BeginFrame, 35 TakeStats, 32
// SetRenderStrategy (null: a new default strategy; or the fake F(10)), 36 SetMatrix, and the getters. Compared: the
// object after each step (the default strategy by its vtable word), every fake call in order (the arrays and the
// graphics system made, connected, released), and the step results.
DIFF_TEST(sg_display_list_lifecycle, 0xa9d460)
{
    Fixture fixture;
    auto construct = ctx.Original<void *(__thiscall *)(void *)>(0xa9d460);
    auto create = ctx.Original<void *(__cdecl *)()>(0xaa0e00);
    constexpr unsigned kStrategy = 10;
    for (uint32_t seed = 0; seed < 400; ++seed) {
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 gen(seed * 7919u + 0xa9d460u);
            World world(gen, seed % 7 != 3);
            svcfx::F(kStrategy).Reset(200 + kStrategy);
            // Every fourth seed through the factories (0xaa0e00 and the hook's adapter) on fresh heap blocks.
            const bool factory = seed % 4 == 0;
            uint8_t *memory = nullptr;
            if (!factory) {
                memory = static_cast<uint8_t *>(::operator new(kObjectSize));
                std::memset(memory, 0xcd, kObjectSize);
                svcfx::AddRange(memory, kObjectSize, 0x7a000000);
            }
            ctx.Mark(side ? "ours" : "original");
            if (factory) memory = reinterpret_cast<uint8_t *>(side ? static_cast<void *>(cDisplayListAdapters::Create()) : create());
            else if (side) new (memory) cDisplayList;
            else construct(memory);
            gAt = memory;
            gConstructed = true;
            Blob &o = outcome[side];
            if (side == 0 && !factory) {
                // The masked words are the constructor's untouched bytes (a write there would be hidden otherwise).
                ++ctx.cases;
                bool left = memory[0x33] == 0xcd;
                for (uint32_t off = 0; off < kObjectSize; ++off) left = left && (!LeftByConstructor(off & ~3u) || memory[off] == 0xcd);
                if (!left) ctx.Fail("lifecycle seed %u: the original's constructor wrote a word the capture masks", seed);
            }
            Capture(o);
            std::vector<std::pair<int32_t, uint32_t>> live;
            const unsigned steps = gen() % 12;
            for (unsigned i = 0; i < steps; ++i) {
                const uint32_t op = gen() % 9;
                uint32_t result = 0x5e5e5e5e;
                uint32_t out[8];
                for (uint32_t &w : out) w = 0x5e5e5e5e;
                float floats[16];
                for (float &f : floats) f = static_cast<float>(gen() % 200) / 8.0f;
                switch (op) {
                case 0: {
                    World::Draw(gen);
                    const uint32_t vertexKB = gen() % 0x400, indexKB = gen() % 0x400;
                    Slot<void(__thiscall *)(void *, uint32_t, uint32_t)>(33)(memory, vertexKB, indexKB);
                    break;
                }
                case 1: Slot<void(__thiscall *)(void *, uint32_t)>(31)(memory, gen() % 5); break;
                case 2: {
                    const uint32_t count = 1 + gen() % 5;
                    result = static_cast<uint32_t>(Slot<int32_t(__thiscall *)(void *, uint32_t)>(18)(memory, count));
                    live.push_back({static_cast<int32_t>(result), count});
                    break;
                }
                case 3:
                    if (!live.empty()) {
                        const auto block = live.back();
                        live.pop_back();
                        Slot<void(__thiscall *)(void *, int32_t, uint32_t)>(20)(memory, block.first, block.second);
                    }
                    break;
                case 4: Slot<void(__thiscall *)(void *, float)>(17)(memory, floats[0]); break;
                case 5:
                    result = static_cast<uint32_t>(
                        Slot<uint32_t *(__thiscall *)(void *, uint32_t *, uint8_t)>(35)(memory, out, static_cast<uint8_t>(gen() % 2)) - out);
                    break;
                case 6:
                    Slot<void(__thiscall *)(void *, void *)>(32)(memory, gen() % 2 ? nullptr : &svcfx::F(kStrategy));
                    break;
                case 7: Slot<void(__thiscall *)(void *, const float *)>(36)(memory, floats); break;
                case 8: {
                    const unsigned getter = 39 + gen() % 5;
                    result = Slot<uint32_t(__thiscall *)(void *)>(getter)(memory);
                    if (getter == 39 || getter == 40) result &= 0xff;
                    if (getter == 42) result = svcfx::P(&svcfx::F(kStrategy)) == result ? 1 : result ? 2 : 0;
                    break;
                }
                }
                o.push_back(0x100 + op);
                o.push_back(result);
                o.insert(o.end(), std::begin(out), std::end(out));
                Capture(o);
            }
            Slot<void *(__thiscall *)(void *, uint32_t)>(48)(memory, 1);
            gAt = gObject;
            gConstructed = false;
            o.insert(o.end(), svcfx::gEv.begin(), svcfx::gEv.end());
        }
        Same(ctx, outcome[0], outcome[1], "lifecycle", seed);
    }
}

namespace {

// ---- the scene slots' world ------------------------------------------------------------------------------------------
//
// The device F(kDevice) answers BeginScene (29), EndScene (30) and GetStateConst (20: the state F(kState), whose Reset
// is slot 9). The log F(kLog) takes SetUint32 (9) and SetRZCharStr (14, its text logged). The shape nodes are three
// small objects on a hand-made vtable (their slots 86-90 are past the svcfx fakes' 80): GetLayerState (86) writes the
// node's two words, slot 88 answers the node's last frame, slot 90 stores the frame; each call is logged.
constexpr unsigned kState = 10, kLog = 11;
constexpr uint32_t evNode = 0x6e00;

struct FakeNode {
    void **vt;
    uint32_t layer[2];
    uint32_t lastFrame;
    uint32_t stored;
};
FakeNode gNodes[3];
void *gNodeVTable[100];

uint32_t NodeIndex(const FakeNode *node) { return static_cast<uint32_t>(node - gNodes); }

uint32_t *__fastcall NodeLayerState(FakeNode *self, void *, uint32_t *out)
{
    svcfx::Ev(evNode + 86);
    svcfx::Ev(NodeIndex(self));
    out[0] = self->layer[0];
    out[1] = self->layer[1];
    return out;
}

uint32_t __fastcall NodeLastFrame(FakeNode *self, void *)
{
    svcfx::Ev(evNode + 88);
    svcfx::Ev(NodeIndex(self));
    return self->lastFrame;
}

void __fastcall NodeStore(FakeNode *self, void *, uint32_t value)
{
    svcfx::Ev(evNode + 90);
    svcfx::Ev(NodeIndex(self));
    svcfx::Ev(value);
    self->stored = value;
}

uint32_t LogText(svcfx::Obj *, uint32_t *args)
{
    for (const char *p = reinterpret_cast<const char *>(static_cast<uintptr_t>(args[0])); *p; ++p)
        svcfx::Ev(static_cast<uint8_t>(*p));
    svcfx::Ev(0x7e7e);
    return 1;
}

} // namespace

// 0xa9fae0 BeginScene, 0xaa08b0 EndScene, 0xa9f4c0 DrawNode, 0xa9f3c0 AddLightNode: runs of up to 40 steps on 1-3 passes:
// the scenes (the device's calls, the pass stack emptied, the log's header with the frame, the log closed after its
// last frame or kept when it has no frame count), passes pushed by the test (some with their node or second entry
// counts set back, so records are reused), nodes drawn (random placements, boxes, plane masks; the node's last frame
// the display list's or not), light nodes added with the first switch on or off. After each step the object, the
// passes with their node records and entries, and the nodes; at the end every call in order and the log's count.
DIFF_TEST(sg_display_list_scene, 0xa9fae0)
{
    Fixture fixture;
    using VoidFn = void(__thiscall *)(void *);
    using DrawNodeFn = void(__thiscall *)(void *, void *, const void *, const void *, uint32_t);
    using AddLightNodeFn = void(__thiscall *)(void *, void *, uint32_t);
    gNodeVTable[86] = reinterpret_cast<void *>(&NodeLayerState);
    gNodeVTable[88] = reinterpret_cast<void *>(&NodeLastFrame);
    gNodeVTable[90] = reinterpret_cast<void *>(&NodeStore);
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 gen(seed * 7919u + 0xa9fae0u);
            World world(gen);
            svcfx::F(kDevice).Set(29, "");
            svcfx::F(kDevice).Set(30, "");
            svcfx::F(kDevice).Set(20, "", &svcfx::F(kState));
            svcfx::F(kState).Set(9, "d");
            svcfx::F(kLog).Set(9, "d", 1);
            svcfx::F(kLog).Set(14, "x", 1, &LogText); // the text, logged by LogText
            svcfx::F(kLog).refs = 1;
            Fresh(side);
            ctx.Mark(side ? "ours" : "original");
            for (FakeNode &node : gNodes) node = {gNodeVTable, {gen(), gen()}, gen() % 4, 0x5e5e5e5e};
            const uint32_t passes = 1 + gen() % 3;
            Slot<void(__thiscall *)(void *, uint32_t)>(31)(gObject, passes);
            cDisplayList *const self = Self();
            auto configure = [&] {
                self->mFrame = gen() % 4;
                self->mSwitches[0] = static_cast<uint8_t>(gen() % 2);
                self->mLogFrames = gen() % 3;
                self->mLog.p = gen() % 3 ? reinterpret_cast<cIGZUnknown *>(&svcfx::F(kLog)) : nullptr;
            };
            configure();
            Blob &o = outcome[side];
            const unsigned steps = 1 + gen() % 40;
            for (unsigned i = 0; i < steps; ++i) {
                const uint32_t op = gen() % 9;
                const bool stacked = !self->mPassStack.empty();
                switch (op) {
                case 0: Slot<VoidFn>(21)(gObject); break;
                case 1: Slot<VoidFn>(22)(gObject); break;
                case 2:
                case 3: {
                    Pass *const pass = &self->mPasses[gen() % passes];
                    self->mPassStack.push_back(pass);
                    if (gen() % 4 == 0) pass->mNodeCount = gen() % (pass->mNodes.size() + 1);
                    if (gen() % 4 == 0) pass->mEntryCountB = gen() % 3;
                    break;
                }
                case 4:
                case 5:
                    if (stacked) {
                        uint32_t placement[13], bounds[6];
                        for (uint32_t &w : placement) w = gen();
                        for (uint32_t &w : bounds) w = gen();
                        FakeNode *const node = &gNodes[gen() % 3];
                        if (gen() % 3 == 0) node->lastFrame = self->mFrame;
                        Slot<DrawNodeFn>(25)(gObject, node, placement, bounds, gen());
                    }
                    break;
                case 6:
                    if (stacked || !self->mSwitches[0])
                        Slot<AddLightNodeFn>(26)(gObject, &gNodes[gen() % 3], gen());
                    break;
                case 7: configure(); break;
                default: self->mFrame = gen() % 4; break;
                }
                o.push_back(0x100 + op);
                Capture(o);
                for (const FakeNode &node : gNodes) {
                    o.push_back(node.lastFrame);
                    o.push_back(node.stored);
                }
            }
            o.insert(o.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            o.push_back(static_cast<uint32_t>(svcfx::F(kLog).refs));
            self->mLog.p = nullptr;
            FreeAll();
        }
        Same(ctx, outcome[0], outcome[1], "scene", seed);
    }
}

// The slots the structure owns sit where the game's vtable has them (a slot-order mistake calls the wrong function).
DIFF_TEST(sg_display_list_slot_order, 0xe75404)
{
    static const struct {
        unsigned slot;
        uint32_t rva;
    } kSlotsAt[] = {{17, 0xaa1570}, {18, 0xa9f6a0}, {19, 0xaa21d0}, {20, 0xaa0db0}, {21, 0xa9fae0}, {22, 0xaa08b0},
                    {23, 0xa9fb40}, {24, 0xaa0920}, {25, 0xa9f4c0}, {26, 0xa9f3c0}, {27, 0xa9f600}, {28, 0xa9e700},
                    {29, 0xa9f460}, {30, 0xa9f140}, {31, 0xaa2b90}, {32, 0xaa2c80}, {33, 0xaa2b30}, {34, 0xaa07f0},
                    {35, 0xaa2d90}, {36, 0xaa2b50}, {37, 0xaa2cf0}, {38, 0xaa2b10}, {40, 0xaa0fc0}, {41, 0xaa1620},
                    {44, 0xaa0890}, {45, 0xaa0ea0}, {46, 0xaa2e00}, {47, 0xaa0690}, {15, 0xaa0fd0}, {16, 0xaa2d10}};
    for (const auto &entry : kSlotsAt) {
        ++ctx.cases;
        if (shape::GameVTable(kVTable)[entry.slot] != GameAddress<void>(entry.rva))
            ctx.Fail("slot %u is not 0x%x", entry.slot, entry.rva);
    }
}
