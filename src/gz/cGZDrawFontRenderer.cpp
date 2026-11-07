// cGZDrawFontRenderer (src/gz/cGZDrawFontRenderer.h; docs/subsystems/rendering.md, R5).
#include "gz/cGZDrawFontRenderer.h"

#include <algorithm>
#include <list>
#include <new>

#include "gz/cGZDrawContext.h"
#include "gz/cGZDrawContextAsBuffer.h"
#include "gz/cIGZCOM.h"
#include "gz/cIGZDrawContextEngine.h"
#include "gz/cRZCursor.h" // GetGraphicSystem
#include "ifaces/cIGZGraphicSystem.h"
#include "rz/StaticGetters.h" // GetDirectorFrameworkObject

namespace {

cIGZCOM *Com() { return static_cast<cIGZCOM *>(GetDirectorFrameworkObject()); }

// 32-bit wrapping arithmetic on the coordinates, as the original's.
int32_t Add(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b)); }
int32_t Sub(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b)); }

// The atlas's value for each coverage byte: white, with the coverage as alpha, in the native colour type's layout
// (alpha, then red and blue of `blue` bits and green of `green` bits, alpha at the top).
void BuildColorTable(uint32_t *table, uint32_t type)
{
    uint32_t alpha = 8, blue = 8, green = 8;
    switch (type) {
    case 4: alpha = 0, blue = 5, green = 5; break; // 1555 read as 555
    case 5: alpha = 0, blue = 5, green = 6; break; // 565
    case 6: alpha = 1, blue = 5, green = 5; break; // 1555
    case 7: alpha = 4, blue = 4, green = 4; break; // 4444
    default: break;                                // 8888
    }
    const uint32_t coverageMask = 0x100u - (0x100u >> alpha); // the coverage's top `alpha` bits
    const uint32_t alphaMax = (1u << alpha) - 1;
    // ORIGINAL-BUG: with no alpha bits (types 4 and 5) the original divides by zero here (an integer divide fault);
    // ours leaves the alpha out (its mask is 0 then anyway). The atlas is always Init'd to type 7.
    const uint32_t scale = alphaMax != 0 ? 0xffffffffu / alphaMax : 0;
    const uint32_t shift = 32 - 2 * blue - green - alpha;
    const uint32_t alphaBits = alphaMax << (green + 2 * blue);
    const uint32_t blueMax = (1u << blue) - 1;
    const uint32_t white = (blueMax << (green + blue)) + (((1u << green) - 1) << blue) + blueMax;
    for (uint32_t i = 0; i < 256; ++i) table[i] = ((((i & coverageMask) * scale) >> shift) & alphaBits) + white;
}

// One glyph's bitmap into the atlas: `rows` rows of `width` table values of Pixel's size.
template <class Pixel>
void CopyGlyph(uint8_t *row, uint32_t stride, const uint8_t *source, int32_t pitch, int32_t width, int32_t rows,
               const uint32_t *table)
{
    for (; rows > 0; --rows, row += stride, source += pitch) {
        Pixel *out = reinterpret_cast<Pixel *>(row);
        for (int32_t i = 0; i < width; ++i) out[i] = static_cast<Pixel>(table[source[i]]);
    }
}
void CopyGlyph24(uint8_t *row, uint32_t stride, const uint8_t *source, int32_t pitch, int32_t width, int32_t rows,
                 const uint32_t *table)
{
    for (; rows > 0; --rows, row += stride, source += pitch) {
        uint8_t *out = row;
        for (int32_t i = 0; i < width; ++i, out += 3) {
            const uint32_t value = table[source[i]];
            out[0] = static_cast<uint8_t>(value);
            out[1] = static_cast<uint8_t>(value >> 8);
            out[2] = static_cast<uint8_t>(value >> 16);
        }
    }
}

} // namespace

cGZDrawFontRenderer::cGZDrawFontRenderer()
    : mRefCount(0), mAtlasWidth(kAtlasSide), mAtlasHeight(kAtlasSide), mAtlas(nullptr), mCursorX(0), mCursorY(0),
      mRowHeight(0), mCurrent(), mBufferRenderer(nullptr), mContext(nullptr), mDelegating(false)
{
    // The buffer font renderer, for the targets the atlas route does not take (its answer is not checked).
    Com()->GetClassObject(kBufferRenderer, kIid, reinterpret_cast<void **>(&mBufferRenderer));
}

cGZDrawFontRenderer::~cGZDrawFontRenderer()
{
    if (mContext) mContext->Release();
    if (mBufferRenderer) mBufferRenderer->Release();
    // The places go with the map (the original frees them before the atlas's Release).
    mFonts.clear();
    if (mAtlas) mAtlas->Release();
}

cGZDrawFontRenderer *cGZDrawFontRenderer::Create() { return new cGZDrawFontRenderer; }

cGZDrawFontRenderer *__fastcall cGZDrawFontRendererAdapters::Construct(void *memory, void *)
{
    return new (memory) cGZDrawFontRenderer;
}

void __fastcall cGZDrawFontRendererAdapters::Destruct(cGZDrawFontRenderer *self, void *)
{
    self->cGZDrawFontRenderer::~cGZDrawFontRenderer();
}

bool cGZDrawFontRenderer::QueryInterface(uint32_t riid, void **out)
{
    if (riid != 1 && riid != kIid) return false;
    *out = static_cast<cIGZFontRenderer *>(this);
    AddRef();
    return true;
}

uint32_t cGZDrawFontRenderer::AddRef() { return static_cast<uint32_t>(++mRefCount); }

uint32_t cGZDrawFontRenderer::Release()
{
    if (--mRefCount != 0) return static_cast<uint32_t>(mRefCount);
    delete this;
    return 0;
}

bool cGZDrawFontRenderer::vf3(uint32_t) { return true; }

bool cGZDrawFontRenderer::ReleaseFont(uint32_t font)
{
    mFonts.erase(font);
    return true;
}

bool cGZDrawFontRenderer::vf9(uint32_t, uint32_t, uint32_t, uint32_t) { return true; }

void cGZDrawFontRenderer::SetAtlasSize(uint32_t width, uint32_t height)
{
    mAtlasWidth = width;
    mAtlasHeight = height;
    BuildAtlas();
}

bool cGZDrawFontRenderer::BeginDraw(uint32_t font, cIGZBuffer *target, uint32_t mode)
{
    mDelegating = true;
    mOriginY = 0;
    mOriginX = 0;
    cIGZDrawContextAsBuffer *drawable = nullptr;
    if (target->QueryInterface(cIGZDrawContextAsBuffer::kIid, reinterpret_cast<void **>(&drawable))) {
        cIGZDrawContext *const context = drawable->LockContext();
        cGZDrawSurface surface;
        if (!context->LockSurface(&surface, 1)) {
            // No surface to blit into (a hardware context): the atlas route, through the context, which stays locked
            // until EndDraw.
            context->UnlockSurface();
            cIGZDrawContext *const held = mContext;
            if (context != held) {
                context->AddRef();
                mContext = context;
                if (held) held->Release();
            }
            mDelegating = false;
        } else {
            // The buffer renderer draws: the glyphs go in the context's coordinates (its origin added).
            cGZPoint origin;
            const cGZPoint *const at = context->GetOrigin(&origin);
            mOriginX = at->x;
            mOriginY = at->y;
            drawable->UnlockContext();
        }
    }
    bool result;
    if (mDelegating) {
        result = mBufferRenderer->BeginDraw(font, target, mode);
    } else {
        if (!mAtlas) BuildAtlas();
        mCurrent = mFonts.find(font);
        if (mCurrent == mFonts.end()) {
            mFonts.try_emplace(font);
            mCurrent = mFonts.find(font);
        }
        result = true;
    }
    if (drawable) drawable->Release();
    return result;
}

bool cGZDrawFontRenderer::EndDraw(uint32_t font, cIGZBuffer *target, uint32_t mode)
{
    if (mDelegating) return mBufferRenderer->EndDraw(font, target, mode);
    mCurrent = mFonts.end();
    cIGZDrawContextAsBuffer *drawable = nullptr;
    target->QueryInterface(cIGZDrawContextAsBuffer::kIid, reinterpret_cast<void **>(&drawable));
    // ORIGINAL-BUG: the original unlocks without checking the answer (a call through null when the target is not a
    // drawable buffer); ours skips it.
    if (drawable) {
        drawable->UnlockContext();
        drawable->Release();
    }
    return true;
}

bool cGZDrawFontRenderer::DrawGlyphs(uint32_t font, cIGZBuffer *target, cGZFontGlyph *glyphs, uint32_t count,
                                     uint32_t mode, const uint32_t *colours, const cGZRect *clip)
{
    if (mDelegating) {
        // In place: the caller's glyphs keep the origin.
        for (uint32_t i = 0; i < count; ++i) {
            cGZRect &rect = glyphs[i].rect;
            rect.left = Add(rect.left, mOriginX);
            rect.right = Add(rect.right, mOriginX);
            rect.top = Add(rect.top, mOriginY);
            rect.bottom = Add(rect.bottom, mOriginY);
        }
        cGZRect moved;
        const cGZRect *movedClip = nullptr;
        if (clip) {
            moved = {Add(clip->left, mOriginX), Add(clip->top, mOriginY), Add(clip->right, mOriginX),
                     Add(clip->bottom, mOriginY)};
            movedClip = &moved;
        }
        return mBufferRenderer->DrawGlyphs(font, target, glyphs, count, mode, colours, movedClip);
    }

    uint8_t red, green, blue;
    GetGraphicSystem()->ConvertNativeToRGB(*colours, &red, &green, &blue);
    mContext->SetColor(0xff000000u | uint32_t(red) << 16 | uint32_t(green) << 8 | blue);
    mContext->PushTransform();
    if (clip) mContext->IntersectClip(clip);
    mContext->PushState();
    mContext->vf22(3);
    mContext->vf24(1);

    // Passes over the glyphs not drawn yet: each places up to 64 (found in the atlas or, once a pass found none, added
    // to it; once a pass added none either, the atlas may start over), then draws them. A pass that draws anything
    // starts the next with neither.
    std::list<cGZFontGlyph *> waiting;
    for (uint32_t i = 0; i < count; ++i) waiting.push_back(&glyphs[i]);
    bool add = false, allowReset = false;
    while (!waiting.empty()) {
        cGZFontGlyph *drawn[kPassGlyphs];
        Place *places[kPassGlyphs];
        uint32_t placed = 0;
        for (auto it = waiting.begin(); it != waiting.end() && placed < kPassGlyphs;) {
            cGZFontGlyph *const glyph = *it;
            Font &known = mCurrent->second;
            const auto found = known.find(glyph->code);
            Place *place = nullptr;
            if (found != known.end()) {
                place = &found->second;
            } else if (add) {
                mContext->ClearTexture();
                place = AddGlyph(font, glyph, allowReset);
            }
            if (place) {
                drawn[placed] = glyph;
                places[placed] = place;
                ++placed;
                it = waiting.erase(it);
                allowReset = false;
                // ORIGINAL-BUG: the glyph after a placed one waits for the next pass (the original steps on from the
                // erased node's successor); reproduced, as it decides which glyphs share an atlas.
                if (it == waiting.end()) break;
            }
            ++it;
        }
        if (placed == 0) {
            if (!add) add = true;
            else allowReset = true;
            continue;
        }
        add = false;
        allowReset = false;
        for (uint32_t i = 0; i < placed; ++i) {
            const cGZRect &rect = drawn[i]->rect;
            mContext->SetTexture(mAtlas, float(Sub(rect.left, places[i]->x)), float(Sub(rect.top, places[i]->y)), 1.0f,
                                 1.0f);
            mContext->FillRect(&rect);
        }
    }

    mContext->ClearTexture();
    mContext->vf22(0);
    mContext->vf24(0);
    mContext->PopState();
    mContext->PopTransform();
    return true;
}

void cGZDrawFontRenderer::BuildAtlas()
{
    cIGZCOM *const com = Com();
    if (mAtlas) {
        cIGZBuffer *const old = mAtlas;
        mAtlas = nullptr;
        old->Release();
    }
    // The answer's low byte.
    if (static_cast<uint8_t>(com->GetClassObject(kBufferClass, cIGZBuffer::kIid, reinterpret_cast<void **>(&mAtlas)))) {
        mAtlas->Init(mAtlasWidth, mAtlasHeight, kAtlasFormat, kAtlasBits);
        if (mAtlas->Lock(kClearLock)) {
            mAtlas->Fill(0, nullptr);
            mAtlas->Unlock(kClearLock);
        }
        BuildColorTable(mColorTable, mAtlas->GetNativeColorType().format);
    }
    Reset();
}

void cGZDrawFontRenderer::Reset()
{
    mCursorX = 0;
    mCursorY = 0;
    mRowHeight = 0;
    mFonts.clear();
    if (mAtlas && mAtlas->IsReady() && mAtlas->Lock(kClearLock)) {
        mAtlas->Fill(0, nullptr);
        mAtlas->Unlock(kClearLock);
    }
}

cGZDrawFontRenderer::Place *cGZDrawFontRenderer::AddGlyph(uint32_t font, const cGZFontGlyph *glyph, bool allowReset)
{
    const int32_t width = glyph->width, height = glyph->height;
    const int32_t atlasWidth = static_cast<int32_t>(mAtlas->GetWidth());
    const int32_t atlasHeight = static_cast<int32_t>(mAtlas->GetHeight());
    int32_t x;
    if (Add(mCursorX, width) < atlasWidth && Add(mCursorY, height) < atlasHeight) {
        // In the current row, a pixel below the tallest glyph's.
        x = mCursorX;
        mRowHeight = std::max(mRowHeight, Add(height, 1));
    } else {
        mCursorY = Add(mCursorY, mRowHeight);
        mCursorX = 0;
        x = 0;
        if (Add(Add(height, 1), mCursorY) > atlasHeight) {
            if (!allowReset) return nullptr;
            Reset();
            x = mCursorX;
        }
        // ORIGINAL-BUG: a new row's height is the glyph's, without the pixel of space the current row's glyphs leave
        // (a later, taller glyph of the row widens it with the pixel); reproduced.
        mRowHeight = height;
    }
    const int32_t y = mCursorY;
    Font &known = mFonts.try_emplace(font).first->second;
    Place *const place = &known.emplace(glyph->code, Place{x, y}).first->second;
    mCurrent = mFonts.find(font);

    if (mAtlas->Lock(kCopyLock)) {
        uint8_t *const bits = static_cast<uint8_t *>(mAtlas->GetColorSurfaceBits());
        const uint32_t stride = mAtlas->GetColorSurfaceStride();
        const uint32_t bytes = mAtlas->GetBytesPerPixel();
        uint8_t *const row = bits + (static_cast<uint32_t>(mCursorY) * stride + static_cast<uint32_t>(mCursorX) * bytes);
        switch (mAtlas->GetBitsPerPixel()) {
        case 8: CopyGlyph<uint8_t>(row, stride, glyph->pixels, glyph->pitch, width, height, mColorTable); break;
        case 16: CopyGlyph<uint16_t>(row, stride, glyph->pixels, glyph->pitch, width, height, mColorTable); break;
        case 24: CopyGlyph24(row, stride, glyph->pixels, glyph->pitch, width, height, mColorTable); break;
        case 32: CopyGlyph<uint32_t>(row, stride, glyph->pixels, glyph->pitch, width, height, mColorTable); break;
        default: break; // nothing copied
        }
        mCursorX = Add(mCursorX, Add(width, 1));
        mAtlas->Unlock(kCopyLock);
    }
    return place;
}
