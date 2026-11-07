#pragma once
// nGZSceneGraph::cMaterialManager (lane RD, milestone O row M1a; docs/subsystems/scenegraph.md): the scene graph's
// material registry, a function-local static its getter 0xacc040 builds. It keeps one entry per material name (a string
// pool handle): the material definition, its parsed form, a reference count the renderers take by index, and the
// variants made of it (one per vertex format key, built by the slow path 0xacadd0); a queue of variant jobs it works
// through on the tick for at most 25 ms; entries whose last reference went (and that are marked for deferred deletion)
// wait ten ticks before they go. It also owns 34 small device buffers (16 vertex arrays of each of two kinds, two index
// arrays holding the triangle 0, 1, 2), made again whenever the current device comes back (the graphics system's
// CurrentDeviceEvent).
//
// Standalone shape (docs/standalone-shape.md): our class with the game's layout. The getter is hooked and builds our
// object, so every slot, the constructor and the destructor are in effect through it. Slots 22 and 34-36 (lane RF, "M1a
// part 3 result" in scenegraph.md) are ours too; only 0xae8980 remains the image's (0xae8750 and 0xae7340: lane SA, sg/cTextureManager.h). The layout is
// the game's to the byte (the members are the game's own STL types: MSVC 14.40, as ours).
//
// Layout (0x110 bytes, vtable 0xe78514, 39 slots):
//   +0 vtable, +4 reference count and active bit (the behaviour template's)
//   +8    the entries: std::map from the name handle to Entry (0x74-byte nodes)
//   +0x10 the entries by index: std::vector<Entry *> (a released index's slot is null)
//   +0x1c the free indices: std::vector<uint32_t>
//   +0x28 the entries waiting to go: std::set<Entry *>
//   +0x30, +0x50 two hashed sets of objects told when the materials are reset (the slots of part 2 fill them)
//   +0x70 the variant jobs: std::deque<Job>
//   +0x84 16 vertex arrays (kind 1), +0xc4 16 vertex arrays (kind 2), +0x104 / +0x108 the two index arrays
//   +0x10c the device buffers are made, +0x10d started (slot 15 sets it; Shutdown clears it)
#include "game/owned.h"
#include "gz/cAbstractBehaviorTypeST.h"
#include "gz/cIGZUnknown.h"
#include "rz/StaticGetters2.h" // ServiceCell

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace nGZGraphic4 {
class cIStaticVertexArray;
}

namespace nGZSceneGraph {

// 0x98bf70 (__cdecl): lookup2's mix of three words (a, b, c), the result its c. The variant keys' hash (cMaterialManager
// and cApplyColorScalarRadiance's slot 16 use it).
uint32_t __cdecl MixHash3(uint32_t a, uint32_t b, uint32_t c);

// What slot 33 answers for an entry index (all zero when the manager is not started or the index is -1).
struct MaterialInfo {
    uint32_t nameHash;  // the material's slot 19
    void *material;     // not counted
    void *parsed;
    int32_t references;
    uint32_t variants;  // how many variants it has
};
static_assert(sizeof(MaterialInfo) == 0x14, "MaterialInfo: 0x14 bytes");

// What slots 34-36 return through the hidden pointer: {a clone of begin, begin, end}, each counted for the caller (the
// caller releases all three).
struct MaterialRange {
    cIGZUnknown *cursor;
    cIGZUnknown *begin;
    cIGZUnknown *end;
};
static_assert(sizeof(MaterialRange) == 12, "MaterialRange: 12 bytes");

// nGZSceneGraph::cIMaterialManager (interface id 0xdbf985f7; slots 15-37 after the behaviour's; the template's deleting
// destructor is 38). The slots of part 2 are named by slot until their work is known; their arguments are words.
class cIMaterialManager : public nGZEvent::cIAbstractBehavior {
public:
    static constexpr uint32_t kIid = 0xdbf985f7;

    virtual void Start() = 0;                                           // 15
    virtual void Shutdown() = 0;                                        // 16
    virtual void OnTick() = 0;                                          // 17
    virtual void FlushPending() = 0;                                    // 18: every waiting entry goes now
    // 19: the index of the named material's entry; one not there is loaded (0xacc640), or made from the director's
    // material class (the "#" names), and added (BuildEntry with 7).
    virtual uint32_t FindOrLoad(const char *name) = 0;
    virtual void AddRefIndex(int32_t index) = 0;                        // 20
    virtual void ReleaseIndex(int32_t index) = 0;                       // 21
    // 22 (0xacd2d0, __thiscall, ret 0x14): the variant of the entry at `index` for the key (made, by BuildVariant with
    // refine set, when the entry has none). With `queue` (read as a byte) the variants of the keys it implies are
    // queued as jobs for OnTick: the key with its related-format field set to each of the entry's words, and the
    // blob-derived keys of those, plus (when the entry's byte5c is set) the flag variants made from `refA` / `refB`;
    // only keys without a variant are queued. Returns the variant (null when none could be made; the original reads
    // an uninitialised word then).
    virtual cIGZUnknown *GetVariant(uint32_t index, const uint32_t *key, uint32_t queue, uint32_t refA, uint32_t refB) = 0;
    // 23 (0xacc630 is a jump to BuildEntry 0xacbaf0): the material object added under its name (a new entry takes a
    // free index; an old one is reset when the object says it changed), its parsed form read; the entry's index.
    virtual uint32_t AddMaterial(cIGZUnknown *material, uint32_t flags) = 0;
    // 24: the named entry's variants made again (0xacce00); no name: with flags bit 0 the listeners told and dropped,
    // then every entry's.
    virtual void ReloadAll(const char *name, uint32_t flags) = 0;
    // 25: name 0: every entry's variants dropped (and with flags bit 0 the listeners told); else that entry's.
    virtual void Reset(const char *name, uint32_t flags) = 0;
    // 26: an object to tell on reset: kept in set B when it answers the interface query 0xcd2e1781, else in set A (the new
    // one told with slot 4(1)); answers the stored object's query for `iid`.
    virtual uint32_t Register(cIGZUnknown *object, uint32_t iid, void **out) = 0;
    virtual void Unregister(cIGZUnknown *object) = 0;                   // 27: out of A or B, as 26 put it
    virtual void MarkAllStale() = 0;                                    // 28: flag 4 on every entry with flag 1, Reset
    virtual void LoadStale() = 0;                                       // 29: every stale entry loaded again
    virtual uint32_t Slot30(uint32_t a) = 0;                            // 30
    virtual bool IsLoaded(const char *name) = 0;                        // 31: an entry and not stale
    virtual bool IsAvailable(const char *name) = 0;                     // 32: loaded, or its "_txmt" resource is there
    virtual MaterialInfo GetInfo(int32_t index) = 0;                    // 33
    // 34, 35, 36 (__thiscall, a hidden return pointer then the arguments): an iterator range, three counted iterator
    // objects (the game's classes, pool-allocated, their other slots the image's): over the entries by index (34, skipping
    // empty indices), over the variants of the entry at an index (35), over the listeners of set B (36).
    virtual MaterialRange EntryRange() = 0;                             // 34: 0xaca850
    virtual MaterialRange VariantRange(uint32_t index) = 0;             // 35: 0xaccc00
    virtual MaterialRange ListenerRange() = 0;                          // 36: 0xacd7e0
    // 37 (a hidden return pointer, then the name): the info of the first entry in use whose material's slot 20 text is
    // the name (case-insensitively); no name or none: the info of index -1 (slot 33).
    virtual MaterialInfo GetInfoByName(const char *name) = 0;
};

// The behaviour template's cells (cAbstractBehaviorTypeT<cIMaterialManager, cRefSingleThreaded>).
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f6998); // the cell below (game/owned.h)
struct cMaterialManagerBehaviorTraits {
    static constexpr uint32_t kEventManagerCell = 0x10f6998; // 0xacad50's cache
    static nGZEvent::cIConnectionManager *&EventManagerCell() { return ts2_rva_10f6998; }
    static nGZEvent::cEventAttributeMap *Registry();
    // 0xacc550 (cdecl): the registry, built on first use with the graphics system's device event 0xca7ccbbc
    // ("CurrentDeviceEvent") added to the base events; a non-zero id stores `attribute` under it. Our own map (the
    // original's is the game global 0x10f698c, read by nothing else). Not inlined: tests stand in for it.
    __declspec(noinline) static nGZEvent::cEventAttributeMap *__cdecl RegistryFor(uint32_t id,
                                                                                  const nGZEvent::cEventAttribute *attribute);
    static void ResetRegistryForTest(); // tests only: the next use builds the registry again
};

class cMaterialManager : public nGZEvent::cAbstractBehaviorTypeST<cIMaterialManager, cMaterialManagerBehaviorTraits> {
    using Behavior = nGZEvent::cAbstractBehaviorTypeST<cIMaterialManager, cMaterialManagerBehaviorTraits>;

public:
    static constexpr size_t kSize = 0x110;
    static constexpr uint32_t kGraphicSystemId = 0x85522803;
    static constexpr uint32_t kGraphicSystemIid = 0xa55227bb;
    static constexpr uint32_t kDeviceEvent = 0xca7ccbbc; // the graphics system's CurrentDeviceEvent
    static constexpr uint32_t kIidBehavior2 = 0x3b253dc7;
    static constexpr uint32_t kIidBehaviorType = 0xfb859324;
    static constexpr uint32_t kBuffers = 16;            // vertex arrays of each kind
    static constexpr uint32_t kTickBudgetMs = 25;       // OnTick's variant jobs stop past this
    static constexpr int32_t kDeleteDelay = 10;         // ticks a released entry waits

    // 0xacc040 (__cdecl): the manager, built on first use (a function-local static of ours).
    __declspec(noinline) static cMaterialManager *Get();

    cMaterialManager();  // 0xac9810
    ~cMaterialManager(); // 0xac9ee0 (members); 0xaca420 the deleting destructor

    // A counted pointer: released when replaced or dropped.
    template <class T> struct Ref {
        T *p = nullptr;
        Ref() = default;
        Ref(const Ref &other) : p(other.p)
        {
            if (p) p->AddRef();
        }
        ~Ref()
        {
            if (p) p->Release();
        }
        // The new one counted, stored, the old one released (nothing when they are the same).
        Ref &operator=(const Ref &other)
        {
            Assign(other.p);
            return *this;
        }
        void Assign(T *object)
        {
            T *const old = p;
            if (object == old) return;
            if (object) object->AddRef();
            p = object;
            if (old) old->Release();
        }
        // Cleared first, then released.
        void Reset()
        {
            if (T *const old = p) {
                p = nullptr;
                old->Release();
            }
        }
    };

    // A string pool handle held by a cell (closed when it goes); copying opens the handle again (0xaeb6f0).
    struct NameCell : ServiceCell {
        NameCell(); // the cell of no name
        explicit NameCell(const ServiceCell &other); // opens the same handle
        NameCell(const NameCell &other);
        NameCell &operator=(const NameCell &) = delete;
        ~NameCell() { Close(); }
    };
    // 0xae87d0: the cell of a name (the pool's handle; -1 for no name).
    struct NamedCell : ServiceCell {
        explicit NamedCell(const char *name);
        NamedCell(const NamedCell &) = delete;
        ~NamedCell() { Close(); }
    };
    // The entries' order: by handle (transparent, so a NamedCell finds an entry).
    struct NameLess {
        using is_transparent = void;
        bool operator()(const ServiceCell &a, const ServiceCell &b) const { return a.mValue < b.mValue; }
    };

    // A variant's key: three words of a vertex format.
    struct VariantKey {
        uint32_t w[3];
        bool operator==(const VariantKey &o) const { return w[1] == o.w[1] && w[2] == o.w[2] && w[0] == o.w[0]; }
    };
    struct VariantHash {
        size_t operator()(const VariantKey &k) const { return MixHash3(k.w[0], k.w[1], k.w[2]); }
    };
    struct Blob52 {
        uint32_t w[13];
    };
    // An object an entry holds in its set at +0x3c (counted while there; the set's erase 0x16f520 releases each). Only
    // part 2 fills the set, so its order here (by address) is ours: nothing of ours depends on it.
    struct Held {
        cIGZUnknown *p;
        explicit Held(cIGZUnknown *object) : p(object)
        {
            if (p) p->AddRef();
        }
        Held(const Held &) = delete;
        ~Held()
        {
            if (p) p->Release();
        }
        bool operator<(const Held &o) const { return p < o.p; }
    };

    // A material entry (0x60 bytes, the value of a map node at +0x14).
    struct Entry {
        Entry() : index(0xffffffff), flags(5), references(0), countdown(0), byte5c(0) {} // 0xac9650
        Entry(uint32_t i, const NameCell &n) : index(i), name(n), flags(0), references(0), countdown(0), byte5c(0) {}
        uint32_t index;                   // +0 its slot in mByIndex
        NameCell name;                     // +4
        Ref<cIGZUnknown> material;         // +8 the definition (slot 19: its name's hash)
        Ref<cIGZUnknown> parsed;           // +0xc
        uint32_t flags;                    // +0x10 bit 0 loaded, bit 1 deleted when unused, bit 2 stale
        int32_t references;                // +0x14 AddRefIndex / ReleaseIndex
        int32_t countdown;                 // +0x18 ticks left while waiting to go
        std::unordered_map<VariantKey, Ref<cIGZUnknown>, VariantHash> variants; // +0x1c
        std::set<Held> keys;               // +0x3c objects held (released when dropped)
        std::vector<uint32_t> words;       // +0x44
        std::vector<Blob52> blobs;         // +0x50
        uint8_t byte5c;                    // +0x5c
    };

    // A variant job (0x14 bytes): the entry's name and the key to make a variant for, and a format object.
    struct Job {
        NameCell name;      // +0
        VariantKey key;     // +4
        Ref<cIGZUnknown> format; // +0x10
        Job(const NameCell &jobName, const VariantKey &jobKey) : name(jobName), key(jobKey) {}
        Job(const Job &) = delete;
        // The name and key copied, the format counted (as the deque moves jobs on an erase).
        Job &operator=(const Job &other)
        {
            name.Assign(&other.name.mValue);
            key = other.key;
            format = other.format;
            return *this;
        }
        ~Job() = default; // the format released, then the name closed
    };

    // The listener sets: an object's hash and equality are its slots 6 and 7.
    struct ListenerHash {
        size_t operator()(cIGZUnknown *object) const;
    };
    struct ListenerEqual {
        bool operator()(cIGZUnknown *a, cIGZUnknown *b) const;
    };
    using ListenerSet = std::unordered_set<cIGZUnknown *, ListenerHash, ListenerEqual>;

    // cIGZUnknown
    bool QueryInterface(uint32_t riid, void **out) override; // 0xacc510
    // cIAbstractBehavior
    void OnEvent(uint32_t type, uint32_t a, void *data, uint32_t b) override; // 0xacba20

    // cIMaterialManager
    void Start() override;                                                     // 0xacbae0
    void Shutdown() override;                                                  // 0xacd8b0
    void OnTick() override;                                                    // 0xacda40
    void FlushPending() override;                                              // 0xacb630
    uint32_t FindOrLoad(const char *name) override;                            // 0xacb7b0
    void AddRefIndex(int32_t index) override;                                  // 0xacb400
    void ReleaseIndex(int32_t index) override;                                 // 0xaccb20
    cIGZUnknown *GetVariant(uint32_t index, const uint32_t *key, uint32_t queue, uint32_t refA, uint32_t refB) override; // 0xacd2d0
    uint32_t AddMaterial(cIGZUnknown *material, uint32_t flags) override;      // 0xacc630 -> BuildEntry 0xacbaf0
    void ReloadAll(const char *name, uint32_t flags) override;                 // 0xaccfb0
    void Reset(const char *name, uint32_t flags) override;                     // 0xacb260
    uint32_t Register(cIGZUnknown *object, uint32_t iid, void **out) override; // 0xacc840
    void Unregister(cIGZUnknown *object) override;                             // 0xacdbc0
    void MarkAllStale() override;                                              // 0xacac60
    void LoadStale() override;                                                 // 0xacb470
    uint32_t Slot30(uint32_t a) override;                                      // 0xaccce0 (image)
    bool IsLoaded(const char *name) override;                                  // 0xacba60
    bool IsAvailable(const char *name) override;                               // 0xacc0d0
    MaterialInfo GetInfo(int32_t index) override;                              // 0xacc410
    MaterialRange EntryRange() override;                                       // 0xaca850
    MaterialRange VariantRange(uint32_t index) override;                       // 0xaccc00
    MaterialRange ListenerRange() override;                                    // 0xacd7e0
    MaterialInfo GetInfoByName(const char *name) override;                     // 0xacc320

    // 0xacadd0 (thiscall, ret 0xc): a variant of the entry's material for the key, added to the entry's variants. With
    // the buffers made, an existing variant that accepts the key (its slot 10) is shared; else the parser makes one.
    void BuildVariant(Entry *entry, const VariantKey *key, bool refine);
    // 0xacce00 (thiscall, ret 4): the entry's variants dropped and made again for the same keys.
    void ReloadEntry(Entry *entry);
    // 0xacc640: the named material's "_txmt" resource loaded and added (BuildEntry 3); false when it is not there.
    bool LoadMaterial(const char *name);
    // 0xacbaf0 (slot 23's body): the entry for the material, made or refreshed; returns its index. Non-virtual so the
    // hook address is the body, not a vcall thunk.
    uint32_t BuildEntry(cIGZUnknown *material, uint32_t flags);

    // 0xacbe90: the 34 device buffers made on the current device (when it is ready): the old ones released, all
    // created, then zeroed (the index arrays: the triangle 0, 1, 2). mBuffersReady only when every one was made.
    void CreateDeviceObjects();
    // 0xacb780: the device event: code 1 (the device is back) makes the buffers again and reloads the materials (slot 24).
    void OnDeviceEvent(const void *data);
    // 0xacb560: the entry's variants and jobs dropped and it leaves the waiting set; one marked for deletion (flag 2)
    // goes, its index freed.
    void DestroyEntry(Entry *entry);
    // 0xacb170: the entry's variants and keys dropped (when it has variants) and its queued jobs removed.
    void ResetEntry(Entry *entry);

    using EntryMap = std::map<NameCell, Entry, NameLess>;
    using VertexRef = Ref<nGZGraphic4::cIStaticVertexArray>;

    EntryMap mEntries;                    // +8
    std::vector<Entry *> mByIndex;        // +0x10
    std::vector<uint32_t> mFreeIndices;   // +0x1c
    std::set<Entry *> mPending;           // +0x28
    ListenerSet mListenersA;              // +0x30
    ListenerSet mListenersB;              // +0x50
    std::deque<Job> mJobs;                // +0x70
    VertexRef mVerticesA[kBuffers];       // +0x84
    VertexRef mVerticesB[kBuffers];       // +0xc4
    VertexRef mIndicesA;                  // +0x104
    VertexRef mIndicesB;                  // +0x108
    bool mBuffersReady;                   // +0x10c
    bool mStarted;                        // +0x10d
};

static_assert(sizeof(cMaterialManager::Entry) == 0x60, "Entry: 0x60 bytes");
static_assert(offsetof(cMaterialManager::Entry, variants) == 0x1c && offsetof(cMaterialManager::Entry, keys) == 0x3c &&
                  offsetof(cMaterialManager::Entry, words) == 0x44 && offsetof(cMaterialManager::Entry, blobs) == 0x50 &&
                  offsetof(cMaterialManager::Entry, byte5c) == 0x5c,
              "Entry layout");
static_assert(sizeof(cMaterialManager::Job) == 0x14, "Job: 0x14 bytes");
static_assert(sizeof(cMaterialManager) == cMaterialManager::kSize, "cMaterialManager: 0x110 bytes");
static_assert(offsetof(cMaterialManager, mEntries) == 8 && offsetof(cMaterialManager, mByIndex) == 0x10 &&
                  offsetof(cMaterialManager, mFreeIndices) == 0x1c && offsetof(cMaterialManager, mPending) == 0x28 &&
                  offsetof(cMaterialManager, mListenersA) == 0x30 && offsetof(cMaterialManager, mListenersB) == 0x50 &&
                  offsetof(cMaterialManager, mJobs) == 0x70 && offsetof(cMaterialManager, mVerticesA) == 0x84 &&
                  offsetof(cMaterialManager, mVerticesB) == 0xc4 && offsetof(cMaterialManager, mIndicesA) == 0x104 &&
                  offsetof(cMaterialManager, mIndicesB) == 0x108 && offsetof(cMaterialManager, mBuffersReady) == 0x10c &&
                  offsetof(cMaterialManager, mStarted) == 0x10d,
              "cMaterialManager layout");

} // namespace nGZSceneGraph
