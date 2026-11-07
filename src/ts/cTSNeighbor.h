#pragma once
#include "gz/cIGZPersistResource.h"
#include "gz/cIGZSerializable.h"

#include <cstddef>
#include <cstdint>

// cTSNeighbor: one neighbour record of the neighbourhood, a Sim outside its lot (RTTI cTSNeighbor; bases cITSNeighbor, itself a
// cIGZSerializable, at +0 and cIGZPersistResource at +4). 0x214 bytes; the game's vtables 0xdcc494 (54 slots) and 0xdcc570 (12
// slots, the persist face at +4). RVAs are 1.18.0.187.
//
// The record keeps the Sim's id (+0xc), the person data object that id names (+0x14, counted; its data id at +0x10), the
// relationship table (+0x18, counted), the record object of the neighbour's second resource (+0x1c, counted) and 0xe7
// 16-bit attributes (+0x2c) of which a few are known: the age (index 0x3a), the gender (0x41), the species (0xba).
// Its persisted form has the class id 0xaace2efb (also the type of its resource key) and version 0x71, with the attribute
// count chosen by a layout number (0x36 now).
//
// Ages: 1 and below baby, 2 toddler, 3..15 child, 16..18 teen, 19..50 adult, above elder. A species other than 0 is a pet.
//
// Standalone shape (docs/standalone-shape.md, lane EJ): a real C++ class; the compiler emits both vtables (slot 53 of the first is
// the scalar deleting destructor of the interface's virtual destructor), the persist face's adjustor thunks and the RTTI. The
// game's 0xda66e4, which the original Create stores at +4 before the class vtable, is cIGZPersistResource's own vtable (stored
// first by every implementer's constructor), not another base. Creation: the creator 0x4b73c0 only (constructor inline; its
// address stored by the cTSSimCOMDirector constructor 0x415ae0): hooked to Create. The game's vtables are referenced only by
// that creator and the deleting destructor 0x4b72e0. The RVA hooks on the old functions are adapters (cTSNeighborAdapters).

// cITSNeighbor: the neighbour's own face (+0), slots 6..53 after cIGZSerializable's. Callers of other objects keep the generated
// ifaces::cITSNeighbor view.
class cITSNeighbor : public cIGZSerializable {
public:
    // 0x4b8400 (slot 6, ret 8): takes the id and the person data object (counted), reads the data id from it (its slot 28)
    // and clears the attributes. AL = 1.
    virtual uint8_t Attach(int16_t id, void *personData) = 0;
    // 0x4b8e80 (slot 7): lets go of the person data, resets (slot 8 with 0) and lets go of the relationship table, lets go
    // of the record object.
    virtual void Detach() = 0;
    // folded 0x433660 (slots 8 and 9): the id (+0xc). AX only.
    virtual uint16_t GetId() = 0;
    virtual uint16_t GetId2() = 0;
    // 0x4b8170 (slot 10): the person data object; when there is none yet and the data id is set, it is looked up through
    // the app singleton (slot 24, then 14) and held.
    virtual void *GetPersonData() = 0;
    // folded 0x4b8e40 (slot 11, ret 4): a counted store of the person data (the new one counted first, the old one released).
    virtual void SetPersonData(void *personData) = 0;
    // folded 0x148ba0 (slot 12): the relationship table (+0x18).
    virtual void *GetRelationshipTable() = 0;
    // folded 0x4b8d20 (slot 13, ret 4): a counted store of the record object (+0x1c).
    virtual void SetRecord(void *record) = 0;
    // 0x4b7950 (slot 14, ret 4): the record object (+0x1c). With none yet, `create` set, a non-zero id and group (+0x200):
    // asks the persist service for key {0x3053cf74, group, id, 0xaace2efb} (interface 0x3053cf67) and, when it has none,
    // makes one through the director framework (class 0x3053cf74) and initialises it with 0.
    virtual void *GetRecord(uint8_t create) = 0;
    // folded 0x149cf0 (slot 15): the person data id (+0x10).
    virtual uint32_t GetPersonDataId() = 0;
    // 0x4b80a0 (slot 16, ret 4): the attribute at the index (0 for 0xe7 and up; no check below zero). AX only.
    virtual uint16_t GetAttribute(int32_t index) = 0;
    // 0x4b8e10 (slot 17, ret 8): stores the attribute and sets the modified byte when it differs (indexes from 0xe7 up are
    // ignored).
    virtual void SetAttribute(int32_t index, uint16_t value) = 0;
    // 0x4b80c0 (slot 18, ret 4): sets the modified byte unless `keepClean` has bit 0 set; the attribute array.
    virtual uint16_t *GetAttributes(uint8_t keepClean) = 0;
    // 0x4b8140 (slot 19): a mask for the species: 1 -> 2, 2 -> 4, 3 -> 8, else 1.
    virtual uint32_t GetSpeciesMask() = 0;
    // 0x4b8130 (slot 20): 2 when the gender attribute is 0, else 1.
    virtual uint32_t GetGenderMask() = 0;
    // 0x4b80d0 (slot 21): a mask for the age: baby 0x20, toddler 1, child 2, teen 4, adult 8 or 0x40 (by the flag attribute),
    // elder 0x10.
    virtual uint32_t GetAgeMask() = 0;
    // 0x4b8690 / 0x4b8490 (slots 22 / 23): the species attribute is 0 / not 0.
    virtual uint8_t IsHuman() = 0;
    virtual uint8_t IsPet() = 0;
    // 0x4b86a0 / 0x4b8550 (slots 24 / 25): the gender attribute is 0 / 1.
    virtual uint8_t IsGender0() = 0;
    virtual uint8_t IsGender1() = 0;
    // 0x4b84a0 / 0x4b86e0 / 0x4b84e0 (slots 26..28): human and baby / toddler / child.
    virtual uint8_t IsBaby() = 0;
    virtual uint8_t IsToddler() = 0;
    virtual uint8_t IsChild() = 0;
    // 0x4b86b0 (slot 29): human and teen.
    virtual uint8_t IsTeen() = 0;
    // 0x4b8740 / 0x4b8460 / 0x4b8530 (slots 30..32): human adult with the flag attribute set / human adult / human elder.
    virtual uint8_t IsAdultWithFlag() = 0;
    virtual uint8_t IsAdult() = 0;
    virtual uint8_t IsElder() = 0;
    // 0x4b81d0 (slot 33): a life stage number from species (0..3) and age; 0 for other species values.
    virtual uint32_t GetLifeStage() = 0;
    // 0x4b8700 (slot 34): the person data's slot 4 object answers 0x11 to its slot 3 with 9.
    virtual uint8_t HasKindInfo11() = 0;
    // 0x4b84c0 (slot 35): the person data reports its slot 65.
    virtual uint8_t IsPersonDataFlagged() = 0;
    // folded 0x46a260 / 0x46de50 (slots 36 / 37, the second ret 4): the byte at +0x20.
    virtual uint8_t GetFlag20() = 0;
    virtual void SetFlag20(uint8_t value) = 0;
    // 0x4b8510 (slot 38): the modified byte, or else the relationship table's slot 16.
    virtual uint8_t IsModified() = 0;
    // 0x4b8d50 (slot 39, ret 4): stores the low byte as the modified byte, then tail-calls the relationship table's slot 17.
    virtual uint32_t SetModified(uint32_t flag) = 0;
    // 0x4b7a20 (slot 40, ret 4): builds the neighbour's display name into `sink` (its slot 16, a text) and answers whether it is
    // not empty; AL only. Without person data (slot 10) the sink's slot 26 gets (0, -1) and the answer is 0. The person data
    // gives the first name (its slot 49) and, through its slot 54 object's slot 7, the last name; the second-language service's
    // slot 38 object formats them (its slot 28) when there is one, otherwise the name is the non-empty one of the two or
    // "first last". A non-empty name is then put through a string of table 0x336 (entry: the attribute 0xe6 through the person-data
    // cache object 0x54ac40 gives, or this slot 16) as its format, when that string exists.
    virtual uint8_t BuildDisplayName(void *sink) = 0;
    // folded 0x15e970 / 0x2109b0 (slots 41 / 42, the second ret 4): the word at +0x24 (the related count the manager fills).
    virtual uint32_t GetRelatedCount() = 0;
    virtual void SetRelatedCount(uint32_t count) = 0;
    // folded 0x15f390 (slot 43): the layout number (+0x28).
    virtual uint32_t GetLayoutVersion() = 0;
    // 0x4b7fc0 (slot 44, ret 8): the person data's slot 67 object's slot 11 builds the key {0xac598eac, it, 1, 0}; asks
    // the persist service for it (interface 0xac598ea8) and answers that object's slot 113 with the two arguments (0 when
    // there is none). AX only.
    virtual uint16_t GetScore(uint16_t a, uint16_t b) = 0;
    // 0x4b74e0 (slot 45, ret 8): a graded compatibility of this neighbour with another from their relationship tables and
    // attributes. 0 for a null `other`, when the relationship table gives no 0x8000 flag for it, and in other failing cases.
    virtual int32_t GetRelationshipLevel(void *other, int32_t kind) = 0;
    // 0x4b8560 / 0x4b82d0 (slots 46 / 47, ret 8): in the list the app singleton's slot 6 gives for this id, the first item of
    // type 0xcf505ab0 / 0x8fcd5705 with at least two values whose first value equals `key`: with the second value non-zero,
    // stores third / second as a float and answers 1. AL only.
    virtual uint8_t GetValueRatioA(uint16_t key, float *out) = 0;
    virtual uint8_t GetValueRatioB(uint16_t key, float *out) = 0;
    // folded 0x1c04e0 (slot 48): the persist face (null for a null this).
    virtual cIGZPersistResource *AsPersistResource() = 0;
    // folded 0x18a7a0 (slot 49, ret 4): false.
    virtual uint8_t AnswersNo(void *argument) = 0;
    // 0x4b7f30 (slot 50, ret 0xc): runs 0x55cce0 on a temporary holder of this neighbour with the three arguments.
    virtual void RunRelationshipQuery(uint32_t a, uint32_t b, uint32_t c) = 0;
    // 0x4b7f90 / 0x4b8de0 (slots 51 / 52, ret 4 / 8): the words at +0x20c (which 1) / +0x210 (which 2); 0 for others / store.
    virtual uint32_t GetExtraWord(int32_t which) = 0;
    virtual void SetExtraWord(int32_t which, uint32_t value) = 0;
    // slot 53, 0x4b72e0 (ret 4): the scalar deleting destructor.
    virtual ~cITSNeighbor() = default;
};

class cTSNeighbor : public cITSNeighbor, public cIGZPersistResource {
public:
    // The game's vtables (the tests relate ours to them; nothing in src/ installs them any more).
    static constexpr uint32_t kOriginalVTable = 0xdcc494;
    static constexpr uint32_t kOriginalPersistVTable = 0xdcc570;
    static constexpr uint32_t kFormatVersion = 0x71; // the version Write stores
    static constexpr uint32_t kLayoutVersion = 0x36; // the attribute layout Write stores
    static constexpr unsigned kAttributeCount = 0xe7;
    static constexpr unsigned kAgeAttribute = 0x3a;     // +0xa0
    static constexpr unsigned kGenderAttribute = 0x41;  // +0xae
    static constexpr unsigned kFlagAttribute = 0xaf;    // +0x18a
    static constexpr unsigned kSpeciesAttribute = 0xba; // +0x1a0
    static constexpr uint32_t kObjectSize = 0x214;

    // The constructor inlined in 0x4b73c0: fields, a new relationship table held once, the attributes cleared. The bytes at +0xe,
    // +0x22 and +0x1fa are left alone.
    cTSNeighbor();
    // 0x4b72e0's destructor part: lets go of the person data, resets and releases the relationship table, releases the record
    // object (Detach), then the members' releases of whatever a release above put back.
    ~cTSNeighbor() override;
    cTSNeighbor(const cTSNeighbor &) = delete;
    cTSNeighbor &operator=(const cTSNeighbor &) = delete;
    // 0x4b73c0 (cdecl): new; null when the allocation fails.
    static cTSNeighbor *__cdecl Create();

    // cIGZUnknown (both faces; the persist face's slots 0-2 are adjustor thunks, as in the original: 0x4b87c2, the folded
    // 0x1448e9, 0x4b8d0a)
    // 0x4b8770 (slot 0, ret 8): ids 1, 0xe4fda3d4 and 0x3b43bf8a answer this, 0x456b8f1d the persist face; counted through slot 1.
    bool QueryInterface(uint32_t id, void **out) override;
    uint32_t AddRef() override; // folded 0x145260
    // 0x4b8cf0 (slot 2): counts above one go down by one; otherwise slot 53 (flags 1) and 0.
    uint32_t Release() override;

    // cIGZSerializable
    // 0x4b8ed0 (slot 3, ret 4): version 0x71, layout 0x36 (twice), the 0xe7 attributes, the id, the data id, the relationship
    // table (slot 3 with the stream and the id), a zero byte and the two words at +0x20c / +0x210. Whether the stream has no
    // error.
    bool Write(ifaces::cIGZOStream *stream) override;
    // 0x4b87d0 (slot 4, ret 4): reads what Write writes, as older formats had it (see the .cpp). Whether the stream has no
    // error.
    bool Read(ifaces::cIGZIStream *stream) override;
    uint32_t GetGZCLSID() override; // 0x4b74d0 (slot 5): the class id

    // cITSNeighbor
    uint8_t Attach(int16_t id, void *personData) override;
    void Detach() override;
    uint16_t GetId() override;
    uint16_t GetId2() override;
    void *GetPersonData() override;
    void SetPersonData(void *personData) override;
    void *GetRelationshipTable() override;
    void SetRecord(void *record) override;
    void *GetRecord(uint8_t create) override;
    uint32_t GetPersonDataId() override;
    uint16_t GetAttribute(int32_t index) override;
    void SetAttribute(int32_t index, uint16_t value) override;
    uint16_t *GetAttributes(uint8_t keepClean) override;
    uint32_t GetSpeciesMask() override;
    uint32_t GetGenderMask() override;
    uint32_t GetAgeMask() override;
    uint8_t IsHuman() override;
    uint8_t IsPet() override;
    uint8_t IsGender0() override;
    uint8_t IsGender1() override;
    uint8_t IsBaby() override;
    uint8_t IsToddler() override;
    uint8_t IsChild() override;
    uint8_t IsTeen() override;
    uint8_t IsAdultWithFlag() override;
    uint8_t IsAdult() override;
    uint8_t IsElder() override;
    uint32_t GetLifeStage() override;
    uint8_t HasKindInfo11() override;
    uint8_t IsPersonDataFlagged() override;
    uint8_t GetFlag20() override;
    void SetFlag20(uint8_t value) override;
    uint8_t IsModified() override;
    uint32_t SetModified(uint32_t flag) override;
    uint8_t BuildDisplayName(void *sink) override;
    uint32_t GetRelatedCount() override;
    void SetRelatedCount(uint32_t count) override;
    uint32_t GetLayoutVersion() override;
    uint16_t GetScore(uint16_t a, uint16_t b) override;
    int32_t GetRelationshipLevel(void *other, int32_t kind) override;
    uint8_t GetValueRatioA(uint16_t key, float *out) override;
    uint8_t GetValueRatioB(uint16_t key, float *out) override;
    cIGZPersistResource *AsPersistResource() override;
    uint8_t AnswersNo(void *argument) override;
    void RunRelationshipQuery(uint32_t a, uint32_t b, uint32_t c) override;
    uint32_t GetExtraWord(int32_t which) override;
    void SetExtraWord(int32_t which, uint32_t value) override;

    // cIGZPersistResource (+4; these run with this = the face, so the key at +0x1fc of the neighbour is at +0x1f8 there)
    // 0x4b7f10 (slot 3): the 16-byte key {type, group, instance, instance high}.
    cGZPersistResourceKey GetKey() override;
    // 0x4b8d90 (slot 4): copies the key in and then compares the stored words with the source: the modified byte is set only
    // when they differ, which they cannot (unless `key` points into the record). True.
    bool SetKey(const cGZPersistResourceKey &key) override;
    // 0x4b82c0 / 0x4b7ef0 / 0x4b7f00 (slots 5..7): the key's type, group and instance words.
    uint32_t GetType() override;
    uint32_t GetGroup() override;
    uint32_t GetInstance() override;
    // 0x4b8e70 / 0x4b8d70 / 0x4b8d80 (slots 8..10): store one word of the key. True.
    bool SetType(uint32_t type) override;
    bool SetGroup(uint32_t group) override;
    bool SetInstance(uint32_t instance) override;
    // folded 0x147a30 (slot 11): the count (the face's +4).
    uint32_t GetRefCount() override;

    // +0x00 / +0x04: the two vtable pointers.
    uint32_t mRefCount;         // +0x08
    int16_t mId;                // +0x0c
    uint16_t mPad0E;            // +0x0e
    uint32_t mPersonDataId;     // +0x10 the data id of the person data object
    void *mPersonData;          // +0x14 counted
    void *mRelationships;       // +0x18 counted relationship table
    void *mRecord;              // +0x1c counted record object (second resource)
    uint8_t mFlag20;            // +0x20
    uint8_t mModified;          // +0x21
    uint8_t mPad22[2];          // +0x22
    uint32_t mField24;          // +0x24 the related count
    uint32_t mLayoutVersion;    // +0x28 the layout number Read/Write carry
    uint16_t mAttributes[kAttributeCount]; // +0x2c
    uint16_t mPad1FA;           // +0x1fa
    uint32_t mKey[4];           // +0x1fc type, group, instance, instance high
    uint32_t mExtra20C;         // +0x20c
    uint32_t mExtra210;         // +0x210
};
static_assert(offsetof(cTSNeighbor, mRefCount) == 0x8);
static_assert(offsetof(cTSNeighbor, mId) == 0xc);
static_assert(offsetof(cTSNeighbor, mPersonDataId) == 0x10);
static_assert(offsetof(cTSNeighbor, mPersonData) == 0x14);
static_assert(offsetof(cTSNeighbor, mRelationships) == 0x18);
static_assert(offsetof(cTSNeighbor, mRecord) == 0x1c);
static_assert(offsetof(cTSNeighbor, mFlag20) == 0x20);
static_assert(offsetof(cTSNeighbor, mModified) == 0x21);
static_assert(offsetof(cTSNeighbor, mField24) == 0x24);
static_assert(offsetof(cTSNeighbor, mLayoutVersion) == 0x28);
static_assert(offsetof(cTSNeighbor, mAttributes) == 0x2c);
static_assert(offsetof(cTSNeighbor, mKey) == 0x1fc);
static_assert(offsetof(cTSNeighbor, mExtra210) == 0x210);
static_assert(sizeof(cTSNeighbor) == 0x214);

// Where the persist face sits (RTTI; base offsets are not constant expressions: the tests check it).
namespace neighbor {
constexpr uint32_t kPersistFace = 0x4;
} // namespace neighbor

// The RVA hooks' entry points (src/hook/tables/cTSNeighbor.cpp). Every neighbour the game creates carries our vtables, so these
// run only where the game calls an old entry point directly or holds a neighbour built with its own vtables (none while the
// Create hook is on). Original calling convention (this in ecx = the face of the slot), our functions called without virtual
// dispatch, bodies inlined.
struct cTSNeighborAdapters {
    using Self = cTSNeighbor;
    static uint32_t __fastcall Release(Self *self, void *);
    static uint32_t __fastcall GetGZCLSID(Self *self, void *);
    static bool __fastcall Write(Self *self, void *, ifaces::cIGZOStream *stream);
    static void __fastcall Detach(Self *self, void *);
    static void __fastcall SetAttribute(Self *self, void *, int32_t index, uint16_t value);
    static uint16_t *__fastcall GetAttributes(Self *self, void *, uint8_t keepClean);
    static uint32_t __fastcall GetGenderMask(Self *self, void *);
    static uint32_t __fastcall GetAgeMask(Self *self, void *);
    static uint8_t __fastcall IsHuman(Self *self, void *);
    static uint8_t __fastcall IsPet(Self *self, void *);
    static uint8_t __fastcall IsGender0(Self *self, void *);
    static uint8_t __fastcall IsGender1(Self *self, void *);
    static uint8_t __fastcall IsBaby(Self *self, void *);
    static uint8_t __fastcall IsChild(Self *self, void *);
    static uint8_t __fastcall IsTeen(Self *self, void *);
    static uint8_t __fastcall IsAdultWithFlag(Self *self, void *);
    static uint8_t __fastcall IsAdult(Self *self, void *);
    static uint8_t __fastcall IsElder(Self *self, void *);
    static uint8_t __fastcall IsPersonDataFlagged(Self *self, void *);
    static uint32_t __fastcall GetExtraWord(Self *self, void *, int32_t which);
    static void __fastcall SetExtraWord(Self *self, void *, int32_t which, uint32_t value);
    // The persist face's slots (this = the +4 face).
    static cGZPersistResourceKey *__fastcall GetKey(cIGZPersistResource *face, void *, cGZPersistResourceKey *out);
    static bool __fastcall SetKey(cIGZPersistResource *face, void *, const cGZPersistResourceKey *key);
    static uint32_t __fastcall GetType(cIGZPersistResource *face, void *);
    static uint32_t __fastcall GetGroup(cIGZPersistResource *face, void *);
    static uint32_t __fastcall GetInstance(cIGZPersistResource *face, void *);
    static bool __fastcall SetType(cIGZPersistResource *face, void *, uint32_t type);
    static bool __fastcall SetGroup(cIGZPersistResource *face, void *, uint32_t group);
    static bool __fastcall SetInstance(cIGZPersistResource *face, void *, uint32_t instance);
};
