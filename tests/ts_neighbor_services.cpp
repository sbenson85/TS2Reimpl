// Differential test for cTSNeighbor::BuildDisplayName (0x4b7a20, slot 40). Fixture: tests/ts_services_fixture.h.
//
// The function only goes through virtual slots of the neighbour, so `this` is a recording fake. The person data, its slot 54
// object, the second-language service (cache 0x10cc2dc) and its formatter, the sink, the app singleton chain of the helper
// 0x54ac40 (run as original code on both sides) are fakes; the string table 0x336 is the resource-manager fake of
// tests/ts_string_fake.h (cache 0x10f51a8), so the original and our cTSString constructors load from it.
#include "ts_services_fixture.h"
#include "ts_string_fake.h"

#include "rz/cRZString.h"
#include "ts/cTSNeighbor.h"

#include <new>
#include <random>
#include <string>

namespace {

// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{0, "d"}, {1, "d"}, {2, "d"}, {4, "d"}};
using namespace svcfx;

enum Fk { kThis, kPerson, kDetails, kLang, kFormatter, kSink, kApp, kOwner, kEntryObj };

const char *const kNames[] = {"", "Ann", "Bob", "Mortimer-Longfellow-Smith", "x", "Zed Q"};
const char *const kFormats[] = {"", "%s", "Dr. %s", "plain text", "[%s]!", "n/a"};

void AssignText(void *out, const char *text) { static_cast<cRZString *>(out)->mString.assign(text); }

uint32_t PersonFirst(Obj *o, uint32_t *a)
{
    AssignText(reinterpret_cast<void *>(a[0]), kNames[o->value[0]]);
    return 0;
}
uint32_t DetailsLast(Obj *o, uint32_t *a)
{
    AssignText(reinterpret_cast<void *>(a[0]), kNames[o->value[0]]);
    return 0;
}
void LogText(const cRZString *s)
{
    Ev(uint32_t(s->mString.size()));
    for (char c : s->mString) Ev(uint8_t(c));
}
// formatter slot 28 (first, last, out): logs the inputs, writes a fixed result
uint32_t Format(Obj *o, uint32_t *a)
{
    LogText(reinterpret_cast<const cRZString *>(a[0]));
    LogText(reinterpret_cast<const cRZString *>(a[1]));
    AssignText(reinterpret_cast<void *>(a[2]), kNames[o->value[0]]);
    return 0;
}
// sink slot 16 (text): logs the text
uint32_t SinkText(Obj *, uint32_t *a)
{
    LogText(reinterpret_cast<const cRZString *>(a[0]));
    return 0;
}
// the language service's slot 38 (0): the formatter or nothing
uint32_t ThisEntry(Obj *o, uint32_t *) { return 0xabcd0000u | o->value[0]; }

} // namespace

DIFF_TEST(NeighborServices_BuildDisplayName, 0x4b7a20)
{
    tsfake::ManagerFixture strings(ctx);
    // Our side (docs/standalone-shape.md): the function our vtable holds in slot 40 (vtable route, even seeds) or the RVA hook's
    // adapter (odd seeds), on the fake `this` either way; the fake's own slot 40 records a call, so an adapter that dispatched
    // virtually would show in the log.
    void *ourSlot40;
    {
        alignas(8) static uint8_t buffer[sizeof(cTSNeighbor)];
        auto *n = ::new (buffer) cTSNeighbor;
        ourSlot40 = (*reinterpret_cast<void ***>(buffer))[40];
        n->~cTSNeighbor();
    }
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            ResetAll();
            std::mt19937 rng(seed * 7919u + 29);
            Globals g;
            strings.Reset(tsfake::Mode::Present);
            const uint32_t formatPick = rng();
            const bool formatFound = rng() % 4 != 0;
            tsfake::gAnswerer = [&](const tsfake::Ask &ask, std::string &text) {
                text = kFormats[(formatPick + ask.instance) % 6];
                return formatFound;
            };

            const bool hasPerson = rng() % 8 != 0;
            F(kThis).Set(10, 0, hasPerson ? P(&F(kPerson)) : 0u);
            F(kThis).Set(15, 0, 100 + rng() % 50);
            F(kThis).Set(16, "d", 0u, &ThisEntry);
            F(kThis).Set(40, 1, 0u);
            F(kThis).value[0] = rng() % 4 == 0 ? 0 : 1 + rng() % 0xfff;
            F(kPerson).value[0] = rng() % 6;
            F(kPerson).Set(49, "p", 0u, &PersonFirst);
            F(kPerson).Set(54, 0, &F(kDetails));
            F(kDetails).value[0] = rng() % 6;
            F(kDetails).Set(7, "p", 0u, &DetailsLast);
            const unsigned languageMode = rng() % 3; // 0 no service, 1 service without formatter, 2 formatter
            F(kLang).Set(38, 1, languageMode == 2 ? P(&F(kFormatter)) : 0u);
            F(kFormatter).value[0] = rng() % 6;
            F(kFormatter).Set(28, "ppp", 0u, &Format);
            F(kSink).Set(16, "p", 0u, &SinkText);
            F(kSink).Set(26, "-d", 0u);
            g.Set(kSecondLanguageCache, languageMode == 0 ? nullptr : &F(kLang));
            // 0x54ac40's chain: app singleton slot 26 -> owner slot 43 (id) -> entry object slot 10 (0xe6, 1)
            F(kApp).Set(26, 0, rng() % 3 == 0 ? 0u : P(&F(kOwner)));
            F(kOwner).Set(43, "d", rng() % 3 == 0 ? 0u : P(&F(kEntryObj)));
            F(kEntryObj).Set(10, "dd", rng() % 4 == 0 ? 0xffff0000u : 0xff00u + rng() % 4 * 0x10000 + rng() % 6);
            g.Set(kAppCell, &F(kApp));

            Blob &b = blobs[side];
            using Fn = uint8_t(__thiscall *)(void *, void *);
            uint8_t r;
            if (side == 0) r = ctx.Original<Fn>(0x4b7a20)(&F(kThis), &F(kSink));
            else r = reinterpret_cast<Fn>(ourSlot40)(&F(kThis), &F(kSink)); // (its adapter was dropped, lane GP)
            b.push_back(r);
            b.insert(b.end(), gEv.begin(), gEv.end());
            CaptureFakes(b, kValueKinds);
            for (const auto &ask : tsfake::gAsks)
                b.insert(b.end(), {ask.type, ask.group, ask.instance, ask.keyWord3, ask.interfaceId, uint32_t(ask.loadFlags), ask.reserved});
            b.insert(b.end(), tsfake::gEvents.begin(), tsfake::gEvents.end());
        }
        Same(ctx, blobs[0], blobs[1], "BuildDisplayName", seed);
    }
}
