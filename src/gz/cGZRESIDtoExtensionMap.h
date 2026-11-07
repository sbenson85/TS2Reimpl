#pragma once
// cGZRESIDtoExtensionMap (standalone shape, slot-hooked): the resource type <-> file extension map of a directory
// segment (game vtable 0xe5ed38, 10 slots, 0x10 bytes; RTTI faces cIGZPersistRESIDtoExtensionMap, cIGZUnknown).
// A std::multimap from the type id to a cRZString extension (written without the dot), in type order, equal types
// in insertion order.
//
// Creation: no constructor function. The only objects are embedded at +0x7c of cGZDBSegmentDirectoryFiles, built
// inline by its constructors 0x99e4c0, 0x99e8f0 and 0x99ebf0 and destroyed inline by its destructor 0x9a1bb0 (each
// stores the game's vtable 0xe5ed38 itself; none is ours yet). So the objects carry the game's vtable and every slot
// body of the class is hooked at its RVA to an adapter (the shared slots 1, 2 and 9 excepted: verified elsewhere).
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <map>

// The map's interface (iid 0xa56c8cfd).
class cIGZPersistRESIDtoExtensionMap : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xa56c8cfd;
    // Adds `extension` (its Data / Strlen) under `type`, after any entries of that type. True.
    virtual bool AddExtension(uint32_t type, const cIGZString &extension) = 0;           // slot 3
    // The type of the first entry (in map order) whose extension equals `extension` ignoring case, with or without
    // a leading '.' on `extension`. Failing that, an extension of 8 characters after an optional '.' is read as the
    // hexadecimal type id itself. False when neither.
    virtual bool GetTypeForExtension(const cIGZString &extension, uint32_t *type) = 0;   // slot 4
    // The first extension stored under `type` into `extension` (FromChar). False when there is none.
    virtual bool GetExtensionForType(const uint32_t &type, cIGZString *extension) = 0;   // slot 5
    virtual void Clear() = 0;                                                            // slot 6
    // Clear, then AddExtension for each of `other`'s entries in order (`other` is read as a cGZRESIDtoExtensionMap).
    virtual void CopyFrom(const cIGZPersistRESIDtoExtensionMap &other) = 0;              // slot 7
    virtual ~cIGZPersistRESIDtoExtensionMap() = default;                                 // slot 8 (deleting)
    virtual uint32_t GetCount() = 0;                                                     // slot 9
};

namespace residext {

// An extension held by value in the map (the game's cRZString vtable, refcount 0), built in its node from the counted
// text and destroyed with Destruct (the node destructor 0x1baba2). Never copied.
struct Extension {
    union {
        cRZString s;
    };
    Extension(const char *data, uint32_t length)
    {
        s.Construct();
        s.mString.assign(data, length);
    }
    Extension(const Extension &) = delete;
    Extension &operator=(const Extension &) = delete;
    ~Extension() { s.Destruct(); }
};
static_assert(sizeof(Extension) == 0x20, "a cRZString by value");

using Map = std::multimap<uint32_t, Extension>;
static_assert(sizeof(Map) == 8, "the game's std::multimap: head node, size");

} // namespace residext

class cGZRESIDtoExtensionMap : public cIGZPersistRESIDtoExtensionMap {
public:
    static constexpr uint32_t kSize = 0x10;
    // The length of a type id written as an extension (eight hexadecimal digits).
    static constexpr uint32_t kHexTypeLength = 8;

    cGZRESIDtoExtensionMap() = default;
    ~cGZRESIDtoExtensionMap() override = default;  // slot 8, the deleting destructor 0x99ecd0
    cGZRESIDtoExtensionMap(const cGZRESIDtoExtensionMap &) = delete;
    cGZRESIDtoExtensionMap &operator=(const cGZRESIDtoExtensionMap &) = delete;

    bool QueryInterface(uint32_t riid, void **ppv) override;                             // 0x9a2690
    uint32_t AddRef() override;                                                          // shared_vf_148fb0
    // When the count is above 1 it drops by one; otherwise the object deletes itself (slot 8) and 0 is returned.
    uint32_t Release() override;                                                         // shared_vf_85adc0
    bool AddExtension(uint32_t type, const cIGZString &extension) override;              // 0x99f0f0
    bool GetTypeForExtension(const cIGZString &extension, uint32_t *type) override;      // 0x9a0dd0
    bool GetExtensionForType(const uint32_t &type, cIGZString *extension) override;      // 0x9a26c0
    void Clear() override;                                                               // 0x9a0d40
    void CopyFrom(const cIGZPersistRESIDtoExtensionMap &other) override;                 // 0x99faa0
    uint32_t GetCount() override;                                                        // shared_vf_145770

    uint32_t mRefCount = 0;   // +0x04
    residext::Map mMap;       // +0x08 head node, +0x0c size
};

static_assert(sizeof(cGZRESIDtoExtensionMap) == cGZRESIDtoExtensionMap::kSize, "cGZRESIDtoExtensionMap size");
static_assert(offsetof(cGZRESIDtoExtensionMap, mRefCount) == 4 && offsetof(cGZRESIDtoExtensionMap, mMap) == 8,
              "cGZRESIDtoExtensionMap layout");

// The slot hooks' entry points (__thiscall: `this` in ecx, the dummy edx); each calls our member, qualified.
struct cGZRESIDtoExtensionMapAdapters {
    static bool __fastcall QueryInterface(cGZRESIDtoExtensionMap *self, void *edx, uint32_t riid, void **ppv);
    static bool __fastcall AddExtension(cGZRESIDtoExtensionMap *self, void *edx, uint32_t type,
                                        const cIGZString *extension);
    static bool __fastcall GetTypeForExtension(cGZRESIDtoExtensionMap *self, void *edx, const cIGZString *extension,
                                               uint32_t *type);
    static bool __fastcall GetExtensionForType(cGZRESIDtoExtensionMap *self, void *edx, const uint32_t *type,
                                               cIGZString *extension);
    static void __fastcall Clear(cGZRESIDtoExtensionMap *self, void *edx);
    static void __fastcall CopyFrom(cGZRESIDtoExtensionMap *self, void *edx, const cGZRESIDtoExtensionMap *other);
    static cGZRESIDtoExtensionMap *__fastcall DeletingDestructor(cGZRESIDtoExtensionMap *self, void *edx,
                                                                 uint32_t flags);
};
