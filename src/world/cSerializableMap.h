#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>

#include "game/vtable.h"
#include "rz/cRZString.h"

namespace ifaces {
class cIGZIStream;
class cIGZOStream;
} // namespace ifaces

namespace nTSWorld {
// nTSWorld::cSerializableMap<Key, Value, Less, cProxyManager> (lane VP, campaign world-lot WL-0b): a reference-counted map
// that writes and reads itself, 0x18 bytes. cProxyManager builds both copies inline (0x81adc0) with the game's vtables, so
// this struct keeps the game's layout and our members are hooked at their RVAs. Bases: cRZUnknown +0 (vtable, count),
// cIGZSerializable +8, std::map +0xc (head, size); +0x14 a word the record carries (slots 6 / 7 of the primary table, the
// shared 0x20daf0 / 0x149d00, set and get it).
// Record: u32 class id (slot 5 of the face), u32 version 1, u32 the word, s32 count, then per entry key and value in key
// order. Read checks class id and version (false otherwise), stores the word, then inserts each pair whose key is not in
// the map yet: the map is not cleared, and an existing key keeps its value.
// Copies: GUID (u32 keys and values, unsigned order; vtables 0xe051a8 / 0xe051cc) and string (cRZString keys and values,
// case-insensitive order; vtables 0xe05168 / 0xe0518c; lane VQ).
template <class Key, class Less> struct cSerializableMap {
    static constexpr uint32_t kClassId = 0xace46235;
    static constexpr uint32_t kVersion = 1;
    static constexpr uint32_t kSerializableIid = 0xe4fda3d4;

    // The map, with the insert the string copy keeps out of line (0x81a8a0, std::map::insert of a value: the lower bound
    // first, then the node only when the key is new; called only from that copy's Read). The GUID copy's Read has it
    // inline.
    struct Tree : std::map<Key, Key, Less> {
        using Base = std::map<Key, Key, Less>;
        std::pair<typename Base::iterator, bool> Insert(const typename Base::value_type &entry) { return this->insert(entry); }
    };

    const void *mVTable;       // +0x00
    uint32_t mRefCount;        // +0x04
    const void *mSerializable; // +0x08 the cIGZSerializable face's vtable
    Tree mMap;                 // +0x0c
    uint32_t mWord;            // +0x14

    // 0x81bb90 (shared by both copies): a null out fails; cIGZSerializable gives the +8 face (0 for a null this) and
    // AddRefs through slot 1; anything else goes to the cIGZUnknown-only QueryInterface 0x1b945c on this.
    bool QueryInterface(uint32_t riid, void **out);
    // Slot 3 (GUID 0x81ac60): both vtables stored, cRZUnknown's destructor body, the tree freed, then the object itself
    // when bit 0 of the flags is set.
    cSerializableMap *DeletingDestructor(uint32_t flags);

    // The cIGZSerializable face (this = object + 8); its table holds these.
    struct Face {
        cSerializableMap *Owner() { return reinterpret_cast<cSerializableMap *>(reinterpret_cast<char *>(this) - 8); }
        bool QueryInterface(uint32_t riid, void **out); // 0x81bbcf (shared): the primary's, on this - 8
        bool Write(ifaces::cIGZOStream *stream);        // GUID 0x81d0f0
        bool Read(ifaces::cIGZIStream *stream);         // GUID 0x81bc20
        uint32_t GetGZCLSID() const;                    // 0x81adb0 (shared)
    };
};

// cProxyManager::cGZGUIDLessThan: GUIDs as unsigned numbers.
struct cGZGUIDLessThan {
    bool operator()(uint32_t a, uint32_t b) const { return a < b; }
};
using cGuidMap = cSerializableMap<uint32_t, cGZGUIDLessThan>;
inline constexpr uint32_t kGuidMapVTable = 0xe051a8, kGuidMapFaceVTable = 0xe051cc;
static_assert(sizeof(cGuidMap) == 0x18, "delete 0x18 in 0x81ac60");
static_assert(offsetof(cGuidMap, mMap) == 0x0c && offsetof(cGuidMap, mWord) == 0x14, "the game's layout");
template <> cGuidMap *cGuidMap::DeletingDestructor(uint32_t flags);
template <> bool cGuidMap::Face::Write(ifaces::cIGZOStream *stream);
template <> bool cGuidMap::Face::Read(ifaces::cIGZIStream *stream);

// A cRZString held by value: the string copy's keys and values (node +0x10 / +0x30) and its Read's locals. The game's
// cRZString layout (vtable 0xd9b470, std::string, count). A copy takes the other's text straight from its std::string (no
// virtual call) and starts with a count of 0, as the original's inline copy constructor does.
struct cMapString {
    const void *mVTable = cRZString::VTable();
    std::string mString;
    uint32_t mRefCount = 0;

    cMapString() = default;
    cMapString(const cMapString &other) : mString(other.mString) {}
    cMapString &operator=(const cMapString &) = delete;

    const char *ToChar() const { return VirtualCall<const char *(__thiscall *)(const void *), 5>(this); }
};
static_assert(sizeof(cMapString) == sizeof(cRZString), "the game's cRZString layout");

// cProxyManager::cRZStringLessThanNoCase: _stricmp of the two strings' text (each through its own slot 5) below 0.
struct cRZStringLessThanNoCase {
    bool operator()(const cMapString &a, const cMapString &b) const { return _stricmp(a.ToChar(), b.ToChar()) < 0; }
};
using cStringPairMap = cSerializableMap<cMapString, cRZStringLessThanNoCase>;
inline constexpr uint32_t kStringPairMapVTable = 0xe05168, kStringPairMapFaceVTable = 0xe0518c;
static_assert(sizeof(cStringPairMap) == 0x18, "delete 0x18 in 0x81aca0");
static_assert(offsetof(cStringPairMap, mMap) == 0x0c && offsetof(cStringPairMap, mWord) == 0x14, "the game's layout");
static_assert(sizeof(std::_Tree_node<cStringPairMap::Tree::value_type, void *>) == 0x50, "new 0x50 in 0x81a8a0");
template <> cStringPairMap *cStringPairMap::DeletingDestructor(uint32_t flags);
template <> bool cStringPairMap::Face::Write(ifaces::cIGZOStream *stream);
template <> bool cStringPairMap::Face::Read(ifaces::cIGZIStream *stream);
} // namespace nTSWorld
