#pragma once
#include "gz/cIGZUnknown.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// cTSEffectsCollection: the effects database the effects scripts fill (lane IT). Effects by name, each a list of components
// (particles, decals, sounds, ... by component type and description name), the component descriptions per type (13 types),
// and two lists the manager links at the end of a load (effect aliases by message pair, message triggers). RTTI: one face,
// cITSEffectsCollection (+0, vtable 0xe8e42c, 13 slots), a cIGZUnknown. 0x74 bytes. All functions /O2 ("speed").
// RVAs 1.18.0.187.
//
// Where it lives: cTSEffectsManager has it as its base at +8 (constructor 0xbc4870 calls ours), cTSEffectsResource at +4
// (constructor 0xbd2ba0, game code: its base subobject is built by the constructor hook, and the resource's own vtable at +4
// then serves the collection slots through the RVA hooks below), and the cTSEffectsCOMDirector factory 0xbd8820 creates
// stand-alone ones. Containers are the game's MSVC 14.40 standard library types (layouts identical to ours).
//
// Standalone shape (docs/standalone-shape.md): a real C++ class; the hooks are adapters (cTSEffectsCollectionAdapters) for
// the game-built resources (game vtables) and the direct calls (the manager's slots call slot 3 and 4 by address).

// One component of an effect (0x88 bytes): a description (by component type and name) placed with the effect's parameters.
// The collection reads the name, the type and the resolved description index; the rest is the effect player's (copied as is).
// The field groups below are as the copies move them (the vector's element assignment in 0xbd4e10 and its uninitialised
// copy 0xbd6aa0): two bytes at +0x58 / +0x59 and then two bytes of padding (never copied), two 16-bit fields at +0x7c / +0x7e,
// and the group at +0x20 below.

// +0x20: a byte and a 16-bit field at +0x22 that a copy construction does not carry over (it is zeroed; the byte at +0x21 is
// left as it was), while an assignment copies all four bytes.
struct EffectComponentByteAndCounter {
    uint8_t mByte;     // +0
    uint8_t mByte1;    // +1 (copied only by assignment)
    uint16_t mCounter; // +2 zero in a copy

    EffectComponentByteAndCounter() = default;
    EffectComponentByteAndCounter(const EffectComponentByteAndCounter &other) : mByte(other.mByte), mCounter(0) {}
    EffectComponentByteAndCounter &operator=(const EffectComponentByteAndCounter &other) = default;
};
static_assert(sizeof(EffectComponentByteAndCounter) == 4);

struct EffectComponent {
    std::string mName;    // +0x00 description name (lower-cased in place when the effect is added)
    int32_t mType;        // +0x18 component type: 1..13 index the description tables, 0 has no description
    uint32_t mWord1c;     // +0x1c
    EffectComponentByteAndCounter mFlags20; // +0x20
    uint32_t mWords24[11]; // +0x24
    uint32_t mWords50[2]; // +0x50
    uint8_t mByte58;      // +0x58
    uint8_t mByte59;      // +0x59
    uint32_t mWords5c[8]; // +0x5c
    uint16_t mHalf7c;     // +0x7c
    uint16_t mHalf7e;     // +0x7e
    uint32_t mWord80;     // +0x80
    int32_t mIndex;       // +0x84 description index in its type's table; negative until resolved

    // 0xbf99f0 (the vector's value construction, count at a time): type 1, the identity placement, the six floats at +0x5c and
    // the word at +0x80 1.0, the index -1. The bytes at +0x21 and +0x5a are left alone.
    EffectComponent();
};
static_assert(sizeof(EffectComponent) == 0x88 && offsetof(EffectComponent, mType) == 0x18 &&
                  offsetof(EffectComponent, mWords50) == 0x50 && offsetof(EffectComponent, mByte59) == 0x59 &&
                  offsetof(EffectComponent, mWords5c) == 0x5c && offsetof(EffectComponent, mHalf7e) == 0x7e &&
                  offsetof(EffectComponent, mIndex) == 0x84,
              "EffectComponent layout");

// One effect (0x28 bytes). The default constructor is a user-provided one in the original (0xbd2940 writes these fields and
// leaves the padding at +6 alone).
struct EffectEntry {
    uint32_t mFlags = 0;                       // +0x00
    uint16_t mVariant = 1;                     // +0x04 effects with one name differ by this (the lookup key's second part)
    uint32_t mParams[5] = {0, 0, 0, 0, 0};     // +0x08
    std::vector<EffectComponent> mComponents;  // +0x1c

    EffectEntry() {}
};
static_assert(sizeof(EffectEntry) == 0x28 && offsetof(EffectEntry, mVariant) == 4 && offsetof(EffectEntry, mParams) == 8 &&
                  offsetof(EffectEntry, mComponents) == 0x1c,
              "EffectEntry layout");

class cITSComponentEffect; // src/ts/cTSEffectBase.h

// A component description (cITSComponentDescription: the cTSCompDescriptionBaseT<D, type> objects the effects parser
// creates; src/ts/cTSCompDescription.h). Slot 0 gives the component type (1..13), slots 7 / 8 count references.
class cITSComponentDescription {
public:
    virtual int32_t GetType() = 0;                         // slot 0
    virtual cITSComponentEffect *CreateEffect() = 0;       // slot 1: a new effect of this description (brush, scrubber, game: none)
    virtual void CopyTo(cITSComponentDescription *to) = 0; // slot 2 (ret 4): the description into `to` when its type is ours
    virtual void *GetDescription() = 0;                    // slot 3: the description struct
    virtual void Read(void *stream) = 0;                   // slot 4 (ret 4): a version, then the description (cIGZIStream)
    virtual void Write(void *stream) = 0;                  // slot 5 (ret 4): the version, then the description (cIGZOStream)
    virtual uint32_t GetSize() = 0;                        // slot 6: the object's size
    virtual uint32_t AddRef() = 0;                         // slot 7
    virtual uint32_t Release() = 0;                        // slot 8
};

// A counted description pointer (the vectors' element: AddRef through slot 7 when taken, Release through slot 8 when
// dropped). Copy only, as the original's: the vector's reallocation copies (AddRef each) and then releases the old ones.
class EffectDescriptionRef {
public:
    EffectDescriptionRef() : mObject(nullptr) {} // a resize's new entries
    EffectDescriptionRef(cITSComponentDescription *description) : mObject(description)
    {
        if (mObject) mObject->AddRef();
    }
    EffectDescriptionRef(const EffectDescriptionRef &other) : EffectDescriptionRef(other.mObject) {}
    EffectDescriptionRef &operator=(cITSComponentDescription *description)
    {
        cITSComponentDescription *old = mObject;
        if (description != old) {
            if (description) description->AddRef();
            mObject = description;
            if (old) old->Release();
        }
        return *this;
    }
    ~EffectDescriptionRef() noexcept(false)
    {
        if (mObject) mObject->Release();
    }
    cITSComponentDescription *Get() const { return mObject; }

private:
    cITSComponentDescription *mObject;
};
static_assert(sizeof(EffectDescriptionRef) == 4);

// The descriptions of one component type (0x2c bytes): name (lower case) to index, and the descriptions by index (unnamed
// ones have no name entry).
struct EffectDescriptionTable {
    std::unordered_map<std::string, uint32_t> mNames;    // +0x00
    std::vector<EffectDescriptionRef> mDescriptions;     // +0x20
};
static_assert(sizeof(EffectDescriptionTable) == 0x2c && offsetof(EffectDescriptionTable, mDescriptions) == 0x20,
              "EffectDescriptionTable layout");

// An effect alias for a message pair (0x20 bytes; slot 9): the manager's link step (its slot 4 at +8) maps (mFirst,
// mSecond) to every effect named mEffect.
struct EffectMessageAlias {
    std::string mEffect; // +0x00
    uint32_t mFirst;     // +0x18
    uint32_t mSecond;    // +0x1c
};
static_assert(sizeof(EffectMessageAlias) == 0x20, "EffectMessageAlias layout");

// A message trigger (0x1c bytes; slot 10): the manager registers for mMessage and plays mEffect when it arrives.
struct EffectMessageTrigger {
    uint32_t mMessage;   // +0x00
    std::string mEffect; // +0x04
};
static_assert(sizeof(EffectMessageTrigger) == 0x1c, "EffectMessageTrigger layout");

// The collection's interface (id 0xc97cf5ce; global name as its RTTI).
class cITSEffectsCollection : public cIGZUnknown {
public:
    virtual void Clear() = 0;                                                         // slot 3
    virtual void Link() = 0;                                                          // slot 4
    virtual void AddEffect(const char *name, const EffectEntry *effect) = 0;          // slot 5 (ret 8)
    virtual bool GetEffect(const char *name, EffectEntry *out) = 0;                   // slot 6 (ret 8)
    virtual uint32_t AddDescription(const char *name, cITSComponentDescription *description) = 0; // slot 7 (ret 8)
    virtual bool GetDescription(const char *name, int32_t type, cITSComponentDescription **out) = 0; // slot 8 (ret 0xc)
    virtual void AddMessageAlias(const char *effect, uint32_t first, uint32_t second) = 0; // slot 9 (ret 0xc)
    virtual void AddMessageTrigger(const EffectMessageTrigger &trigger) = 0;          // slot 10 (ret 4)
    virtual bool SetEffectVariant(const char *name, uint32_t variant, const char *newName) = 0; // slot 11 (ret 0xc)
    virtual ~cITSEffectsCollection() = default;                                       // slot 12
};

class cTSEffectsCollection : public cITSEffectsCollection {
public:
    static constexpr uint32_t kOriginalVTable = 0xe8e42c;
    static constexpr uint32_t kInterfaceId = 0xc97cf5ce;
    static constexpr uint32_t kTypeCount = 13;
    static constexpr size_t kSize = 0x74;

    cTSEffectsCollection();            // 0xbd7210
    ~cTSEffectsCollection() override;  // 0xbd7670; slot 12 the scalar deleting destructor 0xbd7820
    cTSEffectsCollection(const cTSEffectsCollection &) = delete;
    cTSEffectsCollection &operator=(const cTSEffectsCollection &) = delete;
    static cTSEffectsCollection *Create(); // 0xbd8820 (cdecl): new, null on failure

    bool QueryInterface(uint32_t riid, void **out) override; // slot 0, 0xbd8a40
    uint32_t AddRef() override;                              // slot 1, folded 0x148fb0 (a verified body: ++count)
    uint32_t Release() override;                             // slot 2, folded 0x244060: at 0, delete this (slot 12)

    void Clear() override;                                                         // slot 3, 0xbd8680
    void Link() override;                                                          // slot 4, folded 0x140fe0 (empty)
    void AddEffect(const char *name, const EffectEntry *effect) override;          // slot 5, 0xbd7c00
    bool GetEffect(const char *name, EffectEntry *out) override;                   // slot 6, 0xbd8970
    uint32_t AddDescription(const char *name, cITSComponentDescription *description) override; // slot 7, 0xbd7840
    bool GetDescription(const char *name, int32_t type, cITSComponentDescription **out) override; // slot 8, 0xbd8890
    void AddMessageAlias(const char *effect, uint32_t first, uint32_t second) override; // slot 9, 0xbd7b40
    void AddMessageTrigger(const EffectMessageTrigger &trigger) override;          // slot 10, 0xbd7ef0
    bool SetEffectVariant(const char *name, uint32_t variant, const char *newName) override; // slot 11, 0xbd8b50

    // 0xbd87b0 (ret 8): the index of the effect named `name` with that variant, -1 when there is none.
    int32_t FindEffectIndex(const std::string &name, uint32_t variant);

    // 0xbd7f30 (ret 4): this collection's contents into `target` (cTSEffectsResource's slot 5, which the manager's
    // ParseResources calls with its own collection): swapped in whole when the target has no effects; else added through
    // the target's slots (AddAllTo, with mFlag58 set) or appended (AppendTo).
    void MergeInto(cTSEffectsCollection *target);
    // 0xbd8080 (ret 4): every named description (table order, each table's name list in order) through the target's slot 7,
    // then every effect (name list order) through its slot 5.
    void AddAllTo(cTSEffectsCollection *target);
    // 0xbd8160 (ret 4): the descriptions, effects, triggers and aliases appended to the target's; this collection's
    // component indices are first moved past the target's (by type), and each name replaces the target's entry of the same
    // name and variant.
    void AppendTo(cTSEffectsCollection *target);

    // mScratch = name, lower-cased (the common first step of the lookups).
    void SetScratchLower(const char *name);

    // +0x00 vtable
    uint32_t mRefCount;                                                // +0x04
    std::unordered_multimap<std::string, uint32_t> mEffectsByName;     // +0x08 lower-case name to effect index
    std::vector<EffectEntry> mEffects;                                 // +0x28
    std::vector<EffectDescriptionTable> mTables;                       // +0x34 one per component type (type - 1)
    std::vector<EffectMessageAlias> mMessageAliases;                   // +0x40
    std::vector<EffectMessageTrigger> mMessageTriggers;                // +0x4c
    bool mFlag58;                                                      // +0x58 zero after construction; MergeInto's choice
    std::string mScratch;                                              // +0x5c the lower-cased key of the last lookup
};
static_assert(sizeof(cTSEffectsCollection) == cTSEffectsCollection::kSize, "cTSEffectsCollection layout");
static_assert(offsetof(cTSEffectsCollection, mRefCount) == 4 && offsetof(cTSEffectsCollection, mEffectsByName) == 8 &&
                  offsetof(cTSEffectsCollection, mEffects) == 0x28 && offsetof(cTSEffectsCollection, mTables) == 0x34 &&
                  offsetof(cTSEffectsCollection, mMessageAliases) == 0x40 &&
                  offsetof(cTSEffectsCollection, mMessageTriggers) == 0x4c && offsetof(cTSEffectsCollection, mFlag58) == 0x58 &&
                  offsetof(cTSEffectsCollection, mScratch) == 0x5c,
              "cTSEffectsCollection members");

// The RVA hooks' entry points (src/hook/tables/cTSEffectsCollection.cpp): this in ecx = the collection face, our members
// called without virtual dispatch.
struct cTSEffectsCollectionAdapters {
    using Self = cTSEffectsCollection;
    static Self *__fastcall Construct(Self *memory, void *);
    static void __fastcall Destruct(Self *self, void *);
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t riid, void **out);
    static void __fastcall Clear(Self *self, void *);
    static void __fastcall AddEffect(Self *self, void *, const char *name, const EffectEntry *effect);
    static bool __fastcall GetEffect(Self *self, void *, const char *name, EffectEntry *out);
    static uint32_t __fastcall AddDescription(Self *self, void *, const char *name, cITSComponentDescription *description);
    static bool __fastcall GetDescription(Self *self, void *, const char *name, int32_t type, cITSComponentDescription **out);
    static void __fastcall AddMessageAlias(Self *self, void *, const char *effect, uint32_t first, uint32_t second);
    static void __fastcall AddMessageTrigger(Self *self, void *, const EffectMessageTrigger &trigger);
    static bool __fastcall SetEffectVariant(Self *self, void *, const char *name, uint32_t variant, const char *newName);
    static int32_t __fastcall FindEffectIndex(Self *self, void *, const std::string &name, uint32_t variant);
    static void __fastcall MergeInto(Self *self, void *, Self *target);
    static void __fastcall AddAllTo(Self *self, void *, Self *target);
    static void __fastcall AppendTo(Self *self, void *, Self *target);
};
