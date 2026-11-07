// Differential tests for cTSWantDefinition (lane IL, src/ts/cTSWantDefinition.h).
//
// WantDefinition_Read: a game-built definition (the image's operator new 0x824632, memory filled with 0xcd, the constructor
// 0x5b10e0: vtables 0xdd8548 / 0xdd85a4, so every slot runs the original) against ours (`new`, or our constructor adapter on
// the same kind of memory). Each case reads one or two random property sets (ReadProperties, slot 21; our cGZPropertySetString
// on both sides) holding random subsets of the 21 property names the class knows, mostly with the value type it reads them as,
// sometimes another (the read then fails), with values drawn from the image's value names (level, objectType, integerOperator),
// variants of them and junk. A stringSet value in range loads the table's entries 1-4 through cTSString from a fake resource
// manager (tests/ts_string_fake.h: found or not, random texts) in the group a fake persist system (cache 0x10faf6c) answers.
// Compared: the constructor's fields, every field and string after each read (the word at +0x20 masked to its low half: the
// constructor's `mov word ptr [esi + 0x20], 0` at 0x5b1131 leaves +0x22/+0x23 as the memory was, and nothing else writes
// them), slots 3-19 (pointer results as offsets), GetString (slot 12) for indices 0-4, WriteProperties (slot 20) into a new
// set read back by name and type, the persistence face (+4: QueryInterface, AddRef, GetKey, SetKey, the type / group /
// instance setters and getters, GetRefCount), QueryInterface from both faces, and Release down to destruction; plus every
// fake call (the persist system's, the resource manager's asks and the resources' Strlen / Data / Release).
//
// Covered through the vtables (game 0xdd8548 / 0xdd85a4 on the original side, ours on the other):
//   face +0 slot 0 QueryInterface 0x5b1950; slot 2 Release 0x5b2720 (and through it the destructor 0x5b12d0);
//   slot 4 GetName 0x5b18e0; slot 5 GetCategory 0x5b18d0; slot 12 GetString 0x5b1800; slot 14 GetFeedbackTree 0x5b1940;
//   slot 15 GetSimArrayTree 0x5b1930; slot 20 WriteProperties 0x5b1500; slot 21 ReadProperties 0x5b19b0;
//   face +4 slot 0 QueryInterface thunk 0x5b1999; slot 2 Release thunk 0x5b2748.
#include "difftest.h"
#include "ts_services_fixture.h"
#include "ts_string_fake.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"
#include "gz/cGZPropertySet.h"
#include "rz/cRZString.h"
#include "ts/cTSWantDefinition.h"

#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace wantdef {

using svcfx::Blob;
using svcfx::F;

uint32_t Hash(const char *s, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) h = (h ^ uint8_t(s[i])) * 16777619u;
    return h;
}
uint32_t Hash(const std::string &s) { return Hash(s.data(), s.size()); }
const char *Text(uint32_t cell) { return *GameAddress<const char *>(cell); }

constexpr uint32_t kSize = 0x1a0;
constexpr uint32_t kFirstName = 0x1017278; // the 21 property name cells, 4 apart
constexpr uint32_t kNames = 21;
constexpr uint32_t kFirstValue = 0x10172cc; // the 13 value name cells (level, objectType, integerOperator)
constexpr uint32_t kValues = 13;

enum Type : uint32_t { kU32, kS32, kBool, kStr, kTypes };
// The type ReadProperties reads each name as, in cell order.
constexpr Type kNatural[kNames] = {kU32, kS32, kS32, kBool, kStr, kStr, kStr, kStr, kStr, kU32, kU32,
                                   kU32, kStr, kStr, kStr, kBool, kBool, kStr, kStr, kS32, kU32};
constexpr uint32_t kStringSetName = 11;

struct Property {
    uint32_t name;
    Type type;
    uint32_t number;
    std::string text;
};
struct Plan {
    std::vector<std::vector<Property>> reads;
    std::map<uint32_t, std::pair<bool, std::string>> table; // instance -> found, text
    uint32_t group;
    bool nameFirst;
    uint32_t addRefs;
    uint32_t key[4];
};
Plan gPlan;

std::string RandomText(std::mt19937 &rng)
{
    switch (rng() % 8) {
    case 0: return std::string();
    case 1: {
        std::string s = Text(kFirstValue + 4 * (rng() % kValues));
        s[rng() % s.size()] ^= 0x20;
        return s;
    }
    case 2: return std::string(Text(kFirstValue + 4 * (rng() % kValues))) + char('a' + rng() % 26);
    case 4: {
        std::string s = Text(kFirstValue + 4 * (rng() % kValues));
        s.insert(rng() % (s.size() + 1), 1, '\0'); // the strings keep their length (WriteProperties copies by length)
        return s;
    }
    case 3: {
        std::string s;
        for (uint32_t i = 0, n = rng() % 40; i < n; ++i) s += char('a' + rng() % 26);
        return s;
    }
    default: return Text(kFirstValue + 4 * (rng() % kValues));
    }
}

uint32_t RandomNumber(std::mt19937 &rng, uint32_t name)
{
    if (name == kStringSetName) {
        static const uint32_t edges[] = {0, 1, 2, 0x7ffe, 0x7fff, 0x8000, 0x8001, 0xffff, 0x10001, 0x18001};
        if (rng() % 3) return 1 + rng() % 0x7fff;
        if (rng() % 2) return edges[rng() % std::size(edges)];
        return uint32_t(rng());
    }
    switch (rng() % 5) {
    case 0: return 0;
    case 1: return uint32_t(-int32_t(rng() % 100));
    case 2: return rng() % 3;
    default: return uint32_t(rng());
    }
}

void RandomPlan(std::mt19937 &rng)
{
    Plan &p = gPlan;
    p.reads.clear();
    p.table.clear();
    for (uint32_t r = 0, n = rng() % 3 ? 1 : 2; r < n; ++r) {
        std::vector<Property> props;
        const uint32_t density = 30 + rng() % 70;
        for (uint32_t name = 0; name < kNames; ++name) {
            if (rng() % 100 >= density) continue;
            Property q{name, rng() % 5 ? kNatural[name] : Type(rng() % kTypes), 0, {}};
            q.number = RandomNumber(rng, name);
            if (q.type == kBool) q.number = rng() % 6 ? q.number % 2 : uint8_t(rng());
            if (q.type == kStr) q.text = RandomText(rng);
            props.push_back(q);
        }
        // the order the set is filled in (the set's own order is its business; both sides read the same set)
        for (size_t i = props.size(); i > 1; --i) std::swap(props[i - 1], props[rng() % i]);
        p.reads.push_back(props);
    }
    for (const auto &props : p.reads)
        for (const Property &q : props)
            if (q.name == kStringSetName)
                for (uint32_t entry = 1; entry <= 4; ++entry) {
                    std::string text = rng() % 5 ? RandomText(rng) : std::string();
                    if (rng() % 8 == 0) text += "\xe2\x80\xa2 item";
                    p.table[(q.number & 0xffff) << 16 | entry] = {rng() % 4 != 0, text};
                }
    p.group = uint32_t(rng());
    p.nameFirst = rng() % 4 == 0;
    p.addRefs = rng() % 4;
    for (uint32_t &k : p.key) k = uint32_t(rng());
}

std::vector<cRZString *> gHeld;

void Fill(cGZPropertySetString *set, const std::vector<Property> &props)
{
    for (const Property &q : props) {
        const GZServiceString key(Text(kFirstName + 4 * q.name));
        switch (q.type) {
        case kU32: {
            gz::AnyUint32 v(q.number);
            set->SetProperty(&key, &v);
            break;
        }
        case kS32: {
            AnySint32 v{GameAddress<void>(gzany::kSint32VTable), int32_t(q.number)};
            set->SetProperty(&key, &v);
            break;
        }
        case kBool: {
            gz::AnyBoolean v(uint8_t(q.number));
            set->SetProperty(&key, &v);
            break;
        }
        default: {
            // A string the test keeps a reference to: its count after the read shows whether the read let go of it.
            auto *text = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
            text->Construct();
            text->mString = q.text;
            text->AddRef();
            gHeld.push_back(text);
            AnyString v{GameAddress<void>(gzany::kStringVTable), text->AsInterface()};
            text->AddRef();
            set->SetProperty(&key, &v);
            v.Destruct();
            break;
        }
        }
    }
}

template <class T> T &At(void *object, uint32_t off) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(object) + off); }
uint32_t Off(const void *object, const void *p)
{
    return p ? uint32_t(static_cast<const uint8_t *>(p) - static_cast<const uint8_t *>(object)) : 0xffffffffu;
}
template <int Slot, class R = uint32_t, class... A> R Call(void *object, A... args)
{
    return VirtualCall<R(__thiscall *)(void *, A...), Slot>(object, args...);
}

void DigestString(Blob &d, const cRZString &s)
{
    d.push_back(s.mVTable == GameAddress<void>(cRZString::kVTable));
    d.push_back(Hash(s.mString));
    d.push_back(uint32_t(s.mString.size()));
    d.push_back(s.mRefCount);
}

void DigestFields(Blob &d, void *object)
{
    for (uint32_t off = 8; off < 0x50; off += 4) d.push_back(At<uint32_t>(object, off) & (off == 0x20 ? 0xffffu : ~0u));
    for (uint32_t off = 0x150; off < 0x160; off += 4) d.push_back(At<uint32_t>(object, off));
    for (uint32_t i = 0; i < 5; ++i) DigestString(d, At<cRZString>(object, 0x50 + 0x20 * i));
    for (uint32_t off : {0xf0u, 0x110u, 0x130u, 0x160u, 0x180u}) DigestString(d, At<cRZString>(object, off));
}

void DigestGetters(Blob &d, void *object)
{
    d.push_back(Call<3>(object));
    d.push_back(Off(object, Call<4, void *>(object)));
    d.push_back(Off(object, Call<5, void *>(object)));
    d.push_back(Call<6>(object));
    d.push_back(Call<7>(object));
    d.push_back(Call<8>(object) & 0xff);
    d.push_back(Call<9>(object));
    d.push_back(Off(object, Call<10, void *>(object)));
    d.push_back(Off(object, Call<11, void *>(object)));
    for (uint32_t index = 0; index < 5; ++index) {
        alignas(4) uint8_t bytes[sizeof(cRZString)];
        auto *out = reinterpret_cast<cRZString *>(bytes);
        out->Construct();
        Call<12, void>(object, index, static_cast<void *>(out));
        DigestString(d, *out);
        out->Destruct();
    }
    d.push_back(Off(object, Call<13, void *>(object)));
    d.push_back(Off(object, Call<14, void *>(object)));
    d.push_back(Off(object, Call<15, void *>(object)));
    for (uint32_t slot = 16; slot < 20; ++slot) {
        switch (slot) {
        case 16: d.push_back(Call<16>(object)); break;
        case 17: d.push_back(Call<17>(object)); break;
        case 18: d.push_back(Call<18>(object)); break;
        default: d.push_back(Call<19>(object)); break;
        }
    }
}

void DigestWrite(Blob &d, void *object)
{
    auto *set = new cGZPropertySetString;
    Call<20, void>(object, static_cast<cIGZPropertySetString *>(set));
    d.push_back(set->GetCount());
    for (uint32_t name = 0; name < kNames; ++name) {
        const GZServiceString key(Text(kFirstName + 4 * name));
        gz::AnyUint32 u(0x5555);
        AnySint32 s{GameAddress<void>(gzany::kSint32VTable), 0x6666};
        gz::AnyBoolean b(0x77);
        AnyString t{GameAddress<void>(gzany::kStringVTable), nullptr};
        d.push_back(set->GetPropertyValue(&key, &u));
        d.push_back(u.mValue);
        d.push_back(set->GetPropertyValue(&key, &s));
        d.push_back(uint32_t(s.mValue));
        d.push_back(set->GetPropertyValue(&key, &b));
        d.push_back(b.mValue);
        d.push_back(set->GetPropertyValue(&key, &t));
        if (t.mString) {
            const auto *text = reinterpret_cast<const cRZString *>(t.mString);
            d.push_back(Off(object, text) < kSize); // a copy, not the definition's own string
            DigestString(d, *text);
            Call<2>(t.mString);
        }
    }
    delete static_cast<cIGZPropertySetString *>(set);
}

void DigestPersistFace(Blob &d, void *object, std::mt19937 &rng)
{
    void *face = static_cast<uint8_t *>(object) + 4;
    for (uint32_t iid : {1u, cTSWantDefinition::kIid, cTSWantDefinition::kPersistResourceIid, 2u, uint32_t(rng())}) {
        for (void *self : {object, face}) {
            void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
            d.push_back(Call<0, uint32_t>(self, iid, &out) & 0xff);
            d.push_back(out == reinterpret_cast<void *>(uintptr_t(0x1234)) ? 0x1234u : Off(object, out));
            d.push_back(At<uint32_t>(object, 8));
        }
    }
    d.push_back(Call<1>(face));
    d.push_back(Call<11>(face));
    cGZPersistResourceKey key{};
    d.push_back(Off(&key, Call<3, cGZPersistResourceKey *>(face, &key)));
    d.insert(d.end(), {key.type, key.group, key.instance, key.resource});
    const cGZPersistResourceKey next{gPlan.key[0], gPlan.key[1], gPlan.key[2], gPlan.key[3]};
    d.push_back(Call<4, uint32_t>(face, &next) & 0xff);
    d.push_back(Call<5>(face));
    d.push_back(Call<6>(face));
    d.push_back(Call<7>(face));
    d.push_back(Call<8, uint32_t>(face, uint32_t(rng())) & 0xff);
    d.push_back(Call<9, uint32_t>(face, uint32_t(rng())) & 0xff);
    d.push_back(Call<10, uint32_t>(face, uint32_t(rng())) & 0xff);
    for (uint32_t off = 0xc; off < 0x1c; off += 4) d.push_back(At<uint32_t>(object, off));
    d.push_back(Call<2>(face));
}

// One case on one definition (`object`, freshly constructed), ending with its destruction.
Blob Run(void *object, std::mt19937 &rng)
{
    svcfx::ResetAll();
    F(0).Set(10, "p", gPlan.group);
    Blob d;
    DigestFields(d, object);
    if (gPlan.nameFirst) d.push_back(Off(object, Call<4, void *>(object)));
    for (const auto &props : gPlan.reads) {
        auto *set = new cGZPropertySetString;
        Fill(set, props);
        Call<21, void>(object, static_cast<cIGZPropertySetString *>(set));
        delete static_cast<cIGZPropertySetString *>(set);
        DigestFields(d, object);
        for (cRZString *text : gHeld) {
            d.push_back(text->mRefCount);
            text->Release();
        }
        gHeld.clear();
    }
    DigestGetters(d, object);
    DigestFields(d, object);
    DigestWrite(d, object);
    DigestPersistFace(d, object, rng);
    for (uint32_t i = 0; i < gPlan.addRefs; ++i) d.push_back(Call<1>(object));
    // Release down to destruction (a count of 1 or below destroys).
    for (int32_t refs = At<int32_t>(object, 8);; --refs) {
        d.push_back(Call<2>(object));
        if (refs <= 1) break;
    }
    d.insert(d.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    svcfx::CaptureFakes(d);
    for (const tsfake::Ask &a : tsfake::gAsks)
        d.insert(d.end(), {a.type, a.group, a.instance, a.keyWord3, a.interfaceId, uint32_t(a.loadFlags), a.reserved});
    d.insert(d.end(), tsfake::gEvents.begin(), tsfake::gEvents.end());
    d.push_back(uint32_t(tsfake::gResourcesCreated));
    d.push_back(uint32_t(tsfake::gResourcesReleased));
    return d;
}

bool Answer(const tsfake::Ask &ask, std::string &text)
{
    const auto it = gPlan.table.find(ask.instance);
    if (it == gPlan.table.end()) return false;
    text = it->second.second;
    return it->second.first;
}

} // namespace wantdef

using namespace wantdef;

DIFF_TEST(WantDefinition_Read, 0x5b19b0)
{
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    globals.Set(svcfx::kPersistCell, &F(0));
    tsfake::ManagerFixture manager(ctx);
    tsfake::gAnswerer = &Answer;
    const uint32_t seeds = std::getenv("WANTDEF_SEEDS") ? uint32_t(std::atoi(std::getenv("WANTDEF_SEEDS"))) : 2000;
    for (uint32_t seed = 1; seed <= seeds; ++seed) {
        std::mt19937 plan(seed);
        RandomPlan(plan);
        Blob r[2];
        for (int side = 0; side < 2; ++side) {
            manager.Reset(tsfake::Mode::Present);
            std::mt19937 rng(seed * 7 + 1);
            void *object;
            if (side == 1 && seed % 2) {
                object = new cTSWantDefinition;
            } else {
                void *memory = ctx.Original<void *(__cdecl *)(size_t)>(0x824632)(kSize);
                std::memset(memory, 0xcd, kSize);
                object = side == 0 ? ctx.Original<void *(__thiscall *)(void *)>(0x5b10e0)(memory)
                                   : cTSWantDefinitionAdapters::Construct(memory, nullptr);
            }
            r[side] = Run(object, rng);
        }
        if (!svcfx::Same(ctx, r[0], r[1], "WantDefinition", seed)) break;
    }
}
