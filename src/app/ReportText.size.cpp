#define _CRT_SECURE_NO_WARNINGS
#include "app/ReportText.h"

#include "game/game.h"

#define NOMINMAX
#include <windows.h>

#include <intrin.h>
#include <cstring>
#include <time.h>

namespace appbase {
namespace {

constexpr uint8_t kDaysInMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}; // table at 0xda30ac

// Format strings of cRZDate::Format (rdata at 0xda3128, 0xda3134, 0xda3140, 0xda314c, 0xda3158).
const char kFormatSlash[] = "%u/%u/%4u";
const char kFormatDash[] = "%u-%u-%4u";
const char kFormatDot[] = "%u.%u.%4u";
const char kFormatYearDot[] = "%4u.%u.%u";
const char kFormatYearSlash[] = "%4u/%u/%u";

// The C runtime's time zone variables the game's code reads through two tiny accessors
// (0x82f83e: seconds west of UTC; 0x82f832: the daylight flag).
int32_t TimeZone()
{
    return static_cast<int32_t>(*__timezone());
}

int32_t DaylightFlag()
{
    return *__daylight();
}

} // namespace

// ---- cRZDate ----

uint32_t __cdecl cRZDate::IsLeapYear(uint32_t year)
{
    if ((year & 3) == 0 && year % 100 != 0) return 1;
    return year % 400 == 0 ? 1 : 0;
}

uint32_t __cdecl cRZDate::IsValid(uint32_t month, uint32_t day, uint32_t year)
{
    if (day == 0 || month == 0 || month - 1 > 11) return 0;
    uint32_t length = kDaysInMonth[month - 1];
    if (IsLeapYear(year) && month == 2) ++length;
    return length >= day ? 1 : 0;
}

uint32_t __cdecl cRZDate::ToJulianDay(uint32_t month, uint32_t day, uint32_t year)
{
    uint32_t result = IsValid(month, day, year);
    if (!result) return result;
    uint32_t y = month <= 2 ? year - 1 : year;
    uint32_t century = y / 100;
    uint32_t inCentury = y - century * 100;
    uint32_t monthTerm = ((month > 2 ? 0xfffff8d4u : 0u) + 0x563 + month * 0x99) / 5;
    return monthTerm + 0x1a431f + ((inCentury * 0x5b5) >> 2) + day + ((century * 0x23ab1) >> 2);
}

cRZDate *cRZDate::Copy(const cRZDate *other)
{
    vtable = GameAddress<void>(kVTable);
    julianDay = other->julianDay;
    return this;
}

cRZDate *cRZDate::ConstructDays(uint32_t days, uint32_t year)
{
    vtable = GameAddress<void>(kVTable);
    Set(days, year);
    return this;
}

cRZDate *cRZDate::ConstructDate(uint32_t month, uint32_t day, uint32_t year)
{
    vtable = GameAddress<void>(kVTable);
    julianDay = ToJulianDay(month, day, year);
    return this;
}

cRZDate *cRZDate::ConstructToday()
{
    vtable = GameAddress<void>(kVTable);
    // The original keeps the low half of time(0) in a 32-bit local and gives localtime its address,
    // which reads 64 bits: the next four bytes on its stack are the saved frame pointer.
    struct {
        uint32_t low;
        uint32_t high;
    } slot;
    slot.low = static_cast<uint32_t>(_time64(nullptr));
    slot.high = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(_AddressOfReturnAddress())) - 4;
    __time64_t value;
    static_assert(sizeof(slot) == sizeof(value), "time slot");
    std::memcpy(&value, &slot, sizeof(value));
    struct tm local;
    _localtime64_s(&local, &value);
    julianDay = ToJulianDay(local.tm_mon + 1, local.tm_mday, local.tm_year + 0x76c);
    return this;
}

void cRZDate::Set(uint32_t days, uint32_t year)
{
    uint32_t base = year == 0 ? kEpochDay : ToJulianDay(12, 31, year - 1);
    julianDay = days + base;
}

void cRZDate::Decompose(uint32_t *month, uint32_t *day, uint32_t *year) const
{
    uint32_t esi = julianDay * 4 - 0x690c7c;
    uint32_t quad = (esi - 1) / 0x23ab1;
    esi -= quad * 0x23ab1;
    esi = (esi - 1) & 0xfffffffc;
    uint32_t block = (esi + 3) / 0x5b5;
    esi -= block * 0x5b5;
    esi = ((esi + 7) >> 2) * 5;
    uint32_t m = (esi - 3) / 0x99;
    esi -= m * 0x99;
    *day = (esi + 2) / 5;
    uint32_t y = quad * 100 + block;
    if (m < 10) {
        m += 3;
    } else {
        m -= 9;
        ++y;
    }
    *month = m;
    *year = y;
}

uint32_t cRZDate::GetMonth() const
{
    uint32_t month, day, year;
    Decompose(&month, &day, &year);
    return month;
}

uint32_t cRZDate::GetDay() const
{
    uint32_t month, day, year;
    Decompose(&month, &day, &year);
    return day;
}

uint32_t cRZDate::GetYear() const
{
    uint32_t month, day, year;
    Decompose(&month, &day, &year);
    return year;
}

uint32_t cRZDate::GetWeekday() const
{
    return ((julianDay + 1) % 7 + 6) % 7 + 1;
}

cRZDate *cRZDate::GetPreviousWeekday(cRZDate *out, uint32_t weekday) const
{
    uint32_t target = weekday - 1;
    uint32_t current = GetWeekday() - 1;
    uint32_t back = current - target;
    if (!(target <= current)) back += 7;
    out->vtable = GameAddress<void>(kVTable);
    out->julianDay = julianDay - back;
    return out;
}

void cRZDate::Format(cIGZString *out, int32_t format) const
{
    uint32_t selector = format ? static_cast<uint32_t>(format) : 1;
    uint32_t month, day, year;
    Decompose(&month, &day, &year);
    switch (selector) {
    case 1:
    case 0xffffffffu:
        out->Sprintf(kFormatSlash, month, day, year);
        break;
    case 3:
    case 4:
    case 10:
    case 11:
        out->Sprintf(kFormatDot, day, month, year);
        break;
    case 7:
    case 8:
    case 14:
        out->Sprintf(kFormatDash, day, month, year);
        break;
    case 9:
        out->Sprintf(kFormatYearDot, year, month, day);
        break;
    case 15:
        out->Sprintf(kFormatYearSlash, year, month, day);
        break;
    default:
        out->Sprintf(kFormatSlash, day, month, year);
        break;
    }
}

// ---- cRZTime ----

cRZTime *__cdecl cRZTime::FromDate(cRZTime *out, const cRZDate *date, uint32_t hours)
{
    out->seconds = ((date->julianDay - cRZDate::kEpochDay) * 24 + hours) * 0xe10 + static_cast<uint32_t>(TimeZone());
    return out;
}

cRZTime *__cdecl cRZTime::DaylightStart(cRZTime *out, uint32_t year)
{
    cRZDate base, result;
    base.ConstructDate(4, 8, year);
    const cRZDate *date;
    if (year == 0x7b6) {
        date = result.ConstructDate(1, 6, 0x7b6);
    } else if (year == 0x7b7) {
        date = result.ConstructDate(2, 0x17, 0x7b7);
    } else {
        date = base.GetPreviousWeekday(&result, 7);
    }
    FromDate(out, date, 2);
    return out;
}

cRZTime *__cdecl cRZTime::DaylightEnd(cRZTime *out, uint32_t year)
{
    cRZDate base, result;
    base.ConstructDate(10, 0x1f, year);
    FromDate(out, base.GetPreviousWeekday(&result, 7), 1);
    return out;
}

cRZTime *cRZTime::Construct(const cRZDate *date, uint32_t hour, uint32_t minute, uint32_t second)
{
    if (date->julianDay == 0) {
        seconds = 0;
        return this;
    }
    uint32_t value = (((date->julianDay - cRZDate::kEpochDay) * 24 + hour) * 0x3c + minute) * 0x3c + second - 0xe10;
    seconds = value;
    if (value != 0) seconds += static_cast<uint32_t>(TimeZone());
    uint32_t first = IsDaylightSaving();
    seconds += 0xe10;
    if (first) {
        if (IsDaylightSaving()) seconds -= 0xe10;
    } else {
        if (IsDaylightSaving()) seconds = 0;
    }
    return this;
}

cRZTime *cRZTime::ConstructNow()
{
    __time64_t now;
    _time64(&now);
    struct tm local;
    _localtime64_s(&local, &now);
    cRZDate date;
    date.ConstructDate(local.tm_mon + 1, local.tm_mday, local.tm_year + 0x76c);
    cRZTime built;
    built.Construct(&date, local.tm_hour, local.tm_min, local.tm_sec);
    seconds = built.seconds;
    return this;
}

uint32_t cRZTime::IsDaylightSaving() const
{
    if (DaylightFlag() == 0) return 0;
    cRZDate date;
    date.ConstructDays(seconds / 0x15180, 0);
    uint32_t year = date.GetYear();
    cRZTime start, end;
    if (seconds < DaylightStart(&start, year)->seconds) return 0;
    if (seconds >= DaylightEnd(&end, year)->seconds) return 0;
    return 1;
}

uint32_t cRZTime::LocalSeconds() const
{
    uint32_t shifted = seconds - static_cast<uint32_t>(TimeZone());
    cRZTime moved;
    moved.seconds = shifted;
    return moved.IsDaylightSaving() == 0 ? shifted : shifted + 0xe10;
}

uint32_t cRZTime::GetHour() const
{
    return LocalSeconds() % 0x15180 / 0xe10;
}

uint32_t cRZTime::GetMinute() const
{
    return LocalSeconds() % 0xe10 / 0x3c;
}

uint32_t __cdecl SecondsSinceGuidEpoch()
{
    cRZDate origin, today;
    origin.ConstructDate(2, 2, 0x7cd);
    today.ConstructToday();
    cRZTime now;
    now.ConstructNow();
    uint32_t days = today.julianDay - origin.julianDay;
    uint32_t minutes = (now.GetHour() + days * 0x18) * 0x3c + now.GetMinute();
    return minutes * 0x3c + now.seconds % 0x3c;
}

// ---- 0x1bbb10 ----

namespace {

constexpr uint32_t kIatGetModuleHandleA = 0xd9a114, kIatFormatMessageA = 0xd9a134;

template <typename Fn> Fn Import(uint32_t iatRva)
{
    return *GameAddress<Fn>(iatRva);
}

const char *ExceptionCodeName(uint32_t code)
{
    switch (code) {
    case 0xc0000005: return "ACCESS_VIOLATION";
    case 0x80000002: return "DATATYPE_MISALIGNMENT";
    case 0x80000003: return "BREAKPOINT";
    case 0x80000004: return "SINGLE_STEP";
    case 0xc000008c: return "ARRAY_BOUNDS_EXCEEDED";
    case 0xc000008d: return "FLT_DENORMAL_OPERAND";
    case 0xc000008e: return "FLT_DIVIDE_BY_ZERO";
    case 0xc000008f: return "FLT_INEXACT_RESULT";
    case 0xc0000090: return "FLT_INVALID_OPERATION";
    case 0xc0000091: return "FLT_OVERFLOW";
    case 0xc0000092: return "FLT_STACK_CHECK";
    case 0xc0000093: return "FLT_UNDERFLOW";
    case 0xc0000094: return "INT_DIVIDE_BY_ZERO";
    case 0xc0000095: return "INT_OVERFLOW";
    case 0xc0000096: return "PRIV_INSTRUCTION";
    case 0xc0000006: return "IN_PAGE_ERROR";
    case 0xc000001d: return "ILLEGAL_INSTRUCTION";
    case 0xc0000025: return "NONCONTINUABLE_EXCEPTION";
    case 0xc00000fd: return "STACK_OVERFLOW";
    case 0xc0000026: return "INVALID_DISPOSITION";
    case 0x80000001: return "GUARD_PAGE";
    case 0xc0000008: return "INVALID_HANDLE";
    default: return nullptr;
    }
}

} // namespace

void __stdcall DescribeExceptionCode(uint32_t code, cRZString *out)
{
    if (const char *name = ExceptionCodeName(code)) {
        out->mString.assign(name, std::strlen(name));
        return;
    }
    char text[0x200];
    std::memset(text, 0, sizeof(text));
    void *module = Import<void *(WINAPI *)(const char *)>(kIatGetModuleHandleA)("NTDLL.DLL");
    Import<DWORD(WINAPI *)(DWORD, const void *, DWORD, DWORD, char *, DWORD, void *)>(kIatFormatMessageA)(0xa00, module, code, 0, text, 0x1ff, nullptr);
    text[0x1ff] = 0;
    out->mString.assign(text, std::strlen(text));
}

} // namespace appbase
