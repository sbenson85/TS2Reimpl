#pragma once
// Shared by host/shared/d3dlog_tracker.cpp (resources, names, uploads) and host/shared/d3dlog_device.cpp (the
// wrappers). See host/shared/d3dlog.h.
#include "d3dlog.h"
#include "devlog.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d3dlog {

// One record under construction: the call's name, then its arguments as space-separated tokens.
class Call {
public:
    explicit Call(const char *name) : mText(name) {}
    Call &U(uint32_t v);
    Call &I(int32_t v);
    Call &X(uint32_t v);
    Call &F(float v);
    Call &Floats(const float *values, uint32_t count); // [a b c] or null
    Call &Ints(const int *values, uint32_t count);
    Call &H(uint64_t hash);                             // #<12 hex>
    Call &Bytes(const void *data, size_t size);         // #<hash of the bytes> or null
    Call &S(std::string_view token);
    Call &Rect(const RECT *rect);                       // x0,y0,x1,y1 or null
    Call &Present(const D3DPRESENT_PARAMETERS *pp);
    Call &Hr(HRESULT hr);                               // =0x... unless D3D_OK
    const std::string &Text() const { return mText; }

private:
    std::string mText;
};

// Static buffers' shadow copies, in all, at most this many bytes (the 32-bit game's address space is shared).
constexpr uint64_t kShadowBudget = 96ull << 20;

// What the log knows about one object created through the wrapper.
struct Resource {
    char kind = '?';       // V I T C U (volume texture) S Z O (surfaces) B (back buffer) v p d b q
    bool dynamic = false;  // a D3DUSAGE_DYNAMIC buffer: named by order
    bool shadowed = false; // a buffer whose bytes are kept (dynamic ones always, static ones within the budget)
    uint64_t desc = 0;     // hash of the creation parameters (shaders, declarations: of their contents)
    uint64_t content = 0;  // textures (and unshadowed static buffers): hash of the uploads so far
    uint32_t ordinal = 0;  // the n-th object of its kind and desc (dynamic buffers, surfaces, state blocks, queries)
    uint32_t size = 0;     // buffers: bytes
    uint32_t indexSize = 0;
    std::vector<uint8_t> shadow; // shadowed buffers: the bytes as last uploaded
};

// A draw's content identity (format 2): what an indexed draw takes from its index buffer.
struct IndexedRange {
    uint32_t low = 0, high = 0; // the smallest and largest index drawn
    uint64_t hash = 0;          // of the indices minus `low`, as 32-bit values
};

// A lock in progress.
struct PendingLock {
    uint8_t *data = nullptr;
    uint32_t offset = 0, size = 0; // buffers (size 0: to the end)
    RECT rect{};                   // surfaces and texture levels
    bool hasRect = false;
    uint32_t pitch = 0;
    DWORD flags = 0;
};

// The draw-time view of the device's stream sources and index buffer.
struct StreamBinding {
    const void *buffer = nullptr;
    uint32_t offset = 0, stride = 0;
};

class Tracker {
public:
    explicit Tracker(devlog::Recorder &recorder) : mRecorder(recorder) {}

    void Log(const Call &call, devlog::Kind kind, bool draw = false, bool present = false);
    devlog::Recorder &Recorder() { return mRecorder; }

    // Registers an object the wrapper created (or got: back buffers) and patches its class's lock slots.
    void Register(const void *object, char kind, uint64_t desc, bool dynamic = false, uint32_t size = 0,
                  uint32_t indexSize = 0);
    void RegisterTexture(IDirect3DTexture9 *texture, uint64_t desc, bool dynamic);
    void RegisterCube(IDirect3DCubeTexture9 *cube, uint64_t desc, bool dynamic);
    void RegisterBuffer(void *buffer, char kind, uint64_t desc, bool dynamic, uint32_t size, uint32_t indexSize);
    void RegisterSurface(IDirect3DSurface9 *surface, char kind, uint64_t desc);

    std::string Name(const void *object, char expected); // expected: 'T' texture, 'S' surface, 'V'/'I' buffer, other
    // Mixes `what` into the content hash of a texture or surface (UpdateTexture, UpdateSurface).
    void Derive(const void *object, char expected, uint64_t what);

    bool IsDynamic(const void *buffer);
    bool IsShadowed(const void *buffer);
    // Hash of bytes [offset, offset+size) of a shadowed buffer (clipped), or 0 when it isn't shadowed.
    uint64_t ShadowHash(const void *buffer, uint64_t offset, uint64_t size);
    // The indices [start, start+count) of a shadowed index buffer (clipped); false when it isn't shadowed or the range
    // is empty.
    bool Indices(const void *buffer, uint32_t start, uint32_t count, IndexedRange &out);
    uint32_t IndexSize(const void *buffer);

    // Lock slot callbacks (host/shared/d3dlog_tracker.cpp's vtable patches).
    void OnBufferLock(void *buffer, UINT offset, UINT size, void *data, DWORD flags);
    void OnBufferUnlock(void *buffer);
    void OnRectLock(void *object, uint32_t sub, const D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags);
    void OnTextureUnlock(IDirect3DBaseTexture9 *texture, uint32_t face, uint32_t level, bool cube);
    void OnSurfaceUnlock(IDirect3DSurface9 *surface);

    StreamBinding streams[16];
    const void *indices = nullptr;

private:
    std::string NameOf(const Resource &r) const;
    std::string FallbackName(const void *object, char expected);
    uint64_t RectHash(const PendingLock &lock, const D3DSURFACE_DESC &desc, uint64_t fallback);
    bool TakeLock(const void *object, uint32_t sub, PendingLock &out);

    devlog::Recorder &mRecorder;
    std::mutex mLock;
    std::unordered_map<const void *, Resource> mResources;
    std::map<std::pair<const void *, uint32_t>, PendingLock> mLocks;
    std::map<std::pair<char, uint64_t>, uint32_t> mOrdinals;
    uint64_t mShadowBytes = 0; // static buffers' shadows
};

// The tracker the patched lock slots report to (one device at a time).
extern Tracker *gTracker;

// Bytes per block and block size (1 for uncompressed formats) of a surface format; false when unknown.
bool FormatLayout(D3DFORMAT format, uint32_t &blockBytes, uint32_t &blockSize);
// Vertices or indices a primitive count takes.
uint32_t VertexCount(D3DPRIMITIVETYPE type, uint32_t primitives);
// Length in DWORDs of shader byte code (up to and including the end token), at most `limit`.
uint32_t ShaderLength(const DWORD *code, uint32_t limit = 0x10000);
// Number of elements of a vertex declaration, the D3DDECL_END entry included.
uint32_t DeclarationLength(const D3DVERTEXELEMENT9 *elements, uint32_t limit = 64);

} // namespace d3dlog
