// cTSWaterEffect (src/ts/cTSWaterEffect.h), lane WP: original vs ours.
//
// Mode 0 builds the effect with the original Create 0xc09210, mode 1 with ours; every slot then runs through the object's own
// vtable. After each step: the result, the log of every fake's calls, both vtable words and every field. At the end the
// destructor through the +4 face's deleting slot (flags 0 leaves the block to compare, flags 1 frees it), with the frees by size.
//
// Fakes: the effects world (tests/ts_effects_collection.h). The water body is a pool node whose slots 3 to 9 and 11 are
// overridden here (slot 11 gives the shared body-node, trees[2]); the splash's message is the game's own message class with its
// vtable's slots patched to log (the constructor 0x1c033d runs for real in the original, inline in ours), from a pool of blocks
// that both sides' allocators (0x1be3b0 and RZMemory::Malloc) hand out; the message server (0x9c5a89 and ours) is a fake whose
// send logs the message's fields.
//
// Covered RVAs: 0xc09210 (Create), 0xc08bf0 (constructor), 0xc08c70 (destructor), 0xc08d00 (deleting destructor), 0xc08cf4
// (its +4 thunk), 0xc091c0 (slot 2), 0xc091f0 (slot 3), 0xc08f40 (slot 5), 0xc09080 (slot 6), 0xc08d20 (MakeBody), 0xc09010
// (TakeBodyDown), 0xc08df0 (Splash).
#include "ts_effects_collection.h"

#include "free_recorder.h"
#include "rz/RZMemory.h"
#include "rz/RZMessageServer.h"
#include "rz/cRZMessage2Standard.h"
#include "shape.h"
#include "ts/cTSCompDescription.h"
#include "ts/cTSWaterEffect.h"

#include <cmath>
#include <cstring>
#include <new>

namespace {

using namespace fxtest;

constexpr uint32_t kGameTables[2] = {0xe91594, 0xe915d0};
constexpr uint32_t kGameBaseTables[2] = {0xe8bd68, 0xda2fa8};

using CreateFn = void *(__cdecl *)(const void *);
using ConstructFn = void *(__thiscall *)(void *, const void *);

enum : uint32_t {
    evBody = 0xc1000000,   // | slot; the body's id, then the arguments
    evMsg = 0xc2000000,    // | slot; the block's index, then the arguments
    evSend = 0xc3000000,   // the server's send: the block's index, the second argument, then the message's words
    evAlloc = 0xc4000000,
    evServer = 0xc5000000,
};

// ---- the body ----
void __fastcall BodyVoid3(FakeNode *n, void *) { Ev({evBody | 3, n->id}); }
void __fastcall BodyVoid4(FakeNode *n, void *) { Ev({evBody | 4, n->id}); }
void __fastcall BodyTwo(FakeNode *n, void *, uint32_t a, uint32_t b) { Ev({evBody | 5, n->id, a, b}); }
void __fastcall BodyInt(FakeNode *n, void *, int32_t a) { Ev({evBody | 6, n->id, uint32_t(a)}); }
void __fastcall BodyAddress(FakeNode *n, void *, const float *p) { Ev({evBody | 7, n->id, Bits(p[0]), Bits(p[1]), Bits(p[2]), Bits(p[3]), Bits(p[4])}); }
void __fastcall BodyVoid8(FakeNode *n, void *) { Ev({evBody | 8, n->id}); }
void __fastcall BodyFloat(FakeNode *n, void *, float v) { Ev({evBody | 9, n->id, Bits(v)}); }
FakeNode *__fastcall BodyNode(FakeNode *n, void *)
{
    Ev({evBody | 11, n->id});
    return &gWorld->trees[2];
}

// ---- the message pool and the server ----
alignas(8) uint8_t gPool[8][0x2c];
uint32_t gPoolNext = 0;
uint32_t PoolIndex(const void *p) { return uint32_t((static_cast<const uint8_t *>(p) - &gPool[0][0]) / 0x2c); }

void *__cdecl PoolMalloc(uint32_t bytes)
{
    Ev({evAlloc, bytes});
    uint8_t *p = gPool[gPoolNext++ % 8];
    std::memset(p, 0xcd, 0x2c);
    return p;
}

uint32_t __fastcall MsgAddRef(uint8_t *m, void *)
{
    Ev({evMsg | 1, PoolIndex(m)});
    uint32_t &count = *reinterpret_cast<uint32_t *>(m + 4);
    return ++count;
}
uint32_t __fastcall MsgRelease(uint8_t *m, void *)
{
    Ev({evMsg | 2, PoolIndex(m)});
    uint32_t &count = *reinterpret_cast<uint32_t *>(m + 4);
    return --count;
}
void __fastcall MsgSetId(uint8_t *m, void *, uint32_t id) { Ev({evMsg | 5, PoolIndex(m), id}); }
template <unsigned Slot> void __fastcall MsgSetValue(uint8_t *m, void *, int32_t v) { Ev({evMsg | Slot, PoolIndex(m), uint32_t(v)}); }

struct FakeServer {
    void *const *vt;
};
void __fastcall ServerSend(FakeServer *, void *, uint8_t *m, uint32_t how)
{
    Ev({evSend, PoolIndex(m), how});
    for (uint32_t off = 4; off < 0x2c; off += 4) Ev({*reinterpret_cast<uint32_t *>(m + off)});
}
template <unsigned Slot> uint32_t __fastcall ServerOther(FakeServer *, void *)
{
    Ev({evServer | Slot});
    return 0;
}
void *gServerVT[8];
FakeServer gServer{gServerVT};
void *__cdecl StandServer()
{
    Ev({evServer});
    return &gServer;
}

struct Overrides {
    void *nodeSaved[kFakeNodeSlots];
    void *messageSaved[64];
    void **messageVT;
    Overrides()
    {
        std::memcpy(nodeSaved, gFakeNodeVT, sizeof(nodeSaved));
        gFakeNodeVT[3] = reinterpret_cast<void *>(&BodyVoid3);
        gFakeNodeVT[4] = reinterpret_cast<void *>(&BodyVoid4);
        gFakeNodeVT[5] = reinterpret_cast<void *>(&BodyTwo);
        gFakeNodeVT[6] = reinterpret_cast<void *>(&BodyInt);
        gFakeNodeVT[7] = reinterpret_cast<void *>(&BodyAddress);
        gFakeNodeVT[8] = reinterpret_cast<void *>(&BodyVoid8);
        gFakeNodeVT[9] = reinterpret_cast<void *>(&BodyFloat);
        gFakeNodeVT[11] = reinterpret_cast<void *>(&BodyNode);
        messageVT = GameAddress<void *>(cRZMessage2Standard::kVTable);
        DWORD old;
        VirtualProtect(messageVT, sizeof(messageSaved), PAGE_READWRITE, &old);
        std::memcpy(messageSaved, messageVT, sizeof(messageSaved));
        messageVT[1] = reinterpret_cast<void *>(&MsgAddRef);
        messageVT[2] = reinterpret_cast<void *>(&MsgRelease);
        messageVT[5] = reinterpret_cast<void *>(&MsgSetId);
        messageVT[11] = reinterpret_cast<void *>(&MsgSetValue<11>);
        messageVT[16] = reinterpret_cast<void *>(&MsgSetValue<16>);
        messageVT[21] = reinterpret_cast<void *>(&MsgSetValue<21>);
        messageVT[26] = reinterpret_cast<void *>(&MsgSetValue<26>);
        gServerVT[0] = reinterpret_cast<void *>(&ServerOther<0>);
        gServerVT[1] = reinterpret_cast<void *>(&ServerOther<1>);
        gServerVT[2] = reinterpret_cast<void *>(&ServerOther<2>);
        gServerVT[3] = reinterpret_cast<void *>(&ServerOther<3>);
        gServerVT[4] = reinterpret_cast<void *>(&ServerSend);
        gServerVT[5] = reinterpret_cast<void *>(&ServerOther<5>);
        gServerVT[6] = reinterpret_cast<void *>(&ServerOther<6>);
        gServerVT[7] = reinterpret_cast<void *>(&ServerOther<7>);
        gPoolNext = 0;
    }
    ~Overrides()
    {
        std::memcpy(gFakeNodeVT, nodeSaved, sizeof(nodeSaved));
        std::memcpy(messageVT, messageSaved, sizeof(messageSaved));
    }
};

struct OurWaterTables {
    EffectVt live, destroyed;
};
const OurWaterTables &OurTables()
{
    static OurWaterTables t = [] {
        OurWaterTables v{};
        alignas(8) static uint8_t block[sizeof(cTSWaterEffect)];
        auto *e = ::new (block) cTSWaterEffect(nullptr);
        v.live = EffectVt::Of(block);
        CallSlot<void *>(block + 4, 0, uint32_t(0));
        (void)e;
        v.destroyed = EffectVt::Of(block);
        return v;
    }();
    return t;
}

uint32_t TableCode(uint32_t word)
{
    const OurWaterTables &ours = OurTables();
    const unsigned live = shape::FaceOf(word, 2, shape::GameTables{kGameTables}, ours.live);
    if (live != ~0u) return shape::kFaceCode | live;
    const unsigned destroyed = shape::FaceOf(word, 2, shape::GameTables{kGameBaseTables}, ours.destroyed);
    if (destroyed != ~0u) return shape::kFaceCode | 0x10 | destroyed;
    return word;
}

uint32_t PointerId(uint32_t word) { return IdOf(reinterpret_cast<const void *>(uintptr_t(word))); }

// Words 0..3 the tables, count and description, 4 the running byte, 5 the elapsed time, 6..8 the position (not before the
// first Update), 9 the owner, 10 the body.
void PushWater(Blob &b, const void *effect, const void *description, bool position)
{
    const auto *w = static_cast<const uint32_t *>(effect);
    b.push_back(TableCode(w[0]));
    b.push_back(TableCode(w[1]));
    b.push_back(w[2]);
    b.push_back(uint32_t(w[3] - uint32_t(reinterpret_cast<uintptr_t>(description))));
    b.push_back(w[4] & 0xff);
    b.push_back(w[5]);
    if (position)
        for (int i = 6; i < 9; ++i) b.push_back(w[i]);
    b.push_back(PointerId(w[9]));
    b.push_back(PointerId(w[10]));
}

void Drain(Blob &b)
{
    b.insert(b.end(), gEv.begin(), gEv.end());
    gEv.clear();
}

void RandomDescription(cTSWaterDescription &d, std::mt19937 &r)
{
    const auto value = [&r] {
        switch (r() % 7) {
        case 0: return 0.0f;
        case 1: return 0.5f;
        case 2: return 1.0f;
        case 3: return -1.0f;
        case 4: return 123456.7f;
        case 5: return std::bit_cast<float>(0x7fc00000u | (r() & 0x3fffffu));
        default: return testhelp::FewOrUniform(r, -3.0f, 3.0f);
        }
    };
    d.mFlags = r() % 3 ? (r() & 1) : r();
    d.mByte8 = uint8_t(r());
    d.mByte9 = uint8_t(r());
    d.mShortA = int16_t(r());
    if (r() % 3 == 0) d.mFloat8 = value();
    d.mFloatC = value();
    // ORIGINAL-BUG (not reproduced): a negative interval makes AddToScene's splash loop run forever (the elapsed time only grows).
    d.mFloat10 = r() % 4 == 0 ? 0.0f : r() % 2 ? 0.25f * float(1 + r() % 4) : std::fabs(value());
    if (d.mFloat10 > 0.0f && d.mFloat10 < 0.1f) d.mFloat10 = 0.1f; // a tiny interval is only slow
    d.mFloat14 = value();
    d.mFloat18 = value();
    d.mFloat1C = value();
    d.mFloat20 = value();
    d.mFloat24 = value();
}

} // namespace

DIFF_TEST(WaterEffect_Construct, 0xc08bf0)
{
    cTSWaterDescription description;
    for (uint32_t seed = 0; seed < 40; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            Blob &b = blobs[mode];
            std::mt19937 r(seed + 1);
            alignas(8) uint8_t block[sizeof(cTSWaterEffect)];
            for (uint8_t &x : block) x = uint8_t(r());
            void *e = mode ? static_cast<void *>(::new (block) cTSWaterEffect(&description)) : ctx.Original<ConstructFn>(0xc08bf0)(block, &description);
            b.push_back(uint32_t(e == block));
            PushWater(b, block, &description, false);
            for (size_t i = 0x11; i < 0x14; ++i) b.push_back(block[i]); // the running flag's padding
            for (size_t i = 0x18; i < 0x24; ++i) b.push_back(block[i]); // the position's garbage stays
            CallSlot<void *>(block + 4, 0, uint32_t(0));
        }
        Same(ctx, blobs[0], blobs[1], "WaterEffect_Construct", seed);
    }
}

DIFF_TEST(WaterEffect_Slots, 0xc09210)
{
    const CreateFn original = ctx.Original<CreateFn>(0xc09210);
    for (uint32_t seed = 0; seed < 400; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            EffectsWorld w;
            Overrides overrides;
            shape::StandIn alloc(0x1be3b0, &RZMemory::Malloc, &PoolMalloc);
            shape::StandIn server(0x9c5a89, &GetCachedMessageServer, &StandServer);
            gEv.clear();
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 7919u + 3);
            cTSWaterDescription description;
            RandomDescription(description, r);
            void *e = mode ? static_cast<void *>(cTSWaterEffect::Create(&description)) : original(&description);
            auto *object = static_cast<cTSWaterEffect *>(static_cast<cITSComponentEffect *>(e));
            PushWater(b, e, &description, false);
            CallSlot<uint32_t>(static_cast<uint8_t *>(e) + 4, 2); // the caller's reference
            CallSlot<void>(e, 0, static_cast<void *>(&w.owners[0]));
            {
                fx::EffectsTransform local, parent;
                RandomTransform(local, r);
                RandomTransform(parent, r);
                CallSlot<void>(e, 6, &local, &parent);
                PushWater(b, e, &description, true);
            }
            for (uint32_t step = 0; step < 40; ++step) {
                b.push_back(0x0b000000u | step);
                const uint32_t pick = r() % 7;
                if (pick <= 1) {
                    // Bounded: a huge time step would run the splash loop for ever.
                    const uint32_t kind = r() % 10;
                    const float dt = kind == 0 ? 0.0f : kind == 1 ? std::bit_cast<float>(0x7fc00000u | (r() & 0x3fffffu)) : kind == 2 ? -0.25f
                                     : std::uniform_real_distribution<float>(0.0f, 1.5f)(r);
                    CallSlot<void>(e, 5, reinterpret_cast<void *>(uintptr_t(Bits(dt))), reinterpret_cast<void *>(uintptr_t(r())));
                    b.push_back(0xd5);
                } else if (pick == 2) {
                    CallSlot<void>(e, 3, uint32_t(r()));
                    b.push_back(0xd3);
                } else if (pick == 3) {
                    CallSlot<void>(e, 2, uint32_t(r()));
                    b.push_back(0xd2);
                } else if (pick == 6) {
                    fx::EffectsTransform local, parent;
                    RandomTransform(local, r);
                    RandomTransform(parent, r);
                    CallSlot<void>(e, 6, &local, &parent);
                    b.push_back(0xd6);
                } else {
                    // Without the node: the random slots 3, 5, 6 and 9 are left to the steps above (a garbage time step would
                    // run the splash loop for ever).
                    EffectStep(b, e, w, r, false, true);
                }
                Drain(b);
                PushWater(b, e, &description, true);
                // ORIGINAL-BUG (not reproduced): Start with the body flag and no owner attached calls through the null owner. The
                // owner dropped by an Attach(null) goes back before the next step.
                if (!object->mOwner.Get()) CallSlot<void>(e, 0, static_cast<void *>(&w.owners[0]));
            }
            b.push_back(0x0b0000ffu);
            // The end: the deleting destructor through the count's face.
            const bool free = seed % 2 != 0;
            alignas(8) uint8_t after[sizeof(cTSWaterEffect)];
            difftest::FreeRecorder rec;
            if (free) {
                object->mRefCount = 1;
                b.push_back(CallSlot<uint32_t>(e, 13));
            } else {
                b.push_back(uint32_t(CallSlot<void *>(static_cast<uint8_t *>(e) + 4, 0, uint32_t(0)) == e));
                std::memcpy(after, e, sizeof(after));
            }
            rec.Stop();
            Drain(b);
            size_t sizes[4] = {};
            const size_t freed = rec.SortedSizes(sizes, 4);
            b.push_back(uint32_t(freed));
            b.push_back(uint32_t(rec.Freed(e)));
            for (size_t i = 0; i < freed && i < 4; ++i) b.push_back(uint32_t(sizes[i]));
            if (!free) {
                const auto *words = reinterpret_cast<const uint32_t *>(after);
                b.push_back(TableCode(words[0]));
                b.push_back(TableCode(words[1]));
                // Word 4 is the running byte and three bytes of padding; WaterEffect_Construct compares the padding.
                for (int i = 2; i < 11; ++i) {
                    if (i == 3) b.push_back(uint32_t(words[i] - uint32_t(reinterpret_cast<uintptr_t>(&description))));
                    else if (i == 4) b.push_back(words[i] & 0xff);
                    else if (i == 9 || i == 10) b.push_back(PointerId(words[i]));
                    else b.push_back(words[i]);
                }
                ::operator delete(e);
            }
            Drain(b);
            // Every counted reference the effect took has gone back.
            for (const FakeOwner &o : w.owners) b.push_back(o.refs);
            for (const FakeNode &n : w.nodes) b.push_back(n.refs);
        }
        Same(ctx, blobs[0], blobs[1], "WaterEffect_Slots", seed);
    }
}
