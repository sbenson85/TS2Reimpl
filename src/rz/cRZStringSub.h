#pragma once
#include "rz/cRZString.h"

#include <cstdint>

// The substring helpers of cRZString (non-virtual methods in the original, not vtable slots; lane LZ). Each
// builds a new cRZString in `out` (raw memory, constructed here) and returns `out`. All __thiscall on the source
// string. Positions and counts are in bytes and unsigned.
struct cRZStringSub : cRZString {
    // 0x1ad18d (ret 0xc): the `count` bytes from `start` (fewer when the string ends first); a start at or past
    // the end gives an empty string.
    cRZString *Mid(cRZString *out, uint32_t start, uint32_t count) const;
    // 0x1ad13b (ret 8): the first `count` bytes; a count longer than the string copies all of it.
    cRZString *Left(cRZString *out, uint32_t count) const;
    // 0x1ad3ed (ret 8): the last `count` bytes; a count longer than the string copies all of it.
    cRZString *Right(cRZString *out, uint32_t count) const;
};
static_assert(sizeof(cRZStringSub) == sizeof(cRZString), "cRZStringSub adds no data");

inline const cRZStringSub &Sub(const cRZString &text)
{
    return static_cast<const cRZStringSub &>(text);
}
