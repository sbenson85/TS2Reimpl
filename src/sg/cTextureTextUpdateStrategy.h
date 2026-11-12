#pragma once
// nGZSceneGraph::cTextureTextUpdateStrategy (lane 261111m; docs/subsystems/scenegraph.md): the strategy of the debug
// text billboards (MakeBillboardTextOutput 0xaf3860, src/sg/SGUtilitiesText.cpp). A ring of text lines drawn top down in
// the "DebugFont" on a cleared surface. Game vtable 0xe7d44c, 0x1c bytes; base nRZSceneGraph::cTextureUpdateStrategy.
//
// Standalone shape: ours derives from the base strategy, every slot an override (slots 1, 2, 3 and 5 the base's, folded
// bodies in the game). The only creator is 0xaf3860 (hooked, ours), so the constructor 0xaf3560, the slots (QI 0xaf4100,
// Update 0xaf44b0, the deleting destructor 0xaf3780), Push 0xaf37f0 and Clear 0xaf40b0 (called only by 0xaf4250 /
// 0xaf42a0, hooked) are in effect through their callers. The vector helpers 0xaf3220 (destroy range), 0xaf3290
// (reallocate) and 0xaf34f0 (default-construct n) are the game's std::vector<Line> instantiation, our STL's here.
#include "sg/cTextureUpdateStrategy.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class cIGZFont;

namespace nGZSceneGraph {

class cTextureTextUpdateStrategy final : public nRZSceneGraph::cTextureUpdateStrategy {
public:
    static constexpr uint32_t kSize = 0x1c;
    static constexpr uint32_t kIid = 0xcb86c1bd; // what the label helpers ask of the texture's strategy (any iid answers)
    static constexpr uint32_t kOpaque = 0xff000000;

    // One line (0x24 bytes): its colour (A R G B as the ARGB face packs it; alpha forced to 0xff by Push) and a
    // cRZString of the text built and destroyed by hand. A copy duplicates the text (refcount 0); no move, as the game's.
    struct Line {
        uint32_t mColour = kOpaque; // +0: default opaque black
        union {
            cRZString mText; // +4
        };
        Line();
        Line(const Line &other);
        Line &operator=(const Line &) = delete;
        ~Line();
    };

    // 0xaf3560 (thiscall (height, fontSize), ret 8): the font system's "DebugFont" at `fontSize` (shared), quality 1;
    // height / the font's line spacing lines (unsigned; at least 1), each default.
    cTextureTextUpdateStrategy(uint32_t height, uint32_t fontSize);
    // The deleting destructor 0xaf3780 (slot 6): the lines freed first, then the font released.
    ~cTextureTextUpdateStrategy() override;
    cTextureTextUpdateStrategy(const cTextureTextUpdateStrategy &) = delete;
    cTextureTextUpdateStrategy &operator=(const cTextureTextUpdateStrategy &) = delete;

    // 0: 0xaf4100: any iid; with `out`, this (counted) and true; false for a null out.
    bool QueryInterface(uint32_t riid, void **out) override;
    // 4: 0xaf44b0: the surface cleared (every row byte 0), then the last lines pushed (the oldest first) one line
    // spacing apart from y 0, at x 4, each in its colour (the ARGB face's PackColor) with the font's second colour
    // 0x8000000.
    void Update(nGZGraphic4::cILockableSurface *surface, uint32_t face, bool first) override;

    // 0xaf37f0 (thiscall (colour, text), ret 8): the line count % size becomes {colour | 0xff000000, text}; count + 1.
    void Push(uint32_t colour, const char *text);
    // 0xaf40b0 (thiscall): every line back to the default (the vector emptied and resized to its size), count 0.
    void Clear();

    cIGZFont *mFont = nullptr; // +8, owned
    uint32_t mCount = 0;       // +0xc: lines pushed since the last Clear
    std::vector<Line> mLines;  // +0x10
};

static_assert(sizeof(cTextureTextUpdateStrategy::Line) == 0x24, "cTextureTextUpdateStrategy::Line: 0x24 bytes");
static_assert(sizeof(cTextureTextUpdateStrategy) == cTextureTextUpdateStrategy::kSize &&
                  offsetof(cTextureTextUpdateStrategy, mFont) == 8 &&
                  offsetof(cTextureTextUpdateStrategy, mCount) == 0xc &&
                  offsetof(cTextureTextUpdateStrategy, mLines) == 0x10,
              "cTextureTextUpdateStrategy layout");

} // namespace nGZSceneGraph
