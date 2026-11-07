#pragma once
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "rz/cRZLanguageUtility.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <set>

// An element of the name lists slot 46 fills: a cRZString (the game's layout) copied in by the game's copy constructor
// (text from 0, all of it) and destroyed through its own vtable's slot 33 (the deleting destructor, flags 0), as the
// original's list instantiation does. Raw storage, so the compiler adds no destruction of its own.
struct cRZLanguageNameEntry {
    explicit cRZLanguageNameEntry(const cRZString &text);
    cRZLanguageNameEntry(const cRZLanguageNameEntry &other);
    ~cRZLanguageNameEntry();
    cRZLanguageNameEntry &operator=(const cRZLanguageNameEntry &) = delete;
    cRZString *String() { return reinterpret_cast<cRZString *>(raw); }
    alignas(4) unsigned char raw[sizeof(cRZString)];
};
static_assert(sizeof(cRZLanguageNameEntry) == 0x20, "a list element is a cRZString");

// cIGZLanguageManager (interface id 0x54b7d5): the language service's own face (game vtable 0xe9f0a0, 48 slots).
//
// Language ids are those of cIGZLanguageUtility (1 US English ... 0x2c the last; rz/cRZLanguageUtility.h); country ids
// run from 1 to 0x38 (0x36 the United States). A language or country argument of 0 means the manager's current one.
// Slots that write a name take the out string first; slots that look a name up take the name and an out id.
class cIGZLanguageManager : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x54b7d5;

    virtual bool Init() = 0;                                                       // 3
    virtual bool Shutdown() = 0;                                                   // 4
    // 5: allows a language (ids above 0x2c are ignored). The original returns what its set insert left in eax.
    virtual void AllowLanguage(uint32_t language) = 0;
    virtual void DisallowLanguage(uint32_t language) = 0;                          // 6
    virtual uint32_t GetLanguage() = 0;                                            // 7
    virtual bool SetLanguage(uint32_t language) = 0;                               // 8
    virtual bool IsLanguageAllowed(uint32_t language) = 0;                         // 9: every one while none is listed
    virtual uint32_t GetUserDefaultLanguage() = 0;                                 // 10
    virtual uint32_t GetLanguageLcid(uint32_t language) = 0;                       // 11
    virtual uint32_t LanguageFromLcid(uint32_t lcid) = 0;                          // 12 (the low word)
    virtual bool GetLocaleName(cIGZString *out, uint32_t language) = 0;            // 13 (the CRT's: "enu")
    virtual bool NextAllowedLanguage(uint32_t *language) = 0;                      // 14
    virtual bool HasLanguageName(cIGZString *out, uint32_t language) = 0;          // 15
    virtual bool GetLanguageName(cIGZString *out, uint32_t language) = 0;          // 16 ("English")
    virtual bool GetNativeLanguageName(cIGZString *out, uint32_t language) = 0;    // 17
    virtual bool LanguageFromName(const cIGZString *name, uint32_t *language) = 0; // 18
    virtual bool LanguageFromNativeName(const cIGZString *name, uint32_t *language) = 0; // 19
    virtual bool TranslateLanguageName(cIGZString *name, uint32_t language) = 0;   // 20
    virtual bool GetLanguageNameAgain(cIGZString *out, uint32_t language) = 0;     // 21: slot 16
    virtual bool GetNativeLanguageNameAgain(cIGZString *out, uint32_t language) = 0; // 22: slot 17
    virtual uint32_t GetBaseLanguage(uint32_t language) = 0;                       // 23
    virtual bool GetLanguageCode(cIGZString *out, uint32_t language) = 0;          // 24 ("en")
    virtual bool LanguageFromCode(const cIGZString *code, uint32_t *language) = 0; // 25
    virtual bool GetCountryCode(cIGZString *out, uint32_t country) = 0;            // 26
    virtual bool CountryFromCode(const cIGZString *code, uint32_t *country) = 0;   // 27
    virtual uint32_t GetCountry() = 0;                                             // 28
    virtual bool SetCountry(uint32_t country) = 0;                                 // 29
    virtual uint32_t GetCountryLanguage(uint32_t country) = 0;                     // 30
    virtual uint32_t GetLanguageCountry(uint32_t language) = 0;                    // 31
    virtual int32_t vf32() = 0;                                                    // 32: -1
    virtual bool vf33(uint32_t value) = 0;                                         // 33: true
    virtual bool GetCountryName(cIGZString *out, uint32_t country) = 0;            // 34 ("UnitedStates")
    virtual bool GetCountryNameAgain(cIGZString *out, uint32_t country) = 0;       // 35: slot 34
    virtual bool CountryFromName(const cIGZString *name, uint32_t *country) = 0;   // 36
    virtual bool CountryFromNameAgain(const cIGZString *name, uint32_t *country) = 0; // 37: through slot 35
    virtual cIGZLanguageUtility *GetLanguageUtility(uint32_t language) = 0;        // 38
    virtual cIGZLanguageUtility *CreateLanguageUtility(uint32_t language) = 0;     // 39
    virtual float vf40(uint32_t value) = 0;                                        // 40: 1.0
    virtual void vf41(uint32_t a, uint32_t b) = 0;                                 // 41: nothing (eax untouched)
    // 42 to 45: store the value(s) through the out pointer and return true; the other words are popped unread.
    virtual bool vf42(uint32_t lo, uint32_t hi, uint32_t *out, uint32_t unused) = 0;
    virtual bool vf43(uint32_t value, uint32_t *out, uint32_t unused) = 0;
    virtual bool vf44(uint32_t lo, uint32_t hi, uint32_t unused, uint32_t *out, uint32_t unused2) = 0;
    virtual bool vf45(uint32_t value, uint32_t unused, uint32_t *out, uint32_t unused2) = 0;
    // 46: the names (slot 16) of the allowed languages slot 14 steps to from 1, each step from the one after the last
    // listed (so language 1 is never listed, nor one right after a listed one); the list emptied first.
    virtual void GetLanguageNames(std::list<cRZLanguageNameEntry> *out) = 0;
    // 47: the allowed languages in order; the list emptied first.
    virtual void GetAllowedLanguages(std::list<uint32_t> *out) = 0;
};

// The CRT locale calls of the locale helper (0xc74831): our CRT by default (the standalone build). While the image's
// CRT is in the process the game's other code reads that CRT's locale, so the host should bind these to the image's
// setlocale (0x837dcc) and _setmbcp (0x84a8d7) (not done yet: lane LS's report).
struct cRZLanguageManagerCrt {
    char *(__cdecl *setLocale)(int category, const char *locale);
    int(__cdecl *setMbcp)(int codePage);
};
cRZLanguageManagerCrt &LanguageManagerCrt();

// cRZLanguageManager (0x68 bytes): the language service, in the standalone shape (docs/standalone-shape.md). RTTI bases
// cIGZLanguageManager at +0, cRZSystemService at +4 (cIGZSystemService +4, cRZUnknownMT +8); game vtables 0xe9f0a0 (48
// slots), 0xe9f164 (13), 0xe9f19c (6). Service id 0x441e5070, priority 0xffd23940.
//
// Creation: only the creator 0xc6f338 (registered by cTSAppFWCOMDirector as class 0xa3024279) builds one, through the
// constructor 0xc6ed1c; its hook builds this class, so no manager carries the game's vtables (referenced only by that
// constructor and the destructor 0xc6edcf, reached only from the deleting destructor 0xc6ee2a and its +8 thunk 0xc6ee22).
//
// Slots (game RVAs): QueryInterface 0xc747ac (+4 / +8 thunks 0xc747e9 / 0xc747f1); AddRef / Release the folded 0x145240
// / 0x1a85b0 (the service part's); Init / Shutdown the shared 0x144910 (true; the service face's slots 8 / 9 reach them
// through 0xb31c78); 5 0xc6ee68, 6 0xc748f0, 7 the folded 0x147770, 8 0xc74960, 9 0xc6eeab, 10 0xc70b25, 11 0xc731c6, 12
// 0xc71bd8, 13 0xc7225f, 14 0xc7301d, 15 0xc7193d, 16 0xc71964, 17 0xc7214a, 18 0xc7207e, 19 0xc720e4, 20 0xc7202b, 21
// 0x55e17e, 22 the shared 0x96de02 (`jmp [eax + 0x44]`), 23 0xc71fc4, 24 0xc716bb, 25 0xc718d7, 26 0xc71342, 27
// 0xc71656, 28 the folded 0x1457d0, 29 0xc7491f, 30 0xc7304b, 31 0xc711c2, 32 the folded 0x1b86e0, 33 the folded
// 0x148260, 34 0xc705fb, 35 0xc709e7, 36 0xc70917, 37 0xc7097f, 38 0xc724ab, 39 0xc72fe8, 40 the shared 0x20d940, 41
// the folded 0x1b9a50, 42 0xc6ef7b, 43 0xc6ef6c, 44 0xc6efa0, 45 0xc6ef91, 46 0xc7047f, 47 0xc70433.
class cRZLanguageManager : public cIGZLanguageManager, public rz::cRZSystemService {
public:
    static constexpr uint32_t kVTable = 0xe9f0a0; // the game's, for tests
    static constexpr uint32_t kSystemServiceVTable = 0xe9f164;
    static constexpr uint32_t kUnknownVTable = 0xe9f19c;
    static constexpr uint32_t kSize = 0x68;
    static constexpr uint32_t kServiceId = 0x441e5070;
    static constexpr int32_t kPriority = static_cast<int32_t>(0xffd23940);
    static constexpr uint32_t kDefaultLanguage = 1;
    static constexpr uint32_t kDefaultCountry = 0x36;
    static constexpr uint32_t kLastLanguage = 0x2c;
    static constexpr uint32_t kLastCountry = 0x38;

    // 0xc6ed1c: the service part, the allowed set empty, language 1, country 0x36, then the utility (constructed in
    // place) Init'ed and given the language and country through its slots 32 and 33.
    cRZLanguageManager();
    // 0xc6edcf (deleting 0xc6ee2a, +8 thunk 0xc6ee22): the utility's Shutdown (slot 4), its destructor, the set, the
    // service part.
    ~cRZLanguageManager() override;
    cRZLanguageManager(const cRZLanguageManager &) = delete;
    cRZLanguageManager &operator=(const cRZLanguageManager &) = delete;

    // 0xc6f338 (__cdecl, the class's creator): a new manager's cIGZSystemService face.
    static cIGZSystemService *__cdecl Create();

    // cIGZUnknown (every face)
    bool QueryInterface(uint32_t riid, void **ppv) override;
    uint32_t AddRef() override;
    uint32_t Release() override;

    // cIGZLanguageManager (Init / Shutdown also the service face's)
    bool Init() override;
    bool Shutdown() override;
    void AllowLanguage(uint32_t language) override;
    void DisallowLanguage(uint32_t language) override;
    uint32_t GetLanguage() override;
    bool SetLanguage(uint32_t language) override;
    bool IsLanguageAllowed(uint32_t language) override;
    uint32_t GetUserDefaultLanguage() override;
    uint32_t GetLanguageLcid(uint32_t language) override;
    uint32_t LanguageFromLcid(uint32_t lcid) override;
    bool GetLocaleName(cIGZString *out, uint32_t language) override;
    bool NextAllowedLanguage(uint32_t *language) override;
    bool HasLanguageName(cIGZString *out, uint32_t language) override;
    bool GetLanguageName(cIGZString *out, uint32_t language) override;
    bool GetNativeLanguageName(cIGZString *out, uint32_t language) override;
    bool LanguageFromName(const cIGZString *name, uint32_t *language) override;
    bool LanguageFromNativeName(const cIGZString *name, uint32_t *language) override;
    bool TranslateLanguageName(cIGZString *name, uint32_t language) override;
    bool GetLanguageNameAgain(cIGZString *out, uint32_t language) override;
    bool GetNativeLanguageNameAgain(cIGZString *out, uint32_t language) override;
    uint32_t GetBaseLanguage(uint32_t language) override;
    bool GetLanguageCode(cIGZString *out, uint32_t language) override;
    bool LanguageFromCode(const cIGZString *code, uint32_t *language) override;
    bool GetCountryCode(cIGZString *out, uint32_t country) override;
    bool CountryFromCode(const cIGZString *code, uint32_t *country) override;
    uint32_t GetCountry() override;
    bool SetCountry(uint32_t country) override;
    uint32_t GetCountryLanguage(uint32_t country) override;
    uint32_t GetLanguageCountry(uint32_t language) override;
    int32_t vf32() override;
    bool vf33(uint32_t value) override;
    bool GetCountryName(cIGZString *out, uint32_t country) override;
    bool GetCountryNameAgain(cIGZString *out, uint32_t country) override;
    bool CountryFromName(const cIGZString *name, uint32_t *country) override;
    bool CountryFromNameAgain(const cIGZString *name, uint32_t *country) override;
    cIGZLanguageUtility *GetLanguageUtility(uint32_t language) override;
    cIGZLanguageUtility *CreateLanguageUtility(uint32_t language) override;
    float vf40(uint32_t value) override;
    void vf41(uint32_t a, uint32_t b) override;
    bool vf42(uint32_t lo, uint32_t hi, uint32_t *out, uint32_t unused) override;
    bool vf43(uint32_t value, uint32_t *out, uint32_t unused) override;
    bool vf44(uint32_t lo, uint32_t hi, uint32_t unused, uint32_t *out, uint32_t unused2) override;
    bool vf45(uint32_t value, uint32_t unused, uint32_t *out, uint32_t unused2) override;
    void GetLanguageNames(std::list<cRZLanguageNameEntry> *out) override;
    void GetAllowedLanguages(std::list<uint32_t> *out) override;

    // 0xc74831 (__cdecl, this and the language): the language's CRT locale name (slot 13); when there is one, the CRT's
    // collation, monetary, numeric and time categories set to it (RZText::SetLocale 0x1b18b5), stopping at the first
    // that fails, and after all four the multibyte code page to the locale's (_setmbcp(-4), _MB_CP_LOCALE). Whether
    // there was a name.
    bool ApplyLocale(uint32_t language);
    // 0x1b18b5 (__cdecl): forgets the cached system code page (RZText::SystemCodePage), then the CRT's setlocale.
    static char *SetLocale(int category, const char *locale);

    // Shared body of slots 18, 19, 25, 27, 36 and 37: the first id from 1 to `last` that `getName` (a slot of this,
    // called through the vtable) answers true for with a text the utility's Compare (case-insensitive) finds equal to
    // `name`; otherwise `none`. One string, made before the loop, takes every candidate.
    using NameGetter = bool (cIGZLanguageManager::*)(cIGZString *out, uint32_t id);
    bool FindByName(NameGetter getName, uint32_t last, uint32_t none, const cIGZString *name, uint32_t *id);

    uint32_t mUnknown20;                // +0x20 cleared by the constructor, read by nothing here
    std::set<uint32_t> mAllowedLanguages; // +0x24 (empty: every language allowed)
    uint32_t mLanguage;                 // +0x2c
    uint32_t mCountry;                  // +0x30
    cRZLanguageUtility mUtility;        // +0x34 the current language's utility (slot 38 hands it out)
};

static_assert(sizeof(cRZLanguageManager) == cRZLanguageManager::kSize, "cRZLanguageManager size");
static_assert(offsetof(cRZLanguageManager, mServiceId) == 0x10, "cRZLanguageManager layout: the service part at +4");
static_assert(offsetof(cRZLanguageManager, mRefCount) == 0xc, "cRZLanguageManager layout: cRZUnknownMT at +8");
static_assert(offsetof(cRZLanguageManager, mUnknown20) == 0x20, "cRZLanguageManager layout");
static_assert(offsetof(cRZLanguageManager, mAllowedLanguages) == 0x24, "cRZLanguageManager layout");
static_assert(offsetof(cRZLanguageManager, mLanguage) == 0x2c, "cRZLanguageManager layout");
static_assert(offsetof(cRZLanguageManager, mCountry) == 0x30, "cRZLanguageManager layout");
static_assert(offsetof(cRZLanguageManager, mUtility) == 0x34, "cRZLanguageManager layout");
