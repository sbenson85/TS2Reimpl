// nTSSG::cLightingManager (lane IR): the scene graph's lighting manager. Interfaces: cILightingManager at +0 (vtable
// 0xdb5e90, 46 slots) and cIGZMessageTarget2 at +4 (vtable 0xdb5f4c). 0x548 bytes (the deleting destructor 0x2cbc40 frees
// that much); constructor 0x2ca700 and destructor 0x2cb800 are still the original's.
//
// The light sets (0x214 bytes each) live in mLightSets; mCurrentSet picks one, and an index past the end means the
// built-in default set at +0x2d0.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nTSSG {

struct cLightVector {
    float x, y, z;
};

// One light set (0x214 bytes). Only the vectors the replaced getters read are named.
struct cLightSet {
    uint8_t mFields00[0x34];
    cLightVector mDirection;  // 0x034 slot 30 returns it negated
    uint8_t mFields40[0x174 - 0x40];
    cLightVector mVector174;  // 0x174 slot 29
    uint8_t mFields180[0x18c - 0x180];
    cLightVector mVector18C;  // 0x18c slot 27
    uint8_t mFields198[0x19c - 0x198];
    cLightVector mVector19C;  // 0x19c slot 28
    uint8_t mFields1A8[0x214 - 0x1a8];
};
static_assert(sizeof(cLightSet) == 0x214);
static_assert(offsetof(cLightSet, mVector19C) == 0x19c);

// What slot 21 appends (20 bytes).
struct cLightSample {
    float mValues[4];
    float mWeight;
};

class cLightingManager {
public:
    // 0x2dfda0 slot 0 (ret 8): ids 1 and 0x6b390bfe give the object, 0x452294aa its message-target face (+4); each
    // AddRefs through slot 1. False for other ids and a null `out`.
    bool QueryInterface(uint32_t iid, void **out);
    // 0x2dfee0 slot 2: the count (+8) drops by one; at zero the object deletes itself (slot 38 (1)) and 0 is returned.
    uint32_t Release();
    // 0x2e1010 slot 18 (ret 4): mFlag44 = `value`, mUseDefaultName = 1.
    void SetFlag44(uint8_t value);
    // 0x2d8370 slot 20: 0x2d4010 (0), then, when the global 0xffde64 is set, 0x2dfc80 (1.0); mFlag81 = 1.
    void Refresh();
    // 0x2cbd40 slot 21 (ret 8): appends {values[0..3], weight} to mSamples.
    void AddSample(const float *values, float weight);
    // 0x2ded30 slot 23: the game's default name (0xdaefa0) while mUseDefaultName is set, else mName's text.
    const char *Name();
    // 0x2db060 slot 25: the word at +0x404 of the object at +0x2c.
    uint32_t Value404();
    // 0x2e3690 / 0x2d8310 / 0x2d4e20 slots 27, 28, 29 (by value): the current light set's vectors at +0x18c, +0x19c and
    // +0x174.
    cLightVector Vector18C();
    cLightVector Vector19C();
    cLightVector Vector174();
    // 0x2df7e0 slot 30 (by value): the current light set's direction, negated.
    cLightVector NegatedDirection();
    // 0x2df860 slot 31 (by value): mVector2C4.
    cLightVector Vector2C4();
    // 0x2ded50 slot 34 (ret 4): the colour mRef24's slot 16 gives for `key`, weighted by the game's three luminance
    // factors (0xdadf5c, 0xdb687c, 0xdb6874).
    float Luminance(uint32_t key);
    // 0x2cbc40 slot 38 (ret 4): the destructor (0x2cb800, the original's), then the memory freed when `flags` bit 0 is
    // set. Returns the object.
    cLightingManager *DeletingDestructor(uint32_t flags);

    const void *mVTable;          // 0x000 0xdb5e90
    const void *mVTable4;         // 0x004 0xdb5f4c (cIGZMessageTarget2)
    uint32_t mRefCount;           // 0x008
    uint8_t mFields0C[0x24 - 0x0c];
    void *mRef24;                 // 0x024 slot 34 asks its slot 16 for a colour
    uint32_t mField28;            // 0x028
    uint8_t *mRef2C;              // 0x02c slot 25 reads its +0x404
    uint8_t mFields30[0x44 - 0x30];
    uint8_t mFlag44;              // 0x044 slot 18
    uint8_t mFields45[0x4c - 0x45];
    uint8_t mUseDefaultName;      // 0x04c 1 at construction; slot 18 sets it
    uint8_t mFields4D[0x81 - 0x4d];
    uint8_t mFlag81;              // 0x081 slot 20 sets it
    uint8_t mFields82[0x98 - 0x82];
    std::vector<cLightSet> mLightSets; // 0x098 (one at construction)
    uint8_t mFieldsA4[0xb0 - 0xa4];
    const void *mNameVTable;      // 0x0b0 a cRZString: vtable, text, reference count
    std::string mName;            // 0x0b4
    uint32_t mNameRefCount;       // 0x0cc
    uint32_t mCurrentSet;         // 0x0d0 an index into mLightSets; out of range: mDefaultSet
    uint8_t mFieldsD4[0x2b0 - 0xd4];
    std::vector<cLightSample> mSamples; // 0x2b0 slot 21 appends
    uint8_t mFields2BC[0x2c4 - 0x2bc];
    cLightVector mVector2C4;      // 0x2c4 (1, 1, 1) at construction; slot 31
    cLightSet mDefaultSet;        // 0x2d0
    uint8_t mFields4E4[0x548 - 0x4e4]; // a stopwatch (0x4e8), a cRZString (0x524)

private:
    const cLightSet &CurrentSet() const;
};
static_assert(offsetof(cLightingManager, mRef24) == 0x24);
static_assert(offsetof(cLightingManager, mUseDefaultName) == 0x4c);
static_assert(offsetof(cLightingManager, mLightSets) == 0x98);
static_assert(offsetof(cLightingManager, mName) == 0xb4);
static_assert(offsetof(cLightingManager, mCurrentSet) == 0xd0);
static_assert(offsetof(cLightingManager, mSamples) == 0x2b0);
static_assert(offsetof(cLightingManager, mDefaultSet) == 0x2d0);
static_assert(sizeof(cLightingManager) == 0x548);

} // namespace nTSSG
