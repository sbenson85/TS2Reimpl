#pragma once
// nGZGraphic4::cDeviceResourceManagerDX (RTTI 0x1098400; vftable 0xd9f9d0, 19 slots; 0x3c bytes): the device's register
// of its resources (textures, buffers, surfaces) and the texture memory budget (lane NT, rendering campaign R1c;
// docs/subsystems/rendering.md). Every resource registers itself (slot 6 Add / 5 Insert) when created and unregisters
// (slot 8 Remove / 7 Erase) when released; the manager adds up the video memory they take (mMemoryDelta since the last
// ManageTextureMemory), and when the device sees the delta or the pending bytes (the textures' SetQuality) pass the limit
// it calls slot 12, which lowers the textures' levels (largest first) until what they take fits the budget.
//
// Standalone shape (docs/standalone-shape.md): our class, our vtable. cDeviceDX::Init (ours) makes the manager through
// cDeviceResourceManagerAdapters::Construct (the constructor 0x17ad50); CreateDevice and ReleaseDevice (ours) call the
// members. Nothing else in the image reaches the game's vtable 0xd9f9d0, so the original's slots are in effect through
// their callers. The consumers (surfaces, textures, vertex arrays and formats) call the slots through the generated view
// ifaces::cIDeviceResourceManager, which sees any object with this slot order.
//
// The set is a std::set (the original's _Tree: head node, size; nodes of 0x14 bytes with the pointer at +0x10) and the
// working list a std::vector of 16-byte entries: the layout is the original's, and only our code reads it.
//
// cDeviceResourceIterator (vftable 0xd9fa20, 9 slots; 0xc bytes from the fixed pool 0xfedb58) walks the set; only slot 9
// (GetResources) makes them, and nothing in the image calls that slot.
#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

#include "gz/cDeviceEnum.h" // cIteratorRange
#include "gz/cSurfaceDX.h"  // cIDeviceResource, cIAllocDeviceResource
#include "gz/cIGZUnknown.h"

#include "game/owned.h"
TS2_OWNED_STATIC(const char, d9fa60)[]; // the source file of the class's reports (kSourceFileRva)

#include "game/owned.h"
#include "rz/RZFastFixedAllocators.h"

// The classes' fixed-size pools (operator new / delete).
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0xc>, fedb58);

namespace nGZGraphic4 {

class cIDeviceDX;
class cITextureBaseDX;

constexpr uint32_t kIidDeviceResourceManager = 0xbae8fd06;

// The set of resources: ordered by address, not counted.
using cDeviceResourceSet = std::set<cIDeviceResource *>;
static_assert(sizeof(cDeviceResourceSet) == 8, "std::set: head and size");

// The settings GetPolicy hands out (through a hidden pointer; the three padding bytes are left as they were).
struct cResourcePolicy {
    uint8_t mEnabled;  // +0
    float mQuality;    // +4: the share of the available memory the textures may take, in percent
    uint32_t mLimit;   // +8: bytes of change that make the device call ManageTextureMemory
};
static_assert(sizeof(cResourcePolicy) == 0xc, "cResourcePolicy: 12 bytes");

// cIGZBidirectionalIterator<cIDeviceResource *, cGZElementByValue<..>> (vftable 0xd9fa20, 9 slots).
class cIDeviceResourceIterator : public cIGZUnknown {
public:
    virtual cIDeviceResourceIterator *Clone() = 0;                    // slot 3: a new iterator at the same place (count 0)
    virtual bool IsEqual(const cIDeviceResourceIterator *other) = 0;  // slot 4
    virtual cIDeviceResource *Get() = 0;                              // slot 5
    virtual void Next() = 0;                                          // slot 6
    virtual void Prev() = 0;                                          // slot 7
    virtual ~cIDeviceResourceIterator() = default;                    // slot 8: the deleting destructor
};

using cDeviceResourceRange = cIteratorRange<cIDeviceResourceIterator>;

// nGZGraphic4::cIDeviceResourceManager (iid 0xbae8fd06).
class cIDeviceResourceManager : public cIGZUnknown {
public:
    virtual cIDeviceDX *GetDevice() = 0;       // slot 3 (shared 0x46390)
    virtual cIDeviceDX *GetDeviceConst() = 0;  // slot 4 (shared 0x46390)
    // 5: registers `resource` (a set insert), no budget.
    virtual void Insert(cIDeviceResource *resource) = 0;
    // 6: registers it and counts its video memory into mMemoryDelta; a default-pool resource makes the available memory
    // unknown (read again).
    virtual void Add(cIAllocDeviceResource *resource) = 0;
    virtual uint32_t Erase(cIDeviceResource *resource) = 0;  // 7: the set erase; the number erased (0 or 1)
    virtual void Remove(cIAllocDeviceResource *resource) = 0;    // 8: unregisters it and takes its video memory off
    virtual cDeviceResourceRange GetResources() = 0;         // 9: cursor, begin and end over the set (each counted)
    virtual void SetPolicy(bool enabled, float quality, uint32_t limit) = 0; // 10: then ResetBudget
    virtual void ResetBudget() = 0;                          // 11: untracked, the available memory unknown
    virtual void ManageTextureMemory() = 0;                  // 12: lowers texture levels until the budget fits
    virtual void RefreshAvailableMemory() = 0;               // 13: evicts managed resources, reads the available memory
    virtual void AddPending(int32_t bytes) = 0;              // 14: adds abs(bytes) to mPending
    virtual cResourcePolicy GetPolicy() = 0;                 // 15
    virtual uint32_t GetFreeMemory() = 0;                    // 16: available less reserved (0 when the device is not ready)
    virtual float GetQuality() = 0;                          // 17 (shared 0x17b050)
    virtual ~cIDeviceResourceManager() = default;            // slot 18: the deleting destructor
};

// One texture ManageTextureMemory may lower: the texture (not counted), the level it will keep as the top one, the
// bytes it takes at that level and one over its quality (what a level's bytes weigh against the others).
struct cBudgetEntry {
    cITextureBaseDX *mTexture; // +0
    uint32_t mLevel;           // +4
    int32_t mSize;             // +8
    float mFactor;             // +0xc
};
static_assert(sizeof(cBudgetEntry) == 0x10, "cBudgetEntry: 16 bytes");

class cDeviceResourceManagerDX : public cIDeviceResourceManager {
public:
    static constexpr size_t kSize = 0x3c;
    static constexpr int32_t kUntracked = 0x7fffffff; // mMemoryDelta when the budget is not being tracked
    static constexpr uint32_t kDefaultLimit = 0x200000;
    static constexpr float kDefaultQuality = 95.0f;
    static constexpr uint32_t kSourceFileRva = 0xd9fa60; // LogD3DError's file
    static constexpr uint32_t kIidTextureBaseDX = 0x1b891e38;
    static constexpr uint32_t kIidDeviceEnum = 0x6c02c311;

    // 0x17ad50 (thiscall, ret 4): no resources, the budget on and tracking nothing yet.
    explicit cDeviceResourceManagerDX(cIDeviceDX *device);
    ~cDeviceResourceManagerDX() override; // the vector's buffer, then the set (0x17af90 is the deleting destructor)

    // Non-virtual members, called by cDeviceDX's CreateDevice and ReleaseDevice.
    // 0x17bb80: releases every registered resource (slot 3 of each unregisters it) until the set is empty.
    void ReleaseAll();
    // 0x17b710: ResetBudget, then releases each resource that does not survive a reset (slot 8 false).
    void ReleaseNonSurvivors();
    // 0x17bb30 (declared ret 8; it reads neither argument): releases each resource that does not survive a mode change
    // (slot 9 false); the budget is left alone.
    void ReleaseOnModeChange();
    // 0x17bb00: the available memory is the new device's, the budget starts again, nothing is reserved.
    void TakeDevice(IDirect3DDevice9 *device);

    bool QueryInterface(uint32_t riid, void **ppvObj) override; // slot 0 (0x17b7d0): 1 and 0xbae8fd06
    uint32_t AddRef() override;                                 // slot 1 (shared 0x148fb0)
    uint32_t Release() override;                                // slot 2 (0x17baa0): slot 18 at 0
    cIDeviceDX *GetDevice() override;
    cIDeviceDX *GetDeviceConst() override;
    void Insert(cIDeviceResource *resource) override;           // 0x17b8e0
    void Add(cIAllocDeviceResource *resource) override;            // 0x17b800
    uint32_t Erase(cIDeviceResource *resource) override;        // 0x17bc10
    void Remove(cIAllocDeviceResource *resource) override;         // 0x17bbb0
    cDeviceResourceRange GetResources() override;               // 0x17b990
    void SetPolicy(bool enabled, float quality, uint32_t limit) override; // 0x17bac0
    void ResetBudget() override;                                // 0x17baf0
    void ManageTextureMemory() override;                        // 0x17b080
    void RefreshAvailableMemory() override;                     // 0x17b690
    void AddPending(int32_t bytes) override;                    // 0x17ba90
    cResourcePolicy GetPolicy() override;                       // 0x17b060
    uint32_t GetFreeMemory() override;                          // 0x17b010
    float GetQuality() override;                                // shared 0x17b050

private:
    friend struct cDeviceResourceManagerLayout;

    uint32_t mRefCount;           // +0x04 (not atomic)
    cIDeviceDX *mDevice;          // +0x08 (not counted)

public:
    // cDeviceDX reads these before every draw (the budget check): the device calls slot 12 once either delta passes
    // the limit.
    int32_t mMemoryDelta;         // +0x0c: bytes added since the last ManageTextureMemory, or kUntracked
    int32_t mPending;             // +0x10: slot 14 adds to it
    uint8_t mEnabled;             // +0x14 (constructor: 1)
    uint8_t mPad15[3];            // +0x15: never written
    float mQuality;               // +0x18 (constructor: 95.0)
    uint32_t mLimit;              // +0x1c (constructor: 0x200000)

private:
    uint32_t mReserved;           // +0x20: slot 16 subtracts it from the available memory
    uint32_t mAvailable;          // +0x24: the available texture memory last read (0: read again)
    std::vector<cBudgetEntry> mEntries; // +0x28: ManageTextureMemory's working list (kept between calls)
    cDeviceResourceSet mResources;      // +0x34
};

struct cDeviceResourceManagerLayout {
    static_assert(offsetof(cDeviceResourceManagerDX, mRefCount) == 0x04, "layout");
    static_assert(offsetof(cDeviceResourceManagerDX, mDevice) == 0x08, "layout");
    static_assert(offsetof(cDeviceResourceManagerDX, mMemoryDelta) == 0x0c, "layout");
    static_assert(offsetof(cDeviceResourceManagerDX, mPending) == 0x10, "layout");
    static_assert(offsetof(cDeviceResourceManagerDX, mEnabled) == 0x14, "layout");
    static_assert(offsetof(cDeviceResourceManagerDX, mQuality) == 0x18, "layout");
    static_assert(offsetof(cDeviceResourceManagerDX, mLimit) == 0x1c, "layout");
    static_assert(offsetof(cDeviceResourceManagerDX, mReserved) == 0x20, "layout");
    static_assert(offsetof(cDeviceResourceManagerDX, mAvailable) == 0x24, "layout");
    static_assert(offsetof(cDeviceResourceManagerDX, mEntries) == 0x28, "layout");
    static_assert(offsetof(cDeviceResourceManagerDX, mResources) == 0x34, "layout");
};
static_assert(sizeof(cDeviceResourceManagerDX) == cDeviceResourceManagerDX::kSize, "cDeviceResourceManagerDX size");

// cDeviceDX::Init makes the manager through this (a function of its own, so the tests can stand in for it).
struct cDeviceResourceManagerAdapters {
    // 0x17ad50's work on `memory` (0x3c bytes): placement construction.
    static cDeviceResourceManagerDX *__fastcall Construct(void *memory, void *, cIDeviceDX *device);
};

// The iterator over the manager's set (vftable 0xd9fa20; 0xc bytes from the fixed pool 0xfedb58).
class cDeviceResourceIterator : public cIDeviceResourceIterator {
public:
    static constexpr uint32_t kSize = 0xc;
    static constexpr uint32_t kPool = 0xfedb58; // the cRZFastFixedAllocator2<0xc> instance the original uses
    static rz::cRZFastFixedAllocator2<0xc> &Pool() { return ts2_rva_fedb58; }

    // Count 0, at `position` (a node of the set, or its end).
    explicit cDeviceResourceIterator(cDeviceResourceSet::const_iterator position);

    static void *operator new(size_t size);
    static void operator delete(void *block, size_t size);

    bool QueryInterface(uint32_t riid, void **ppvObj) override; // slot 0 (shared 0x178b20): only 1; null out: false
    uint32_t AddRef() override;                                 // slot 1 (shared 0x148fb0)
    uint32_t Release() override;                                // slot 2 (shared 0x178b60): slot 8 at 0
    cIDeviceResourceIterator *Clone() override;                 // slot 3 (0x17b620)
    bool IsEqual(const cIDeviceResourceIterator *other) override; // slot 4 (shared 0x17b680): the positions
    cIDeviceResource *Get() override;                           // slot 5 (shared 0x17b670)
    void Next() override;                                       // slot 6 (shared 0x17b700)
    void Prev() override;                                       // slot 7 (shared 0x17b760)
    ~cDeviceResourceIterator() override;                        // slot 8 (0x17aed0)

private:
    uint32_t mRefCount;                       // +0x04
    cDeviceResourceSet::const_iterator mPosition; // +0x08
};
static_assert(sizeof(cDeviceResourceIterator) == 0xc, "cDeviceResourceIterator: 0xc bytes");

} // namespace nGZGraphic4
