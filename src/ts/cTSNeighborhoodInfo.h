#pragma once
#include "gz/cGZPersistResourceManager.h" // cGZPersistResourceKey
#include "gz/cIGZPersistResource.h"
#include "gz/cIGZSelfDescribing.h"
#include "gz/cIGZSerializable.h"
#include "gz/cIGZUnknown.h"
#include "rz/RZUnknownBase.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>

// cTSNeighborhoodInfo: a neighbourhood's descriptive record (RTTI cTSNeighborhoodInfo; bases cITSNeighborhoodInfo at +0
// (vtable 0xdcbd50, 34 slots), cIGZSerializable at +4 (0xdcbddc, 6), cIGZPersistResource at +8 (0xdcbdf8, 12),
// cIGZSelfDescribing at +0xc (0xdcbe2c, 5), cRZUnknown at +0x10 (0xdcbe44, 6) with its count at +0x14). 0xd4 bytes.
//
// The record is a resource of the persist system (key at +0xb0: type, group, instance, high word); its two texts come from
// a string table resource found through that key, and a record object (+0xc0, counted) is created on demand. What the file
// holds (Read/Write, version 10): two texts (+0x58, +0x78) and a handful of words; the two texts at +0x18 / +0x38 (name,
// description) are not saved.
//
// Standalone shape (docs/standalone-shape.md, lane EA): a real C++ class deriving from its five interfaces, cRZUnknown a real
// base (rz/RZUnknownBase.h). The compiler emits the five vtables (slot order = declaration order), the adjustor thunks and the
// RTTI; the destructor is cRZUnknown's virtual one (slot 3 of the +0x10 face, the only face with a destructor slot, as the
// original). The game creates records at two sites: Create 0x4a6720 (the constructor inline; registered by address in the
// cTSSimCOMDirector constructor) and 0x767bc7 (in an unnamed function at 0x76794d, called by
// cTSWinProcNeighborhoodMainMenu: `operator new(0xd4)` then the constructor 0x4a6350). Both are hooked (Create to Create, the
// constructor to an adapter that placement-news this class), so every record carries our vtables. The game's vtables are
// referenced only by those two and the destructor 0x4a6500. The RVA hooks on the old slot functions are adapters
// (cTSNeighborhoodInfoAdapters). RTTI: the type descriptor 0x10a5e38 has no code references.

// The record's own interface (id 0xac8a7a29; the original's name, global as its RTTI). Field names where the meaning is known;
// "TextD"/"Kind"/"FieldAC" are saved words whose meaning is not known. The file prefix (+0x58) starts the names of the lot files (lane CJ).
class cITSNeighborhoodInfo : public cIGZUnknown {
public:
    virtual void SetName(const char *text) = 0;        // slot 3 (ret 4): assign to the name (+0x18)
    virtual cRZString *GetName() = 0;                  // slot 4, folded 0x158f90: &name
    virtual void SetDescription(const char *text) = 0; // slot 5, folded 0x2b5480 (add ecx, 0x3c; jmp assign)
    virtual cRZString *GetDescription() = 0;           // slot 6, folded 0x4a66f0
    virtual void SetFilePrefix(const char *text) = 0;       // slot 7, folded 0x458290
    virtual cRZString *GetFilePrefix() = 0;                 // slot 8, folded 0x238ec0
    virtual void SetTextD(const char *text) = 0;       // slot 9
    virtual cRZString *GetTextD() = 0;                 // slot 10, folded 0x4a6cf0
    virtual void SetNumber(int32_t number) = 0;        // slot 11
    virtual int32_t GetNumber() = 0;                   // slot 12, folded 0x253dc0
    virtual void SetFlags(uint32_t mask, bool set) = 0; // slot 13 (ret 8; the low byte of `set` only)
    virtual bool HasFlags(uint32_t mask) = 0;          // slot 14: every bit of the mask set
    virtual bool IsNumber(int32_t number) = 0;         // slot 15
    virtual bool MatchesName(const char *text) = 0;    // slot 16
    virtual void SetKind(uint32_t kind) = 0;           // slot 17, folded 0x4a71d0
    virtual uint32_t GetKind() = 0;                    // slot 18, folded 0x254fd0
    virtual void SetFieldAC(uint32_t value) = 0;       // slot 19, folded 0x4820e0
    virtual uint32_t GetFieldAC() = 0;                 // slot 20, folded 0x24ad60
    virtual void SetCategory(int32_t category) = 0;    // slot 21, folded 0x470710
    virtual int32_t GetCategory() = 0;                 // slot 22, folded 0x155e40
    virtual cIGZSerializable *AsSerializable() = 0;   // slot 23, folded 0x1c04e0: this ? this + 4 : 0
    virtual cIGZPersistResource *AsPersistResource() = 0; // slot 24, folded 0x3a4f40: this ? this + 8 : 0
    virtual void LoadTexts() = 0;                      // slot 25
    virtual void Unload() = 0;                         // slot 26
    virtual bool StoreTexts() = 0;                     // slot 27
    virtual bool GetRecord(void **out) = 0;            // slot 28 (ret 4)
    virtual int32_t GetSlotValue(uint32_t index) = 0;  // slot 29 (ret 4)
    virtual void SetSlotValue(uint32_t value, uint32_t index) = 0; // slot 30 (ret 8)
    virtual bool IsKind567() = 0;                      // slot 31
    virtual bool IsCategory13WithFlag4() = 0;          // slot 32
    virtual bool IsCategory16WithFlag4() = 0;          // slot 33
};

class cTSNeighborhoodInfo : public cITSNeighborhoodInfo,
                            public cIGZSerializable,
                            public cIGZPersistResource,
                            public cIGZSelfDescribing,
                            public rz::cRZUnknown {
public:
    // The game's vtables (the tests relate ours to them; nothing in src/ installs them any more).
    static constexpr uint32_t kOriginalVTable = 0xdcbd50;
    static constexpr uint32_t kOriginalSerialVTable = 0xdcbddc;
    static constexpr uint32_t kOriginalPersistVTable = 0xdcbdf8;
    static constexpr uint32_t kOriginalDescribeVTable = 0xdcbe2c;
    static constexpr uint32_t kOriginalUnknownVTable = 0xdcbe44;
    static constexpr uint32_t kClassId = 0xac8a7a2e;
    static constexpr uint32_t kInterfaceId = 0xac8a7a29;
    static constexpr uint32_t kVersion = 10;
    static constexpr uint32_t kMaxStoredValue = 0x11; // 0xb39350 answers this constant; Read rejects a stored word above it
    static constexpr uint32_t kSize = 0xd4;

    cTSNeighborhoodInfo();           // 0x4a6350 (the base interfaces' vtables stored first, dead stores; cRZUnknown's 0x1b9052)
    ~cTSNeighborhoodInfo() override; // 0x4a6500; the scalar deleting destructor 0x4a66c0 (ret 4) is the compiler's (slot 3 of
                                     // the +0x10 face, through the adjustor thunk 0x4a66ac in the original)
    cTSNeighborhoodInfo(const cTSNeighborhoodInfo &) = delete;
    cTSNeighborhoodInfo &operator=(const cTSNeighborhoodInfo &) = delete;
    static cTSNeighborhoodInfo *Create(); // 0x4a6720 (cdecl): new, null when the allocation fails

    // cIGZUnknown (every face; the other faces' slots 0-2 are adjustor thunks, as in the original: QueryInterface 0x4a6d7c,
    // 0x4a6d84, 0x4a6d94, 0x4a6d8c; AddRef/Release the folded 0x233078/0x233708 and their kin)
    bool QueryInterface(uint32_t riid, void **out) override; // slot 0, 0x4a6d00 (ret 8)
    uint32_t AddRef() override;                              // slot 1, folded 0x233070: cRZUnknown's
    uint32_t Release() override;                             // slot 2, folded 0x233700: cRZUnknown's

    // cITSNeighborhoodInfo (+0)
    void SetName(const char *text) override;          // 0x4a71c0
    cRZString *GetName() override;
    void SetDescription(const char *text) override;
    cRZString *GetDescription() override;
    void SetFilePrefix(const char *text) override;
    cRZString *GetFilePrefix() override;
    void SetTextD(const char *text) override;          // 0x4a71e0
    cRZString *GetTextD() override;
    void SetNumber(int32_t number) override;           // 0x4a7180
    int32_t GetNumber() override;
    void SetFlags(uint32_t mask, bool set) override;   // 0x4a7140
    bool HasFlags(uint32_t mask) override;             // 0x4a6700
    bool IsNumber(int32_t number) override;            // 0x4a6ae0
    // 0x4a6af0: whether the text equals the name (+0x18) or the first saved text (+0x58), ignoring case (their slot 13 compares
    // with length -1). 0/1 in AL.
    bool MatchesName(const char *text) override;
    void SetKind(uint32_t kind) override;
    uint32_t GetKind() override;
    void SetFieldAC(uint32_t value) override;
    uint32_t GetFieldAC() override;
    void SetCategory(int32_t category) override;
    int32_t GetCategory() override;
    cIGZSerializable *AsSerializable() override;
    cIGZPersistResource *AsPersistResource() override;
    // 0x4a6b90: reads both texts from the string table the persist system finds for the key (see the body for the two table
    // types by version).
    void LoadTexts() override;
    // 0x4a7220: evicts the record's resources from the persist system and drops the record object.
    void Unload() override;
    // 0x4a6fb0: writes the texts into a resource found (or created through the framework) for the key; answers whether the
    // persist system accepted the record and the resource wrote cleanly.
    bool StoreTexts() override;
    // 0x4a6960: the record object (created on demand through the framework), counted for the caller. False when it can't be
    // created.
    bool GetRecord(void **out) override;
    // 0x4a6950 / 0x4a71f0: the four small values (+0xc4) by index (unchecked), and set one when both the value and the index
    // are at most 3.
    int32_t GetSlotValue(uint32_t index) override;
    void SetSlotValue(uint32_t value, uint32_t index) override;
    bool IsKind567() override;             // 0x4a6b70: the kind (+0xa8) is 5, 6 or 7
    bool IsCategory13WithFlag4() override; // 0x4a6b30: category 13 and HasFlags(4) (through the vtable)
    bool IsCategory16WithFlag4() override; // 0x4a6b50: category 16 and HasFlags(4)

    // cIGZSerializable (+4; these run with this = the face)
    // 0x4a7310: version word 10, then the words below in the order Read takes them. True when the stream reports no error.
    bool Write(ifaces::cIGZOStream *stream) override;
    // 0x4a6da0: the reverse, version dependent (see the body). False when the stream errors or a stored word exceeds the
    // supported maximum (0x11), true otherwise.
    bool Read(ifaces::cIGZIStream *stream) override;
    uint32_t GetGZCLSID() override; // 0x4a6900

    // cIGZPersistResource (+8)
    cGZPersistResourceKey GetKey() override;                // 0x4a6910
    bool SetKey(const cGZPersistResourceKey &key) override; // 0x4a71a0
    uint32_t GetType() override;                            // folded 0x254fd0
    uint32_t GetGroup() override;                           // folded 0x24ad60
    uint32_t GetInstance() override;                        // folded 0x24ad70
    bool SetType(uint32_t type) override;                   // folded 0x4a7210
    bool SetGroup(uint32_t group) override;                 // folded 0x4a7170
    bool SetInstance(uint32_t instance) override;           // 0x4a7190
    uint32_t GetRefCount() override;                        // folded 0x47c6e0 (add ecx, 8; jmp cRZUnknown's count getter)

    // cIGZSelfDescribing (+0xc)
    bool Describe(void *target) override;     // 0x4a6930: the name (slot 4, through the vtable) to the target's slot 16; true
    bool DescribeMore(void *target) override; // folded 0x18a7a0: false

    // +0x00..+0x10: the five vtable pointers, +0x14 the cRZUnknown count (mRefCount)
    // The texts are cRZString in its hook shape (non-polymorphic; built and destroyed by Construct / Destruct, which store the
    // game's string vtable 0xd9b470, as the original's inlined constructors do): unions keep the compiler from constructing or
    // destroying their std::string a second time.
    union { cRZString mName; };        // +0x18 from the string table (entry 1), not saved
    union { cRZString mDescription; }; // +0x38 from the string table (entry 2), not saved
    union { cRZString mFilePrefix; };  // +0x58 saved; FormatNumber and the lot file names start with it
    union { cRZString mTextD; };       // +0x78 saved from version 5
    int32_t mNumber;                   // +0x98 -1 by default; saved
    uint32_t mFlags;                   // +0x9c saved from version 9
    uint32_t mVersion;                 // +0xa0 the version word of the last Read (10 by default)
    int32_t mCategory;                 // +0xa4 saved from version 8, else derived from mKind
    uint32_t mKind;                    // +0xa8 saved from version 4 (1 by default)
    uint32_t mFieldAC;                 // +0xac saved from version 6
    cGZPersistResourceKey mKey;        // +0xb0 the persist face's resource key
    void *mRecord;                     // +0xc0 counted, created on demand
    int32_t mSlotValues[4];            // +0xc4 saved as bytes from version 10 (1, 2, 3, 0 by default)
};
static_assert(sizeof(cTSNeighborhoodInfo) == cTSNeighborhoodInfo::kSize, "cTSNeighborhoodInfo layout");
static_assert(offsetof(cTSNeighborhoodInfo, mRefCount) == 0x14, "the cRZUnknown count");
static_assert(offsetof(cTSNeighborhoodInfo, mName) == 0x18, "name");
static_assert(offsetof(cTSNeighborhoodInfo, mTextD) == 0x78, "text D");
static_assert(offsetof(cTSNeighborhoodInfo, mNumber) == 0x98, "number");
static_assert(offsetof(cTSNeighborhoodInfo, mKey) == 0xb0, "key");
static_assert(offsetof(cTSNeighborhoodInfo, mRecord) == 0xc0, "record");
static_assert(offsetof(cTSNeighborhoodInfo, mSlotValues) == 0xc4, "slot values");

// Where each interface sits in the object (RTTI; base offsets are not constant expressions: the tests check them).
namespace nhinfo {
constexpr uint32_t kSerialFace = 0x4;
constexpr uint32_t kPersistFace = 0x8;
constexpr uint32_t kDescribeFace = 0xc;
constexpr uint32_t kUnknownFace = 0x10;
} // namespace nhinfo

// The RVA hooks' entry points (src/hook/tables/cTSNeighborhoodInfo.cpp). Every record the game creates carries our vtables, so
// these run only where the game calls an old entry point directly (the constructor from 0x767bc7, GetGZCLSID from the COM
// director) or holds a record built with its own vtables (none while the constructor and Create hooks are on). Original calling
// convention (this in ecx = the face of the slot), our functions called without virtual dispatch, bodies inlined.
struct cTSNeighborhoodInfoAdapters {
    using Self = cTSNeighborhoodInfo;
    static Self *__fastcall Construct(Self *memory, void *);
    static void __fastcall Destruct(Self *self, void *);
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);
    static Self *__fastcall UnknownDeletingDestructor(rz::cRZUnknown *face, void *, uint32_t flags); // the thunk 0x4a66ac
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t riid, void **out);
    static bool __fastcall SerialQueryInterface(cIGZSerializable *face, void *, uint32_t riid, void **out);
    static bool __fastcall PersistQueryInterface(cIGZPersistResource *face, void *, uint32_t riid, void **out);
    static bool __fastcall DescribeQueryInterface(cIGZSelfDescribing *face, void *, uint32_t riid, void **out);
    static bool __fastcall UnknownQueryInterface(rz::cRZUnknown *face, void *, uint32_t riid, void **out);
    static void __fastcall SetName(Self *self, void *, const char *text);
    static void __fastcall SetTextD(Self *self, void *, const char *text);
    static void __fastcall SetNumber(Self *self, void *, int32_t number);
    static bool __fastcall IsNumber(Self *self, void *, int32_t number);
    static void __fastcall SetFlags(Self *self, void *, uint32_t mask, bool set);
    static bool __fastcall HasFlags(Self *self, void *, uint32_t mask);
    static bool __fastcall MatchesName(Self *self, void *, const char *text);
    static int32_t __fastcall GetSlotValue(Self *self, void *, uint32_t index);
    static void __fastcall SetSlotValue(Self *self, void *, uint32_t value, uint32_t index);
    static bool __fastcall IsKind567(Self *self, void *);
    static bool __fastcall IsCategory13WithFlag4(Self *self, void *);
    static bool __fastcall IsCategory16WithFlag4(Self *self, void *);
    static void __fastcall LoadTexts(Self *self, void *);
    static void __fastcall Unload(Self *self, void *);
    static bool __fastcall StoreTexts(Self *self, void *);
    static bool __fastcall GetRecord(Self *self, void *, void **out);
    static uint32_t __fastcall GetGZCLSID(cIGZSerializable *face, void *);
    static bool __fastcall Write(cIGZSerializable *face, void *, ifaces::cIGZOStream *stream);
    static bool __fastcall Read(cIGZSerializable *face, void *, ifaces::cIGZIStream *stream);
    static cGZPersistResourceKey *__fastcall GetKey(cIGZPersistResource *face, void *, cGZPersistResourceKey *out);
    static bool __fastcall SetKey(cIGZPersistResource *face, void *, const cGZPersistResourceKey *key);
    static bool __fastcall SetInstance(cIGZPersistResource *face, void *, uint32_t instance);
    static bool __fastcall Describe(cIGZSelfDescribing *face, void *, void *target);
};
