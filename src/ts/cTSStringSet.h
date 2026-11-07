#pragma once
#include "rz/cRZCmdLine.h" // cRZStringVector
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

class cIGZString;

// cTSStringSet: the text resource behind STR#, CTSS, TTAs and FAMs records (framework class 0xdb4cb0f9,
// interface cITSStringSet 0xfb4cb0de, own class iid 0xfb4cbaab). Structure pass: lane DA, handoff in
// notes/feedback/DA.md. Every member is __thiscall and /O2 ("speed" profile); RVAs are 1.18.0.187.
//
// Texts are stored per language (the record's language byte minus 1; 0 is the default, -1 as an argument means the
// current language, cached in 0xff275c from the language service). Indices are 1-based. A set is created empty by
// its factory with only its key; GetStrings/GetComments load one language at a time from the record on first use.
//
// The map value owns its strings: the original's map destructor (_Erase_tree 0x1eab00) Tidies each vector
// (0x1441eb) and its copy-assign (0x1eaf90, library) copies them. StringList gives cRZStringVector those semantics.
struct cTSStringSetStringList : cRZStringVector {
    cTSStringSetStringList() : cRZStringVector{} {}
    cTSStringSetStringList(const cTSStringSetStringList &other);            // copy-construct the strings
    cTSStringSetStringList &operator=(const cTSStringSetStringList &other); // assign (map copy-assign)
    ~cTSStringSetStringList();                                              // Tidy 0x1441eb
};
static_assert(sizeof(cTSStringSetStringList) == 12);

// std::map<signed char, vector<cRZString>>: 8 bytes (head, size), 0x20-byte nodes (key at +0x10, vector at +0x14).
// The key compares signed (jl in the original's lower_bound 0x1eab50).
using cTSStringSetMap = std::map<signed char, cTSStringSetStringList>;
static_assert(sizeof(cTSStringSetMap) == 8);

class cTSStringSet {
public:
    static constexpr uint32_t kVTable = 0xda67fc;        // cITSStringSet, 38 slots
    static constexpr uint32_t kPersistVTable = 0xda6898; // cIGZPersistResource face at +4, 12 slots
    static constexpr uint32_t kIid = 0xfb4cb0de;         // cITSStringSet
    static constexpr uint32_t kClassIid = 0xfb4cbaab;    // cTSStringSet itself (QI gives the primary)
    static constexpr uint32_t kClassId = 0xdb4cb0f9;     // framework class (COMDirectorData: 0x1eba70)
    static constexpr uint32_t kTypeSTR = 0x53545223;     // 'STR#': SetKey's default type
    static constexpr uint32_t kTypeCTSS = 0x43545353;    // 'CTSS' catalog descriptions
    static constexpr uint32_t kTypeTTAs = 0x54544173;    // 'TTAs' pie-menu strings
    static constexpr int kLanguageCount = 44;            // loops run 0..43
    static constexpr int kNameSize = 0x40;               // record name field (factory Write pads to it)

    // The cIGZPersistResource face (+4). Its slots 1, 5..7 and 11 are shared verified bodies; slots 3/4/8/9/10 are
    // shared_vf (folded with other classes: not hooked) and act on the key at this + 8 (= primary + 0xc).
    struct PersistFace {
        const void *mVTable;
        bool QueryInterface(uint32_t riid, void **out); // 0x1ee2d2: adjustor thunk (this - 4) to 0x1ee280
        uint32_t Release();                             // 0x1ee34a: adjustor thunk to 0x1ee330
    };

    // Construction (also used by the factory and the COM creator 0x1eba70: new 0x7c, Construct).
    cTSStringSet *Construct();                       // 0x1eace0 (EH: states 0/1 around the two map heads)
    void Destruct();                                 // 0x1eae50 body (no EH)
    void *DeletingDestructor(uint32_t flags);        // 0x1eb230, slot 35 (ret 4): delete(this, 0x7c) when bit 0
    static cTSStringSet *Create();                   // 0x1eba70 (cdecl, 86 B): COM creator for kClassId

    // cITSStringSet (primary vtable). Slot 1 (AddRef 0x145260), 3 (GetPersistResource 0x1c04e0), 19 (Load 0x6c6b0:
    // returns 0) and 36 (GetGroup 0x149cf0) are shared verified bodies; 26 (0x1ebbc0) and 37 (0x1eeb80) are shared_vf.
    bool QueryInterface(uint32_t riid, void **out);                            // 0x1ee280, slot 0 (ret 8)
    uint32_t Release();                                                        // 0x1ee330, slot 2
    void CopyFrom(void *other);                                                // 0x1eb620, slot 4 (ret 4)
    uint32_t GetStringCount(int32_t language);                                 // 0x1eb6e0, slot 5 (ret 4)
    const cRZString *GetString(uint32_t index, int32_t language);              // 0x1ebfb0, slot 6 (ret 8)
    bool GetStringInto(cIGZString *out, uint32_t index, int32_t language);     // 0x1ec080, slot 7 (ret 0xc)
    bool GetStringExact(cIGZString *out, uint32_t index, int32_t language);    // 0x1ebb70, slot 8 (ret 0xc)
    void CopyStringFrom(void *source, uint32_t sourceIndex, uint32_t index);   // 0x1eeca0, slot 9 (ret 0xc)
    void SetString(const cIGZString *text, uint32_t index, int32_t language);  // 0x1eed90, slot 10 (ret 0xc)
    void InsertString(const cIGZString *text, uint32_t index, int32_t unusedLanguage); // 0x1ec360, slot 11 (ret 0xc: the third argument is never read)
    uint32_t AppendStringFrom(void *source, uint32_t sourceIndex);             // 0x1eb510, slot 12 (ret 8)
    uint32_t AppendString(const cIGZString *text, int32_t language);           // 0x1eb550, slot 13 (ret 8)
    void RemoveString(uint32_t index, int32_t unusedLanguage);                 // 0x1ee3b0, slot 14 (ret 8: the second argument is never read)
    bool GetComment(cIGZString *out, uint32_t index, int32_t unusedLanguage); // 0x1ebb20, slot 15 (ret 0xc: the third argument is never read)
    void SetComment(const cIGZString *text, uint32_t index, int32_t unusedLanguage); // 0x1ee930, slot 16 (ret 0xc: the third argument is never read)
    void SetKey(const uint32_t *key);                                          // 0x1eeba0, slot 17 (ret 4)
    void SetKeyFromFile(void *file, uint32_t instance, uint32_t type);         // 0x1eebf0, slot 18 (ret 0xc)
    uint32_t LoadFromFile(void *file, uint32_t instance);                      // 0x1ec790, slot 20 (ret 8)
    uint32_t Commit();                                                         // 0x1ee510, slot 21: always 0
    uint32_t SaveToFile(void *file, uint32_t instance);                        // 0x1ee4f0, slot 22 (ret 8)
    void LoadAll();                                                            // 0x1eb8f0, slot 23
    bool GetName(cIGZString *out);                                             // 0x1ebbe0, slot 24 (ret 4)
    void SetName(const cIGZString *text);                                      // 0x1eec60, slot 25 (ret 4, tail jump)
    uint16_t GetInstance();                                                    // 0x1ebbc0, slot 26 (shared_vf, 5 B)
    void SetInstance(int16_t instance);                                        // 0x1eeb90, slot 27 (ret 4)
    bool IsWritable();                                                         // 0x1eb580, slot 28
    void SortByText();                                                         // 0x1eefd0, slot 29
    void ResetSortOrder();                                                     // 0x1ef250, slot 30
    uint32_t GetSortedIndex(uint32_t position);                                // 0x1ebf20, slot 31 (ret 4)
    void Clear();                                                              // 0x1ee360, slot 32
    void SetShowSource(uint8_t show);                                          // 0x1ee690, slot 33 (ret 4)
    uint8_t GetShowSource();                                                   // 0x1ebb10, slot 34 (4 B: not hookable)
    void SetGroup(uint32_t group);                                             // 0x1eeb80, slot 37 (shared_vf)

    // Non-virtual members.
    cRZStringVector *GetStrings(signed char language);       // 0x1ecf60 (ret 4, 4.8 KB): find or load mStrings
    cRZStringVector *GetComments(signed char language);      // 0x1ec7e0 (ret 4, 1.9 KB): find or load mComments
    cRZStringVector *GetOrAddStrings(signed char language);  // 0x1eb860 (ret 4): find or insert, no loading
    void LoadAllLanguages();                                 // 0x1ec7b0: GetStrings(0..43)
    void MarkNeedsText(uint32_t index, int32_t language);    // 0x1eb250 (ret 8): placeholder comment
    bool WriteStrings(void *stream);                         // 0x1ef4e0 (ret 4): the 0xfffd body (cIGZOStream)
    static bool OpenResourceRecord(const uint32_t *key, void **record); // 0x1ec1c0 (cdecl)
    static bool IsValidUtf8(const uint8_t *bytes, int32_t length);      // 0x1b285f (cdecl): the whole text is well-formed UTF-8

    const void *mVTable;          // +0x00 cITSStringSet
    PersistFace mPersist;         // +0x04 cIGZPersistResource
    uint32_t mRefCount;           // +0x08
    uint32_t mKey[4];             // +0x0c type, group, instance, instance high (face key: mPersist + 8)
    cRZString mName;              // +0x1c the record's 64-byte name (written by GetStrings/GetComments)
    cTSStringSetMap mStrings;     // +0x3c language -> texts
    cTSStringSetMap mComments;    // +0x44 language -> comments (only language 0 is used)
    std::vector<uint32_t> mSortOrder; // +0x4c positions 0..count (slots 29-31; push_back realloc 0x18b860)
    uint8_t mShowSource;          // +0x58 slot 33/34; GetStringInto appends the source for language -1
    cRZString mSourceName;        // +0x5c file name of the key's segment (slot 33)
};

static_assert(sizeof(cTSStringSet) == 0x7c, "cTSStringSet layout (factory news 0x7c)");
static_assert(offsetof(cTSStringSet, mPersist) == 0x04);
static_assert(offsetof(cTSStringSet, mRefCount) == 0x08);
static_assert(offsetof(cTSStringSet, mKey) == 0x0c);
static_assert(offsetof(cTSStringSet, mName) == 0x1c);
static_assert(offsetof(cTSStringSet, mStrings) == 0x3c);
static_assert(offsetof(cTSStringSet, mComments) == 0x44);
static_assert(offsetof(cTSStringSet, mSortOrder) == 0x4c);
static_assert(offsetof(cTSStringSet, mShowSource) == 0x58);
static_assert(offsetof(cTSStringSet, mSourceName) == 0x5c);

// cTSResourceFactoryStringSet (framework class 0xdb4cb1e8, cIGZPersistResourceFactory vtable 0xda68cc, 8 slots,
// 8 bytes). Registered for STR# and CTSS (cTSPersistSystem::Init). Slots 0/2 are the factories' shared_vf bodies
// 0x1ee250/0x1ee310 (QI ids 1 and 0xa56b8f17; Release calls slot 7 below 2); slot 1 is 0x148fb0 (verified); slot 5
// Read is shared_vf 0x1474b0 (returns 0: strings load lazily).
class cTSResourceFactoryStringSet {
public:
    static constexpr uint32_t kVTable = 0xda68cc;
    static constexpr uint32_t kClassId = 0xdb4cb1e8;

    static cTSResourceFactoryStringSet *Create(); // 0x1eba40 (cdecl, 37 B; not a Ghidra function): new 8, vtable, 0
    bool CreateInstance(void *record, uint32_t riid, void **out, uint32_t a4, uint32_t a5);     // 0x1eb710, slot 3
    bool CreateInstanceByType(uint32_t type, uint32_t riid, void **out, uint32_t a4, uint32_t a5); // 0x1eb7c0, slot 4
    bool Write(void *resource, void *record);              // 0x1ef2c0, slot 6 (ret 8)
    void *DeletingDestructor(uint32_t flags);              // 0x1eb200, slot 7 (ret 4)

    const void *mVTable; // +0
    uint32_t mRefCount;  // +4
};

static_assert(sizeof(cTSResourceFactoryStringSet) == 8);
