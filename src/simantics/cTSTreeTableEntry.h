#pragma once
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator
#include "gz/cIGZSerializable.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class cIGZString;

// cTSTreeTableEntry: one interaction of an interaction table (a TTAB row; the table is cTSTreeTable): its action and check
// tree ids, the availability flags, the motive advertisements per life stage, the trait advertisements per life stage,
// attenuation, autonomy threshold, its name and the recent-use records. RTTI: cITSTreeTableEntry : cIGZSerializable :
// cIGZUnknown, all at +0 (one vtable, 0xdc4440, 99 slots). 0x7c bytes. RVAs 1.18.0.187.
//
// Standalone shape (docs/standalone-shape.md, lane GN): a real C++ class; the compiler emits its vtable. Creation: the
// constructor 0x430f60 (called by the table's slot 15 0x431410 and Read 0x433b10, both game code: reached through the
// Construct hook, a placement new) and the copy constructor 0x430d20 (called only by Write, ours). The game's vtable is
// referenced only by those two and the destructor 0x4310e0. The RVA hooks on the old slot functions are adapters
// (cTSTreeTableEntryAdapters) for objects built while the Construct hook is off.
//
// Life stages index the advertisement vectors: 0..4 (adult, child, toddler, teen, elder: the motive ads at +0x10, 13 and 14
// (baby) folded onto 0 where a function says so) and 5..12 (the trait ads at +0x1c, index stage - 5). Motives are 0..15;
// 1, 2, 4, 10, 11 and 12 are skipped by some functions (sets below).

// cTSTriple<short> (the original's template name): a polymorphic triple of shorts, 12 bytes. Both advertisement kinds derive
// from it without adding anything; the game's deleting destructor 0x4311c0 (in both derived vtables) resets the vtable to the
// base's 0xdc4428 and frees 12 bytes.
template <class T> class cTSTriple {
public:
    cTSTriple() : mFirst(0), mSecond(0), mThird(0) {}
    cTSTriple(const cTSTriple &other) : mFirst(other.mFirst), mSecond(other.mSecond), mThird(other.mThird) {}
    cTSTriple &operator=(const cTSTriple &other)
    {
        mFirst = other.mFirst;
        mSecond = other.mSecond;
        mThird = other.mThird;
        return *this;
    }
    virtual ~cTSTriple() {}

    T mFirst;  // +4
    T mSecond; // +6
    T mThird;  // +8
};
static_assert(sizeof(cTSTriple<int16_t>) == 12, "cTSTriple<short> layout");

// A motive advertisement (game vtable 0xdc4430): mFirst = the personality type (index into the motive table 0xdc4314; 0 for
// none), mSecond = the minimum, mThird = the delta. Old files store them as min, delta, type.
class cTSTreeTableAd : public cTSTriple<int16_t> {};
// A trait advertisement value (game vtable 0xdc4438): three alternatives, picked per element by two bits of a person's mask.
class cTSTraitAdValue : public cTSTriple<int16_t> {};
static_assert(sizeof(cTSTreeTableAd) == 12 && sizeof(cTSTraitAdValue) == 12, "advertisement layout");

using cTSTreeTableAdVector = std::vector<cTSTreeTableAd>;               // one life stage's motive ads, by motive
using cTSTreeTableAdStages = std::vector<cTSTreeTableAdVector>;         // +0x10: by life stage 0..4
using cTSTraitAdVector = std::vector<cTSTraitAdValue>;                  // one trait's values
using cTSTraitAdList = std::vector<cTSTraitAdVector>;                   // one life stage's traits, by index
using cTSTraitAdStages = std::vector<cTSTraitAdList>;                   // +0x1c: by life stage - 5

// A recent use (8 bytes): the tick and the actor, kept at the object id's index. The second word is 0 in pushed records.
struct cTSTreeTableUse {
    int32_t mTick;
    int16_t mActor;
    int16_t mZero;
};
static_assert(sizeof(cTSTreeTableUse) == 8, "use record layout");
using cTSTreeTableUseVector = std::vector<cTSTreeTableUse, GZServiceAllocator<cTSTreeTableUse>>;

// The entry's name: a cRZString (hook-shaped, built and destroyed by hand) as a member whose destructor runs in the original's
// order (after the use records, before the advertisements).
struct cTSTreeTableName {
    union { cRZString mString; };
    cTSTreeTableName() { mString.Construct(); }
    cTSTreeTableName(const cTSTreeTableName &other) { mString.Construct(other.mString, 0, 0xffffffffu); }
    cTSTreeTableName &operator=(const cTSTreeTableName &) = delete;
    ~cTSTreeTableName() { mString.Destruct(); }
};
static_assert(sizeof(cTSTreeTableName) == 0x20, "name layout");

// The entry's interface (the original's name, global as its RTTI): slots 6..98 after cIGZSerializable's.
class cITSTreeTableEntry : public cIGZSerializable {
public:
    virtual bool ResetToDefaults(uint32_t unused) = 0;              // 6
    virtual void CopyFrom(cITSTreeTableEntry *other) = 0;           // 7
    virtual uint32_t GetIndex() = 0;                                // 8
    virtual void SetIndex(uint32_t index) = 0;                      // 9
    virtual uint32_t GetFlags() = 0;                                // 10
    virtual bool HasFlags(uint32_t mask) = 0;                       // 11: every bit of mask set (age bits: availability)
    virtual void SetMaskedFlags(uint32_t values, uint32_t mask) = 0; // 12: the bits of mask from values
    virtual void SetFlags(uint32_t bits, bool on) = 0;              // 13
    virtual bool GetCanJoin() = 0;                                  // 14: bit 1
    virtual void SetCanJoin(bool on) = 0;                           // 15
    virtual bool GetRunImmediately() = 0;                           // 16: bit 2
    virtual void SetRunImmediately(bool on) = 0;                    // 17
    virtual uint32_t GetAgeFlags() = 0;                             // 18: flags & 0x3f013e51
    virtual bool GetAllowSubQueue() = 0;                            // 19: bit 14
    virtual void SetAllowSubQueue(bool on) = 0;                     // 20
    virtual bool GetAllowOnSelf() = 0;                              // 21: bit 20
    virtual void SetAllowOnSelf(bool on) = 0;                       // 22
    virtual bool GetOverrideDisable() = 0;                          // 23: bit 21
    virtual void SetOverrideDisable(bool on) = 0;                   // 24
    virtual bool GetPreferredByFurious() = 0;                       // 25: bit 23
    virtual void SetPreferredByFurious(bool on) = 0;                // 26
    virtual bool GetTypeSubQueue() = 0;                             // 27: bit 15
    virtual void SetTypeSubQueue(bool on) = 0;                      // 28
    virtual bool GetResetMotiveDecay() = 0;                         // 29: bit 19
    virtual void SetResetMotiveDecay(bool on) = 0;                  // 30
    virtual int16_t GetUIDisplayType() = 0;                         // 31
    virtual void SetUIDisplayType(int16_t type) = 0;                // 32
    virtual bool GetParentalControl() = 0;                          // 33: bit 17
    virtual void SetParentalControl(bool on) = 0;                   // 34
    virtual bool GetFlag5() = 0;                                    // 35: bit 5
    virtual void SetFlag5(bool on) = 0;                             // 36
    virtual uint32_t GetJoinIndex() = 0;                            // 37
    virtual void SetJoinIndex(uint32_t index) = 0;                  // 38
    virtual bool GetDebugOnly() = 0;                                // 39: bit 7
    virtual void SetDebugOnly(bool on) = 0;                         // 40
    virtual bool GetAutoFirstSelect() = 0;                          // 41: bit 8
    virtual void SetAutoFirstSelect(bool on) = 0;                   // 42
    virtual bool IsAutonomyDisabled() = 0;                          // 43: threshold >= 100
    virtual uint32_t GetFacialAnimType() = 0;                       // 44
    virtual void SetFacialAnimType(uint32_t type) = 0;              // 45
    virtual uint32_t GetIconTableIndex() = 0;                       // 46
    virtual void SetIconTableIndex(uint32_t index) = 0;             // 47
    virtual uint32_t GetIconGuid() = 0;                             // 48
    virtual void SetIconGuid(uint32_t guid) = 0;                    // 49
    virtual bool GetFlag22() = 0;                                   // 50: bit 22
    virtual void SetFlag22(bool on) = 0;                            // 51
    virtual uint32_t GetMotiveAdCount(int32_t stage) = 0;           // 52
    virtual int16_t *GetMotiveAdDelta(uint32_t motive, uint32_t stage) = 0; // 53: &ad.mThird of slot 57 (mapping, creating)
    virtual int16_t *GetMotiveAdMin(uint32_t motive, uint32_t stage) = 0;   // 54: &ad.mSecond
    virtual int16_t *GetMotiveAdType(uint32_t motive, uint32_t stage) = 0;  // 55: &ad.mFirst
    virtual bool HasAdvertisement(uint32_t motive, uint32_t stage) = 0;     // 56
    virtual cTSTreeTableAd *GetMotiveAd(uint32_t motive, uint32_t stage, bool babyAsAdult, bool create) = 0; // 57
    virtual cTSTreeTableAd *FindMotiveAd(int32_t motive, int32_t stage, bool babyAsAdult) = 0;              // 58
    virtual void SetMotiveAd(int32_t motive, int32_t stage, const cTSTreeTableAd *ad, bool trim) = 0;     // 59
    virtual cTSTreeTableAdVector GetMotiveAds(int32_t stage) = 0;   // 60
    virtual cTSTraitAdList GetTraitAds(uint32_t stage) = 0;         // 61
    virtual int16_t GetTraitAdValue(int32_t trait, uint32_t stage, uint32_t element, int32_t which) = 0; // 62
    virtual void SetTraitAdValue(int32_t trait, uint32_t stage, uint32_t element, int32_t which, int16_t value,
                                 bool trim) = 0;                    // 63
    virtual void ScaleAds(void *person, float factor, uint32_t motive) = 0; // 64
    virtual float GetAdValue(void *person, uint32_t motive) = 0;    // 65: thousandths
    virtual float GetMotiveScore(uint32_t motive, uint32_t stage, void *personalityMap) = 0; // 66
    virtual float GetTraitAdFor(uint32_t motive, uint32_t stage, uint32_t mask) = 0;      // 67: thousandths
    virtual void CopyStageToAll(uint32_t stage, bool trim) = 0;     // 68
    virtual void ClearAds() = 0;                                    // 69
    virtual void ClearStage(uint32_t stage, bool trim) = 0;         // 70
    virtual void ClearMotiveAds() = 0;                              // 71
    virtual void ClearTraitAds() = 0;                               // 72
    virtual void RecordUse(int32_t tick, uint16_t actor, uint32_t objectId) = 0;     // 73
    virtual bool WasRecentlyUsed(int32_t tick, int32_t actor, uint32_t objectId) = 0; // 74
    virtual void ClearUses() = 0;                                   // 75
    virtual uint16_t GetActionTreeId() = 0;                         // 76
    virtual void SetActionTreeId(uint16_t id) = 0;                  // 77
    virtual uint16_t GetCheckTreeId() = 0;                          // 78
    virtual void SetCheckTreeId(uint16_t id) = 0;                   // 79
    virtual uint32_t GetAttenuation() = 0;                          // 80: 0 custom, 1 none, 2 low, 3 medium, 4 high
    virtual void SetAttenuation(uint32_t code) = 0;                 // 81
    virtual float GetMemoryIterativeMultiplier() = 0;               // 82
    virtual void SetMemoryIterativeMultiplier(float value) = 0;     // 83
    virtual float GetAttenuationValue(bool alternate) = 0;          // 84
    virtual void SetCustomAttenuation(float value) = 0;             // 85: code 0 and the value
    virtual float GetCustomAttenuation() = 0;                       // 86
    virtual uint32_t GetAutonomyThreshold() = 0;                    // 87
    virtual void SetAutonomyThreshold(uint32_t threshold) = 0;      // 88
    virtual bool GetName(cIGZString *out) = 0;                      // 89
    virtual cRZString *GetNameString() = 0;                         // 90
    virtual cIGZString *SetName(const cIGZString &name) = 0;        // 91: the name's slot 16 (operator=)
    virtual void DumpText(void *stream, uint32_t unused) = 0;       // 92: tab-separated debug text (ret 8: the second dword is never read)
    virtual void Trim() = 0;                                        // 93: drops trailing empty ads
    virtual bool GetAffectsOwnership() = 0;                         // 94: bit 18
    virtual void SetAffectsOwnership(bool on) = 0;                  // 95
    virtual cITSTreeTableEntry *AsTreeTableEntry() = 0;             // 96
    virtual cITSTreeTableEntry *AsTreeTableEntry2() = 0;            // 97
    virtual ~cITSTreeTableEntry() = default;                        // 98: the deleting destructor
};

class cTSTreeTableEntry : public cITSTreeTableEntry {
public:
    static constexpr uint32_t kOriginalVTable = 0xdc4440;
    static constexpr uint32_t kVTableSlots = 99;
    static constexpr uint32_t kInterfaceId = 0xfb5a203a;  // cITSTreeTableEntry
    static constexpr uint32_t kClassId = 0x1b5a2044;      // the implementation (CopyFrom's fast path asks for it)
    static constexpr uint32_t kSerializableId = 0xe4fda3d4;
    static constexpr uint32_t kMotiveTable = 0xdc4314;    // 12-byte rows by ad type: {person data index, inverted, ?}
    static constexpr size_t kSize = 0x7c;

    cTSTreeTableEntry();                                  // 0x430f60
    cTSTreeTableEntry(const cTSTreeTableEntry &other);    // 0x430d20: no uses, reference count 0
    cTSTreeTableEntry &operator=(const cTSTreeTableEntry &) = delete;
    ~cTSTreeTableEntry() override;                        // 0x4310e0 (slot 98: 0x4312e0)

    // cIGZUnknown
    bool QueryInterface(uint32_t riid, void **out) override; // 0x433a50
    uint32_t AddRef() override;                              // folded 0x148fb0
    uint32_t Release() override;                             // 0x434400
    // cIGZSerializable
    bool Write(ifaces::cIGZOStream *stream) override;        // 0x435a80
    bool Read(ifaces::cIGZIStream *stream) override;         // 0x433e60
    uint32_t GetGZCLSID() override;                          // folded 0x6c6b0: 0

    bool ResetToDefaults(uint32_t unused) override;
    void CopyFrom(cITSTreeTableEntry *other) override;
    uint32_t GetIndex() override;
    void SetIndex(uint32_t index) override;
    uint32_t GetFlags() override;
    bool HasFlags(uint32_t mask) override;
    void SetMaskedFlags(uint32_t values, uint32_t mask) override;
    void SetFlags(uint32_t bits, bool on) override;
    bool GetCanJoin() override;
    void SetCanJoin(bool on) override;
    bool GetRunImmediately() override;
    void SetRunImmediately(bool on) override;
    uint32_t GetAgeFlags() override;
    bool GetAllowSubQueue() override;
    void SetAllowSubQueue(bool on) override;
    bool GetAllowOnSelf() override;
    void SetAllowOnSelf(bool on) override;
    bool GetOverrideDisable() override;
    void SetOverrideDisable(bool on) override;
    bool GetPreferredByFurious() override;
    void SetPreferredByFurious(bool on) override;
    bool GetTypeSubQueue() override;
    void SetTypeSubQueue(bool on) override;
    bool GetResetMotiveDecay() override;
    void SetResetMotiveDecay(bool on) override;
    int16_t GetUIDisplayType() override;
    void SetUIDisplayType(int16_t type) override;
    bool GetParentalControl() override;
    void SetParentalControl(bool on) override;
    bool GetFlag5() override;
    void SetFlag5(bool on) override;
    uint32_t GetJoinIndex() override;
    void SetJoinIndex(uint32_t index) override;
    bool GetDebugOnly() override;
    void SetDebugOnly(bool on) override;
    bool GetAutoFirstSelect() override;
    void SetAutoFirstSelect(bool on) override;
    bool IsAutonomyDisabled() override;
    uint32_t GetFacialAnimType() override;
    void SetFacialAnimType(uint32_t type) override;
    uint32_t GetIconTableIndex() override;
    void SetIconTableIndex(uint32_t index) override;
    uint32_t GetIconGuid() override;
    void SetIconGuid(uint32_t guid) override;
    bool GetFlag22() override;
    void SetFlag22(bool on) override;
    uint32_t GetMotiveAdCount(int32_t stage) override;
    int16_t *GetMotiveAdDelta(uint32_t motive, uint32_t stage) override;
    int16_t *GetMotiveAdMin(uint32_t motive, uint32_t stage) override;
    int16_t *GetMotiveAdType(uint32_t motive, uint32_t stage) override;
    bool HasAdvertisement(uint32_t motive, uint32_t stage) override;
    cTSTreeTableAd *GetMotiveAd(uint32_t motive, uint32_t stage, bool babyAsAdult, bool create) override;
    cTSTreeTableAd *FindMotiveAd(int32_t motive, int32_t stage, bool babyAsAdult) override;
    void SetMotiveAd(int32_t motive, int32_t stage, const cTSTreeTableAd *ad, bool trim) override;
    cTSTreeTableAdVector GetMotiveAds(int32_t stage) override;
    cTSTraitAdList GetTraitAds(uint32_t stage) override;
    int16_t GetTraitAdValue(int32_t trait, uint32_t stage, uint32_t element, int32_t which) override;
    void SetTraitAdValue(int32_t trait, uint32_t stage, uint32_t element, int32_t which, int16_t value, bool trim) override;
    void ScaleAds(void *person, float factor, uint32_t motive) override;
    float GetAdValue(void *person, uint32_t motive) override;
    float GetMotiveScore(uint32_t motive, uint32_t stage, void *personalityMap) override;
    float GetTraitAdFor(uint32_t motive, uint32_t stage, uint32_t mask) override;
    void CopyStageToAll(uint32_t stage, bool trim) override;
    void ClearAds() override;
    void ClearStage(uint32_t stage, bool trim) override;
    void ClearMotiveAds() override;
    void ClearTraitAds() override;
    void RecordUse(int32_t tick, uint16_t actor, uint32_t objectId) override;
    bool WasRecentlyUsed(int32_t tick, int32_t actor, uint32_t objectId) override;
    void ClearUses() override;
    uint16_t GetActionTreeId() override;
    void SetActionTreeId(uint16_t id) override;
    uint16_t GetCheckTreeId() override;
    void SetCheckTreeId(uint16_t id) override;
    uint32_t GetAttenuation() override;
    void SetAttenuation(uint32_t code) override;
    float GetMemoryIterativeMultiplier() override;
    void SetMemoryIterativeMultiplier(float value) override;
    float GetAttenuationValue(bool alternate) override;
    void SetCustomAttenuation(float value) override;
    float GetCustomAttenuation() override;
    uint32_t GetAutonomyThreshold() override;
    void SetAutonomyThreshold(uint32_t threshold) override;
    bool GetName(cIGZString *out) override;
    cRZString *GetNameString() override;
    cIGZString *SetName(const cIGZString &name) override;
    void DumpText(void *stream, uint32_t unused) override;
    void Trim() override;
    bool GetAffectsOwnership() override;
    void SetAffectsOwnership(bool on) override;
    cITSTreeTableEntry *AsTreeTableEntry() override;
    cITSTreeTableEntry *AsTreeTableEntry2() override;

    // Non-virtual members (hooked directly: one base chain, so member pointers are one word).
    bool WriteBody(ifaces::cIGZOStream *stream, bool trim);                      // 0x4357c0
    void ScaleMotiveMin(void *person, float factor, uint32_t motive);            // 0x433850
    void ScaleTraitAds(void *person, float factor, int32_t trait);               // 0x4338a0
    static bool IsAllZero(const cTSTraitAdVector *values);                       // 0x42ef30 (cdecl)
    static void TrimMotiveAds(cTSTreeTableAdStages *stages);                     // 0x42f210 (cdecl)
    static void TrimTraitAds(cTSTraitAdStages *stages);                          // 0x42ef80 (cdecl)

    // +0x00 vtable
    uint32_t mRefCount;                   // +0x04
    uint16_t mCheckTreeId;                // +0x08
    uint16_t mActionTreeId;               // +0x0a
    int16_t mUIDisplayType;               // +0x0c
    cTSTreeTableAdStages mMotiveAds;      // +0x10
    cTSTraitAdStages mTraitAds;           // +0x1c
    uint32_t mAttenuation;                // +0x28
    float mCustomAttenuation;             // +0x2c
    uint32_t mFlags;                      // +0x30
    uint32_t mIndex;                      // +0x34
    uint32_t mAutonomyThreshold;          // +0x38
    uint32_t mJoinIndex;                  // +0x3c
    uint32_t mFacialAnimType;             // +0x40
    float mMemoryIterativeMultiplier;     // +0x44
    cTSTreeTableName mName;               // +0x48
    uint32_t mIconTableIndex;             // +0x68
    uint32_t mIconGuid;                   // +0x6c
    cTSTreeTableUseVector mUses;          // +0x70
};
static_assert(sizeof(cTSTreeTableEntry) == cTSTreeTableEntry::kSize, "cTSTreeTableEntry layout");
static_assert(offsetof(cTSTreeTableEntry, mRefCount) == 0x04);
static_assert(offsetof(cTSTreeTableEntry, mCheckTreeId) == 0x08);
static_assert(offsetof(cTSTreeTableEntry, mUIDisplayType) == 0x0c);
static_assert(offsetof(cTSTreeTableEntry, mMotiveAds) == 0x10);
static_assert(offsetof(cTSTreeTableEntry, mTraitAds) == 0x1c);
static_assert(offsetof(cTSTreeTableEntry, mAttenuation) == 0x28);
static_assert(offsetof(cTSTreeTableEntry, mFlags) == 0x30);
static_assert(offsetof(cTSTreeTableEntry, mMemoryIterativeMultiplier) == 0x44);
static_assert(offsetof(cTSTreeTableEntry, mName) == 0x48);
static_assert(offsetof(cTSTreeTableEntry, mIconTableIndex) == 0x68);
static_assert(offsetof(cTSTreeTableEntry, mUses) == 0x70);

// The RVA hooks' entry points (src/hook/tables/cTSTreeTableEntry.cpp): the old entry points, for entries the game built with
// its own vtable (none while the Construct hook is on). Original calling convention (this in ecx), our functions called
// without virtual dispatch, bodies inlined.
struct cTSTreeTableEntryAdapters {
    using Self = cTSTreeTableEntry;
    static Self *__fastcall Construct(Self *memory, void *, uint32_t unused); // the original takes a word it never reads (ret 4; cTSTreeTable passes its slot 8)
    static Self *__fastcall CopyConstruct(Self *memory, void *, const Self *other);
    static void __fastcall Destruct(Self *self, void *);
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t riid, void **out);
    static uint32_t __fastcall Release(Self *self, void *);
    static bool __fastcall Write(Self *self, void *, ifaces::cIGZOStream *stream);
    static bool __fastcall Read(Self *self, void *, ifaces::cIGZIStream *stream);
    static bool __fastcall ResetToDefaults(Self *self, void *, uint32_t unused);
    static void __fastcall CopyFrom(Self *self, void *, cITSTreeTableEntry *other);
    static bool __fastcall HasFlags(Self *self, void *, uint32_t mask);
    static void __fastcall SetMaskedFlags(Self *self, void *, uint32_t values, uint32_t mask);
    static void __fastcall SetFlags(Self *self, void *, uint32_t bits, bool on);
    static bool __fastcall GetCanJoin(Self *self, void *);
    static void __fastcall SetCanJoin(Self *self, void *, bool on);
    static bool __fastcall GetRunImmediately(Self *self, void *);
    static void __fastcall SetRunImmediately(Self *self, void *, bool on);
    static uint32_t __fastcall GetAgeFlags(Self *self, void *);
    static bool __fastcall GetAllowSubQueue(Self *self, void *);
    static void __fastcall SetAllowSubQueue(Self *self, void *, bool on);
    static bool __fastcall GetAllowOnSelf(Self *self, void *);
    static void __fastcall SetAllowOnSelf(Self *self, void *, bool on);
    static bool __fastcall GetOverrideDisable(Self *self, void *);
    static void __fastcall SetOverrideDisable(Self *self, void *, bool on);
    static bool __fastcall GetPreferredByFurious(Self *self, void *);
    static void __fastcall SetPreferredByFurious(Self *self, void *, bool on);
    static bool __fastcall GetTypeSubQueue(Self *self, void *);
    static void __fastcall SetTypeSubQueue(Self *self, void *, bool on);
    static bool __fastcall GetResetMotiveDecay(Self *self, void *);
    static void __fastcall SetResetMotiveDecay(Self *self, void *, bool on);
    static bool __fastcall GetParentalControl(Self *self, void *);
    static void __fastcall SetParentalControl(Self *self, void *, bool on);
    static bool __fastcall GetFlag5(Self *self, void *);
    static void __fastcall SetFlag5(Self *self, void *, bool on);
    static bool __fastcall GetDebugOnly(Self *self, void *);
    static void __fastcall SetDebugOnly(Self *self, void *, bool on);
    static bool __fastcall GetAutoFirstSelect(Self *self, void *);
    static void __fastcall SetAutoFirstSelect(Self *self, void *, bool on);
    static bool __fastcall IsAutonomyDisabled(Self *self, void *);
    static bool __fastcall GetFlag22(Self *self, void *);
    static void __fastcall SetFlag22(Self *self, void *, bool on);
    static uint32_t __fastcall GetMotiveAdCount(Self *self, void *, int32_t stage);
    static int16_t *__fastcall GetMotiveAdDelta(Self *self, void *, uint32_t motive, uint32_t stage);
    static int16_t *__fastcall GetMotiveAdMin(Self *self, void *, uint32_t motive, uint32_t stage);
    static int16_t *__fastcall GetMotiveAdType(Self *self, void *, uint32_t motive, uint32_t stage);
    static bool __fastcall HasAdvertisement(Self *self, void *, uint32_t motive, uint32_t stage);
    static cTSTreeTableAd *__fastcall GetMotiveAd(Self *self, void *, uint32_t motive, uint32_t stage, bool babyAsAdult,
                                                  bool create);
    static cTSTreeTableAd *__fastcall FindMotiveAd(Self *self, void *, int32_t motive, int32_t stage, bool babyAsAdult);
    static void __fastcall SetMotiveAd(Self *self, void *, int32_t motive, int32_t stage, const cTSTreeTableAd *ad, bool trim);
    static cTSTreeTableAdVector *__fastcall GetMotiveAds(Self *self, void *, cTSTreeTableAdVector *out, int32_t stage);
    static cTSTraitAdList *__fastcall GetTraitAds(Self *self, void *, cTSTraitAdList *out, uint32_t stage);
    static int16_t __fastcall GetTraitAdValue(Self *self, void *, int32_t trait, uint32_t stage, uint32_t element,
                                              int32_t which);
    static void __fastcall SetTraitAdValue(Self *self, void *, int32_t trait, uint32_t stage, uint32_t element, int32_t which,
                                           int16_t value, bool trim);
    static void __fastcall ScaleAds(Self *self, void *, void *person, float factor, uint32_t motive);
    static float __fastcall GetAdValue(Self *self, void *, void *person, uint32_t motive);
    static float __fastcall GetMotiveScore(Self *self, void *, uint32_t motive, uint32_t stage, void *personalityMap);
    static float __fastcall GetTraitAdFor(Self *self, void *, uint32_t motive, uint32_t stage, uint32_t mask);
    static void __fastcall CopyStageToAll(Self *self, void *, uint32_t stage, bool trim);
    static void __fastcall ClearAds(Self *self, void *);
    static void __fastcall ClearStage(Self *self, void *, uint32_t stage, bool trim);
    static void __fastcall ClearMotiveAds(Self *self, void *);
    static void __fastcall ClearTraitAds(Self *self, void *);
    static void __fastcall RecordUse(Self *self, void *, int32_t tick, uint16_t actor, uint32_t objectId);
    static bool __fastcall WasRecentlyUsed(Self *self, void *, int32_t tick, int32_t actor, uint32_t objectId);
    static void __fastcall ClearUses(Self *self, void *);
    static float __fastcall GetAttenuationValue(Self *self, void *, bool alternate);
    static void __fastcall SetCustomAttenuation(Self *self, void *, float value);
    static bool __fastcall GetName(Self *self, void *, cIGZString *out);
    static cIGZString *__fastcall SetName(Self *self, void *, const cIGZString &name);
    static void __fastcall Trim(Self *self, void *);
    static bool __fastcall GetAffectsOwnership(Self *self, void *);
    static void __fastcall SetAffectsOwnership(Self *self, void *, bool on);
};
