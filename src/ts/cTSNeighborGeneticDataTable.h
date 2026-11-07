#pragma once
#include <cstddef>
#include <cstdint>
#include <map>

// A counted reference to one of the table's data objects (a cIGZUnknown-style object: slot 1 AddRef, slot 2 Release, null
// allowed). It is the value type of the table's map, so the map's node (0x18 bytes: three links, colour and nil bytes,
// the id, this pointer) has the game's layout, and erasing a node releases its object as the game's map code does.
class NeighborDataRef {
public:
    NeighborDataRef() = default;
    explicit NeighborDataRef(void *object) : mObject(object) { Retain(); }
    NeighborDataRef(const NeighborDataRef &other) : mObject(other.mObject) { Retain(); }
    NeighborDataRef &operator=(const NeighborDataRef &other) { Assign(other.mObject); return *this; }
    ~NeighborDataRef() noexcept(false) { Drop(mObject); }

    // Counts the new object, stores it, then releases the old one (in that order: a throwing AddRef leaves the old object in
    // place); nothing happens for the same pointer.
    void Assign(void *object)
    {
        void *old = mObject;
        if (object == old) return;
        Hold(object);
        mObject = object;
        Drop(old);
    }
    void *Get() const { return mObject; }

private:
    void Retain() const { Hold(mObject); }
    static void Hold(void *object);
    static void Drop(void *object);
    void *mObject = nullptr;
};
static_assert(sizeof(NeighborDataRef) == 4);

// The map the table keeps: id -> data object. MSVC 14.40's std::map, as in the game.
using NeighborDataMap = std::map<uint32_t, NeighborDataRef>;
static_assert(sizeof(NeighborDataMap) == 8);

class cTSNeighborGeneticDataTable;

// The cITSDataContainer face at +4 (only its vtable pointer). The game's two container slots run with `this` at
// this subobject.
struct NeighborContainerFace {
    const void *mVTable; // +0x04 of the table

    // Slot 4, 0x4f1b60 (ret 4): copies the table's data objects into a new table (created by class id 0x2c310f46,
    // keyed by the container's group) and saves that table into the container's DB segment. AL = result of the save, 0
    // when the container does not give the id source or the segment. A null container returns 0.
    uint8_t ExportTo(void *container);

    // Slot 5, 0x4f13b0 (ret 4): loads the table the container's group names (instance 1, else instance 0) from the
    // persist system and, for every id the container lists, copies that id's data object into this table under the
    // id the container maps it to. AL = 1 when the table was found and every id mapped, else 0.
    uint8_t ImportFrom(void *container);

    cTSNeighborGeneticDataTable *Owner() { return reinterpret_cast<cTSNeighborGeneticDataTable *>(reinterpret_cast<char *>(this) - 4); }
};
static_assert(sizeof(NeighborContainerFace) == 4);

// The genetic data table of a neighborhood: the data object of every neighbor (sim) id, stored as resources of type
// 0xebfee33f in the group of the table's own key. Primary interface cITSNeighborGeneticDataTable (also cIGZSerializable /
// cIGZUnknown) at +0, cITSDataContainer at +4, cIGZPersistResource at +8. The constructor's object is 0x2c bytes.
// There is no virtual destructor: Release destroys the object itself.
class cTSNeighborGeneticDataTable {
public:
    static constexpr uint32_t kVTable = 0xdcdf6c;
    static constexpr uint32_t kContainerVTable = 0xdcdfc8;
    static constexpr uint32_t kPersistVTable = 0xdcdfe4;
    static constexpr uint32_t kClassId = 0x2c310f46;
    static constexpr uint32_t kDataResourceType = 0xebfee33f; // the type word of the data objects' resource keys

    static cTSNeighborGeneticDataTable *__cdecl Create(); // 0x4f0f30: allocate 0x2c bytes and construct
    cTSNeighborGeneticDataTable *Construct(); // 0x4f0ac0: install the three vtables, zero the fields, create the empty map
    uint8_t QueryInterface(uint32_t id, void **out); // 0x4f10c0: ids give this, the container face or the persist face, counted; AL only
    uint32_t Release(); // 0x4f1660: counts above one are decremented, otherwise the table destroys itself and frees its memory
    uint8_t Write(void *stream); // 0x4f1950: version 1, count, then each id and its data object saved under the table's key
    uint8_t Read(void *stream); // 0x4f1130: version, count, then each id's data object is fetched from the persist system and added
    uint32_t GetGZCLSID(); // 0x4f0fc0: 0x2c310f46
    uint8_t Clear(); // 0x4f1920: release every data object and empty the map; clears the modified flag; returns 1
    uint8_t Save(); // 0x4f1860: save the table into the DB segment that holds its key; clears the modified flag
    uint8_t Set(uint32_t id, void *data); // 0x4f0b80: replace (returns 1) or add (returns 0) the data object of an id; marks modified
    uint8_t Remove(uint32_t id); // 0x4f16c0: remove an id and release its data object; marks modified when it existed
    void SetModified(); // 0x4f10b0
    void *Get(uint32_t id); // 0x4f1000: the id's data object (uncounted) or null
    uint32_t FirstId(); // 0x4f0fe0: the smallest id, 0xffffffff when empty
    uint32_t NextId(uint32_t id); // 0x4f1050: the next id after `id`, 0xffffffff when none
    void *CreateData(); // 0x4f0ea0: a new empty data object from the resource manager (uncounted), null without a manager
    void *LoadOrCreate(uint32_t id); // 0x4f0d00: the id's data resource from the resource manager (or a new one), stored in the table
    uint8_t ReadFromHolder(void *holder); // 0x4f1760: Read through the reader the holder offers (0x051b11bd)
    uint8_t WriteToHolder(void *holder); // 0x4f17e0: Write through the writer the holder offers (0xc51b11cb)

    const void *mVTable; // +0x00
    NeighborContainerFace mContainer; // +0x04
    const void *mPersistVTable; // +0x08
    uint32_t mRefCount; // +0x0c
    uint32_t mKey[4]; // +0x10: the table's resource key: type, group, instance, resource (the persist face's slots 3/4 copy it)
    uint8_t mModified; // +0x20 (the three bytes after it are never written)
    NeighborDataMap mData; // +0x24: ids -> data objects (head pointer, count)

private:
    void Destroy() noexcept;
};
static_assert(sizeof(cTSNeighborGeneticDataTable) == 0x2c);
static_assert(offsetof(cTSNeighborGeneticDataTable, mContainer) == 0x04);
static_assert(offsetof(cTSNeighborGeneticDataTable, mPersistVTable) == 0x08);
static_assert(offsetof(cTSNeighborGeneticDataTable, mRefCount) == 0x0c);
static_assert(offsetof(cTSNeighborGeneticDataTable, mKey) == 0x10);
static_assert(offsetof(cTSNeighborGeneticDataTable, mModified) == 0x20);
static_assert(offsetof(cTSNeighborGeneticDataTable, mData) == 0x24);
