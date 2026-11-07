#pragma once
// cGZDrawFontRenderer: the font renderer of draw contexts (docs/subsystems/rendering.md, R5). A target that is a drawable buffer (cIGZDrawContextAsBuffer) whose context cannot lock its surface
// (a hardware context) is drawn into through the context: the glyphs are packed into a 256 x 256 atlas buffer (format 7,
// 16 bits) row by row as they are first met, keyed by font and character, and each glyph is a FillRect textured from
// its place in the atlas. Any other target goes to the buffer font renderer (COM class 0x8cda30b), with the glyphs moved
// by the context's origin when the target is a drawable buffer.
//
// 0x458 bytes (operator new), one face: cIGZFontRenderer (vtable 0xe4f09c, 11 slots: 8 the interface's, then the
// deleting destructor 0x8c3810, a true answer and SetAtlasSize). Made by the COM director's creator 0x8c32a0 (the
// constructor 0x8c2440 inline) only; the constructor and the destructor 0x8c2530 are referenced by nothing.
// Standalone shape (docs/standalone-shape.md): the creator and the constructor are hooked, so every renderer carries our
// vtable and the slots are in effect through their callers.
#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "gz/cIGZFontRenderer.h"

class cIGZDrawContext;

class cGZDrawFontRenderer : public cIGZFontRenderer {
public:
    static constexpr uint32_t kSize = 0x458;
    static constexpr uint32_t kBufferRenderer = 0x8cda30b; // the COM class of the buffer font renderer
    static constexpr uint32_t kBufferClass = 0xc470d325;   // the COM class of the atlas buffer
    static constexpr uint32_t kAtlasSide = 0x100;
    static constexpr uint32_t kAtlasFormat = 7, kAtlasBits = 16;
    static constexpr uint32_t kClearLock = 0x8001, kCopyLock = 0x8040; // the atlas's Lock / Unlock flags
    static constexpr uint32_t kPassGlyphs = 64; // DrawGlyphs places at most this many glyphs before drawing them

    cGZDrawFontRenderer(); // 0x8c2440
    // 0x8c32a0, the COM director's creator (the constructor inline): a new renderer, count 0.
    static cGZDrawFontRenderer *Create();

    // cIGZUnknown
    bool QueryInterface(uint32_t riid, void **out) override; // 0x8c2570: ids 1 and kIid
    uint32_t AddRef() override;                              // 0x8c25a0
    uint32_t Release() override;                             // 0x8c25b0: the deleting destructor at 0
    // cIGZFontRenderer
    bool vf3(uint32_t font) override;         // 0x8c25d0: true
    bool ReleaseFont(uint32_t font) override; // 0x8c25e0: the font's atlas places forgotten
    bool BeginDraw(uint32_t font, cIGZBuffer *target, uint32_t mode) override; // 0x8c2670
    bool EndDraw(uint32_t font, cIGZBuffer *target, uint32_t mode) override;   // 0x8c27e0
    bool DrawGlyphs(uint32_t font, cIGZBuffer *target, cGZFontGlyph *glyphs, uint32_t count, uint32_t mode,
                    const uint32_t *colours, const cGZRect *clip) override; // 0x8c2840
    // Its own slots: 8 the deleting destructor 0x8c3810 (the destructor 0x8c2530), 9 and 10.
    virtual ~cGZDrawFontRenderer();
    virtual bool vf9(uint32_t, uint32_t, uint32_t, uint32_t); // 0x8c2ce0: true
    // 0x8c2cf0: a new atlas of that size. No value: the original leaves BuildAtlas's last eax (a leftover).
    virtual void SetAtlasSize(uint32_t width, uint32_t height);

    // A glyph's place in the atlas (its top left corner).
    struct Place {
        int32_t x, y;
    };
    using Font = std::unordered_map<uint32_t, Place>; // by character
    using Fonts = std::unordered_map<uint32_t, Font>; // by font

    // 0x8c2d10: a new atlas from the COM director (Init to the size, cleared) and the colour table for its native colour
    // type, then Reset.
    void BuildAtlas();
    // 0x8c2f10: the packing back to the top left, every place forgotten, the atlas cleared (when ready).
    void Reset();
    // 0x8c2f80: places `glyph` of `font` at the packing cursor (next row when it does not fit; when the rows run out,
    // Reset if `allowReset`, else null) and copies its bitmap into the atlas through the colour table.
    Place *AddGlyph(uint32_t font, const cGZFontGlyph *glyph, bool allowReset);

    int32_t mRefCount;              // +4
    uint32_t mAtlasWidth;           // +8 what BuildAtlas Inits the atlas to
    uint32_t mAtlasHeight;          // +0xc
    cIGZBuffer *mAtlas;             // +0x10 (counted)
    int32_t mCursorX;               // +0x14 the packing: the next place in the current row
    int32_t mCursorY;               // +0x18 the current row's top
    int32_t mRowHeight;             // +0x1c
    Fonts mFonts;                   // +0x20 the glyphs in the atlas
    Fonts::iterator mCurrent;       // +0x40 BeginDraw's font (end() outside a draw)
    uint32_t mColorTable[256];      // +0x44 coverage to the atlas's native colour (white, alpha by coverage)
    cIGZFontRenderer *mBufferRenderer; // +0x444 (counted)
    cIGZDrawContext *mContext;      // +0x448 the target's context for the atlas route (counted)
    bool mDelegating;               // +0x44c BeginDraw's choice: the buffer renderer draws
    int32_t mOriginX;               // +0x450 the target context's origin (the buffer route)
    int32_t mOriginY;               // +0x454
};
static_assert(sizeof(cGZDrawFontRenderer) == cGZDrawFontRenderer::kSize, "cGZDrawFontRenderer: 0x458 bytes");
static_assert(offsetof(cGZDrawFontRenderer, mAtlas) == 0x10 && offsetof(cGZDrawFontRenderer, mFonts) == 0x20 &&
                  offsetof(cGZDrawFontRenderer, mCurrent) == 0x40 && offsetof(cGZDrawFontRenderer, mColorTable) == 0x44 &&
                  offsetof(cGZDrawFontRenderer, mBufferRenderer) == 0x444 &&
                  offsetof(cGZDrawFontRenderer, mDelegating) == 0x44c && offsetof(cGZDrawFontRenderer, mOriginY) == 0x454,
              "cGZDrawFontRenderer layout");

// The hooks' adapters: the constructor 0x8c2440 (ecx = the memory) and the destructor 0x8c2530 (ecx = the object; both
// are referenced by nothing in the image, hooked so no original body is left that a missed caller could run).
struct cGZDrawFontRendererAdapters {
    static cGZDrawFontRenderer *__fastcall Construct(void *memory, void *);
    static void __fastcall Destruct(cGZDrawFontRenderer *self, void *);
};
