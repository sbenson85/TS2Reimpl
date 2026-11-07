// Differential test for cTSSaveGameInfo::Describe 0xcd8140 (src/ts/cTSSaveGameInfo.cpp, lane DY).
//
// The info's property set is a fake (svcfx F(1): GetFirst / GetNext over a list of keys and values; GetFirst may fail);
// values are real Anys with the game's vtables (AnyUint32, AnyString over a real cRZString or over a logging fake
// cIGZString whose Data and Strlen disagree, and other types, which print the placeholder). Keys: the indices 0 / 1 / 2
// (2's label is never used: ORIGINAL-BUG in src), other numbers, text atoi reads as 0, keys with NULs. The output is
// a real cRZString holding earlier text. Compared: the output's text, every set call, and the fake string's calls in
// order.
#include "difftest.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/cGZAny.h"
#include "gz/cGZPropertySet.h"
#include "rz/cRZString.h"
#include "ts/cTSSaveGameInfo.h"

#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{1, "ddd"}};

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;

struct Property {
    std::string key;
    void *value;
};
std::vector<Property> gProperties;

uint32_t SetGetFirst(svcfx::Obj *self, uint32_t *args)
{
    self->value[0] = 0;
    if (gProperties.empty() || self->value[1]) return self->value[1] & 0xff00; // failing: 0 or 0x100
    reinterpret_cast<GZServiceString *>(uintptr_t(args[0]))->assign(gProperties[0].key.data(), gProperties[0].key.size());
    *reinterpret_cast<void **>(uintptr_t(args[1])) = gProperties[0].value;
    self->value[0] = 1;
    return 1;
}
uint32_t SetGetNext(svcfx::Obj *self, uint32_t *args)
{
    const uint32_t i = self->value[0];
    if (i >= gProperties.size()) return self->value[2];
    reinterpret_cast<GZServiceString *>(uintptr_t(args[0]))->assign(gProperties[i].key.data(), gProperties[i].key.size());
    *reinterpret_cast<void **>(uintptr_t(args[1])) = gProperties[i].value;
    self->value[0] = i + 1;
    return 1;
}

// A cIGZString whose Data (slot 6) and Strlen (slot 7) are logged; the length may differ from the text's.
struct FakeString {
    void **vt;
    const char *text;
    uint32_t length;
};
const char *__fastcall FakeData(FakeString *self, void *)
{
    Ev(0x66);
    return self->text;
}
uint32_t __fastcall FakeStrlen(FakeString *self, void *)
{
    Ev(0x67);
    return self->length;
}
void *gFakeStringVT[8] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, reinterpret_cast<void *>(&FakeData),
                          reinterpret_cast<void *>(&FakeStrlen)};
const char kFakeText[] = "fake text\0with more after its NUL";

struct Values {
    std::vector<uint32_t *> words;
    std::vector<cRZString *> strings;
    std::vector<FakeString *> fakes;
    ~Values()
    {
        for (uint32_t *w : words) delete[] w;
        for (cRZString *s : strings) {
            s->Destruct();
            operator delete(s);
        }
        for (FakeString *f : fakes) delete f;
    }
    void *Word(uint32_t vtable, uint32_t value)
    {
        auto *w = new uint32_t[2]{svcfx::P(GameAddress<const void>(vtable)), value};
        words.push_back(w);
        return w;
    }
};

const char *const kKeys[] = {"0", "1", "2", "3", "12", "abc", "-1", " 1", "00", "1x", "4294967297", "", "2 ", "0x2",
                             "a key that is longer than fifteen chars"};
const char *const kTexts[] = {"", "The Sims 2", "1.18.0.187", "a text that is longer than fifteen characters", "x = y\n"};

void *RandomValue(std::mt19937 &rng, Values &values)
{
    switch (rng() % 7) {
    case 0:
    case 1: {
        const uint32_t v[] = {0, 1, 0x7fffffff, 0x80000000, 0xffffffff, 12};
        return values.Word(gzany::kUint32VTable, rng() % 2 ? v[rng() % 6] : uint32_t(rng()));
    }
    case 2:
    case 3: {
        auto *s = static_cast<cRZString *>(operator new(sizeof(cRZString)));
        s->Construct();
        std::string text = kTexts[rng() % std::size(kTexts)];
        if (rng() % 6 == 0) text.insert(text.size() / 2, 1, '\0');
        s->mString.assign(text.data(), text.size());
        s->mRefCount = 1;
        values.strings.push_back(s);
        return values.Word(gzany::kStringVTable, svcfx::P(s));
    }
    case 4: {
        const uint32_t lengths[] = {0, 4, 9, 20, uint32_t(sizeof(kFakeText) - 1)};
        auto *f = new FakeString{gFakeStringVT, kFakeText, lengths[rng() % 5]};
        values.fakes.push_back(f);
        return values.Word(gzany::kStringVTable, svcfx::P(f));
    }
    case 5: return values.Word(gzany::kSint32VTable, rng());
    default: {
        const uint32_t vtables[] = {gzany::kFloat32VTable, gzany::kBooleanVTable, gzany::kAnyAnyVTable};
        return values.Word(vtables[rng() % 3], rng());
    }
    }
}

} // namespace

DIFF_TEST(SaveGameInfo_Describe, 0xcd8140)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 1200; ++seed) {
        std::mt19937 rng(seed);
        Values values;
        gProperties.clear();
        for (uint32_t n = seed % 9 == 0 ? 0u : uint32_t(1 + rng() % 6); n; --n) {
            std::string key = kKeys[rng() % std::size(kKeys)];
            if (rng() % 10 == 0) key.insert(key.size() / 2, 1, '\0');
            gProperties.push_back({key, RandomValue(rng, values)});
        }
        const bool noSet = seed % 23 == 0;
        const uint32_t firstFails = rng() % 12 == 0 ? (rng() % 2 ? 0x100u : 0x200u) : 0u;
        const uint32_t nextEnd = rng() % 4 ? 0u : 0x100u;
        const char *previous = rng() % 2 ? "previous" : "a previous text that is longer than fifteen chars";
        Blob result[2];
        for (int s = 0; s < 2; ++s) {
            svcfx::ResetAll();
            F(1).Set(6, "pp", 0, &SetGetFirst);
            F(1).Set(7, "pp", 0, &SetGetNext);
            F(1).value[1] = firstFails;
            F(1).value[2] = nextEnd;
            cTSSaveGameInfo info{GameAddress<const void>(cTSSaveGameInfo::kVTable), 1,
                                 noSet ? nullptr : reinterpret_cast<cGZPropertySetString *>(&F(1))};
            cRZString out;
            out.Construct(previous);
            if (s == 0)
                ctx.Original<void(__thiscall *)(void *, void *)>(0xcd8140)(&info, out.AsInterface());
            else
                info.Describe(out.AsInterface());
            result[s].push_back(uint32_t(out.mString.size()));
            for (char c : out.mString) result[s].push_back(uint8_t(c));
            result[s].insert(result[s].end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::CaptureFakes(result[s], kValueKinds);
            out.Destruct();
        }
        if (!svcfx::Same(ctx, result[0], result[1], "Describe", seed)) break;
    }
}
