#pragma once
// nGZSceneGraph::cImageData as a whole class (lane SZ, scenegraph M4b part 2): the scene graph's image (record
// "cImageData" version 9, class 0x1c4a276c of the scene graph's COM director): one or more entries, each a chain of mip
// levels (cLevelInfo, counted) with a colour and a float. Standalone shape (docs/standalone-shape.md), on our cSGResource:
//
//   class cImageData : public cIImageData, public cSGResource
//
//   +0x00 the cIImageData face (vtable 0xe764fc, 41 slots)
//   +0x04 the cSGResource part (vtable 0xe765a4, 13 slots; the deleting destructor 0xab0180 at 12); vbptr +0x20
//   +0x30 mFormat     the pixel format of every level (cLevelInfo::Format)
//   +0x34 mScale      a float, 1.0 by default (slots 10 / 34)
//   +0x38 mEntries    std::vector<ImageEntry> (40-byte entries)
//   +0x44 mCurrent    the entry the per-entry slots address (slot 7 sets it; never checked against the count)
//   +0x48 mName       a cRZString (the missing-texture name a failed read leaves; slots 13 / 37)
//   +0x68 the vtordisp, +0x6c the virtual base cISGResource (vtable 0xe765dc, 25 slots; 18 / 19 answer "_txtr" / "6tx")
//
// The switch: the director's factory 0xab1590 is hooked to Create and the constructor 0xaafd60 to our compiler's (thiscall,
// the most-derived flag on the stack: cTSTextureRecipeImageData's constructor 0xcc3e10 builds its base part with flag 0),
// the destructor 0xab0070 to an adapter (the derived destructor 0xcc4020 and an unwind funclet call it). No game object
// then carries the class's own vtables; cTSTextureRecipeImageData's vtables still name many of the slot bodies, so those
// keep their hooks (adapters below). The cdecl helpers 0xab1960 (an entry's level chain) and 0xab0250 (a chain's bytes)
// are hooked to ours too.
//
// Every slot is ours. Slots 14, 16, 38 and 40 have bodies cTSTextureRecipeImageData's vtables name (adapters, hooks); the
// others are reached only through our vtable (in effect through callers).
#include "rz/cRZString.h"
#include "sg/cLevelInfo.h"
#include "sg/cSGResource.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace nGZSceneGraph {

// A counted level (the element of an entry's deque): AddRef / Release through the level's virtual base, null allowed.
class LevelRef {
public:
    LevelRef() = default;
    explicit LevelRef(cLevelInfo *level) : mLevel(level) { Hold(mLevel); }
    LevelRef(const LevelRef &other) : mLevel(other.mLevel) { Hold(mLevel); }
    LevelRef &operator=(const LevelRef &other);
    ~LevelRef() { Drop(mLevel); }

    cLevelInfo *Get() const { return mLevel; }
    cLevelInfo *operator->() const { return mLevel; }
    // The slot takes the new level (counted), lets the old one go; nothing when they are the same.
    void Reset(cLevelInfo *level);

    static void Hold(cLevelInfo *level)
    {
        if (level) level->Face()->AddRef();
    }
    static void Drop(cLevelInfo *level)
    {
        if (level) level->Face()->Release();
    }

    cLevelInfo *mLevel = nullptr;
};
static_assert(sizeof(LevelRef) == 4, "LevelRef");

// One entry (40 bytes): the levels, largest first; the colour (1, 1, 1, 1 by default); a float (10 by default).
struct ImageEntry {
    static constexpr float kDefaultScale = 10.0f;

    ImageEntry();
    std::deque<LevelRef> mLevels; // +0x00 (proxy, map, map size, offset, size)
    float mColour[4];             // +0x14
    float mScale;                 // +0x24
};
static_assert(sizeof(ImageEntry) == 40, "ImageEntry");

// nGZSceneGraph::cIImageData (interface id 0xfc49f6ee): the image's face. Names after the bodies (provisional).
class cIImageData : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xfc49f6ee;

    virtual void Clone(cIImageData **out) = 0;                                  // 3
    virtual cISGResource *GetResourceConst() = 0;                               // 4
    virtual cISGResource *GetResource() = 0;                                    // 5
    virtual void Init(int32_t width, int32_t height, uint32_t levels, int32_t format, float scale, uint32_t entries) = 0; // 6
    virtual void SetCurrentEntry(uint32_t index) = 0;                           // 7
    virtual uint8_t *LevelDataConst(uint32_t level) = 0;                        // 8
    virtual uint8_t *LevelData(uint32_t level) = 0;                             // 9
    virtual void SetScale(float value) = 0;                                     // 10
    virtual void SetEntryColour(const float *colour) = 0;                       // 11
    virtual void SetEntryScale(float value) = 0;                                // 12
    virtual void SetName(const char *text) = 0;                                 // 13
    virtual void RegenerateMips() = 0;                                          // 14
    virtual void DropTopLevels(uint32_t n) = 0;                                 // 15
    virtual bool SetFormat(int32_t format) = 0;                                 // 16
    virtual void FillOpaque(const float *colour, int32_t level) = 0;            // 17
    virtual void Fill(const float *colour, int32_t level) = 0;                  // 18
    virtual void SetPixelOpaque(int32_t x, int32_t y, const float *colour, int32_t level) = 0; // 19
    virtual void SetPixel(int32_t x, int32_t y, const float *colour, int32_t level) = 0;       // 20
    virtual void SetPixelChannel(int32_t x, int32_t y, float value, int32_t level) = 0;        // 21
    virtual cISGResource *LevelResource(uint32_t level) = 0;                    // 22: the level's slot 14
    virtual void SetLevelFlag(uint32_t level, uint8_t value) = 0;               // 23: the level's slot 15
    virtual uint8_t HasPixels() = 0;                                            // 24
    virtual int32_t EntryCount() = 0;                                           // 25
    virtual int32_t LevelWidth(uint32_t level) = 0;                             // 26
    virtual int32_t LevelHeight(uint32_t level) = 0;                            // 27
    virtual int32_t LevelPitch(uint32_t level) = 0;                             // 28
    virtual uint32_t TotalBytes() = 0;                                          // 29
    virtual uint32_t LevelBytes(uint32_t level) = 0;                            // 30
    virtual uint32_t LevelCount() = 0;                                          // 31
    virtual int32_t GetFormat() = 0;                                            // 32
    virtual void *PixelFormat() = 0;                                            // 33: the format object
    virtual float GetScale() = 0;                                               // 34
    virtual float *GetEntryColour(float *out) = 0;                                // 35
    virtual float GetEntryScale() = 0;                                          // 36
    virtual const char *GetNameText() = 0;                                      // 37
    virtual float *GetPixelRGB(float *out, int32_t x, int32_t y, int32_t level) = 0;             // 38
    virtual float *GetPixel(float *out, int32_t x, int32_t y, int32_t level) = 0;              // 39
    virtual float GetPixelChannel(int32_t x, int32_t y, int32_t level) = 0;                    // 40
};

class cImageData : public cIImageData, public cSGResource {
public:
    static constexpr size_t kSize = 0x70;
    static constexpr uint32_t kClassId = 0x1c4a276c; // the director's class id, and the type-info record's word
    static constexpr uint32_t kVersion = 9;          // the type-info record's version
    static constexpr int kFaceOffset = 0x6c;         // the virtual base's static distance (Read / Write bodies' this)

    cImageData();            // 0xaafd60
    ~cImageData() override;  // 0xab0070; deleting 0xab0180

    // The director's factory (0xab1590, __cdecl): a new image, uncounted; the cIImageData face.
    static cIImageData *__cdecl Create();

    // 0xab1960 (__cdecl): the entry's levels made again: emptied, then a level of width x height in the format (its pixels
    // sized to its byte count) and the halved ones, until `levels` are made, a size is not a power of two or both are
    // below 2. The first level is made whatever the size and the count.
    static void __cdecl BuildLevels(ImageEntry *entry, int32_t width, int32_t height, uint32_t levels, int32_t format);
    // 0xab0250 (__cdecl): the bytes of up to `levels` levels of the chain from width x height (0 for none), stopping as
    // BuildLevels does (unsigned here); *width / *height are left halved past the levels counted.
    static uint32_t __cdecl MipChainBytes(uint32_t levels, int32_t format, uint32_t *width, uint32_t *height);

    // ---- cIGZUnknown ----
    bool QueryInterface(uint32_t iid, void **out) override; // 0xab2330
    uint32_t AddRef() override;                             // 0xab0230 (jmp to the base's)
    uint32_t Release() override;                            // 0xab32b0 (jmp to the base's)

    // ---- cIImageData ----
    void Clone(cIImageData **out) override;                 // 0xab1390
    cISGResource *GetResourceConst() override;              // 0xab0330 (both)
    cISGResource *GetResource() override;
    void Init(int32_t width, int32_t height, uint32_t levels, int32_t format, float scale, uint32_t entries) override; // 0xab1890
    void SetCurrentEntry(uint32_t index) override;          // shared 0x3326e0
    uint8_t *LevelDataConst(uint32_t level) override;       // 0xab2030 (both)
    uint8_t *LevelData(uint32_t level) override;
    void SetScale(float value) override;                    // shared 0x550760
    void SetEntryColour(const float *colour) override;      // 0xab3950
    void SetEntryScale(float value) override;               // 0xab3340
    void SetName(const char *text) override;                // 0xab3360
    void RegenerateMips() override;                         // 0xab1680
    void DropTopLevels(uint32_t n) override;                // 0xab14d0
    bool SetFormat(int32_t format) override;                // 0xab03c0
    void FillOpaque(const float *colour, int32_t level) override; // 0xab0e50
    void Fill(const float *colour, int32_t level) override; // 0xab08b0
    void SetPixelOpaque(int32_t x, int32_t y, const float *colour, int32_t level) override; // 0xab36d0
    void SetPixel(int32_t x, int32_t y, const float *colour, int32_t level) override;       // 0xab3390
    void SetPixelChannel(int32_t x, int32_t y, float value, int32_t level) override;        // 0xab3530
    cISGResource *LevelResource(uint32_t level) override;   // 0xab0370
    void SetLevelFlag(uint32_t level, uint8_t value) override; // 0xab3300
    uint8_t HasPixels() override;                           // 0xab1b40
    int32_t EntryCount() override;                          // 0xab1c40
    int32_t LevelWidth(uint32_t level) override;            // 0xab3ae0
    int32_t LevelHeight(uint32_t level) override;           // 0xab1830
    int32_t LevelPitch(uint32_t level) override;            // 0xab1c60
    uint32_t TotalBytes() override;                         // 0xab1460
    uint32_t LevelBytes(uint32_t level) override;           // 0xab1b90
    uint32_t LevelCount() override;                         // 0xab1c20
    int32_t GetFormat() override;                           // shared 0x1457d0
    void *PixelFormat() override;                           // 0xab2070
    float GetScale() override;                              // shared 0x54fc20
    float *GetEntryColour(float *out) override;               // 0xab32d0
    float GetEntryScale() override;                         // 0xab03b0
    const char *GetNameText() override;                     // 0xab1870
    float *GetPixelRGB(float *out, int32_t x, int32_t y, int32_t level) override;             // 0xab2150
    float *GetPixel(float *out, int32_t x, int32_t y, int32_t level) override;              // 0xab1ca0
    float GetPixelChannel(int32_t x, int32_t y, int32_t level) override;                    // 0xab1ec0

    // ---- cIGZPersistSerializableReferent ----
    void Read(ifaces::cIGZPersistReader *reader) override;  // 0xab23d0
    cPersistTypeInfo GetTypeInfo() override;                // 0xab3980: {"cImageData", 0x1c4a276c, 9}
    void Write(ifaces::cIGZPersistWriter *writer) override; // 0xab3b20
    uint32_t GetOwnerInfo() override;                       // the cSGResource part's (0xa3c750)

    // ---- cISGResource (the virtual base) ----
    const void *GetRecord18() override; // 0xab1c00: "_txtr"
    const void *GetRecord19() override; // 0xab1be0: "6tx"

    // 0xab2da0 (thiscall, ret 0x18): one entry's levels from the stream. Before version 9 one block of every level's bytes
    // (after `skipBytes` skipped); from 9 a count of records, each a level's bytes or a resource name (the level replaced by
    // the resource the manager finds; none found: the name kept in `missingName` and false).
    bool ReadLevels(ifaces::cIGZPersistReader *reader, const cPersistTypeInfo *info, uint32_t skipBytes, void *manager,
                    ImageEntry *entry, cRZString *missingName);

    cISGResource *Face() { return static_cast<cISGResource *>(this); }
    ImageEntry &Current() { return mEntries[mCurrent]; }
    cLevelInfo *CurrentLevel(uint32_t level) { return Current().mLevels[level].Get(); }
    bool NoEntries() const;

    int32_t mFormat = 0;                // +0x30
    float mScale = 1.0f;                // +0x34
    std::vector<ImageEntry> mEntries;   // +0x38
    uint32_t mCurrent = 0;              // +0x44
    union {                             // +0x48: constructed and destroyed by hand (no default constructor)
        cRZString mName;
    };
};

// The hooks' entry points (src/hook/tables/cImageData.cpp): the bodies cTSTextureRecipeImageData's vtables name, the ones
// the game calls directly, and the verified slot hooks kept. Qualified calls, bodies inlined. `this` is the +0 face for the
// cIImageData slots and the virtual base at its static distance (+0x6c) for Read / Write / GetTypeInfo.
struct cImageDataAdapters {
    using Self = cImageData;
    static Self *FromFace(void *face) { return reinterpret_cast<Self *>(static_cast<uint8_t *>(face) - Self::kFaceOffset); }

    static void __fastcall Destruct(Self *self, void *);                                         // 0xab0070
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t iid, void **out);         // 0xab2330
    static void __fastcall Clone(Self *self, void *, cIImageData **out);                         // 0xab1390
    static cISGResource *__fastcall GetResource(Self *self, void *);                             // 0xab0330
    static void __fastcall Init(Self *self, void *, int32_t width, int32_t height, uint32_t levels, int32_t format,
                                float scale, uint32_t entries);                                  // 0xab1890
    static uint8_t *__fastcall LevelData(Self *self, void *, uint32_t level);                    // 0xab2030
    static void __fastcall SetEntryColour(Self *self, void *, const float *colour);              // 0xab3950
    static void __fastcall SetEntryScale(Self *self, void *, float value);                       // 0xab3340
    static void __fastcall SetName(Self *self, void *, const char *text);                        // 0xab3360
    static void __fastcall DropTopLevels(Self *self, void *, uint32_t n);                        // 0xab14d0
    static void __fastcall RegenerateMips(Self *self, void *);                                   // 0xab1680
    static bool __fastcall SetFormat(Self *self, void *, int32_t format);                        // 0xab03c0
    static float *__fastcall GetPixelRGB(Self *self, void *, float *out, int32_t x, int32_t y, int32_t level); // 0xab2150
    static float __fastcall GetPixelChannel(Self *self, void *, int32_t x, int32_t y, int32_t level);        // 0xab1ec0
    static cISGResource *__fastcall LevelResource(Self *self, void *, uint32_t level);           // 0xab0370
    static void __fastcall SetLevelFlag(Self *self, void *, uint32_t level, uint8_t value);      // 0xab3300
    static uint8_t __fastcall HasPixels(Self *self, void *);                                     // 0xab1b40
    static int32_t __fastcall EntryCount(Self *self, void *);                                    // 0xab1c40
    static int32_t __fastcall LevelWidth(Self *self, void *, uint32_t level);                    // 0xab3ae0
    static int32_t __fastcall LevelHeight(Self *self, void *, uint32_t level);                   // 0xab1830
    static int32_t __fastcall LevelPitch(Self *self, void *, uint32_t level);                    // 0xab1c60
    static uint32_t __fastcall TotalBytes(Self *self, void *);                                   // 0xab1460
    static uint32_t __fastcall LevelBytes(Self *self, void *, uint32_t level);                   // 0xab1b90
    static uint32_t __fastcall LevelCount(Self *self, void *);                                   // 0xab1c20
    static void *__fastcall PixelFormat(Self *self, void *);                                     // 0xab2070
    static float *__fastcall GetEntryColour(Self *self, void *, float *out);                       // 0xab32d0
    static float __fastcall GetEntryScale(Self *self, void *);                                   // 0xab03b0
    static const char *__fastcall GetNameText(Self *self, void *);                               // 0xab1870
    static void __fastcall Read(void *face, void *, ifaces::cIGZPersistReader *reader);          // 0xab23d0
    static cPersistTypeInfo *__fastcall GetTypeInfo(void *face, void *, cPersistTypeInfo *out);  // 0xab3980
    static void __fastcall Write(void *face, void *, ifaces::cIGZPersistWriter *writer);         // 0xab3b20
    static bool __fastcall ReadLevels(Self *self, void *, ifaces::cIGZPersistReader *reader, const cPersistTypeInfo *info,
                                      uint32_t skipBytes, void *manager, ImageEntry *entry, cRZString *missingName); // 0xab2da0
};

// The virtual base's thunks cTSTextureRecipeImageData's vtable shares (`this` is the virtual base; the displacement to
// the object is the dword at this - 4). Hook-shaped, verified before the class (kept).
class cImageDataSubobject {
public:
    // vf108_6 (11 bytes, 0xab32f3: sub ecx,[ecx-4]; sub ecx,0x38; jmp 0xa3c750): cSGSecondary::VirtualBaseIfObject.
    void *VirtualBaseIfObject();
    // vf108_18 (14 bytes, 0xab1bf6 -> 0xab1c00): the record "_txtr" in the image (RVA 0xdb0818); the displacement is
    // still read, so a null `this` faults as the original does.
    void *ConstantSlot18();
    // vf108_19 (14 bytes, 0xab1bd4 -> 0xab1be0): the record "6tx" (RVA 0xda6450).
    void *ConstantSlot19();
};

} // namespace nGZSceneGraph

// The constructor's hook: our compiler's constructor (C name the linker resolves to the decorated one, /alternatename in
// cImageData.cpp). A code address, never data.
extern "C" const uint8_t cImageData_Constructor; // ??0cImageData@nGZSceneGraph@@QAE@XZ
