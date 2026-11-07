#pragma once
// The tables a GZWinTreeView keeps on its window (the applier 0x86c19e builds them, the script writer 0x8792f2 reads them).
// Each is a property holding a number that is the address of a std::map; the game's code reads them in place, so these are
// the layouts of its MSVC 14.40 maps. Shared by src/gz/cGZLegacyHandlers6.size.cpp and cGZLegacyScriptWriterTree.size.cpp.
#include "game/game.h"
#include "rz/cRZString.h"

#include <cstdint>
#include <map>
#include <string>

namespace legacy_tree {

// The properties (each a number that is the address of a std::map).
constexpr uint32_t kImageNumbersProperty = 0x46c207a5;   // image name -> the face's number for it
constexpr uint32_t kNumberImagesProperty = 0xc6c207c8;   // the number -> the name
constexpr uint32_t kImageResourcesProperty = 0x66c20802; // image name -> group, instance
constexpr uint32_t kNodeFontsProperty = 0x86c21da0;      // node name -> font number
constexpr uint32_t kNodeNumbersProperty = 0xe6c21d83;    // node name -> the face's number for it
constexpr uint32_t kNumberNodesProperty = 0x26c21e35;    // the number -> the name

// A table key as the game's tables hold it: a cRZString (0x20 bytes; the game's vtable, the string, a count of zero), ordered by
// its text.
struct TreeName {
    const void *mVTable;
    std::string mString;
    uint32_t mRefCount;
    explicit TreeName(const std::string &text)
        : mVTable(GameAddress<void>(cRZString::kVTable)), mString(text), mRefCount(0)
    {
    }
    TreeName(const TreeName &other) : TreeName(other.mString) {}
    TreeName &operator=(const TreeName &) = delete;
};
static_assert(sizeof(TreeName) == sizeof(cRZString), "TreeName is a cRZString");

struct TreeNameLess {
    bool operator()(const TreeName &first, const TreeName &second) const { return first.mString < second.mString; }
};

struct ImageResource {
    uint32_t mGroup;
    uint32_t mInstance;
};

using NameNumberMap = std::map<TreeName, uint32_t, TreeNameLess>;        // node 0x34
using NumberNameMap = std::map<int32_t, TreeName>;                       // node 0x34
using NameResourceMap = std::map<TreeName, ImageResource, TreeNameLess>; // node 0x38

} // namespace legacy_tree
