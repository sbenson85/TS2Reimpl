// nGZGraphic4's texture atlas packer (lane OL): src/gz/cTextureAtlas.h.
#include "gz/cTextureAtlas.h"

namespace nGZGraphic4 {

uint32_t cTextureAtlas::AddNode(int32_t left, int32_t top, int32_t width, int32_t height, int32_t parent)
{
    uint32_t index;
    if (mFreeNodes.empty()) {
        index = static_cast<uint32_t>(mNodes.size());
        mNodes.emplace_back();
    } else {
        index = mFreeNodes.back();
        mFreeNodes.pop_back();
    }
    cAtlasNode &node = mNodes[index];
    node.rect.left = left;
    node.rect.top = top;
    node.rect.right = width + left;
    node.rect.bottom = height + top;
    node.parent = parent;
    return index;
}

int32_t cTextureAtlas::AllocateIn(int32_t node, uint32_t width, uint32_t height)
{
    const cAtlasNode *entry = &mNodes[node];
    if (width > static_cast<uint32_t>(entry->rect.right - entry->rect.left)) return kNone;
    uint32_t nodeHeight = static_cast<uint32_t>(entry->rect.bottom - entry->rect.top);
    while (height <= nodeHeight) {
        entry = &mNodes[node];
        if (entry->first != kNone) {
            // Split already: the first child, then the second.
            int32_t found = AllocateIn(entry->first, width, height);
            if (found == kNone) found = AllocateIn(mNodes[node].second, width, height);
            return found;
        }
        if (entry->used) return kNone;
        const int32_t left = entry->rect.left;
        const int32_t top = entry->rect.top;
        const uint32_t nodeWidth = static_cast<uint32_t>(entry->rect.right - left);
        if (nodeWidth == width && nodeHeight == height) {
            mNodes[node].used = true;
            return node;
        }
        // Cut along the shorter leftover: a strip of the wanted width (whole height) when the leftover height is the
        // smaller one, else a strip of the wanted height (whole width).
        int32_t first, second;
        if (nodeHeight - height < nodeWidth - width) {
            first = static_cast<int32_t>(AddNode(left, top, static_cast<int32_t>(width), entry->rect.bottom - top, node));
            const cAtlasNode &after = mNodes[node];
            second = static_cast<int32_t>(AddNode(after.rect.left + static_cast<int32_t>(width), after.rect.top,
                                                  static_cast<int32_t>(nodeWidth - width),
                                                  after.rect.bottom - after.rect.top, node));
        } else {
            first = static_cast<int32_t>(AddNode(left, top, static_cast<int32_t>(nodeWidth),
                                                 static_cast<int32_t>(height), node));
            const cAtlasNode &after = mNodes[node];
            second = static_cast<int32_t>(AddNode(after.rect.left, after.rect.top + static_cast<int32_t>(height),
                                                  after.rect.right - after.rect.left,
                                                  static_cast<int32_t>(nodeHeight - height), node));
        }
        mNodes[node].first = first;
        mNodes[node].second = second;
        node = mNodes[node].first;
        const cAtlasNode &child = mNodes[node];
        if (static_cast<uint32_t>(child.rect.right - child.rect.left) < width) return kNone;
        nodeHeight = static_cast<uint32_t>(child.rect.bottom - child.rect.top);
    }
    return kNone;
}

int32_t cTextureAtlas::Allocate(int32_t width, int32_t height)
{
    const int32_t node = AllocateIn(0, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    if (node != kNone) {
        ++mRegions;
        mFreeArea -= width * height;
    }
    return node;
}

void cTextureAtlas::Free(int32_t node)
{
    cAtlasNode &freed = mNodes[node];
    freed.used = false;
    --mRegions;
    mFreeArea += (freed.rect.bottom - freed.rect.top) * (freed.rect.right - freed.rect.left);
    int32_t parent = freed.parent;
    while (parent != kNone) {
        const int32_t first = mNodes[parent].first;
        const int32_t second = mNodes[parent].second;
        // Two free leaves under a parent become the parent again.
        if (mNodes[first].first != kNone || mNodes[first].used) return;
        if (mNodes[second].first != kNone || mNodes[second].used) return;
        mNodes[parent].first = kNone;
        mNodes[parent].second = kNone;
        mFreeNodes.push_back(static_cast<uint32_t>(first));
        mFreeNodes.push_back(static_cast<uint32_t>(second));
        parent = mNodes[parent].parent;
    }
}

void cTextureAtlas::Init(int32_t width, int32_t height)
{
    mFreeArea = width * height;
    AddNode(0, 0, width, height, kNone);
}

void cTextureAtlas::Clear()
{
    mRegions = 0;
    mFreeArea = 0;
    mNodes.clear();
    mFreeNodes.clear();
}

} // namespace nGZGraphic4
