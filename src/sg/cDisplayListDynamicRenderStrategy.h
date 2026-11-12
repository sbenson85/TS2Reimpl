#pragma once
// nTSSG::cDisplayListDynamicRenderStrategy (lane 261108g, scenegraph TS1b; lane IR named the forwarding slots): the
// scene graph's display-list render strategy for the main view. A cRZUnknown (vtable 0xdbbadc, count +4) with the TS
// strategy interface cITSDisplayListRenderStrategy at +8 (vtable 0xdbbafc, 11 slots), 0x194 bytes. The COM director's
// factory 0x372190 (class 0x0bf07804) builds it (new 0x194, the constructor 0x36e400) and answers its +8 face.
//
// It owns the render surfaces it draws into (src/sg/cTSRenderSurface.h): the back buffer, two render-target pairs
// ("dirty rect" targets, for the dirty-rectangle drawing), the matching render-texture pair, three reflection textures
// (screen, pool, ocean), the post-effects texture, the censor texture and one more texture surface. Slot 8 (0x375b00)
// builds them on first use, sizes them from the device's back buffer and the settings (antialiasing, debugRenderRes),
// and UpdateSurfaces (0x372a50) creates or destroys each one and picks the two "current" targets (+0x114, +0x118) from
// the settings (dynamicRenderStrategy, useTurboRect, useRenderTextures, reflections, post-processing). It also keeps a
// cDisplayListCensorRenderStrategy (+0xc) whose BeginPass / Invalidate / AddBox it forwards, and a list of registered
// items (slots 9 / 10).
//
// Drawing a pass (lane 261108k, row TS1c): the viewer keeps a frame state object for the strategy (class 0x6d548978,
// DynamicStrategyState: the drawing mode, the projection it last drew with, how many frames in a row the dirty-rect
// drawing has run, and a table of rectangles). Render (+8 slot 4) takes that state, notes whether the projection moved,
// reads the settings into the members (Prepare: the mode, scissoring, turbo rect, post-processing), then Draw empties
// the frame's rectangle lists and hands the items to one of the two drawing paths (0x3743e0 for the reflection type 3,
// 0x374670 for the others) and Finish copies the debug buffer and draws the dirty rectangles' outlines. The
// primary slot 6 (SetUpTarget) is what the drawing paths call to choose the surfaces for the pass, apply them, set the
// projection, viewport and depth range, and clear. Clear (+8 slot 5) starts a frame: the censor strategy's Clear, the
// frame count, the debug buffer and "render opaque UI" settings, the latter passed to the main window's children.
//
// Standalone shape (docs/standalone-shape.md): our class, our vtables. The constructor 0x36e400 and the factory 0x372190
// (held as data by the COM director's class table) are hooked; the destructors, every slot, slot 8's helpers, the draw
// steps and the member constructors (0x36e9c0, 0x36ed40) are reached only from our code. DisableScissor (0x3714b0) is
// hooked too (it was while the original drawing paths called it). Row TS1d (lane 261108m) made the outlines (0x371670
// and its drawer), the debug buffer's overlay (0x376840), the window callback (0x3752d0) and the frame state's class
// ours; the state's factory 0x3721f0 and destructor 0x36f5c0 are hooked (the director's class table and the deleting
// destructor 0x36fbb0 reach them), and the window property read 0x372670. Row TS1e (lane 261108o) made the paths'
// per-frame helpers ours (MarkRecords, ComputeBounds, SetUpScissor, hooked), row TS1f (lane 261108p) the frame's dirty
// rectangles (UpdateDirtyRects 0x3706a0, hooked, with the table's Update and the helper's window walk), row TS1g
// (lane 261108r) the item drawers and clears and row TS1i (lane 261108s) the two drawing paths 0x3743e0 / 0x374670,
// the side drawer 0x374bb0, the item list 0x373a70, the copies 0x371240 / 0x373f00 and the registered items' present
// 0x371150: reached only from Draw (ours), so in effect through it. The rectangle lists stay typed by layout here.
#include "gz/RenderStates.h"
#include "rz/GameAllocator.h"
#include "rz/RZUnknownBase.h"
#include "sg/cDisplayListCensorRenderStrategy.h"
#include "sg/cIntervalTree.h"
#include "sg/cShape.h"          // ShapeMaterialHandle
#include "sg/cTextureManager.h" // cTextureHandle
#include "ts/CountedRef.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <set>
#include <unordered_map>
#include <vector>

namespace nGZGraphic4 {
class cIDeviceDX;
class cISurface;
}
struct cGZRect;

namespace nTSSG {

class cITSRenderSurface;
class cTSRenderBackbufferSurface;
class cTSRenderTargetSurface;
class cTSRenderTextureSurface;

// The element types of the containers only TS1d's code fills (DynamicWords16 in the lists: a rectangle, min x, min y,
// max x, max y; the others are TS1d's to find). Plain words: the game's destructors free the blocks without running
// anything on the elements.
struct DynamicWords16 {
    uint32_t w[4];
};
struct DynamicWords24 {
    uint32_t w[6];
};
struct DynamicWords32 {
    uint32_t w[8];
};

// A registered item (the strategy's mRegistered holds pointers to them, slots 9 / 10; the registrant owns the block):
// a surface the finished frame is stretch-copied into by PresentRegistered (0x371150) while mPending is set.
struct DynamicRegisteredItem {
    nGZGraphic4::cISurface *mSurface; // +0 the copy's target (slot 6 IsCreated, slot 17 StretchRect)
    int32_t mRect[4];                 // +4 the target rectangle (a RECT)
    uint8_t mKeep;                    // +0x14 copied every frame: mPending stays set after a copy
    uint8_t mPending;                 // +0x15 a copy wanted
    uint8_t mDone;                    // +0x16 set by a copy that worked
    uint8_t mFailed;                  // +0x17 set by one that did not
};
static_assert(offsetof(DynamicRegisteredItem, mRect) == 4);
static_assert(offsetof(DynamicRegisteredItem, mKeep) == 0x14);
static_assert(offsetof(DynamicRegisteredItem, mFailed) == 0x17);

// A window the helper saw (the maps' values, keyed by the window's id; 0x18 bytes): its screen rectangle, the helper's
// frame it was last seen in, whether it was added to the dirty rectangles that frame and whether it was opaque.
struct DynamicWindowEntry {
    int32_t mRect[4];  // +0 min x, min y, max x, max y
    uint32_t mFrame;   // +0x10
    uint8_t mDirty;    // +0x14
    uint8_t mOpaque;   // +0x15
    uint8_t mPad16[2]; // +0x16 (Record stores whatever its stack held; ours zero)
};
static_assert(sizeof(DynamicWindowEntry) == sizeof(DynamicWords24));

// The helper (+0x18, 0x28 bytes; constructor 0x36ed40, destructor 0x36fa10): the dirty rectangles of the UI windows
// over the view (Collect, the dirty-rect drawing's), two hash maps of the windows seen, this frame's and the last (the
// game allocates them through RZMemory; map constructor 0x36ee00: 100 buckets asked, so 128) and a few words.
struct DynamicStrategyHelper {
    // The maps' hash is the key itself (the game's insert 0x36dfc0 hands the key to the bucket search as its hash). Its
    // operator() is deliberately not noexcept: with a noexcept hash, MSVC's unordered_map::clear unlinks a few nodes
    // bucket by bucket; without, it frees the list and resets every bucket, as the game's clear (0x376f90) always does.
    struct UnusedHash {
        size_t operator()(uint32_t key) const { return key; } // not noexcept, on purpose (see above)
    };
    using Map = std::unordered_map<uint32_t, DynamicWords24, UnusedHash, std::equal_to<uint32_t>,
                                   GameAllocator<std::pair<const uint32_t, DynamicWords24>>>;
    static constexpr size_t kMapBuckets = 100;

    // The dirty-rect frame's UI rectangles (row TS1f).
    // 0x372480: mByte24 set, mItems emptied, CollectWindows; returns mItems.
    std::vector<DynamicWords16> *Collect();
    // 0x3702c0: the area sums zeroed, the main window's children walked (WindowCallback, 0x370e40, with this), then the
    // rectangles of the last frame's windows not seen this frame added.
    void CollectWindows();
    // 0x370430 (ret 4): a rectangle appended to mItems, its area summed in mAddedArea.
    void Add(const int32_t *rect);
    // 0x370cd0 (ret 4): a visible window's rectangle added when it is new, moved, asked for (its property's bit 1) or
    // changed opacity (see the .cpp), and its entry recorded for this frame.
    void Window(cIGZUnknown *window);
    // 0x375a20 (ret 0x10): this frame's entry for window `id`, its area summed in mRecordedArea.
    void Record(uint32_t id, const int32_t *rect, uint8_t dirty, uint8_t opaque);
    // 0x370e40 (__cdecl, the window walk's callback, held as data by CollectWindows and itself): a visible window not
    // flagged 0x80 handed to Window, then its children walked the same way; false (stop the walk) when that walk says so.
    static bool __cdecl WindowCallback(cIGZUnknown *parent, uint32_t value, cIGZUnknown *window,
                                       DynamicStrategyHelper *helper);

    // 0x36ed40: an empty vector, the two maps made (new 0x20 each), the words zeroed (+0x1c / +0x20 left as they were).
    DynamicStrategyHelper();
    // 0x36fa10: the maps deleted (+0x10 first), then the vector.
    ~DynamicStrategyHelper();
    DynamicStrategyHelper(const DynamicStrategyHelper &) = delete;
    DynamicStrategyHelper &operator=(const DynamicStrategyHelper &) = delete;

    // 0x3755e0 (ret 4): a new frame number: the maps swapped (the frame that ends keeps its map at +0x10) and the one
    // now at +0x14 emptied; when the frame does not follow the last one, Reset as well. Nothing for the same frame.
    void FrameStep(uint32_t frame);
    // 0x370fc0: both maps emptied (+0x14 first) and the byte at +0x24 cleared.
    void Reset();

    std::vector<DynamicWords16> mItems; // +0 the UI's dirty rectangles (Collect)
    uint8_t mFlagC = 0;                 // +0xc renderOpaqueUI (UpdateDirtyRects): unchanged transparent windows only
                                        // redrawn when their property's bit 0 is set
    Map *mMap10;                        // +0x10 the last frame's (FrameStep)
    Map *mMap14;                        // +0x14 the frame's (FrameStep empties it)
    uint32_t mFrame = 0;                // +0x18 the frame of FrameStep
    // ORIGINAL-BUG: two area sums nothing reads (the constructor leaves them; CollectWindows zeroes them).
    uint32_t mRecordedArea;             // +0x1c Record's
    uint32_t mAddedArea;                // +0x20 Add's
    uint8_t mByte24 = 0;                // +0x24 set by Collect, cleared by Reset
    uint8_t mDebug = 0;                 // +0x25 Prepare: the dirty-rect debug drawing is on (mode 1, dirtyRectsDebug)
};
static_assert(sizeof(DynamicStrategyHelper::Map) == 0x20, "the game's map (new 0x20)");
static_assert(offsetof(DynamicStrategyHelper, mFlagC) == 0xc);
static_assert(offsetof(DynamicStrategyHelper, mMap10) == 0x10);
static_assert(offsetof(DynamicStrategyHelper, mFrame) == 0x18);
static_assert(offsetof(DynamicStrategyHelper, mByte24) == 0x24);
static_assert(offsetof(DynamicStrategyHelper, mDebug) == 0x25);
static_assert(sizeof(DynamicStrategyHelper) == 0x28);

// A rectangle record of the frame state's table (0x24 bytes): a rectangle and the frame it was last seen in, with the
// flags the outlines (0x371520) choose it by. The table's constructor (0x36e960) makes 1000 of them and zeroes only
// the frame, which is why the constructor is user-provided (a vector of them value-initialises nothing else).
struct DynamicRectRecord {
    DynamicRectRecord() { mFrame = 0; }

    // Collect (0x36fc40) writes every field but +0x18 and +0x23 for each item it is given, by the item's mUserData.
    int32_t mRect[4];       // +0 min x, min y, max x, max y: the item's pixel rectangle (DrawItemPixelRect)
    uint32_t mFrame;        // +0x10 the table's frame it was last drawn in (the outlines: within one frame of it)
    int32_t mSortKey;       // +0x14 the item's sort key
    uint32_t mUnknown18;    // +0x18 (never written)
    uint8_t mFlag1c;        // +0x1c the item changed (its rectangle recomputed); the outlines' kind 2
    uint8_t mFlag1d;        // +0x1d kind 4 (MarkRecords; Collect clears it)
    uint8_t mDrawn;         // +0x1e the rectangle is on screen; never outlined without it
    uint8_t mFlag1f;        // +0x1f as mFlag1c; kind 0x10 only without it
    uint8_t mFlag20;        // +0x20 an even sort key (sorted by material); kind 8 (and 0x10)
    uint8_t mUnknown21;     // +0x21 an even sort key and unchanged
    uint8_t mSecond;        // +0x22 the turbo-rect side; outlined only by the drawer of the same side
    uint8_t mUnknown23;     // +0x23 (never written)
};
static_assert(offsetof(DynamicRectRecord, mFrame) == 0x10);
static_assert(offsetof(DynamicRectRecord, mFlag1c) == 0x1c);
static_assert(offsetof(DynamicRectRecord, mSecond) == 0x22);
static_assert(sizeof(DynamicRectRecord) == 0x24);

// The frame state's table (DynamicStrategyState::mTable, 0x50 bytes; constructor 0x36e8b0, destructor 0x36f640): a
// record per draw item slot (the item's mUserData) and the dirty rectangles the items' changes give each frame (Update,
// row TS1f).
struct DynamicStrategyTable {
    static constexpr size_t kRecords = 1000;

    // 0x36e8b0: the vectors empty but the records' (1000 records, 0x36e960), the words zeroed (+0x44 / +0x48 left as
    // they were).
    DynamicStrategyTable() : mRecords(kRecords) {}
    // 0x36f640: the members in reverse order (the compiler's).
    ~DynamicStrategyTable() = default;
    DynamicStrategyTable(const DynamicStrategyTable &) = delete;
    DynamicStrategyTable &operator=(const DynamicStrategyTable &) = delete;

    // 0x375590 (ret 4): a new frame number: Reset unless it follows the last one, then the visible-record bitsets
    // swapped. Nothing for the same frame.
    void FrameStep(uint32_t frame);
    // 0x370f90: the records and the bitsets emptied and the byte at +0x4c cleared.
    void Reset();

    // The frame's dirty rectangles of the items (row TS1f; the dirty-rect drawing's, UpdateDirtyRects). The
    // arguments: the pass, its sorted items, whether the dirty-rect drawing has run two frames or more (the
    // rectangles are only gathered then), the strategy's mClearMode (the turbo-rect choice: 0 and 1 a side per
    // rectangle, 2 none), the pass's pixel rectangle, and whether last frame's records may stand for unchanged items.
    // 0x372430 (ret 0x1c): mFilled set, the rectangles and their sides emptied, then Collect; returns mDirtyRects.
    std::vector<DynamicWords16> *Update(nGZSceneGraph::Pass *pass, const nGZSceneGraph::DrawItem *items,
                                        uint32_t count, bool gather, uint32_t clearMode, const int32_t *pixelRect,
                                        bool useRecords);
    // 0x36fc40 (ret 0x1c): each item's record brought up to date and the rectangles it covered or covers now pushed
    // when it changed (see the .cpp), then (gathering) those of the records drawn last frame and not now
    // (PushVanished).
    void Collect(nGZSceneGraph::Pass *pass, const nGZSceneGraph::DrawItem *items, uint32_t count, bool gather,
                 uint32_t clearMode, const int32_t *pixelRect, bool useRecords);
    // 0x3703f0 (ret 4): a rectangle appended to mDirtyRects, its area summed in mDirtyArea.
    void Push(const int32_t *rect);
    // 0x370980 (ret 4): the rectangles of the records visible last frame (mLastVisible) and not this one pushed, by
    // the clear mode as Collect pushes a changed record's old rectangle.
    void PushVanished(uint32_t clearMode);
    // 0x372150 (ret 4): the record of an item's slot (its mUserData), null past the records.
    DynamicRectRecord *Record(const nGZSceneGraph::DrawItem *item);

    std::vector<bool> mDirtyTurbo;           // +0 per dirty rectangle: drawn on the turbo-rect side (modes 0 and 1)
    std::vector<DynamicWords16> mDirtyRects;  // +0x10 Update's
    std::vector<DynamicRectRecord> mRecords;  // +0x1c
    std::vector<uint32_t> mLastVisible;       // +0x28 last frame's mVisible (FrameStep swaps them)
    std::vector<uint32_t> mVisible;           // +0x34 a bit per record (index i: 0x80000000 >> (i & 31) of word i >> 5)
                                              // whose rectangle is on screen this frame
    uint32_t mFrame = 0;                      // +0x40 the frame of FrameStep
    // ORIGINAL-BUG: two area sums nothing reads or resets (not constructed; they wrap).
    uint32_t mRecordArea;                     // +0x44 Collect's: the records' rectangles
    uint32_t mDirtyArea;                      // +0x48 Push's
    uint8_t mFilled = 0;                      // +0x4c Prepare counts dirty-rect frames only while it is set (Reset clears it)
    uint8_t mDebug = 0;                       // +0x4d Prepare: as DynamicStrategyHelper::mDebug
};
static_assert(sizeof(std::vector<bool>) == 0x10, "the game's vector<bool>: a word vector and the bit count");
static_assert(offsetof(DynamicStrategyTable, mDirtyRects) == 0x10);
static_assert(offsetof(DynamicStrategyTable, mRecords) == 0x1c);
static_assert(offsetof(DynamicStrategyTable, mLastVisible) == 0x28);
static_assert(offsetof(DynamicStrategyTable, mVisible) == 0x34);
static_assert(offsetof(DynamicStrategyTable, mRecordArea) == 0x44);
static_assert(offsetof(DynamicStrategyTable, mFrame) == 0x40);
static_assert(offsetof(DynamicStrategyTable, mFilled) == 0x4c);
static_assert(offsetof(DynamicStrategyTable, mDebug) == 0x4d);
static_assert(sizeof(DynamicStrategyTable) == 0x50);

// The frame state the viewer keeps for the strategy (its +0xa8 reference, the viewer's slots 99 / 116; class 0x6d548978,
// a counted COM object of 0x64 bytes). Its slots are src/misc/ClassesRound3's (on the game's vtable 0xdbbac8, which the
// constructor stores; the deleting destructor 0x36fbb0 there reaches the destructor below through its hook). The
// factory, constructor and destructor are ours.
struct DynamicStrategyState {
    // The drawing modes (dynamicRenderStrategy): everything each frame, dirty rectangles only, and dirty rectangles kept
    // in the dirty-rect surfaces (mCurrent / mCurrentSecond; the only mode with the turbo rect).
    static constexpr uint32_t kFull = 0, kDirtyRects = 1, kDirtySurfaces = 2;
    static constexpr uint32_t kVTable = 0xdbbac8;

    // 0x36e790: the game's vtable, every word zeroed, a new table (none when the allocation fails).
    DynamicStrategyState();
    // 0x36f5c0 (hooked): the game's vtable stored, the table deleted, then the vector.
    ~DynamicStrategyState();
    DynamicStrategyState(const DynamicStrategyState &) = delete;
    DynamicStrategyState &operator=(const DynamicStrategyState &) = delete;
    // 0x3721f0 (__cdecl; hooked, the COM director's class 0x6d548978): a new state, its count 0 (null when the
    // allocation fails).
    static DynamicStrategyState *Create();

    cIGZUnknown *Unknown() { return reinterpret_cast<cIGZUnknown *>(this); }

    const void *mVTable;                  // +0 cIGZUnknown
    uint32_t mMode = 0;                   // +4 this frame's
    uint32_t mLastMode = 0;               // +8 the last frame's (Prepare)
    uint32_t mProjection[16] = {};        // +0xc the matrix the last frame drew with (Render; the pass's mMatrices[4] bits)
    DynamicStrategyTable *mTable;         // +0x4c
    uint32_t mFrames = 0;                 // +0x50 dirty-rect frames in a row with an unchanged view (0: draw everything)
    std::vector<DynamicWords16> mRects54; // +0x54 (filled by the drawing paths, row TS1e)
    uint32_t mRefCount = 0;               // +0x60 (ClassesRound3's AddRef / Release)
};
static_assert(offsetof(DynamicStrategyState, mProjection) == 0xc);
static_assert(offsetof(DynamicStrategyState, mTable) == 0x4c);
static_assert(offsetof(DynamicStrategyState, mRects54) == 0x54);
static_assert(offsetof(DynamicStrategyState, mRefCount) == 0x60);
static_assert(sizeof(DynamicStrategyState) == 0x64);

// 0x36d3c0 (__cdecl): whether two 4 x 4 matrices differ: some element pair 32 or more representable floats apart (the
// bit patterns mapped to a signed order; the distance taken as a - b, a difference of -32 itself still counting as
// close), checked from the first element on.
bool MatricesDiffer(const uint32_t *a, const uint32_t *b);

// A window's (cGZWin) number-valued property, as the opaque-UI callback and the drawing paths keep one (id 0x300d422):
// 0x372670 (__cdecl; hooked, the draw path's 0x370cd0 calls it too): the variant the window's slot 126 answers
// (counted) read with its GetValUint32 (slot 26) into a zeroed word, 0 without one.
uint32_t WindowPropertyUint32(cIGZUnknown *window, uint32_t id);
// 0x375660 (__cdecl): the variant's SetValUint32 (slot 27); without one a new cRZVariant set and handed to the window's
// slot 127.
void SetWindowPropertyUint32(cIGZUnknown *window, uint32_t id, uint32_t value);

// The member at +0x98 (0x28 bytes; constructor 0x36e9c0, its destructor inline in 0x36f220): the interval trees of the
// dirty rectangles (src/sg/cIntervalTree.h, lane 261108o), one of their x spans and one of their y spans, with the nodes
// in its own vector, and the two sets a query fills.
struct DynamicStrategyTrees {
    using Node = cIntervalTree::Node;

    // 0x36e9c0: the sets and vector empty, then each tree made (new 0x10, the constructor 0x401910 with 0: the nodes are
    // the vector's; none when the allocation fails) and stored counted.
    DynamicStrategyTrees();
    DynamicStrategyTrees(const DynamicStrategyTrees &) = delete;
    DynamicStrategyTrees &operator=(const DynamicStrategyTrees &) = delete;

    // 0x370b70 (ret 4): both trees emptied (unchecked), the node vector resized to two per rectangle, then for each
    // rectangle of rects, numbered from 1, a node of its x span inserted into mXTree and one of its y span into mYTree.
    void Build(const std::list<DynamicWords16> *rects);
    // 0x373750 (ret 4): whether rect (min x, min y, max x, max y) meets one of the built rectangles: the rectangles whose
    // x span meets rect's into mXHits, then (when there are any) those whose y span does into mYHits, true when both
    // sets share a rectangle (HitsShareRectangle).
    bool Overlaps(const int32_t *rect);

    fx::CountedRef<cIntervalTree> mXTree;  // +0
    fx::CountedRef<cIntervalTree> mYTree;  // +4
    cIntervalTree::NodeSet mXHits;         // +8
    cIntervalTree::NodeSet mYHits;         // +0x10
    std::vector<Node> mNodes;              // +0x18
    uint32_t mNodeCount;                   // +0x24 the nodes Build has used (the constructor leaves it)
};
static_assert(offsetof(DynamicStrategyTrees, mXHits) == 8);
static_assert(offsetof(DynamicStrategyTrees, mNodes) == 0x18);
static_assert(offsetof(DynamicStrategyTrees, mNodeCount) == 0x24);
static_assert(sizeof(DynamicStrategyTrees) == 0x28);

// 0x3735b0 (__cdecl): whether two runs of nodes, each sorted by value, share a value (a sorted merge).
bool HitsShareRectangle(cIntervalTree::NodeSet::const_iterator first1, cIntervalTree::NodeSet::const_iterator last1,
                        cIntervalTree::NodeSet::const_iterator first2, cIntervalTree::NodeSet::const_iterator last2);

// The two render states the strategy keeps (constructed inline by 0x36e400; their destructors store cStateHeader's
// vtable): a scissor test (id 0x20) and a viewport (id 1), both over the whole view in the fractions the scene graph's
// viewports use: left / top 0, right / bottom 1.0 (the float's bits in the integer words). The viewport's depth words
// are left as they were.
struct DynamicScissor : nGZGraphic4::cScissorTestState {
    DynamicScissor();
    ~DynamicScissor() { mVTable = cStateHeader::VTable(); }
    DynamicScissor(const DynamicScissor &) = delete;
    DynamicScissor &operator=(const DynamicScissor &) = delete;
};
struct DynamicViewport : nGZGraphic4::cViewportState {
    DynamicViewport();
    ~DynamicViewport() { mVTable = cStateHeader::VTable(); }
    DynamicViewport(const DynamicViewport &) = delete;
    DynamicViewport &operator=(const DynamicViewport &) = delete;
};

class cDisplayListDynamicRenderStrategy final : public rz::cRZUnknown, public cITSDisplayListRenderStrategy {
public:
    static constexpr uint32_t kSize = 0x194;
    static constexpr uint32_t kClassId = 0x0bf07804; // the COM director's class (factory 0x372190)

    // 0x36e400: see the .cpp.
    cDisplayListDynamicRenderStrategy();
    // 0x36f220 (deleting 0x36fb80, slot 3): the surfaces' device objects released (ReleaseSurfaceResources), every
    // surface deleted, the helper deleted, the censor strategy released, then the members.
    ~cDisplayListDynamicRenderStrategy() override;

    // 0x373cd0 (folded with the censor strategy's): the two strategy interfaces answer the +8 face, counted.
    bool QueryInterface(uint32_t iid, void **out) override;
    uint32_t AddRef() override { return rz::cRZUnknown::AddRef(); }
    uint32_t Release() override { return rz::cRZUnknown::Release(); }
    // 0x374dd0 (primary slot 6, ret 4): the pass's target set up and cleared; see the .cpp.
    virtual void SetUpTarget(nGZSceneGraph::Pass *pass);

    // 0x3703e0 (+8 slot 3): the censor strategy's BeginPass.
    void BeginPass(nGZSceneGraph::Pass *pass) override;
    // 0x374930 (+8 slot 4): draws the sorted items; see the .cpp.
    void Render(nGZSceneGraph::Pass *pass, nGZSceneGraph::DrawItem *items, uint32_t count) override;
    // 0x373920 (+8 slot 5): a new frame; see the .cpp.
    void Clear() override;
    // 0x373690 (+8 slot 6): mInvalidated set, then the censor strategy's Invalidate.
    void Invalidate() override;
    // 0x3703d0 (+8 slot 7): the censor strategy's AddBox.
    void AddBox(const nGZCompGeom::BoundBox *box) override;
    // 0x375b00 (+8 slot 8): the surfaces built (the first time), sized and updated; see the .cpp.
    void Slot8() override;
    // 0x370380 (+8 slot 9): the item appended unless it is registered already.
    void Register(void *item) override;
    // 0x373de0 (+8 slot 10): the item's first registration removed, if any.
    void Unregister(void *item) override;

    cDisplayListDynamicRenderStrategy(const cDisplayListDynamicRenderStrategy &) = delete;
    cDisplayListDynamicRenderStrategy &operator=(const cDisplayListDynamicRenderStrategy &) = delete;

    // 0x372a50 (ret 4): each surface created or destroyed by the settings, the current targets chosen.
    void UpdateSurfaces(nGZGraphic4::cIDeviceDX *device);
    // 0x3737f0 (ret 4; `this` unused): whether the device can stretch-copy between two 16 x 16 depth-stencil
    // surfaces of the D24S8 kind (the "turbo rect" path).
    bool TurboRectSupported(nGZGraphic4::cIDeviceDX *device);
    // 0x373d70: the targets' surfaces and the textures' render-target textures released (no null checks).
    void ReleaseSurfaceResources();

    // The draw steps (Render's callees; see the .cpp).
    // 0x371b10 (ret 8): the settings read into the members and the frame state's mode and frame count updated.
    void Prepare(nGZSceneGraph::Pass *pass, bool viewChanged);
    // 0x373e20 (ret 0xc): the frame's lists emptied, the items drawn by the pass type's path, then Finish.
    void Draw(nGZSceneGraph::Pass *pass, nGZSceneGraph::DrawItem *items, uint32_t count);
    // 0x376460 (ret 4): the debug buffer shown and the dirty rectangles' outlines drawn, or the rectangles kept for the
    // next pass.
    void Finish(nGZSceneGraph::Pass *pass);
    // 0x3714b0 (ret 4; hooked): the scissor test turned off on the pass's device state.
    void DisableScissor(nGZSceneGraph::Pass *pass);
    // The drawing paths and their own callees (row TS1i, lane 261108s; reached only from Draw, see the .cpp).
    // 0x3743e0 (ret 0xc): the reflection pass type's path: the dirty rectangles, then the reflection's items drawn into
    // mTarget (whole in the first two dirty-rect frames, else only in the frame's rectangles) under the whole-surface
    // viewport, the others under the pass's.
    void DrawOwnPath(nGZSceneGraph::Pass *pass, nGZSceneGraph::DrawItem *items, uint32_t count);
    // 0x374670 (ret 0xc): the other types' path: a whole frame (the dirty-rect drawing's first) or the dirty
    // rectangles drawn on both sides, then Present and the registered items' copies.
    void DrawPath(nGZSceneGraph::Pass *pass, nGZSceneGraph::DrawItem *items, uint32_t count);
    // 0x374bb0 (ret 8): one side's items (`which`: 0 mRects into mTarget, 1 mTurboRects into mTargetSecond) drawn in
    // its rectangles, or all of them before two dirty-rect frames.
    void DrawSide(nGZSceneGraph::Pass *pass, uint8_t which);
    // 0x373a70 (ret 0xc; the pass unused): mItems / mItemRecords set to the items and their records.
    void ListItems(nGZSceneGraph::Pass *pass, nGZSceneGraph::DrawItem *items, uint32_t count);
    // 0x371150: in the dirty-rect drawing, mFinal's colour surface stretch-copied into each registered item that wants it.
    void PresentRegistered();
    // 0x371240: mTarget copied into mTargetSecond (with the turbo rect from the second frame on: the depth, then the
    // colour of the turbo rectangles only).
    void CopyToSecond();
    // 0x373f00 (ret 4): the frame copied into mFinal (through the post-processing items when they run).
    void Present(nGZSceneGraph::Pass *pass);
    // The drawing paths' callees below (rows TS1e-TS1g) are kept out of line: they are hooked at their RVAs, and the
    // paths' difftest (tests/sg_display_list_dynamic_draw.cpp) stands them in on both sides.
    // The drawing paths' per-frame helpers (row TS1e; each hooked, as the original paths called them; ours do now).
    // `which` is the turbo-rect side (0 mRects / mBounds, 1 mTurboRects / mTurboBounds).
    // 0x3724a0 (ret 0x14): the records' +0x1d flags for this frame (their overlap with `rects`; see the .cpp).
    __declspec(noinline) void MarkRecords(const std::list<DynamicWords16> *rects, nGZSceneGraph::Pass *pass, bool turbo, bool test,
                     uint8_t which);
    // 0x370eb0 (ret 8): the side's bounds from `rects`, clipped to the pass's pixels.
    __declspec(noinline) void ComputeBounds(const std::list<DynamicWords16> *rects, uint8_t which);
    // 0x3719a0 (ret 8): the scissor test set to the side's bounds and applied; false when it is not.
    __declspec(noinline) bool SetUpScissor(nGZSceneGraph::Pass *pass, uint8_t which);
    // The drawing paths' item drawers and clears (row TS1g, lane 261108r; each hooked while the paths were the
    // original's, and kept). Byte flags are compared whole, as the original does (a value
    // above 1 matches neither side of a test against a comparison's 0 or 1).
    // 0x3743a0 (ret 8): one item drawn (its material's slot 11), the kept scissor state applied first while the .bss
    // toggle 0x10d9d24 and useScissoring are both on; the pass's draw count up.
    void DrawOneItem(nGZSceneGraph::Pass *pass, nGZSceneGraph::DrawItem *item);
    // 0x374350 (ret 0x10): of `count` items in a row, those whose sort key is at least 11000 (`late`) or below it.
    __declspec(noinline) void DrawItemRun(nGZSceneGraph::Pass *pass, nGZSceneGraph::DrawItem *items, uint32_t count, uint8_t late);
    // 0x374270 (ret 8): the frame's items whose records are on screen, the reflection's own (sort key -9998) or the
    // others by `reflection`; without `reflection` in a dirty-rect frame (the state's mode 2, two or more frames) only
    // the records marked this frame (+0x1d).
    __declspec(noinline) void DrawReflectionItems(nGZSceneGraph::Pass *pass, uint8_t reflection);
    // 0x374100 (ret 8): the frame's items whose records are on screen, picked by `mode` 0..7 (see the .cpp); in a
    // dirty-rect frame (mode 1 or 2, two or more frames) a picked item below 11000 only when marked this frame.
    __declspec(noinline) void DrawItemsBy(nGZSceneGraph::Pass *pass, uint32_t mode);
    // 0x374ad0 (ret 4): the censor's drawn boxes listed; with any, the records marked against them and the censor's
    // texture drawn (RenderTexture with the frame's items), else the frame state's kept rectangles emptied.
    __declspec(noinline) void DrawCensor(nGZSceneGraph::Pass *pass);
    // 0x373b70 (ret 0xc): outside the debug modes above 3, each of `rects` cleared on the side's target (`second`:
    // mTargetSecond, else mTarget; the flags (second ^ 1) * 2 + 4, so 4 or 6), scaled by the target's aspect times its
    // scale unless that is 1.
    __declspec(noinline) void ClearRects(nGZSceneGraph::Pass *pass, const std::list<DynamicWords16> *rects, uint8_t second);
    // The frame's dirty rectangles (row TS1f; hooked while the drawing paths were the original's).
    // 0x3706a0 (ret 0x10): the UI's changed rectangles (the helper's), the items' (the table's) and the censor's,
    // merged into mRects / mTurboRects (AddRect) by side; see the .cpp. `useRecords` (the drawing path 0x374670
    // passes mRedraw): last frame's records may stand for the items that did not change.
    __declspec(noinline) void UpdateDirtyRects(nGZSceneGraph::Pass *pass, const nGZSceneGraph::DrawItem *items, uint32_t count,
                          bool useRecords);
    // 0x370470 (ret 8): a non-empty rectangle merged into the list element whose union with it wastes least area, when
    // that waste is at most mMergeThreshold (the union then re-merged when it grew the element), or appended.
    void AddRect(std::list<DynamicWords16> *list, const int32_t *rect);
    // 0x375240 (__thiscall on a flag byte, ret 4): the flag set and handed with OpaqueUICallback to the main window's
    // slot 37 (with id 0x22ba0121).
    static void MarkOpaqueUI(uint8_t *flag, uint8_t value);
    // 0x3752d0 (__cdecl, the window walk's callback: cGZWin::vf37 0x930cff calls it for each child with the parent, the
    // child's slot 78 answer, the child and the walk's data): a visible window's opaque-UI state brought in line with the
    // flag the data points at, then the walk carried into its children; false stops the walk. See the .cpp.
    static bool __cdecl OpaqueUICallback(cIGZUnknown *parent, uint32_t value, cIGZUnknown *window, uint8_t *flag);
    // The outlines and the debug-buffer overlay (Finish's callees; see the .cpp).
    // 0x371670 (ret 0x1c): the dirty rectangles' outlines in the dirty-rect debug modes (dirtyRectsDebug 1 and 2).
    void DrawOutlines(DynamicStrategyState *state, DynamicStrategyTable *table, DynamicStrategyHelper *helper,
                      std::list<DynamicWords16> *rects, const int32_t *bounds, const int32_t *pixels, bool second);
    // 0x376840 (ret 8): the stencil buffer shown over the back buffer (debugStencil) after a debug buffer's copy.
    void DrawDebugOverlay(nGZSceneGraph::Pass *pass, cITSRenderSurface *surface);

    fx::CountedRef<cDisplayListCensorRenderStrategy> mCensor; // +0xc
    fx::CountedRef<cIGZUnknown> mDirector10;  // +0x10 a colour brush (class 0xdc012582, cIColorBrush), Resize(1)
    fx::CountedRef<cIGZUnknown> mDirector14;  // +0x14 an outline colour brush (class 0x9c1d3c77)
    DynamicStrategyHelper *mHelper;           // +0x18
    nGZSceneGraph::ShapeMaterialHandle mMaterial1c; // +0x1c
    nGZSceneGraph::cTextureHandle mTexture20;               // +0x20
    nGZSceneGraph::ShapeMaterialHandle mMaterial24; // +0x24
    nGZSceneGraph::ShapeMaterialHandle mMaterial28; // +0x28
    uint32_t mInvalidated = 0;                // +0x2c set by Invalidate (slot 6)
    std::vector<nGZSceneGraph::DrawItem *> mItems;   // +0x30 the items the drawing paths drew (row TS1g fills it)
    std::vector<DynamicRectRecord *> mItemRecords;   // +0x3c each item's record, at the same index
    std::list<DynamicWords16> mRects;         // +0x48 this frame's dirty rectangles (Draw empties it; TS1d's paths fill it)
    std::list<DynamicWords16> mTurboRects;    // +0x50 the turbo rect pass's
    int32_t mBounds[4];                       // +0x58 mRects' bounds (Draw: the empty rectangle; not constructed)
    int32_t mTurboBounds[4];                  // +0x68 mTurboRects'
    uint8_t mUseScissoring = 0;               // +0x78 useScissoring (Prepare; off unless the turbo rect may run)
    uint8_t mByte79;                          // +0x79 cleared by Prepare (not constructed)
    uint8_t mUnset7a[2];                      // +0x7a
    DynamicScissor mScissor;                  // +0x7c
    DynamicStrategyTrees mTrees;              // +0x98
    uint32_t mDebugMode = 0;                  // +0xc0 dirtyRectsDebug (Prepare; 0 outside the dirty-rect drawing)
    uint8_t mRenderOpaqueUI = 0;              // +0xc4 renderOpaqueUI (Clear)
    uint8_t mDisableIntervalTrees;            // +0xc5 dirtyRectsDisableIntervalTrees (Prepare; not constructed)
    uint8_t mUnsetC6[2];                      // +0xc6
    uint32_t mMergeThreshold;                 // +0xc8 dirtyRectsMergeThreshold (Prepare)
    uint8_t mTurbo = 0;                       // +0xcc useTurboRect, cleared when the turbo rect cannot run (Prepare)
    uint8_t mUnsetCD;                         // +0xcd
    uint8_t mDirtyRects;                      // +0xce the dirty-rect drawing may run (Prepare: the main view's type 0)
    uint8_t mUnsetCF;                         // +0xcf
    uint8_t mRedraw;                          // +0xd0 the view is drawn whole this frame (Prepare)
    uint8_t mPostProcessing;                  // +0xd1 enablePostProcessing (Prepare)
    uint8_t mReducedResolution = 0;           // +0xd2 debugRenderRes shrank the dirty-rect surfaces (slot 8)
    uint8_t mUnsetD3;                         // +0xd3
    DynamicStrategyState *mState = nullptr;   // +0xd4 the viewer's frame state, during Render (not counted here)
    uint32_t mFrame = 0;                      // +0xd8 counted by Clear
    int32_t mPixelRect[4];                    // +0xdc the pass's (Prepare)
    uint32_t mDebugBuffer = 0;                // +0xec debugRenderBuffer (Clear): which buffer Finish shows, 0 none
    cITSRenderSurface *mTarget;               // +0xf0 SetUpTarget's choices: the surface drawn into
    cITSRenderSurface *mTargetSecond;         // +0xf4 the turbo rect's
    cITSRenderSurface *mTargetPost;           // +0xf8 the post-processing one, or none
    uint32_t mUnsetFC;                        // +0xfc
    cITSRenderSurface *mFinal;                // +0x100 the surface the frame ends in
    cTSRenderTargetSurface *mDirtyTarget1 = nullptr;    // +0x104 "DirtyRectTarget1"
    cTSRenderTargetSurface *mDirtyTarget2 = nullptr;    // +0x108 "DirtyRectTarget2"
    cTSRenderTextureSurface *mDirtyTexture1 = nullptr;  // +0x10c "DirtyRectTexture1"
    cTSRenderTextureSurface *mDirtyTexture2 = nullptr;  // +0x110 "DirtyRectTexture2"
    cITSRenderSurface *mCurrent;              // +0x114 the dirty-rect surface in use (UpdateSurfaces; not constructed)
    cITSRenderSurface *mCurrentSecond;        // +0x118 its turbo-rect partner, or none
    cTSRenderTextureSurface *mScreenReflection = nullptr; // +0x11c
    cTSRenderTextureSurface *mPoolReflection = nullptr;   // +0x120
    cTSRenderTextureSurface *mOceanReflection = nullptr;  // +0x124
    cTSRenderTextureSurface *mCensorTexture = nullptr;    // +0x128 "CensorTexture" (CensorMaterial)
    cTSRenderTextureSurface *mPostEffects = nullptr;      // +0x12c "@@PostEffectsTexture@@" (postprocessingmaterial)
    cTSRenderTextureSurface *mTexture130 = nullptr;       // +0x130 never named (sized 0 x 0 by slot 8)
    cTSRenderBackbufferSurface *mBackbuffer = nullptr;    // +0x134 "BackbufferTarget"
    std::vector<void *> mRegistered;          // +0x138 slots 9 / 10 (DynamicRegisteredItem pointers)
    uint32_t mClearMode;                      // +0x144 Prepare: 0 the turbo rect with useSplitRect, 1 without, 2 no turbo
    // A reflection pass's results, kept by Finish for debugRenderBuffer 4 (the screen reflection shown).
    DynamicStrategyState *mLastState = nullptr; // +0x148
    std::list<DynamicWords16> mLastRects;     // +0x14c
    int32_t mLastBounds[4];                   // +0x154
    int32_t mLastPixelRect[4];                // +0x164
    DynamicViewport mViewport;                // +0x174
};
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mCensor) == 0xc);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mHelper) == 0x18);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mMaterial1c) == 0x1c);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mInvalidated) == 0x2c);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mItems) == 0x30);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mItemRecords) == 0x3c);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mRects) == 0x48);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mTurboRects) == 0x50);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mBounds) == 0x58);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mTurboBounds) == 0x68);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mUseScissoring) == 0x78);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mScissor) == 0x7c);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mTrees) == 0x98);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mDebugMode) == 0xc0);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mRenderOpaqueUI) == 0xc4);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mMergeThreshold) == 0xc8);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mTurbo) == 0xcc);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mDirtyRects) == 0xce);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mRedraw) == 0xd0);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mReducedResolution) == 0xd2);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mState) == 0xd4);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mPixelRect) == 0xdc);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mDebugBuffer) == 0xec);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mTarget) == 0xf0);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mFinal) == 0x100);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mDirtyTarget1) == 0x104);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mCurrent) == 0x114);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mScreenReflection) == 0x11c);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mBackbuffer) == 0x134);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mRegistered) == 0x138);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mClearMode) == 0x144);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mLastState) == 0x148);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mLastRects) == 0x14c);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mLastBounds) == 0x154);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mLastPixelRect) == 0x164);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mViewport) == 0x174);
static_assert(sizeof(cDisplayListDynamicRenderStrategy) == cDisplayListDynamicRenderStrategy::kSize);

// The hooks' entry points (src/hook/tables/cDisplayListDynamicRenderStrategy.cpp): original calling convention, our
// bodies.
struct cDisplayListDynamicRenderStrategyAdapters {
    using Self = cDisplayListDynamicRenderStrategy;
    // 0x36e400 (__thiscall on the 0x194-byte block): our object built there.
    static Self *__fastcall Construct(Self *memory, void *);
    // 0x372190 (__cdecl): a new object, as its +8 interface (null when the allocation fails).
    static cITSDisplayListRenderStrategy *__cdecl Create();
    // 0x3714b0 (__thiscall, ret 4): DisableScissor.
    static void __fastcall DisableScissor(Self *self, void *, nGZSceneGraph::Pass *pass);
    // 0x3724a0 (__thiscall, ret 0x14), 0x370eb0 (ret 8), 0x3719a0 (ret 8): the members of the same names. The byte
    // arguments arrive as dwords whose low byte the original reads.
    static void __fastcall MarkRecords(Self *self, void *, const std::list<DynamicWords16> *rects,
                                       nGZSceneGraph::Pass *pass, uint32_t turbo, uint32_t test, uint32_t which);
    static void __fastcall ComputeBounds(Self *self, void *, const std::list<DynamicWords16> *rects, uint32_t which);
    static bool __fastcall SetUpScissor(Self *self, void *, nGZSceneGraph::Pass *pass, uint32_t which);
    // 0x3706a0 (__thiscall, ret 0x10): UpdateDirtyRects (the byte argument as a dword).
    static void __fastcall UpdateDirtyRects(Self *self, void *, nGZSceneGraph::Pass *pass,
                                            const nGZSceneGraph::DrawItem *items, uint32_t count, uint32_t useRecords);
    // 0x374350 (ret 0x10), 0x374270 (ret 8), 0x373b70 (ret 0xc): the members of the same names (row TS1g), the byte
    // argument as a dword.
    static void __fastcall DrawItemRun(Self *self, void *, nGZSceneGraph::Pass *pass, nGZSceneGraph::DrawItem *items,
                                       uint32_t count, uint32_t late);
    static void __fastcall DrawReflectionItems(Self *self, void *, nGZSceneGraph::Pass *pass, uint32_t reflection);
    static void __fastcall ClearRects(Self *self, void *, nGZSceneGraph::Pass *pass,
                                      const std::list<DynamicWords16> *rects, uint32_t second);
    // 0x3721f0 (__cdecl, the COM director's class 0x6d548978): DynamicStrategyState::Create.
    static DynamicStrategyState *__cdecl CreateState();
    // 0x36f5c0 (__thiscall): the state's destructor (ClassesRound3's deleting destructor 0x36fbb0 calls it).
    static void __fastcall DestroyState(DynamicStrategyState *self, void *);
};
// 0x36f5c0's hooked entry: DestroyState with the state in ecx (ClassesRound3's 0x36fbb0 calls it).
void __fastcall DestroyDynamicStrategyState(void *state);

// The dirty rectangles' free helpers (row TS1f, __cdecl; reached only from our code).
// 0x372270: the item's pixel rectangle (its bounds, by its placement and the pass's view-projection, rounded out),
// clipped to `pixelRect`; false (the empty rectangle when behind the eye) when nothing is left. See the .cpp.
bool DrawItemPixelRect(int32_t *out, const int32_t *pixelRect, nGZSceneGraph::Pass *pass,
                       const nGZSceneGraph::DrawItem *item);
// 0x372600: the window's rectangle in screen pixels (its own, offset by each parent's position).
void WindowScreenRect(cIGZUnknown *window, int32_t *out);
// 0x3726f0: the window's id (slot 80).
uint32_t WindowId(cIGZUnknown *window);
// 0x3738b0: whether the window covers what is under it (opaque fill colour, or its flags; see the .cpp).
bool WindowOpaque(cIGZUnknown *window);

} // namespace nTSSG
