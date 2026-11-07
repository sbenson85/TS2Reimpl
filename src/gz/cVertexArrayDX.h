#pragma once
// The Direct3D vertex and index arrays of nGZGraphic4 (lane NJ, rendering R3b item 1): cStaticVertexComponentArrayDX,
// cDynamicVertexComponentArrayDX (a vertex buffer), cStaticVertexIndexArrayDX, cDynamicVertexIndexArrayDX (a 16-bit
// index buffer). docs/subsystems/rendering.md, R3.
//
// Standalone shape (docs/standalone-shape.md): each class is made by a cdecl creator (no arguments; the object comes
// from the class's fixed-size pool with a reference count of 0). The four creators are hooked
// (src/hook/tables/cVertexArrayDX.cpp): they are the switch, so the game's vtables are never reached and every slot is in
// effect through its callers.
//
// Layout (two faces, as the original's):
//   +0 the primary interface, +4 the Direct3D face (the device, the buffer),
//   +8 the kind given to Create (bit 0: managed rather than system memory, bit 1: software vertex processing; the
//   dynamic arrays: 1 / 2 the caps decide, otherwise system memory), +0xc the lock count, +0x10 the device (cIDeviceDX,
//   not counted), +0x14 the IDirect3DVertexBuffer9 / IDirect3DIndexBuffer9 (counted), +0x18 the reference count;
//   the dynamic arrays: +0x1c the offset of the current span, +0x20 its bytes, +0x24 the buffer's bytes.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d9.h>

#include <cstddef>
#include <cstdint>

#include "gz/cSurfaceDX.h" // cIAllocDeviceResource, the resource ids

namespace nGZGraphic4 {

class cIDeviceDX;

constexpr uint32_t kIidVertexArray = 0xfb7a964e;               // every array
constexpr uint32_t kIidVertexComponentArrayDX = 0x5b7a96c6;    // the component arrays' +4 face
constexpr uint32_t kIidVertexIndexArrayDX = 0xbb813fed;        // the index arrays' +4 face
constexpr uint32_t kIidD3DVertexBuffer = 0x998d7e9;            // +0x14, not counted (answered before the null check)
constexpr uint32_t kIidD3DIndexBuffer = 0x4998d917;            // likewise
constexpr uint32_t kIidStaticVertexComponentArray = 0xfbffec42;
constexpr uint32_t kIidDynamicVertexComponentArray = 0xfbfffcd7;
constexpr uint32_t kIidStaticVertexIndexArray = 0xdbfff0de;
constexpr uint32_t kIidDynamicVertexIndexArray = 0x3c0437f9;
constexpr uint32_t kIidDynamicArray = 0x7c0503f9;              // both dynamic arrays

// The arrays' LogD3DError source file.
constexpr uint32_t kArraySourceFileRva = 0xd9e2f8;

// nGZGraphic4::cIVertexComponentArray / cIVertexIndexArray (15-19; the two interfaces have the same slots).
class cIVertexArray : public cIAllocDeviceResource {
public:
    // 15: the static arrays lock `size` bytes at `offset` (read only when `write` is 0); the dynamic ones the bytes at
    // `offset` past the current span's start, `size` 0 for the whole span, without overwriting (`write` unused).
    // Answers the bytes.
    virtual void *Lock(uint32_t write, uint32_t offset, uint32_t size) = 0;
    virtual void Unlock() = 0;               // 16
    virtual uint32_t GetKind() = 0;          // 17: the kind given to Create
    virtual uint32_t GetSize() = 0;          // 18: the static arrays' buffer bytes; the dynamic ones' span bytes
    virtual uint32_t GetLockCount() = 0;     // 19
};

class cIStaticVertexArray : public cIVertexArray {
public:
    virtual bool Create(cIDeviceDX *device, uint32_t size, uint32_t kind) = 0; // 20
    virtual ~cIStaticVertexArray() = default;                                   // 21
};
class cIDynamicVertexArray : public cIVertexArray {
public:
    // 20: the next span of `count` items of `stride` bytes after the current one; when it would pass the end, the
    // buffer is discarded and the span starts at 0. Answers the span's first item.
    virtual uint32_t Reserve(uint32_t stride, uint32_t count) = 0;
    virtual uint32_t GetCapacity() = 0;                                         // 21: the buffer's bytes
    virtual uint32_t GetOffset() = 0;                                           // 22: the span's first byte
    virtual bool Create(cIDeviceDX *device, uint32_t size, uint32_t kind) = 0; // 23
    virtual ~cIDynamicVertexArray() = default;                                  // 24
};

// The +4 face (cIVertexComponentArrayDX / cIVertexIndexArrayDX).
class cIVertexArrayDX : public cIGZUnknown {
public:
    virtual cIVertexArray *GetArray() = 0;      // 3
    virtual cIVertexArray *GetArrayConst() = 0; // 4
    virtual cIDeviceDX *GetDevice() = 0;        // 5
    virtual void *GetD3DBuffer() = 0;           // 6: IDirect3DVertexBuffer9 / IDirect3DIndexBuffer9
};

// The component / index differences.
struct ComponentTraits {
    using Buffer = IDirect3DVertexBuffer9;
    using Desc = D3DVERTEXBUFFER_DESC;
    static constexpr uint32_t kIidBuffer = kIidD3DVertexBuffer, kIidFace = kIidVertexComponentArrayDX;
    // LogD3DError lines: GetMemorySize, SurvivesReset, GetPool, GetVideoMemorySize, Lock, Unlock, GetSize,
    // SurvivesModeChange, Create; the dynamic ones' Lock, Reserve (lock, unlock), Create.
    static constexpr uint32_t kLineMemory = 0xbd, kLineReset = 0xcd, kLinePool = 0xe9, kLineVideo = 0xf3,
                              kLineLock = 0x10e, kLineUnlock = 0x118, kLineSize = 0x128, kLineModeChange = 0x199,
                              kLineCreate = 0x1bf, kLineDynamicLock = 0x24f, kLineDiscard = 0x26c,
                              kLineDiscardUnlock = 0x26d, kLineDynamicCreate = 0x29e;
    static HRESULT Make(IDirect3DDevice9 *d3d, uint32_t size, uint32_t usage, uint32_t pool, Buffer **out)
    {
        return d3d->CreateVertexBuffer(size, usage, 0, static_cast<D3DPOOL>(pool), out, nullptr);
    }
};
struct IndexTraits {
    using Buffer = IDirect3DIndexBuffer9;
    using Desc = D3DINDEXBUFFER_DESC;
    static constexpr uint32_t kIidBuffer = kIidD3DIndexBuffer, kIidFace = kIidVertexIndexArrayDX;
    static constexpr uint32_t kLineMemory = 0x348, kLineReset = 0x358, kLinePool = 0x374, kLineVideo = 0x37e,
                              kLineLock = 0x399, kLineUnlock = 0x3a3, kLineSize = 0x3b3, kLineModeChange = 0x424,
                              kLineCreate = 0x44a, kLineDynamicLock = 0x4da, kLineDiscard = 0x4f3,
                              kLineDiscardUnlock = 0x4f4, kLineDynamicCreate = 0x525;
    static HRESULT Make(IDirect3DDevice9 *d3d, uint32_t size, uint32_t usage, uint32_t pool, Buffer **out)
    {
        return d3d->CreateIndexBuffer(size, usage, D3DFMT_INDEX16, static_cast<D3DPOOL>(pool), out, nullptr);
    }
};

// What every array shares (the original's cVertexComponentArrayDX<> / cVertexIndexArrayDX<>).
template <class Primary, class Traits> class cVertexArrayDX : public Primary, public cIVertexArrayDX {
public:
    using Buffer = typename Traits::Buffer;

    cVertexArrayDX() = default;
    cVertexArrayDX(const cVertexArrayDX &) = delete;
    cVertexArrayDX &operator=(const cVertexArrayDX &) = delete;

    uint32_t AddRef() override { return ++mRefs; }
    uint32_t Release() override;
    void ReleaseResource() override;
    cIDeviceDX *GetDevice() override { return mDevice; }
    cIDeviceDX *GetDeviceConst() override { return mDevice; }
    bool IsCreated() override { return mBuffer != nullptr; }
    uint32_t GetMemorySize() override;
    bool SurvivesReset() override;
    uint32_t SetPriority(uint32_t priority) override { return mBuffer->SetPriority(priority); }
    void PreLoad() override { mBuffer->PreLoad(); }
    uint32_t GetPool() override;
    uint32_t GetVideoMemorySize() override;
    uint32_t GetPriority() override { return mBuffer->GetPriority(); }
    void Unlock() override;
    uint32_t GetKind() override { return mKind; }
    uint32_t GetLockCount() override { return mLockCount; }

    cIVertexArray *GetArray() override { return this; }
    cIVertexArray *GetArrayConst() override { return this; }
    void *GetD3DBuffer() override { return mBuffer; }

protected:
    ~cVertexArrayDX();
    // QueryInterface: the buffer ids, the shared ids and `own` (the class's ids, up to two).
    bool Answer(uint32_t riid, void **out, uint32_t own0, uint32_t own1);
    typename Traits::Desc Describe(uint32_t line);
    // The registration with the device's manager that Create ends with: the buffer (counted) and the fields.
    void Take(cIDeviceDX *deviceDX, uint32_t kind, Buffer *buffer);

public:
    uint32_t mKind = 0;            // +8
    uint32_t mLockCount = 0;       // +0xc
    cIDeviceDX *mDevice = nullptr; // +0x10
    Buffer *mBuffer = nullptr;     // +0x14
    uint32_t mRefs = 0;            // +0x18
};

template <class Traits> class cStaticVertexArrayDX : public cVertexArrayDX<cIStaticVertexArray, Traits> {
public:
    void *Lock(uint32_t write, uint32_t offset, uint32_t size) override;
    uint32_t GetSize() override;
    bool SurvivesModeChange() override;
    bool Create(cIDeviceDX *device, uint32_t size, uint32_t kind) override;

protected:
    ~cStaticVertexArrayDX() override;
};

template <class Traits> class cDynamicVertexArrayDX : public cVertexArrayDX<cIDynamicVertexArray, Traits> {
public:
    void ReleaseResource() override;
    bool SurvivesModeChange() override { return false; }
    void *Lock(uint32_t write, uint32_t offset, uint32_t size) override;
    uint32_t GetSize() override { return mSpanBytes; }
    uint32_t GetCapacity() override { return mCapacity; }
    uint32_t GetOffset() override { return mOffset; }
    bool Create(cIDeviceDX *device, uint32_t size, uint32_t kind) override;

    uint32_t mOffset = 0;    // +0x1c
    uint32_t mSpanBytes = 0; // +0x20
    uint32_t mCapacity = 0;  // +0x24

protected:
    ~cDynamicVertexArrayDX() override;
    // The discard Reserve does when the span would pass the end.
    void Discard();
};

class cStaticVertexComponentArrayDX final : public cStaticVertexArrayDX<ComponentTraits> {
public:
    static constexpr uint32_t kSize = 0x1c, kPoolRva = 0x10ca160;
    static void *operator new(size_t size);
    static void operator delete(void *block, size_t size);
    static cIStaticVertexArray *__cdecl New(); // the creator 0x165fa0 (hooked)

    bool QueryInterface(uint32_t riid, void **out) override;
    ~cStaticVertexComponentArrayDX() override = default;
};

class cDynamicVertexComponentArrayDX final : public cDynamicVertexArrayDX<ComponentTraits> {
public:
    static constexpr uint32_t kSize = 0x28, kPoolRva = 0x10ca16c;
    static void *operator new(size_t size);
    static void operator delete(void *block, size_t size);
    static cIDynamicVertexArray *__cdecl New(); // 0x165ea0 (hooked)

    bool QueryInterface(uint32_t riid, void **out) override;
    uint32_t Reserve(uint32_t stride, uint32_t count) override;
    ~cDynamicVertexComponentArrayDX() override = default;
};

class cStaticVertexIndexArrayDX final : public cStaticVertexArrayDX<IndexTraits> {
public:
    static constexpr uint32_t kSize = 0x1c, kPoolRva = 0x10ca178;
    static void *operator new(size_t size);
    static void operator delete(void *block, size_t size);
    static cIStaticVertexArray *__cdecl New(); // 0x166000 (hooked)

    bool QueryInterface(uint32_t riid, void **out) override;
    ~cStaticVertexIndexArrayDX() override = default;
};

class cDynamicVertexIndexArrayDX final : public cDynamicVertexArrayDX<IndexTraits> {
public:
    static constexpr uint32_t kSize = 0x28, kPoolRva = 0x10ca184;
    static void *operator new(size_t size);
    static void operator delete(void *block, size_t size);
    static cIDynamicVertexArray *__cdecl New(); // 0x165f20 (hooked)

    bool QueryInterface(uint32_t riid, void **out) override;
    uint32_t Reserve(uint32_t count, uint32_t stride) override;
    ~cDynamicVertexIndexArrayDX() override = default;
};

static_assert(sizeof(cStaticVertexComponentArrayDX) == cStaticVertexComponentArrayDX::kSize, "0x1c bytes");
static_assert(sizeof(cDynamicVertexComponentArrayDX) == cDynamicVertexComponentArrayDX::kSize, "0x28 bytes");
static_assert(sizeof(cStaticVertexIndexArrayDX) == cStaticVertexIndexArrayDX::kSize, "0x1c bytes");
static_assert(sizeof(cDynamicVertexIndexArrayDX) == cDynamicVertexIndexArrayDX::kSize, "0x28 bytes");

} // namespace nGZGraphic4
