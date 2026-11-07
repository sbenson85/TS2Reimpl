#pragma once
#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

#include "game/owned.h"
#include "gz/cIGZPropertySet.h" // GZServiceString, the two set interfaces

class cIGZString;

// The GZ framework's property sets (cGZPropertySetT<cKeyUint32TraitsT> and cGZPropertySetString) and the cIGZAny
// values they hold. Structure pass: lane DM (notes/feedback/DM.md has the per-function handoff); bodies lane DP; the
// standalone shape (docs/standalone-shape.md) lane EQ. RVAs 1.18.0.187.
// Both instantiations are /O1 code (0x1be000-0x1bf900, built for size: implementations go in a .size.cpp).
//
// Values are cIGZAny objects (vtable + payload) compared by type id, never by C++ type.
//
// Users: every property-set record (cGZPropertySetFactory, 0xebfee33f genetic data / 0xebfee342 save-game info),
// the CAS records built through a temporary property set (cTSBinInfoFactory: BINX, collections, outfits; skin
// entries, product patterns, model materials, the want tree manager), the application's property manager service
// (0x0ac1887e, src/gz/cGZPropertyManager.h, derived from cGZPropertySetString; src/app/AppInit.h still has
// construct-only copies of these two classes: appinit::cGZPropertySetBase / cGZPropertySetString), SimAntics prims (NewPropertySet), tuning lookups (cTSFamily), cTSSimDNA.

// ---------------------------------------------------------------------------------------------------------------
// cIGZAny (abstract, vtable 0xd9c654: slot 0 is the deleting destructor 0x14c840, the rest _purecall).
//   slot 0 DeletingDestructor(flags)       slot 3 Clone() -> new copy (operator new)
//   slot 1 bool Assign(const cIGZAny *src)  only when src->GetType() == GetType() (else 0, nothing copied)
//   slot 2 int Compare(const cIGZAny *o)    -1 / 0 / 1 (AnyString: this string's slot 15 compare, case flag 1)
//   slot 4 uint32_t GetType()               slot 5 const char *GetTypeName() (the XML element name)
// The value classes themselves (AnyUint32 ... AnyStringSet) are lane DQ's: src/gz/cGZAny.h. AnyStringSet is 0x24 bytes.
namespace gzany {
constexpr uint32_t kAnyVTable = 0xd9c654;         // cIGZAny; what a destroyed Any's vtable is reset to

constexpr uint32_t kTypeUint32 = 0xeb61e4f7;      // AnyUint32, vtable 0xd9f534 (slots 0x14c5f0 0x177b90 0x177b70 0x177ab0 0x179d00 0x179d10)
constexpr uint32_t kTypeSint32 = 0x0c264712;      // AnySint32, vtable 0xda3d48
constexpr uint32_t kTypeFloat32 = 0xabc78708;     // AnyFloat32, vtable 0xda3d70
constexpr uint32_t kTypeBoolean = 0xcba908e1;     // AnyBoolean, vtable 0xd9c670
constexpr uint32_t kTypeString = 0x0b8bea18;      // AnyString, vtable 0xda3d98 (payload: counted cIGZString*)
constexpr uint32_t kTypeStringSet = 0xad9a861e;   // AnyStringSet, vtable 0xdac644 (an unordered_set of service strings at +4: cGZAny.h)
constexpr uint32_t kTypeAnyAny = 0xae24d4f7;      // AnyAny (anonymous namespace), vtable 0xda3e00: wraps a Clone

constexpr uint32_t kUint32VTable = 0xd9f534;
constexpr uint32_t kSint32VTable = 0xda3d48;
constexpr uint32_t kFloat32VTable = 0xda3d70;
constexpr uint32_t kBooleanVTable = 0xd9c670;
constexpr uint32_t kStringVTable = 0xda3d98;
constexpr uint32_t kStringSetVTable = 0xdac644;
constexpr uint32_t kAnyAnyVTable = 0xda3e00;
} // namespace gzany

// The family's vtables and type names as owned statics (game/owned.h; lane VX): in hooked builds the image's (every
// slot hooked, src/hook/tables/cGZAny.cpp), in an image-free program our tables (gz/cGZAnyTables.cpp) and texts
// (standalone/owned_statics.cpp). The type names are the texts right after each 6-slot vtable (vtable + 0x18).
TS2_OWNED_STATIC(const OwnedVTable, d9c654); // cIGZAny
TS2_OWNED_STATIC(const OwnedVTable, d9f534); // AnyUint32
TS2_OWNED_STATIC(const OwnedVTable, da3d48); // AnySint32
TS2_OWNED_STATIC(const OwnedVTable, da3d70); // AnyFloat32
TS2_OWNED_STATIC(const OwnedVTable, d9c670); // AnyBoolean
TS2_OWNED_STATIC(const OwnedVTable, da3d98); // AnyString
TS2_OWNED_STATIC(const OwnedVTable, da3e00); // AnyAny
TS2_OWNED_STATIC(const char, da3d60)[];      // "AnySint32"
TS2_OWNED_STATIC(const char, da3db0)[];      // "AnyString"
TS2_OWNED_STATIC(const char, da3e18)[];      // "AnyAny"

namespace gzany {
inline const void *AnyVTable() { return &ts2_rva_d9c654; }
inline const void *Uint32VTable() { return &ts2_rva_d9f534; }
inline const void *Sint32VTable() { return &ts2_rva_da3d48; }
inline const void *Float32VTable() { return &ts2_rva_da3d70; }
inline const void *BooleanVTable() { return &ts2_rva_d9c670; }
inline const void *StringVTable() { return &ts2_rva_da3d98; }
inline const void *AnyAnyVTable() { return &ts2_rva_da3e00; }
} // namespace gzany

// An entry's inline cIGZAny: one of the 8-byte value classes of src/gz/cGZAny.h (AnyUint32, AnySint32, AnyFloat32,
// AnyBoolean, AnyString, AnyAny), held as its common shape: vtable + one payload dword.
struct InlineAny {
    const void *mVTable; // +0
    uint32_t mValue;     // +4 (AnyBoolean: low byte; AnyString: cIGZString*; AnyAny: the held clone)
};
static_assert(sizeof(InlineAny) == 8);

// ---------------------------------------------------------------------------------------------------------------
// One property: key + an inline cIGZAny. Plain data: the set copies entries as 16 raw bytes (the library vector
// emplace 0x1beb5b, erase's memmove), so it has no constructor or destructor of its own; Construct / Destroy are
// called explicitly, as in the original.
struct cGZPropertyEntry {
    // 0x1bec9b (ret 8): key, then a copy of value by type: the five built-in types inline (AnyString through its
    // copy constructor 0x1bec79, which shares the text object), anything else as AnyAny {0xda3e00, value->Clone()}.
    cGZPropertyEntry *Construct(uint32_t key, void *value);
    void *Value();   // 0x1bee0a: the stored cIGZAny (&mValue, or the clone an AnyAny holds)
    void Destroy();  // 0x1bf210: an AnyAny's clone is deleted (slot 0, flag 1), then the inline value destroyed (slot 0, 0)

    uint32_t mKey;         // +0 (string sets: the offset of the key text)
    InlineAny mValue;      // +4 inline cIGZAny (vtable + payload; AnyAny: payload is the cIGZAny* clone)
    uint32_t mValueExtra;  // +0xc never written: whatever the stack held where the original built the entry
};
static_assert(sizeof(cGZPropertyEntry) == 16);
static_assert(std::is_trivially_copyable_v<cGZPropertyEntry>);

// ---------------------------------------------------------------------------------------------------------------
// What both sets keep after their vtable pointer (+0x04..+0x18): the count, the entries sorted by key (std::lower_bound:
// the original's 0x1bebca) and the GetFirst / GetNext cursor, with the uint32-keyed bodies the original's cGZPropertySetT
// template gives both instantiations (the string set calls the uint32 set's slot bodies directly with the key text's offset as
// the key). A plain base (no vtable): in a set it sits at +4, after the interface's vtable pointer.
namespace gz {
class cGZPropertyTable {
public:
    using EntryVector = std::vector<cGZPropertyEntry>;

    cGZPropertyEntry *Find(uint32_t key);                  // the entry with this key, or null (every lookup)
    bool SetEntry(uint32_t key, void *value);              // 0x1bf49c's body
    bool RemoveEntry(uint32_t key);                        // 0x1bee49's body
    void ClearEntries();                                   // 0x1bee23 (the cursor is left as it was)
    bool FirstEntry(uint32_t *key, void **value);          // 0x1bf239's body (null arguments refused)
    bool NextEntry(uint32_t *key, void **value);           // 0x1bf389's body
    void *EntryValue(uint32_t key);                        // 0x1bf5e9's body
    bool EntryValueInto(uint32_t key, void *out);          // 0x1bf626's body
    uint32_t EntryCount() { return static_cast<uint32_t>(mEntries.size()); } // 0x1bf41f

    uint32_t mRefCount = 0;              // set +0x04
    EntryVector mEntries;                // set +0x08 sorted by mKey
    cGZPropertyEntry *mCursor = nullptr; // set +0x14 GetFirst / GetNext position
};
static_assert(sizeof(cGZPropertyTable) == 0x14);
} // namespace gz

// ---------------------------------------------------------------------------------------------------------------
// cGZPropertySetT<cKeyUint32TraitsT<unsigned long, 0xebb10cdd, 0x6bb22f3c>> (iid 0xebb10cdd), 0x18 bytes.
// vtable 0xda3e24, 15 slots (interface cIGZPropertySetUint32): 0 QI 0x1bf44d, 1 AddRef 0x148fb0 (shared), 2 Release
// 0x1bf485, 3 SetProperty 0x1bf49c, 4 RemoveProperty 0x1bee49, 5 Clear 0x1bee23, 6 GetFirst 0x1bf239, 7 GetNext
// 0x1bf389, 8 GetProperty 0x1bf5e9, 9 GetPropertyValue 0x1bf626, 10 HasProperty 0x1bf2cc, 11 GetCount 0x1bf41f,
// 12 CopyFrom 0x1bf148, 13 DeletingDestructor 0x1bedac, 14 KeyToString 0x1bf336.
// AddRef, Release, Clear, GetCount and KeyToString are folded with other classes' slots (shared_vf_*): written here, not hooked.
//
// Standalone shape (docs/standalone-shape.md, lane EQ): a real C++ class, the original's template instantiation by name (its RTTI
// name is the original's). Creation sites: the constructor 0x1bec5f (hooked: placement new, so the 14 game callers (sounds, the
// alias modifier, hit tracks, ...) get our vtable), the uint32 resource (ours, a base). The vtable 0xda3e24 is referenced by the
// constructor and the destructor 0x1bed39 alone. The slot hooks are adapters (cGZPropertySetUint32Adapters).
template <class Traits> class cGZPropertySetT;
using cGZPropertySetUint32 = cGZPropertySetT<cKeyUint32Traits>;

template <> class cGZPropertySetT<cKeyUint32Traits> : public cIGZPropertySetUint32, public gz::cGZPropertyTable {
public:
    static constexpr uint32_t kOriginalVTable = 0xda3e24;
    static constexpr uint32_t kIid = 0xebb10cdd;

    cGZPropertySetT() = default;   // 0x1bec5f (vtable, +4..+0x14 zero)
    ~cGZPropertySetT() override;   // 0x1bed39: Clear, free the entries; slot 13 the scalar deleting destructor 0x1bedac (0x18)
    cGZPropertySetT(const cGZPropertySetT &) = delete;
    cGZPropertySetT &operator=(const cGZPropertySetT &) = delete;

    bool QueryInterface(uint32_t riid, void **out) override;           // slot 0, 0x1bf44d
    uint32_t AddRef() override;                                        // slot 1, folded 0x148fb0
    uint32_t Release() override;                                       // slot 2, folded 0x1bf485
    bool SetProperty(const uint32_t *key, void *value) override;       // slot 3, 0x1bf49c (ret 8)
    bool RemoveProperty(const uint32_t *key) override;                 // slot 4, 0x1bee49
    void Clear() override;                                             // slot 5, folded 0x1bee23
    bool GetFirst(uint32_t *key, void **value) override;               // slot 6, 0x1bf239
    bool GetNext(uint32_t *key, void **value) override;                // slot 7, 0x1bf389
    void *GetProperty(const uint32_t *key) override;                   // slot 8, 0x1bf5e9
    bool GetPropertyValue(const uint32_t *key, void *out) override;    // slot 9, 0x1bf626
    bool HasProperty(const uint32_t *key) override;                    // slot 10, 0x1bf2cc
    uint32_t GetCount() override;                                      // slot 11, folded 0x1bf41f
    void CopyFrom(cIGZPropertySetUint32 *other) override;              // slot 12, 0x1bf148
    const char *KeyToString(uint32_t key, cIGZString *out) override;   // slot 14, folded 0x1bf336
    static const char *__cdecl FormatKey(const uint32_t *key, cIGZString *out); // 0x1bf349: "0x%08x" (0xda3e60)
};
static_assert(sizeof(cGZPropertySetUint32) == 0x18);
static_assert(offsetof(cGZPropertySetUint32, mRefCount) == 4);
static_assert(offsetof(cGZPropertySetUint32, mEntries) == 8);
static_assert(offsetof(cGZPropertySetUint32, mCursor) == 0x14);

// The uint32 set's RVA hooks' entry points (src/hook/tables/cGZPropertySet.cpp): the constructor for the game's creation sites,
// the rest for direct calls and sets built with the game's vtable (none while the constructor hook is on). Original calling
// convention (this in ecx), our members called without virtual dispatch, bodies inlined.
struct cGZPropertySetUint32Adapters {
    using Self = cGZPropertySetUint32;
    static Self *__fastcall Construct(void *memory, void *);                                    // 0x1bec5f
    static void __fastcall Destruct(Self *self, void *);                                        // 0x1bed39
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);             // 0x1bedac (slot 13)
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t riid, void **out);       // 0x1bf44d
    static bool __fastcall SetProperty(Self *self, void *, const uint32_t *key, void *value);   // 0x1bf49c
    static bool __fastcall RemoveProperty(Self *self, void *, const uint32_t *key);             // 0x1bee49
    static bool __fastcall GetFirst(Self *self, void *, uint32_t *key, void **value);           // 0x1bf239
    static bool __fastcall GetNext(Self *self, void *, uint32_t *key, void **value);            // 0x1bf389
    static void *__fastcall GetProperty(Self *self, void *, const uint32_t *key);               // 0x1bf5e9
    static bool __fastcall GetPropertyValue(Self *self, void *, const uint32_t *key, void *out); // 0x1bf626
    static bool __fastcall HasProperty(Self *self, void *, const uint32_t *key);                // 0x1bf2cc
    static void __fastcall CopyFrom(Self *self, void *, cIGZPropertySetUint32 *other);          // 0x1bf148
};

// ---------------------------------------------------------------------------------------------------------------
// cGZPropertySetString (RTTI base cGZPropertySetT<cKeyStringTraitsT<ibasic_string<char>, 0x4bb0d269, 0x6bb22f38>>,
// iid 0x4bb0d269), 0x24 bytes. Each distinct key's text is kept NUL-terminated in a vector<char>, and the entries
// (the uint32 set's bodies, called directly) are keyed by the text's byte offset. Lookups compare length then
// _memicmp: keys are case-insensitive, and the first spelling set is the one kept.
// vtable 0xd9f718 (interface cIGZPropertySetString; the RTTI base's own, 0xda3dc0, has _purecall in 3-10; its QI
// 0x1bf429, dtor 0x1bed22, deleting dtor 0x1bed8d): 0 QI 0x1bf429, 1 AddRef 0x148fb0, 2 Release 0x1bf485,
// 3 SetProperty 0x1bf515, 4 RemoveProperty 0x1bee9c, 5 Clear 0x1bee23 (leaves the key text), 6 GetFirst 0x1bf26d,
// 7 GetNext 0x1bf3c0, 8 GetProperty 0x1bf670, 9 GetPropertyValue 0x1bf69a, 10 HasProperty 0x1bf306, 11 GetCount
// 0x1bf41f, 12 CopyFrom 0x1bf0d1, 13 DeletingDestructor 0x177a70, 14 KeyToString 0x1bf36a.
//
// Standalone shape (docs/standalone-shape.md, lane EQ): a real C++ class (the abstract RTTI base cGZPropertySetT<string traits>
// is not a class of ours: nothing is ever built as one, and its destructor 0x1bed22 is DestroyEntries below). Creation sites:
// the constructor 0x22b157 (hooked in src/hook/tables/AppInit.cpp: placement new), our resource (a base) and our SimAntics
// NewPropertySet; the game's sites that build the set inline (the base constructor 0x1bec45, then the vtable 0xd9f718 stored by
// the caller: Prim6D's original, cTSConfigurationManager, 0x1774a0, 0xcb1790 / 0xcb18a0 / 0xcb1cf0 / 0xcb1e60 / 0xcb28b0,
// 0xccdf90) keep the game's vtable, served by the slot adapters.
class cGZPropertySetString : public cIGZPropertySetString, public gz::cGZPropertyTable {
public:
    static constexpr uint32_t kOriginalVTable = 0xd9f718;
    static constexpr uint32_t kOriginalBaseVTable = 0xda3dc0;
    static constexpr uint32_t kIid = 0x4bb0d269;
    static constexpr uint32_t kNoKey = 0xffffffff;

    cGZPropertySetString() { new (&mKeyText) std::vector<char>(); } // 0x22b157 (the base part 0x1bec45)
    // 0x1777e0: the key text first, then DestroyEntries (0x1bed22); slot 13 the scalar deleting destructor 0x177a70 (0x24)
    ~cGZPropertySetString() override;
    cGZPropertySetString(const cGZPropertySetString &) = delete;
    cGZPropertySetString &operator=(const cGZPropertySetString &) = delete;

    bool QueryInterface(uint32_t riid, void **out) override;                 // slot 0, 0x1bf429
    uint32_t AddRef() override;                                              // slot 1, folded 0x148fb0
    uint32_t Release() override;                                             // slot 2, folded 0x1bf485
    bool SetProperty(const GZServiceString *key, void *value) override;     // slot 3, 0x1bf515 (ret 8)
    bool RemoveProperty(const GZServiceString *key) override;               // slot 4, 0x1bee9c
    void Clear() override;                                                   // slot 5, folded 0x1bee23
    bool GetFirst(GZServiceString *key, void **value) override;             // slot 6, 0x1bf26d
    bool GetNext(GZServiceString *key, void **value) override;              // slot 7, 0x1bf3c0
    void *GetProperty(const GZServiceString *key) override;                 // slot 8, 0x1bf670
    bool GetPropertyValue(const GZServiceString *key, void *out) override;  // slot 9, 0x1bf69a
    bool HasProperty(const GZServiceString *key) override;                  // slot 10, 0x1bf306
    uint32_t GetCount() override;                                           // slot 11, folded 0x1bf41f
    void CopyFrom(cIGZPropertySetString *other) override;                   // slot 12, 0x1bf0d1
    const char *KeyToString(uint32_t keyOffset, cIGZString *out) override;  // slot 14, 0x1bf36a (ret 8)

    uint32_t InternKey(const GZServiceString *key);                   // 0x1bf191: offset, appending the text if new
    uint32_t FindKey(const GZServiceString *key);                     // 0x1befff: offset or kNoKey
    // 0x1bed22, the RTTI base's destructor: Clear, then the entries' storage freed (the key text is left).
    void DestroyEntries();

    // GetFirst / GetNext after the base call: the key text at offset into *key.
    void CopyKeyText(uint32_t offset, GZServiceString *key);

    // +0x18 the keys, NUL-terminated, in the order first set. A union member: the destructor frees it first, then clears the
    // entries (the original's order), which plain member destruction would reverse.
    union { std::vector<char> mKeyText; };
};
static_assert(sizeof(cGZPropertySetString) == 0x24);
static_assert(offsetof(cGZPropertySetString, mRefCount) == 4);
static_assert(offsetof(cGZPropertySetString, mEntries) == 8);
static_assert(offsetof(cGZPropertySetString, mCursor) == 0x14);
static_assert(offsetof(cGZPropertySetString, mKeyText) == 0x18);

// The string set's RVA hooks' entry points (src/hook/tables/cGZPropertySet.cpp): sets the game builds inline keep the game's
// vtable and reach our code through these; the constructor 0x22b157 is AppInit's entry (it builds ours).
struct cGZPropertySetStringAdapters {
    using Self = cGZPropertySetString;
    static Self *__fastcall Construct(void *memory, void *);                                          // 0x22b157
    static void __fastcall Destruct(Self *self, void *);                                              // 0x1777e0
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);                   // 0x177a70
    static void __fastcall DestroyEntries(Self *self, void *);                                        // 0x1bed22
    // 0x1bed8d, the RTTI base's slot 13: DestroyEntries, then (flags & 1) the base's 0x18 bytes freed; the key text is left.
    static Self *__fastcall DeletingDestructorBase(Self *self, void *, uint32_t flags);
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t riid, void **out);             // 0x1bf429
    static bool __fastcall SetProperty(Self *self, void *, const GZServiceString *key, void *value);   // 0x1bf515
    static bool __fastcall RemoveProperty(Self *self, void *, const GZServiceString *key);            // 0x1bee9c
    static bool __fastcall GetFirst(Self *self, void *, GZServiceString *key, void **value);          // 0x1bf26d
    static bool __fastcall GetNext(Self *self, void *, GZServiceString *key, void **value);           // 0x1bf3c0
    static void *__fastcall GetProperty(Self *self, void *, const GZServiceString *key);              // 0x1bf670
    static bool __fastcall GetPropertyValue(Self *self, void *, const GZServiceString *key, void *out); // 0x1bf69a
    static bool __fastcall HasProperty(Self *self, void *, const GZServiceString *key);               // 0x1bf306
    static void __fastcall CopyFrom(Self *self, void *, cIGZPropertySetString *other);                // 0x1bf0d1
    static const char *__fastcall KeyToString(Self *self, void *, uint32_t keyOffset, cIGZString *out); // 0x1bf36a
    static uint32_t __fastcall InternKey(Self *self, void *, const GZServiceString *key);             // 0x1bf191
    static uint32_t __fastcall FindKey(Self *self, void *, const GZServiceString *key);               // 0x1befff
};
