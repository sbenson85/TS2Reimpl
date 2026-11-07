#pragma once
#include "game/owned.h"
#include "rz/cRZString.h"

#include <cstdint>

// The string key parser (0xae6e60) and the functions it calls directly: CRC tables and hashes, a
// strtoul wrapper, a bsearch over the keyword table, the cached service object, and the comparator
// used by that bsearch. The cRZString helpers 0x1ad167, 0x1acd5b and 0x1acd83 belong to the
// cRZString group but are used only here so far, so they live in these files too.
//
// Calling conventions are the original's (RVAs are 1.18.0.187):
//   cRZString helpers and CrcTables::Initialize: __thiscall; the rest __cdecl unless noted.

// Non-virtual helpers of cRZString. `this` is the cRZString (the original's ecx); the struct holds
// no data, so the same pointer is used as a cRZString.
struct cRZStringHelpers {
    // 0x1ad167 (__thiscall, no stack arguments): lowercases the string in place (_strlwr).
    void LowerCase();

    // 0x1acd5b (__thiscall, 2 stack arguments, ret 8): other is a cIGZString; asks it for its length
    // (vtable slot 7) and its characters (slot 5), then calls EndsWith(data, length, caseSensitive).
    bool EndsWithObject(void *other, uint8_t caseSensitive);

    // 0x1acd83 (__thiscall, 3 stack arguments, ret 0xc): true when the last `length` characters of
    // the string equal text[0..length). With caseSensitive == 0 both are lowercased first (on copies).
    bool EndsWith(const char *text, uint32_t length, uint8_t caseSensitive);
};

namespace StringKeyParser {

// The out object of ParseKey: four dwords (the original's offsets +0, +4, +8, +0xc).
struct Result {
    uint32_t value;   // +0: the preset when given, else the keyword table's value (0 when none)
    uint32_t id;      // +4: the value of the name, the number, or the service lookup
    uint32_t hash24;  // +8: CRC24 of the local text, | 0xff000000 (masked to 24 bits when found in the table)
    uint32_t crc32;   // +0xc: CRC32 of the local text, 0 when it is empty
};
static_assert(sizeof(Result) == 0x10, "StringKeyParser::Result layout");

// 0x9cbb60 (__thiscall; returns its `this` unchanged): builds the CRC24 and CRC32 tables once, into
// game globals 0x10f51d0 and 0x10f55d0, guarded by the byte at 0x10f59d0.
struct CrcTables {
    void *Initialize();
};

// 0x9cbbdd (__cdecl): CRC24 of a NUL-terminated string (signed chars; lowercased with tolower when
// lower != 0), seeded with 0xb704ce, result masked to 24 bits.
uint32_t Crc24(const char *text, uint8_t lower);

// 0x9cbc3e (__cdecl): CRC32 (MSB-first, poly 0x04c11db7, seed 0xffffffff, no final xor), same
// character rules as Crc24.
uint32_t Crc32(const char *text, uint8_t lower);

// 0x1b10f3 (__cdecl): strtoul(text, null, base), base 16 after an optional "0x"/"0X" prefix, else 10.
uint32_t ParseNumber(const char *text);

// 0xc4ad97 is the CRT's bsearch (db/crt-equivalents.jsonl): the parser calls ours.

// 0xae7300 (__cdecl, no arguments): if the cached object at game global 0x10f8f2c is null, asks
// the service object (GetServiceObject) for it through vtable slot 5 with the identifiers
// 0x056b906e and 0x2efe1873; on success takes a reference on the cached object (its slot 1).
void EnsureServiceObject();

// 0xae7460 (__cdecl): bsearch comparator over {uint32 hash, const char *text} entries: the hash
// difference (wrapped to 32 bits) when the hashes differ, else _stricmp of the texts.
int32_t CompareEntry(const void *key, const void *entry);

// 0xae6e60 (__cdecl, 4 stack arguments): parses a key like "name!text", "*!text", "#num!text",
// "##num!text" or "text". Fills `out`, always returns true (al = 1). `preset` is returned as
// out->value when non-zero; `ext` is the name used by the "*" form (ignored when null or empty).
bool ParseKey(Result *out, const char *key, uint32_t preset, const char *ext);

// An entry of the parser's name table (14 of them, sorted by hash): the CRC24 hash of the lowercased text, | 0xff000000.
struct HashedText {
    uint32_t hash;
    const char *text;
};

} // namespace StringKeyParser

// The parser's statics (owned.h): the image's objects in the hooked game, standalone/owned_statics.cpp's otherwise.
// The CRC tables are filled on first use by CrcTables::Initialize's body (the flag first, as the original does), so
// an image-free program starts them zeroed. The name table is data (hash, text) in the program.
TS2_OWNED_STATIC(uint32_t, 10f51d0)[256];            // CRC24 table
TS2_OWNED_STATIC(uint32_t, 10f55d0)[256];            // CRC32 table
TS2_OWNED_STATIC(uint8_t, 10f59d0);                  // the CRC tables are built
TS2_OWNED_STATIC(void *, 10f8f2c);                   // the service object ParseKey's "*!" and "name!" forms ask (cached)
TS2_OWNED_STATIC(StringKeyParser::HashedText, 10718c8)[14]; // the name table
