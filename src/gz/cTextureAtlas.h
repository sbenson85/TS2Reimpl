#pragma once
// nGZGraphic4's texture atlas packer (1.18.0.187; docs/subsystems/rendering.md, R4b step 1, lane OL): a binary tree of
// rectangles carving one texture into regions. cDraw2D keeps three pools of them (src/gz/cDraw2D.h); the scenegraph's
// render-target texture manager and a few other callers use the same functions, so each is hooked at its own RVA.
//
// A node is a rectangle of the texture; a leaf is free or used (one region handed out), a node with children is split
// into two (the first child holds the corner). Freeing a region merges two free sibling leaves back into their parent,
// all the way up. Node 0 is the whole texture (Init). Node indices stay put (the tree is a vector): merged-away nodes go
// on a free list and the next split reuses them before the vector grows.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "gz/cIGZBuffer.h" // cGZRect

namespace nGZGraphic4 {

// A node of the packing tree (0x20 bytes).
struct cAtlasNode {
    cGZRect rect;    // +0 the part of the texture it covers
    int32_t parent;  // +0x10 -1 for the root
    int32_t first;   // +0x14 the first child, -1 for a leaf
    int32_t second;  // +0x18 the second child
    bool used;       // +0x1c a leaf handed out
    uint8_t pad[3];  // +0x1d left as the allocation had it by the original (zero here)

    cAtlasNode() : rect{0, 0, 0, 0}, parent(-1), first(-1), second(-1), used(false), pad{} {} // 0x18bb00
};
static_assert(sizeof(cAtlasNode) == 0x20, "cAtlasNode: 0x20 bytes");
static_assert(offsetof(cAtlasNode, parent) == 0x10 && offsetof(cAtlasNode, used) == 0x1c, "node layout");

// The packer (0x20 bytes).
class cTextureAtlas {
public:
    static constexpr uint32_t kSize = 0x20;
    static constexpr int32_t kNone = -1;

    cTextureAtlas() = default; // 0x18bb40: all zeros

    // 0x18bf00 (thiscall, ret 8): a fresh tree over a width x height texture: the free area set and node 0 made. Does not
    // clear what is there (Clear first).
    void Init(int32_t width, int32_t height);
    // 0x18bf30: the tree forgotten (the vectors keep their storage).
    void Clear();
    // 0x18bdf0 (ret 8): a region of width x height, or -1; counts it and takes its area from the free area.
    int32_t Allocate(int32_t width, int32_t height);
    // 0x18bb80 (ret 0xc): the search under `node`: a free leaf of exactly that size (splitting bigger ones, first child
    // first), or -1.
    int32_t AllocateIn(int32_t node, uint32_t width, uint32_t height);
    // 0x18bd40 (ret 0x14): a node for the rectangle (left, top, width, height) under `parent`: a recycled one or a new one
    // at the end of the vector.
    uint32_t AddNode(int32_t left, int32_t top, int32_t width, int32_t height, int32_t parent);
    // 0x18be20 (ret 4): the region `node` handed back; free siblings merge into their parent up the tree.
    void Free(int32_t node);

    uint32_t mRegions = 0;                // +0 regions handed out
    int32_t mFreeArea = 0;                // +4 texels not handed out
    std::vector<cAtlasNode> mNodes;       // +8
    std::vector<uint32_t> mFreeNodes;     // +0x14 recycled node indices
};
static_assert(sizeof(cTextureAtlas) == cTextureAtlas::kSize && offsetof(cTextureAtlas, mNodes) == 8 &&
                  offsetof(cTextureAtlas, mFreeNodes) == 0x14,
              "cTextureAtlas: 0x20 bytes");

} // namespace nGZGraphic4
