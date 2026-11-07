#pragma once
// cTSWantDefinition (lane IL): one want's definition, a persist resource (class id 0xed7d7b4d, interface cITSWantDefinition
// 0xed7d7b4e) the want tree manager creates (cTSWantTreeManager::CreateInstance#2 0x552370) and fills from a want node's
// property set (ReadProperties, slot 21): its id, score, influence, fear flag, level, icons, the three tree names, the event
// and object conditions, the integer test, the node text, the category and its four strings (from a string table). 0x1a0
// bytes; game vtables 0xdd8548 (+0, 22 slots) and 0xdd85a4 (+4, cIGZPersistResource, 12 slots).
//
// Standalone shape (docs/standalone-shape.md): a real class. The constructor 0x5b10e0 (hooked: placement new) is the only
// place the game stores the vtables besides the destructor 0x5b12d0, so every definition carries ours and the slots are in
// effect through their callers. The cRZStrings are still hook-shaped (Construct / Destruct), so they sit in anonymous
// unions, built and destroyed by hand as the original's inlined code does.
#include "gz/cGZPersistResourceManager.h" // cGZPersistResourceKey
#include "gz/cIGZPersistResource.h"
#include "gz/cIGZPropertySet.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZString.h"

#include <cstdint>

// The definition's own interface (cITSWantDefinition, iid 0xed7d7b4e): slots 3-21.
class cITSWantDefinition : public cIGZUnknown {
public:
    virtual uint32_t GetId() = 0;                                  // slot 3
    virtual cRZString *GetName() = 0;                              // slot 4
    virtual cRZString *GetCategory() = 0;                          // slot 5
    virtual int32_t GetScore() = 0;                                // slot 6
    virtual int32_t GetInfluence() = 0;                            // slot 7
    virtual uint8_t IsFear() = 0;                                  // slot 8 (the raw byte: a property's AnyBoolean byte as read)
    virtual uint32_t GetLevel() = 0;                               // slot 9
    virtual cGZPersistResourceKey *GetPrimaryIcon() = 0;           // slot 10
    virtual cGZPersistResourceKey *GetSecondaryIcon() = 0;         // slot 11
    virtual void GetString(uint32_t index, cIGZString *out) = 0;   // slot 12
    virtual cRZString *GetCheckTree() = 0;                         // slot 13
    virtual cRZString *GetFeedbackTree() = 0;                      // slot 14
    virtual cRZString *GetSimArrayTree() = 0;                      // slot 15
    virtual uint32_t GetObjectType() = 0;                          // slot 16
    virtual uint32_t GetIntegerOperator() = 0;                     // slot 17
    virtual int32_t GetIntegerMultiplier() = 0;                    // slot 18
    virtual uint32_t GetGameVersionFlags() = 0;                    // slot 19
    virtual void WriteProperties(cIGZPropertySetString *set) = 0;  // slot 20
    virtual void ReadProperties(cIGZPropertySetString *set) = 0;   // slot 21
};

class cTSWantDefinition : public cITSWantDefinition, public cIGZPersistResource {
public:
    static constexpr uint32_t kClassId = 0xed7d7b4d;
    static constexpr uint32_t kIid = 0xed7d7b4e;
    static constexpr uint32_t kPersistResourceIid = 0x456b8f1d;
    static constexpr uint32_t kSize = 0x1a0;
    static constexpr uint32_t kStringCount = 5; // mStrings: index 0 is never filled by ReadProperties

    cTSWantDefinition();  // 0x5b10e0
    cTSWantDefinition(const cTSWantDefinition &) = delete;
    cTSWantDefinition &operator=(const cTSWantDefinition &) = delete;

    // cIGZUnknown (both faces)
    bool QueryInterface(uint32_t iid, void **out) override; // 0x5b1950: 1, kIid -> this; kPersistResourceIid -> the +4 face
    uint32_t AddRef() override;                             // shared 0x145260: ++count at +8
    uint32_t Release() override;                            // 0x5b2720: at 1 or below the destructor and the 0x1a0 bytes freed

    // cITSWantDefinition
    uint32_t GetId() override;                              // slot 3 (shared 0x147d00): +0x1c
    // slot 4 (0x5b18e0): the node text; an empty one first becomes "<unnamed want 0x%08x>" of the id.
    cRZString *GetName() override;
    cRZString *GetCategory() override;                      // slot 5 (0x5b18d0): &+0x180
    int32_t GetScore() override;                            // slot 6 (shared 0x15f390)
    int32_t GetInfluence() override;                        // slot 7 (shared 0x147770)
    uint8_t IsFear() override;                              // slot 8 (shared 0x557720): the byte at +0x21
    uint32_t GetLevel() override;                           // slot 9 (shared 0x15e970)
    cGZPersistResourceKey *GetPrimaryIcon() override;       // slot 10 (shared 0x1c6e40): &+0x30
    cGZPersistResourceKey *GetSecondaryIcon() override;     // slot 11 (shared 0x1c8800): &+0x40
    // slot 12 (0x5b1800): mStrings[index] into `out` (its slot 17); an empty one gives "<" name ">" instead.
    void GetString(uint32_t index, cIGZString *out) override;
    cRZString *GetCheckTree() override;                     // slot 13 (shared 0x5b17f0): &+0xf0
    cRZString *GetFeedbackTree() override;                  // slot 14 (0x5b1940): &+0x110
    cRZString *GetSimArrayTree() override;                  // slot 15 (0x5b1930): &+0x130
    uint32_t GetObjectType() override;                      // slot 16 (shared 0x142850): +0x154
    uint32_t GetIntegerOperator() override;                 // slot 17 (shared 0x5b1920): +0x158
    int32_t GetIntegerMultiplier() override;                // slot 18 (shared 0x5b1910): +0x15c
    uint32_t GetGameVersionFlags() override;                // slot 19 (shared 0x151b40): +0x150
    // slot 20 (0x5b1500): id, score, influence, isFear (as a uint32) and nodeText (a new string) into the set.
    void WriteProperties(cIGZPropertySetString *set) override;
    // slot 21 (0x5b19b0): every property the definition knows, each when present (see the .cpp).
    void ReadProperties(cIGZPropertySetString *set) override;

    // cIGZPersistResource (+4)
    cGZPersistResourceKey GetKey() override;                // shared 0x1ebbd0: +0xc
    bool SetKey(const cGZPersistResourceKey &key) override; // shared 0x1eec50
    uint32_t GetType() override;                            // shared 0x46390 (face +8)
    uint32_t GetGroup() override;                           // shared 0x145770
    uint32_t GetInstance() override;                        // shared 0x149cf0
    bool SetType(uint32_t type) override;                   // shared 0x1eefc0
    bool SetGroup(uint32_t group) override;                 // shared 0x1eeb70
    bool SetInstance(uint32_t instance) override;           // shared 0x1eec40
    uint32_t GetRefCount() override;                        // shared 0x147a30 (face +4)

    // +0x00 the cITSWantDefinition vtable, +0x04 the cIGZPersistResource vtable
    int32_t mRefCount = 0;                   // +0x08
    cGZPersistResourceKey mKey{};            // +0x0c the resource key (+0x18 its last word)
    uint32_t mId = 0;                        // +0x1c the want's guid ("id")
    uint8_t mByte20 = 0;                     // +0x20 cleared by the constructor (a word store with +0x21), never read here
    uint8_t mIsFear = 0;                     // +0x21 ("isFear"; when absent, whether the score is negative)
    uint32_t mLevel = 0;                     // +0x24 ("level": 2 Power, 1 Memorable, 0 other)
    int32_t mScore = 0;                      // +0x28
    int32_t mInfluence = 0;                  // +0x2c
    cGZPersistResourceKey mPrimaryIcon;      // +0x30 {0x856ddbac, 0x499db772, icon, 0}
    cGZPersistResourceKey mSecondaryIcon;    // +0x40
    union { cRZString mStrings[kStringCount]; }; // +0x50 the string table's entries 1-4 (0 unused)
    union { cRZString mCheckTree; };         // +0xf0
    union { cRZString mFeedbackTree; };      // +0x110
    union { cRZString mSimArrayTree; };      // +0x130
    uint32_t mGameVersionFlags = 1;          // +0x150
    uint32_t mObjectType = 0;                // +0x154 (eventRequiresSim 1, eventRequiresObject 2; objectType names 1-6)
    uint32_t mIntegerOperator = 0;           // +0x158 (Equals 1 .. Greater_Then_Or_Equals 5)
    int32_t mIntegerMultiplier = 1;          // +0x15c
    union { cRZString mNodeText; };          // +0x160
    union { cRZString mCategory; };          // +0x180

private:
    ~cTSWantDefinition(); // 0x5b12d0 (not virtual: Release calls it)
};
static_assert(sizeof(cTSWantDefinition) == cTSWantDefinition::kSize, "cTSWantDefinition size");

// The constructor hook's entry point (placement new of ours: every definition the game creates carries our vtables).
struct cTSWantDefinitionAdapters {
    static cTSWantDefinition *__fastcall Construct(void *memory, void *); // 0x5b10e0
};
