#pragma once
// nGZSceneGraph::cLevelInfo as a whole class (lane SX, scenegraph M4b): one mip level of an image (cImageData's levels,
// cTSTextureRecipeImageData's), a scene-graph resource of its own (class 0xed534136 of the scene graph's COM director,
// record "cLevelInfo" version 9). Standalone shape (docs/standalone-shape.md), on our cSGResource:
//
//   class cLevelInfo : public cSGResource
//
//   +0x00 the cSGResource part (vtable 0xe76444, 17 slots: QI 0xab2380, the cGZPersistResource key slots, the deleting
//         destructor 0xab01a0 at 12, then this class's own 13-16); vbptr +0x1c, name +0x20, segment +0x24, flags +0x28
//   +0x2c mWidth, +0x30 mHeight     the level's size in pixels
//   +0x34 mPitch                    bytes per row (per row of 4x4 blocks for the DXT formats)
//   +0x38 mByteCount                the level's byte size
//   +0x3c mFlag                     slots 15 / 16
//   +0x40 mPixels                   the level's bytes (LevelPixels: VirtualAlloc'd from 0x1000 bytes up)
//   +0x4c the vtordisp, +0x50 the virtual base cISGResource (vtable 0xe7648c, 25 slots; 18 / 19 answer "_lifo" / "6li")
//
// The switch: the director's factory 0xab15f0 is hooked to Create, and both constructors (0xaaff70 default, 0xaafe10
// (width, height, format)) to our compiler's (thiscall with the most-derived flag last on the stack: their callers in
// cImageData and cTSTextureRecipeImageData build levels on the stack and on the heap with flag 1). The vtables 0xe76444 /
// 0xe7648c are referenced by those two constructors alone, so no game object carries them and every slot is ours through
// them. The copy 0xab0120 (called by cTSTextureRecipeImageData) is hooked to Assign. The pixel buffer's resize 0xab41b0 is
// called by Assign and Read alone: in effect through them.
#include "sg/cSGResource.h"

#include <cstddef>
#include <cstdint>

namespace nGZSceneGraph {

// The pixel buffer at +0x40 (12 bytes): a block from operator new[] below mThreshold bytes, else from VirtualAlloc
// (reserve and commit, top-down, read-write), freed the matching way.
struct LevelPixels {
    static constexpr uint32_t kThreshold = 0x1000;

    uint32_t mThreshold = kThreshold; // +0
    uint8_t *mData = nullptr;         // +4
    uint32_t mSize = 0;               // +8

    LevelPixels() = default;
    ~LevelPixels() { Release(); }
    LevelPixels(const LevelPixels &) = delete;
    LevelPixels &operator=(const LevelPixels &) = delete;

    // 0xab41b0 (thiscall, ret 4): nothing when the size is already n; else the old block goes (whatever its size, even
    // none: delete[] of null below the threshold) and a new one of n bytes comes (none for 0). The bytes are not kept.
    void Resize(uint32_t n);
    // The destructor's part (inlined in the game's): the block goes when there is one.
    void Release();
    // The block's free, by the size it was made with.
    void FreeBlock();
};
static_assert(sizeof(LevelPixels) == 12, "LevelPixels layout");

class cLevelInfo : public cSGResource {
public:
    static constexpr size_t kSize = 0x54;
    static constexpr uint32_t kClassId = 0xed534136; // the director's class id, and the type-info record's word
    static constexpr uint32_t kVersion = 9;          // the type-info record's version
    static constexpr uint32_t kIidSelf = 0x0d54228e; // QueryInterface gives the cSGResource part (the object), counted

    // The pixel formats the sizing constructor knows (the image's format word): bytes per pixel, or DXT blocks.
    enum Format : int32_t {
        kFormatRgba32 = 1, kFormatRgb24 = 2, kFormatByte = 3, kFormatDxt1 = 4, kFormatDxt3 = 5, kFormatByteB = 6,
        kFormatWord = 7, kFormatDxt5 = 8, kFormatRgb24B = 9,
    };

    cLevelInfo();                                       // 0xaaff70
    cLevelInfo(int32_t width, int32_t height, int32_t format); // 0xaafe10: pitch and byte count from the format
    ~cLevelInfo() override;                             // deleting 0xab01a0 (the pixels, then the cSGResource part)

    // The director's factory (0xab15f0, __cdecl): a new level, uncounted; the virtual base.
    static cISGResource *__cdecl Create();

    // ---- cIGZUnknown ----
    bool QueryInterface(uint32_t iid, void **out) override; // 0xab2380
    uint32_t AddRef() override;
    uint32_t Release() override;

    // ---- cIGZPersistSerializableReferent ----
    void Read(ifaces::cIGZPersistReader *reader) override;  // 0xab2cf0
    cPersistTypeInfo GetTypeInfo() override;                // 0xab3a30: {"cLevelInfo", 0xed534136, 9}
    void Write(ifaces::cIGZPersistWriter *writer) override; // 0xab3f10
    uint32_t GetOwnerInfo() override;                       // the cSGResource part's (0xa3c750)

    // ---- cISGResource (the virtual base) ----
    const void *GetRecord18() override; // 0xab1c10: "_lifo"
    const void *GetRecord19() override; // 0xab1bf0: "6li"

    // ---- this class's own slots (13-16 of the primary vtable) ----
    virtual cISGResource *GetResourceConst();  // 13 (0xab0350, both): the virtual base, null for null
    virtual cISGResource *GetResource();       // 14
    virtual void SetFlag15(uint8_t value);     // 15 (0x677f80, folded)
    virtual uint8_t GetFlag16();               // 16 (shared 0x78e3d0)

    // 0xab0120 (thiscall, ret 4; returns this): the size words, the flag and a copy of the pixels; not the resource part.
    cLevelInfo *Assign(const cLevelInfo &other);

    cISGResource *Face() { return static_cast<cISGResource *>(this); }

    int32_t mWidth = 0;       // +0x2c
    int32_t mHeight = 0;      // +0x30
    int32_t mPitch = 0;       // +0x34
    uint32_t mByteCount = 0;  // +0x38
    uint8_t mFlag = 0;        // +0x3c
    LevelPixels mPixels;      // +0x40
};

// The hooks' entry points (src/hook/tables/cLevelInfo.cpp).
struct cLevelInfoAdapters {
    static cLevelInfo *__fastcall Assign(cLevelInfo *self, void *, const cLevelInfo *other); // 0xab0120
};

} // namespace nGZSceneGraph

// The constructors' hooks: their code (C names the linker resolves to the decorated ones, /alternatename in
// cLevelInfo.cpp), as cSGResource's (sg/cSGResource.h). Code addresses, never data.
extern "C" const uint8_t cLevelInfo_Constructor;       // ??0cLevelInfo@nGZSceneGraph@@QAE@XZ
extern "C" const uint8_t cLevelInfo_SizedConstructor;  // ??0cLevelInfo@nGZSceneGraph@@QAE@HHH@Z
