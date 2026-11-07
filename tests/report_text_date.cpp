#define _CRT_SECURE_NO_WARNINGS
// Differential tests for the game's calendar date (cRZDate) and time-of-day (cRZTime) classes in
// src/app/ReportText.*: the time stamp of the exception reporter's file names.
//
// The time class reads the C runtime's time-zone variables and a global holding the epoch day; the
// fixture sets those on both sides (the game's CRT copy in the image, and ours) and restores them.
#define NOMINMAX
#include "app/ReportText.h"
#include "difftest.h"
#include "game/game.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>
#include <time.h>

using appbase::cRZDate;
using appbase::cRZTime;

namespace {

constexpr uint32_t kEpochGlobal = 0x10cafe0, kTimeZoneGlobal = 0x10dfc98, kDaylightGlobal = 0x10dfc9c;
constexpr uint32_t kIsLeap = 0x1b995c, kIsValid = 0x1b98a4, kToJulian = 0x1b98e2, kCopy = 0x1b969a, kCtorDays = 0x1b96af;
constexpr uint32_t kCtorDate = 0x1b96cb, kCtorToday = 0x1b96f1, kSet = 0x1b9a8b, kDecompose = 0x1b998a;
constexpr uint32_t kGetMonth = 0x1b9a26, kGetDay = 0x1b9888, kGetYear = 0x1b9ad0, kGetWeekday = 0x1b9aba;
constexpr uint32_t kPrevWeekday = 0x1b9a53, kFormat = 0x1b9738;
constexpr uint32_t kFromDate = 0x1452d5, kDstStart = 0x145268, kDstEnd = 0x145455, kTimeCtor = 0x144f81;
constexpr uint32_t kTimeNow = 0x144ff4, kIsDst = 0x14583b, kLocalSeconds = 0x145ad1, kGetHour = 0x145821, kGetMinute = 0x145afc;
constexpr uint32_t kGuidSeconds = 0x145774;

// The image's localtime wrapper (0x82fb8c, cdecl tm*, const __int64*) normally ends in the game CRT's
// time-zone initialisation, which asks the OS (GetTimeZoneInformation traps in the harness). With the
// clock fixture it is redirected to our CRT's conversion, so only the code around it is under test.
int __cdecl StubLocalTime(struct tm *out, const __time64_t *value)
{
    return _localtime64_s(out, value);
}

// Sets the epoch day and the time-zone variables on both sides for the life of the object.
class Fixture {
public:
    Fixture(difftest::Context &c, bool warmClock = false, int32_t zone = 28800, int32_t daylight = 1) : ctx(c)
    {
        if (warmClock) { // let our C runtime run its one-time time-zone initialisation before the variables are set
            __time64_t zero = 0;
            struct tm scratchTm;
            _localtime64_s(&scratchTm, &zero);
            site = ctx.image.At<uint8_t>(0x82fb8c);
            std::memcpy(saved, site, 5);
            DWORD old;
            VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
            int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&StubLocalTime) - (reinterpret_cast<uintptr_t>(site) + 5));
            site[0] = 0xE9;
            std::memcpy(site + 1, &rel, 4);
        }
        savedEpoch = *ctx.image.At<uint32_t>(kEpochGlobal);
        savedZone = *ctx.image.At<int32_t>(kTimeZoneGlobal);
        savedDaylight = *ctx.image.At<int32_t>(kDaylightGlobal);
        ourZone = *__timezone();
        ourDaylight = *__daylight();
        *ctx.image.At<uint32_t>(kEpochGlobal) = cRZDate::kEpochDay; // what the game's static initialiser stores
        Set(zone, daylight);
    }
    ~Fixture()
    {
        if (site) std::memcpy(site, saved, 5);
        *ctx.image.At<uint32_t>(kEpochGlobal) = savedEpoch;
        *ctx.image.At<int32_t>(kTimeZoneGlobal) = savedZone;
        *ctx.image.At<int32_t>(kDaylightGlobal) = savedDaylight;
        *__timezone() = ourZone;
        *__daylight() = ourDaylight;
    }
    void Set(int32_t zone, int32_t daylight)
    {
        *ctx.image.At<int32_t>(kTimeZoneGlobal) = zone;
        *ctx.image.At<int32_t>(kDaylightGlobal) = daylight;
        *__timezone() = zone;
        *__daylight() = daylight;
    }

private:
    difftest::Context &ctx;
    uint8_t *site = nullptr;
    uint8_t saved[5];
    uint32_t savedEpoch;
    int32_t savedZone, savedDaylight, ourZone, ourDaylight;
};

template <typename A, typename B> void Expect(difftest::Context &ctx, const char *what, uint64_t input, A original, B ours)
{
    ++ctx.cases;
    if (static_cast<uint64_t>(original) != static_cast<uint64_t>(ours))
        ctx.Fail("%s(%llx): original %llx, ours %llx", what, static_cast<unsigned long long>(input), static_cast<unsigned long long>(original), static_cast<unsigned long long>(ours));
}

// Julian days: around the epoch, anywhere, and the extremes.
uint32_t RandomDay(std::mt19937 &rng)
{
    switch (rng() % 6) {
    case 0: return 0;
    case 1: return cRZDate::kEpochDay - 20000 + rng() % 60000;
    case 2: return rng() % 0x400000;
    case 3: return rng();
    case 4: return 0x24db1a + rng() % 30000; // 1970s-2050s
    default: return 0xffffff00u + rng() % 256;
    }
}

uint32_t RandomYear(std::mt19937 &rng)
{
    switch (rng() % 5) {
    case 0: return rng() % 3000;
    case 1: return 1960 + rng() % 120;
    case 2: return 1970 + rng() % 20; // the special-cased 1974 / 1975 and the 1986 limit
    case 3: return rng();
    default: return 1899 + rng() % 400;
    }
}

int32_t RandomZone(std::mt19937 &rng)
{
    static const int32_t kZones[] = {0, 28800, 25200, 18000, -3600, -19800, 43200, -43200, 3600, 14400};
    return rng() % 3 == 0 ? static_cast<int32_t>(rng() % 100000) - 50000 : kZones[rng() % 10];
}

int32_t RandomDaylight(std::mt19937 &rng)
{
    static const int32_t kFlags[] = {1, 1, 0, 2, -1};
    return kFlags[rng() % 5];
}

struct DateObject {
    const void *vtable;
    uint32_t julianDay;
};

void Garbage(DateObject &d, std::mt19937 &rng)
{
    d.vtable = reinterpret_cast<const void *>(static_cast<uintptr_t>(rng()));
    d.julianDay = rng();
}

} // namespace

// ---- calendar helpers ----

DIFF_TEST(ReportText_IsLeapYear, kIsLeap)
{
    auto orig = ctx.Original<uint32_t(__cdecl *)(uint32_t)>(kIsLeap);
    std::mt19937 rng(1);
    for (uint32_t year = 0; year < 5000; ++year) Expect(ctx, "leap", year, orig(year), cRZDate::IsLeapYear(year));
    for (int i = 0; i < 3000; ++i) {
        uint32_t year = rng();
        Expect(ctx, "leap", year, orig(year), cRZDate::IsLeapYear(year));
    }
}

DIFF_TEST(ReportText_IsValid, kIsValid)
{
    auto orig = ctx.Original<uint32_t(__cdecl *)(uint32_t, uint32_t, uint32_t)>(kIsValid);
    std::mt19937 rng(2);
    for (uint32_t month = 0; month < 15; ++month)
        for (uint32_t day = 0; day < 34; ++day)
            for (uint32_t year : {1900u, 1996u, 2000u, 2100u, 2023u, 0u}) {
                uint32_t a = orig(month, day, year), b = cRZDate::IsValid(month, day, year);
                Expect(ctx, "valid", month << 16 | day << 8 | (year & 0xff), a, b);
            }
    for (int i = 0; i < 3000; ++i) {
        uint32_t month = rng() % 3 ? rng() % 14 : rng(), day = rng() % 3 ? rng() % 35 : rng(), year = RandomYear(rng);
        Expect(ctx, "valid", month, orig(month, day, year), cRZDate::IsValid(month, day, year));
    }
}

DIFF_TEST(ReportText_ToJulianDay, kToJulian)
{
    auto orig = ctx.Original<uint32_t(__cdecl *)(uint32_t, uint32_t, uint32_t)>(kToJulian);
    std::mt19937 rng(3);
    for (uint32_t year : {1u, 4u, 100u, 400u, 1582u, 1899u, 1900u, 1970u, 1996u, 2000u, 2024u, 2100u, 9999u})
        for (uint32_t month = 0; month < 14; ++month)
            for (uint32_t day = 0; day < 33; ++day)
                Expect(ctx, "julian", month << 8 | day, orig(month, day, year), cRZDate::ToJulianDay(month, day, year));
    for (int i = 0; i < 6000; ++i) {
        uint32_t month = 1 + rng() % 12, day = 1 + rng() % 31, year = RandomYear(rng);
        if (rng() % 8 == 0) month = rng();
        Expect(ctx, "julian", year, orig(month, day, year), cRZDate::ToJulianDay(month, day, year));
    }
}

// ---- cRZDate construction ----

DIFF_TEST(ReportText_DateCopy, kCopy)
{
    auto orig = ctx.Original<DateObject *(__fastcall *)(DateObject *, void *, const DateObject *)>(kCopy);
    std::mt19937 rng(4);
    for (int i = 0; i < 200; ++i) {
        DateObject source, a, b;
        Garbage(source, rng);
        Garbage(a, rng);
        b = a;
        DateObject *ra = orig(&a, nullptr, &source);
        cRZDate *rb = reinterpret_cast<cRZDate *>(&b)->Copy(reinterpret_cast<const cRZDate *>(&source));
        ++ctx.cases;
        if (ra != &a || rb != reinterpret_cast<cRZDate *>(&b)) ctx.Fail("copy: return value is not this");
        if (a.julianDay != b.julianDay || a.vtable != b.vtable) ctx.Fail("copy: original %p/%x, ours %p/%x", a.vtable, a.julianDay, b.vtable, b.julianDay);
        if (a.vtable != ctx.image.At(cRZDate::kVTable)) ctx.Fail("copy: vtable is not the game's");
    }
}

DIFF_TEST(ReportText_DateCtorDays, kCtorDays)
{
    Fixture fixture(ctx);
    auto orig = ctx.Original<DateObject *(__fastcall *)(DateObject *, void *, uint32_t, uint32_t)>(kCtorDays);
    std::mt19937 rng(5);
    for (int i = 0; i < 3000; ++i) {
        uint32_t days = rng() % 3 ? rng() % 800000 : rng(), year = rng() % 4 == 0 ? 0 : RandomYear(rng);
        DateObject a, b;
        Garbage(a, rng);
        b = a;
        DateObject *ra = orig(&a, nullptr, days, year);
        cRZDate *rb = reinterpret_cast<cRZDate *>(&b)->ConstructDays(days, year);
        ++ctx.cases;
        if (ra != &a || rb != reinterpret_cast<cRZDate *>(&b)) ctx.Fail("ctor days: return value is not this");
        if (a.julianDay != b.julianDay || a.vtable != b.vtable) ctx.Fail("ctor days(%u, %u): original %x, ours %x", days, year, a.julianDay, b.julianDay);
    }
}

DIFF_TEST(ReportText_DateSet, kSet)
{
    Fixture fixture(ctx);
    auto orig = ctx.Original<void(__fastcall *)(DateObject *, void *, uint32_t, uint32_t)>(kSet);
    std::mt19937 rng(6);
    for (int i = 0; i < 3000; ++i) {
        uint32_t days = rng() % 3 ? rng() % 800000 : rng(), year = rng() % 4 == 0 ? 0 : RandomYear(rng);
        DateObject a, b;
        Garbage(a, rng);
        b = a;
        orig(&a, nullptr, days, year); // eax on return is the sum, which no caller uses
        reinterpret_cast<cRZDate *>(&b)->Set(days, year);
        ++ctx.cases;
        if (a.julianDay != b.julianDay || a.vtable != b.vtable) ctx.Fail("set(%u, %u): original %x, ours %x", days, year, a.julianDay, b.julianDay);
    }
}

DIFF_TEST(ReportText_DateCtorDate, kCtorDate)
{
    auto orig = ctx.Original<DateObject *(__fastcall *)(DateObject *, void *, uint32_t, uint32_t, uint32_t)>(kCtorDate);
    std::mt19937 rng(7);
    for (int i = 0; i < 4000; ++i) {
        uint32_t month = rng() % 5 ? 1 + rng() % 12 : rng() % 16, day = rng() % 5 ? 1 + rng() % 31 : rng() % 40, year = RandomYear(rng);
        DateObject a, b;
        Garbage(a, rng);
        b = a;
        DateObject *ra = orig(&a, nullptr, month, day, year);
        cRZDate *rb = reinterpret_cast<cRZDate *>(&b)->ConstructDate(month, day, year);
        ++ctx.cases;
        if (ra != &a || rb != reinterpret_cast<cRZDate *>(&b)) ctx.Fail("ctor date: return value is not this");
        if (a.julianDay != b.julianDay || a.vtable != b.vtable) ctx.Fail("ctor date(%u,%u,%u): original %x, ours %x", month, day, year, a.julianDay, b.julianDay);
    }
}

// ---- cRZDate fields ----

DIFF_TEST(ReportText_DateDecompose, kDecompose)
{
    auto orig = ctx.Original<void(__fastcall *)(const DateObject *, void *, uint32_t *, uint32_t *, uint32_t *)>(kDecompose);
    std::mt19937 rng(8);
    for (int i = 0; i < 8000; ++i) {
        DateObject d;
        Garbage(d, rng);
        d.julianDay = i < 2 ? i : RandomDay(rng);
        uint32_t a[3] = {0xA1A1A1A1, 0xA2A2A2A2, 0xA3A3A3A3}, b[3] = {0xA1A1A1A1, 0xA2A2A2A2, 0xA3A3A3A3};
        orig(&d, nullptr, &a[0], &a[1], &a[2]); // eax on return is a leftover
        reinterpret_cast<const cRZDate *>(&d)->Decompose(&b[0], &b[1], &b[2]);
        ++ctx.cases;
        if (std::memcmp(a, b, sizeof(a)) != 0)
            ctx.Fail("decompose(%x): original %u/%u/%u, ours %u/%u/%u", d.julianDay, a[0], a[1], a[2], b[0], b[1], b[2]);
    }
}

DIFF_TEST(ReportText_DateGetters, kGetYear)
{
    using Get = uint32_t(__fastcall *)(const DateObject *, void *);
    Get month = ctx.Original<Get>(kGetMonth), day = ctx.Original<Get>(kGetDay), year = ctx.Original<Get>(kGetYear), weekday = ctx.Original<Get>(kGetWeekday);
    std::mt19937 rng(9);
    for (int i = 0; i < 8000; ++i) {
        DateObject d;
        Garbage(d, rng);
        d.julianDay = RandomDay(rng);
        const cRZDate *ours = reinterpret_cast<const cRZDate *>(&d);
        Expect(ctx, "month", d.julianDay, month(&d, nullptr), ours->GetMonth());
        Expect(ctx, "day", d.julianDay, day(&d, nullptr), ours->GetDay());
        Expect(ctx, "year", d.julianDay, year(&d, nullptr), ours->GetYear());
        Expect(ctx, "weekday", d.julianDay, weekday(&d, nullptr), ours->GetWeekday());
    }
}

DIFF_TEST(ReportText_DatePreviousWeekday, kPrevWeekday)
{
    auto orig = ctx.Original<DateObject *(__fastcall *)(const DateObject *, void *, DateObject *, uint32_t)>(kPrevWeekday);
    std::mt19937 rng(10);
    for (int i = 0; i < 6000; ++i) {
        DateObject d;
        Garbage(d, rng);
        d.julianDay = RandomDay(rng);
        uint32_t weekday = rng() % 4 == 0 ? rng() : rng() % 11;
        DateObject a, b;
        Garbage(a, rng);
        b = a;
        DateObject *ra = orig(&d, nullptr, &a, weekday);
        cRZDate *rb = reinterpret_cast<const cRZDate *>(&d)->GetPreviousWeekday(reinterpret_cast<cRZDate *>(&b), weekday);
        ++ctx.cases;
        if (ra != &a || rb != reinterpret_cast<cRZDate *>(&b)) ctx.Fail("previous weekday: return value is not out");
        if (a.julianDay != b.julianDay || a.vtable != b.vtable) ctx.Fail("previous weekday(%x, %u): original %x, ours %x", d.julianDay, weekday, a.julianDay, b.julianDay);
    }
}

// ---- Format ----

namespace {

struct SprintfRecord {
    int calls = 0;
    const void *self = nullptr;
    std::string format;
    uint32_t args[3] = {0, 0, 0};
};
SprintfRecord gRecord;

void __cdecl RecordingSprintf(void *self, const char *format, uint32_t a, uint32_t b, uint32_t c)
{
    ++gRecord.calls;
    gRecord.self = self;
    gRecord.format = format;
    gRecord.args[0] = a;
    gRecord.args[1] = b;
    gRecord.args[2] = c;
}

void __cdecl UnexpectedSlot() { }

struct FakeString {
    const void *const *vtable;
};

} // namespace

DIFF_TEST(ReportText_DateFormat, kFormat)
{
    auto orig = ctx.Original<void(__fastcall *)(const DateObject *, void *, FakeString *, int32_t)>(kFormat);
    const void *vtable[34];
    for (const void *&slot : vtable) slot = reinterpret_cast<const void *>(&UnexpectedSlot);
    vtable[31] = reinterpret_cast<const void *>(&RecordingSprintf);
    FakeString out{vtable};
    std::mt19937 rng(11);
    std::vector<int32_t> formats;
    for (int32_t f = -3; f < 25; ++f) formats.push_back(f);
    for (int i = 0; i < 200; ++i) formats.push_back(static_cast<int32_t>(rng()));
    for (int round = 0; round < 6; ++round)
        for (int32_t format : formats) {
            DateObject d;
            Garbage(d, rng);
            d.julianDay = round == 0 ? 0x24db1a + 10000 : RandomDay(rng);
            SprintfRecord seen[2];
            gRecord = SprintfRecord();
            orig(&d, nullptr, &out, format);
            seen[0] = gRecord;
            gRecord = SprintfRecord();
            reinterpret_cast<const cRZDate *>(&d)->Format(reinterpret_cast<cIGZString *>(&out), format);
            seen[1] = gRecord;
            ++ctx.cases;
            if (seen[0].calls != seen[1].calls || seen[0].self != seen[1].self || seen[0].format != seen[1].format ||
                std::memcmp(seen[0].args, seen[1].args, sizeof(seen[0].args)) != 0)
                ctx.Fail("format %d of %x: original %d call(s) \"%s\" %u %u %u, ours %d call(s) \"%s\" %u %u %u", format, d.julianDay, seen[0].calls,
                         seen[0].format.c_str(), seen[0].args[0], seen[0].args[1], seen[0].args[2], seen[1].calls, seen[1].format.c_str(), seen[1].args[0],
                         seen[1].args[1], seen[1].args[2]);
        }
}

// ---- cRZTime ----

DIFF_TEST(ReportText_TimeFromDate, kFromDate)
{
    Fixture fixture(ctx);
    auto orig = ctx.Original<cRZTime *(__cdecl *)(cRZTime *, const DateObject *, uint32_t)>(kFromDate);
    std::mt19937 rng(12);
    for (int i = 0; i < 4000; ++i) {
        fixture.Set(RandomZone(rng), 1);
        DateObject d;
        Garbage(d, rng);
        d.julianDay = RandomDay(rng);
        uint32_t hours = rng() % 3 ? rng() % 48 : rng();
        cRZTime a{0xA1A1A1A1}, b{0xA1A1A1A1};
        cRZTime *ra = orig(&a, &d, hours);
        cRZTime *rb = cRZTime::FromDate(&b, reinterpret_cast<const cRZDate *>(&d), hours);
        ++ctx.cases;
        if (ra != &a || rb != &b) ctx.Fail("from date: return value is not out");
        Expect(ctx, "from date", d.julianDay, a.seconds, b.seconds);
    }
}

DIFF_TEST(ReportText_TimeDaylightStart, kDstStart)
{
    Fixture fixture(ctx);
    auto orig = ctx.Original<cRZTime *(__cdecl *)(cRZTime *, uint32_t)>(kDstStart);
    std::mt19937 rng(13);
    for (int i = 0; i < 6000; ++i) {
        fixture.Set(RandomZone(rng), 1);
        uint32_t year = i < 3000 ? i : RandomYear(rng);
        if (i >= 3000 && i < 3100) year = 1970 + i % 30;
        cRZTime a{0xA1A1A1A1}, b{0xA1A1A1A1};
        cRZTime *ra = orig(&a, year), *rb = cRZTime::DaylightStart(&b, year);
        ++ctx.cases;
        if (ra != &a || rb != &b) ctx.Fail("daylight start: return value is not out");
        Expect(ctx, "daylight start", year, a.seconds, b.seconds);
    }
}

DIFF_TEST(ReportText_TimeDaylightEnd, kDstEnd)
{
    Fixture fixture(ctx);
    auto orig = ctx.Original<cRZTime *(__cdecl *)(cRZTime *, uint32_t)>(kDstEnd);
    std::mt19937 rng(14);
    for (int i = 0; i < 6000; ++i) {
        fixture.Set(RandomZone(rng), 1);
        uint32_t year = i < 3000 ? i : RandomYear(rng);
        cRZTime a{0xA1A1A1A1}, b{0xA1A1A1A1};
        cRZTime *ra = orig(&a, year), *rb = cRZTime::DaylightEnd(&b, year);
        ++ctx.cases;
        if (ra != &a || rb != &b) ctx.Fail("daylight end: return value is not out");
        Expect(ctx, "daylight end", year, a.seconds, b.seconds);
    }
}

namespace {

// A moment: random, or within a few hours of a daylight-saving boundary of a year (computed by the
// original's own helpers, so the boundary comes from the code under test's reference).
uint32_t RandomMoment(difftest::Context &ctx, std::mt19937 &rng)
{
    auto start = ctx.Original<cRZTime *(__cdecl *)(cRZTime *, uint32_t)>(kDstStart);
    auto end = ctx.Original<cRZTime *(__cdecl *)(cRZTime *, uint32_t)>(kDstEnd);
    switch (rng() % 4) {
    case 0: return rng();
    case 1: return rng() % 0x7fffffff;
    default: {
        cRZTime boundary{0};
        uint32_t year = 1970 + rng() % 80;
        (rng() % 2 ? start : end)(&boundary, year);
        if (rng() % 3 == 0) return boundary.seconds + static_cast<uint32_t>(static_cast<int32_t>(rng() % 3) - 1); // exactly on it
        return boundary.seconds + static_cast<uint32_t>(static_cast<int32_t>(rng() % 20000) - 10000);
    }
    }
}

} // namespace

DIFF_TEST(ReportText_TimeIsDaylightSaving, kIsDst)
{
    Fixture fixture(ctx);
    auto orig = ctx.Original<uint32_t(__fastcall *)(const cRZTime *, void *)>(kIsDst);
    std::mt19937 rng(15);
    for (int i = 0; i < 8000; ++i) {
        fixture.Set(RandomZone(rng), RandomDaylight(rng));
        cRZTime t{RandomMoment(ctx, rng)};
        Expect(ctx, "is dst", t.seconds, orig(&t, nullptr), t.IsDaylightSaving());
    }
}

DIFF_TEST(ReportText_TimeLocalSeconds, kLocalSeconds)
{
    Fixture fixture(ctx);
    using Get = uint32_t(__fastcall *)(const cRZTime *, void *);
    Get local = ctx.Original<Get>(kLocalSeconds), hour = ctx.Original<Get>(kGetHour), minute = ctx.Original<Get>(kGetMinute);
    std::mt19937 rng(16);
    for (int i = 0; i < 8000; ++i) {
        fixture.Set(RandomZone(rng), RandomDaylight(rng));
        cRZTime t{RandomMoment(ctx, rng)};
        Expect(ctx, "local seconds", t.seconds, local(&t, nullptr), t.LocalSeconds());
        Expect(ctx, "hour", t.seconds, hour(&t, nullptr), t.GetHour());
        Expect(ctx, "minute", t.seconds, minute(&t, nullptr), t.GetMinute());
    }
}

DIFF_TEST(ReportText_TimeCtor, kTimeCtor)
{
    Fixture fixture(ctx);
    auto orig = ctx.Original<cRZTime *(__fastcall *)(cRZTime *, void *, const DateObject *, uint32_t, uint32_t, uint32_t)>(kTimeCtor);
    auto start = ctx.Original<cRZTime *(__cdecl *)(cRZTime *, uint32_t)>(kDstStart);
    auto end = ctx.Original<cRZTime *(__cdecl *)(cRZTime *, uint32_t)>(kDstEnd);
    std::mt19937 rng(17);
    for (int i = 0; i < 12000; ++i) {
        fixture.Set(RandomZone(rng), RandomDaylight(rng));
        DateObject d;
        Garbage(d, rng);
        d.julianDay = RandomDay(rng);
        uint32_t hour = rng() % 24, minute = rng() % 60, second = rng() % 60;
        if (rng() % 6 == 0) {
            hour = rng() % 40;
            minute = rng();
            second = rng();
        }
        if (i % 3 == 0) { // the day of a daylight-saving boundary, around the changeover hour
            cRZTime boundary{0};
            uint32_t year = 1970 + rng() % 70;
            (rng() % 2 ? start : end)(&boundary, year);
            uint32_t days = boundary.seconds / 86400;
            d.julianDay = cRZDate::kEpochDay + days + rng() % 3 - 1;
            hour = rng() % 5;
        }
        cRZTime a{0xA1A1A1A1}, b{0xA1A1A1A1};
        cRZTime *ra = orig(&a, nullptr, &d, hour, minute, second);
        cRZTime *rb = b.Construct(reinterpret_cast<const cRZDate *>(&d), hour, minute, second);
        ++ctx.cases;
        if (ra != &a || rb != &b) ctx.Fail("time ctor: return value is not this");
        Expect(ctx, "time ctor", d.julianDay, a.seconds, b.seconds);
    }
}

namespace {

// Both sides read the real clock, so their results may differ by the time between the calls.
void ExpectClose(difftest::Context &ctx, const char *what, uint32_t original, uint32_t ours)
{
    ++ctx.cases;
    uint32_t diff = original > ours ? original - ours : ours - original;
    if (diff > 2) ctx.Fail("%s: original %u, ours %u", what, original, ours);
}

} // namespace

DIFF_TEST(ReportText_TimeNow, kTimeNow)
{
    Fixture fixture(ctx, true);
    auto orig = ctx.Original<cRZTime *(__fastcall *)(cRZTime *, void *)>(kTimeNow);
    std::mt19937 rng(18);
    for (int i = 0; i < 60; ++i) {
        fixture.Set(RandomZone(rng), RandomDaylight(rng));
        cRZTime a{0xA1A1A1A1}, b{0xA1A1A1A1};
        cRZTime *ra = orig(&a, nullptr);
        cRZTime *rb = b.ConstructNow();
        ++ctx.cases;
        if (ra != &a || rb != &b) ctx.Fail("now: return value is not this");
        ExpectClose(ctx, "now", a.seconds, b.seconds);
    }
}

DIFF_TEST(ReportText_DateToday, kCtorToday)
{
    Fixture fixture(ctx, true);
    auto orig = ctx.Original<DateObject *(__fastcall *)(DateObject *, void *)>(kCtorToday);
    std::mt19937 rng(19);
    for (int i = 0; i < 20; ++i) {
        DateObject a, b;
        Garbage(a, rng);
        b = a;
        DateObject *ra = orig(&a, nullptr);
        cRZDate *rb = reinterpret_cast<cRZDate *>(&b)->ConstructToday();
        ++ctx.cases;
        if (ra != &a || rb != reinterpret_cast<cRZDate *>(&b)) ctx.Fail("today: return value is not this");
        // ORIGINAL-BUG (see ReportText.h): the date is Julian day 0 on both sides.
        if (a.julianDay != b.julianDay || a.vtable != b.vtable) ctx.Fail("today: original %x, ours %x", a.julianDay, b.julianDay);
        if (a.julianDay != 0) ctx.Fail("today: expected the original's Julian day 0, got %x", a.julianDay);
    }
}

DIFF_TEST(ReportText_SecondsSinceGuidEpoch, kGuidSeconds)
{
    Fixture fixture(ctx, true);
    auto orig = ctx.Original<uint32_t(__cdecl *)()>(kGuidSeconds);
    std::mt19937 rng(20);
    for (int i = 0; i < 40; ++i) {
        fixture.Set(RandomZone(rng), RandomDaylight(rng));
        uint32_t a = orig();
        uint32_t b = appbase::SecondsSinceGuidEpoch();
        ExpectClose(ctx, "guid seconds", a, b);
    }
}
