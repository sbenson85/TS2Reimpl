// Differential tests for nTSSG::cTSSGSystem's constructor 0x246110 (ours: cTSSGSystemAdapters::Construct, `::new (memory)
// cTSSGSystem`) and destructor 0x246f00 (ours: cTSSGSystem::Destruct), src/sg/cTSSGSystemLifetime.cpp. Hook shape: both
// sides run on object images of their own, built from the same seed.
//
// Constructor: both buffers start as the same random bytes; the two game globals it copies (the default camera position
// 0x10cc860 and the forward axis 0xff78f4, three floats each) hold random floats. Compared: the result, every byte of the
// 0x3b8 except the message-id set's head pointer (+0x14, a heap address of each side's own), and the set's ids in order.
//
// Destructor: each side's object is built by the original constructor from the seed's random bytes, then every reference
// field gets a recording fake (tests/ts_services_fixture.h), null, or (the fields held through a virtual base) a block
// whose virtual-base table reaches a fake; the vectors get elements (null or not), the set extra ids, and the three
// vtable words junk. EndCinematic (0x255250) is stood in on both sides (it logs the vtable words it sees and its
// argument). The entry handles' destructor 0xae82b0 runs for real on both sides (the image's and ours): the texture
// manager is a fake in 0xae8750's cache 0x10f8f94 whose slot 19 (ReleaseId) logs. Compared: the event log (each release
// with its fake and order), every fake's count, the object's bytes afterwards (but the set's freed head pointer), and the
// number and sizes of the heap blocks freed (FreeRecorder).
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "sg/cTSSGSystem.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

namespace {

using namespace svcfx;
using nTSSG::cTSSGSystem;

constexpr uint32_t kSize = cTSSGSystem::kSize;
constexpr uint32_t kObjectTag = 0x5e000000;
constexpr uint32_t kBlockTag = 0x6d000000;
constexpr uint32_t kSetOffset = 0x14;
// The object's words as Bytes logs them (svcfx::DumpWords): 'p' pointer, 'd' data, 'x' at kSetOffset (the set's head
// pointer, not compared).
constexpr char kObjectKinds[] =
    "p-ppdxdd dd------ dd----pp pppppppp pppppppp pppppppp pppppppp pppppppp "
    "pppppppp ppp----- -----d-- -d-d---- --dpdd-d dddddddd d------- -------- "
    "-------- -------- -------- -------- -------- ------dd d-ppdddd dddddddd "
    "ddddddd- ddd-dddd ddd-ddpd dddddddd dddddddd d-d-pp";
enum : uint32_t { evStand = 0x90 };

// Overwrites `size` bytes of the image at `rva` (data or read-only data) for the scope.
struct ImagePoke {
    uint8_t *at;
    uint8_t saved[16];
    size_t size;
    ImagePoke(uint32_t rva, const void *bytes, size_t n) : at(GameAddress<uint8_t>(rva)), size(n)
    {
        DWORD old;
        VirtualProtect(at, size, PAGE_READWRITE, &old);
        std::memcpy(saved, at, size);
        std::memcpy(at, bytes, size);
    }
    ~ImagePoke() { std::memcpy(at, saved, size); }
};

// The object's bytes as words, but the set's head pointer.
Blob Bytes(const uint8_t *object)
{
    Blob b;
    svcfx::DumpWords(b, object, kSize, kObjectKinds);
    return b;
}

// ---- constructor ----

} // namespace

// 0x246110 constructor.
DIFF_TEST(sg_tssg_system_construct, 0x246110)
{
    auto original = ctx.Original<void *(__thiscall *)(void *)>(0x246110);
    for (uint32_t seed = 0; seed < 30; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x246110u);
        float camera[3], axis[3];
        for (float &f : camera) f = float(int32_t(gen() % 20001) - 10000) / 7.0f;
        for (float &f : axis) f = float(int32_t(gen() % 2001) - 1000) / 1000.0f;
        ImagePoke pokeCamera(0x10cc860, camera, sizeof(camera));
        ImagePoke pokeAxis(0xff78f4, axis, sizeof(axis));
        std::vector<uint8_t> start(kSize);
        for (uint8_t &byte : start) byte = uint8_t(gen());

        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            alignas(8) uint8_t buffer[kSize];
            std::memcpy(buffer, start.data(), kSize);
            ResetAll();
            AddRange(buffer, kSize, kObjectTag);
            void *result = side == 0 ? original(buffer) : nTSSG::cTSSGSystemAdapters::Construct(buffer, nullptr);
            Blob &b = blobs[side];
            b.push_back(Norm(P(result)));
            Blob bytes = Bytes(buffer);
            b.insert(b.end(), bytes.begin(), bytes.end());
            auto *system = reinterpret_cast<cTSSGSystem *>(buffer);
            b.push_back(uint32_t(system->mMessageIds.size()));
            b.insert(b.end(), system->mMessageIds.begin(), system->mMessageIds.end());
            std::destroy_at(&system->mMessageIds);
        }
        Same(ctx, blobs[0], blobs[1], "construct", seed);
    }
}

namespace {

// ---- destructor ----

// A block standing for an object built with virtual inheritance: its virtual-base tables at +4 and +8, entry 1 of each
// leading to a fake (a different one for each table).
struct VBlock {
    uint32_t head;
    const int32_t *table4;
    const int32_t *table8;
    int32_t entries4[2];
    int32_t entries8[2];
};
VBlock gBlocks[6];

void SetBlocks()
{
    for (unsigned i = 0; i < 6; ++i) {
        VBlock &b = gBlocks[i];
        const unsigned fake = 18 + i;
        b.head = 0xb10c0000u + i;
        b.entries4[0] = b.entries8[0] = 0;
        b.entries4[1] = int32_t(P(&F(fake)) - P(&b.table4));
        b.entries8[1] = int32_t(P(&F(fake - 6)) - P(&b.table8)); // another fake: the table used shows
        b.table4 = b.entries4;
        b.table8 = b.entries8;
    }
    AddRange(gBlocks, sizeof(gBlocks), kBlockTag);
}

void __fastcall StandEndCinematic(void *self, void *, uint32_t abort)
{
    const auto *words = static_cast<const uint32_t *>(self);
    Ev(evStand), Ev(0x255250), Ev(Norm(P(self))), Ev(abort & 0xff), Ev(Norm(words[0])), Ev(Norm(words[2])), Ev(Norm(words[3]));
}
// The texture manager the entry handles release their ids to (ids 400: past the fixture's).
Obj gTextureManager;

// Plain references (released through slot 2): 0x58..0xa8, 0xb4..0xbc, the overlays 0xc4..0xe4, 0xe8..0x108, 0x110..0x11c,
// 0x2c8, 0x2cc, 0x358, 0x3b0, 0x3b4. Held through a virtual base: +4 at 0xac, 0xb0, 0xc0, 0x120, 0x124, 0x128; +8 at 0x10c.
std::vector<uint32_t> PlainOffsets()
{
    std::vector<uint32_t> out;
    for (uint32_t o = 0x58; o <= 0xa8; o += 4) out.push_back(o);
    for (uint32_t o = 0xb4; o <= 0x108; o += 4)
        if (o != 0xc0) out.push_back(o);
    for (uint32_t o = 0x110; o <= 0x11c; o += 4) out.push_back(o);
    for (uint32_t o : {0x2c8u, 0x2ccu, 0x358u, 0x3b0u, 0x3b4u}) out.push_back(o);
    return out;
}
constexpr uint32_t kVirtualOffsets[] = {0xac, 0xb0, 0xc0, 0x120, 0x124, 0x128, 0x10c};

void Build(uint32_t seed, uint8_t *buffer, void *(__thiscall *construct)(void *))
{
    std::mt19937 gen(seed * 104729u + 0x246f00u);
    for (uint32_t i = 0; i < kSize; ++i) buffer[i] = uint8_t(gen());
    construct(buffer);
    auto *s = reinterpret_cast<cTSSGSystem *>(buffer);
    auto put = [&](uint32_t offset, const void *value) { std::memcpy(buffer + offset, &value, 4); };
    // References: null in about one case of four (always in seed 0, never in seed 1).
    auto present = [&] { return seed == 1 || (seed != 0 && gen() % 4 != 0); };
    for (uint32_t offset : PlainOffsets()) put(offset, present() ? &F(gen() % 18) : nullptr);
    for (uint32_t offset : kVirtualOffsets) put(offset, present() ? &gBlocks[gen() % 6] : nullptr);
    for (uint32_t offset : {0u, 8u, 0xcu}) put(offset, reinterpret_cast<void *>(uintptr_t(gen())));
    // Vectors: 0..4 elements (none in seed 0, four in seed 1).
    auto count = [&] { return seed == 0 ? 0u : seed == 1 ? 4u : unsigned(gen() % 5); };
    s->mHeldObjects.resize(count());
    for (void *&p : s->mHeldObjects) p = gen() % 4 ? &F(gen() % 18) : nullptr;
    for (auto *actors : {&s->mCinematicActors, &s->mCinematicExtras}) {
        actors->resize(count());
        for (auto &a : *actors) {
            a.mObject.mObject = gen() % 4 ? &gBlocks[gen() % 6] : nullptr;
            a.mValue = gen();
            a.mRestore = uint8_t(gen());
        }
    }
    s->mCinematicRefs.resize(count());
    for (auto &r : s->mCinematicRefs) r.mObject = gen() % 4 ? &gBlocks[gen() % 6] : nullptr;
    s->mEntryHandles.resize(count());
    for (auto &h : s->mEntryHandles) h.mValue = reinterpret_cast<void *>(uintptr_t(gen() % 4 ? gen() | 1 : 0xffffffffu));
    for (unsigned i = 0, n = gen() % 4; i < n; ++i) s->mMessageIds.insert(gen());
}

} // namespace

// 0x246f00 destructor.
DIFF_TEST(sg_tssg_system_destruct, 0x246f00)
{
    shape::StandIn endCinematic(0x255250, &cTSSGSystem::EndCinematic, &StandEndCinematic);
    Globals globals;
    globals.Set(0x10f8f94, &gTextureManager); // the texture manager's cache (0xae8750)
    auto construct = ctx.Original<void *(__thiscall *)(void *)>(0x246110);
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x246f00);
    for (uint32_t seed = 0; seed < 60; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            alignas(8) uint8_t buffer[kSize];
            ResetAll();
            SetBlocks();
            gTextureManager.Reset(400);
            gTextureManager.Set(19, "d"); // ReleaseId
            AddRange(buffer, kSize, kObjectTag);
            Build(seed, buffer, construct);
            gEv.clear();
            // The event log grows under NotCounted, but FreeRecorder still sees the old buffers it frees: give it room
            // first, or a fresh process records the log's growth as the original's frees (side 0 grows it first).
            gEv.reserve(1024);
            difftest::FreeRecorder rec;
            if (side == 0) original(buffer);
            else reinterpret_cast<cTSSGSystem *>(buffer)->Destruct();
            rec.Stop();
            Blob &b = blobs[side];
            b = gEv;
            CaptureFakes(b);
            Blob bytes = Bytes(buffer);
            b.insert(b.end(), bytes.begin(), bytes.end());
            size_t sizes[64];
            const size_t freed = rec.SortedSizes(sizes, 64);
            b.push_back(uint32_t(freed));
            for (size_t i = 0; i < freed && i < 64; ++i) b.push_back(uint32_t(sizes[i]));
        }
        Same(ctx, blobs[0], blobs[1], "destruct", seed);
    }
}
