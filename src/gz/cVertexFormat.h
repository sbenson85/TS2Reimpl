#pragma once
// The vertex formats and their Direct3D declarations (lane NJ, rendering R3 part 3): nGZGraphic4::cVertexFormat,
// cVertexFormatsManager, cVertexDeclarationDX and cVertexDeclarationsManagerDX. docs/subsystems/rendering.md, R3.
//
// A vertex format is a list of streams, each a list of 16-byte elements (a usage id, a usage index, a component type,
// the stream and the element's place in it). Formats are made by the formats manager, a builder: BeginFormat,
// AddElement..., then EndFormat answers the shared format equal to what was built, making it when it is new. The
// declarations manager turns a format into an IDirect3DVertexDeclaration9 per device (cVertexDeclarationDX, from a
// fixed-size pool).
//
// Standalone shape (docs/standalone-shape.md): both managers are static singletons answered by getters (the graphic
// system's slots 17 / 18 jump to them); the getters 0x16db40 / 0x16e760 are hooked and answer ours, so the managers'
// vtables are never reached, and the formats and declarations only the managers make are ours too: every slot is in
// effect through its callers.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d9.h>

#include <cstdint>
#include <vector>

#include "gz/cSurfaceDX.h" // cIDeviceResource

class cIGZString;

#include "game/owned.h"
#include "rz/RZFastFixedAllocators.h"

// The classes' fixed-size pools (operator new / delete).
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x58>, fed220);
TS2_OWNED_STATIC(const char, d9ea18)[]; // the vertex declarations' LogD3DError source file

namespace nGZGraphic4 {

class cIDeviceDX;

// One element (16 bytes, as the original's).
struct cVertexElement {
    uint32_t usage;   // +0: the usage id (a hash: position, normal, texture coordinate...)
    uint8_t index;    // +4: the usage index
    uint8_t pad0[3];
    uint32_t type;    // +8: 0-3 one to four floats, 4 four bytes, 5 a colour; 6 the "none" FindElement answers
    uint8_t stream;   // +0xc
    uint8_t position; // +0xd: the element's place in its stream (EndFormat renumbers after sorting)
    uint8_t pad1[2];
};
static_assert(sizeof(cVertexElement) == 16, "element layout");

constexpr uint32_t kIidVertexFormat = 0x1bb36b8b;
constexpr uint32_t kIidVertexFormatsManager = 0x9bb38afb;
constexpr uint32_t kIidVertexDeclarationsManager = 0x0b620947;
constexpr uint32_t kIidVertexDeclarationsManagerDX = 0xcb8d837a;
constexpr uint32_t kIidVertexDeclaration = 0xcb448989;
constexpr uint32_t kIidVertexDeclarationDX = 0x8b448b45;
constexpr uint32_t kIidD3DVertexDeclaration = 0x8b448b4c; // +0x10, not counted (answered before the null check)

// 0x18dbe0 (cdecl, hooked): the usage's short name ("pos", "norm", "tc"...; "UNKNOWN" for others), for Describe and
// the scene graph's shader validation.
const char *VertexUsageName(uint32_t usage);

namespace format_detail {
// 0x16d5c0 (in effect through EndFormat): the order EndFormat sorts each stream's elements in.
int32_t SortKey(const cVertexElement &e);
} // namespace format_detail

// nGZGraphic4::cIVertexFormat.
class cIVertexFormat : public cIGZUnknown {
public:
    virtual uint32_t GetId() = 0;                                  // 3: its place in the formats manager
    virtual uint32_t GetStreamCount() = 0;                         // 4
    virtual uint32_t GetElementCount(uint32_t stream) = 0;         // 5
    virtual uint32_t GetTotalElementCount() = 0;                   // 6
    // 7: the element by value (the hidden result pointer, answered).
    virtual cVertexElement *GetElement(cVertexElement *out, uint32_t stream, uint32_t i) = 0;
    // 8 / 9: the element with the usage and index in the stream / in any stream; when there is none, type 6 and the
    // stream count as its stream.
    virtual cVertexElement *FindElement(cVertexElement *out, uint32_t stream, uint32_t usage, uint8_t index) = 0;
    virtual cVertexElement *FindElementAny(cVertexElement *out, uint32_t usage, uint8_t index) = 0;
    virtual int32_t GetMaxIndex(uint32_t usage) = 0;               // 10: the highest index of the usage, -1 if none
    virtual uint32_t GetStreamSize(uint32_t stream) = 0;           // 11: bytes per vertex in the stream
    virtual uint32_t GetElementSize(uint32_t stream, uint32_t i) = 0; // 12
    virtual uint32_t GetElementOffset(uint32_t stream, int32_t i) = 0; // 13: the bytes before element i
    virtual uint32_t GetOffset(uint32_t stream, uint32_t usage, uint8_t index) = 0; // 14: FindElement's offset
    virtual uint32_t GetOffsetAny(uint32_t usage, uint8_t index) = 0;              // 15: FindElementAny's
    virtual void Describe(cIGZString *text) = 0; // 16: "id:N{S name index:type...}" into the string
    virtual ~cIVertexFormat() = default;         // 17
};

// nGZGraphic4::cIVertexFormatsManager.
class cIVertexFormatsManager : public cIGZUnknown {
public:
    virtual void BeginFormat() = 0; // 3: drops the formats only the manager holds; building
    virtual void AddElement(uint32_t stream, uint32_t type, uint32_t usage, uint8_t index) = 0; // 4
    virtual void EndFormat(cIVertexFormat **out) = 0;     // 5: the format equal to the one built (counted)
    virtual bool IsBuilding() = 0;                        // 6
    virtual uint32_t GetFormatCount() = 0;                // 7: entry 0 is always empty
    virtual cIVertexFormat *GetFormat(uint32_t id) = 0;   // 8 (not counted)
    virtual ~cIVertexFormatsManager() = default;          // 9
};

class cVertexFormat final : public cIVertexFormat {
public:
    bool QueryInterface(uint32_t riid, void **out) override;
    uint32_t AddRef() override { return ++mRefs; }
    uint32_t Release() override;
    uint32_t GetId() override { return mId; }
    uint32_t GetStreamCount() override { return static_cast<uint32_t>(mStreams.size()); }
    uint32_t GetElementCount(uint32_t stream) override { return static_cast<uint32_t>(mStreams[stream].size()); }
    uint32_t GetTotalElementCount() override;
    cVertexElement *GetElement(cVertexElement *out, uint32_t stream, uint32_t i) override;
    cVertexElement *FindElement(cVertexElement *out, uint32_t stream, uint32_t usage, uint8_t index) override;
    cVertexElement *FindElementAny(cVertexElement *out, uint32_t usage, uint8_t index) override;
    int32_t GetMaxIndex(uint32_t usage) override;
    uint32_t GetStreamSize(uint32_t stream) override;
    uint32_t GetElementSize(uint32_t stream, uint32_t i) override;
    uint32_t GetElementOffset(uint32_t stream, int32_t i) override;
    uint32_t GetOffset(uint32_t stream, uint32_t usage, uint8_t index) override;
    uint32_t GetOffsetAny(uint32_t usage, uint8_t index) override;
    void Describe(cIGZString *text) override;
    ~cVertexFormat() override = default;

    using Stream = std::vector<cVertexElement>;
    uint32_t mRefs = 0;           // +4
    uint32_t mId = 0;             // +8
    std::vector<Stream> mStreams; // +0xc

private:
    cVertexElement *None(cVertexElement *out);
};
static_assert(sizeof(cVertexFormat) == 0x18, "cVertexFormat size");

class cVertexFormatsManager final : public cIVertexFormatsManager {
public:
    static constexpr uint32_t kGetterRva = 0x16db40;
    static cVertexFormatsManager *Instance(); // the getter (hooked)

    bool QueryInterface(uint32_t riid, void **out) override;
    uint32_t AddRef() override { return 1; } // a static object: not counted
    uint32_t Release() override { return 1; }
    void BeginFormat() override;
    void AddElement(uint32_t stream, uint32_t type, uint32_t usage, uint8_t index) override;
    void EndFormat(cIVertexFormat **out) override;
    bool IsBuilding() override { return mBuilding; }
    uint32_t GetFormatCount() override { return static_cast<uint32_t>(mFormats.size()); }
    cIVertexFormat *GetFormat(uint32_t id) override { return mFormats[id]; }
    ~cVertexFormatsManager() override;

    cVertexFormatsManager();
    cVertexFormatsManager(const cVertexFormatsManager &) = delete;
    cVertexFormatsManager &operator=(const cVertexFormatsManager &) = delete;

    std::vector<cIVertexFormat *> mFormats; // +4: counted; entry 0 empty
    cVertexFormat *mBuilder = nullptr;      // +0x10 (counted)
    bool mBuilding = false;                 // +0x14
};
static_assert(sizeof(cVertexFormatsManager) == 0x18, "cVertexFormatsManager size");

// nGZGraphic4::cIVertexDeclaration(DX).
class cIVertexDeclarationDX : public cIDeviceResource {
public:
    virtual uint32_t GetId() = 0;                                 // 10: its place in the declarations manager
    virtual uint32_t GetStreamSize(uint32_t stream) = 0;          // 11: the format's bytes per vertex in the stream
    virtual cIVertexFormat *GetFormat() = 0;                      // 12
    virtual cIDeviceDX *GetDeviceDX() = 0;                        // 13
    virtual IDirect3DVertexDeclaration9 *GetD3DDeclaration() = 0; // 14
    virtual ~cIVertexDeclarationDX() = default;                   // 15
};

class cIVertexDeclarationsManagerDX : public cIGZUnknown {
public:
    // 3: the declaration of `format` on `device` (counted), made when there is none; false when the device isn't ready
    // or Direct3D is out of memory.
    virtual bool GetDeclaration(cIDeviceDX *device, cIVertexFormat *format, cIVertexDeclarationDX **out) = 0;
    virtual uint32_t GetDeclarationCount() = 0;                       // 4: entry 0 is always empty
    virtual cIVertexDeclarationDX *GetDeclarationAt(uint32_t id) = 0; // 5 (not counted)
    virtual const char *const *GetSemanticRenames() = 0;              // 6: shader semantic pairs, null-terminated
    virtual ~cIVertexDeclarationsManagerDX() = default;               // 7
};

class cVertexDeclarationDX final : public cIVertexDeclarationDX {
public:
    static constexpr uint32_t kPoolRva = 0xfed220; // RZFastFixed0x58
    static rz::cRZFastFixedAllocator2<0x58> &Pool() { return ts2_rva_fed220; }

    bool QueryInterface(uint32_t riid, void **out) override;
    uint32_t AddRef() override { return ++mRefs; }
    uint32_t Release() override;
    void ReleaseResource() override;
    cIDeviceDX *GetDevice() override { return mDevice; }
    cIDeviceDX *GetDeviceConst() override { return mDevice; }
    bool IsCreated() override { return mDeclaration != nullptr; }
    uint32_t GetMemorySize() override;
    bool SurvivesReset() override { return false; }
    bool SurvivesModeChange() override { return false; }
    uint32_t GetId() override { return mId; }
    uint32_t GetStreamSize(uint32_t stream) override { return mStreamSizes[stream]; }
    cIVertexFormat *GetFormat() override { return mFormat; }
    cIDeviceDX *GetDeviceDX() override { return mDevice; }
    IDirect3DVertexDeclaration9 *GetD3DDeclaration() override { return mDeclaration; }
    ~cVertexDeclarationDX() override;

    static void *operator new(size_t size);
    static void operator delete(void *block, size_t size);

    uint32_t mRefs = 0;                                  // +4
    uint32_t mId = 0;                                    // +8
    cIDeviceDX *mDevice = nullptr;                       // +0xc (not counted)
    IDirect3DVertexDeclaration9 *mDeclaration = nullptr; // +0x10 (counted)
    uint32_t mStreamSizes[16];                           // +0x14 (set by the manager when it makes the declaration)
    cIVertexFormat *mFormat = nullptr;                   // +0x54 (counted)

private:
    void Drop();
};
static_assert(sizeof(cVertexDeclarationDX) == 0x58, "cVertexDeclarationDX size");

class cVertexDeclarationsManagerDX final : public cIVertexDeclarationsManagerDX {
public:
    static constexpr uint32_t kGetterRva = 0x16e760;
    static cVertexDeclarationsManagerDX *Instance(); // the getter (hooked)

    bool QueryInterface(uint32_t riid, void **out) override;
    uint32_t AddRef() override { return 1; }
    uint32_t Release() override { return 1; }
    bool GetDeclaration(cIDeviceDX *device, cIVertexFormat *format, cIVertexDeclarationDX **out) override;
    uint32_t GetDeclarationCount() override { return static_cast<uint32_t>(mDeclarations.size()); }
    cIVertexDeclarationDX *GetDeclarationAt(uint32_t id) override { return mDeclarations[id]; }
    const char *const *GetSemanticRenames() override;
    ~cVertexDeclarationsManagerDX() override = default;

    cVertexDeclarationsManagerDX();
    cVertexDeclarationsManagerDX(const cVertexDeclarationsManagerDX &) = delete;
    cVertexDeclarationsManagerDX &operator=(const cVertexDeclarationsManagerDX &) = delete;

    std::vector<cIVertexDeclarationDX *> mDeclarations; // +4: not counted (a declaration's release empties its entry)
    std::vector<D3DVERTEXELEMENT9> mElements;            // +0x10: scratch for the declaration being made
};
static_assert(sizeof(cVertexDeclarationsManagerDX) == 0x1c, "cVertexDeclarationsManagerDX size");

} // namespace nGZGraphic4
