#pragma once
// nGZGraphic4::cDraw2D: the Direct3D 2D renderer (1.18.0.187; docs/subsystems/rendering.md, R5b). It is the draw
// system's hardware engine (cGZDrawContextRenderToHardware at +8: the draw context's primitives as batches of screen
// vertices), the factory that makes that engine for a back buffer (+0x100), and a direct 2D interface of its own
// (cIDraw2D, the primary face: clip stack, materials drawing lines, rectangles, triangles and quads, image records
// packed into texture atlases). cDraw2DSystemService (below) owns the one instance, ticks it and registers its factory
// with the draw system.
//
// Standalone shape (docs/standalone-shape.md): one interface per RTTI face in the original order, every slot an
// override. cDraw2D is made only by cDraw2DSystemService::Init (0x150210, ours), and the service only by its creator
// 0x14f2b0 (the COM director's class 0x4c9282f8), which is hooked: that hook is the switch that puts our vtables on
// every cDraw2D and every service, so the slots, constructor and destructor are in effect through it. The image
// records and their texture atlases (lane OL; the packer is gz/cTextureAtlas.h, the buffer cache gz/
// cDraw2DBufferHardwareCache.h) are ours too: slots 25, 26, 29, 30, 31 and the engine's 30, the record allocator, the
// placement and the release of a record's cells, and the pixel copy into the atlas textures (UploadRecord 0x152410 and
// UploadKeyed32 0x14dfa0, lane OZ). CreateDeviceObjects 0x150330 is ours too (lane OU).
//
// Device objects (CreateDeviceObjects 0x150330 makes them when the device is ready or comes back; ReleaseDeviceObjects
// lets them go when it goes): the device and its state cache, state blocks (a default one, four blend modes, the
// engine's), a vertex format, offsets of the position, colour and texture coordinates in the 20-byte vertex, a vertex
// shader and declaration, a dynamic vertex and index array.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "gz/cAbstractBehaviorTypeST.h"
#include "gz/cGZDrawContextRenderToHardware.h"
#include "gz/cIGZDrawContextEngine.h"
#include "gz/cIGZSystemService.h"
#include "gz/cShaderProgramsDX.h"
#include "gz/cTextureAtlas.h"
#include "gz/cVertexFormat.h"

namespace nGZGraphic4 {

class cIDeviceDX;
class cIDeviceState;
class cIDynamicVertexArray;
class cIDraw2D;

// What cIDraw2D draws with (SetMaterial): slot 3 the state block it applies (0: the default one), slot 4 the shape.
class cIDraw2DMaterial : public cIGZUnknown {
public:
    virtual uint32_t GetStateBlock(cIDraw2D *draw2d, cIDeviceDX *device) = 0; // 3
    // 4: `count` points (x, y floats) and one value per point (0, 1, 2, 3 around a shape; 0, 1, 1, 0 along a line);
    // kind 0 a filled shape, 1 a line.
    virtual void Draw(cIDraw2D *draw2d, uint32_t kind, uint32_t count, const float *points, const float *values) = 0;
};

// A clip on the stack (0x1c bytes): the origin drawing is relative to, the clip rectangle (target pixels) and a depth.
struct cDraw2DClip {
    int32_t originX, originY;
    cGZRect rect;
    float depth;
};
static_assert(sizeof(cDraw2DClip) == 0x1c, "cDraw2DClip: 0x1c bytes");

struct cDraw2DPoint {
    int32_t x, y;
};

// GetRecordInfo's answer: an image record's size and its buffer's pixel format (0x18a7f0's).
struct cDraw2DRecordInfo {
    uint32_t width, height;
    void *pixelFormat;
};

// nGZGraphic4::cIDraw2D (slots 15-42 after the behaviour's; the template's deleting destructor is 43).
class cIDraw2D : public nGZEvent::cIAbstractBehavior {
public:
    static constexpr uint32_t kIid = 0xba25716f;

    virtual void SetMaterial(cIDraw2DMaterial *material) = 0;                                  // 15
    virtual void PushClipRect(const cGZRect *rect, bool relative, float depth) = 0;            // 16: at (0, 0)
    virtual void PushClipOrigin(int32_t x, int32_t y, bool relative, float depth) = 0;         // 17: no limit
    // 18: a clip of `rect` moved by (x, y), relative to the current clip (its origin and rectangle) or to the target.
    virtual void PushClip(int32_t x, int32_t y, const cGZRect *rect, bool relative, float depth) = 0;
    virtual void PopClip() = 0;                                                                // 19
    virtual void DrawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, float width) = 0;    // 20
    virtual void FillRect(int32_t x, int32_t y, int32_t width, int32_t height) = 0;            // 21
    virtual void FillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2) = 0; // 22
    virtual void FillQuad(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3,
                          int32_t y3) = 0;                                                     // 23
    virtual void SetBlendMode(uint32_t mode, float alpha) = 0;                                 // 24: no material
    // 25: the part `source` of the record's image, stretched to `destination` (both in image / target pixels), drawn a
    // quad per atlas cell under the clip; nothing when the clip is empty or the record -1.
    virtual void DrawImage(const cGZRect *destination, int32_t record, const cGZRect *source) = 0;
    virtual void DrawRecord(const cGZRect *destination, int32_t record) = 0;                   // 26: the whole image
    virtual void DrawImageAt(int32_t x, int32_t y, int32_t record, const cGZRect *source) = 0; // 27: slot 25
    virtual void DrawRecordAt(int32_t x, int32_t y, int32_t record) = 0;                       // 28: slot 26
    // 29: a record for a width x height image; c is the colour depth class (0 alpha only, below 0x18 16 bits, else 32),
    // d the pixel flavour. Its buffer is made and its cells placed. The index, or -1 (no buffer or no room).
    virtual int32_t CreateRecord(uint32_t width, uint32_t height, uint32_t c, uint32_t d) = 0;
    // 30: record `record`'s pixels from a memory image (bits, format, width, height, pitch), copied into the record's
    // buffer and uploaded; nothing for the record -1. `key` (not null) is a word stored at the record's +0x1c.
    virtual void SetRecordPixels(int32_t record, void *bits, void *format, uint32_t width, uint32_t height,
                                 uint32_t pitch, const uint32_t *key) = 0;
    virtual void RebuildAtlases() = 0;                                                         // 31
    virtual void Tick() = 0;                                                                   // 32: 31 when due
    virtual void AddRefRecord(int32_t record) = 0;                                             // 33
    virtual void ReleaseRecord(int32_t record) = 0;                                            // 34
    virtual void ApplyDrawState() = 0;                                                         // 35
    // 36: room for `vertices` 20-byte vertices and `indices` 16-bit indices; the four write cursors.
    virtual bool LockBatch(uint32_t vertices, uint32_t indices, float **positions, uint32_t **colours, float **uvs,
                           uint16_t **indexOut) = 0;
    virtual void DrawBatch() = 0;                                                              // 37
    virtual int32_t GetClipDepth() = 0;                                                        // 38: clips on the stack
    virtual cDraw2DPoint GetClipOrigin() = 0;                                                  // 39
    virtual cGZRect GetClipRect() = 0;                                                         // 40
    virtual cIDraw2DMaterial *GetMaterial() = 0;                                               // 41 (folded 0x151b40)
    virtual cDraw2DRecordInfo GetRecordInfo(int32_t record) = 0;                               // 42
};

// The behaviour template's cells (cAbstractBehaviorTypeT<cIDraw2D, cRefSingleThreaded>).
struct cDraw2DBehaviorTraits {
    static constexpr uint32_t kEventManagerCell = 0x10c95ec; // 0x14e670's cache
    // The registry (the template's slots 12-14 read it): RegistryFor(0, ...) without adding a record.
    static nGZEvent::cEventAttributeMap *Registry();
    // 0x1516f0 (cdecl; lane OZ): the registry, built on first use, and `attribute` added under `id` when id is not 0
    // (replacing the record there). First use: the base events registered by the shared 0x1464f0, a copy of the base
    // registry (0x1480d0) and the device event 0xca7ccbbc ("CurrentDeviceEvent") added. Our own map (the original's
    // is the game global 0x10c95e0, read by nothing else; the original's unused 3-byte allocation is not made).
    __declspec(noinline) static nGZEvent::cEventAttributeMap *__cdecl RegistryFor(uint32_t id, const nGZEvent::cEventAttribute *attribute); // not inlined into Registry: tests stand in for it there
    // Tests only: forgets the registry, so the next use builds it again.
    static void ResetRegistryForTest();
};

class cDraw2D : public nGZEvent::cAbstractBehaviorTypeST<cIDraw2D, cDraw2DBehaviorTraits>,
                public cGZDrawContextRenderToHardware,
                public cIGZDrawContextEngineFactory {
    using Behavior = nGZEvent::cAbstractBehaviorTypeST<cIDraw2D, cDraw2DBehaviorTraits>;

public:
    static constexpr uint32_t kSize = 0x1c4;
    static constexpr uint32_t kGraphicSystemId = 0x85522803;  // cGraphicSystemDX's service and the face asked for
    static constexpr uint32_t kGraphicSystemIid = 0xa55227bb;
    static constexpr uint32_t kDeviceStateEvent = 0xca7ccbbc; // cGraphicSystemDX's: the current device's state
    static constexpr uint32_t kDrawSystemCell = 0x10c95dc;    // the draw system, cached (0x14f330's)
    static constexpr uint32_t kBatchVertices = 0xccc;         // a batch's most vertices (LockBatch's limit)
    static constexpr uint32_t kBatchIndices = 0x8000;
    static constexpr uint32_t kIidBehavior2 = 0x3b253dc7;    // counted (every behaviour type answers it)
    static constexpr uint32_t kIidBehaviorType = 0xfb859324; // the object itself, not counted

    // A counted pointer: released when replaced or dropped.
    template <class T> struct Ref {
        T *p = nullptr;
        Ref() = default;
        Ref(const Ref &) = delete;
        Ref &operator=(const Ref &) = delete;
        ~Ref()
        {
            if (p) p->Release();
        }
        // Cleared first, then released (ReleaseDeviceObjects).
        void Reset()
        {
            if (T *const old = p) {
                p = nullptr;
                old->Release();
            }
        }
        // The new one counted, stored, the old one released (nothing when they are the same).
        void Assign(T *object)
        {
            T *const old = p;
            if (object == old) return;
            if (object) object->AddRef();
            p = object;
            if (old) old->Release();
        }
        uint32_t Word() const { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
    };

    // The textures of a pool, counted: released (each one there) when the pool goes (0x14c060).
    struct TextureList {
        std::vector<cIGZUnknown *> items;
        TextureList() = default;
        TextureList(const TextureList &) = delete;
        TextureList &operator=(const TextureList &) = delete;
        ~TextureList()
        {
            for (cIGZUnknown *texture : items)
                if (texture) texture->Release();
        }
    };
    // An atlas pool (0x18 bytes): its atlases (the packer's trees) and one texture per atlas. Pool 0 is unused, 1 holds
    // the 16-bit textures and 2 the 32-bit ones.
    struct Pool {
        std::vector<cTextureAtlas> atlases; // +0
        TextureList textures;               // +0xc
    };
    // Where a record's cell sits: atlas and region (a node of the atlas's tree).
    struct CellRef {
        uint32_t atlas, region;
    };
    static constexpr uint32_t kUnplaced = 3; // Record::pool of a record without cells
    // An image record (0x3c bytes): counted, its size, the buffer it is copied from, its cells in a pool (columns x rows,
    // row by row). A new one is unplaced and empty (the original leaves the border bytes as the stack had them).
    struct Record {
        int32_t refCount = 0;          // +0
        uint32_t width = 0, height = 0; // +4
        uint32_t c = 0, d = 0;         // +0xc (CreateRecord's)
        uint8_t bufferUncounted = 0;   // +0x14: the buffer is not released with the record
        uint8_t pad15[3] = {};
        cIGZBuffer *buffer = nullptr;  // +0x18
        uint32_t word1c = 0;           // +0x1c
        uint32_t columns = 0, rows = 0; // +0x20
        uint32_t pool = kUnplaced;     // +0x28
        std::vector<CellRef> cells;    // +0x2c, columns * rows
        uint8_t borderX = 0, borderY = 0; // +0x38: each cell's border texels (2 per side where the texture is smaller)
        uint8_t pad3a[2] = {};
    };

    cDraw2D();           // 0x14ba80
    ~cDraw2D() override; // 0x14c1a0 (deleting: slot 43 0x14c680, the engine's slot 24 0x14c5a8)

    // ---- cIGZUnknown (all three faces) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0x151390
    uint32_t AddRef() override { return Behavior::AddRef(); }   // shared 0x14c930
    uint32_t Release() override { return Behavior::Release(); } // 0x1518a0

    // ---- nGZEvent::cIAbstractBehaviorType ----
    void OnEvent(uint32_t type, uint32_t arg1, void *data, uint32_t arg3) override; // 0x1500e0

    // ---- cIDraw2D ----
    void SetMaterial(cIDraw2DMaterial *material) override;                                   // 0x151ad0
    void PushClipRect(const cGZRect *rect, bool relative, float depth) override;             // 0x1511c0
    void PushClipOrigin(int32_t x, int32_t y, bool relative, float depth) override;          // 0x151350
    void PushClip(int32_t x, int32_t y, const cGZRect *rect, bool relative, float depth) override; // 0x1511e0
    void PopClip() override;                                                                 // 0x151180
    void DrawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, float width) override;     // 0x150f20
    void FillRect(int32_t x, int32_t y, int32_t width, int32_t height) override;             // 0x14f0b0
    void FillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2) override; // 0x14eee0
    void FillQuad(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3,
                  int32_t y3) override;                                                      // 0x14efc0
    void SetBlendMode(uint32_t mode, float alpha) override;                                  // 0x151c40
    void DrawImage(const cGZRect *destination, int32_t record, const cGZRect *source) override; // 0x14d2e0
    void DrawRecord(const cGZRect *destination, int32_t record) override;                    // 0x14d8e0
    void DrawImageAt(int32_t x, int32_t y, int32_t record, const cGZRect *source) override;  // 0x14df20
    void DrawRecordAt(int32_t x, int32_t y, int32_t record) override;                        // 0x14deb0
    int32_t CreateRecord(uint32_t width, uint32_t height, uint32_t c, uint32_t d) override;  // 0x14eb10
    void SetRecordPixels(int32_t record, void *bits, void *format, uint32_t width, uint32_t height, uint32_t pitch,
                         const uint32_t *key) override;                                      // 0x1522d0
    void RebuildAtlases() override;                                                          // 0x14e1e0
    void Tick() override;                                                                    // 0x14ee00
    void AddRefRecord(int32_t record) override;                                              // 0x14eec0
    void ReleaseRecord(int32_t record) override;                                             // 0x151940
    void ApplyDrawState() override;                                                          // 0x151fa0
    bool LockBatch(uint32_t vertices, uint32_t indices, float **positions, uint32_t **colours, float **uvs,
                   uint16_t **indexOut) override;                                            // 0x1519c0
    void DrawBatch() override;                                                               // 0x14ee30
    int32_t GetClipDepth() override;                                                         // 0x14e710
    cDraw2DPoint GetClipOrigin() override;                                                   // 0x14e740
    cGZRect GetClipRect() override;                                                          // 0x14e6f0
    cIDraw2DMaterial *GetMaterial() override { return mMaterial.p; }                         // shared 0x151b40
    cDraw2DRecordInfo GetRecordInfo(int32_t record) override;                                // 0x150140

    // ---- cGZDrawContextRenderToHardware (+8): its derived part ----
    void vf19() override;                                                    // 0x151a60: the draw system's flag
    bool OnTargetSet() override;                                             // 0x14f960
    void OnDetach() override {}                                              // shared 0x140fe0
    void ApplyState() override;                                              // 0x14fd00
    void SetViewport(const cGZRect *viewport) override;                      // 0x14fc20
    void SetProjection(const float *matrix) override;                        // 0x14fb60
    bool SetTextureSource(cIGZBuffer *texture) override;                     // 0x14f970
    void GetTextureTiles(std::vector<Tile> *tiles, std::vector<TileRect> *rects) override; // 0x14f6f0
    void SelectTile(int32_t index, uint32_t scaled, uint32_t zero) override; // 0x14fb10
    void DrawTriangles(const Vertex *vertices, uint32_t count) override;     // 0x14f580
    void DrawQuads(const Vertex *vertices, uint32_t count) override;         // 0x14f390

    // ---- cIGZDrawContextEngineFactory (+0x100) ----
    bool CreateEngine(cIGZBuffer *target, cIGZDrawContextEngine **engine) override; // 0x14ea70

    // ---- non-virtual ----
    // 0x150330: the device and its state cache, the vertex format and its offsets, the vertex shader and declaration,
    // the dynamic arrays and the state blocks, each made when missing (or no longer created), and the clip stack reset
    // to the whole target.
    void CreateDeviceObjects();
    void ReleaseDeviceObjects(); // 0x152180
    // 0x151d20: the top clip, cut to the target, as the device's clip, viewport and the projection constants.
    void ApplyTopClip();
    // 0x14c960: a free record's index (the last one freed), else a new default record at the end.
    int32_t AllocateRecord();
    // 0x14ca90: packs the record's image into its pool's atlas textures (the 16-bit pool for a record under 24 bits, the
    // 32-bit one else, whichever the display supports), row by row and column by column; `create` lets it make new
    // atlases and textures. False (and no cells) when it does not fit.
    bool PlaceRecord(Record &record, bool create);
    // 0x14eca0: the record's cells handed back to their atlases; an atlas left empty is cleared and loses its texture.
    // True when one did (the atlases want rebuilding).
    bool ReleaseRecordCells(Record &record);
    // 0x152410 (thiscall, ret 4; lane OZ): the record's buffer pixels copied into its cells' textures (colour key and
    // border texels included). A record with rows but no columns reads stale locals in the original (not reproduced).
    void UploadRecord(Record &record);
    // 0x14dfa0 (cdecl, 8 arguments; lane OZ): the width x height 32-bit pixels at `src` (rows `stride` bytes apart)
    // written into the rectangle `rect` of the lockable `surface`, a pixel whose (value & mask) equals `key` written
    // as 0. Nothing is locked when width or height is 0.
    static void __cdecl UploadKeyed32(const uint8_t *src, uint32_t width, uint32_t height, uint32_t stride, void *surface,
                                      const cGZRect *rect, uint32_t key, uint32_t mask);
    // 0x150e20 (thiscall, this = out; __fastcall with edx unused has its shape, so it is hooked as it is): the overlap
    // of a and b, or all zeros and false when they don't meet. Written field by field, so out may be a. Slot 25 (game
    // code) calls it too.
    static bool __fastcall Intersect(cGZRect *out, void *unusedEdx, const cGZRect *a, const cGZRect *b);

    Ref<cIDeviceDX> mDevice;                // +0x104
    Ref<cIDeviceState> mState;              // +0x108
    Ref<cIDeviceResource> mDefaultBlock;    // +0x10c (a material without a block of its own; a cStateBlockDX)
    Ref<cIDeviceResource> mBlendBlocks[4];  // +0x110 SetBlendMode's
    Ref<cIDeviceResource> mEngineBlock;     // +0x120 ApplyState's
    int32_t mCurrentRecord;                 // +0x124 the engine's texture record (-1 none)
    uint32_t mChangeCount;                  // +0x128 the state cache's count when the engine last set state
    bool mDrawSystemFlag;                   // +0x12c the draw system's flag (vf19; not set by the constructor)
    uint8_t mPad12d[3];
    Ref<cIVertexFormat> mVertexFormat;      // +0x130
    uint32_t mPositionOffset;               // +0x134 in the 20-byte vertex
    uint32_t mColourOffset;                 // +0x138
    uint32_t mUvOffset;                     // +0x13c
    Ref<cIShaderProgramDX> mVertexShader;   // +0x140
    Ref<cIVertexDeclarationDX> mDeclaration; // +0x144
    Ref<cIDynamicVertexArray> mVertexArray;    // +0x148
    Ref<cIDynamicVertexArray> mIndexArray;     // +0x14c
    Ref<cIDraw2DMaterial> mMaterial;        // +0x150
    Pool mPools[3];                         // +0x154
    std::vector<Record> mRecords;           // +0x19c
    std::vector<int32_t> mFreeRecords;      // +0x1a8
    std::vector<cDraw2DClip> mClips;        // +0x1b4
    bool mClipEmpty;                        // +0x1c0 the top clip is empty: nothing draws
    bool mNeedsRebuild;                     // +0x1c1 the atlases want rebuilding (Tick)
    uint8_t mPad1c2[2];
};
static_assert(sizeof(cDraw2D::Pool) == 0x18 && sizeof(cDraw2D::Record) == 0x3c, "element sizes");
static_assert(offsetof(cDraw2D::Record, buffer) == 0x18 && offsetof(cDraw2D::Record, cells) == 0x2c &&
                  offsetof(cDraw2D::Record, borderX) == 0x38,
              "record layout");
static_assert(offsetof(cDraw2D, mDevice) == 0x104 && offsetof(cDraw2D, mCurrentRecord) == 0x124 &&
                  offsetof(cDraw2D, mVertexFormat) == 0x130 && offsetof(cDraw2D, mMaterial) == 0x150 &&
                  offsetof(cDraw2D, mPools) == 0x154 && offsetof(cDraw2D, mRecords) == 0x19c &&
                  offsetof(cDraw2D, mClips) == 0x1b4 && offsetof(cDraw2D, mClipEmpty) == 0x1c0,
              "layout");
static_assert(sizeof(cDraw2D) == cDraw2D::kSize, "cDraw2D: 0x1c4 bytes");

// nGZGraphic4::cDraw2DSystemService's interface: the system service's slots and the renderer (slot 13).
class cIDraw2DSystemService : public cIGZSystemService {
public:
    static constexpr uint32_t kIid = 0x4c9282f2;
    virtual cIDraw2D *GetDraw2D() = 0; // 13 (folded 0x46390: +8)
};

// nGZGraphic4::cDraw2DSystemService (0x14 bytes, vtable 0xd9c518): service id 0x89027355, priority 1499999. Init makes
// the renderer and registers its factory with the draw system; OnTick ticks it.
class cDraw2DSystemService : public cIDraw2DSystemService {
public:
    static constexpr uint32_t kSize = 0x14;
    static constexpr uint32_t kServiceId = 0x89027355;
    static constexpr int32_t kPriority = 1499999;

    // 0x14f2b0 (cdecl): a new service (null when the allocation fails). The COM director's creator for class
    // 0x4c9282f8; hooked (the switch).
    static cDraw2DSystemService *__cdecl Create();

    cDraw2DSystemService() = default;
    virtual ~cDraw2DSystemService(); // 0x14c7e0 (slot 14, deleting)

    bool QueryInterface(uint32_t riid, void **out) override; // 0x151450: ids 1, 0x287fb697, 0x4c9282f2
    uint32_t AddRef() override { return ++mRefCount; }        // shared 0x148fb0
    uint32_t Release() override;                              // shared 0x151920
    uint32_t GetServiceID() override { return mServiceId; }                  // shared 0x145770
    void SetServiceID(uint32_t id) override { mServiceId = id; }            // shared 0x149c60
    int32_t GetServicePriority() override { return kPriority; }              // 0x14f380
    bool IsServiceRunning() override { return mRunning; }                   // shared 0x150f10
    void SetServiceRunning(bool running) override { mRunning = running; }   // shared 0x1520c0
    bool Init() override;                                                    // 0x150210
    bool Shutdown() override;                                                // 0x152140
    bool OnTick(uint32_t counter) override;                                  // 0x151160
    bool OnIdle(uint32_t) override { return true; }                          // shared 0x148260
    int32_t GetServiceTickPriority() override { return kPriority; }          // 0x14f380
    cIDraw2D *GetDraw2D() override { return mDraw2D; }                       // shared 0x46390

    uint32_t mRefCount = 0;              // +4
    cDraw2D *mDraw2D = nullptr;          // +8 counted
    uint32_t mServiceId = kServiceId;    // +0xc
    bool mRunning = false;               // +0x10
    uint8_t mPad11[3];
};
static_assert(offsetof(cDraw2DSystemService, mDraw2D) == 8 && offsetof(cDraw2DSystemService, mRunning) == 0x10,
              "layout");
static_assert(sizeof(cDraw2DSystemService) == cDraw2DSystemService::kSize, "cDraw2DSystemService: 0x14 bytes");

} // namespace nGZGraphic4
