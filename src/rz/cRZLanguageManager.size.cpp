#include "rz/cRZLanguageManager.h"

#include "game/game.h"
#include "game/import.h"
#include "game/vtable.h"
#include "rz/RZTextConvert.h"

#include <windows.h>

#include <clocale>
#include <cstdint>
#include <mbctype.h>
#include <iterator>

namespace {

// Game texts (RVAs of C strings in the image).
constexpr uint32_t kEmptyText = 0xd9b9d0;
constexpr uint32_t kUntranslatedText = 0xe9f4a4;
constexpr uint32_t kUnitedStatesText = 0xe9f7e4; // slot 34's answer for an unknown country
constexpr uint32_t kIatGetUserDefaultLCID = 0xd9a34c;

const char *Text(uint32_t rva) { return GameAddress<const char>(rva); }

// A cRZString in the frame (the original's locals carry the game's string vtable, as these do).
struct LocalText {
    union {
        cRZString s;
    };
    LocalText() { s.Construct(); }
    explicit LocalText(const cIGZString &other) { s.Construct(other); }
    ~LocalText() { s.Destruct(); }
    LocalText(const LocalText &) = delete;
    LocalText &operator=(const LocalText &) = delete;
    cIGZString *I() { return s.AsInterface(); }
};

struct LcidLanguage {
    uint16_t lcid;
    uint8_t language;
};

// The tables, read from the original's jump tables (entry i is id i + 1 unless said otherwise; values are text RVAs).

// Slot 11: each language's LCID (language 1 first).
constexpr uint32_t kLanguageLcid[0x2c] = {
    0x409, // 0x1
    0x809, // 0x2
    0x40c, // 0x3
    0x407, // 0x4
    0x410, // 0x5
    0x40a, // 0x6
    0x413, // 0x7
    0x406, // 0x8
    0x41d, // 0x9
    0x414, // 0xa
    0x40b, // 0xb
    0x40d, // 0xc
    0x419, // 0xd
    0x816, // 0xe
    0x411, // 0xf
    0x415, // 0x10
    0x804, // 0x11
    0x404, // 0x12
    0x41e, // 0x13
    0x412, // 0x14
    0x439, // 0x15
    0x401, // 0x16
    0x402, // 0x17
    0x1, // 0x18
    0x422, // 0x19
    0x405, // 0x1a
    0x408, // 0x1b
    0x40e, // 0x1c
    0x40f, // 0x1d
    0x418, // 0x1e
    0x1, // 0x1f
    0x41b, // 0x20
    0x41c, // 0x21
    0x41f, // 0x22
    0x416, // 0x23
    0x1, // 0x24
    0x1, // 0x25
    0x1, // 0x26
    0x1, // 0x27
    0x1, // 0x28
    0x1, // 0x29
    0x80a, // 0x2a
    0x1, // 0x2b
    0x42a, // 0x2c
};

// Slot 13: each language's CRT locale name.
constexpr uint32_t kLocaleNames[0x2c] = {
    0xe9f1b4, // 0x1
    0xe9f1b8, // 0x2
    0xe9f1bc, // 0x3
    0xe9f1c0, // 0x4
    0xe9f1c4, // 0x5
    0xe9f1c8, // 0x6
    0xe9f1e0, // 0x7
    0xe9f1cc, // 0x8
    0xe9f1d0, // 0x9
    0xe98e44, // 0xa
    0xe9f1e4, // 0xb
    0xe9f270, // 0xc
    0xe9f1d4, // 0xd
    0xe9f1e8, // 0xe
    0xe9f1dc, // 0xf
    0xe9f1d8, // 0x10
    0xe9f1f0, // 0x11
    0xe9f1f4, // 0x12
    0xe98e90, // 0x13
    0xe9f1f8, // 0x14
    0xe9f22c, // 0x15
    0xe9f234, // 0x16
    0xe9f1d4, // 0x17
    0xe9f1d4, // 0x18
    0xe9f1d4, // 0x19
    0xe9f1fc, // 0x1a
    0xe9f214, // 0x1b
    0xe9f218, // 0x1c
    0xe9f21c, // 0x1d
    0xe9f244, // 0x1e
    0xe9f23c, // 0x1f
    0xe8415c, // 0x20
    0xe9f264, // 0x21
    0xe9f228, // 0x22
    0xe9f1ec, // 0x23
    0xe9f20c, // 0x24
    0xe9f208, // 0x25
    0xe9f204, // 0x26
    0xe9f210, // 0x27
    0xe9f220, // 0x28
    0xe9f200, // 0x29
    0xe9f224, // 0x2a
    0xe9f25c, // 0x2b
    0xe9f250, // 0x2c
};

// Slot 16: each language's English name.
constexpr uint32_t kLanguageNames[0x2c] = {
    0xe9f278, // 0x1
    0xe9f280, // 0x2
    0xe9f28c, // 0x3
    0xe9f294, // 0x4
    0xe9f29c, // 0x5
    0xe9f2a4, // 0x6
    0xe9f2d4, // 0x7
    0xe9f2ac, // 0x8
    0xe9f2b4, // 0x9
    0xe9f2dc, // 0xa
    0xe9f2e8, // 0xb
    0xe9f2bc, // 0xc
    0xe9f2c4, // 0xd
    0xe9f2fc, // 0xe
    0xe9f2f0, // 0xf
    0xe9f2cc, // 0x10
    0xe9f320, // 0x11
    0xe9f334, // 0x12
    0xe9f348, // 0x13
    0xe9f350, // 0x14
    0xe9f358, // 0x15
    0xe9f360, // 0x16
    0xe9f368, // 0x17
    0xe9f374, // 0x18
    0xe9f380, // 0x19
    0xe9f38c, // 0x1a
    0xe9f394, // 0x1b
    0xe9f39c, // 0x1c
    0xe9f3a8, // 0x1d
    0xe9f3b4, // 0x1e
    0xe9f3c0, // 0x1f
    0xe9f3c8, // 0x20
    0xe9f3d0, // 0x21
    0xe9f3dc, // 0x22
    0xe9f308, // 0x23
    0xe9f3e4, // 0x24
    0xe9f3f4, // 0x25
    0xe9f404, // 0x26
    0xe9f414, // 0x27
    0xe9f424, // 0x28
    0xe9f434, // 0x29
    0xe9f444, // 0x2a
    0xe9f454, // 0x2b
    0xe9f45c, // 0x2c
};

// Slot 24: each language's two-letter code.
constexpr uint32_t kLanguageCodes[0x2c] = {
    0xe9f4d4, // 0x1
    0xe9f4d8, // 0x2
    0xe9f4dc, // 0x3
    0xe9f4e0, // 0x4
    0xe9f4e4, // 0x5
    0xe9f4e8, // 0x6
    0xe9f4ec, // 0x7
    0xe9f4f0, // 0x8
    0xe9f4f4, // 0x9
    0xdb2a5c, // 0xa
    0xe522f8, // 0xb
    0xe9f4f8, // 0xc
    0xe9f4fc, // 0xd
    0xe5cc00, // 0xe
    0xe9f500, // 0xf
    0xe9f504, // 0x10
    0xe9f508, // 0x11
    0xe9f508, // 0x12
    0xe9f50c, // 0x13
    0xe9f510, // 0x14
    0xe9f51c, // 0x15
    0xe9f520, // 0x16
    0xe8a9a8, // 0x17
    0xe9f4fc, // 0x18
    0xe9f4d8, // 0x19
    0xe9f524, // 0x1a
    0xe9f528, // 0x1b
    0xe9f52c, // 0x1c
    0xe9f530, // 0x1d
    0xe9f534, // 0x1e
    0xe9f538, // 0x1f
    0xe9f53c, // 0x20
    0xe9f514, // 0x21
    0xe9f518, // 0x22
    0xe5cc00, // 0x23
    0xe9f4dc, // 0x24
    0xe9f4dc, // 0x25
    0xe9f4dc, // 0x26
    0xe9f540, // 0x27
    0xe9f4e4, // 0x28
    0xe9f4ec, // 0x29
    0xe9f544, // 0x2a
    0xe9f548, // 0x2b
    0xe9f54c, // 0x2c
};

// Slot 26: each country's code (0: none, the default case).
constexpr uint32_t kCountryCodes[0x38] = {
    0xe9f514, // 0x1
    0xe9f520, // 0x2
    0xe9f550, // 0x3
    0xe51e70, // 0x4
    0xe9f554, // 0x5
    0xe9f558, // 0x6
    0xe8a9a8, // 0x7
    0xe9f55c, // 0x8
    0xe9f560, // 0x9
    0xe9f564, // 0xa
    0xe9f568, // 0xb
    0xe9f56c, // 0xc
    0xe9f570, // 0xd
    0xe9f574, // 0xe
    0xe9f578, // 0xf
    0xe522f8, // 0x10
    0xe9f4dc, // 0x11
    0xe9f57c, // 0x12
    0xe9f4e0, // 0x13
    0xe9f540, // 0x14
    0, // 0x15
    0xe9f580, // 0x16
    0xe9f52c, // 0x17
    0xe9f530, // 0x18
    0xe9f584, // 0x19
    0xe9f588, // 0x1a
    0xe9f58c, // 0x1b
    0xe9f590, // 0x1c
    0xe9f594, // 0x1d
    0xe9f4e4, // 0x1e
    0xe9f598, // 0x1f
    0xe9f59c, // 0x20
    0xe9f5a0, // 0x21
    0xe9f4ec, // 0x22
    0xe9f5a4, // 0x23
    0xdb2a5c, // 0x24
    0xe9f5a8, // 0x25
    0xe9f504, // 0x26
    0xe5cc00, // 0x27
    0xe9f534, // 0x28
    0xe9f4fc, // 0x29
    0xe9f5ac, // 0x2a
    0xe9f53c, // 0x2b
    0xe9f5b0, // 0x2c
    0xe9f4e8, // 0x2d
    0xe04500, // 0x2e
    0xe9f5b4, // 0x2f
    0xe9f5b8, // 0x30
    0xe9f5bc, // 0x31
    0xe9f50c, // 0x32
    0xe9f518, // 0x33
    0xe9f5c0, // 0x34
    0xe9f5c4, // 0x35
    0xe9f5c8, // 0x36
    0xe9f5cc, // 0x37
    0xe9f5d0, // 0x38
};

// Slot 30: each country's language, countries 2 to 0x38 (0: none, as the default).
constexpr uint32_t kCountryLanguage[0x37] = {
    0x6, // 0x2
    0x2, // 0x3
    0x4, // 0x4
    0x26, // 0x5
    0x23, // 0x6
    0x17, // 0x7
    0x1, // 0x8
    0x6, // 0x9
    0x11, // 0xa
    0x6, // 0xb
    0x6, // 0xc
    0x1a, // 0xd
    0x8, // 0xe
    0x1, // 0xf
    0xb, // 0x10
    0x0, // 0x11
    0xd, // 0x12
    0x4, // 0x13
    0x1b, // 0x14
    0x0, // 0x15
    0x12, // 0x16
    0x1c, // 0x17
    0x1d, // 0x18
    0x1, // 0x19
    0x16, // 0x1a
    0x16, // 0x1b
    0x2, // 0x1c
    0xc, // 0x1d
    0x5, // 0x1e
    0xf, // 0x1f
    0x14, // 0x20
    0x2a, // 0x21
    0x7, // 0x22
    0x2, // 0x23
    0xa, // 0x24
    0x2b, // 0x25
    0x10, // 0x26
    0xe, // 0x27
    0x1e, // 0x28
    0xd, // 0x29
    0x11, // 0x2a
    0x20, // 0x2b
    0x1, // 0x2c
    0x6, // 0x2d
    0x9, // 0x2e
    0x2, // 0x2f
    0x1, // 0x30
    0x12, // 0x31
    0x13, // 0x32
    0x0, // 0x33
    0x19, // 0x34
    0x2, // 0x35
    0x1, // 0x36
    0x2c, // 0x37
    0x20, // 0x38
};

// Slot 31: each language's country.
constexpr uint32_t kLanguageCountry[0x2c] = {
    0x36, // 0x1
    0x35, // 0x2
    0x11, // 0x3
    0x13, // 0x4
    0x1e, // 0x5
    0x2d, // 0x6
    0x22, // 0x7
    0xe, // 0x8
    0x2e, // 0x9
    0x24, // 0xa
    0x10, // 0xb
    0x1d, // 0xc
    0x29, // 0xd
    0x27, // 0xe
    0x1f, // 0xf
    0x26, // 0x10
    0xa, // 0x11
    0x16, // 0x12
    0x32, // 0x13
    0x20, // 0x14
    0x19, // 0x15
    0x1a, // 0x16
    0x7, // 0x17
    0x29, // 0x18
    0x34, // 0x19
    0xd, // 0x1a
    0x14, // 0x1b
    0x17, // 0x1c
    0x18, // 0x1d
    0x28, // 0x1e
    0x1e, // 0x1f
    0x2b, // 0x20
    0x1, // 0x21
    0x33, // 0x22
    0x6, // 0x23
    0x2f, // 0x24
    0x8, // 0x25
    0x5, // 0x26
    0x2f, // 0x27
    0x2f, // 0x28
    0x5, // 0x29
    0x21, // 0x2a
    0x25, // 0x2b
    0x37, // 0x2c
};

// Slot 34: each country's English name.
constexpr uint32_t kCountryNames[0x38] = {
    0xe9f5d4, // 0x1
    0xe9f5dc, // 0x2
    0xe9f5e8, // 0x3
    0xe9f5f4, // 0x4
    0xe9f5fc, // 0x5
    0xe9f604, // 0x6
    0xe9f60c, // 0x7
    0xe9f618, // 0x8
    0xe9f620, // 0x9
    0xe9f628, // 0xa
    0xe9f630, // 0xb
    0xe9f63c, // 0xc
    0xe9f644, // 0xd
    0xe9f654, // 0xe
    0xe9f65c, // 0xf
    0xe9f664, // 0x10
    0xe9f66c, // 0x11
    0xe9f674, // 0x12
    0xe9f67c, // 0x13
    0xe9f684, // 0x14
    0xe9f68c, // 0x15
    0xe9f698, // 0x16
    0xe9f6a4, // 0x17
    0xe9f6ac, // 0x18
    0xe9f6b4, // 0x19
    0xe9f6bc, // 0x1a
    0xe9f6c4, // 0x1b
    0xe9f6cc, // 0x1c
    0xe9f6d4, // 0x1d
    0xe9f6dc, // 0x1e
    0xe9f6e4, // 0x1f
    0xe9f6ec, // 0x20
    0xe9f6f4, // 0x21
    0xe9f6fc, // 0x22
    0xe9f708, // 0x23
    0xe9f714, // 0x24
    0xe9f71c, // 0x25
    0xe9f728, // 0x26
    0xe9f730, // 0x27
    0xe9f73c, // 0x28
    0xe9f744, // 0x29
    0xe9f74c, // 0x2a
    0xe9f758, // 0x2b
    0xe9f764, // 0x2c
    0xe9f770, // 0x2d
    0xe9f778, // 0x2e
    0xe9f780, // 0x2f
    0xe9f78c, // 0x30
    0xd9b9d0, // 0x31
    0xe9f794, // 0x32
    0xe9f7a0, // 0x33
    0xe9f7a8, // 0x34
    0xe9f7b0, // 0x35
    0xe9f7c0, // 0x36
    0xe9f7d0, // 0x37
    0xe9f7d8, // 0x38
};

// Slot 12: the LCIDs (low word) that map to a language other than the default (1), in order.
constexpr LcidLanguage kLcidLanguages[] = {
    {0x0401, 0x16},
    {0x0402, 0x17},
    {0x0404, 0x12},
    {0x0405, 0x1a},
    {0x0406, 0x8},
    {0x0407, 0x4},
    {0x0408, 0x1b},
    {0x040a, 0x6},
    {0x040b, 0xb},
    {0x040c, 0x3},
    {0x040d, 0xc},
    {0x040e, 0x1c},
    {0x040f, 0x1d},
    {0x0410, 0x5},
    {0x0411, 0xf},
    {0x0412, 0x14},
    {0x0413, 0x7},
    {0x0414, 0xa},
    {0x0415, 0x10},
    {0x0416, 0x23},
    {0x0418, 0x1e},
    {0x0419, 0xd},
    {0x041b, 0x20},
    {0x041c, 0x21},
    {0x041d, 0x9},
    {0x041e, 0x13},
    {0x041f, 0x22},
    {0x0422, 0x19},
    {0x042a, 0x2c},
    {0x0439, 0x15},
    {0x0801, 0x16},
    {0x0804, 0x11},
    {0x0807, 0x4},
    {0x0809, 0x2},
    {0x080a, 0x2a},
    {0x080c, 0x3},
    {0x0810, 0x5},
    {0x0812, 0x14},
    {0x0813, 0x7},
    {0x0814, 0xa},
    {0x0816, 0xe},
    {0x081d, 0x9},
    {0x0c01, 0x16},
    {0x0c04, 0x12},
    {0x0c07, 0x4},
    {0x0c09, 0x2},
    {0x0c0a, 0x6},
    {0x0c0c, 0x3},
    {0x1001, 0x16},
    {0x1004, 0x11},
    {0x1007, 0x4},
    {0x100a, 0x6},
    {0x100c, 0x3},
    {0x1401, 0x16},
    {0x1404, 0x11},
    {0x1407, 0x4},
    {0x1409, 0x2},
    {0x140a, 0x6},
    {0x140c, 0x3},
    {0x1801, 0x16},
    {0x1809, 0x2},
    {0x180a, 0x6},
    {0x180c, 0x3},
    {0x1c01, 0x16},
    {0x1c09, 0x2},
    {0x1c0a, 0x6},
    {0x2001, 0x16},
    {0x2009, 0x2},
    {0x200a, 0x6},
    {0x2401, 0x16},
    {0x2409, 0x2},
    {0x240a, 0x6},
    {0x2801, 0x16},
    {0x2809, 0x2},
    {0x280a, 0x6},
    {0x2c01, 0x16},
    {0x2c09, 0x2},
    {0x2c0a, 0x6},
    {0x3001, 0x16},
    {0x3009, 0x2},
    {0x300a, 0x6},
    {0x3401, 0x16},
    {0x3409, 0x2},
    {0x340a, 0x6},
    {0x3801, 0x16},
    {0x380a, 0x6},
    {0x3c01, 0x16},
    {0x3c0a, 0x6},
    {0x4001, 0x16},
    {0x400a, 0x6},
    {0x440a, 0x6},
    {0x480a, 0x6},
    {0x4c0a, 0x6},
    {0x500a, 0x6},
};

// The text of `id` (1 to N) in `table`, or null (outside the table, or a 0 entry).
template <size_t N> const char *TableText(const uint32_t (&table)[N], uint32_t id)
{
    if (id - 1 >= N || table[id - 1] == 0) return nullptr;
    return Text(table[id - 1]);
}

} // namespace

// ---- the list element ----------------------------------------------------------------------------------------------

cRZLanguageNameEntry::cRZLanguageNameEntry(const cRZString &text)
{
    String()->Construct(text, 0, 0xffffffffu);
}

cRZLanguageNameEntry::cRZLanguageNameEntry(const cRZLanguageNameEntry &other)
{
    String()->Construct(*reinterpret_cast<const cRZString *>(other.raw), 0, 0xffffffffu);
}

cRZLanguageNameEntry::~cRZLanguageNameEntry()
{
    VirtualCall<void *(__thiscall *)(void *, uint32_t), 33>(String(), 0u);
}

// ---- the CRT seam --------------------------------------------------------------------------------------------------

cRZLanguageManagerCrt &LanguageManagerCrt()
{
    static cRZLanguageManagerCrt crt = {&std::setlocale, &_setmbcp};
    return crt;
}

// ---- construction --------------------------------------------------------------------------------------------------

cRZLanguageManager::cRZLanguageManager()
    : rz::cRZSystemService(kServiceId, kPriority), mUnknown20(0), mLanguage(kDefaultLanguage), mCountry(kDefaultCountry)
{
    mUtility.Init();
    mUtility.SetLanguage(kDefaultLanguage);
    mUtility.SetCountry(kDefaultCountry);
}

cRZLanguageManager::~cRZLanguageManager()
{
    mUtility.Shutdown();
}

cIGZSystemService *__cdecl cRZLanguageManager::Create()
{
    return new cRZLanguageManager;
}

// ---- cIGZUnknown ---------------------------------------------------------------------------------------------------

bool cRZLanguageManager::QueryInterface(uint32_t riid, void **ppv)
{
    // Only the two faces' ids (not cIGZUnknown's 1); no test of ppv.
    if (riid == cIGZLanguageManager::kIid) {
        *ppv = static_cast<cIGZLanguageManager *>(this);
    } else if (riid == cIGZSystemService::kIID) {
        *ppv = static_cast<cIGZSystemService *>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

uint32_t cRZLanguageManager::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cRZLanguageManager::Release() { return rz::cRZSystemService::Release(); }

// ---- the language --------------------------------------------------------------------------------------------------

bool cRZLanguageManager::Init() { return true; }
bool cRZLanguageManager::Shutdown() { return true; }

void cRZLanguageManager::AllowLanguage(uint32_t language)
{
    if (language <= kLastLanguage) mAllowedLanguages.insert(language);
}

void cRZLanguageManager::DisallowLanguage(uint32_t language) { mAllowedLanguages.erase(language); }

uint32_t cRZLanguageManager::GetLanguage() { return mLanguage; }

bool cRZLanguageManager::SetLanguage(uint32_t language)
{
    if (language == 0) language = kDefaultLanguage;
    if (language == mLanguage) return true;
    if (!IsLanguageAllowed(language)) return false;
    mLanguage = language;
    mUtility.SetLanguage(language);
    return ApplyLocale(language);
}

bool cRZLanguageManager::IsLanguageAllowed(uint32_t language)
{
    return mAllowedLanguages.empty() || mAllowedLanguages.count(language) != 0;
}

uint32_t cRZLanguageManager::GetUserDefaultLanguage()
{
    const LCID lcid = ImportFunction<LCID(WINAPI *)(), kIatGetUserDefaultLCID>()();
    return LanguageFromLcid(static_cast<uint16_t>(lcid));
}

uint32_t cRZLanguageManager::GetLanguageLcid(uint32_t language)
{
    if (language == 0) language = mLanguage;
    if (language - 1 >= std::size(kLanguageLcid)) return 1;
    return kLanguageLcid[language - 1];
}

uint32_t cRZLanguageManager::LanguageFromLcid(uint32_t lcid)
{
    const uint16_t id = static_cast<uint16_t>(lcid);
    for (const LcidLanguage &row : kLcidLanguages)
        if (row.lcid == id) return row.language;
    return 1;
}

bool cRZLanguageManager::GetLocaleName(cIGZString *out, uint32_t language)
{
    if (language == 0) language = mLanguage;
    const char *name = TableText(kLocaleNames, language);
    if (!name) return false;
    out->FromChar(name);
    return true;
}

bool cRZLanguageManager::NextAllowedLanguage(uint32_t *language)
{
    // Each candidate is stored before slot 9 sees it; one past the last id when none is allowed (unsigned, as the
    // original's jbe).
    while (*language <= kLastLanguage) {
        ++*language;
        if (IsLanguageAllowed(*language)) return true;
    }
    ++*language;
    return false;
}

bool cRZLanguageManager::HasLanguageName(cIGZString *out, uint32_t language)
{
    if (language == 0) language = mLanguage;
    GetLanguageName(out, language);
    return out->Strlen() != 0;
}

bool cRZLanguageManager::GetLanguageName(cIGZString *out, uint32_t language)
{
    if (language == 0) language = mLanguage;
    const char *name = TableText(kLanguageNames, language);
    if (!name) {
        out->FromChar(Text(kEmptyText));
        return false;
    }
    out->FromChar(name);
    return true;
}

bool cRZLanguageManager::GetNativeLanguageName(cIGZString *out, uint32_t language)
{
    if (language == 0) language = mLanguage;
    uint32_t text = 0;
    switch (language) {
    case 1:
    case 2:
        text = 0xe9f278;
        break;
    case 3:
        text = 0xe9f468;
        break;
    case 4:
        text = 0xe9f474;
        break;
    case 5:
        text = 0xe9f47c;
        break;
    case 6:
        text = 0xe9f488;
        break;
    case 7:
        text = 0xe9f4b4;
        break;
    case 8:
        text = 0xe9f494;
        break;
    case 9:
        text = 0xe9f49c;
        break;
    case 0xb:
        text = 0xe9f4c0;
        break;
    case 0xd:
        text = 0xe9f2c4;
        break;
    case 0xe:
    case 0x23:
        text = 0xe9f4c8;
        break;
    case 0xa:
    case 0xc:
    case 0xf:
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
    case 0x14:
        text = kUntranslatedText;
        break;
    default:
        break;
    }
    if (text != 0) out->FromChar(Text(text));
    bool result = true;
    // ORIGINAL-BUG: the test is inverted: a name just written (or text the string already held) is replaced by
    // "Untranslated" and the answer is false; only an unknown language with an empty string answers true.
    if (out->Strlen() != 0) {
        out->FromChar(Text(kUntranslatedText));
        result = false;
    }
    cRZLanguageUtility::ToDefaultEncoding(out);
    return result;
}

bool cRZLanguageManager::FindByName(NameGetter getName, uint32_t last, uint32_t none, const cIGZString *name,
                                    uint32_t *id)
{
    LocalText candidate;
    for (uint32_t i = 1; i <= last; ++i) {
        if (!(this->*getName)(candidate.I(), i)) continue;
        if (mUtility.Compare(name, candidate.I(), false) == 0) {
            *id = i;
            return true;
        }
    }
    *id = none;
    return false;
}

bool cRZLanguageManager::LanguageFromName(const cIGZString *name, uint32_t *language)
{
    return FindByName(&cIGZLanguageManager::GetLanguageName, kLastLanguage, kDefaultLanguage, name, language);
}

bool cRZLanguageManager::LanguageFromNativeName(const cIGZString *name, uint32_t *language)
{
    return FindByName(&cIGZLanguageManager::GetNativeLanguageName, kLastLanguage, kDefaultLanguage, name, language);
}

bool cRZLanguageManager::TranslateLanguageName(cIGZString *name, uint32_t language)
{
    LocalText copy(*name);
    // ORIGINAL-BUG: the original calls its own slot (20) on the copy, so it recurses until the stack runs out.
    const bool result = TranslateLanguageName(copy.I(), language);
    if (result) name->FromChar(copy.s.mString.c_str());
    return result;
}

bool cRZLanguageManager::GetLanguageNameAgain(cIGZString *out, uint32_t language) { return GetLanguageName(out, language); }

bool cRZLanguageManager::GetNativeLanguageNameAgain(cIGZString *out, uint32_t language)
{
    return GetNativeLanguageName(out, language);
}

uint32_t cRZLanguageManager::GetBaseLanguage(uint32_t language)
{
    if (language == 0) language = mLanguage;
    switch (language) {
    case 2:
        return 1;
    case 0x24:
    case 0x25:
    case 0x26:
        return 3;
    case 0x27:
        return 4;
    case 0x28:
        return 5;
    case 0x29:
        return 7;
    case 0x2a:
        return 6;
    case 0x17:
    case 0x18:
    case 0x19:
        return 0xd;
    default:
        return language;
    }
}

bool cRZLanguageManager::GetLanguageCode(cIGZString *out, uint32_t language)
{
    if (language == 0) language = mLanguage;
    const char *code = TableText(kLanguageCodes, language);
    if (!code) {
        out->FromChar(Text(kEmptyText));
        return false;
    }
    out->FromChar(code);
    return true;
}

bool cRZLanguageManager::LanguageFromCode(const cIGZString *code, uint32_t *language)
{
    return FindByName(&cIGZLanguageManager::GetLanguageCode, kLastLanguage, kDefaultLanguage, code, language);
}

// ---- the country ---------------------------------------------------------------------------------------------------

bool cRZLanguageManager::GetCountryCode(cIGZString *out, uint32_t country)
{
    if (country == 0) country = mCountry;
    const char *code = TableText(kCountryCodes, country);
    if (!code) {
        out->FromChar(Text(kEmptyText));
        return false;
    }
    out->FromChar(code);
    return true;
}

bool cRZLanguageManager::CountryFromCode(const cIGZString *code, uint32_t *country)
{
    return FindByName(&cIGZLanguageManager::GetCountryCode, kLastCountry + 1, kDefaultCountry, code, country);
}

uint32_t cRZLanguageManager::GetCountry() { return mCountry; }

bool cRZLanguageManager::SetCountry(uint32_t country)
{
    if (country == 0) country = kDefaultCountry;
    mCountry = country;
    mUtility.SetCountry(country);
    return true;
}

uint32_t cRZLanguageManager::GetCountryLanguage(uint32_t country)
{
    if (country == 0) country = mCountry;
    if (country - 2 >= std::size(kCountryLanguage)) return 0;
    return kCountryLanguage[country - 2];
}

uint32_t cRZLanguageManager::GetLanguageCountry(uint32_t language)
{
    if (language == 0) language = mLanguage;
    if (language - 1 >= std::size(kLanguageCountry)) return kDefaultCountry;
    return kLanguageCountry[language - 1];
}

int32_t cRZLanguageManager::vf32() { return -1; }
bool cRZLanguageManager::vf33(uint32_t) { return true; }

bool cRZLanguageManager::GetCountryName(cIGZString *out, uint32_t country)
{
    if (country == 0) country = mCountry;
    const char *name = TableText(kCountryNames, country);
    if (!name) {
        out->FromChar(Text(kUnitedStatesText));
        return false;
    }
    out->FromChar(name);
    return true;
}

bool cRZLanguageManager::GetCountryNameAgain(cIGZString *out, uint32_t country) { return GetCountryName(out, country); }

bool cRZLanguageManager::CountryFromName(const cIGZString *name, uint32_t *country)
{
    return FindByName(&cIGZLanguageManager::GetCountryName, kLastCountry + 1, kDefaultCountry, name, country);
}

bool cRZLanguageManager::CountryFromNameAgain(const cIGZString *name, uint32_t *country)
{
    return FindByName(&cIGZLanguageManager::GetCountryNameAgain, kLastCountry + 1, kDefaultCountry, name, country);
}

// ---- utilities -----------------------------------------------------------------------------------------------------

cIGZLanguageUtility *cRZLanguageManager::GetLanguageUtility(uint32_t language)
{
    if (language == 0 || language == mLanguage) return &mUtility;
    return nullptr;
}

cIGZLanguageUtility *cRZLanguageManager::CreateLanguageUtility(uint32_t language)
{
    // Not Init'ed (the constructor's utility is); the caller holds the one reference.
    cRZLanguageUtility *utility = new cRZLanguageUtility;
    utility->AddRef();
    utility->SetLanguage(language);
    return utility;
}

float cRZLanguageManager::vf40(uint32_t) { return 1.0f; }
void cRZLanguageManager::vf41(uint32_t, uint32_t) {}

bool cRZLanguageManager::vf42(uint32_t lo, uint32_t hi, uint32_t *out, uint32_t)
{
    out[0] = lo;
    out[1] = hi;
    return true;
}

bool cRZLanguageManager::vf43(uint32_t value, uint32_t *out, uint32_t)
{
    *out = value;
    return true;
}

bool cRZLanguageManager::vf44(uint32_t lo, uint32_t hi, uint32_t, uint32_t *out, uint32_t)
{
    out[0] = lo;
    out[1] = hi;
    return true;
}

bool cRZLanguageManager::vf45(uint32_t value, uint32_t, uint32_t *out, uint32_t)
{
    *out = value;
    return true;
}

void cRZLanguageManager::GetLanguageNames(std::list<cRZLanguageNameEntry> *out)
{
    out->clear();
    for (uint32_t language = 1; language <= kLastLanguage; ++language) {
        if (!NextAllowedLanguage(&language)) continue;
        LocalText name;
        GetLanguageName(name.I(), language);
        out->emplace_back(name.s);
    }
}

void cRZLanguageManager::GetAllowedLanguages(std::list<uint32_t> *out)
{
    out->clear();
    for (uint32_t language : mAllowedLanguages) out->push_back(language);
}

// ---- the CRT locale ------------------------------------------------------------------------------------------------

char *cRZLanguageManager::SetLocale(int category, const char *locale)
{
    ts2_rva_ff23a4 = -3;
    return LanguageManagerCrt().setLocale(category, locale);
}

bool cRZLanguageManager::ApplyLocale(uint32_t language)
{
    LocalText name;
    if (!GetLocaleName(name.I(), language)) return false;
    const char *text = name.s.mString.c_str();
    if (SetLocale(LC_COLLATE, text) && SetLocale(LC_MONETARY, text) && SetLocale(LC_NUMERIC, text) &&
        SetLocale(LC_TIME, text))
        LanguageManagerCrt().setMbcp(_MB_CP_LOCALE);
    return true;
}
