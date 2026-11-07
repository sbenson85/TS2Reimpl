// nTSSG::cShadowManager (lane IR): the scene graph's shadow manager (framework class 0x0bf9c04f; cTSSGSystem keeps it at
// +0xa8). 0xd4 bytes (the deleting destructor 0x2e6e10 frees that much); constructor 0x2e63a0 and destructor 0x2e6c70 are
// still the original's. Interfaces: cIShadowManager at +0 (vtable 0xdb68ac, 18 slots), cIGZMessageTarget2 at +4
// (vtable 0xdb68f8).
//
// The options (+0x24..+0x4c) count their changes in mChangeCount; the constructor's defaults go through the same
// counting stores. Shadows live in mShadows, 0x98-byte entries in use while their mUsers is above zero.
#pragma once

#include <cstddef>
#include <cstdint>

namespace nTSSG {

// One shadow (0x98 bytes; the vector at +0x54).
struct cShadowEntry {
    int32_t mUsers;               // 0x00 in use while above zero
    uint8_t mFields04[0x2c - 0x04];
    uint32_t mMatrixStamp;        // 0x2c slot 14 hands it out with the matrix
    uint32_t mTextureHandle;      // 0x30 slot 13 turns it into the texture (0x20dfb5's slot 24)
    uint32_t mScaleKeys[2];       // 0x34 slot 15 hands them to the renderer's slot 8
    float mMatrix[16];            // 0x3c slot 14
    float mBox[7];                // 0x7c slot 16
};
static_assert(sizeof(cShadowEntry) == 0x98);
static_assert(offsetof(cShadowEntry, mMatrix) == 0x3c);
static_assert(offsetof(cShadowEntry, mBox) == 0x7c);

// What slot 16 returns (by value): an entry's mBox, or 0, 0, 0, 0, 0, 0, 1 for a bad index.
struct cShadowBox {
    float mValues[7];
};

class cShadowManager {
public:
    // 0x2e89a0 slot 0 (ret 8): ids 1 and 0xcbf9c006 give the object, 0x452294aa its message-target face (+4); each
    // AddRefs through slot 1. False for other ids and for a null `out`.
    bool QueryInterface(uint32_t iid, void **out);
    // 0x2e8a10 slot 2: the count drops by one; at zero the object deletes itself (slot 17 (1)) and 0 is returned.
    uint32_t Release();
    // 0x2e8d80 slot 6 (ret 8): option bit `index` (0..9; others ignored) set to `on`, counted in mChangeCount when it
    // changes. `on` is compared as a byte with the bit (0 or 1), so any other value counts as a change that sets it.
    void SetOption(uint32_t index, uint8_t on);
    // 0x2e7b90 slot 11 (ret 4): *out = the option bits.
    void GetOptionBits(uint32_t *out);
    // 0x2e9620 slot 13 (ret 4): the texture of shadow `index` (the service 0x20dfb5's slot 24 on its handle); 0 for a
    // bad index or an unused entry.
    uint32_t ShadowTexture(int32_t index);
    // 0x2e9480 slot 14 (ret 0xc): copies shadow `index`'s matrix to `out` and true, unless the index is bad or the
    // entry unused, or `stamp` is given and already equals the entry's stamp (else it gets it).
    bool GetShadowMatrix(int32_t index, float *out, uint32_t *stamp);
    // 0x2e95a0 slot 15 (ret 0xc): the renderer's (+0x50) slot 8 with shadow `index`'s two keys, `offset` and `scale`;
    // for a bad index offset = (0, 0) and scale = (1, 1). Nothing is returned (the original leaves eax as it happens).
    void GetShadowScale(int32_t index, float *offset, float *scale);
    // 0x2e9500 slot 16 (ret 8): shadow `index`'s box (see cShadowBox).
    cShadowBox GetShadowBox(int32_t index);
    // 0x2e6e10 slot 17 (ret 4): the destructor (0x2e6c70, the original's), then the memory freed when `flags` bit 0 is
    // set. Returns the object.
    cShadowManager *DeletingDestructor(uint32_t flags);

    const void *mVTable;          // 0x00 0xdb68ac
    const void *mVTable4;         // 0x04 0xdb68f8 (cIGZMessageTarget2)
    uint8_t mStarted;             // 0x08 slot 4 clears it (and does its work only when it was set)
    uint8_t mPad09[3];
    uint32_t mRefCount;           // 0x0c
    uint32_t mTree10[2];          // 0x10 a std::set (head, size; 0x14-byte nodes)
    uint32_t mMessageIds[2];      // 0x18 std::set<uint32_t> (head, size): the six message ids the object listens to
    int32_t mChangeCount;         // 0x20 1 at construction; every option change adds one
    float mOption24;              // 0x24 1.0
    float mOption28;              // 0x28 10.0
    int32_t mOption2C;            // 0x2c 3
    int32_t mOption30;            // 0x30 2
    int32_t mOption34;            // 0x34 0x80
    int32_t mMapSize;             // 0x38 0x80: the shadow texture's width and height
    float mOption3C;              // 0x3c 0.7
    float mOption40;              // 0x40 -1.0
    float mOption44;              // 0x44 0.7
    float mOption48;              // 0x48 2.0
    uint32_t mOptionBits;         // 0x4c bits 0..9 (slot 6); bits 4 and 6 set at construction
    void *mRenderer;              // 0x50 reference; slot 15 asks its slot 8
    cShadowEntry *mShadows;       // 0x54 std::vector<cShadowEntry> (first, last, end)
    cShadowEntry *mShadowsLast;   // 0x58
    cShadowEntry *mShadowsEnd;    // 0x5c
    uint32_t mFreeIndices[3];     // 0x60 std::vector<int> (0..63 when the shadows are made)
    uint8_t mHash6C[0x8c - 0x6c]; // 0x6c a std::unordered_map (max load 1.0, 0x2c-byte nodes, mask 7)
    uint8_t mQueue8C[0xa0 - 0x8c]; // 0x8c a container with a proxy (0x8c) and four words
    uint32_t mFieldsA0[6];        // 0xa0
    float mFieldB8;               // 0xb8 1.0
    void *mRefBC;                 // 0xbc framework class 0xdca76dbb
    void *mRefC0;                 // 0xc0 framework class 0x25232b11
    void *mRefC4;                 // 0xc4 framework class 0x65246462
    uint32_t mHandleC8;           // 0xc8 a texture handle (0xae82b0 ends it)
    void *mRefCC;                 // 0xcc a 0x2c-byte object (0x3c9e50)
    uint8_t mFlagD0;              // 0xd0 0
    uint8_t mFlagD1;              // 0xd1 1
    uint8_t mPadD2[2];

private:
    cShadowEntry *Entry(int32_t index);
};
static_assert(offsetof(cShadowManager, mChangeCount) == 0x20);
static_assert(offsetof(cShadowManager, mOptionBits) == 0x4c);
static_assert(offsetof(cShadowManager, mShadows) == 0x54);
static_assert(offsetof(cShadowManager, mHash6C) == 0x6c);
static_assert(offsetof(cShadowManager, mRefBC) == 0xbc);
static_assert(offsetof(cShadowManager, mRefCC) == 0xcc);
static_assert(sizeof(cShadowManager) == 0xd4);

} // namespace nTSSG
