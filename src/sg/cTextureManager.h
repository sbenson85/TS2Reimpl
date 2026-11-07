#pragma once
// nGZSceneGraph::cTextureManager (lanes SA and SE, milestone O row M4a; docs/subsystems/scenegraph.md): the scene graph's
// texture registry, and the counted texture handles every material class holds.
//
// The manager keeps one record per texture name (a string pool handle): the texture object, the handles' count and
// the texture's id (its index in the by-id table; dropped ids are reused). Slot 17 finds a name or loads its "_txtr"
// resource (a missing one gets a 128x128 stand-in that draws through cMissingTextureUpdateStrategy); slot 20 adds a
// texture under a name; slot 27 fills a texture's two sampler state blocks from the filter settings. The constructor
// makes the 1x1 "DefaultWhiteA1Texture" when a device is up, and listens to the graphics system's CurrentDeviceEvent
// (code 1, the device back: slot 21 reloads the stale textures).
//
// Standalone shape (docs/standalone-shape.md): our class with the game's layout. The getter 0xade2e0 is hooked and
// builds our object (a function-local static), so the constructor, the destructor and every slot are in effect through
// it (the game's vtable 0xe7c778 is referenced only by its constructor and destructor, whose one caller is the getter).
// The game classes the manager makes stay the image's: the texture classes (the director's COM), the update strategies
// (constructors 0xaebd30 and 0xadd030), the handle iterators (pool 0x10f8bd4, cTextureHandleIterator 0xe7c7f4).
//
// The handles (cTextureHandle, 4 bytes: a texture id, -1 for none) count their id in the manager: made from an id
// (0xae8260) or a name (0xae8280, the manager's slot 17 finds or loads it), copied (0xae8240), assigned (0xae82e0),
// reset (0xae8710) and destroyed (0xae82b0); Get (0xae8730) is the id's texture (slot 23). Each asks the manager
// afresh through 0xae8750, which caches the scene graph system's slot 6 (GetTextureManager) in the game global
// 0x10f8f94. 0xae7340 (the "#0x<group>!" name of a resource text) is with them: the material classes use it on the
// names they hand to slot 17.
#include "game/owned.h"
#include "gz/cAbstractBehaviorTypeST.h"
#include "gz/cIGZUnknown.h"
#include "rz/StaticGetters2.h" // ServiceCell

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

class cIGZString;

namespace nGZSceneGraph {

// What slot 24 answers for a texture id ({0, 0} when the manager is not started or the id is -1).
struct TextureInfo {
    uint32_t name;       // the name's text (the string pool's slot 8 of the record's name cell)
    int32_t references;
};
static_assert(sizeof(TextureInfo) == 8, "TextureInfo: 8 bytes");

// What slots 22 / 26 set and answer: three filter settings (slot 27 reads the mode and the bias).
struct TextureFilterParams {
    uint32_t a;  // +8 of the manager (3 at start)
    uint32_t mode; // +0xc (3 at start): slot 27's switch
    float bias;  // +0x10 (-0.2 at start): slot 27's second block, +0x10
};
static_assert(sizeof(TextureFilterParams) == 12, "TextureFilterParams: 12 bytes");

// What slot 28 returns through the hidden pointer: {a clone of begin, begin, end}, each counted for the caller.
struct TextureRange {
    cIGZUnknown *cursor;
    cIGZUnknown *begin;
    cIGZUnknown *end;
};
static_assert(sizeof(TextureRange) == 12, "TextureRange: 12 bytes");

// nGZSceneGraph::cITextureManager (interface id 0x4b05e995; slots 15-28 after the behaviour's; the template's deleting
// destructor is 29).
class cITextureManager : public nGZEvent::cIAbstractBehavior {
public:
    static constexpr uint32_t kIid = 0x4b05e995;

    virtual void Start() = 0;                                                  // 15
    virtual void Shutdown() = 0;                                               // 16
    virtual uint32_t FindOrLoad(const char *name) = 0;                         // 17: the named texture's id
    virtual void AddRefId(uint32_t id) = 0;                                    // 18
    virtual void ReleaseId(uint32_t id) = 0;                                   // 19
    virtual uint32_t Add(cIGZUnknown *texture, const char *name, uint32_t flags) = 0; // 20: its id
    virtual void ReloadStale() = 0;                                            // 21
    virtual void SetFilterParams(uint32_t a, uint32_t mode, float bias) = 0;   // 22
    virtual cIGZUnknown *GetTexture(uint32_t id) = 0;                          // 23 (not counted)
    virtual TextureInfo GetInfo(uint32_t id) = 0;                              // 24
    virtual bool Has(const char *name) = 0;                                    // 25
    virtual TextureFilterParams GetFilterParams() = 0;                         // 26
    virtual void SamplerStates(void *stateA, void *stateB, cIGZUnknown *texture) = 0; // 27
    virtual TextureRange Range() = 0;                                          // 28
};

// The behaviour template's cells (cAbstractBehaviorTypeT<cITextureManager, cRefSingleThreaded>).
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f8c50); // the cell below (game/owned.h)
struct cTextureManagerBehaviorTraits {
    static constexpr uint32_t kEventManagerCell = 0x10f8c50; // 0xaddb60's cache
    static nGZEvent::cIConnectionManager *&EventManagerCell() { return ts2_rva_10f8c50; }
    static nGZEvent::cEventAttributeMap *Registry();
    // 0xade400 (cdecl): the registry, built on first use (0xadce80) with the graphics system's device event 0xca7ccbbc
    // ("CurrentDeviceEvent") added to the base events; a non-zero id stores `attribute` under it. Our own map (the
    // original's is the game global 0x10f8c44, read by nothing else). Not inlined: tests stand in for it.
    __declspec(noinline) static nGZEvent::cEventAttributeMap *__cdecl RegistryFor(uint32_t id,
                                                                                  const nGZEvent::cEventAttribute *attribute);
    static void ResetRegistryForTest(); // tests only: the next use builds the registry again
};

// The texture manager (0x38 bytes; the game's static is 0x10f8c08, vtable 0xe7c778, 30 slots).
//   +0 vtable, +4 reference count and active bit (the behaviour template's)
//   +8 the filter settings, +0x14 the records by name (std::map, 0x28-byte nodes), +0x1c the records by id
//   (std::vector, null for a dropped id), +0x28 the dropped ids (std::vector), +0x34 started
// The map and vectors are the original's layout in our STL (both allocate from the process heap).
class cTextureManager
    : public nGZEvent::cAbstractBehaviorTypeST<cITextureManager, cTextureManagerBehaviorTraits> {
    using Behavior = nGZEvent::cAbstractBehaviorTypeST<cITextureManager, cTextureManagerBehaviorTraits>;

public:
    static constexpr size_t kSize = 0x38;
    static constexpr uint32_t kGraphicSystemId = 0x85522803;
    static constexpr uint32_t kGraphicSystemIid = 0xa55227bb;
    static constexpr uint32_t kDeviceEvent = 0xca7ccbbc; // the graphics system's CurrentDeviceEvent
    static constexpr uint32_t kIidBehavior2 = 0x3b253dc7;
    static constexpr uint32_t kIidBehaviorType = 0xfb859324;

    // 0xade2e0 (__cdecl): the manager, built on first use (a function-local static of ours).
    __declspec(noinline) static cTextureManager *Get();

    cTextureManager();  // 0xadd170
    ~cTextureManager(); // 0xadd480 (members); 0xadd6e0 the deleting destructor

    // A string pool handle owned by a cell (closed when it goes; GetCachedObjectB's slot 7). A copy opens the same
    // handle again (0xaeb6f0: the pool's slot 6, even for -1).
    struct Cell : ServiceCell {
        explicit Cell(uint32_t value) { mValue = value; }
        Cell(const Cell &other);
        Cell &operator=(const Cell &) = delete;
        ~Cell() { Close(); }
    };
    // The map's order: by handle (transparent: a plain ServiceCell finds a record).
    struct CellLess {
        using is_transparent = void;
        bool operator()(const ServiceCell &a, const ServiceCell &b) const { return a.mValue < b.mValue; }
    };
    // A texture's record: the map node's value (+0x14 of the 0x28-byte node); mById[id] points at it. Going, it releases
    // its texture, then closes its name (then the node's key closes). A copy opens the name again and counts the texture.
    struct Record {
        Cell name;            // +0: the texture's name (the key holds the same handle)
        cIGZUnknown *texture; // +4: counted
        int32_t references;   // +8: the handles' count (slots 18 / 19)
        uint32_t id;          // +0xc
        uint32_t flags;       // +0x10: 1 dropped at its last release (else the texture's slot 4), 4 reloaded by slot 21
        Record(uint32_t n, cIGZUnknown *t, int32_t r, uint32_t i, uint32_t f)
            : name(n), texture(t), references(r), id(i), flags(f) {}
        Record(const Record &other);
        Record &operator=(const Record &) = delete;
        ~Record()
        {
            if (texture) texture->Release();
        }
    };
    using RecordMap = std::map<Cell, Record, CellLess>;

    // cIGZUnknown
    bool QueryInterface(uint32_t riid, void **out) override; // 0xade3c0
    // cIAbstractBehavior
    void OnEvent(uint32_t type, uint32_t a, void *data, uint32_t b) override; // 0xade1f0

    // cITextureManager
    void Start() override;                                                     // 0xade290
    void Shutdown() override;                                                  // 0xade970
    uint32_t FindOrLoad(const char *name) override;                            // 0xaddd60
    void AddRefId(uint32_t id) override;                                       // 0xaddc00
    void ReleaseId(uint32_t id) override;                                      // 0xade7e0
    uint32_t Add(cIGZUnknown *texture, const char *name, uint32_t flags) override; // 0xade4e0
    void ReloadStale() override;                                               // 0xade2a0
    void SetFilterParams(uint32_t a, uint32_t mode, float bias) override;      // 0xade930
    cIGZUnknown *GetTexture(uint32_t id) override;                             // 0xadeb10
    TextureInfo GetInfo(uint32_t id) override;                                 // 0xadeb20
    bool Has(const char *name) override;                                       // 0xade230
    TextureFilterParams GetFilterParams() override;                            // 0xadeb70
    void SamplerStates(void *stateA, void *stateB, cIGZUnknown *texture) override; // 0xade9e0
    TextureRange Range() override;                                             // 0xadd700

    // 0xaddd40 (ret 4): the device event: code 1 (the device is back) reloads the stale textures (slot 21).
    void OnDeviceEvent(const void *data);

    TextureFilterParams mFilter;    // +8 (slots 22 / 26)
    RecordMap mRecords;             // +0x14: by name
    std::vector<Record *> mById;    // +0x1c: by id (null for a dropped id)
    std::vector<uint32_t> mFreeIds; // +0x28: dropped ids, for reuse
    bool mStarted;                  // +0x34: slots 18, 19 and 24 do nothing until slot 15
};
static_assert(sizeof(cTextureManager::Record) == 0x14, "Record: 0x14 bytes");
static_assert(offsetof(cTextureManager, mFilter) == 8, "mFilter at +8");
static_assert(offsetof(cTextureManager, mRecords) == 0x14, "mRecords at +0x14");
static_assert(offsetof(cTextureManager, mById) == 0x1c, "mById at +0x1c");
static_assert(offsetof(cTextureManager, mFreeIds) == 0x28, "mFreeIds at +0x28");
static_assert(offsetof(cTextureManager, mStarted) == 0x34, "mStarted at +0x34");
static_assert(sizeof(cTextureManager) == cTextureManager::kSize, "cTextureManager: 0x38 bytes");

// 0xae8750 (__cdecl, no arguments): the texture manager, cached in the game global 0x10f8f94: when that is null, the
// scene graph system (service 0xe5074b6b, interface 0x25074b4d) is asked for and its slot 6 (GetTextureManager) is
// cached; the reference to the system is released after the store (and when slot 6 throws). With no framework the
// original reads its null holder (an access violation), as ours does.
cITextureManager *TextureManager();

// A counted texture id (4 bytes; -1 for none). The members are the original's functions (thiscall, return this where
// the original does); the C++ constructors and destructor call them.
struct cTextureHandle {
    static constexpr uint32_t kNone = 0xffffffffu;
    uint32_t mId;

    cTextureHandle() : mId(kNone) {}
    explicit cTextureHandle(uint32_t id) { InitFromId(id); }
    explicit cTextureHandle(const char *name) { InitFromName(name); }
    cTextureHandle(const cTextureHandle &other) { CopyFrom(&other); }
    cTextureHandle &operator=(const cTextureHandle &other) { return *Assign(&other); }
    ~cTextureHandle() { Destroy(); }

    // The handle held in a raw word (the classes that keep their handles as uint32_t).
    static cTextureHandle *At(uint32_t *word) { return reinterpret_cast<cTextureHandle *>(word); }
    static const cTextureHandle *At(const uint32_t *word) { return reinterpret_cast<const cTextureHandle *>(word); }

    // 0xae8240 (ret 4): the copy constructor: the other's id, counted (slot 18).
    cTextureHandle *CopyFrom(const cTextureHandle *other);
    // 0xae8260 (ret 4): the id, counted (slot 18), even -1 (the manager ignores it).
    cTextureHandle *InitFromId(uint32_t id);
    // 0xae8280 (ret 4): the named texture's id (slot 17), counted (slot 18).
    cTextureHandle *InitFromName(const char *name);
    // 0xae82e0 (ret 4): the other's id counted (slot 18), ours released (slot 19), then the other's taken.
    cTextureHandle *Assign(const cTextureHandle *other);
    // 0xae82b0: the destructor (Reset in a noexcept frame).
    void Destroy() noexcept;
    // 0xae8710: when there is an id, it is released (slot 19) and the handle is -1.
    void Reset();
    // 0xae8730: the id's texture (slot 23, not counted); null for -1.
    cIGZUnknown *Get() const;
};
static_assert(sizeof(cTextureHandle) == 4, "cTextureHandle: 4 bytes");

// 0xae7340 (__cdecl): the text of a resource name in `out` (a cIGZString, through its slots). "##..." texts are taken
// whole. Otherwise the group is `group`, or with a `groupName` the string-key service's slot 0 of it (CRC24 | 0x7f000000
// when there is no service); the group 0x1c0532fa counts as none. A text's part after its first '!' is used, unless a
// "##" comes before that '!'. With a group: "#0x<group, 8 hex digits>!" and the text; without: the text (no text:
// emptied). Always true.
bool __cdecl ResolveResourceName(cIGZString *out, const char *text, uint32_t group, const char *groupName);

} // namespace nGZSceneGraph

// 0xae8750's cache (owned.h): the image's global in the hooked game, standalone/owned_statics.cpp's otherwise.
TS2_OWNED_STATIC(void *, 10f8f94);
