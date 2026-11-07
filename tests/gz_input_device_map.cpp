// Differential tests for nGZEvent::cInputDeviceMap (src/gz/cInputDeviceMap.cpp, standalone shape; lane LW). The original
// side builds a map with the original factory 0x1a8d95 (or its constructor 0x1a8af6 on filled memory), ours with Create
// (or the constructor hook's adapter); both run the same random script through our interface declarations (whose slots
// are the game's vtable 0xda223c, checked by InputDeviceMap_VTable), reading the original object as ours (its two maps are
// MSVC 14.40 std::maps of the same node layout).
//
// The world: operator new / sized delete (0x824632 / 0x824624) patched to malloc / free, as ours are; event bodies are
// FakeEvents (a C++ class of the body interface, logging every size query, clone and destruction, the clones numbered per
// side; sizes 16 to 32, and a 4-byte body of a vtable only); the target is a hand-made behaviour whose OnQuery (slot 4) logs the type, the second argument and what the data
// is (a FakeEvent's id and number, or the cVectorEvent's position, with its game vtable checked) and answers from a
// cycle that includes values with only a high byte set. After every step: the result, the object's words +0x14..+0x2c
// and both maps walked. The last Release's frees are compared by size.
#include "difftest.h"
#include "drawfx_core.h"
#include "free_recorder.h"
#include "code_patch.h"
#include "game/game.h"

#include "gz/cInputDeviceMap.h"

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>

using namespace nGZEvent;

namespace {

constexpr uint32_t kFactory = 0x1a8d95;
constexpr uint32_t kCtor = 0x1a8af6;
using Factory = cIInputDeviceMap *(__cdecl *)();
using Ctor = void *(__thiscall *)(void *);

void *__cdecl ImageNew(size_t size) { return malloc(size); }
void __cdecl ImageSizedDelete(void *p, size_t) { free(p); }

uint32_t gClones = 0;  // clones made on this side (their numbers)
uint32_t gQueries = 0; // OnQuery calls on this side
uint32_t gSalt = 0;    // the seed's answer offset

// An event body: its id and size, and the number of the clone it is (0 for the script's own).
class FakeEvent : public cIEventBody {
public:
    FakeEvent(uint32_t id, uint32_t size) : mId(id), mSize(size), mGen(0) {}
    FakeEvent(const FakeEvent &o) : mId(o.mId), mSize(o.mSize), mGen(0) {}
    ~FakeEvent() override { drawfx::Note("destroy", {mId, mGen}); }
    cIEventBody *Assign(const cIEventBody *) override { return Unexpected(1); }
    cIEventBody *Slot2(const cIEventBody *) override { return Unexpected(2); }
    uint32_t GetEventType() const override { return Unexpected(3), 0; }
    uint32_t GetSize() const override
    {
        drawfx::Note("size", {mId, mGen});
        return mSize;
    }
    uint32_t GetBodySize() const override { return Unexpected(5), 0; }
    cIEventBody *CloneInto(void *memory) const override
    {
        auto *copy = ::new (memory) FakeEvent(*this);
        copy->mGen = ++gClones;
        drawfx::Note("clone", {mId, mGen, copy->mGen});
        return copy;
    }

    uint32_t mId, mSize, mGen;

private:
    cIEventBody *Unexpected(uint32_t slot) const
    {
        drawfx::Note("unexpected event slot", slot);
        return nullptr;
    }
};
static_assert(sizeof(FakeEvent) == 16, "FakeEvent fits the smallest size");

// The smallest body there can be: a vtable only (4 bytes, id kTinyId; its clones are not numbered).
constexpr uint32_t kTinyId = 9;
class TinyEvent : public cIEventBody {
public:
    ~TinyEvent() override { drawfx::Note("destroy", {kTinyId, 0}); }
    cIEventBody *Assign(const cIEventBody *) override { return Unexpected(1); }
    cIEventBody *Slot2(const cIEventBody *) override { return Unexpected(2); }
    uint32_t GetEventType() const override { return Unexpected(3), 0; }
    uint32_t GetSize() const override
    {
        drawfx::Note("size", {kTinyId, 0});
        return 4;
    }
    uint32_t GetBodySize() const override { return Unexpected(5), 0; }
    cIEventBody *CloneInto(void *memory) const override
    {
        drawfx::Note("clone", {kTinyId, 0, 0});
        return ::new (memory) TinyEvent;
    }

private:
    cIEventBody *Unexpected(uint32_t slot) const
    {
        drawfx::Note("unexpected event slot", slot);
        return nullptr;
    }
};
static_assert(sizeof(TinyEvent) == 4, "TinyEvent is a vtable only");

const void *VTableOf(const void *object) { return *static_cast<const void *const *>(object); }
const void *FakeEventVTable()
{
    static const FakeEvent proto(0, 16);
    return VTableOf(&proto);
}
const void *TinyEventVTable()
{
    static const TinyEvent proto;
    return VTableOf(&proto);
}

// A body's id and clone number (0, 0 for none; ~0 for something else).
void Identify(const void *body, uint32_t &id, uint32_t &gen)
{
    id = gen = 0;
    if (!body) return;
    if (VTableOf(body) == FakeEventVTable()) {
        id = static_cast<const FakeEvent *>(body)->mId;
        gen = static_cast<const FakeEvent *>(body)->mGen;
    } else if (VTableOf(body) == TinyEventVTable()) {
        id = kTinyId;
    } else {
        id = gen = ~0u;
    }
}

// What an OnQuery's data is.
void NoteData(uint32_t type, uint32_t arg1, const void *data)
{
    if (!data) {
        drawfx::Note("query null", {type, arg1});
        return;
    }
    const void *vt = VTableOf(data);
    if (vt == FakeEventVTable() || vt == TinyEventVTable()) {
        uint32_t id, gen;
        Identify(data, id, gen);
        drawfx::Note("query event", {type, arg1, id, gen});
    } else if (vt == GameAddress<const void>(cVectorEvent::kVTable)) {
        const auto *v = static_cast<const cVectorEvent *>(data);
        drawfx::Note("query vector", {type, arg1, uint32_t(v->mX), uint32_t(v->mY)});
    } else {
        drawfx::Note("query other", {type, arg1});
    }
}

// The target behaviour: slot 4 answers, every other slot logs.
struct FakeTarget {
    void *const *vtable;
};
constexpr uint32_t kAnswers[] = {0, 1, 0x100, 0x101, 0, 0xff00, 2, 0};
uint32_t __fastcall TargetOnQuery(FakeTarget *, void *, uint32_t type, uint32_t arg1, void *data)
{
    NoteData(type, arg1, data);
    return kAnswers[(gQueries++ + gSalt) % 8];
}
uint32_t __fastcall TargetOther(FakeTarget *, void *)
{
    drawfx::Note("unexpected target slot", 0);
    return 0;
}
void *const kTargetTable[15] = {
    reinterpret_cast<void *>(&TargetOther),   reinterpret_cast<void *>(&TargetOther),
    reinterpret_cast<void *>(&TargetOther),   reinterpret_cast<void *>(&TargetOther),
    reinterpret_cast<void *>(&TargetOnQuery), reinterpret_cast<void *>(&TargetOther),
    reinterpret_cast<void *>(&TargetOther),   reinterpret_cast<void *>(&TargetOther),
    reinterpret_cast<void *>(&TargetOther),   reinterpret_cast<void *>(&TargetOther),
    reinterpret_cast<void *>(&TargetOther),   reinterpret_cast<void *>(&TargetOther),
    reinterpret_cast<void *>(&TargetOther),   reinterpret_cast<void *>(&TargetOther),
    reinterpret_cast<void *>(&TargetOther),
};

// The object's words +0x14..+0x2c (the +0x28 word whole only when both sides filled the memory alike) and both maps.
void NoteMap(const cIInputDeviceMap *face, bool filled)
{
    const auto *m = static_cast<const cInputDeviceMap *>(face);
    const auto *raw = reinterpret_cast<const uint8_t *>(m);
    auto word = [&](unsigned at) { return *reinterpret_cast<const uint32_t *>(raw + at); };
    drawfx::Note("words", {word(0x14), word(0x18), word(0x1c), word(0x20), word(0x24),
                           filled ? word(0x28) : (word(0x28) & 0xffffff), word(0x2c)});
    drawfx::Note("sizes", {uint32_t(m->mButtons.size()), uint32_t(m->mMotions.size())});
    for (const auto &[k, v] : m->mButtons) {
        uint32_t id, gen;
        Identify(v.mBody.empty() ? nullptr : v.mBody.data(), id, gen);
        drawfx::Note("button", {k.mDevice, k.mControl, k.mButton, k.mCode, v.mType, uint32_t(v.mBody.size()), id, gen});
    }
    for (const auto &[k, type] : m->mMotions)
        drawfx::Note("motion", {k.mMode, k.mPhase, k.mModifiers, std::bit_cast<uint32_t>(k.mScale), k.mCode, type});
}

constexpr float kScales[] = {1.0f, 0.5f, 2.0f, -1.5f, 0.3f, 3.75f};
constexpr int32_t kCoords[] = {-1, 0, 1, 5, 17, -40, 333, 800, 0x7fff};
constexpr uint32_t kHighs[] = {0, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x18, 0x41, 0x37, 0x62, 0x55, 0x80, 0x06};
constexpr uint32_t kWheels[] = {0, 120, 0xff88, 7, 0xfff9, 0x8000, 0x7fff, 240};

// The script: a random mix of every slot, mouse input weighted.
void RunScript(cIInputDeviceMap *map, std::mt19937 &rng, bool filled)
{
    FakeEvent events[4] = {{1, 16}, {2, 20}, {3, 24}, {4, 32}};
    TinyEvent tiny;
    const cIEventBody *bodies[5] = {&events[0], &events[1], &events[2], &events[3], &tiny};
    FakeTarget target{kTargetTable};
    auto *behaviour = reinterpret_cast<cIAbstractBehavior *>(&target);
    auto pick = [&](uint32_t n) { return uint32_t(rng() % n); };
    auto low4 = [&] { return pick(4); };
    auto coord = [&] { return kCoords[pick(9)]; };
    auto flags = [&] { return (kHighs[pick(15)] << 16) | kWheels[pick(8)]; };
    auto mode = [&] { return pick(5) == 0 ? cInputDeviceMap::kAny : pick(8); };
    auto modifiers = [&] { return pick(3) == 0 ? cInputDeviceMap::kAny : pick(3) == 0 ? pick(8) : pick(2); };
    auto type = [&] { return 0x1000 + pick(64); };

    drawfx::Note("add ref", map->AddRef());
    NoteMap(map, filled);
    const int steps = 10 + int(pick(70));
    for (int step = 0; step < steps; ++step) {
        switch (pick(16)) {
        case 0:
        case 1: {
            const uint32_t kind = pick(17);
            cInputRecord record{kind, 0, 0, 0};
            if (kind >= 7) record = {kind, uint32_t(coord()), uint32_t(coord()), flags()};
            else record = {kind, low4(), low4(), pick(3)};
            drawfx::Note("translate", {kind, map->Translate(&record, behaviour) ? 1u : 0u});
            break;
        }
        case 2:
        case 3: {
            const uint32_t d = low4(), c = low4(), b = 1 + pick(4), t = type();
            map->MapButton(d, c, b, t, bodies[pick(5)]);
            drawfx::Note("map button", {d, c, b, t});
            break;
        }
        case 4: {
            const uint32_t d = low4(), c = low4(), b = 1 + pick(4), t = type();
            const uint32_t code = pick(3) ? b + (d * 9 + c) * 8 : pick(80);
            map->MapButtonKey(cButtonKey{d, c, b, code}, t, bodies[pick(5)]);
            drawfx::Note("map button key", {d, c, b, code, t});
            break;
        }
        case 5:
        case 6: {
            const uint32_t m = mode(), p = 1 + pick(3), f = modifiers(), t = type();
            map->MapMotion(m, p, f, t);
            drawfx::Note("map motion", {m, p, f, t});
            break;
        }
        case 7: {
            const uint32_t m = mode(), p = 1 + pick(3), f = modifiers(), t = type();
            const uint32_t code = pick(3) ? m + (p + f * 4) * 9 : pick(400);
            const float s = kScales[pick(6)];
            map->MapMotionKey(cMotionKey{m, p, f, s, code}, t);
            drawfx::Note("map motion key", {m, p, f, std::bit_cast<uint32_t>(s), code, t});
            break;
        }
        case 8: {
            const uint32_t kind = 4 + pick(4), d = low4(), c = low4(), s = pick(3);
            drawfx::Note("button", {kind, map->HandleButton(behaviour, kind, d, c, s) ? 1u : 0u});
            break;
        }
        case 9:
        case 10:
        case 11:
        case 12:
        case 13: {
            const uint32_t kind = 6 + pick(10), f = flags();
            const int32_t x = coord(), y = coord();
            drawfx::Note("mouse", {kind, uint32_t(x), uint32_t(y), f, map->HandleMouse(behaviour, kind, x, y, f) ? 1u : 0u});
            break;
        }
        case 14: {
            const uint32_t iids[] = {1, cIInputDeviceMap::kIid, 0, 0x9c168263};
            void *out = reinterpret_cast<void *>(0x5a5a5a5a);
            const bool yes = map->QueryInterface(iids[pick(4)], pick(6) ? &out : nullptr);
            drawfx::Note("qi", {yes ? 1u : 0u, out == map ? 1u : drawfx::P(out)});
            break;
        }
        default:
            if (pick(2)) drawfx::Note("add ref", map->AddRef());
            else if (static_cast<cInputDeviceMap *>(map)->mRefCount > 1) drawfx::Note("release", map->Release());
            break;
        }
        NoteMap(map, filled);
    }
    // The last references: every count down to the deletion (slot 10, the destructor 0x1a8b75).
    while (static_cast<cInputDeviceMap *>(map)->mRefCount > 1) drawfx::Note("release", map->Release());
    difftest::FreeRecorder rec;
    drawfx::Note("last", map->Release());
    rec.Stop();
    size_t sizes[256];
    const size_t n = rec.SortedSizes(sizes, 256);
    drawfx::Note("frees", {uint32_t(n), rec.Overflowed() ? 1u : 0u});
    for (size_t i = 0; i < n; ++i) drawfx::Note("freed", uint32_t(sizes[i]));
}

} // namespace

// The factory 0x1a8d95 (against Create) and the constructor 0x1a8af6 (against the adapter) under the script: every slot
// of 0xda223c reached (0x1a9204 QueryInterface, 0x18fd70 AddRef, 0x1a9226 Release, 0x1a8d4e Translate, 0x1a8bd1 MapButton,
// 0x1a8c13 MapButtonKey, 0x1a8c9f MapMotion, 0x1a8c6e MapMotionKey, 0x1a8db1 HandleButton, 0x1a8ec0 HandleMouse, 0x1a8bb2
// the deleting destructor), and through them 0x1a923d SendButton, 0x1a926d SendMotionAll, 0x1a935a SendMotion, the
// destructor 0x1a8b75 and the stored event's 0x1a8ae0 (type constructor), 0x1a8a9d (copy), 0x1a8b5a (destructor) and
// 0x1a8d03 (Assign).
DIFF_TEST(InputDeviceMap_Script, 0x1a8d95)
{
    shape::CodePatch imageNew(ctx.image.At(0x824632), reinterpret_cast<void *>(&ImageNew));
    shape::CodePatch imageDelete(ctx.image.At(0x824624), reinterpret_cast<void *>(&ImageSizedDelete));
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        const bool viaCtor = seed % 3 == 2;
        auto side = [&](bool ours) {
            return drawfx::Side(
                [&] {
                    gClones = 0;
                    gQueries = 0;
                    gSalt = seed % 8;
                },
                [&] {
                    std::mt19937 rng(seed);
                    cIInputDeviceMap *map;
                    if (viaCtor) {
                        void *memory = malloc(cInputDeviceMap::kGameSize);
                        std::memset(memory, 0xcd, cInputDeviceMap::kGameSize);
                        map = ours ? static_cast<cIInputDeviceMap *>(cInputDeviceMapAdapters::Construct(memory, nullptr))
                                   : static_cast<cIInputDeviceMap *>(ctx.Original<Ctor>(kCtor)(memory));
                        drawfx::Note("built", drawfx::P(map) == drawfx::P(memory) ? 1u : 0u);
                    } else {
                        map = ours ? cInputDeviceMap::Create() : ctx.Original<Factory>(kFactory)();
                    }
                    RunScript(map, rng, viaCtor);
                });
        };
        auto a = side(false);
        auto b = side(true);
        if (!drawfx::Same(ctx, a.first, b.first, "input map", seed, a.second.empty() ? b.second : a.second)) return;
    }
}

// The game's vtable 0xda223c: the slots the script calls through our declarations are these RVAs, in this order.
DIFF_TEST(InputDeviceMap_VTable, 0x1a8af6)
{
    const uint32_t slots[] = {0x1a9204, 0x18fd70, 0x1a9226, 0x1a8d4e, 0x1a8bd1, 0x1a8c13,
                              0x1a8c9f, 0x1a8c6e, 0x1a8db1, 0x1a8ec0, 0x1a8bb2};
    void *const *table = GameAddress<void *>(cInputDeviceMap::kVTable);
    for (unsigned s = 0; s < 11; ++s) {
        ++ctx.cases;
        if (table[s] != GameAddress<void>(slots[s])) ctx.Fail("vtable 0x%x slot %u is not 0x%x", cInputDeviceMap::kVTable, s, slots[s]);
    }
    // Ours has the same eleven slots: a map built by the adapter holds our table, whose slot 10 is the deleting destructor.
    alignas(8) uint8_t buffer[cInputDeviceMap::kGameSize];
    auto *map = cInputDeviceMapAdapters::Construct(buffer, nullptr);
    ++ctx.cases;
    if (*reinterpret_cast<void **>(buffer) == GameAddress<void>(cInputDeviceMap::kVTable)) ctx.Fail("ours carries the game's vtable");
    map->~cInputDeviceMap();
}
