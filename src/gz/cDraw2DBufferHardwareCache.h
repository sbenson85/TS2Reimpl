#pragma once
// nGZGraphic4::cDraw2DBufferHardwareCache (1.18.0.187; docs/subsystems/rendering.md, R5b, lane OL): what a buffer keeps
// of its life as a Direct3D texture. cDraw2D::SetTextureSource makes one for a buffer that has none (the buffer holds it,
// counted, and the draw system watches it, uncounted); Load puts the buffer in an image record of the renderer (a record
// that borrows the buffer: bufferUncounted), UploadRecord copies the pixels in again whenever the buffer's change
// counter moved, and the draw system's Trim / FlushAll make it let the record go (Unload).
//
// Standalone shape (docs/standalone-shape.md): a real class on the cIGZUnknown face in the original slot order, made
// only by cDraw2D::SetTextureSource (ours) from the 0x20-byte pool 0xfeb8e0 like the original's, so every slot is in effect
// through its callers; the draw system's view of it is cIGZBufferHardwareCache (gz/cIGZDrawContextEngine.h), the same
// vtable layout (slot 3 is Attach there: ret 4, the buffer; slot 5 forwards to 11 with its stack word).
//
// Layout (0x20): +0 vtable, +4 reference count, +8 the renderer (cDraw2D, not counted; 0 after Detach), +0xc the buffer
// (not counted), +0x10 the record handle (cDraw2DRecordRef: -1 none), +0x14 the draw system tick it was loaded at,
// +0x18 the buffer's change counter when its pixels were last copied, +0x1c the dirty flag.
#include <cstddef>
#include <cstdint>

#include "gz/cDraw2D.h"
#include "gz/cIGZBuffer.h"
#include "gz/cIGZUnknown.h"

namespace nGZGraphic4 {

// A counted image record of the renderer in the game's Draw2D service (0x18c000: the service 0x89027355's slot 13, kept
// in a cell). 4 bytes, -1 for none. The members are the game's 0x18bf60 / 0x18bfc0 / 0x18c090 / 0x18bf90, which only the
// cache calls.
class cDraw2DRecordRef {
public:
    int32_t mIndex = -1;

    cDraw2DRecordRef() = default;
    cDraw2DRecordRef(const cDraw2DRecordRef &) = delete;
    cDraw2DRecordRef &operator=(const cDraw2DRecordRef &) = delete;
    ~cDraw2DRecordRef() { Destruct(); }

    // 0x18bf60 (ret 4, returns this): takes `index` and counts it (AddRefRecord).
    cDraw2DRecordRef *Set(int32_t index);
    // 0x18bfc0 (ret 4, returns this): counts the other's record, lets this one's go, takes it.
    cDraw2DRecordRef *Assign(const cDraw2DRecordRef *other);
    // 0x18c090: lets the record go (ReleaseRecord) unless there is none.
    void Release();
    // 0x18bf90: Release under an unwind frame.
    void Destruct() { Release(); }
    bool IsInvalid() const { return mIndex == -1; } // 0x18c080 (SGResourceBlockers::cInvalidDwordTag)
};
static_assert(sizeof(cDraw2DRecordRef) == 4, "cDraw2DRecordRef: 4 bytes");

class cDraw2DBufferHardwareCache : public cIGZUnknown {
public:
    static constexpr uint32_t kSize = 0x20;
    static constexpr uint32_t kPoolRva = 0xfeb8e0;          // the 0x20-byte block pool (RZFastFixed0x20)
    static constexpr uint32_t kIidHardwareCache = 0xc99579d; // cIGZBufferHardwareCache
    static constexpr uint32_t kIidOther = 0xfa59f7b9;       // also answered, as the same object

    // Made by SetTextureSource: unreferenced, the renderer and no buffer.
    explicit cDraw2DBufferHardwareCache(cDraw2D *owner);

    // The pool's blocks (0x14a2d0 / the page free list); delete returns the block.
    static void *operator new(size_t size);
    static void operator delete(void *block);

    // ---- cIGZUnknown ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0x151420: ids 1, 0xc99579d, 0xfa59f7b9
    uint32_t AddRef() override { return ++mRefCount; }        // shared 0x148fb0
    uint32_t Release() override;                              // shared 0x151900: at zero the deleting destructor (slot 13)

    // ---- the cache ----
    // 3 (0x1501b0, ret 4): takes the buffer and has the draw system watch the cache.
    virtual bool Attach(cIGZBuffer *buffer);
    virtual void Detach();                                    // 4 (0x1520d0): unloaded, nothing kept, the draw system's watch ends
    virtual bool LoadForTick(uint32_t tick);                  // 5 (0x150320): slot 11
    virtual void Unload();                                    // 6 (0x152160): the record let go, dirty
    virtual cIGZBuffer *GetBuffer() { return mBuffer; }       // 7 (shared 0x145770)
    virtual bool IsStale();                                   // 8 (0x150ed0): the pixels need copying again
    virtual void Invalidate() { mDirty = true; }              // 9 (0x150eb0)
    virtual bool IsLoaded() { return !mRecord.IsInvalid(); }  // 10 (0x150ec0)
    // 11 (0x151480, ret 4): in a record now (made on the first call, `tick` stored), its pixels copied if stale.
    // False when the renderer has no room for it.
    virtual bool Load(uint32_t tick);
    virtual uint32_t GetLastUseTick() { return mTick; }       // 12 (shared 0x149d00)
    virtual ~cDraw2DBufferHardwareCache();                    // 13 (0x14c6b0, deleting)

    uint32_t mRefCount;         // +4
    cDraw2D *mOwner;            // +8
    cIGZBuffer *mBuffer;        // +0xc
    cDraw2DRecordRef mRecord;   // +0x10
    uint32_t mTick;             // +0x14
    uint32_t mChangeCounter;    // +0x18
    bool mDirty;                // +0x1c
    uint8_t mPad1d[3];
};
static_assert(sizeof(cDraw2DBufferHardwareCache) == cDraw2DBufferHardwareCache::kSize &&
                  offsetof(cDraw2DBufferHardwareCache, mRecord) == 0x10 &&
                  offsetof(cDraw2DBufferHardwareCache, mDirty) == 0x1c,
              "cDraw2DBufferHardwareCache: 0x20 bytes");

} // namespace nGZGraphic4
