#pragma once
// cTSWantTreeLoader (lane IL): the expat content handler that reads a want tree resource (the tree manager's records, XML)
// into cTSWantNode trees: one tree per <tree>, nodes from <wantNode> nested by <children>, each node's advertisement
// weights (<ads>), flag maps (<bool>) and ranges (<range>), and each node's property set (<cGZPropertySetString>, read by a
// cGZPropertySetSerializerXmlString subclass that hands the end tag back). Game vtable 0xdd43c0 (12 slots), 0x48 bytes.
// Creation sites: LoadTree (0x5550c0, called by cTSWantTreeManager::vf3 for each tree record) through the constructor
// 0x551bd0, and cTSWantTreeManager::vf8 0x554a50 (trees from a text file on disk), which builds one inline with the game's
// vtable.
//
// Shape: a real class on cIGZExpatContentHandler. LoadTree (hooked) builds ours; the loaders vf8 still builds keep the game's
// vtable and reach our code through the slot adapters (StartElement, EndElement, Release; the layout is the original's).
// Strings are the image's, read through their pointer cells at run time.
#include "gz/cGZPropertySetSerializer.h"
#include "gz/cIGZPropertySetSerializer.h"

#include <cstdint>
#include <map>
#include <string>

class cTSWantTreeLoader : public cIGZExpatContentHandler {
public:
    static constexpr uint32_t kIid = 0x4b6b9c9b; // cIGZExpatContentHandler
    static constexpr uint32_t kNoFactor = 7;     // mFactor outside an <ads> key

    cTSWantTreeLoader() = default; // 0x551bd0 (see the fields: the original leaves +0xc..+0x28 unset)
    cTSWantTreeLoader(const cTSWantTreeLoader &) = delete;
    cTSWantTreeLoader &operator=(const cTSWantTreeLoader &) = delete;

    // 0x5550c0 (__cdecl): the record `key` (a resource key) is opened through the resource manager, its stream read into a
    // buffer and parsed by the framework's expat parser with a new loader (owner `manager`, a cTSWantTreeManager) as the
    // handler; the loader is reset and released afterwards.
    static void LoadTree(const void *key, void *manager);
    // 0x556240: the parser, the property loader, the property set and the current node released (each field cleared
    // first), the manager pointer cleared.
    void Reset();

    // slot 0 (shared_vf_492810, folded with the tuning loader's: never hooked): iid 1 or kIid answer this.
    bool QueryInterface(uint32_t iid, void **out) override;
    uint32_t AddRef() override;  // slot 1 (shared 0x18fd70): ++count at +0x2c
    uint32_t Release() override; // slot 2 (0x5560b0): the last release frees the node, the set, the property loader, the parser
    // slot 3 (0x553210, ret 0x10): `count` is the number of attribute strings (name, value pairs). Always true.
    bool StartElement(const char *name, const char **attributes, uint32_t count, uint32_t a4) override;
    bool EndElement(const char *name) override; // slot 4 (0x552960): always true
    // slots 5-11: everything else accepted (shared bodies returning true: 0x1477b0 for 5/8/10, 0x144910 for 6/7, 0x148260
    // for 9, 0x1ac160 for 11).
    bool CharacterData(const char *, uint32_t) override { return true; }
    bool Callback6() override { return true; }
    bool Callback7() override { return true; }
    bool Callback8(uint32_t, uint32_t) override { return true; }
    bool Callback9(uint32_t) override { return true; }
    bool Callback10(uint32_t, uint32_t) override { return true; }
    bool Callback11(uint32_t, uint32_t, uint32_t, uint32_t) override { return true; }

    // Element names neither handler knows, with the tree id current when last seen (the original's function-local static
    // maps 0x10db040 for StartElement and 0x10db04c for EndElement, std::map<cRZString, uint32_t>; nothing reads them), and
    // the once-only flags of a too new (> 8, 0x10db03c) and a too old (< 3, 0x10db03d) tree version. Exposed for the tests.
    static std::map<std::string, uint32_t> &UnknownStartTags();
    static std::map<std::string, uint32_t> &UnknownEndTags();
    static bool sNewVersionSeen;
    static bool sOldVersionSeen;

    void *mManager = nullptr;      // 0x04 the cTSWantTreeManager being filled (its tree map at +0x34)
    void *mParser = nullptr;       // 0x08 the expat parser (counted)
    // 0x0c-0x28: the current <ads>, <bool> and <range> values. The original's constructor leaves them unset (the first
    // <ads> start writes them all); ours zeroes them.
    uint32_t mFactor = 0;          // 0x0c advertisement factor 0..6 (age .. personData) of the <ads> key; kNoFactor none
    int32_t mWeight = 0;           // 0x10
    bool mSuperZero = false;       // 0x14
    int32_t mBoolKey = 0;          // 0x18 key of a <bool> (life stage, aspiration flag, personality trait)
    bool mBoolValue = false;       // 0x1c
    int32_t mRangeMin = 0;         // 0x20
    int32_t mRangeMax = 0;         // 0x24
    int32_t mRangeIndex = 0;       // 0x28
    int32_t mRefCount = 0;         // 0x2c
    uint32_t mTreeId = 0;          // 0x30 the tree's id (property "id" of the tree's own property set)
    int32_t mVersion = 0;          // 0x34
    bool mInEditorValues = false;  // 0x38 inside <editorValues>: everything is skipped
    bool mSeenNode = false;        // 0x39 a <wantNode> started since the last </tree>
    void *mPropertyLoader = nullptr; // 0x3c cTSWantPropertySetLoader (counted) while a property set is read
    void *mProperties = nullptr;   // 0x40 its cGZPropertySetString (counted)
    void *mNode = nullptr;         // 0x44 the current cTSWantNode (counted)

private:
    ~cTSWantTreeLoader() = default; // Release's `delete this` (the original: sized delete of 0x48); not virtual
};

static_assert(sizeof(cTSWantTreeLoader) == 0x48, "cTSWantTreeLoader size");

// cTSWantPropertySetLoader (the original's is in an anonymous namespace of the tree manager's translation unit), 0x64 bytes:
// the string property set's XML reader (lane EF's cGZPropertySetSerializerXmlString) that the tree loader puts in charge of
// a <cGZPropertySetString> element. Game vtables 0xdd4370 / 0xdd438c; built only by the tree loader's StartElement (the
// original inline), so ours are the only ones while that is hooked ("in_effect": "callers").
class cTSWantPropertySetLoader : public cGZPropertySetSerializerXmlString {
public:
    cTSWantPropertySetLoader() = default;
    ~cTSWantPropertySetLoader() override; // slot 5 0x552220: the parser released (field cleared first), then the base
    // Handler face slot 4 (0x5528f0, ret 4): the base's EndElement; at </cGZPropertySetString> the parser's slot 15 (it
    // takes back its previous handler) and the tree loader's EndElement. Returns the base's result.
    bool EndElement(const char *name) override;

    void *mParser = nullptr;                  // +0x5c the tree loader's parser (counted)
    cTSWantTreeLoader *mTreeLoader = nullptr; // +0x60 not counted
};
static_assert(sizeof(cTSWantPropertySetLoader) == 0x64, "cTSWantPropertySetLoader size");

// The hooked functions' entry points. The slot adapters serve loaders the game builds (cTSWantTreeManager::vf8) with the game's
// vtable: our members, called without virtual dispatch on the original layout.
struct cTSWantTreeLoaderAdapters {
    using Self = cTSWantTreeLoader;
    static Self *__fastcall Construct(void *memory, void *);                                     // 0x551bd0
    static void __fastcall Reset(Self *self, void *);                                            // 0x556240
    static uint32_t __fastcall Release(Self *self, void *);                                      // 0x5560b0
    static bool __fastcall StartElement(Self *self, void *, const char *name, const char **attributes, uint32_t count,
                                        uint32_t a4);                                            // 0x553210
    static bool __fastcall EndElement(Self *self, void *, const char *name);                     // 0x552960
};
