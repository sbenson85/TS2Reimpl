#pragma once
// nTSWorld::cWallLayer (lane VQ, campaign world-lot WL-0c): a serializable map from a wall key (an int) to two words, the
// first whole, the second as two 16-bit halves. RVAs 1.18.0.187. cPartitionManager holds three (its slot 3 makes them
// through the constructor 0x81ff50, the only code that stores the vtable 0xe0536c besides the destructor body 0x81ffa0)
// and reaches the entries through the members below.
//
// Layout (0x10 bytes): +0 the vtable, +4 the reference count, +8 std::map<int32_t, WallEntry> (node 0x1c: key +0x10,
// first word +0x14, second word +0x18).
//
// Record (class id 0x8a84d7b0): u32 class id (the object's own slot 5), u32 version 1, GZStr "cWallLayer", s32 entry
// count, then per entry in key order s32 key, u32 first word, u16 the second word's high half, u16 its low half.
//
// Standalone shape (docs/standalone-shape.md): a real class with our own vtable; the constructor hook builds ours in place
// (placement new). Slots 1-2 are bodies folded across unrelated classes (AddRef 0x148fb0, Release 0x4b1d00): ours only in
// our vtable, never hooked. Left original: 0x820080 (cPartitionManager's setter; it stores an uninitialised stack word as
// the second word, see docs/subsystems/world-lot.md) and the map's operator[] 0x81ffb0 (library code).
#include "gz/cIGZSerializable.h"

#include <cstddef>
#include <cstdint>
#include <map>

namespace ifaces {
class cIGZIStream;
class cIGZOStream;
} // namespace ifaces

namespace nTSWorld {

struct WallEntry {
    uint32_t first;
    uint32_t second; // the halves: index 0 the high one (+0x1a in the node), index 1 the low one (+0x18)
};

class cWallLayer : public cIGZSerializable {
public:
    static constexpr uint32_t kClassId = 0x8a84d7b0;
    static constexpr uint32_t kVTable = 0xe0536c;

    cWallLayer(); // 0x81ff50
    cWallLayer(const cWallLayer &) = delete;
    cWallLayer &operator=(const cWallLayer &) = delete;
    virtual ~cWallLayer(); // slot 6, 0x820060 (deleting); the body 0x81ffa0

    bool QueryInterface(uint32_t riid, void **out) override; // 0x8200d0: riid 1 or the class id
    uint32_t AddRef() override;                              // 0x148fb0
    uint32_t Release() override;                             // 0x4b1d00: at zero, slot 6 with 1
    bool Write(ifaces::cIGZOStream *stream) override;        // 0x8203f0
    bool Read(ifaces::cIGZIStream *stream) override;         // 0x820100
    uint32_t GetGZCLSID() override;                          // 0x8200c0

    void Clear();                                          // 0x8200b0
    void Erase(int32_t key);                               // 0x820240: the key's entry, if any
    void SetHalf(int32_t key, int32_t which, const uint16_t *value); // 0x820290: an existing entry's half 0 / 1
    bool Has(int32_t key);                                 // 0x820300
    uint16_t Half(int32_t key, int32_t which);             // 0x820340: 0 for no entry or another index
    uint32_t First(int32_t key);                           // 0x8203a0: 0 for no entry

    uint32_t mRefCount = 0;                   // +4
    std::map<int32_t, WallEntry> mEntries;    // +8
};
static_assert(sizeof(cWallLayer) == 0x10 && offsetof(cWallLayer, mRefCount) == 4 && offsetof(cWallLayer, mEntries) == 8);

// The hooks' entry points: `this` is a layer with the game's vtable or ours; each calls our member without virtual
// dispatch (Construct builds ours in place).
struct cWallLayerAdapters {
    using Self = cWallLayer;
    static Self *__fastcall Construct(Self *self, void *);
    static void __fastcall DestructBody(Self *self, void *);
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t riid, void **out);
    static bool __fastcall Write(Self *self, void *, ifaces::cIGZOStream *stream);
    static bool __fastcall Read(Self *self, void *, ifaces::cIGZIStream *stream);
    static uint32_t __fastcall GetGZCLSID(Self *self, void *);
    static void __fastcall Clear(Self *self, void *);
    static void __fastcall Erase(Self *self, void *, int32_t key);
    static void __fastcall SetHalf(Self *self, void *, int32_t key, int32_t which, const uint16_t *value);
    static bool __fastcall Has(Self *self, void *, int32_t key);
    static uint16_t __fastcall Half(Self *self, void *, int32_t key, int32_t which);
    static uint32_t __fastcall First(Self *self, void *, int32_t key);
};

} // namespace nTSWorld
