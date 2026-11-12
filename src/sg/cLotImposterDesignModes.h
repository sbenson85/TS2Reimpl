#pragma once
// The design modes of a scene graph (lane 261108j, scenegraph row LT2a): the "DesignModes" data list a "_cres" model may
// carry, mapping each of its material subsets to the material it should be drawn with (key: the subset, value: the
// material name). The lot imposter's Load (src/sg/cLotImposter.cpp) reads each object imposter's list, swaps the model for
// the season's variant (the season manager's name for it) and binds the materials on every shape of the tree. The season
// manager (cSeasonManager 0x335ce0) keeps one cDesignModes of its own and calls SetName and RewriteForLod on it; the
// imposter manager's generation path (row LT2b2) uses the rest.
//
// Both structs are the original's layouts (built inline by their users, members called on them by original code); every
// member is the original's function at its RVA, hooked (thiscall members, cdecl free functions). Names are ours.
// RVAs 1.18.0.187.
//
// A level-of-detail tag in a name is "SWX" and the two characters after it ("SWX" plus a two-digit level, five
// characters: FindLod, StripLod).
#include "world/cSerializableMap.h" // cMapString: the game's cRZString layout with C++ construction

#include <cstddef>
#include <cstdint>
#include <map>

class cIGZString;
class cIGZUnknown;
namespace nGZSceneGraph {
class cICompositionTreeNode;
class cIResourceNode;
class cIShapeRefNode;
} // namespace nGZSceneGraph

namespace nTSSG {

using nTSWorld::cMapString;

// The map's order: the case-sensitive std::string order of the two texts (std::operator< on the strings).
struct cDesignModeLess {
    bool operator()(const cMapString &a, const cMapString &b) const { return a.mString < b.mString; }
};
// subset -> material. A std::map of the game's STL (nodes of 0x50: links, colour, key at +0x10, value at +0x30).
using cDesignModeMap = std::map<cMapString, cMapString, cDesignModeLess>;
static_assert(sizeof(cDesignModeMap) == 8, "the game's std::map: head, size");

// 0x48 bytes: a model's name and its design modes.
struct cDesignModes {
    cMapString mTag;       // +0x00 the object's node tag (LoadFromObject; matched against the imposter definitions)
    cMapString mName;      // +0x20 the model's name (its resource node's tag)
    cDesignModeMap mModes; // +0x40 subset -> material

    cDesignModes() = default;
    ~cDesignModes() = default; // 0x335a60 (the map, the name, then mTag); hooked through Destruct
    // 0x335a60 as the original calls it (this in ecx): the destructor, for the season manager's and the imposter
    // manager's inline instances (src/hook/tables/cLotImposter.cpp).
    static void __fastcall Destruct(cDesignModes *self, void *edx);
    cDesignModes(const cDesignModes &) = delete;
    cDesignModes &operator=(const cDesignModes &) = delete;

    // 0x3f5420: both strings emptied, every entry erased.
    void Clear();
    // 0x3f62f0 (ret 4): cleared, then `node`'s tag as the name and every entry of its "DesignModes" data list (the
    // extension 0xaeb5b0 finds by name: item names as keys, their string values as values). False only for a null node.
    bool Load(nGZSceneGraph::cIResourceNode *node);
    // 0x3f5360 (ret 8): modes[key] = value (inserted, or the value replaced), unless either is null.
    void AddDesignMode(const char *key, const char *value);
    // 0x3f6440 (ret 8): the name from `text`, without its level-of-detail tags when `stripLod`.
    void SetName(const char *text, bool stripLod);
    // 0x3f5550: when the name's level-of-detail tag differs from the first key's, every entry is rewritten for the
    // name's level: the tags stripped from key and value, the name's tag put back into the key (before a trailing
    // "lod90", else at the end) and into the value (before its first '_', and again before the next '_' after that,
    // five characters earlier when the value has a "lod90").
    void RewriteForLod();
    // 0x3f5b80 (ret 4; lane 261111u): cleared, then the design modes of a game object's model. False (left cleared) for
    // a null object or one without a model (its slot 120). The tag from the object's node tag (its slot 4), the name
    // from the object's slot 124 with its level-of-detail tag taken off (and kept, lower-cased). For each element of the
    // model (its slot 291 count, slot 293 each, held around its turn) with a material holder (element slot 3): the
    // subset (element slot 6) -> the material (holder slot 13); then, with the level's tag appended to the subset, each
    // slave subset the object's skin extensions list for it (in extension order; its level's tag stripped when the name
    // had one) -> the material's name built for that slave (0x3ac300 / 0x3ac840 / 0x3ac3e0, not ours).
    __declspec(noinline) bool LoadFromObject(cIGZUnknown *object);
};
static_assert(sizeof(cDesignModes) == 0x48, "the original's layout");
static_assert(offsetof(cDesignModes, mName) == 0x20 && offsetof(cDesignModes, mModes) == 0x40, "the original's layout");

// A counted resource node reference (the original's 4-byte holder): released when it goes.
struct cResourceNodeRef {
    nGZSceneGraph::cIResourceNode *mNode = nullptr;
    cResourceNodeRef() = default;
    ~cResourceNodeRef();
    cResourceNodeRef(const cResourceNodeRef &) = delete;
    cResourceNodeRef &operator=(const cResourceNodeRef &) = delete;
};

// 0x4c bytes: a model's resource node and its design modes (Load builds one on the stack per object imposter). The node
// is declared first so it is released after the modes are destroyed, as the original's 0x335a60 then Release.
struct cDesignModeNode {
    cResourceNodeRef mNode; // +0x00
    cDesignModes mModes;    // +0x04

    // 0x3f7a00: the node released (null), then the modes cleared.
    void Reset();
    // 0x3f7e90 (ret 4): reset, then `object`'s resource node face (iid 0xe519c92c) taken and its design modes loaded.
    // False (left reset) when there is no object or it has no such face.
    bool Set(cIGZUnknown *object);
    // 0x3f8800 (ret 4): the name up to its last "_cres" (all of it without one), built in `out` (raw memory); returns
    // `out`.
    cMapString *BaseName(cMapString *out);
    // 0x3f8a10 (ret 4): the season's variant of the model: the season manager (`seasons`, else the scene graph system's,
    // its slot 66) names it from the base name (its slot 7); when that differs (case-insensitively), the variant's
    // resource node (the resource manager 0x20defb, slot 23) replaces the node in its parent and here, the name follows
    // it, and the design modes are rewritten for its level of detail and applied to its tree.
    void ApplySeason(void *seasons);
    // 0x3f8990: with a node and modes: the tree's shared shapes copied (MakeShapesUnique), then every mode bound on every
    // shape of the tree (in key order).
    __declspec(noinline) void ApplyDesignModesToTree(); // (stood in for by tests of its callers)

    // Lane 261111u (row LT2a2b), ApplyDefinition's callees.
    // 0x3f7b10 (ret 8): reset, then the model `name` (its level-of-detail tags stripped) from the scene graph system
    // (0x20defb, slot 23 with `group`, iid 0xe519c92c) into the node; with one, the stripped name as the modes' name.
    // Whether there is a node.
    __declspec(noinline) bool LoadByName(const char *name, const void *group);
    // 0x3f7a50 (ret 0xc): LoadByName with the object's material name for level `lod` ("_cres" form,
    // DesignMode::FormatMaterialName), else with its slot 124 name. Whether there is a node.
    bool LoadForObject(cIGZUnknown *object, int32_t lod, const void *group);
    // 0x3f7c80 (ret 4): LoadByName with `mapped`'s name (group null); false without a node. Then `mapped`'s tag, name and
    // modes copied here; with modes, the node's "DesignModes" data list (GetOrAddDataList, held) emptied (slot 11) and
    // given every mode (slot 6, in key order), and the modes applied to the tree. True.
    __declspec(noinline) bool Take(cDesignModes *mapped);
    // 0x3f7f40 (ret 4): reset; false for a null object or one 0x3a97f0 rejects (level 0x5a). The object's design modes
    // (LoadFromObject, a local set; false when it fails) and its level-0x5a model (LoadForObject, with the group word its
    // slot 120 / 61 / 76 chain gives when there is a model; false without a node). With modes, the node's "DesignModes"
    // list (held) emptied and given, in key order, each mode whose subset the node's tree has (DesignMode::
    // CollectSubsetNames, case-blind), which also goes into the modes here; then the modes applied to the tree. True.
    __declspec(noinline) bool SetFromObject(cIGZUnknown *object);
};
static_assert(sizeof(cDesignModeNode) == 0x4c, "the original's layout");

// 0x3f8750 (__cdecl): for `node` and every node below it, MakeMaterialsUnique on its shape reference face (iid 0xa516cd10)
// when it has one. Null: nothing.
void __cdecl MakeShapesUnique(nGZSceneGraph::cICompositionTreeNode *node);
// 0x3f8850 (__cdecl): for `node` and every node below it, every shape of its shape reference face bound to material
// `value` for subset `key` (the shape's slot 34). Null: nothing.
void __cdecl BindDesignModeInTree(nGZSceneGraph::cICompositionTreeNode *node, const cMapString *key,
                                  const cMapString *value);
// 0x3f5470 (__cdecl): the first level-of-detail tag of `text` (five characters from the first case-insensitive "SWX")
// into `out` (its slot 16); false, `out` untouched, without one.
bool __cdecl FindLod(cIGZString *text, cIGZString *out);
// 0x3ac4f0 (__cdecl): every level-of-detail tag erased from `text`; how many.
int32_t __cdecl StripLod(cIGZString *text);
// 0xae7af0 (__cdecl): every shape of `node` with more than one referrer (slot 31) replaced by its own copy (a clone,
// mode 1): all the shared shapes removed first, then each copy added, in list order.
void __cdecl MakeMaterialsUnique(nGZSceneGraph::cIShapeRefNode *node);

} // namespace nTSSG
