#pragma once
// What the exception reporter's Report needs for the file names and the text report: the game's
// calendar date and time-of-day classes (the time stamp in the report and dump file names), the
// time-zone / daylight-saving rules they apply, and the report's helpers.
//
// Both classes are tiny value objects. cRZDate carries the game's vtable (0xda311c; slots 0 and 1 are
// `ret 8` stubs, slot 2 is Format) and a Julian day number; cRZTime is a bare count of seconds since
// the Unix epoch (UTC), with local-time views derived from the C runtime's time zone variables.
#include "gz/cIGZString.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>

namespace appbase {

struct cRZTime;

struct cRZDate {
    static constexpr uint32_t kVTable = 0xda311c;
    // The Julian day number of cRZDate(0, 0): the epoch the time class counts from. The game builds
    // the global holding it (0x10cafdc, read by cRZTime at +4) in a static initialiser.
    static constexpr uint32_t kEpochDay = 0x24db1a;

    // 0x1b995c (__cdecl): Gregorian leap year. Returns 0 or 1.
    static uint32_t __cdecl IsLeapYear(uint32_t year);
    // 0x1b98a4 (__cdecl): 1 when the month is 1..12 and the day is 1 up to that month's length (the
    // year only matters for February; it is not checked otherwise), else 0.
    static uint32_t __cdecl IsValid(uint32_t month, uint32_t day, uint32_t year);
    // 0x1b98e2 (__cdecl): the Julian day number of a date, 0 for an invalid one.
    static uint32_t __cdecl ToJulianDay(uint32_t month, uint32_t day, uint32_t year);

    // 0x1b969a (__thiscall, ret 4): copy constructor. Returns this.
    cRZDate *Copy(const cRZDate *other);
    // 0x1b96af (__thiscall, ret 8): `days` after the start of `year`, or after the epoch when the
    // year is 0 (through Set). Returns this.
    cRZDate *ConstructDays(uint32_t days, uint32_t year);
    // 0x1b96cb (__thiscall, ret 0xc): the date; an invalid one becomes Julian day 0. Returns this.
    cRZDate *ConstructDate(uint32_t month, uint32_t day, uint32_t year);
    // 0x1b96f1 (__thiscall): today's local date. Returns this.
    // ORIGINAL-BUG: the time value handed to localtime is read as 64 bits from a 32-bit stack slot,
    // so its upper half is the caller's saved frame pointer. That is never a valid time_t, the
    // conversion fails and leaves every field -1, and the date becomes month 0, which is invalid:
    // Julian day 0 (the report and dump names get a nonsense year/month/day, the time of day is
    // right because it comes from cRZTime).
    cRZDate *ConstructToday();
    // 0x1b9a8b (__thiscall, ret 8): Julian day = days + (year == 0 ? kEpochDay : day 31 December of
    // year - 1).
    void Set(uint32_t days, uint32_t year);
    // 0x1b998a (__thiscall, ret 0xc): the calendar fields of the Julian day.
    void Decompose(uint32_t *month, uint32_t *day, uint32_t *year) const;
    // 0x1b9a26 / 0x1b9888 / 0x1b9ad0 (__thiscall): the month (1..12), day and year.
    uint32_t GetMonth() const;
    uint32_t GetDay() const;
    uint32_t GetYear() const;
    // 0x1b9aba (__thiscall): the day of the week, 1..7 (7 is Sunday).
    uint32_t GetWeekday() const;
    // 0x1b9a53 (__thiscall, ret 8): `out` becomes the latest date on or before this one that falls on
    // the given weekday (1..7; the argument is compared as an unsigned zero-based index). Returns out.
    cRZDate *GetPreviousWeekday(cRZDate *out, uint32_t weekday) const;
    // 0x1b9738 (__thiscall, ret 8, vtable slot 2): writes the date into `out` through its Sprintf slot
    // (31) in one of several layouts: 1 and -1 (and 0): M/D/Y; 3, 4, 10, 11: D.M.Y; 7, 8, 14: D-M-Y;
    // 9: Y.M.D; 15: Y/M/D; every other value: D/M/Y.
    void Format(cIGZString *out, int32_t format) const;

    const void *vtable;   // 0x00: the game's vtable
    uint32_t julianDay;   // 0x04
};

static_assert(sizeof(cRZDate) == 8, "cRZDate size");
static_assert(offsetof(cRZDate, julianDay) == 4, "cRZDate layout");

struct cRZTime {
    // 0x1452d5 (__cdecl): out = ((day - epoch) * 24 + hours) * 3600 + the C runtime's time zone
    // offset (seconds west of UTC). Returns out.
    static cRZTime *__cdecl FromDate(cRZTime *out, const cRZDate *date, uint32_t hours);
    // 0x145268 (__cdecl): the moment daylight saving starts in `year`: 2:00 on 6 Jan 1974 / 23 Feb
    // 1975, otherwise on the Sunday on or before 8 April. Returns out.
    static cRZTime *__cdecl DaylightStart(cRZTime *out, uint32_t year);
    // 0x145455 (__cdecl): the moment it ends: 1:00 on the Sunday on or before 31 October. Returns out.
    static cRZTime *__cdecl DaylightEnd(cRZTime *out, uint32_t year);

    // 0x144f81 (__thiscall, ret 0x10): the time of day on a date as UTC seconds. A date with Julian
    // day 0 gives 0; otherwise the local time is converted with the time zone and the daylight
    // saving rule (two checks, one hour either side; a time inside the spring gap gives 0). Returns this.
    cRZTime *Construct(const cRZDate *date, uint32_t hour, uint32_t minute, uint32_t second);
    // 0x144ff4 (__thiscall): the current time. Returns this.
    cRZTime *ConstructNow();
    // 0x14583b (__thiscall): whether daylight saving applies at this moment (0 or 1): the C runtime's
    // daylight flag is set and the moment lies in [start, end) of its year.
    uint32_t IsDaylightSaving() const;
    // 0x145ad1 (__thiscall): this moment in local time, as seconds (zone offset, then an hour more
    // under daylight saving, tested on the zone-shifted value).
    uint32_t LocalSeconds() const;
    // 0x145821 / 0x145afc (__thiscall): the local hour (0..23) and minute (0..59).
    uint32_t GetHour() const;
    uint32_t GetMinute() const;

    uint32_t seconds; // 0x00
};

static_assert(sizeof(cRZTime) == 4, "cRZTime size");

// 0x145774 (__cdecl): the current local time as seconds since 2 February 1997 (the GUID generator's
// epoch): days between that date and today, plus the local hour, minute and second.
uint32_t __cdecl SecondsSinceGuidEpoch();

// 0x1bbb10 (__stdcall, ret 8): the name of an exception code, without the "EXCEPTION_" prefix
// (ACCESS_VIOLATION, STACK_OVERFLOW, ... the 22 codes of the report), assigned to the string inside
// `out` (the std::string at +4). Any other code asks FormatMessageA for its text from NTDLL.DLL's
// message table (flags 0xa00: from module, ignore inserts; 0x1ff characters at most, the buffer is
// zeroed first) and assigns that. No return value.
void __stdcall DescribeExceptionCode(uint32_t code, cRZString *out);

} // namespace appbase
