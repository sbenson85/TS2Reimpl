// Differential test for nTSSG::cLightingManager::ReadRoomSettings 0x2e3710 (lane 261110p, scenegraph LG3d;
// src/sg/cLightingManagerSettings.cpp). Hook shape: the original runs on the same object image as ours.
//
// The world (one draw for both sides): the manager is 0x548 random bytes (all compared afterwards), the parser a fake
// whose slots 20 / 21 log the section's name, the framework (the service object's stand-in 0x14323f: present or null)
// whose slot 5 hands out the settings object (or writes nothing), and the settings object whose slot 3 logs the key
// (text, size, whether it lives on the heap) and the Any given (its vtable by the float type's address, its float's
// bits) and whose slot 2 counts the releases. The three property readers 0xaf7190 (float), 0xaf7210 (int) and 0xaf7110
// (bool) are the fakes of this file on both sides (code patches on the image: ours calls them through
// OriginalFunction): each answers from a table keyed by the name's text and the number of earlier reads of it, and logs
// the reader kind, the name, the parser, the member's offset in the manager and the byte `*found` holds on entry (what
// the earlier reads' flags left). Answers: not found, found with a value (floats with NaNs and negatives, ints with the
// top bit set, bools 0, 1, 2, 0xff and others) and the flag byte 0, 1, 2, 0x80, 0xff written or left alone, a result
// with garbage above al, a nonzero result with nothing written (the preset values show), and a zero result with a value
// and flag written. The reader asked for the cutoff answers "not found" when no settings object was handed out (the
// original calls through it: ORIGINAL-BUG).
//
// The buffers the fakes fill are sized from the callees' stores: the readers 0xaf7190 / 0xaf7210 store 4 bytes (the
// float or int member, the stack float `cutoff`), 0xaf7110 one byte (the bool member), all three store at most one byte
// to `*found` (the caller's byte local), and the framework's slot 5 stores one pointer (4 bytes) to its output; the
// fakes write exactly those (tests/stack_out.h's FillOut would add nothing: no slot there is wider than the store).
//
// Compared: the result, the log (reads, sections, framework and settings calls in order), every word of the manager,
// the settings object's releases, the key's heap block freed or not, and the heap blocks and bytes each side
// allocated.
#include "code_patch.h"
#include "difftest.h"
#include "sg_lighting_common.h"

#include "gz/cGZAny.h"
#include "gz/cGZPropertySet.h"
#include "gz/cIGZPropertySet.h" // GZServiceString
#include "rz/Singleton_20ddfb.h"
#include "sg/SGUtilities.h"
#include "sg/cLightingManager.h"

#include <cstdint>
#include <cstring>
#include <random>

namespace roomfx {

constexpr uint32_t kManagerSize = 0x548;
alignas(16) uint8_t gManager[kManagerSize];

struct Obj {
    void **vt;
};
void *gParserVT[24], *gFrameworkVT[8], *gSettingsVT[8];
Obj gParser{gParserVT}, gFramework{gFrameworkVT}, gSettings{gSettingsVT};

uint32_t gDraw;
bool gServiceNull;  // the service object is not there
bool gHandOut;      // slot 5 writes the settings object to its output
bool gHanded;       // ... and did
uint32_t gReleases;
uint32_t gSlot5Result, gSetResult;
// Reads of each name so far: a fixed table, so the fakes never touch the heap the two sides are compared on.
struct Seen {
    uint32_t hash, count;
} gSeen[128];
uint32_t gSeenUsed;
uint32_t Occurrence(uint32_t hash)
{
    for (uint32_t i = 0; i < gSeenUsed; ++i)
        if (gSeen[i].hash == hash) return gSeen[i].count++;
    gSeen[gSeenUsed++] = Seen{hash, 1};
    return 0;
}

enum Kind : uint32_t { kFloat = 1, kInt = 2, kBool = 3 };

struct Plan {
    uint32_t ret = 0;      // the result, all 32 bits
    bool write = false;    // the value is stored
    bool flag = false;     // `*found` is stored
    uint8_t flagValue = 0;
    uint32_t value = 0;
};

Plan Draw(const char *name, Kind kind)
{
    const uint32_t h = Hash(name);
    const uint32_t n = Occurrence(h);
    std::mt19937 gen(Mix2(Mix2(gDraw, h), n));
    Plan plan;
    const uint32_t mode = gen() % 8;
    const bool nonzero = mode >= 2 && mode != 6;
    plan.ret = nonzero ? ((1 + gen() % 255) | (gen() % 3 ? 0 : gen() & 0xffffff00u)) : (gen() % 3 ? 0 : gen() & 0xffffff00u);
    plan.write = mode == 2 || mode == 3 || mode == 4 || mode == 6 || mode == 7;
    plan.flag = mode == 2 || mode == 3 || mode == 4 || mode == 6;
    static const uint8_t kFlags[] = {0, 1, 2, 0x80, 0xff, 1, 0};
    plan.flagValue = kFlags[gen() % std::size(kFlags)];
    switch (kind) {
    case kFloat: plan.value = U(Wide(gen, 1000.0f)); break;
    case kInt: plan.value = gen() % 3 ? uint32_t(gen()) | 0x80000000u * (gen() % 2) : gen() % 40; break;
    case kBool: {
        static const uint8_t kBytes[] = {0, 1, 2, 0xff, 0x80, 0x7f};
        plan.value = gen() % 4 ? kBytes[gen() % std::size(kBytes)] : uint8_t(gen());
        break;
    }
    }
    return plan;
}

uint32_t Offset(const void *member)
{
    const uintptr_t at = reinterpret_cast<uintptr_t>(member), base = reinterpret_cast<uintptr_t>(gManager);
    return at >= base && at < base + kManagerSize ? uint32_t(at - base) : 0xffffffffu;
}

uint32_t Read(Kind kind, void *parser, const char *name, void *member, uint8_t *found)
{
    L(0xa000 + kind), L(Hash(name)), L(parser == &gParser), L(Offset(member)), L(found ? *found : 0xff00);
    Plan plan = Draw(name, kind);
    if (!std::strcmp(name, "lightRangeCutoff") && !gHanded) plan = Plan{};
    if (plan.write) {
        if (kind == kBool) std::memcpy(member, &plan.value, 1);
        else std::memcpy(member, &plan.value, 4);
    }
    if (plan.flag && found) *found = plan.flagValue;
    return plan.ret;
}

uint32_t __cdecl ReadFloat(void *parser, const char *name, float *member, uint8_t *found)
{
    return Read(kFloat, parser, name, member, found);
}
uint32_t __cdecl ReadInt(void *parser, const char *name, int32_t *member, uint8_t *found)
{
    return Read(kInt, parser, name, member, found);
}
uint32_t __cdecl ReadBool(void *parser, const char *name, uint8_t *member, uint8_t *found)
{
    return Read(kBool, parser, name, member, found);
}

uint32_t __fastcall Enter(Obj *self, void *, const char *name)
{
    L(0x5ec0), L(self == &gParser), L(Hash(name));
    return gDraw;
}
uint32_t __fastcall Leave(Obj *self, void *)
{
    L(0x5ec1), L(self == &gParser);
    return gDraw + 1;
}

uint32_t __fastcall FrameworkSlot5(Obj *self, void *, uint32_t cls, uint32_t iid, void **out)
{
    L(0xf500), L(self == &gFramework), L(cls), L(iid);
    if (gHandOut) *out = &gSettings, gHanded = true;
    return gSlot5Result;
}

uint32_t __fastcall SettingsRelease(Obj *self, void *)
{
    L(0x5e02), L(self == &gSettings);
    return ++gReleases;
}
uint32_t __fastcall SettingsSet(Obj *self, void *, const GZServiceString *key, const void *value)
{
    L(0x5e03), L(self == &gSettings), L(Hash(key->c_str())), L(uint32_t(key->size()));
    const bool heap = key->capacity() > 15;
    L(heap);
    if (heap) svcfx::gAllocated.push_back(const_cast<char *>(key->c_str()));
    uint32_t type;
    float number;
    std::memcpy(&type, value, 4), std::memcpy(&number, static_cast<const uint8_t *>(value) + 4, 4);
    L(type == P(GameAddress<const void>(gzany::kFloat32VTable))), L(U(number));
    return gSetResult;
}

void *StandService()
{
    L(0x9e00);
    return gServiceNull ? nullptr : &gFramework;
}

void DrawWorld(uint32_t draw)
{
    std::mt19937 gen(draw * 2654435761u + 0x2e3710u);
    gDraw = draw;
    for (auto &b : gManager) b = uint8_t(gen());
    gServiceNull = gen() % 8 == 0;
    gHandOut = gen() % 8 != 0;
    gHanded = false;
    gReleases = 0;
    gSlot5Result = gen(), gSetResult = gen();
    gSeenUsed = 0;
}

Blob Snapshot(uint32_t result, long long blocks, long long bytes)
{
    Blob o = gLog;
    o.push_back(0xeeee), o.push_back(result);
    for (uint32_t i = 0; i < kManagerSize; i += 4) o.push_back(Word<uint32_t>(gManager, i));
    o.push_back(gReleases), o.push_back(gHanded);
    svcfx::CaptureAllocated(o);
    o.push_back(uint32_t(blocks)), o.push_back(uint32_t(bytes));
    return o;
}

struct Scope {
    svcfx::AllocatorScope allocator;
    shape::StandIn readBool{0xaf7110u, &nGZSceneGraph::ReadBoolProperty, &ReadBool};
    shape::StandIn readFloat{0xaf7190u, &nGZSceneGraph::ReadFloatProperty, &ReadFloat};
    shape::StandIn readInt{0xaf7210u, &nGZSceneGraph::ReadIntProperty, &ReadInt};
    shape::StandIn service{0x14323fu, &GetServiceObject, &StandService};
    Scope()
    {
        gParserVT[20] = reinterpret_cast<void *>(&Enter);
        gParserVT[21] = reinterpret_cast<void *>(&Leave);
        gFrameworkVT[5] = reinterpret_cast<void *>(&FrameworkSlot5);
        gSettingsVT[2] = reinterpret_cast<void *>(&SettingsRelease);
        gSettingsVT[3] = reinterpret_cast<void *>(&SettingsSet);
        gLog.reserve(1 << 12);
        svcfx::gAllocated.reserve(8); // SettingsSet's push_back must not grow on one side only
    }
};

} // namespace roomfx

// The three prelude floats, the three sections of twelve (the int among them), the 24 tail reads (bools and floats),
// the 0x1e4 copy when its read says no, the cutoff read and what it hands to the settings object.
DIFF_TEST(sg_lighting_settings_read, 0x2e3710)
{
    using namespace roomfx;
    auto original = ctx.Original<void(__thiscall *)(void *, void *)>(0x2e3710);
    Scope scope;
    SidesAtBothPrecisions(ctx, "read room settings", 3000, true, [&](uint32_t draw, int side) {
        DrawWorld(draw);
        svcfx::gAllocated.clear();
        gLog.clear();
        ctx.Mark(side ? "ours" : "original");
        difftest::AllocationCounter heap;
        if (side == 0) original(gManager, &gParser);
        else reinterpret_cast<nTSSG::cLightingManager *>(gManager)->ReadRoomSettings(&gParser);
        heap.Stop();
        return Snapshot(0, heap.Blocks(), heap.Bytes());
    });
}
