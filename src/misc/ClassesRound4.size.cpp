// Lane IG: the last virtual slots of nearly complete classes, round 4 (size-built functions). See ClassesRound4.h.
#include "misc/ClassesRound4.h"

#include "gz/cGZBufferFontBlit.h"

#include <new>

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"

namespace {
using AddIndexFn = void(__thiscall *)(void *, int32_t);

__forceinline void Add(const nTSSG::cTrianglePolygon *p, int32_t index)
{
    VirtualCall<AddIndexFn, 35>(p->mOwner, index);
}
__forceinline int32_t Corner(const nTSSG::cTrianglePolygon *p, int32_t dx, int32_t dy)
{
    return p->mStride * (p->mRow + dy) + p->mFirstVertex + (p->mColumn + dx);
}
} // namespace

// As nTSSG::cBaseEvenTriangleSecond::AddIndices: (0,0) (1,0) (1,1), reversed when flipped.
void cBaseEvenTriangleSecond::AddIndices()
{
    if (mFlipped == 0) {
        Add(this, Corner(this, 0, 0));
        Add(this, Corner(this, 1, 0));
        Add(this, Corner(this, 1, 1));
    } else {
        Add(this, Corner(this, 1, 1));
        Add(this, Corner(this, 1, 0));
        Add(this, Corner(this, 0, 0));
    }
}

// As nTSSG::cBaseOddTriangleSecond::AddIndices: (1,0) (1,1) (0,1), reversed when flipped.
void cBaseOddTriangleSecond::AddIndices()
{
    if (mFlipped == 0) {
        Add(this, Corner(this, 1, 0));
        Add(this, Corner(this, 1, 1));
        Add(this, Corner(this, 0, 1));
    } else {
        Add(this, Corner(this, 0, 1));
        Add(this, Corner(this, 1, 1));
        Add(this, Corner(this, 1, 0));
    }
}

void cTSUIService::Destruct()
{
    mVTable = GameAddress<void>(kVTable);
}

cTSUIService *cTSUIService::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cTSUIService));
    return this;
}

void cHTMLLink::Destruct()
{
    using DestroyFn = void(__thiscall *)(void *, uint32_t);
    mVTable = GameAddress<void>(kVTable);
    if (mChild1c) VirtualCall<DestroyFn, 33>(mChild1c, 1u);
    if (mChild24) VirtualCall<DestroyFn, 33>(mChild24, 1u);
    if (mChild28) VirtualCall<DestroyFn, 33>(mChild28, 1u);
    mVTable = GameAddress<void>(kBaseVTable);
}

cHTMLLink *cHTMLLink::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cHTMLLink));
    return this;
}

bool cTSUninstalledNeighborhoodTemplatesFilter::Accept(void *entry)
{
    if (!entry) return false;
    if (VirtualCall<int32_t(__thiscall *)(void *), 18>(entry) != 4) return false;
    if (!VirtualCall<bool(__thiscall *)(void *, uint32_t), 14>(entry, 4u)) return false;
    if (!(mCategories & BitMask(VirtualCall<uint32_t(__thiscall *)(void *), 22>(entry)))) return false;
    for (void **it = mInstalledBegin; it != mInstalledEnd; ++it) {
        void *installed = VirtualCall<void *(__thiscall *)(void *), 10>(*it);
        if (VirtualCall<bool(__thiscall *)(void *, uint32_t), 11>(
                installed, VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 8>(entry, 0u)))
            return false;
    }
    return true;
}

uint32_t cGZBufferFontRenderer::Release()
{
    if (mRefCount > 1) return static_cast<uint32_t>(--mRefCount);
    ::operator delete(this, sizeof(cGZBufferFontRenderer));
    return 0;
}

uint32_t cGZBufferFontRenderer::BeginDraw(uint32_t, void *surface, uint32_t)
{
    return VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 6>(surface, 0x8080u);
}

uint32_t cGZBufferFontRenderer::EndDraw(uint32_t, void *surface, uint32_t)
{
    return VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 7>(surface, 0x8080u);
}

bool cGZBufferFontRenderer::DrawGlyphs(uint32_t a, uint32_t b, Glyph *glyphs, uint32_t count, uint32_t e, uint32_t f,
                                       uint32_t g)
{
    // Blits one glyph (cGZBufferFontRendererCode::Blit, 0x90bbba). The glyph's first 16 bytes are the destination
    // rectangle, mPitch (+0x10) is the coverage bitmap's address and mUnknown14 (+0x14) its pitch (lane LS).
    if (count) {
        int32_t rect[4];
        rect[0] = 0;
        rect[1] = 0;
        Glyph *glyph = glyphs;
        while (count--) {
            rect[2] = glyph->mWidth;
            rect[3] = glyph->mHeight;
            if (glyph->mCode >= 0x20)
                reinterpret_cast<cGZBufferFontRendererCode *>(this)->Blit(
                    a, reinterpret_cast<cIGZBuffer *>(b), reinterpret_cast<const int32_t *>(glyph->mBitmap), glyph->mCode,
                    reinterpret_cast<const uint8_t *>(glyph->mPitch), rect, static_cast<int32_t>(glyph->mUnknown14), e,
                    reinterpret_cast<const uint32_t *>(f), reinterpret_cast<const int32_t *>(g));
            ++glyph;
        }
    }
    return true;
}

bool cTSWinDragAndDropMessageFilter::Dispatch(void *window, Message *message)
{
    bool handled = false;
    auto it = mHandlers.find(message->mId);
    if (it != mHandlers.end()) {
        HandlerFn fn = it->second.mFn;
        handled = fn(window, message, it->second.mContext);
    }
    return handled;
}

bool cTSWinDragAndDropMessageFilter::DoMessage(void *window, Message *message)
{
    void *data = message->mData;
    switch (message->mId) {
    case 0x6c085f45: // drag left the window (tentative): unless the drag's +0x18 is this window, send it message 0x14
        if (Dispatch(window, message)) return true;
        if (*reinterpret_cast<void **>(static_cast<uint8_t *>(data) + 0x18) != window) {
            Message leave = {0x14, nullptr, {0, 0}};
            VirtualCall<void(__thiscall *)(void *, Message *), 3>(window, &leave);
            Dispatch(window, &leave);
            return true;
        }
        break;
    case 0x8c085f48:
    case 0x8c0a1fcc:
    case 0xac085f47:
    case 0xac0fe76d:
        // ORIGINAL-BUG: a handler that declines is asked a second time (the default path below dispatches again).
        if (Dispatch(window, message)) return true;
        break;
    case 0xac085f43: // dropped (tentative): when the drag's +0x24 is this window, send it message 0x13
        if (Dispatch(window, message)) return true;
        if (*reinterpret_cast<void **>(static_cast<uint8_t *>(data) + 0x24) == window) {
            Message drop = {0x13, nullptr, {0, 0}};
            VirtualCall<void(__thiscall *)(void *, Message *), 3>(window, &drop);
            return true;
        }
        break;
    }
    return Dispatch(window, message);
}

void cTSWinDragAndDropMessageFilter::Destruct()
{
    mVTable = GameAddress<void>(kVTable);
    mHandlers.~unordered_map();
}

cTSWinDragAndDropMessageFilter *cTSWinDragAndDropMessageFilter::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cTSWinDragAndDropMessageFilter));
    return this;
}

void cTSWinDragAndDropMessageFilter::SetHandler(uint32_t id, HandlerFn fn, void *context)
{
    Handler &handler = mHandlers.try_emplace(id).first->second;
    handler.mFn = fn;
    handler.mContext = context;
}

bool cTSWinDragAndDropMessageFilter::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kIid) return false;
    *out = this;
    VirtualCall<uint32_t(__thiscall *)(void *), 1>(this);
    return true;
}

uint32_t cTSWinDragAndDropMessageFilter::Release()
{
    if (mRefCount > 1) return static_cast<uint32_t>(--mRefCount);
    VirtualCall<void *(__thiscall *)(void *, uint32_t), 4>(this, 1u);
    return 0;
}

bool cGZClipboardManagerClipboardData::QueryInterface(uint32_t riid, void **out)
{
    switch (riid) {
    case 1:
    case kIid:
        *out = this;
        VirtualCall<uint32_t(__thiscall *)(void *), 1>(this);
        return true;
    }
    return false;
}

bool cGZClipboardManagerClipboardData::QueryData(uint32_t riid, void **out)
{
    if (void *data = mData) return VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(data, riid, out);
    return false;
}

void cGZClipboardManagerClipboardData::Destruct()
{
    mVTable = GameAddress<void>(kVTable);
    if (mData) VirtualCall<uint32_t(__thiscall *)(void *), 2>(mData);
}

cGZClipboardManagerClipboardData *cGZClipboardManagerClipboardData::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cGZClipboardManagerClipboardData));
    return this;
}
