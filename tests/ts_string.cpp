// Differential tests for cTSString, the string-table loader object (src/ts/cTSString.*): the empty constructor (0x1d0fb0), the
// assignment (0x1d1000), the loading constructors (0x1d0df0, 0x1d0e90), the loader behind them (0x1d10d0), the deleting destructor
// (0x1d1060), the forwarding of slot 16 (0x1d1050) and the resource manager getter the loader asks (0x9c5b26).
//
// The loader's only outside world is the resource manager, so the tests install the fake of tests/ts_string_fake.h in the game's
// cache global: the original and ours both reach it, and what they ask, what they call and in which order, and what they release is
// compared. The fake answers from a seeded text, with bullets (U+2022) scattered through it, so the replacement by zero width
// spaces is exercised on every kind of neighbourhood.
#include "difftest.h"
#include "free_scope.h"
#include "ts/cTSString.h"
#include "ts_string_fake.h"

#include <windows.h>

#include <cstring>
#include <random>
#include <string>

namespace {

// ---- inputs ---------------------------------------------------------------------------------------------------------------

std::string RandomText(std::mt19937 &r)
{
    static const size_t lengths[] = {0, 1, 5, 14, 15, 16, 17, 30, 31, 32, 100, 300};
    std::string s(lengths[r() % (sizeof(lengths) / sizeof(lengths[0]))], 'a');
    for (char &c : s) c = static_cast<char>(r() % 4 == 0 ? 1 + r() % 255 : 'a' + r() % 26);
    return s;
}

// A text built from pieces that include the bullet's UTF-8 form, parts of it, and its replacement.
std::string BulletText(std::mt19937 &r)
{
    static const char *const pieces[] = {"\xe2\x80\xa2", "\xe2\x80\xa2", "\xe2\x80\xa2\xe2\x80\xa2", "\xe2\x80", "\x80\xa2", "\xe2", "\xa2", "\xe2\x80\x8b",
                                         "\xe2\x80\xa3", "\xc3\xa9", "a", "Hello", " - ", "line\r\n", "%d", "\xe2\xe2\x80\xa2\x80"};
    std::string s;
    const int count = r() % 12;
    for (int i = 0; i < count; ++i) s += pieces[r() % (sizeof(pieces) / sizeof(pieces[0]))];
    if (r() % 6 == 0) s += RandomText(r);
    return s;
}

// A loader as the string tables would have left it: a text, a reference count and random key fields.
void Fill(cTSString &t, std::mt19937 &r, const void *vtable)
{
    std::memset(&t, 0xcd, sizeof(t));
    t.mString.Construct(RandomText(r).c_str());
    t.mString.mVTable = vtable;
    t.mString.mRefCount = r() % 7;
    t.mResourceType = r();
    t.mGroup = r();
    t.mEntry = static_cast<uint16_t>(r());
    t.mTable = static_cast<uint16_t>(r());
    t.mKeyWord3 = r();
    t.mLoaded = static_cast<uint8_t>(r());
    t.mLoadFlags = static_cast<int8_t>(r());
}

bool SameLoader(const cTSString &a, const cTSString &b)
{
    return a.mString.mString == b.mString.mString && a.mString.mString.capacity() == b.mString.mString.capacity() &&
           a.mString.mRefCount == b.mString.mRefCount && a.mString.mVTable == b.mString.mVTable && a.mResourceType == b.mResourceType &&
           a.mGroup == b.mGroup && a.mEntry == b.mEntry && a.mTable == b.mTable && a.mKeyWord3 == b.mKeyWord3 && a.mLoaded == b.mLoaded &&
           a.mLoadFlags == b.mLoadFlags;
}

bool Live(const void *p)
{
    return !difftest::FreedSinceBegin(p); // the call under test ran after difftest::BeginFrees()
}

// What a run of one side left behind (the fake's records), to compare with the other side's.
struct Outcome {
    std::vector<tsfake::Ask> asks;
    std::vector<uint32_t> events;
    int created = 0, released = 0;
    uint32_t firstId = 0, secondId = 0;
    void *cache = nullptr;
    bool threw = false;
    int thrown = 0;
    bool result = false;
};

Outcome Capture(const tsfake::ManagerFixture &fixture)
{
    Outcome o;
    o.asks = tsfake::gAsks;
    o.events = tsfake::gEvents;
    o.created = tsfake::gResourcesCreated;
    o.released = tsfake::gResourcesReleased;
    o.firstId = tsfake::gQueriedFirstId;
    o.secondId = tsfake::gQueriedSecondId;
    o.cache = fixture.CacheValue();
    return o;
}

bool SameOutcome(const Outcome &a, const Outcome &b)
{
    return a.asks == b.asks && a.events == b.events && a.created == b.created && a.released == b.released && a.firstId == b.firstId &&
           a.secondId == b.secondId && a.cache == b.cache && a.threw == b.threw && a.thrown == b.thrown && a.result == b.result;
}

// One seeded scenario for the loading tests: how the manager is found, what it answers and whether an error is thrown.
struct Scenario {
    tsfake::Mode mode;
    bool found;
    bool notFoundButOutput;
    tsfake::Fault fault;
    std::string text;
    bool answerAny;
};

Scenario MakeScenario(std::mt19937 &r)
{
    Scenario s;
    const uint32_t m = r() % 10;
    s.mode = m < 6 ? tsfake::Mode::Present : (m < 8 ? tsfake::Mode::ViaService : tsfake::Mode::Absent);
    s.found = r() % 5 != 0;
    s.notFoundButOutput = r() % 4 == 0;
    const uint32_t f = r() % 12;
    s.fault = f == 0 ? tsfake::Fault::GetResource : (f == 1 ? tsfake::Fault::Strlen : (f == 2 ? tsfake::Fault::Data : tsfake::Fault::None));
    s.text = r() % 5 == 0 ? RandomText(r) : BulletText(r);
    s.answerAny = true;
    return s;
}

void Install(tsfake::ManagerFixture &fixture, const Scenario &s)
{
    fixture.Reset(s.mode);
    tsfake::gFault = s.fault;
    tsfake::gNotFoundButOutput = s.notFoundButOutput;
    // The answer depends on the key too (a group of 0 never has the text), so a swapped key shows.
    const bool found = s.found;
    const std::string text = s.text;
    tsfake::gAnswerer = [found, text](const tsfake::Ask &ask, std::string &out) {
        if (!found) return false;
        out = text;
        // Mix the key into the text so that two different keys give different texts.
        out += static_cast<char>('A' + (ask.group ^ ask.instance ^ ask.type) % 26);
        return true;
    };
}

} // namespace

// ---- 0x1d0fb0, 0x1d1000 ---------------------------------------------------------------------------------------------------

DIFF_TEST(cTSString_ConstructEmpty, 0x1d0fb0)
{
    using Fn = cTSString *(__thiscall *)(cTSString *);
    const Fn original = ctx.Original<Fn>(0x1d0fb0);
    std::mt19937 r(0x1d0fb0);
    for (int i = 0; i < 2000; ++i) {
        alignas(16) uint8_t a[0x40], b[0x40];
        for (int k = 0; k < 0x40; ++k) a[k] = b[k] = static_cast<uint8_t>(r());
        cTSString *ra = original(reinterpret_cast<cTSString *>(a));
        cTSString *rb = reinterpret_cast<cTSString *>(b)->ConstructEmpty();
        ++ctx.cases;
        if (ra != reinterpret_cast<cTSString *>(a) || rb != reinterpret_cast<cTSString *>(b) || std::memcmp(a, b, 0x34) != 0 || std::memcmp(a + 0x34, b + 0x34, 0xc) != 0) {
            ctx.Fail("ConstructEmpty case %d: the objects (or the bytes after them) differ", i);
            break;
        }
    }
}

DIFF_TEST(cTSString_Assign, 0x1d1000)
{
    using Fn = cTSString *(__thiscall *)(cTSString *, const cTSString *);
    const Fn original = ctx.Original<Fn>(0x1d1000);
    const void *vtable = ctx.image.At(cTSString::kVTable);
    std::mt19937 r(0x1d1000);
    for (int i = 0; i < 4000; ++i) {
        cTSString d1, d2, s1, s2;
        const uint32_t seed = r();
        std::mt19937 rd(seed), rs(seed ^ 0x5a5a);
        Fill(d1, rd, vtable);
        Fill(s1, rs, vtable);
        rd.seed(seed);
        rs.seed(seed ^ 0x5a5a);
        Fill(d2, rd, vtable);
        Fill(s2, rs, vtable);
        const bool self = r() % 8 == 0;
        cTSString *ra = original(&d1, self ? &d1 : &s1);
        cTSString *rb = d2.Assign(self ? &d2 : &s2);
        ++ctx.cases;
        const bool same = ra == &d1 && rb == &d2 && SameLoader(d1, d2) && SameLoader(s1, s2);
        if (!same) {
            ctx.Fail("Assign case %d (self %d): the objects differ", i, self);
            d1.mString.Destruct();
            d2.mString.Destruct();
            s1.mString.Destruct();
            s2.mString.Destruct();
            break;
        }
        d1.mString.Destruct();
        d2.mString.Destruct();
        s1.mString.Destruct();
        s2.mString.Destruct();
    }
}

// ---- 0x1d10d0 -------------------------------------------------------------------------------------------------------------

DIFF_TEST(cTSString_Load_1d10d0, 0x1d10d0)
{
    using Fn = uint32_t(__thiscall *)(cTSString *);
    const Fn original = ctx.Original<Fn>(0x1d10d0);
    const void *vtable = ctx.image.At(cTSString::kVTable);
    tsfake::ManagerFixture fixture(ctx);
    std::mt19937 r(0x1d10d0);
    for (int i = 0; i < 6000; ++i) {
        const uint32_t seed = r();
        const Scenario scenario = MakeScenario(r);
        cTSString objects[2];
        Outcome outcomes[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 rf(seed);
            Fill(objects[side], rf, vtable);
            Install(fixture, scenario);
            try {
                outcomes[side].result = side == 0 ? (original(&objects[0]) & 0xff) != 0 : objects[1].Load();
            } catch (int code) {
                outcomes[side].threw = true;
                outcomes[side].thrown = code;
            }
            const Outcome captured = Capture(fixture);
            outcomes[side].asks = captured.asks;
            outcomes[side].events = captured.events;
            outcomes[side].created = captured.created;
            outcomes[side].released = captured.released;
            outcomes[side].firstId = captured.firstId;
            outcomes[side].secondId = captured.secondId;
            outcomes[side].cache = captured.cache;
        }
        ++ctx.cases;
        const bool same = SameOutcome(outcomes[0], outcomes[1]) && SameLoader(objects[0], objects[1]);
        if (!same) {
            ctx.Fail("Load case %d (mode %d, found %d, output %d, fault %d): result %d/%d, threw %d/%d, events %zu/%zu, released %d/%d, state %s", i,
                     static_cast<int>(scenario.mode), scenario.found, scenario.notFoundButOutput, static_cast<int>(scenario.fault), outcomes[0].result,
                     outcomes[1].result, outcomes[0].threw, outcomes[1].threw, outcomes[0].events.size(), outcomes[1].events.size(),
                     outcomes[0].released, outcomes[1].released, SameLoader(objects[0], objects[1]) ? "same" : "different");
            objects[0].mString.Destruct();
            objects[1].mString.Destruct();
            break;
        }
        // Every resource the fake handed out was released, and a found text has no bullet left.
        if (!outcomes[0].threw && outcomes[0].created != outcomes[0].released) ctx.Fail("Load case %d: a resource was not released", i);
        if (objects[1].mLoaded == 1 && !outcomes[1].threw && objects[1].mString.mString.find("\xe2\x80\xa2") != std::string::npos)
            ctx.Fail("Load case %d: a bullet is left", i);
        objects[0].mString.Destruct();
        objects[1].mString.Destruct();
    }
}

// ---- 0x1d0df0, 0x1d0e90 ---------------------------------------------------------------------------------------------------

namespace {

// Runs both constructors of one scenario on raw buffers and compares everything.
template <typename OriginalCall, typename OurCall>
void CheckConstructor(difftest::Context &ctx, const char *name, uint32_t seed, int rounds, OriginalCall callOriginal, OurCall callOurs)
{
    tsfake::ManagerFixture fixture(ctx);
    std::mt19937 r(seed);
    for (int i = 0; i < rounds; ++i) {
        const uint32_t caseSeed = r();
        std::mt19937 rf(caseSeed);
        const Scenario scenario = MakeScenario(rf);
        // Arguments: the words arrive in 32-bit slots with whatever the caller left in the upper halves.
        const uint32_t table = rf(), entry = rf(), group = rf(), flags = rf(), type = rf() % 3 == 0 ? cTSString::kStringTableType : rf();
        alignas(16) uint8_t bytes[2][0x40];
        Outcome outcomes[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 rb(caseSeed ^ 0xb17e);
            for (int k = 0; k < 0x40; ++k) bytes[side][k] = static_cast<uint8_t>(rb());
            Install(fixture, scenario);
            cTSString *object = reinterpret_cast<cTSString *>(bytes[side]);
            try {
                cTSString *returned = side == 0 ? callOriginal(object, table, entry, group, flags, type) : callOurs(object, table, entry, group, flags, type);
                outcomes[side].result = returned == object;
            } catch (int code) {
                outcomes[side].threw = true;
                outcomes[side].thrown = code;
            }
            const Outcome captured = Capture(fixture);
            outcomes[side].asks = captured.asks;
            outcomes[side].events = captured.events;
            outcomes[side].created = captured.created;
            outcomes[side].released = captured.released;
            outcomes[side].firstId = captured.firstId;
            outcomes[side].secondId = captured.secondId;
            outcomes[side].cache = captured.cache;
        }
        ++ctx.cases;
        const cTSString *a = reinterpret_cast<const cTSString *>(bytes[0]);
        const cTSString *b = reinterpret_cast<const cTSString *>(bytes[1]);
        bool same = SameOutcome(outcomes[0], outcomes[1]) && std::memcmp(bytes[0] + 0x34, bytes[1] + 0x34, 0xc) == 0;
        if (same) {
            if (outcomes[0].threw) {
                // The string was destroyed again: nothing on the heap, so the raw bytes are comparable.
                same = std::memcmp(bytes[0], bytes[1], 0x34) == 0;
            } else {
                same = SameLoader(*a, *b);
            }
        }
        if (!same) {
            ctx.Fail("%s case %d (mode %d, found %d, fault %d): the outcomes differ (threw %d/%d, events %zu/%zu, asks %zu/%zu)", name, i,
                     static_cast<int>(scenario.mode), scenario.found, static_cast<int>(scenario.fault), outcomes[0].threw, outcomes[1].threw,
                     outcomes[0].events.size(), outcomes[1].events.size(), outcomes[0].asks.size(), outcomes[1].asks.size());
            if (!outcomes[0].threw) const_cast<cTSString *>(a)->mString.Destruct();
            if (!outcomes[1].threw) const_cast<cTSString *>(b)->mString.Destruct();
            return;
        }
        if (!outcomes[0].threw) {
            const_cast<cTSString *>(a)->mString.Destruct();
            const_cast<cTSString *>(b)->mString.Destruct();
        }
    }
}

} // namespace

DIFF_TEST(cTSString_Construct_1d0df0, 0x1d0df0)
{
    using Fn = cTSString *(__thiscall *)(cTSString *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    const Fn original = ctx.Original<Fn>(0x1d0df0);
    CheckConstructor(
        ctx, "Construct", 0x1d0df0, 4000,
        [&](cTSString *object, uint32_t table, uint32_t entry, uint32_t group, uint32_t flags, uint32_t type) { return original(object, table, entry, group, flags, type); },
        [&](cTSString *object, uint32_t table, uint32_t entry, uint32_t group, uint32_t flags, uint32_t type) {
            return object->Construct(static_cast<uint16_t>(table), static_cast<uint16_t>(entry), group, static_cast<uint8_t>(flags), type);
        });
}

DIFF_TEST(cTSString_ConstructNoFlags_1d0e90, 0x1d0e90)
{
    using Fn = cTSString *(__thiscall *)(cTSString *, uint32_t, uint32_t, uint32_t, uint32_t);
    const Fn original = ctx.Original<Fn>(0x1d0e90);
    CheckConstructor(
        ctx, "ConstructNoFlags", 0x1d0e90, 4000,
        [&](cTSString *object, uint32_t table, uint32_t entry, uint32_t group, uint32_t, uint32_t type) { return original(object, table, entry, group, type); },
        [&](cTSString *object, uint32_t table, uint32_t entry, uint32_t group, uint32_t, uint32_t type) {
            return object->Construct(static_cast<uint16_t>(table), static_cast<uint16_t>(entry), group, type);
        });
}

// ---- 0x1d1060 -------------------------------------------------------------------------------------------------------------

DIFF_TEST(cTSString_DeletingDestructor_1d1060, 0x1d1060)
{
    using Fn = cTSString *(__thiscall *)(cTSString *, uint32_t);
    const Fn original = ctx.Original<Fn>(0x1d1060);
    const void *vtable = ctx.image.At(cTSString::kVTable);
    std::mt19937 r(0x1d1060);
    for (int i = 0; i < 400; ++i) {
        const uint32_t seed = r();
        const uint32_t flags = r() % 4;
        // The object lives in a heap block of exactly its size, as the game's operator new makes it.
        cTSString *objects[2] = {static_cast<cTSString *>(operator new(sizeof(cTSString))), static_cast<cTSString *>(operator new(sizeof(cTSString)))};
        for (int side = 0; side < 2; ++side) {
            std::mt19937 rf(seed);
            Fill(*objects[side], rf, vtable);
        }
        difftest::BeginFrees();
        cTSString *ra = original(objects[0], flags);
        const bool freedA = !Live(objects[0]);
        difftest::BeginFrees();
        cTSString *rb = objects[1]->DeletingDestructor(flags);
        const bool freedB = !Live(objects[1]);
        ++ctx.cases;
        if (ra != objects[0] || rb != objects[1] || freedA != freedB || freedA != ((flags & 1) != 0)) {
            ctx.Fail("DeletingDestructor case %d (flags %u): returned %d/%d, freed %d/%d", i, flags, ra == objects[0], rb == objects[1], freedA, freedB);
            break;
        }
        if (!freedA) {
            // The string is destroyed and the vtable is cRZString's again.
            if (!SameLoader(*objects[0], *objects[1]) || objects[0]->mString.mString.capacity() != 15 || !objects[1]->mString.mString.empty() ||
                objects[1]->mString.mVTable != ctx.image.At(cRZString::kVTable))
                ctx.Fail("DeletingDestructor case %d (flags %u): the destroyed objects differ", i, flags);
            operator delete(objects[0]);
            operator delete(objects[1]);
        }
    }
}

// ---- 0x1d1050 -------------------------------------------------------------------------------------------------------------

DIFF_TEST(cTSString_AssignString_1d1050, 0x1d1050)
{
    using Fn = cRZString *(__thiscall *)(cTSString *, const cIGZString *);
    const Fn original = ctx.Original<Fn>(0x1d1050);
    const void *vtable = ctx.image.At(cTSString::kVTable);
    std::mt19937 r(0x1d1050);
    for (int i = 0; i < 2000; ++i) {
        const uint32_t seed = r();
        cTSString d1, d2;
        cRZString s1, s2;
        std::mt19937 rd(seed);
        Fill(d1, rd, vtable);
        rd.seed(seed);
        Fill(d2, rd, vtable);
        const std::string text = r() % 5 == 0 ? std::string() : BulletText(r);
        s1.Construct(text.c_str());
        s2.Construct(text.c_str());
        const bool self = r() % 8 == 0;
        cRZString *ra = original(&d1, self ? reinterpret_cast<const cIGZString *>(&d1.mString) : s1.AsInterface());
        cRZString *rb = d2.AssignString(self ? *reinterpret_cast<const cIGZString *>(&d2.mString) : *s2.AsInterface());
        ++ctx.cases;
        if (ra != &d1.mString || rb != &d2.mString || !SameLoader(d1, d2) || s1.mString != s2.mString)
            ctx.Fail("AssignString case %d (self %d): the objects differ", i, self);
        d1.mString.Destruct();
        d2.mString.Destruct();
        s1.Destruct();
        s2.Destruct();
    }
}

// ---- 0x9c5b26 -------------------------------------------------------------------------------------------------------------

DIFF_TEST(cTSString_GetResourceManager_9c5b26, 0x9c5b26)
{
    using Fn = void *(__cdecl *)();
    const Fn original = ctx.Original<Fn>(0x9c5b26);
    tsfake::ManagerFixture fixture(ctx);
    for (int round = 0; round < 3; ++round) {
        for (tsfake::Mode mode : {tsfake::Mode::Present, tsfake::Mode::ViaService, tsfake::Mode::Absent}) {
            Outcome outcomes[2];
            void *results[2];
            for (int side = 0; side < 2; ++side) {
                fixture.Reset(mode);
                results[side] = side == 0 ? original() : tsstring::GetResourceManager();
                outcomes[side] = Capture(fixture);
            }
            ++ctx.cases;
            if (results[0] != results[1] || !SameOutcome(outcomes[0], outcomes[1]))
                ctx.Fail("GetResourceManager mode %d: result %p/%p, events %zu/%zu, cache %p/%p", static_cast<int>(mode), results[0], results[1],
                         outcomes[0].events.size(), outcomes[1].events.size(), outcomes[0].cache, outcomes[1].cache);
            // The service is asked with the getter's two identifiers.
            if (mode == tsfake::Mode::ViaService && (outcomes[1].firstId != 0x056b906e || outcomes[1].secondId != 0x656b8efc))
                ctx.Fail("GetResourceManager: the service was asked for %08x/%08x", outcomes[1].firstId, outcomes[1].secondId);
        }
    }
}
