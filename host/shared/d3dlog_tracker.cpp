// The device call log's resource side (host/shared/d3dlog.h): names, uploads seen through the patched lock slots,
// dynamic buffer shadows, and the record builder.
#define NOMINMAX
#include <windows.h>
#include <initguid.h> // this file defines d3d9.h's interface ids (IID_IDirect3DBaseTexture9 for GetContainer)

#include "d3dlog_internal.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace d3dlog {

Tracker *gTracker = nullptr;

// ---- records ----

Call &Call::U(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof(b), " %u", v);
    mText += b;
    return *this;
}

Call &Call::I(int32_t v)
{
    char b[16];
    std::snprintf(b, sizeof(b), " %d", v);
    mText += b;
    return *this;
}

Call &Call::X(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof(b), " 0x%x", v);
    mText += b;
    return *this;
}

Call &Call::F(float v)
{
    char b[32];
    std::snprintf(b, sizeof(b), " %.9g", static_cast<double>(v));
    mText += b;
    return *this;
}

Call &Call::Floats(const float *values, uint32_t count)
{
    if (!values) return S("null");
    mText += " [";
    for (uint32_t i = 0; i < count; ++i) {
        char b[32];
        std::snprintf(b, sizeof(b), i ? " %.9g" : "%.9g", static_cast<double>(values[i]));
        mText += b;
    }
    mText += ']';
    return *this;
}

Call &Call::Ints(const int *values, uint32_t count)
{
    if (!values) return S("null");
    mText += " [";
    for (uint32_t i = 0; i < count; ++i) {
        char b[16];
        std::snprintf(b, sizeof(b), i ? " %d" : "%d", values[i]);
        mText += b;
    }
    mText += ']';
    return *this;
}

Call &Call::H(uint64_t hash)
{
    char b[24];
    std::snprintf(b, sizeof(b), " #%012llx", static_cast<unsigned long long>(hash & 0xffffffffffffull));
    mText += b;
    return *this;
}

Call &Call::Bytes(const void *data, size_t size)
{
    if (!data) return S("null");
    return H(devlog::HashGuarded(data, size, 0xbad));
}

Call &Call::S(std::string_view token)
{
    mText += ' ';
    mText.append(token.data(), token.size());
    return *this;
}

Call &Call::Rect(const RECT *rect)
{
    if (!rect) return S("null");
    char b[64];
    std::snprintf(b, sizeof(b), " %ld,%ld,%ld,%ld", rect->left, rect->top, rect->right, rect->bottom);
    mText += b;
    return *this;
}

// Everything but the window handle (a pointer) and the refresh rate the system picked.
Call &Call::Present(const D3DPRESENT_PARAMETERS *pp)
{
    if (!pp) return S("null");
    U(pp->BackBufferWidth).U(pp->BackBufferHeight).U(pp->BackBufferFormat).U(pp->BackBufferCount);
    U(pp->MultiSampleType).U(pp->MultiSampleQuality).U(pp->SwapEffect).U(pp->Windowed);
    U(pp->EnableAutoDepthStencil).U(pp->AutoDepthStencilFormat).X(pp->Flags).U(pp->FullScreen_RefreshRateInHz);
    return X(pp->PresentationInterval);
}

Call &Call::Hr(HRESULT hr)
{
    if (hr == D3D_OK) return *this;
    char b[24];
    std::snprintf(b, sizeof(b), " =0x%08lx", static_cast<unsigned long>(hr));
    mText += b;
    return *this;
}

void Tracker::Log(const Call &call, devlog::Kind kind, bool draw, bool present)
{
    mRecorder.Record(kind, call.Text(), draw, present);
}

// ---- formats and counts ----

bool FormatLayout(D3DFORMAT format, uint32_t &blockBytes, uint32_t &blockSize)
{
    blockSize = 1;
    switch (format) {
    case D3DFMT_DXT1: blockBytes = 8; blockSize = 4; return true;
    case D3DFMT_DXT2: case D3DFMT_DXT3: case D3DFMT_DXT4: case D3DFMT_DXT5: blockBytes = 16; blockSize = 4; return true;
    case D3DFMT_A8: case D3DFMT_L8: case D3DFMT_P8: case D3DFMT_A4L4: case D3DFMT_R3G3B2: blockBytes = 1; return true;
    case D3DFMT_R5G6B5: case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5: case D3DFMT_A4R4G4B4: case D3DFMT_X4R4G4B4:
    case D3DFMT_A8R3G3B2: case D3DFMT_A8L8: case D3DFMT_A8P8: case D3DFMT_V8U8: case D3DFMT_L6V5U5: case D3DFMT_L16:
    case D3DFMT_D16: case D3DFMT_D16_LOCKABLE: case D3DFMT_D15S1: case D3DFMT_R16F:
        blockBytes = 2; return true;
    case D3DFMT_R8G8B8: blockBytes = 3; return true;
    case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: case D3DFMT_A8B8G8R8: case D3DFMT_X8B8G8R8: case D3DFMT_A2R10G10B10:
    case D3DFMT_A2B10G10R10: case D3DFMT_G16R16: case D3DFMT_X8L8V8U8: case D3DFMT_Q8W8V8U8: case D3DFMT_V16U16:
    case D3DFMT_A2W10V10U10: case D3DFMT_D32: case D3DFMT_D24S8: case D3DFMT_D24X8: case D3DFMT_D24X4S4:
    case D3DFMT_D32F_LOCKABLE: case D3DFMT_R32F: case D3DFMT_G16R16F:
        blockBytes = 4; return true;
    case D3DFMT_A16B16G16R16: case D3DFMT_Q16W16V16U16: case D3DFMT_A16B16G16R16F: case D3DFMT_G32R32F:
        blockBytes = 8; return true;
    case D3DFMT_A32B32G32R32F: blockBytes = 16; return true;
    default: return false;
    }
}

uint32_t VertexCount(D3DPRIMITIVETYPE type, uint32_t primitives)
{
    switch (type) {
    case D3DPT_POINTLIST: return primitives;
    case D3DPT_LINELIST: return primitives * 2;
    case D3DPT_LINESTRIP: return primitives + 1;
    case D3DPT_TRIANGLELIST: return primitives * 3;
    case D3DPT_TRIANGLESTRIP: case D3DPT_TRIANGLEFAN: return primitives + 2;
    default: return 0;
    }
}

uint32_t ShaderLength(const DWORD *code, uint32_t limit)
{
    if (!code) return 0;
    // Token 0 is the version; comments (0xfffe, length in bits 16..30) are skipped whole; 0x0000ffff ends it.
    for (uint32_t i = 1; i < limit;) {
        const DWORD t = code[i];
        if (t == 0x0000ffff) return i + 1;
        if ((t & 0xffff) == 0xfffe) i += 1 + ((t >> 16) & 0x7fff);
        else ++i;
    }
    return limit;
}

uint32_t DeclarationLength(const D3DVERTEXELEMENT9 *elements, uint32_t limit)
{
    if (!elements) return 0;
    for (uint32_t i = 0; i < limit; ++i)
        if (elements[i].Stream == 0xff) return i + 1;
    return limit;
}

// ---- lock slot patches ----

namespace {

constexpr int kBufferLock = 11, kBufferUnlock = 12;  // IDirect3DVertexBuffer9 / IDirect3DIndexBuffer9
constexpr int kTextureLock = 19, kTextureUnlock = 20; // IDirect3DTexture9 / IDirect3DCubeTexture9 LockRect, UnlockRect
constexpr int kSurfaceLock = 13, kSurfaceUnlock = 14; // IDirect3DSurface9 LockRect, UnlockRect

struct Patch {
    void **vtable;
    int lockSlot, unlockSlot;
    void *lock, *unlock; // the originals
};
constexpr int kMaxPatches = 64;
Patch gPatches[kMaxPatches];
std::atomic<int> gPatchCount{0};
std::mutex gPatchLock;

void *Original(const void *self, bool lock)
{
    void **vtable = *static_cast<void **const *>(self);
    const int n = gPatchCount.load();
    for (int i = 0; i < n; ++i)
        if (gPatches[i].vtable == vtable) return lock ? gPatches[i].lock : gPatches[i].unlock;
    return nullptr; // never: every object reaching a thunk has a patched vtable
}

// Set while a texture's own LockRect/UnlockRect runs: a runtime that locks the level's surface through its vtable
// must not be counted twice.
thread_local int tInTextureLock = 0;

HRESULT STDMETHODCALLTYPE BufferLock(void *self, UINT offset, UINT size, void **data, DWORD flags)
{
    auto original = reinterpret_cast<HRESULT(STDMETHODCALLTYPE *)(void *, UINT, UINT, void **, DWORD)>(Original(self, true));
    const HRESULT hr = original(self, offset, size, data, flags);
    if (SUCCEEDED(hr) && data && gTracker) gTracker->OnBufferLock(self, offset, size, *data, flags);
    return hr;
}

HRESULT STDMETHODCALLTYPE BufferUnlock(void *self)
{
    if (gTracker) gTracker->OnBufferUnlock(self); // before the real Unlock: the bytes are only readable while locked
    return reinterpret_cast<HRESULT(STDMETHODCALLTYPE *)(void *)>(Original(self, false))(self);
}

HRESULT STDMETHODCALLTYPE TextureLockRect(IDirect3DTexture9 *self, UINT level, D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags)
{
    auto original = reinterpret_cast<HRESULT(STDMETHODCALLTYPE *)(IDirect3DTexture9 *, UINT, D3DLOCKED_RECT *, const RECT *, DWORD)>(
        Original(self, true));
    ++tInTextureLock;
    const HRESULT hr = original(self, level, locked, rect, flags);
    --tInTextureLock;
    if (SUCCEEDED(hr) && locked && gTracker) gTracker->OnRectLock(self, 0x10000 | level, locked, rect, flags);
    return hr;
}

HRESULT STDMETHODCALLTYPE TextureUnlockRect(IDirect3DTexture9 *self, UINT level)
{
    if (gTracker) gTracker->OnTextureUnlock(self, 0, level, false);
    ++tInTextureLock;
    const HRESULT hr = reinterpret_cast<HRESULT(STDMETHODCALLTYPE *)(IDirect3DTexture9 *, UINT)>(Original(self, false))(self, level);
    --tInTextureLock;
    return hr;
}

HRESULT STDMETHODCALLTYPE CubeLockRect(IDirect3DCubeTexture9 *self, D3DCUBEMAP_FACES face, UINT level, D3DLOCKED_RECT *locked,
                                       const RECT *rect, DWORD flags)
{
    auto original = reinterpret_cast<HRESULT(STDMETHODCALLTYPE *)(IDirect3DCubeTexture9 *, D3DCUBEMAP_FACES, UINT,
                                                                   D3DLOCKED_RECT *, const RECT *, DWORD)>(Original(self, true));
    ++tInTextureLock;
    const HRESULT hr = original(self, face, level, locked, rect, flags);
    --tInTextureLock;
    if (SUCCEEDED(hr) && locked && gTracker)
        gTracker->OnRectLock(self, 0x10000 | static_cast<uint32_t>(face) << 8 | level, locked, rect, flags);
    return hr;
}

HRESULT STDMETHODCALLTYPE CubeUnlockRect(IDirect3DCubeTexture9 *self, D3DCUBEMAP_FACES face, UINT level)
{
    if (gTracker) gTracker->OnTextureUnlock(self, static_cast<uint32_t>(face), level, true);
    ++tInTextureLock;
    const HRESULT hr = reinterpret_cast<HRESULT(STDMETHODCALLTYPE *)(IDirect3DCubeTexture9 *, D3DCUBEMAP_FACES, UINT)>(
        Original(self, false))(self, face, level);
    --tInTextureLock;
    return hr;
}

HRESULT STDMETHODCALLTYPE SurfaceLockRect(IDirect3DSurface9 *self, D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags)
{
    auto original = reinterpret_cast<HRESULT(STDMETHODCALLTYPE *)(IDirect3DSurface9 *, D3DLOCKED_RECT *, const RECT *, DWORD)>(
        Original(self, true));
    const HRESULT hr = original(self, locked, rect, flags);
    if (SUCCEEDED(hr) && locked && gTracker && !tInTextureLock) gTracker->OnRectLock(self, 0x20000, locked, rect, flags);
    return hr;
}

HRESULT STDMETHODCALLTYPE SurfaceUnlockRect(IDirect3DSurface9 *self)
{
    if (gTracker && !tInTextureLock) gTracker->OnSurfaceUnlock(self);
    return reinterpret_cast<HRESULT(STDMETHODCALLTYPE *)(IDirect3DSurface9 *)>(Original(self, false))(self);
}

// Patches the lock slots of `object`'s class, once per vtable.
void PatchClass(const void *object, int lockSlot, void *lock, int unlockSlot, void *unlock)
{
    if (!object) return;
    void **vtable = *static_cast<void **const *>(object);
    std::lock_guard<std::mutex> guard(gPatchLock);
    const int n = gPatchCount.load();
    for (int i = 0; i < n; ++i)
        if (gPatches[i].vtable == vtable) return;
    if (n == kMaxPatches) return;
    // Published before the slots change, so a thunk always finds its originals.
    gPatches[n] = {vtable, lockSlot, unlockSlot, vtable[lockSlot], vtable[unlockSlot]};
    gPatchCount.store(n + 1);
    const int low = lockSlot < unlockSlot ? lockSlot : unlockSlot, high = lockSlot < unlockSlot ? unlockSlot : lockSlot;
    DWORD old;
    VirtualProtect(&vtable[low], (high - low + 1) * sizeof(void *), PAGE_READWRITE, &old);
    vtable[lockSlot] = lock;
    vtable[unlockSlot] = unlock;
    VirtualProtect(&vtable[low], (high - low + 1) * sizeof(void *), old, &old);
}

bool CopyGuarded(void *to, const void *from, size_t size)
{
    __try {
        std::memcpy(to, from, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string Hex12(uint64_t v)
{
    char b[16];
    std::snprintf(b, sizeof(b), "%012llx", static_cast<unsigned long long>(v & 0xffffffffffffull));
    return b;
}

uint64_t DescHash(const void *desc, size_t size)
{
    return devlog::Hash(desc, size);
}

} // namespace

void ResetForTests()
{
    std::lock_guard<std::mutex> guard(gPatchLock);
    const int n = gPatchCount.load();
    for (int i = n - 1; i >= 0; --i) {
        Patch &p = gPatches[i];
        DWORD old;
        VirtualProtect(&p.vtable[p.lockSlot], sizeof(void *), PAGE_READWRITE, &old);
        p.vtable[p.lockSlot] = p.lock;
        VirtualProtect(&p.vtable[p.lockSlot], sizeof(void *), old, &old);
        VirtualProtect(&p.vtable[p.unlockSlot], sizeof(void *), PAGE_READWRITE, &old);
        p.vtable[p.unlockSlot] = p.unlock;
        VirtualProtect(&p.vtable[p.unlockSlot], sizeof(void *), old, &old);
    }
    gPatchCount.store(0);
    gTracker = nullptr;
}

// ---- registration and names ----

void Tracker::Register(const void *object, char kind, uint64_t desc, bool dynamic, uint32_t size, uint32_t indexSize)
{
    if (!object) return;
    std::lock_guard<std::mutex> guard(mLock);
    Resource r;
    r.kind = kind;
    r.dynamic = dynamic;
    r.desc = desc;
    r.size = size;
    r.indexSize = indexSize;
    if (dynamic || kind == 'S' || kind == 'Z' || kind == 'b' || kind == 'q') r.ordinal = mOrdinals[{kind, desc}]++;
    // A new object at a released one's address: the old shadow's bytes go back to the budget.
    auto old = mResources.find(object);
    if (old != mResources.end() && old->second.shadowed && !old->second.dynamic) mShadowBytes -= old->second.shadow.size();
    if ((kind == 'V' || kind == 'I') && (dynamic || mShadowBytes + size <= kShadowBudget)) {
        r.shadowed = true;
        r.shadow.assign(size, 0);
        if (!dynamic) mShadowBytes += size;
    }
    mResources[object] = std::move(r);
}

void Tracker::RegisterBuffer(void *buffer, char kind, uint64_t desc, bool dynamic, uint32_t size, uint32_t indexSize)
{
    if (!buffer) return;
    Register(buffer, kind, desc, dynamic, size, indexSize);
    PatchClass(buffer, kBufferLock, reinterpret_cast<void *>(&BufferLock), kBufferUnlock, reinterpret_cast<void *>(&BufferUnlock));
}

void Tracker::RegisterSurface(IDirect3DSurface9 *surface, char kind, uint64_t desc)
{
    if (!surface) return;
    Register(surface, kind, desc);
    PatchClass(surface, kSurfaceLock, reinterpret_cast<void *>(&SurfaceLockRect), kSurfaceUnlock,
               reinterpret_cast<void *>(&SurfaceUnlockRect));
}

void Tracker::RegisterTexture(IDirect3DTexture9 *texture, uint64_t desc, bool dynamic)
{
    if (!texture) return;
    Register(texture, 'T', desc);
    (void)dynamic; // dynamic textures are content-named like the rest; their uploads go to the stream
    PatchClass(texture, kTextureLock, reinterpret_cast<void *>(&TextureLockRect), kTextureUnlock,
               reinterpret_cast<void *>(&TextureUnlockRect));
    // Its levels' surfaces may be another class: patch that too (D3DX fills levels through them).
    IDirect3DSurface9 *level = nullptr;
    if (SUCCEEDED(texture->GetSurfaceLevel(0, &level)) && level) {
        PatchClass(level, kSurfaceLock, reinterpret_cast<void *>(&SurfaceLockRect), kSurfaceUnlock,
                   reinterpret_cast<void *>(&SurfaceUnlockRect));
        level->Release();
    }
}

void Tracker::RegisterCube(IDirect3DCubeTexture9 *cube, uint64_t desc, bool dynamic)
{
    if (!cube) return;
    Register(cube, 'C', desc);
    (void)dynamic;
    PatchClass(cube, kTextureLock, reinterpret_cast<void *>(&CubeLockRect), kTextureUnlock, reinterpret_cast<void *>(&CubeUnlockRect));
    IDirect3DSurface9 *face = nullptr;
    if (SUCCEEDED(cube->GetCubeMapSurface(D3DCUBEMAP_FACE_POSITIVE_X, 0, &face)) && face) {
        PatchClass(face, kSurfaceLock, reinterpret_cast<void *>(&SurfaceLockRect), kSurfaceUnlock,
                   reinterpret_cast<void *>(&SurfaceUnlockRect));
        face->Release();
    }
}

std::string Tracker::NameOf(const Resource &r) const
{
    char b[64];
    switch (r.kind) {
    case 'V': case 'I':
        if (r.dynamic) {
            std::snprintf(b, sizeof(b), "%cd%u:", r.kind, r.ordinal);
            return b + Hex12(r.desc);
        }
        // Format 2: by parameters only (the draws carry the content); over the budget, by content as in format 1.
        if (r.shadowed) return std::string(1, r.kind) + ":" + Hex12(r.desc);
        [[fallthrough]];
    case 'T': case 'C': case 'U': case 'O':
        return std::string(1, r.kind) + ":" + Hex12(devlog::Mix(r.desc, r.content));
    case 'S': case 'Z':
        std::snprintf(b, sizeof(b), "%c%u:", r.kind, r.ordinal);
        return b + Hex12(r.desc);
    case 'B':
        std::snprintf(b, sizeof(b), "BB%u", static_cast<uint32_t>(r.desc));
        return b;
    case 'b': case 'q':
        std::snprintf(b, sizeof(b), "%c%u.%u", r.kind, static_cast<uint32_t>(r.desc), r.ordinal);
        return b;
    default:
        return std::string(1, r.kind) + ":" + Hex12(r.desc);
    }
}

std::string Tracker::FallbackName(const void *object, char expected)
{
    auto *unknown = const_cast<void *>(object);
    switch (expected) {
    case 'T': {
        auto *texture = static_cast<IDirect3DBaseTexture9 *>(unknown);
        D3DSURFACE_DESC d{};
        switch (texture->GetType()) {
        case D3DRTYPE_TEXTURE:
            static_cast<IDirect3DTexture9 *>(texture)->GetLevelDesc(0, &d);
            return "T?:" + Hex12(DescHash(&d, sizeof(d)));
        case D3DRTYPE_CUBETEXTURE:
            static_cast<IDirect3DCubeTexture9 *>(texture)->GetLevelDesc(0, &d);
            return "C?:" + Hex12(DescHash(&d, sizeof(d)));
        case D3DRTYPE_VOLUMETEXTURE: return "U?";
        default: return "?";
        }
    }
    case 'S': {
        auto *surface = static_cast<IDirect3DSurface9 *>(unknown);
        D3DSURFACE_DESC d{};
        surface->GetDesc(&d);
        IDirect3DBaseTexture9 *container = nullptr;
        if (SUCCEEDED(surface->GetContainer(IID_IDirect3DBaseTexture9, reinterpret_cast<void **>(&container))) && container) {
            char size[32];
            std::snprintf(size, sizeof(size), "/%ux%u", d.Width, d.Height);
            std::string name = Name(container, 'T') + size;
            container->Release();
            return name;
        }
        return "S?:" + Hex12(DescHash(&d, sizeof(d)));
    }
    case 'V': {
        D3DVERTEXBUFFER_DESC d{};
        static_cast<IDirect3DVertexBuffer9 *>(unknown)->GetDesc(&d);
        return "V?:" + Hex12(DescHash(&d, sizeof(d)));
    }
    case 'I': {
        D3DINDEXBUFFER_DESC d{};
        static_cast<IDirect3DIndexBuffer9 *>(unknown)->GetDesc(&d);
        return "I?:" + Hex12(DescHash(&d, sizeof(d)));
    }
    default: return "?";
    }
}

std::string Tracker::Name(const void *object, char expected)
{
    if (!object) return "0";
    {
        std::lock_guard<std::mutex> guard(mLock);
        auto it = mResources.find(object);
        if (it != mResources.end()) return NameOf(it->second);
    }
    return FallbackName(object, expected);
}

void Tracker::Derive(const void *object, char expected, uint64_t what)
{
    if (!object) return;
    {
        std::lock_guard<std::mutex> guard(mLock);
        auto it = mResources.find(object);
        if (it != mResources.end()) {
            it->second.content = devlog::Mix(it->second.content, what);
            return;
        }
    }
    if (expected != 'S') return;
    IDirect3DBaseTexture9 *container = nullptr;
    auto *surface = static_cast<IDirect3DSurface9 *>(const_cast<void *>(object));
    if (FAILED(surface->GetContainer(IID_IDirect3DBaseTexture9, reinterpret_cast<void **>(&container))) || !container) return;
    {
        std::lock_guard<std::mutex> guard(mLock);
        auto it = mResources.find(container);
        if (it != mResources.end()) it->second.content = devlog::Mix(it->second.content, what);
    }
    container->Release();
}

bool Tracker::IsDynamic(const void *buffer)
{
    std::lock_guard<std::mutex> guard(mLock);
    auto it = mResources.find(buffer);
    return it != mResources.end() && it->second.dynamic;
}

bool Tracker::IsShadowed(const void *buffer)
{
    std::lock_guard<std::mutex> guard(mLock);
    auto it = mResources.find(buffer);
    return it != mResources.end() && it->second.shadowed;
}

uint64_t Tracker::ShadowHash(const void *buffer, uint64_t offset, uint64_t size)
{
    std::lock_guard<std::mutex> guard(mLock);
    auto it = mResources.find(buffer);
    if (it == mResources.end() || !it->second.shadowed) return 0;
    const std::vector<uint8_t> &s = it->second.shadow;
    if (offset > s.size()) offset = s.size();
    if (size > s.size() - offset) size = s.size() - offset;
    return devlog::Hash(s.data() + offset, static_cast<size_t>(size));
}

bool Tracker::Indices(const void *buffer, uint32_t start, uint32_t count, IndexedRange &out)
{
    std::lock_guard<std::mutex> guard(mLock);
    auto it = mResources.find(buffer);
    if (it == mResources.end() || !it->second.shadowed) return false;
    const Resource &r = it->second;
    const uint32_t size = r.indexSize == 4 ? 4 : 2;
    const uint64_t available = r.shadow.size() / size;
    if (start >= available || !count) return false;
    if (count > available - start) count = static_cast<uint32_t>(available - start);
    std::vector<uint32_t> values(count);
    const uint8_t *p = r.shadow.data() + static_cast<size_t>(start) * size;
    for (uint32_t i = 0; i < count; ++i) {
        if (size == 4) std::memcpy(&values[i], p + i * 4, 4);
        else {
            uint16_t v;
            std::memcpy(&v, p + i * 2, 2);
            values[i] = v;
        }
    }
    uint32_t low = values[0], high = values[0];
    for (uint32_t v : values) low = v < low ? v : low, high = v > high ? v : high;
    for (uint32_t &v : values) v -= low;
    out.low = low;
    out.high = high;
    out.hash = devlog::Hash(values.data(), values.size() * sizeof(uint32_t));
    return true;
}

uint32_t Tracker::IndexSize(const void *buffer)
{
    std::lock_guard<std::mutex> guard(mLock);
    auto it = mResources.find(buffer);
    return it != mResources.end() && it->second.indexSize ? it->second.indexSize : 2;
}

// ---- uploads ----

bool Tracker::TakeLock(const void *object, uint32_t sub, PendingLock &out)
{
    std::lock_guard<std::mutex> guard(mLock);
    auto it = mLocks.find({object, sub});
    if (it == mLocks.end()) return false;
    out = it->second;
    mLocks.erase(it);
    return true;
}

void Tracker::OnBufferLock(void *buffer, UINT offset, UINT size, void *data, DWORD flags)
{
    PendingLock lock;
    lock.data = static_cast<uint8_t *>(data);
    lock.offset = offset;
    lock.size = size;
    lock.flags = flags;
    std::lock_guard<std::mutex> guard(mLock);
    mLocks[{buffer, 0}] = lock;
}

void Tracker::OnBufferUnlock(void *buffer)
{
    PendingLock lock;
    if (!TakeLock(buffer, 0, lock) || (lock.flags & D3DLOCK_READONLY)) return;
    // The buffer's size: ours, or its description's (D3DVERTEXBUFFER_DESC and D3DINDEXBUFFER_DESC both keep it at +16).
    uint32_t total = 0;
    char kind = 'V';
    {
        std::lock_guard<std::mutex> guard(mLock);
        auto it = mResources.find(buffer);
        if (it != mResources.end()) total = it->second.size, kind = it->second.kind;
    }
    if (!total) {
        auto *resource = static_cast<IDirect3DResource9 *>(buffer);
        if (resource->GetType() == D3DRTYPE_INDEXBUFFER) {
            D3DINDEXBUFFER_DESC d{};
            static_cast<IDirect3DIndexBuffer9 *>(buffer)->GetDesc(&d);
            total = d.Size, kind = 'I';
        } else {
            D3DVERTEXBUFFER_DESC d{};
            static_cast<IDirect3DVertexBuffer9 *>(buffer)->GetDesc(&d);
            total = d.Size;
        }
    }
    uint32_t bytes = lock.size;
    if (lock.offset == 0 && lock.size == 0) bytes = total;
    else if (lock.size == 0) bytes = total > lock.offset ? total - lock.offset : 0;
    if (lock.offset > total) bytes = 0;
    else if (bytes > total - lock.offset) bytes = total - lock.offset;
    const uint64_t hash = devlog::HashGuarded(lock.data, bytes, 0xbad);
    std::string before, after;
    bool dynamic = false, known = false, shadowed = false;
    {
        std::lock_guard<std::mutex> guard(mLock);
        auto it = mResources.find(buffer);
        if (it != mResources.end()) {
            Resource &r = it->second;
            known = true;
            dynamic = r.dynamic;
            shadowed = r.shadowed && lock.offset + static_cast<uint64_t>(bytes) <= r.shadow.size();
            before = NameOf(r);
            if (shadowed) {
                if (!CopyGuarded(r.shadow.data() + lock.offset, lock.data, bytes)) std::memset(r.shadow.data() + lock.offset, 0, bytes);
            } else {
                r.content = devlog::Mix(r.content, devlog::Mix(static_cast<uint64_t>(lock.offset) << 32 | bytes, hash));
            }
            after = NameOf(r);
        }
    }
    if (!known) before = after = FallbackName(buffer, kind);
    Call call("Upload");
    call.S(after);
    if (shadowed) {
        // Format 2: where in the buffer, and the ring's DISCARD/NOOVERWRITE, follow the buffer's history; the draws
        // say what was used.
        call.U(bytes).X(lock.flags & ~static_cast<DWORD>(D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE)).H(hash);
    } else {
        if (!dynamic) call.S("from").S(before);
        call.U(lock.offset).U(bytes).X(lock.flags).H(hash);
    }
    Log(call, dynamic ? devlog::Kind::Stream : devlog::Kind::Async);
}

void Tracker::OnRectLock(void *object, uint32_t sub, const D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags)
{
    PendingLock lock;
    lock.data = static_cast<uint8_t *>(locked->pBits);
    lock.pitch = static_cast<uint32_t>(locked->Pitch);
    lock.flags = flags;
    if (rect) lock.rect = *rect, lock.hasRect = true;
    std::lock_guard<std::mutex> guard(mLock);
    mLocks[{object, sub}] = lock;
}

uint64_t Tracker::RectHash(const PendingLock &lock, const D3DSURFACE_DESC &desc, uint64_t fallback)
{
    uint32_t blockBytes = 0, blockSize = 1;
    const bool known = FormatLayout(desc.Format, blockBytes, blockSize);
    const uint32_t width = lock.hasRect ? static_cast<uint32_t>(lock.rect.right - lock.rect.left) : desc.Width;
    const uint32_t height = lock.hasRect ? static_cast<uint32_t>(lock.rect.bottom - lock.rect.top) : desc.Height;
    const uint32_t rows = (height + blockSize - 1) / blockSize;
    // An unknown format hashes whole pitches (padding included).
    const uint32_t rowBytes = known ? (width + blockSize - 1) / blockSize * blockBytes : lock.pitch;
    uint64_t h = devlog::Mix(rows, rowBytes);
    for (uint32_t y = 0; y < rows; ++y) h = devlog::Mix(h, devlog::HashGuarded(lock.data + static_cast<size_t>(y) * lock.pitch, rowBytes, fallback));
    return h;
}

void Tracker::OnTextureUnlock(IDirect3DBaseTexture9 *texture, uint32_t face, uint32_t level, bool cube)
{
    PendingLock lock;
    if (!TakeLock(texture, 0x10000 | face << 8 | level, lock) || (lock.flags & D3DLOCK_READONLY)) return;
    D3DSURFACE_DESC d{};
    if (cube) static_cast<IDirect3DCubeTexture9 *>(texture)->GetLevelDesc(level, &d);
    else static_cast<IDirect3DTexture9 *>(texture)->GetLevelDesc(level, &d);
    const uint64_t hash = RectHash(lock, d, 0xbad);
    std::string before, after;
    {
        std::lock_guard<std::mutex> guard(mLock);
        auto it = mResources.find(texture);
        if (it != mResources.end()) {
            before = NameOf(it->second);
            const uint64_t where = devlog::Mix(static_cast<uint64_t>(face) << 32 | level,
                                               lock.hasRect ? devlog::Hash(&lock.rect, sizeof(lock.rect)) : 0);
            it->second.content = devlog::Mix(it->second.content, devlog::Mix(where, hash));
            after = NameOf(it->second);
        }
    }
    if (before.empty()) before = after = FallbackName(texture, 'T');
    Call call("Upload");
    call.S(after).S("from").S(before).U(face).U(level).Rect(lock.hasRect ? &lock.rect : nullptr).X(lock.flags).H(hash);
    Log(call, (d.Usage & D3DUSAGE_DYNAMIC) ? devlog::Kind::Stream : devlog::Kind::Async);
}

void Tracker::OnSurfaceUnlock(IDirect3DSurface9 *surface)
{
    PendingLock lock;
    if (!TakeLock(surface, 0x20000, lock) || (lock.flags & D3DLOCK_READONLY)) return;
    D3DSURFACE_DESC d{};
    surface->GetDesc(&d);
    const uint64_t hash = RectHash(lock, d, 0xbad);
    const uint64_t where = devlog::Mix(static_cast<uint64_t>(d.Width) << 32 | d.Height,
                                       lock.hasRect ? devlog::Hash(&lock.rect, sizeof(lock.rect)) : 0);
    // A texture's level: the upload belongs to the texture.
    const void *owner = surface;
    IDirect3DBaseTexture9 *container = nullptr;
    {
        bool known;
        {
            std::lock_guard<std::mutex> guard(mLock);
            known = mResources.count(surface) != 0;
        }
        if (!known && SUCCEEDED(surface->GetContainer(IID_IDirect3DBaseTexture9, reinterpret_cast<void **>(&container))) && container)
            owner = container;
    }
    std::string before, after;
    {
        std::lock_guard<std::mutex> guard(mLock);
        auto it = mResources.find(owner);
        if (it != mResources.end()) {
            before = NameOf(it->second);
            it->second.content = devlog::Mix(it->second.content, devlog::Mix(where, hash));
            after = NameOf(it->second);
        }
    }
    if (before.empty()) before = after = owner == surface ? FallbackName(surface, 'S') : FallbackName(owner, 'T');
    if (container) container->Release();
    char size[32];
    std::snprintf(size, sizeof(size), "%ux%u", d.Width, d.Height);
    Call call("Upload");
    call.S(after).S("from").S(before).S(size).Rect(lock.hasRect ? &lock.rect : nullptr).X(lock.flags).H(hash);
    Log(call, (d.Usage & D3DUSAGE_DYNAMIC) ? devlog::Kind::Stream : devlog::Kind::Async);
}

} // namespace d3dlog
