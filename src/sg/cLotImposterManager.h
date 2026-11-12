#pragma once
// nTSSG::cLotImposterManager (lane 261108j, scenegraph row LT2b): the scene graph's lot imposter manager (framework
// class made by the factory 0x367790; cTSSGSystem keeps it), which builds the neighbourhood view's low-detail lot models
// (cLotImposter, sg/cLotImposter.h). This row is its life cycle and its definitions file: construction, destruction,
// the COM slots, Init / Shutdown, the message face and the object-imposter definitions (the parser that reads them).
// The generation path (slot 5 0x367800 and what it calls, slots 6 / 7 / 9 and the definitions command
// cObjectImposterCommand, vtable 0xdbb480) is row LT2b2 (split in docs/subsystems/scenegraph.md, "LT2b2 scout") and
// stays the original's apart from the leaves below (lane 261111q): the object keeps the image's vtables, so those slots
// run the original on our object (same layout).
// RVAs 1.18.0.187. Names of slots and fields are ours (tentative unless the status entry says otherwise).
//
// 0x1c4 bytes. Interfaces: the manager at +0 (vtable 0xdbb3c4, 10 slots: 0 QueryInterface, 1 AddRef (shared 0x15e520),
// 2 Release, 3 Init, 4 Shutdown, 5 generate, 6, 7, 8 the deleting destructor, 9), cIGZMessageTarget2 at +4 (vtable
// 0xdbb3f0: QueryInterface / Release thunks, AddRef shared 0x1f4daf, 3 DoMessage).
//
// Standalone shape (docs/standalone-shape.md): the factory is the only caller of the constructor, and the deleting
// destructor the only caller of the destructor; the factory, the slots of ours and LoadDefinitions (also called by
// slot 5) are hooked at their RVAs. The definitions' Clear 0x3f7a20 and the map erase 0x362470 are reached only from
// ours (in effect through callers).
#include "gz/CommandParser.h" // cCommandT, cIArguments
#include "gz/cTextureAtlas.h"
#include "rz/GameAllocator.h" // GZServiceAllocator
#include "rz/cRZString.h"
#include "sg/cImposterSliceRecord.h"
#include "sg/cLotImposterDesignModes.h"
#include "sg/cShape.h" // ShapeMaterialHandle
#include "ts/CountedRef.h"
#include "ts/PersistKey.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

class cIGZUnknown;

namespace nTSSG {

// A cRZString member built and destroyed by hand, as the original's inlined code does (Construct 0x144f5b's work;
// Destruct: the vtable stored again, then the std::string destroyed).
struct cImposterString {
    union {
        cRZString s;
    };
    cImposterString() { s.Construct(); }
    explicit cImposterString(const char *text) { s.Construct(text); }
    // The library's copy (the map nodes' keys and values): the text copied, the refcount 0.
    cImposterString(const cImposterString &other)
    {
        s.Construct();
        s.mString = other.s.mString;
    }
    // The text assigned unless it is the same string (the refcount kept).
    cImposterString &operator=(const cImposterString &other)
    {
        if (this != &other) s.mString.assign(other.s.mString.data(), other.s.mString.size());
        return *this;
    }
    ~cImposterString() { s.Destruct(); }
};
static_assert(sizeof(cImposterString) == 0x20, "cRZString");

// Two strings (0x40 bytes; destroyed by 0x257860: the second, then the first).
struct cImposterStringPair {
    cImposterString first;
    cImposterString second;
    cImposterStringPair() = default;
    cImposterStringPair(const char *a, const char *b) : first(a), second(b) {}
};
static_assert(sizeof(cImposterStringPair) == 0x40, "two cRZStrings");

// The definitions map's order: std::operator< on the texts (0x144a19).
struct cImposterKeyLess {
    bool operator()(const cImposterString &a, const cImposterString &b) const { return a.s.mString < b.s.mString; }
};
// The entries map's order: the first texts, then the second (each by std::operator<).
struct cImposterPairLess {
    bool operator()(const cImposterStringPair &a, const cImposterStringPair &b) const
    {
        if (a.first.s.mString < b.first.s.mString) return true;
        if (b.first.s.mString < a.first.s.mString) return false;
        return a.second.s.mString < b.second.s.mString;
    }
};
using cImposterEntries = std::map<cImposterStringPair, cImposterStringPair, cImposterPairLess>;

// One object-imposter definition (the definitions map's value: 0x28 bytes): the model's name and its entries (node 0x90,
// erased by 0x362410): a design mode (subset, material) of the model -> the one the imposter uses. Filled by the
// definitions command (lane 261111u).
struct cImposterDefinition {
    cImposterString name;     // +0x00
    cImposterEntries entries; // +0x20

    cImposterDefinition() = default;
    // 0x3f72b0 (thiscall, ret 4, returns this): the name from `model` (empty for null), no entries.
    explicit cImposterDefinition(const char *model);
    // 0x3f73b0: the entries, then the name.
    ~cImposterDefinition() = default;
    cImposterDefinition(const cImposterDefinition &) = default;
    // 0x3f7430 (ret 4, returns this): the name assigned (unless the same string), the entries replaced by a copy of
    // `other`'s (unless the same map).
    cImposterDefinition &operator=(const cImposterDefinition &other);
    // 0x3f7500 (ret 0x10): entries[(a, b)] = (c, d), inserted or overwritten; nothing when any of the four is null.
    void AddEntry(const char *a, const char *b, const char *c, const char *d);
    // 0x3f8300 (ret 8): the entry for `key` copied into `out` (each string assigned unless the same one): true; false,
    // `out` untouched, without one.
    bool Find(const cImposterStringPair *key, cImposterStringPair *out);

    // The hook shapes of the constructor and destructor (this in ecx).
    static cImposterDefinition *__fastcall Construct(cImposterDefinition *self, void *edx, const char *model);
    static void __fastcall Destruct(cImposterDefinition *self, void *edx);
};
static_assert(sizeof(cImposterDefinition) == 0x28, "definition");

// The object-imposter definitions (manager +0x18c): a word the constructor sets, then the map by name (node 0x58).
struct cImposterDefinitions {
    uint32_t mId = 0x0c9a719c;                                                   // +0x00 (not read)
    std::map<cImposterString, cImposterDefinition, cImposterKeyLess> mDefinitions; // +0x04

    void Clear(); // 0x3f7a20 (thiscall): every definition destroyed (the erase 0x362470), the map left empty
    // 0x3f7890 (ret 8): a definition `name` -> `model` (no entries) unless `name` is already defined (then nothing).
    // `name` must not be null (its length is taken unchecked).
    void Define(const char *name, const char *model);
    // 0x3f76e0 (ret 0x18): as the two-argument form with the entry (a, b) -> (c, d) added (AddEntry); when `name` is
    // already defined the entry goes to that definition and `model` is ignored.
    void Define(const char *name, const char *a, const char *b, const char *model, const char *c, const char *d);
    // 0x3f83d0 (ret 8): `out` cleared; the first definition, in key order, whose key (a wildcard pattern) matches
    // `loaded`'s tag (case-insensitive) names `out` (SetName, no level-of-detail strip), and each of `loaded`'s modes with
    // an entry there adds the entry's pair to `out` (AddDesignMode): true. False when none matches.
    __declspec(noinline) bool Map(cDesignModes *loaded, cDesignModes *out); // (stood in for by tests of its callers)
};
static_assert(sizeof(cImposterDefinitions) == 0xc, "definitions");

// A pointer released through its virtual base (vbptr at +4, entry 1; slot 2 there).
struct cVirtualBaseRef {
    void *mObject = nullptr;
    cVirtualBaseRef() = default;
    cVirtualBaseRef(const cVirtualBaseRef &) = delete;
    cVirtualBaseRef &operator=(const cVirtualBaseRef &) = delete;
    ~cVirtualBaseRef();
    void Reset(); // empties the slot, then releases the old object (if any)
};

// cTSSliceImposterCreator (manager +0xb0, 0x88 bytes): constructor 0xb51100 and destructor 0xb51300 are not ours yet.
struct cSliceImposterCreatorMember {
    uint8_t mBytes[0x88];
    cSliceImposterCreatorMember();
    cSliceImposterCreatorMember(const cSliceImposterCreatorMember &) = delete;
    cSliceImposterCreatorMember &operator=(const cSliceImposterCreatorMember &) = delete;
    ~cSliceImposterCreatorMember();
    void Release(); // 0xb518a0 (not ours yet): drops what the last generation made
};
static_assert(sizeof(cSliceImposterCreatorMember) == 0x88, "slice imposter creator");

// A point of the +0x1bc height map (x, y) and the map's order: by x, then by y (plain float compares, so NaN keys are
// equivalent to everything at their level).
struct cHeightKey {
    float x;
    float y;
};
struct cHeightKeyLess {
    bool operator()(const cHeightKey &a, const cHeightKey &b) const
    {
        if (a.x < b.x) return true;
        if (b.x < a.x) return false;
        return a.y < b.y;
    }
};

// The m140 slice record (0x50 bytes) is cImposterSliceRecord (sg/cImposterSliceRecord.h).

// A lot tile as the world database's slot 62 takes it.
struct cImposterTile {
    int32_t x;
    int32_t y;
    int32_t level;
};

class cLotImposterManager {
public:
    static constexpr size_t kSize = 0x1c4;
    static constexpr uint32_t kIid = 0x0bc4a393;
    static constexpr uint32_t kIidMessageTarget2 = 0x452294aa;
    static constexpr uint32_t kMessageGenerate = 0xdc;      // the message the constructor registers for
    static constexpr uint32_t kDefinitionsOption = 0xcc9a696d; // the app's file list for the definitions
    static constexpr uint32_t kParserClass = 0xeaaad987;    // the command parser (framework class, iid 0x95035f4)
    static constexpr uint32_t kParserIid = 0x095035f4;

    cLotImposterManager();  // 0x362a10
    ~cLotImposterManager(); // 0x362de0
    cLotImposterManager(const cLotImposterManager &) = delete;
    cLotImposterManager &operator=(const cLotImposterManager &) = delete;

    // 0x367790 (__cdecl): a new manager (null when the allocation fails).
    static cLotImposterManager *__cdecl Create();

    bool QueryInterface(uint32_t iid, void **out); // 0x369530 slot 0 (thunk 0x36957d at +4)
    uint32_t Release();                            // 0x3697e0 slot 2 (thunk 0x3697f5 at +4)
    bool Init();                                   // 0x369140 slot 3
    bool Shutdown();                               // 0x36c630 slot 4
    cLotImposterManager *DeletingDestructor(uint32_t flags); // 0x3630f0 slot 8
    void LoadDefinitions();                        // 0x369590

    // Generation-path leaves (lane 261111q, row LT2b2).
    __declspec(noinline) void ReleaseGenerationState();    // 0x369800: m03c[4..7] reset, m05c / m068 / m074 emptied, the slice creator's Release
    __declspec(noinline) void ReleaseGenerationServices(); // 0x3698f0: m038, then m03c[0..3] reset (each slot emptied, then released)
    int32_t SliceCount() const;       // 0x368f50: (m0a4 - 2 * m150) / m14c, signed
    float SliceCountF() const;        // 0x368f70: the same as a float
    bool HasSliceBorder() const;      // 0x366fc0: m198[2] set and m150 > 0
    // 0x368fa0 (__cdecl): builds `out` as "LotSnapshot_<W/S/E/N>_<n>" for direction 0..3, else "LotSnapshot_<n>".
    static cRZString *__cdecl SnapshotName(cRZString *out, int32_t n, int32_t direction);

    // Lane 261111r (row LT2b2a). mPlacement is the world database (nTSWorld::cIWorldDB, src/world/cWorldDB.h): the slots
    // named below are its.
    // 0x3677f0 (__cdecl): the class id the nTSSG COM director registers for the manager.
    static uint32_t __cdecl ClassId();
    // 0x369430: every atlas destroyed (the vector keeps its storage).
    __declspec(noinline) void ClearAtlases();
    // 0x369070 (ret 4): the ground height (slot 64) at the tile nearest to the point (x, y) in world units (slot 87 the
    // tile size; each coordinate divided by it, rounded half up through double).
    __declspec(noinline) float GroundHeightAt(const float *xy);
    // 0x369460: whether every tile of the lot rectangle's size is within 0.1 of tile (0, 0)'s ground height (slot 64).
    // ORIGINAL-BUG: the tiles read are (0..width-1, 0..height-1) from the world's corner, not the lot's.
    __declspec(noinline) bool IsFlat();
    // 0x363120 (ret 8): the height map's value at `key` raised to `height` (inserted when absent).
    __declspec(noinline) void KeepHighest(const cHeightKey *key, float height);
    // 0x363250 (ret 8): KeepHighest for (p[0], p[1]) with p[2], or with the ground height there when not `given`.
    __declspec(noinline) void AddHeight(const float *p, bool given);
    // 0x36c7a0: the lot rectangle from the world (slots 81-84, the insets m178, ten tiles off each side the current
    // lot's view (cached slot-23 service, kinds 0xbea28c2 / 0x2c1805b0 / 0xcc181346) asks for), every tile's elevation
    // asked once (slot 62, results unused), then the ground height range, the top level's walls added to the maximum.
    __declspec(noinline) void ComputeLotBounds();
    // 0x368de0 (__stdcall): the current lot's name (the cached slot-23 service's slot 9, its slot 19) into `out` (its
    // slot 17); false without a lot.
    __declspec(noinline) static bool __stdcall CurrentLotName(void *out);
    // 0x36c690 (__cdecl): every pixel's colour (slot 38, three channels) of level 0 scaled by 1 / the scene graph
    // system's (0x20defb) slot 48 object's slot 23 (`which`), written back opaque (slot 19).
    static void __cdecl ScaleImage(void *image, int32_t which);
    // 0x366fe0 (ret 4): when m198[1] is set and m150 > 0, for every slice record on texture `index` (m05c), the border rings 1..m150
    // around the slice's pixel rectangle filled from its edge pixels (slots 39 / 20 of the texture image).
    __declspec(noinline) void CopySliceBorders(int32_t index);
    // 0x3632b0 (ret 4): a place for the slice record's tiles (its columns x m14c by 0x3f6a30's rows x m14c, plus the
    // border on each side) in the first atlas with room, else in a new m0a4-square one (dropped again when even that
    // has no room: the record's texture stays -1); the record gets the atlas index and the inner rectangle in 0..1.
    __declspec(noinline) void PlaceSlice(void *record);
    // 0x368e20 (__stdcall, ret 0x10; ecx unused): the lot `name` names (the app's slot 56 with it, else the cached
    // slot-23 service's slot 9; none: false). Its slot 8 string (slot 7 length, slot 6 text) is given to the persist
    // system's (0x10faf6c) slot 11, whose answer is the resource group (*group); then the persist system's slot 34 opens
    // {0xe519c933, group, 1, 0} as type 0x656b8f0c into *out, and its answer is the result. `unused` is never read.
    __declspec(noinline) static bool __stdcall OpenLotResource(void *name, void *unused, uint32_t *group, void **out);
    // 0x366460 (ret 4): OpenLotResource for `name` (given a string of the current lot's name, slot 23's 6 / 8, which
    // it does not read); when it opens, every key in the resource manager's (0x20deb5) slot 5 list for
    // {0xac506764, group, 0x486d, 0} (type 0x6cd04176) is loaded (the manager's slot 10) and added to the opened
    // resource (slot 26), then that key itself; the list and the resource are released.
    __declspec(noinline) void LoadLotResources(void *name);
    // 0x36c510 (ret 4): the lot `text` names (the app's slot 56 with a string of it, else the cached slot-23 service's
    // slot 9); when there is one, m080 becomes its slot 8 string (m080's slot 16) followed by "!" when m01c, else "_".
    __declspec(noinline) void SetImposterName(const char *text);

    // Lane 261111s (LT2b2a's left rows and row LT2b2).
    // 0x365410 (ret 8): `target`'s SetFromObject with `object` first (true: done, true). Otherwise two fresh design mode
    // sets: the first loaded from `object` (LoadFromObject, answer unused), the definitions (+0x18c, Map) asked to map it
    // into the second; when they do, `target` takes the second (Take) and that is the answer, else false. Both sets are
    // destroyed (the second first).
    __declspec(noinline) bool ApplyDefinition(void *object, cDesignModeNode *target);
    // 0x364940: the generation services made through the director framework (slot 3), each slot emptied and its old
    // object released first (the framework fetched before that): m038 the indexed mesh builder (0x9bffc10d), m03c[0] the
    // geometry node (0x7ba3838c), m03c[2] the shape-ref node (0x65245517), m03c[1] the shape (0xfc6eb1f7), m03c[3] the
    // resource node (0xe519c933). Then wired: m03c[0]'s slot 32 with m038's virtual base, a new bounding volume builder
    // (0x1cfeceb8, held for the call) the same way, m03c[1]'s slot 41 (m03c[0], 0), m03c[2]'s slot 94 (m03c[1]) and
    // m03c[3]'s slot 45 (m03c[2]); the builder released.
    __declspec(noinline) void CreateGenerationServices();
    // 0x369960 slot 6 (ret 4): LoadLotResources for a string of `text`.
    void LoadNamedLot(const char *text);
    // 0x369a30 slot 9: LoadLotResources for the current lot's name, when there is a current lot (CurrentLotName).
    void LoadCurrentLot();

    // The cIGZMessageTarget2 face (+4).
    struct MessageFace {
        const void *mVTable;
        bool DoMessage(void *message); // 0x3668d0 slot 3
        cLotImposterManager *Owner() { return reinterpret_cast<cLotImposterManager *>(reinterpret_cast<uint8_t *>(this) - 4); }
    };

    const void *mVTable;                          // +0x000 0xdbb3c4
    MessageFace mFace;                            // +0x004 0xdbb3f0
    bool mInitialised = false;                    // +0x008
    uint32_t mRefCount = 0;                       // +0x00c
    std::set<uint32_t> mMessageIds;               // +0x010 the messages the face registers for (0xdc)
    fx::CountedRef<cIGZUnknown> mPlacement;       // +0x018 the world database (nTSWorld::cIWorldDB, getter 0x20e0b2) while initialised
    uint8_t m01c = 1;                             // +0x01c
    // The lot's tile rectangle (ComputeLotBounds; inclusive) and its ground height range.
    int32_t mLotMinX = 0;                         // +0x020
    int32_t mLotMaxX = 0;                         // +0x024
    int32_t mLotMinY = 0;                         // +0x028
    int32_t mLotMaxY = 0;                         // +0x02c
    float mMinHeight = 0.0f;                      // +0x030
    float mMaxHeight = 0.0f;                      // +0x034 the highest ground plus the top level's walls
    cVirtualBaseRef m038;                         // +0x038
    fx::CountedRef<cIGZUnknown> m03c[8];          // +0x03c
    std::vector<fx::CountedRef<cIGZUnknown>> m05c; // +0x05c
    std::vector<fx::CountedRef<cIGZUnknown>> m068; // +0x068
    std::vector<nGZSceneGraph::ShapeMaterialHandle> m074; // +0x074 material indices (destroyed: 0xae8870 each)
    cImposterString m080;                         // +0x080
    uint32_t m0a0 = 0x200;                        // +0x0a0
    uint32_t m0a4 = 0x80;                         // +0x0a4
    uint32_t m0a8 = 0x80;                         // +0x0a8
    uint32_t m0ac = 1;                            // +0x0ac
    cSliceImposterCreatorMember mSliceCreator;    // +0x0b0
    uint32_t m138 = 0x10;                         // +0x138
    float m13c = 1.0f;                            // +0x13c
    std::vector<std::array<uint8_t, 0x50>> m140;  // +0x140
    uint32_t m14c = 8;                            // +0x14c
    uint32_t m150 = 1;                            // +0x150
    std::vector<nGZGraphic4::cTextureAtlas> mAtlases; // +0x154 the slice textures' packers (destroyed: 0x14c410 each)
    uint32_t m160[4] = {};                        // +0x160
    uint8_t m170[3] = {1, 0, 1};                  // +0x170
    bool mGenerateOnMessage = true;               // +0x173 DoMessage runs slot 5 on message 0xdc when set
    bool m174 = false;                            // +0x174
    // The lot rectangle's insets (ComputeLotBounds): +0x178 from the low x, +0x17c from the high y, +0x180 from the high
    // x, +0x184 from the low y.
    int32_t m178[4] = {};                         // +0x178
    uint32_t m188 = 4;                            // +0x188
    cImposterDefinitions mDefinitions;            // +0x18c
    uint8_t m198[4] = {1, 1, 1, 1};               // +0x198
    uint32_t m19c = 300;                          // +0x19c
    uint32_t m1a0 = 300;                          // +0x1a0
    std::string mImageExtension{"jpg"};           // +0x1a4
    std::map<cHeightKey, float, cHeightKeyLess> mHeights; // +0x1bc the highest height seen per point (KeepHighest)
};

// 0x36d100 (__cdecl): the class id the nTSSG COM director registers for cLotImposter (sg/cLotImposter.h; done here
// with the manager's row).
uint32_t __cdecl LotImposterClassId();

// cObjectImposterCommand (vtable 0xdbb480, the definitions command; row LT2b2): 0x369500 slot 5, its name.
struct cObjectImposterCommandName {
    const char *GetName(); // "objectImposter"
};

// The definitions command itself (16 bytes, cCommandT: its state at +0xc is the manager's definitions, slot 3 0x369510
// storing it as given).
struct cObjectImposterCommand : nGZCommandParser::cCommandT {
    // 0x366f10 slot 4 (ret 4): the arguments' range of 2 to 6 strings (their slot 10); two go to the definitions'
    // two-argument Define, six to the six-argument one, any other count does nothing. A nGZCommandParser::cError thrown
    // inside is caught and dropped.
    void Execute(nGZCommandParser::cIArguments *args);
};

// 0x362950 (thiscall): cDesignModes' default constructor as the original calls it (memory in ecx): two empty strings and
// an empty map (its head node allocated). Returns `self`.
cDesignModes *__fastcall ConstructDesignModes(cDesignModes *self, void *edx);

// 0x36b2b0 (__cdecl): a new cLotImposterDesc (framework class 0xcd8b6498) asked for its 0x456b8f1d face; with one, the
// desc's slot 4 with 1, the face's slot 9 with `value` and its key (slot 3) appended to `keys`, then the persist system
// told about the key (slot 17) and asked to add the face (slot 16, with 0): its answer's low byte as it is (not made 0 / 1).
// 0 without a desc or face. The face, then the desc, released. `unused` is never read.
uint8_t __cdecl AddImposterDesc(void *unused, void *value, std::vector<PersistKey, GZServiceAllocator<PersistKey>> *keys);

static_assert(offsetof(cLotImposterManager, mMessageIds) == 0x10);
static_assert(offsetof(cLotImposterManager, m038) == 0x38);
static_assert(offsetof(cLotImposterManager, m080) == 0x80);
static_assert(offsetof(cLotImposterManager, mSliceCreator) == 0xb0);
static_assert(offsetof(cLotImposterManager, m140) == 0x140);
static_assert(offsetof(cLotImposterManager, mGenerateOnMessage) == 0x173);
static_assert(offsetof(cLotImposterManager, mDefinitions) == 0x18c);
static_assert(offsetof(cLotImposterManager, mImageExtension) == 0x1a4);
static_assert(offsetof(cLotImposterManager, mHeights) == 0x1bc);
static_assert(offsetof(cLotImposterManager, mAtlases) == 0x154);
static_assert(offsetof(cLotImposterManager, mMaxHeight) == 0x34);
static_assert(sizeof(cLotImposterManager) == cLotImposterManager::kSize);

} // namespace nTSSG
