#pragma once
#include "game/game.h"
#include "game/vtable.h"
#include "rz/GameAllocator.h"
#include "rz/RZMemory.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

// The data model of the person's FindBestAction primitive (SimAntics opcode 0x03; lane AF). RVAs are 1.18.0.187.
//
// A search gathers the interactions the Sim could start (the person's pending list, +0x840), checks each one's check tree
// (CheckInteraction), scores the ones that pass (ScoreInteraction) into ActionRecords (+0x2f8 / +0x43c / +0x84c) with a
// ScoreRecord of how the score came about (+0x314 / +0x458), ranks the best into RankedActions (+0x580), and finally picks one
// (ChooseAction) and makes it the person's next action (StartChosenAction).

struct ActionRecord;
struct ScoreRecord;

// The recent-action vector and the owner-id set use GameAllocator (rz/GameAllocator.h), as the original's do: their blocks
// are freed by original code too (the person's destructor), so they must not come from operator new.

// A set of 32-bit ids with the game's allocator for its nodes and buckets (0x20 bytes: max load factor, list head and size, bucket
// vector, mask, bucket count), as the gather step's local set of owner ids is (0x3b1d10 makes its head node, 0x42d5f0 its buckets,
// 0x1fcdb0 frees them, 0x15ac20 looks an id up).
using OwnerIdSet = std::unordered_set<uint32_t, std::hash<uint32_t>, std::equal_to<uint32_t>, GameAllocator<uint32_t>>;
static_assert(sizeof(OwnerIdSet) == 0x20, "OwnerIdSet layout");

// 0x512d70 and 0x512db0 (__cdecl, the qsort comparators): order ActionRecords / ScoreRecords by score, best first; scores
// within 1e-7 of each other compare equal.
int __cdecl CompareActionRecords(const void *a, const void *b);
int __cdecl CompareScoreRecords(const void *a, const void *b);

// A game string embedded in a record: the cRZString layout (vtable, std::string, reference count), built the way the
// records build theirs (the game's vtable, count zero). Copies take the text only.
struct EmbeddedString {
    const void *mVTable; // +0x00 cRZString's vtable (0xd9b470)
    std::string mText;   // +0x04
    uint32_t mRefCount;  // +0x1c

    EmbeddedString() : mVTable(GameAddress<void>(cRZString::kVTable)), mText(), mRefCount(0) {}
    EmbeddedString(const EmbeddedString &other)
        : mVTable(GameAddress<void>(cRZString::kVTable)), mText(other.mText.data(), other.mText.size()), mRefCount(0)
    {
    }
    EmbeddedString &operator=(const EmbeddedString &other)
    {
        if (this != &other) mText.assign(other.mText.data(), other.mText.size());
        return *this;
    }
};
static_assert(sizeof(EmbeddedString) == 0x20, "EmbeddedString layout");

// A reference-counted game object held by a record (AddRef slot 1, Release slot 2; null allowed).
class HeldReference {
public:
    HeldReference() = default;
    explicit HeldReference(void *object) : mObject(object)
    {
        if (mObject) VirtualCall<uint32_t(__thiscall *)(void *), 1>(mObject);
    }
    HeldReference(const HeldReference &other) : mObject(other.mObject)
    {
        if (mObject) VirtualCall<uint32_t(__thiscall *)(void *), 1>(mObject);
    }
    HeldReference &operator=(const HeldReference &) = delete;
    ~HeldReference()
    {
        if (mObject) VirtualCall<uint32_t(__thiscall *)(void *), 2>(mObject);
    }
    void *Get() const { return mObject; }
    // Takes a new object: AddRef it, store it, Release the old one.
    void Hold(void *object)
    {
        if (object) VirtualCall<uint32_t(__thiscall *)(void *), 1>(object);
        void *const old = mObject;
        mObject = object;
        if (old) VirtualCall<uint32_t(__thiscall *)(void *), 2>(old);
    }

private:
    void *mObject = nullptr;
};
static_assert(sizeof(HeldReference) == 4, "HeldReference layout");

// One scored interaction that may become the Sim's next action (0x58 bytes). The vectors at +0x2f8, +0x43c and +0x84c hold
// them. Fields the search leaves unset hold stack leftovers in the original (marked below); ours are zero.
struct ActionRecord {
    uint16_t mStackObjectId;  // +0x00 the object the interaction is on
    uint16_t mEntryIndex;     // +0x02 the interaction's index in that object's table
    float mScore;             // +0x04 -1 until scored
    uint32_t mParameterA;     // +0x08 two dwords of the interaction's parameters (interaction slot 73)
    uint32_t mParameterB;     // +0x0c
    int16_t mParameterC;      // +0x10 (the interaction's group id; sign-extended into the person at +0x8e0)
    uint8_t mFlag12;          // +0x12 the interaction's slot 31 answer
    uint8_t mPad13;
    uint16_t mArguments[8];   // +0x14 the eight tree argument words (-1 in the fourth marks "none")
    float mMotiveScore;       // +0x24 the +0x60c object's slot 7 of the table entry
    float mDistanceScale;     // +0x28 the person's slot 128 of the stack object and 10.0 (tuning 0x1014404)
    float mDistance;          // +0x2c the interaction's slot 12
    EmbeddedString mName;     // +0x30 the interaction's name
    uint8_t mChecked;         // +0x50 set when the interaction came from the person's own list (needs no check)
    uint8_t mPad51[3];
    HeldReference mInteraction; // +0x54 the interaction (a cTSInteraction), counted

    ActionRecord() : mStackObjectId(0), mEntryIndex(0), mScore(0), mParameterA(0), mParameterB(0), mParameterC(0), mFlag12(0), mPad13(0),
                     mArguments{}, mMotiveScore(0), mDistanceScale(0), mDistance(0), mName(), mChecked(0), mPad51{}, mInteraction()
    {
    }
    ActionRecord(const ActionRecord &other) = default;
    ActionRecord &operator=(const ActionRecord &) = delete;
};
static_assert(sizeof(ActionRecord) == 0x58, "ActionRecord layout");
static_assert(offsetof(ActionRecord, mName) == 0x30, "ActionRecord layout");
static_assert(offsetof(ActionRecord, mInteraction) == 0x54, "ActionRecord layout");

// cTSTreeTableAd (RTTI; a cTSTriple<short>): an advertised value of a tree table, 12 bytes. The base class's vtable (0xdc4428)
// is written first and then the class's own (0xdc4430); the destructor slot only restores the base's.
struct TreeTableAd {
    const void *mVTable; // +0x00 0xdc4430
    uint32_t mFirst;     // +0x04
    uint16_t mSecond;    // +0x08
    uint16_t mPad;       // +0x0a (not copied)

    TreeTableAd() : mVTable(GameAddress<void>(0xdc4430)), mFirst(0), mSecond(0), mPad(0) {}
    TreeTableAd(const TreeTableAd &other) : mVTable(GameAddress<void>(0xdc4430)), mFirst(other.mFirst), mSecond(other.mSecond), mPad(0) {}
};
static_assert(sizeof(TreeTableAd) == 12, "TreeTableAd layout");

// cTSTraitAdValue (RTTI; a cTSTriple<short> with vtable 0xdc4438), 12 bytes.
struct TraitAdValue {
    const void *mVTable; // +0x00 0xdc4438
    uint32_t mFirst;     // +0x04
    uint16_t mSecond;    // +0x08
    uint16_t mPad;       // +0x0a (not copied)

    TraitAdValue() : mVTable(GameAddress<void>(0xdc4438)), mFirst(0), mSecond(0), mPad(0) {}
    TraitAdValue(const TraitAdValue &other) : mVTable(GameAddress<void>(0xdc4438)), mFirst(other.mFirst), mSecond(other.mSecond), mPad(0) {}
};
static_assert(sizeof(TraitAdValue) == 12, "TraitAdValue layout");

// How an ActionRecord's score came about (0x44 bytes; the vectors at +0x314, +0x458 hold them, one per ActionRecord).
struct ScoreRecord {
    float mScore;                                 // +0x00 the final score (-1 until scored)
    uint32_t mFlags;                              // +0x04 1: from the person's own list, 8: rejected as too low, 2: boosted, 4/0x10: origin
    float mObjectBonus;                            // +0x08 cITSPerson slot 79 of the stack object (the score multiplies by 1 + this)
    float mInteractionFactor;                      // +0x0c cITSPerson slot 78 of the stack object and the interaction
    uint32_t mInput;                              // +0x10 the person's input value at the time (+0x830)
    int32_t mObjectCategory;                       // +0x14 the stack object's slot 36, sign-extended
    float mProximityFactor;                        // +0x18 -1 until set; 1 / (distance * scale + 1) when scored
    uint32_t mSkillValues[4];                     // +0x1c four attribute values of the stack object's table (-1: none)
    std::vector<TreeTableAd> mTableAds;           // +0x2c
    std::vector<std::vector<TraitAdValue>> mTraitAds; // +0x38

    ScoreRecord() = default;
    ScoreRecord(const ScoreRecord &) = default;
    ScoreRecord(ScoreRecord &&) noexcept = default;
    ScoreRecord &operator=(const ScoreRecord &) = delete;
};
static_assert(sizeof(ScoreRecord) == 0x44, "ScoreRecord layout");

// The table of all objects the person's +0x1c4 points to: a vector of object pointers, indexed by 1-based object number.
struct ObjectRegistry {
    uint8_t mPad[0x1a24];
    void **mObjectsBegin; // +0x1a24
    void **mObjectsEnd;   // +0x1a28

    // The object with number `number`, or null when the number is not positive, past the end or the slot is empty.
    void *Find(int32_t number) const
    {
        if (number <= 0) return nullptr;
        const int32_t count = static_cast<int32_t>(reinterpret_cast<const uint8_t *>(mObjectsEnd) - reinterpret_cast<const uint8_t *>(mObjectsBegin)) >> 2;
        if (static_cast<uint32_t>(number) > static_cast<uint32_t>(count)) return nullptr;
        return mObjectsBegin[number - 1];
    }
};
static_assert(offsetof(ObjectRegistry, mObjectsBegin) == 0x1a24, "ObjectRegistry layout");

// An action the Sim took lately, remembered so the search can discount repeating it (0x28 bytes; the vector at +0x580, kept
// sorted by expiry, newest first).
struct RecentAction {
    uint16_t mStackObjectId; // +0x00
    uint16_t mEntryIndex;    // +0x02
    int32_t mExpiryTick;     // +0x04 the tick the memory ends (-1: already expired)
    EmbeddedString mName;    // +0x08

    RecentAction() : mStackObjectId(0), mEntryIndex(0), mExpiryTick(0), mName() {}
    RecentAction(const RecentAction &other) = default;
    // Assignment copies the ids, the tick and the text, not the string object's vtable and count.
    RecentAction &operator=(const RecentAction &other) = default;
};
static_assert(sizeof(RecentAction) == 0x28, "RecentAction layout");

using RecentActionVector = std::vector<RecentAction, GameAllocator<RecentAction>>;
static_assert(sizeof(RecentActionVector) == 12, "RecentActionVector layout");

// The person's autonomy parameters as one search saw them (0x8c bytes at +0x320): 0x80 bytes of data at +8 and a counted
// reference at +0x88. 0x4365d0 copies them from the object the person points to at +0x814.
struct SearchSnapshot {
    const void *mVTable;     // +0x00 (not copied)
    uint32_t mField4;        // +0x04 (not copied)
    uint32_t mData[32];      // +0x08..+0x87
    void *mReference;        // +0x88 a reference-counted object (AddRef slot 1, Release slot 2)

    // 0x4365d0 (ret 4): copies the data words and takes the other's reference (AddRef the new one, store, Release the old one;
    // nothing happens to the reference when both are the same object). Returns this.
    SearchSnapshot *Assign(const SearchSnapshot *other);
};
static_assert(sizeof(SearchSnapshot) == 0x8c, "SearchSnapshot layout");
