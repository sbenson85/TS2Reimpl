#include "rz/StringKeyParser.h"
#include "game/game.h"
#include "game/vtable.h"
#include "rz/Singleton_20ddfb.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctype.h>
#include <string.h>

// Invalid-parameter handler of the CRT (library code, the same one the original calls).
extern "C" void __cdecl _invalid_parameter_noinfo(void);

namespace {

// The parser's statics are owned (StringKeyParser.h): ts2_rva_10f51d0 and ts2_rva_10f55d0 are the CRC24 and CRC32
// tables (256 dwords each), ts2_rva_10f59d0 their built byte, ts2_rva_10f8f2c the cached service object and
// ts2_rva_10718c8 the 14-entry name table.
constexpr uint32_t kEntryCount = 14;

constexpr uint32_t kCrc24Poly = 0x1864cfb;
constexpr uint32_t kCrc24Seed = 0xb704ce;
constexpr uint32_t kCrc32Poly = 0x4c11db7;

// The 5-character resource-type suffixes the parser knows (the originals' literals sit at 0xe7ce3c, 0xdbde70, 0xe7ce44,
// 0xdbd570, 0xe71dc4, 0xe696b0, 0xe730f0, 0xdb0810, 0xdb0818, 0xe7ce4c, 0xe7ce54 and 0xe76390), each with the value
// returned when it ends the local text.
struct Keyword {
    const char *text;
    uint32_t value;
};
constexpr Keyword kKeywords[] = {
    {"_scen", 0x25232b11}, {"_cres", 0xe519c933}, {"_shpe", 0xfc6eb1f7}, {"_lght", 0x00000000},
    {"_view", 0xbca76ddc}, {"_gmnd", 0x7ba3838c}, {"_gmdc", 0xac4f8687}, {"_txmt", 0x49596978},
    {"_txtr", 0x1c4a276c}, {"_anim", 0xfb00791e}, {"_ansc", 0x499743e5}, {"_lifo", 0xed534136},
};
constexpr uint32_t kKeywordLength = 5;

using StringKeyParser::HashedText;

uint8_t *CrcTablesReady()
{
    return &ts2_rva_10f59d0;
}

// Fills both tables (0x9cbb60's body). The flag is set before the tables are written, as the original does.
void FillCrcTables()
{
    *CrcTablesReady() = 1;
    uint32_t *t24 = ts2_rva_10f51d0;
    uint32_t *t32 = ts2_rva_10f55d0;
    for (uint32_t i = 0; i < 256; ++i) {
        // CRC-24, MSB first: shift in eight zero bits at the top of a 24-bit register.
        uint32_t v = i << 16;
        for (int k = 0; k < 8; ++k) {
            v <<= 1;
            if (v & 0x1000000) v ^= kCrc24Poly;
        }
        t24[i] = v;

        // CRC-32, MSB first.
        v = i << 24;
        for (int k = 0; k < 8; ++k) {
            uint32_t top = v & 0x80000000;
            v <<= 1;
            if (top) v ^= kCrc32Poly;
        }
        t32[i] = v;
    }
}

void EnsureCrcTables()
{
    if (*CrcTablesReady() == 0) FillCrcTables();
}

// Constructs the local cRZString the parser works on: the game's vtable, empty text, refcount 0.
void InitLocal(cRZString &s)
{
    s.mVTable = cRZString::VTable();
    s.mRefCount = 0;
}

// The suffix test of 0x1acd83 in its case-sensitive form (shared with the lowercased path).
bool SuffixMatch(const std::string &s, const char *text, uint32_t length)
{
    uint32_t size = static_cast<uint32_t>(s.size());
    if (length > size) return false;
    for (uint32_t k = 1; k <= length; ++k) {
        if (s[size - k] != text[length - k]) return false;
    }
    return true;
}

// Compares a key with a table entry: the hash difference when they differ, else _stricmp of the texts.
int32_t CompareHashed(const HashedText *key, const HashedText *entry)
{
    if (key->hash != entry->hash) return static_cast<int32_t>(key->hash - entry->hash);
    return _stricmp(key->text, entry->text);
}

// Looks up the keyword table: the value of the first literal the local text ends with, else 0.
uint32_t KeywordValue(cRZString &local)
{
    cRZStringHelpers *helpers = reinterpret_cast<cRZStringHelpers *>(&local);
    for (const Keyword &kw : kKeywords) {
        if (helpers->EndsWith(kw.text, kKeywordLength, 1)) return kw.value;
    }
    return 0;
}

} // namespace

// ---- CRC tables and hashes ---------------------------------------------------------------------

namespace StringKeyParser {

void *CrcTables::Initialize()
{
    EnsureCrcTables();
    return this;
}

uint32_t Crc24(const char *text, uint8_t lower)
{
    EnsureCrcTables();
    const uint32_t *table = ts2_rva_10f51d0;
    uint32_t h = kCrc24Seed;
    for (const char *p = text; *p; ++p) {
        int c = static_cast<signed char>(*p); // movsx: negative for bytes 0x80-0xff
        if (lower) c = tolower(c);
        uint32_t index = (h >> 16) ^ static_cast<uint32_t>(c);
        h = (h << 8) ^ table[index & 0xff];
    }
    return h & 0xffffff;
}

uint32_t Crc32(const char *text, uint8_t lower)
{
    EnsureCrcTables();
    const uint32_t *table = ts2_rva_10f55d0;
    uint32_t h = 0xffffffff;
    for (const char *p = text; *p; ++p) {
        int c = static_cast<signed char>(*p);
        if (lower) c = tolower(c);
        uint32_t index = (h >> 24) ^ static_cast<uint32_t>(c);
        h = (h << 8) ^ table[index & 0xff];
    }
    return h;
}

uint32_t ParseNumber(const char *text)
{
    int base = 10;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text += 2;
    }
    return static_cast<uint32_t>(strtoul(text, nullptr, base));
}

void EnsureServiceObject()
{
    void **cache = &ts2_rva_10f8f2c;
    if (*cache != nullptr) return;
    void *service = GetServiceObject();
    using Query = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
    if (VirtualCall<Query, 5>(service, 0x056b906e, 0x2efe1873, cache) != 0) {
        void *object = *cache;
        VirtualCall<uint32_t(__thiscall *)(void *), 1>(object); // AddRef
    }
}

int32_t CompareEntry(const void *key, const void *entry)
{
    return CompareHashed(static_cast<const HashedText *>(key), static_cast<const HashedText *>(entry));
}

bool ParseKey(Result *out, const char *key, uint32_t preset, const char *ext)
{
    cRZString local;
    InitLocal(local);

    const char *sep = std::strchr(key, '!');
    if (sep == nullptr) {
        out->id = 0x1c0532fa;
        local.FromChar(key);
    } else if (sep == key) {
        out->id = 0x1c0532fa;
        local.FromChar(key + 1);
    } else if (sep == key + 1 && key[0] == '*' && ext != nullptr && ext[0] != '\0') {
        // "*!text": the identifier comes from the service with the extra name (or a CRC when there is no service).
        EnsureServiceObject();
        void *service = ts2_rva_10f8f2c;
        uint32_t id;
        if (service != nullptr) {
            id = VirtualCall<uint32_t(__thiscall *)(void *, const char *), 0>(service, ext);
        } else {
            id = Crc24(ext, 1) | 0x7f000000;
        }
        out->id = id;
        local.FromChar(key + 2);
    } else if (key[0] == '#') {
        // "#num!text" and "##num!text": the number is parsed from the character after the '#' (or the one after "##").
        out->id = ParseNumber(key[1] == '#' ? key + 2 : key + 1);
        local.FromChar(sep + 1);
    } else {
        // "name!text": the name is hashed (through the service when it is available).
        cRZString name;
        name.mVTable = cRZString::VTable();
        name.mRefCount = 0;
        name.mString.assign(key, static_cast<size_t>(sep - key));
        local.FromChar(sep + 1);
        EnsureServiceObject();
        void *service = ts2_rva_10f8f2c;
        uint32_t id;
        if (service != nullptr) {
            id = VirtualCall<uint32_t(__thiscall *)(void *, const char *), 0>(service, name.Data());
        } else {
            id = Crc24(name.Data(), 1) | 0x7f000000;
        }
        out->id = id;
    }

    // Common tail: hashes of the local text.
    if (local.mString.empty()) {
        out->hash24 = 0;
        out->crc32 = 0;
    } else {
        out->hash24 = Crc24(local.Data(), 1) | 0xff000000;
        out->crc32 = Crc32(local.Data(), 1);
    }

    out->value = preset != 0 ? preset : KeywordValue(local);

    HashedText probe{out->hash24, local.Data()};
    if (::bsearch(&probe, ts2_rva_10718c8, kEntryCount, sizeof(HashedText), &CompareEntry) != nullptr) {
        out->hash24 &= 0xffffff;
    }
    return true;
}

} // namespace StringKeyParser

// ---- cRZString helpers -------------------------------------------------------------------------

void cRZStringHelpers::LowerCase()
{
    cRZString *self = reinterpret_cast<cRZString *>(this);
    _strlwr(&self->mString[0]);
}

bool cRZStringHelpers::EndsWithObject(void *other, uint8_t caseSensitive)
{
    uint32_t length = VirtualCall<uint32_t(__thiscall *)(void *), 7>(other);
    const char *data = VirtualCall<const char *(__thiscall *)(void *), 5>(other);
    return EndsWith(data, length, caseSensitive);
}

bool cRZStringHelpers::EndsWith(const char *text, uint32_t length, uint8_t caseSensitive)
{
    cRZString *self = reinterpret_cast<cRZString *>(this);
    if (caseSensitive != 0) return SuffixMatch(self->mString, text, length);

    // Case-insensitive: lowercased copies of this and of the text, compared the same way.
    cRZString lowered;
    lowered.mVTable = cRZString::VTable();
    lowered.mRefCount = 0;
    lowered.mString = self->mString;
    _strlwr(&lowered.mString[0]);

    cRZString other;
    other.mVTable = cRZString::VTable();
    other.mRefCount = 0;
    other.mString.assign(text, length);
    _strlwr(&other.mString[0]);

    return SuffixMatch(lowered.mString, other.mString.c_str(), static_cast<uint32_t>(other.mString.size()));
}
