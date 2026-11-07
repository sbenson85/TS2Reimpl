#pragma once
// nGZGraphic4::cDeviceStateDX: the render-state cache in front of the Direct3D 9 device (docs/subsystems/rendering.md).
// The engine sets state through it; it keeps what it was asked for (the "wanted" state), sends a change to the device at
// once for most kinds of state and records it in a change record, and holds back the kinds that depend on each other
// (render targets, viewport, scissor, shader constants) until Flush sends them in one go, before a draw or a scene.
//
// Standalone shape (docs/standalone-shape.md): the class derives from its one interface and carries our vtable; the
// constructor hook (cDeviceStateDXAdapters::Construct at 0x17c490) is the switch. The non-virtual members are hooked
// at their RVAs (the device class and the state commands, still game code, call them directly); SetUpDevice and the
// default commands are in effect through their callers (our cDeviceDX::CreateDevice). Slots 9-12 are ours
// (Reset, the state blocks and the command slot), and so are 27-29 (the shader-validation debug drivers and their
// validators, in effect through the slots).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d9.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "gz/cIGZUnknown.h"

#include "game/owned.h"
TS2_OWNED_STATIC(const char, d9fba8)[]; // the source file of the class's reports (kSourceFileRva)

namespace nGZGraphic4 {

class cDeviceDX;
class cStateBlockDX;

// nGZGraphic4::cIDeviceState (RTTI 0x10984ac's base; 32 slots). Only cDeviceStateDX implements it.
class cIDeviceState : public cIGZUnknown {
public:
    virtual cDeviceDX *GetDevice() = 0;            // slot 3 (folded 0x46390: +8)
    virtual cDeviceDX *GetDeviceConst() = 0;       // slot 4 (the same body: the const overload)
    virtual void *GetDeviceEnum() = 0;             // slot 5 (folded 0x145770: +0xc, the cDeviceEnumDX)
    virtual void *GetDeviceEnumConst() = 0;        // slot 6
    virtual void *GetResourceManager() = 0;        // slot 7 (0x17cbf0: the device's slot 18)
    virtual void *GetResourceManagerConst() = 0;   // slot 8 (the same body)
    virtual void Reset(uint32_t flags) = 0;        // slot 9 (0x17e070; src/gz/cDeviceStateDXReset.cpp)
    // Slot 10 (0x17cc00; src/gz/cDeviceStateDXBlocks.cpp): a state block of the groups (1 and 2) made from what differs
    // from the defaults, shared with an equal one the cache made before; `out` is the address of the caller's pointer
    // (cDraw2D passes it as a word), which takes a reference. Returns the block's count after the cache's own
    // reference is dropped: 1, or 0 where the new block was dropped for an equal one.
    virtual uint32_t CreateStateBlock(uint32_t groups, uint32_t out) = 0;
    virtual void ApplyStateBlock(uint32_t block) = 0; // slot 11 (0x17fc00): the block's address
    virtual void ApplyStateCommand(uint32_t command) = 0;          // slot 12 (0x17fd40: the command's address; src/gz/cDeviceStateDXCommands.cpp)
    virtual void SetTexture(uint32_t stage, cIGZUnknown *texture) = 0;                                   // slot 13
    virtual void SetVertexShader(cIGZUnknown *shader) = 0;                                               // slot 14
    virtual void SetPixelShader(cIGZUnknown *shader) = 0;                                                // slot 15
    virtual void SetVertexDeclaration(cIGZUnknown *declaration) = 0;                                     // slot 16
    virtual void SetStreamSource(uint32_t stream, cIGZUnknown *buffer, uint32_t stride, uint32_t offset) = 0; // slot 17
    virtual void SetIndices(cIGZUnknown *indices) = 0;                                                   // slot 18
    virtual float *LockVertexFloatConstants(uint32_t start, uint32_t count) = 0;                         // slot 19
    virtual int32_t *LockVertexIntConstants(uint32_t start, uint32_t count) = 0;                         // slot 20
    virtual BOOL *LockVertexBoolConstants(uint32_t start, uint32_t count) = 0;                           // slot 21
    virtual float *LockPixelFloatConstants(uint32_t start, uint32_t count) = 0;                          // slot 22
    virtual int32_t *LockPixelIntConstants(uint32_t start, uint32_t count) = 0;                          // slot 23
    virtual BOOL *LockPixelBoolConstants(uint32_t start, uint32_t count) = 0;                            // slot 24
    virtual void SetProjection(const D3DMATRIX *matrix) = 0;                                             // slot 25
    virtual void SetWorld(const void *transform) = 0;                                                    // slot 26
    // Slots 27-29 (src/gz/cDeviceStateDXValidate.cpp): the shader-validation debug drivers; each reports what failed
    // only when `log` is set.
    virtual bool CheckVertexProcessing(cIGZUnknown *declaration, bool log) = 0;       // slot 27 (0x181f00)
    virtual bool Validate(bool log) = 0;                                              // slot 28 (0x182220)
    virtual bool ValidateCommand(const void *command, bool hardware, bool log) = 0;   // slot 29 (0x1829d0)
    virtual uint32_t GetChangeCount() = 0;                        // slot 30 (folded 0x148ba0: +0x18)
    virtual ~cIDeviceState() = default;                           // slot 31: the deleting destructor (0x17c950)
};

namespace devstate {

constexpr uint32_t kNoStart = 0x7fffffff; // a constant group's start when nothing is pending

// The state the engine asked for (0x1190 bytes; SetDefaults 0x17c1a0). Two copies in the object: the wanted state at
// +0xa0 and the defaults at +0x75b0. Every field is a dword; matrices, materials and lights are compared word by word
// (bit patterns, not float values), viewport and scissor fractions as floats.
struct StreamSource {
    IDirect3DVertexBuffer9 *buffer;
    uint32_t stride;
    uint32_t offset;
};
struct DeviceState {
    void SetDefaults(); // 0x17c1a0

    IDirect3DBaseTexture9 *textures[16];        // +0x0
    uint32_t softwareVertexProcessing;          // +0x40
    IDirect3DVertexShader9 *vertexShader;       // +0x44
    IDirect3DPixelShader9 *pixelShader;         // +0x48
    uint32_t renderStates[0x59];                // +0x4c: by the engine's index (the device's: kRenderStateMap)
    uint32_t stageStates[8][0x12];              // +0x1b0 (kStageStateMap)
    uint32_t samplerStates[16][0xd];            // +0x3f0 (kSamplerStateMap)
    D3DMATRIX textureTransforms[8];             // +0x730
    D3DMATERIAL9 material;                      // +0x930
    IDirect3DVertexDeclaration9 *declaration;   // +0x974
    StreamSource streams[16];                   // +0x978
    IDirect3DIndexBuffer9 *indices;             // +0xa38
    IDirect3DSurface9 *targets[4];              // +0xa3c
    IDirect3DSurface9 *depth;                   // +0xa4c
    uint32_t targetWidth;                       // +0xa50: of targets[0]
    uint32_t targetHeight;                      // +0xa54
    float scissor[4];                           // +0xa58: left, top, right, bottom as fractions of the target
    uint32_t scissorEnable;                     // +0xa68
    float viewport[4];                          // +0xa6c: left, top, right, bottom fractions
    float minZ;                                 // +0xa7c
    float maxZ;                                 // +0xa80
    uint32_t clipping;                          // +0xa84
    D3DMATRIX projection;                       // +0xa88 (as sent: transposed)
    D3DMATRIX world;                            // +0xac8 (as sent)
    uint32_t ambient;                           // +0xb08
    uint32_t lightCount;                        // +0xb0c: lights enabled, 0..count-1
    D3DLIGHT9 lights[16];                       // +0xb10
};
static_assert(sizeof(D3DMATERIAL9) == 0x44 && sizeof(D3DLIGHT9) == 0x68 && sizeof(D3DVIEWPORT9) == 0x18, "d3d9 layout");
static_assert(offsetof(DeviceState, renderStates) == 0x4c && offsetof(DeviceState, stageStates) == 0x1b0, "layout");
static_assert(offsetof(DeviceState, samplerStates) == 0x3f0 && offsetof(DeviceState, material) == 0x930, "layout");
static_assert(offsetof(DeviceState, streams) == 0x978 && offsetof(DeviceState, targets) == 0xa3c, "layout");
static_assert(offsetof(DeviceState, scissor) == 0xa58 && offsetof(DeviceState, viewport) == 0xa6c, "layout");
static_assert(offsetof(DeviceState, projection) == 0xa88 && offsetof(DeviceState, ambient) == 0xb08, "layout");
static_assert(sizeof(DeviceState) == 0x1190, "layout");

// What changed since the change record was last taken (+0x1c, 0x54 bytes): bits by the engine's indices.
struct ChangeRecord {
    uint32_t renderStates[3];   // +0x1c
    uint16_t stages;            // +0x28: stages with a changed stage state
    uint16_t samplers;          // +0x2a
    uint16_t textures;          // +0x2c
    uint8_t transforms;         // +0x2e: texture transforms
    uint8_t misc;               // +0x2f: kMaterial | kSoftwareVertexProcessing | kVertexShader | kPixelShader
    uint32_t stageStates[8];    // +0x30
    uint16_t samplerStates[16]; // +0x50
    static constexpr uint8_t kMaterial = 1, kSoftwareVertexProcessing = 2, kVertexShader = 4, kPixelShader = 8;
};
static_assert(sizeof(ChangeRecord) == 0x54, "layout");

// The groups Flush sends only when asked (+0x74, 12 bytes): flags, and per constant group the end of the pending range
// (its start at +0x80..+0x94). Vertex ends are 16-bit, pixel ends 8-bit.
struct Pending {
    uint8_t targets;           // +0x74: targets or depth changed (Flush group 8)
    uint8_t viewport;          // +0x75: group 0x10
    uint16_t vertexFloatEnd;   // +0x76 (group 4, as all the ends)
    uint16_t vertexIntEnd;     // +0x78
    uint16_t vertexBoolEnd;    // +0x7a
    uint8_t pixelFloatEnd;     // +0x7c
    uint8_t pixelIntEnd;       // +0x7d
    uint8_t pixelBoolEnd;      // +0x7e
    uint8_t scissor;           // +0x7f: group 8
    // The original tests the three dwords at +0x74 (SetVertexShader, ResetTexCoordStates): any byte set.
    bool Any() const
    {
        return targets || viewport || vertexFloatEnd || vertexIntEnd || vertexBoolEnd || pixelFloatEnd || pixelIntEnd ||
               pixelBoolEnd || scissor;
    }
};
static_assert(sizeof(Pending) == 12, "layout");

// A recorded state change set (+0x8740, 0x12c8 bytes; Clear 0x17dee0): what the wanted state holds that the defaults
// do not. Build (0x17d510) fills it for CreateStateBlock, Serialize (0x182d90) writes it out as a state block's record
// (src/gz/cDeviceStateDXBlocks.cpp); the bytes after each change flag are never written.
struct IndexValue {
    uint32_t index, value;
};
struct StateDelta {
    void Clear(); // 0x17dee0
    // 0x17d510 (cdecl-like: this, groups, defaults, wanted): clears, then records the differences of the groups (1:
    // textures, shaders, render, stage, sampler states, texture transforms, material; 2: declaration, streams, indices).
    void Build(uint32_t groups, const DeviceState &defaults, const DeviceState &wanted);
    // 0x182d90: the record of the delta as a state block keeps it (see cStateBlockDX.h). With a null buffer only the
    // length; otherwise written there, one reference taken on every Direct3D object the record names. Returns the length.
    uint16_t Serialize(uint8_t *buffer) const;

    uint32_t flags;                   // +0x0
    uint32_t changeCount;             // +0x4
    uint32_t textureCount;            // +0x8
    uint32_t textureMask;             // +0xc
    IndexValue textures[16];          // +0x10
    uint8_t softwareVertexProcessing; // +0x90
    uint8_t vertexShaderChanged;      // +0x91
    uint8_t pad92[2];
    uint32_t vertexShader;            // +0x94
    uint8_t pixelShaderChanged;       // +0x98
    uint8_t pad99[3];
    uint32_t pixelShader;             // +0x9c
    uint32_t renderStateCount;        // +0xa0
    uint32_t renderStateMask[3];      // +0xa4
    IndexValue renderStates[0x59];    // +0xb0
    uint32_t stageMask;               // +0x378
    uint32_t stageCounts[8];          // +0x37c
    uint32_t stageBits[8];            // +0x39c
    IndexValue stageStates[8][0x12];  // +0x3bc
    uint32_t samplerMask;             // +0x83c
    uint32_t samplerCounts[16];       // +0x840
    uint32_t samplerBits[16];         // +0x880
    IndexValue samplerStates[16][0xd]; // +0x8c0
    uint32_t transformCount;          // +0xf40
    uint32_t transformMask;           // +0xf44
    struct {
        uint32_t index;
        D3DMATRIX matrix;
    } transforms[8];                  // +0xf48
    uint8_t materialChanged;          // +0x1168
    uint8_t pad1169[3];
    D3DMATERIAL9 material;            // +0x116c
    uint8_t declarationChanged;       // +0x11b0
    uint8_t pad11b1[3];
    uint32_t declaration;             // +0x11b4
    uint32_t streamCount;             // +0x11b8
    uint32_t streamMask;              // +0x11bc
    struct {
        uint32_t index, buffer, stride, offset;
    } streams[16];                    // +0x11c0
    uint8_t indicesChanged;           // +0x12c0
    uint8_t pad12c1[3];
    uint32_t indices;                 // +0x12c4
};
static_assert(offsetof(StateDelta, stageMask) == 0x378 && offsetof(StateDelta, samplerMask) == 0x83c, "layout");
static_assert(offsetof(StateDelta, transformCount) == 0xf40 && offsetof(StateDelta, materialChanged) == 0x1168, "layout");
static_assert(offsetof(StateDelta, streams) == 0x11c0 && sizeof(StateDelta) == 0x12c8, "layout");

// The engine's state indices -> Direct3D's (globals the constructor builds from the tables in the image, every entry not
// in a table left 0xffff / 0xff).
constexpr uint32_t kRenderStateMap = 0x10ca448;  // uint16_t[0x59]
constexpr uint32_t kStageStateMap = 0x10ca4fc;   // uint8_t[0x12]
constexpr uint32_t kSamplerStateMap = 0x10ca510; // uint8_t[0xd]

} // namespace devstate

class cDeviceStateDX : public cIDeviceState {
public:
    static constexpr uint32_t kOriginalVTable = 0xd9faf0;
    static constexpr uint32_t kInterfaceId = 0x9b81aed2; // cIDeviceState (QueryInterface also answers 1)
    static constexpr uint32_t kSourceFileRva = 0xd9fba8; // LogD3DError's source file for every report in this class

    // Flush's group bits.
    static constexpr uint32_t kFlushConstants = 4, kFlushTargets = 8, kFlushViewport = 0x10, kFlushAll = 0x7f;

    cDeviceStateDX(cDeviceDX *device, void *deviceEnum); // 0x17c490 (hook: cDeviceStateDXAdapters::Construct)
    ~cDeviceStateDX() override;                          // 0x17c950 is the deleting destructor (slot 31)
    cDeviceStateDX(const cDeviceStateDX &) = delete;
    cDeviceStateDX &operator=(const cDeviceStateDX &) = delete;

    // cIGZUnknown
    bool QueryInterface(uint32_t riid, void **out) override; // 0x17dc50
    uint32_t AddRef() override;                              // folded 0x148fb0
    uint32_t Release() override;                             // 0x17dcb0

    cDeviceDX *GetDevice() override { return mDevice; }
    cDeviceDX *GetDeviceConst() override { return mDevice; }
    void *GetDeviceEnum() override { return mDeviceEnum; }
    void *GetDeviceEnumConst() override { return mDeviceEnum; }
    void *GetResourceManager() override;      // 0x17cbf0
    void *GetResourceManagerConst() override; // 0x17cbf0
    void Reset(uint32_t flags) override;
    uint32_t CreateStateBlock(uint32_t groups, uint32_t out) override; // src/gz/cDeviceStateDXBlocks.cpp
    void ApplyStateBlock(uint32_t block) override;
    void ApplyStateCommand(uint32_t command) override;          // 0x17fd40
    void SetTexture(uint32_t stage, cIGZUnknown *texture) override;                                   // 0x1801b0
    void SetVertexShader(cIGZUnknown *shader) override;                                               // 0x180550
    void SetPixelShader(cIGZUnknown *shader) override;                                                // 0x17f770
    void SetVertexDeclaration(cIGZUnknown *declaration) override;                                     // 0x180470
    void SetStreamSource(uint32_t stream, cIGZUnknown *buffer, uint32_t stride, uint32_t offset) override; // 0x1803c0
    void SetIndices(cIGZUnknown *indices) override;                                                   // 0x1804e0
    float *LockVertexFloatConstants(uint32_t start, uint32_t count) override;                         // 0x180680
    int32_t *LockVertexIntConstants(uint32_t start, uint32_t count) override;                         // 0x1806d0
    BOOL *LockVertexBoolConstants(uint32_t start, uint32_t count) override;                           // 0x180630
    float *LockPixelFloatConstants(uint32_t start, uint32_t count) override;                          // 0x17f830
    int32_t *LockPixelIntConstants(uint32_t start, uint32_t count) override;                          // 0x17f880
    BOOL *LockPixelBoolConstants(uint32_t start, uint32_t count) override;                            // 0x17f7e0
    void SetProjection(const D3DMATRIX *matrix) override;                                             // 0x17ede0
    void SetWorld(const void *transform) override;                                                    // 0x17ecf0
    bool CheckVertexProcessing(cIGZUnknown *declaration, bool log) override;
    bool Validate(bool log) override;
    bool ValidateCommand(const void *command, bool hardware, bool log) override;
    uint32_t GetChangeCount() override { return mChangeCount; }

    // Non-virtual members (hooked at their RVAs).
    void Flush(uint32_t groups);                                           // 0x17cdf0
    void MarkTargetsDirty();                                               // 0x17ec40
    void SetBackBufferTarget(IDirect3DSurface9 *surface, IDirect3DSurface9 *depth, uint32_t width, uint32_t height); // 0x180050
    void ResetTexCoordStates();                                            // 0x17ff90
    void RestoreTexCoordStates();                                          // 0x17eb90
    void ReleaseDeviceObjects();                                           // 0x1815f0
    // 0x17da90 (lane RJ): the block count, then their record bytes in KB, the average record size and the average
    // number of state changes (the record's byte 3), written to the cIGZOStream face of `stream` (slots 14 and 9).
    void DumpInfo(cIGZUnknown *stream);
    void SetRenderState(uint32_t index, uint32_t value);                   // 0x17f8d0
    void SetRenderStates(uint32_t count, const uint32_t *indices, const uint32_t *values); // 0x17f940
    void SetTextureStageState(uint32_t stage, uint32_t index, uint32_t value);            // 0x180230
    void SetTextureStageStates(uint32_t stage, uint32_t count, const uint32_t *indices, const uint32_t *values); // 0x1802b0
    void SetSamplerStates(uint32_t sampler, uint32_t count, const uint32_t *indices, const uint32_t *values);     // 0x17fac0
    void SetTextureTransform(uint32_t index, const D3DMATRIX *matrix);     // 0x180350
    void SetMaterial(const D3DMATERIAL9 *material);                        // 0x17f0a0
    void SetLight(uint32_t index, const D3DLIGHT9 *light);                 // 0x17efb0
    void SetLightCount(uint32_t count, uint32_t ambient);                  // 0x17f6a0
    void SetClipping(uint32_t clipping);                                   // 0x17ec50
    void SetDepthRange(float minZ, float maxZ);                            // 0x17eca0
    void SetViewportFractions(float left, float top, float right, float bottom); // 0x180720
    void SetScissor(float left, float top, float right, float bottom, uint8_t enable); // 0x17fb60
    void SetRenderTargets(cIGZUnknown *const *targets, cIGZUnknown *depth); // 0x17f9e0
    // The state block code (src/gz/cDeviceStateDXBlocks.cpp). The first two apply the record of the block last applied
    // (mStateBlock98 / mStateBlock9c) and clear the change bits it covers; the others send the defaults for the state
    // the previous block changed and the new one does not.
    void ApplyRecordStates();     // 0x17f180: textures, shaders, render, stage, sampler states, transforms, material
    void ApplyRecordStreams();    // 0x17ee30: declaration, streams, indices
    void RestoreChangedStates();  // 0x181820
    void RestoreChangedStreams(); // 0x1816d0
    // 0x1807b0 (src/gz/cDeviceStateDXSetUp.cpp): takes a new or reset Direct3D device. The first time it builds the
    // defaults (the state tables' default values, then the 33 default commands, copied from the wanted state); every
    // time it reads the device's back buffer and depth surface into the defaults and sends the whole default state.
    void SetUpDevice(IDirect3DDevice9 *device);
    // The default commands (0x186550..0x18a1a0, cdecl(state, caps), called only from SetUpDevice in id order): what the
    // engine's state command of each id sets when it holds its default (src/gz/cDeviceStateDXCommands.cpp).
    void ApplyDefaultCommand(uint32_t id, const D3DCAPS9 *caps);
    // 0x17d980 (cdecl): copies `src` transposed into `dst`; true if any word changed.
    static bool CopyMatrixTransposed(D3DMATRIX *dst, const D3DMATRIX *src);

    uint32_t mRefCount;                          // +0x4
    cDeviceDX *mDevice;                          // +0x8: the owner
    void *mDeviceEnum;                           // +0xc: the device's cDeviceEnumDX (its +0x3c)
    IDirect3DDevice9 *mD3D;                      // +0x10
    uint8_t mDefaultsBuilt;                      // +0x14: SetUpDevice has built the defaults (once per object)
    uint8_t mHardwareVertexProcessing;           // +0x15: what a null vertex shader runs with
    uint8_t mRenderTargetCount;                  // +0x16: simultaneous render targets the device has
    uint8_t mPad17;                              // never written
    uint32_t mChangeCount;                       // +0x18
    devstate::ChangeRecord mChanges;             // +0x1c
    uint16_t mStreamChanges;                     // +0x70
    uint8_t mDeclarationChanged;                 // +0x72
    uint8_t mIndicesChanged;                     // +0x73
    devstate::Pending mPending;                  // +0x74
    uint32_t mVertexFloatStart;                  // +0x80
    uint32_t mVertexIntStart;                    // +0x84
    uint32_t mVertexBoolStart;                   // +0x88
    uint32_t mPixelFloatStart;                   // +0x8c
    uint32_t mPixelIntStart;                     // +0x90
    uint32_t mPixelBoolStart;                    // +0x94
    cStateBlockDX *mStateBlock98;                // +0x98: the block of group 1 last applied (counted; released by ReleaseDeviceObjects)
    cStateBlockDX *mStateBlock9c;                // +0x9c: the block of group 2 last applied (counted)
    devstate::DeviceState mWanted;               // +0xa0
    float mVertexFloats[0x400][4];               // +0x1230
    int32_t mVertexInts[0x100][4];               // +0x5230
    BOOL mVertexBools[0x100];                    // +0x6230
    float mPixelFloats[0xe0][4];                 // +0x6630
    int32_t mPixelInts[0x10][4];                 // +0x7430
    BOOL mPixelBools[0x10];                      // +0x7530
    IDirect3DSurface9 *mSentTargets[4];          // +0x7570: what the device has
    IDirect3DSurface9 *mSentDepth;               // +0x7580
    D3DVIEWPORT9 mSentViewport;                  // +0x7584
    RECT mSentScissor;                           // +0x759c
    uint32_t mSentScissorEnable;                 // +0x75ac
    devstate::DeviceState mDefaults;             // +0x75b0
    devstate::StateDelta mDelta;                 // +0x8740
    std::vector<cStateBlockDX *> mList;          // +0x9a08: the blocks made here, by record (size, then bytes); no references held
    cIGZUnknown *mHeld9a14;                      // +0x9a14 (released by ReleaseDeviceObjects)
    cIGZUnknown *mHeld9a18;                      // +0x9a18
};

// Old entry points (docs/standalone-shape.md, adapters).
struct cDeviceStateDXAdapters {
    static cDeviceStateDX *__fastcall Construct(void *memory, void *, cDeviceDX *device, void *deviceEnum);
};

} // namespace nGZGraphic4

namespace devstate_layout {
using nGZGraphic4::cDeviceStateDX;
static_assert(offsetof(cDeviceStateDX, mRefCount) == 0x4 && offsetof(cDeviceStateDX, mD3D) == 0x10, "layout");
static_assert(offsetof(cDeviceStateDX, mChangeCount) == 0x18 && offsetof(cDeviceStateDX, mChanges) == 0x1c, "layout");
static_assert(offsetof(cDeviceStateDX, mStreamChanges) == 0x70 && offsetof(cDeviceStateDX, mPending) == 0x74, "layout");
static_assert(offsetof(cDeviceStateDX, mVertexFloatStart) == 0x80 && offsetof(cDeviceStateDX, mStateBlock98) == 0x98, "layout");
static_assert(offsetof(cDeviceStateDX, mWanted) == 0xa0 && offsetof(cDeviceStateDX, mVertexFloats) == 0x1230, "layout");
static_assert(offsetof(cDeviceStateDX, mVertexBools) == 0x6230 && offsetof(cDeviceStateDX, mPixelFloats) == 0x6630, "layout");
static_assert(offsetof(cDeviceStateDX, mPixelBools) == 0x7530 && offsetof(cDeviceStateDX, mSentTargets) == 0x7570, "layout");
static_assert(offsetof(cDeviceStateDX, mSentViewport) == 0x7584 && offsetof(cDeviceStateDX, mSentScissor) == 0x759c, "layout");
static_assert(offsetof(cDeviceStateDX, mDefaults) == 0x75b0 && offsetof(cDeviceStateDX, mDelta) == 0x8740, "layout");
static_assert(offsetof(cDeviceStateDX, mList) == 0x9a08 && offsetof(cDeviceStateDX, mHeld9a18) == 0x9a18, "layout");
static_assert(sizeof(cDeviceStateDX) == 0x9a1c, "layout");
} // namespace devstate_layout
