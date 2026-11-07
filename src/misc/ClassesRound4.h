#pragma once
#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "misc/SmallVirtuals.h"

// Lane IG: the last virtual slots of nearly complete classes, round 4 (RVAs 1.18.0.187). Hook-shaped
// (docs/standalone-shape.md): the game builds these objects, so the members are hooked at their RVAs and store the
// game's vtables, as the originals do; only the fields the members touch are named.

namespace nTSSG {
// The triangle polygons of a terrain grid cell (two triangles per cell, "first" and "second"; "even" and "odd" cells
// split along different diagonals). Each member hands the grid indices of its three corners to the owner (+8): slot 1 to
// the owner's slot 35, the snow variants' slot 0 to its slot 36, slot 2 twice to its slot 37 with a 0 / 1 second
// argument (meanings tentative: index lists of the surface, the snow layer and a two-layer list). A corner (dx, dy) of
// the cell is the index mFirstVertex + (mRow + dy) * mStride + mColumn + dx. When +0xc is set, slot 1 hands the corners
// in the other winding and slots 0 and 2 hand nothing (tentative: a cell seen from below).
struct cTrianglePolygon : cBasePolygon {
    int32_t mFlipped;     // +0xc
    int32_t mStride;      // +0x10 (vertices per grid row)
    uint32_t mUnknown14;  // +0x14
    int32_t mColumn;      // +0x18
    int32_t mRow;         // +0x1c
    int32_t mFirstVertex; // +0x20
};
static_assert(offsetof(cTrianglePolygon, mFlipped) == 0xc && offsetof(cTrianglePolygon, mFirstVertex) == 0x20,
              "layout");

struct cBaseEvenTriangleFirst : cTrianglePolygon {
    void AddIndices();        // slot 1, 0x413540 (folded with ::cBaseEvenTriangleFirst's slot 0 and the gables' copies)
    void AddLayeredIndices(); // slot 2, 0x413800
};
struct cSnowEvenTriangleFirst : cBaseEvenTriangleFirst {
    void AddSnowIndices(); // slot 0, 0x413390
};
struct cBaseEvenTriangleSecond : cTrianglePolygon {
    void AddIndices();        // slot 1, 0x4135e0
    void AddLayeredIndices(); // slot 2, 0x4138c0
};
struct cSnowEvenTriangleSecond : cBaseEvenTriangleSecond {
    void AddSnowIndices(); // slot 0, 0x4133f0
};
struct cBaseOddTriangleFirst : cTrianglePolygon {
    void AddIndices();        // slot 1, 0x4136a0 (folded with ::cBaseOddTriangleFirst's slot 0 and the gables' copies)
    void AddLayeredIndices(); // slot 2, 0x413990
};
struct cSnowOddTriangleFirst : cBaseOddTriangleFirst {
    void AddSnowIndices(); // slot 0, 0x413450
};
struct cBaseOddTriangleSecond : cTrianglePolygon {
    void AddIndices();        // slot 1, 0x413740
    void AddLayeredIndices(); // slot 2, 0x413a50
};
struct cSnowOddTriangleSecond : cBaseOddTriangleSecond {
    void AddSnowIndices(); // slot 0, 0x4134b0
};
} // namespace nTSSG

// The database index interface: its deleting destructor (slot 0) stores the interface's own vtable and frees 4 bytes.
struct cIGZDBPFIndex {
    static constexpr uint32_t kVTable = 0xe5fe14;
    cIGZDBPFIndex *DeletingDestructor(uint32_t flags); // slot 0, 0x9a7ca0
    const void *mVTable; // +0
};
static_assert(sizeof(cIGZDBPFIndex) == 4, "layout: the size 0x9a7ca0 frees");

// --- size-built (src/misc/ClassesRound4.size.cpp) ---

// The UI service (8 bytes): its destructor is only the vtable store.
struct cTSUIService {
    static constexpr uint32_t kVTable = 0xdf88e8;
    __declspec(noinline) void Destruct();                   // 0x75f31a (7 bytes, 12 callers)
    cTSUIService *DeletingDestructor(uint32_t flags); // slot 6, 0x75f321
    const void *mVTable; // +0
    uint32_t mUnknown4;  // +4
};
static_assert(sizeof(cTSUIService) == 8, "layout: the size 0x75f321 frees");

// A link in an HTML page (0x2c bytes): three child windows (+0x1c, +0x24, +0x28), each destroyed through its slot 33
// with 1 (tentative: the window's destroy-and-free), then the base's vtable (0xe5bf9c).
struct cHTMLLink {
    static constexpr uint32_t kVTable = 0xe5ce38;
    static constexpr uint32_t kBaseVTable = 0xe5bf9c;
    __declspec(noinline) void Destruct();          // 0x988ba0
    cHTMLLink *DeletingDestructor(uint32_t flags); // slot 0, 0x988df3
    const void *mVTable;      // +0
    uint32_t mUnknown4[6];    // +4
    void *mChild1c;           // +0x1c
    uint32_t mUnknown20;      // +0x20
    void *mChild24;           // +0x24
    void *mChild28;           // +0x28
};
static_assert(sizeof(cHTMLLink) == 0x2c && offsetof(cHTMLLink, mChild28) == 0x28, "layout");

// A clipboard entry of the clipboard manager (0x10 bytes): wraps a data object (+0xc, counted) and forwards queries for
// other interfaces to it.
struct cGZClipboardManagerClipboardData {
    static constexpr uint32_t kVTable = 0xe9ed84;
    static constexpr uint32_t kIid = 0x7ec0006; // cIGZClipboardData (tentative)
    bool QueryInterface(uint32_t riid, void **out);   // slot 0, 0xc6dd75: 1 / kIid -> this, AddRef
    bool QueryData(uint32_t riid, void **out);        // slot 4, 0xc6daf4: the data object's QueryInterface, or false
    __declspec(noinline) void Destruct();             // 0xc6d3dd: vtable, release the data object
    cGZClipboardManagerClipboardData *DeletingDestructor(uint32_t flags); // slot 6, 0xc6d456
    const void *mVTable;  // +0
    uint32_t mUnknown4;   // +4
    uint32_t mUnknown8;   // +8
    void *mData;          // +0xc
};
static_assert(sizeof(cGZClipboardManagerClipboardData) == 0x10, "layout: the size 0xc6d456 frees");

// 0xb39370 (cdecl, 12 bytes, 11 callers): 1 << bit.
__declspec(noinline) uint32_t __cdecl BitMask(uint32_t bit);

// The neighbourhood template browser's "not installed" filter (anonymous namespace, slot 0 Accept). An entry passes when
// it is of kind 4 (its slot 18), answers its slot 14 for 4, its category (slot 22, as a bit) is in mCategories, and no
// installed template (each of mInstalled's slot 10 gives a set whose slot 11 is asked with the entry's slot 8 (0)) has
// it. Meanings tentative.
struct cTSUninstalledNeighborhoodTemplatesFilter {
    bool Accept(void *entry); // slot 0, 0xc2f165 (ret 4)
    const void *mVTable;     // +0
    uint32_t mCategories;    // +4
    void **mInstalledBegin;  // +8
    void **mInstalledEnd;    // +0xc
};
static_assert(offsetof(cTSUninstalledNeighborhoodTemplatesFilter, mInstalledEnd) == 0xc, "layout");

// The font renderer that draws into a pixel buffer (8 bytes: vtable, count). Slots 5 and 6 hand the surface (second
// argument) the colour key 0x8080 through its slots 6 / 7 (meanings tentative: lock / unlock); slot 7 draws a run of
// glyphs (0x24 bytes each) through the blitter 0x90bbba, skipping glyphs whose +0x20 is under 0x20.
struct cGZBufferFontRenderer {
    static constexpr uint32_t kIid = 0xa8bc2a75; // cIGZFontRenderer
    struct Glyph {
        uint8_t mBitmap[0x10]; // +0 (handed to the blitter by address)
        uint32_t mPitch;       // +0x10
        uint32_t mUnknown14;   // +0x14
        int32_t mWidth;        // +0x18
        int32_t mHeight;       // +0x1c
        uint32_t mCode;        // +0x20 (character code: control characters under 0x20 draw nothing)
    };
    static_assert(sizeof(Glyph) == 0x24, "layout");
    bool QueryInterface(uint32_t riid, void **out); // slot 0, 0x90e210
    uint32_t Release();                             // slot 2, 0x90e41f: frees 8 at the last reference
    uint32_t BeginDraw(uint32_t a, void *surface, uint32_t c); // slot 5, 0x90bba9 (ret 0xc)
    uint32_t EndDraw(uint32_t a, void *surface, uint32_t c);   // slot 6, 0x90c118 (ret 0xc)
    bool DrawGlyphs(uint32_t a, uint32_t b, Glyph *glyphs, uint32_t count, uint32_t e, uint32_t f,
                    uint32_t g); // slot 7, 0x90c0af (ret 0x1c)
    const void *mVTable; // +0
    int32_t mRefCount;   // +4
};
static_assert(sizeof(cGZBufferFontRenderer) == 8, "layout: the size Release frees");

// The UI's drag-and-drop message filter (0x28 bytes): handlers by message id (an MSVC unordered_map, the same layout and
// library code as ours), and four drag messages answered by default when no handler takes them.
struct cTSWinDragAndDropMessageFilter {
    static constexpr uint32_t kVTable = 0xdf6ac4;
    static constexpr uint32_t kIid = 0xc6ae7085;
    struct Message {
        uint32_t mId;    // +0
        void *mData;     // +4
        uint32_t mArg[2]; // +8
    };
    using HandlerFn = bool(__cdecl *)(void *window, Message *message, void *context);
    struct Handler {
        HandlerFn mFn;     // node +0xc
        void *mContext;    // node +0x10
    };
    bool DoMessage(void *window, Message *message);                    // slot 3, 0x7452a6 (ret 8)
    __declspec(noinline) bool Dispatch(void *window, Message *message); // 0x74537f (ret 8): the id's handler, or false
    __declspec(noinline) void Destruct();                               // 0x74526d
    cTSWinDragAndDropMessageFilter *DeletingDestructor(uint32_t flags); // slot 4, 0x745287
    void SetHandler(uint32_t id, HandlerFn fn, void *context);          // slot 5, 0x7453d9 (ret 0xc)
    bool QueryInterface(uint32_t riid, void **out);                     // slot 0, 0x7453bb: kIid only
    uint32_t Release();                                                 // slot 2, 0x745404: slot 4 (1) at the last
    const void *mVTable;                            // +0
    int32_t mRefCount;                              // +4
    std::unordered_map<uint32_t, Handler> mHandlers; // +8
};
static_assert(sizeof(cTSWinDragAndDropMessageFilter) == 0x28, "layout: the size 0x745287 frees");

// The roof module's copies of the "second" triangles (no NotifyOwner slot: AddIndices is slot 0), size-built
// (src/misc/ClassesRound4.size.cpp). Same layout as nTSSG::cTrianglePolygon.
struct cBaseEvenTriangleSecond : nTSSG::cTrianglePolygon {
    void AddIndices(); // slot 0, 0x7a2c06
};
struct cBaseOddTriangleSecond : nTSSG::cTrianglePolygon {
    void AddIndices(); // slot 0, 0x7a2cb8
};
