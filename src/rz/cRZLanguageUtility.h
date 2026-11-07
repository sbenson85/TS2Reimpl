#pragma once
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>

// cIGZLanguageUtility (interface id 0x1054b7d5): the per-language formatting helper (names of days and months, times,
// dates, currency and numbers, case mapping, comparison, code pages). Its only implementer is cRZLanguageUtility; the
// language manager hands it out (slots 38 and 39 of cIGZLanguageManager). Slots 3 to 30; the class adds the deleting
// destructor (31) and the two setters (32, 33).
//
// Language ids are the language manager's (1 US English, 2 UK English, 3 French, 4 German, 5 Italian, 6 Spanish, 7 Dutch,
// 8 Danish, 9 Swedish, 0xa Norwegian, 0xb Finnish, 0xc Hebrew, 0xd Russian, 0xe Portuguese, 0xf Japanese, 0x10 Polish,
// 0x11 Simplified Chinese, 0x12 Traditional Chinese, 0x13 Thai, 0x14 Korean, ... 0x23 Brazilian Portuguese, 0x2a
// Mexican Spanish: the LCIDs of cIGZLanguageManager slot 11). The country (+0xc) is a separate id, 0x36 the United States.
class cIGZLanguageUtility : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x1054b7d5;

    virtual bool Init() = 0;                                                  // slot 3
    virtual bool Shutdown() = 0;                                              // slot 4
    virtual bool GetDayName(uint8_t day, cIGZString *out) = 0;                // slot 5 (1..7, Sunday 7)
    virtual bool GetShortDayName(uint8_t day, cIGZString *out) = 0;           // slot 6
    virtual bool GetMonthName(uint8_t month, cIGZString *out) = 0;            // slot 7 (1..12)
    virtual bool GetShortMonthName(uint8_t month, cIGZString *out) = 0;       // slot 8
    virtual bool FormatTime(int32_t hour, int32_t minute, int32_t second, cIGZString *out) = 0;          // slot 9
    virtual bool FormatDate(uint32_t month, uint32_t day, uint32_t year, cIGZString *out, uint32_t style) = 0; // slot 10
    virtual bool GetCurrencySymbol(cIGZString *out) = 0;                      // slot 11
    virtual bool CurrencySymbolFirst() = 0;                                   // slot 12
    virtual bool CurrencySymbolSpaced() = 0;                                  // slot 13
    virtual bool GetThousandsSeparator(cIGZString *out) = 0;                  // slot 14
    virtual bool GetDecimalSeparator(cIGZString *out) = 0;                    // slot 15
    virtual bool FormatMoney(int64_t value, cIGZString *out, const cIGZString *symbol) = 0;              // slot 16
    virtual bool FormatMoneyFloat(double value, cIGZString *out, const cIGZString *format,
                                  const cIGZString *symbol) = 0;                                          // slot 17
    virtual bool FormatInteger(int64_t value, cIGZString *out) = 0;           // slot 18
    virtual bool FormatFloat(double value, cIGZString *out, const cIGZString *format) = 0;               // slot 19
    virtual void ToLower(const cIGZString *src, cIGZString *dst) = 0;        // slot 20
    virtual void ToUpper(const cIGZString *src, cIGZString *dst) = 0;        // slot 21
    virtual int32_t Compare(const cIGZString *a, const cIGZString *b, bool caseSensitive) = 0;           // slot 22
    virtual uint32_t GetCodePage() = 0;                                       // slot 23
    virtual bool SupportsCodePage(uint32_t codePage) = 0;                     // slot 24
    virtual bool vf25() = 0;                                                  // slot 25 (purpose unknown)
    virtual uint32_t CountryIsNotDefault() = 0;                               // slot 26
    virtual bool FamilyNameFirst() = 0;                                       // slot 27
    virtual void FormatFullName(const cIGZString *given, const cIGZString *family, cIGZString *out) = 0; // slot 28
    virtual bool vf29() = 0;                                                  // slot 29 (purpose unknown)
    virtual bool vf30() = 0;                                                  // slot 30 (purpose unknown)
};

// cRZLanguageUtility (0x34 bytes): the standalone shape (docs/standalone-shape.md). Game vtable 0xe9f014 (34 slots), one
// face. Built by the language manager's constructor (embedded at +0x34) and its slot 39 (`new`), both through the
// constructor 0xc6ed94, whose hook builds this class.
//
// Slots: QueryInterface 0xc747f9; AddRef the folded 0x148fb0 (`inc [ecx + 4]`); Release 0xc748d9; Init 0xc7347d;
// Shutdown the folded 0x144910 (true); then 0xc70b3b, 0xc6f35f, 0xc724d4, 0xc6fb1f, 0xc74531, 0xc73504, 0xc709ef, 0xc6f11b,
// 0xc7349d, 0xc733c2, 0xc7112f, 0xc73f26, 0xc73a5b, 0xc74318, 0xc73ddf, 0xc6efb6, 0xc6f018, 0xc6eed9, 0xc704ed, 0xc6f16a,
// 0xc7224e, 0xc724ca, 0xc6ee8c, 0xc73e0c, 0xc6f2d3, 0xc6f2f0 (slots 5 to 30); the deleting destructor 0xc6ee49 (destructor
// 0xc6ee0d); SetLanguage the folded 0x14a260 (`[ecx + 8] = arg`); SetCountry the folded 0x149c60 (`[ecx + 0xc] = arg`).
class cRZLanguageUtility : public cIGZLanguageUtility {
public:
    static constexpr uint32_t kVTable = 0xe9f014; // the game's, for tests
    static constexpr uint32_t kSize = 0x34;
    static constexpr uint32_t kDefaultLanguage = 1;
    static constexpr uint32_t kDefaultCountry = 0x36;

    // 0xc6ed94: count 0, language 1, country 0x36, the flag clear, the separator text a no-break space.
    cRZLanguageUtility();
    // 0xc6ee0d: the separator text's destructor (the game's cRZString vtable stored back, its text freed).
    virtual ~cRZLanguageUtility(); // slot 31 (deleting 0xc6ee49)
    cRZLanguageUtility(const cRZLanguageUtility &) = delete;
    cRZLanguageUtility &operator=(const cRZLanguageUtility &) = delete;

    // cIGZUnknown
    bool QueryInterface(uint32_t riid, void **ppv) override;
    uint32_t AddRef() override;
    uint32_t Release() override;

    // cIGZLanguageUtility
    bool Init() override;
    bool Shutdown() override;
    bool GetDayName(uint8_t day, cIGZString *out) override;
    bool GetShortDayName(uint8_t day, cIGZString *out) override;
    bool GetMonthName(uint8_t month, cIGZString *out) override;
    bool GetShortMonthName(uint8_t month, cIGZString *out) override;
    bool FormatTime(int32_t hour, int32_t minute, int32_t second, cIGZString *out) override;
    bool FormatDate(uint32_t month, uint32_t day, uint32_t year, cIGZString *out, uint32_t style) override;
    bool GetCurrencySymbol(cIGZString *out) override;
    bool CurrencySymbolFirst() override;
    bool CurrencySymbolSpaced() override;
    bool GetThousandsSeparator(cIGZString *out) override;
    bool GetDecimalSeparator(cIGZString *out) override;
    bool FormatMoney(int64_t value, cIGZString *out, const cIGZString *symbol) override;
    bool FormatMoneyFloat(double value, cIGZString *out, const cIGZString *format, const cIGZString *symbol) override;
    bool FormatInteger(int64_t value, cIGZString *out) override;
    bool FormatFloat(double value, cIGZString *out, const cIGZString *format) override;
    void ToLower(const cIGZString *src, cIGZString *dst) override;
    void ToUpper(const cIGZString *src, cIGZString *dst) override;
    int32_t Compare(const cIGZString *a, const cIGZString *b, bool caseSensitive) override;
    uint32_t GetCodePage() override;
    bool SupportsCodePage(uint32_t codePage) override;
    bool vf25() override;
    uint32_t CountryIsNotDefault() override;
    bool FamilyNameFirst() override;
    void FormatFullName(const cIGZString *given, const cIGZString *family, cIGZString *out) override;
    bool vf29() override;
    bool vf30() override;

    virtual void SetLanguage(uint32_t language); // slot 32
    virtual void SetCountry(uint32_t country);   // slot 33

    // 0xc6f07a (__cdecl): re-encodes a text the tables wrote into the default string encoding. That encoding is UTF-8
    // (RZText::DefaultEncoding), the tables' own, so it returns at once.
    static void __cdecl ToDefaultEncoding(cIGZString *text);

    uint32_t mRefCount;      // +0x04 (cRZUnknown style, not interlocked)
    uint32_t mLanguage;      // +0x08
    uint32_t mCountry;       // +0x0c
    bool mLocalCurrency;     // +0x10 cleared by the constructor, set by no slot: while clear the currency symbol is the dollar
    cRZString mGroupSpace;   // +0x14 the no-break space (U+00A0) some languages group digits with and put after a symbol
};

static_assert(sizeof(cRZLanguageUtility) == cRZLanguageUtility::kSize, "cRZLanguageUtility size");
static_assert(offsetof(cRZLanguageUtility, mRefCount) == 0x04, "cRZLanguageUtility layout");
static_assert(offsetof(cRZLanguageUtility, mLanguage) == 0x08, "cRZLanguageUtility layout");
static_assert(offsetof(cRZLanguageUtility, mCountry) == 0x0c, "cRZLanguageUtility layout");
static_assert(offsetof(cRZLanguageUtility, mLocalCurrency) == 0x10, "cRZLanguageUtility layout");
static_assert(offsetof(cRZLanguageUtility, mGroupSpace) == 0x14, "cRZLanguageUtility layout");

// The constructor hook's entry point. The slots' adapters are gone: only the game's vtable reaches their RVAs.
struct cRZLanguageUtilityAdapters {
    static cRZLanguageUtility *__fastcall Construct(void *memory, void *edx); // 0xc6ed94
};
