#pragma once
// nTSWorld::cStringMap<unsigned short> (lane VQ, campaign world-lot WL-0c): the floor pattern table, a serializable pair of
// maps between pattern names and 16-bit ids. RVAs 1.18.0.187. cWorldDB keeps one at +0xc0 (Init), cPartitionManager one of
// its own (slot 3), and cWorldDB's Load builds one; all three through the constructor 0x813b30, the only code that stores
// the vtable 0xe050a8 besides the destructor.
//
// Layout (0x20 bytes): +0 the vtable, +4 the reference count, +8 the table word (the key the pattern catalog files the
// names under; cWorldDB::Init sets 1, the constructor leaves it as it was), +0xc a byte (the constructor 1, cWorldDB::Init 0:
// whether id 0 can be handed out), +0x10 name -> {id, use count} (node 0x1c), +0x18 id -> name (node 0x18). The names are
// the catalog's own strings (pointers, never copied); they are ordered null first, then by _stricmp.
//
// Record (class id 0xcac4fc40): u32 class id (the object's own slot 5), u32 version 1, GZStr "cStringMap", s32 entry count,
// then per name entry in name order GZStr name, u16 id, u32 use count. Read looks each name up in the pattern catalog (the
// cached singleton's slot 16), making a catalog entry for a name it lacks, and files the catalog's string under the id.
//
// Standalone shape (docs/standalone-shape.md): a real class with our own vtable; the constructor hook builds ours in place
// (placement new), so every table is ours. Slots 0-2 are bodies folded across unrelated classes (QueryInterface
// shared_vf_178b20, AddRef 0x148fb0, Release 0x4b1d00): ours only in our vtable, never hooked.
#include "gz/cIGZSerializable.h"

#include <cstddef>
#include <cstdint>
#include <map>

namespace ifaces {
class cIGZIStream;
class cIGZOStream;
} // namespace ifaces

namespace nTSWorld {

// Pattern names ordered as the game orders them: null first, then _stricmp. (The table's own helpers compare std::strings
// built from the names, so a null name never reaches them; the world database's name set orders by this too.)
struct PatternNameLess {
    bool operator()(const char *a, const char *b) const;
};

struct PatternUse {
    uint16_t id;
    uint32_t count;
};

class cStringMap : public cIGZSerializable {
public:
    static constexpr uint32_t kClassId = 0xcac4fc40;
    static constexpr uint32_t kVTable = 0xe050a8;
    static constexpr uint16_t kNoId = 0xffff;

    cStringMap(); // 0x813b30
    cStringMap(const cStringMap &) = delete;
    cStringMap &operator=(const cStringMap &) = delete;
    virtual ~cStringMap(); // slot 6, 0x8141f0 (deleting)

    bool QueryInterface(uint32_t riid, void **out) override; // shared_vf_178b20: riid 1 only
    uint32_t AddRef() override;                              // 0x148fb0
    uint32_t Release() override;                             // 0x4b1d00: at zero, slot 6 with 1
    bool Write(ifaces::cIGZOStream *stream) override;        // 0x81a740
    bool Read(ifaces::cIGZIStream *stream) override;         // 0x816d90
    uint32_t GetGZCLSID() override;                          // 0x815980

    // Slot 7, 0x819d20: the name's id; kNoId for a null name or one the table lacks.
    virtual uint16_t IdOf(const char *name);
    // Slot 8, 0x816050: the id's name (the id by address), null for none.
    virtual const char *NameOf(const uint16_t *id);
    // Slot 9, 0x816680: the next id to hand out. Starts at 0 (the byte set) or 1, then walks the ids in order while
    // each is at most the candidate, adding one per id: the first gap only when the ids from the start are dense.
    virtual uint16_t NextId();

    uint32_t mRefCount = 0;                                    // +4
    uint32_t mWord8;                                           // +8: the catalog's table (never set by the constructor)
    uint8_t mByteC = 1;                                        // +0xc
    std::map<const char *, PatternUse, PatternNameLess> mUses; // +0x10
    std::map<uint16_t, const char *> mNames;                   // +0x18
};
static_assert(sizeof(cStringMap) == 0x20 && offsetof(cStringMap, mRefCount) == 4 && offsetof(cStringMap, mWord8) == 8);
static_assert(offsetof(cStringMap, mByteC) == 0xc && offsetof(cStringMap, mUses) == 0x10 && offsetof(cStringMap, mNames) == 0x18);

// The hooks' entry points: `this` is a table with the game's vtable or ours; each calls our member without virtual
// dispatch (Construct builds ours in place).
struct cStringMapAdapters {
    using Self = cStringMap;
    static Self *__fastcall Construct(Self *self, void *);
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);
    static bool __fastcall Write(Self *self, void *, ifaces::cIGZOStream *stream);
    static bool __fastcall Read(Self *self, void *, ifaces::cIGZIStream *stream);
    static uint32_t __fastcall GetGZCLSID(Self *self, void *);
    static uint16_t __fastcall IdOf(Self *self, void *, const char *name);
    static const char *__fastcall NameOf(Self *self, void *, const uint16_t *id);
    static uint16_t __fastcall NextId(Self *self, void *);
};

} // namespace nTSWorld
