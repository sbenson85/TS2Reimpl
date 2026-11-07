#pragma once
#include "rz/ServiceGetters.h"
#include "rz/cRZString.h"

#include <cstdint>

// cTSString: a cRZString that fills itself from the string tables when it is constructed; the object the string-token
// expansion and the text primitives load their texts through. RVAs are 1.18.0.187; vtable 0xda5128 (34 slots: cRZString's,
// except slot 16, which forwards to cRZString's assignment, and slot 33, the deleting destructor).
//
// The constructors fill in the 16-byte resource key at +0x20 (type, group, instance = table << 16 | entry, a fourth word that is
// always 0) and ask the resource manager for that resource as a cIGZString (Load). The text is the resource's text with every
// bullet (U+2022) replaced by a zero width space (U+200B). When the resource is missing the text stays empty.
struct cTSString {
    static constexpr uint32_t kVTable = 0xda5128;
    static constexpr uint32_t kStringTableType = 0x2026960b; // the resource type of every string table the game loads texts from

    cRZString mString;      // +0x00 vtable, the text, reference count
    uint32_t mResourceType; // +0x20 the resource key's type (kStringTableType)
    uint32_t mGroup;        // +0x24 the key's group: the string table's group
    uint16_t mEntry;        // +0x28 the key's instance, low word: the entry within the table
    uint16_t mTable;        // +0x2a the key's instance, high word: the string table id
    uint32_t mKeyWord3;     // +0x2c the key's last word (always 0)
    uint8_t mLoaded;        // +0x30 1 once the resource was found
    int8_t mLoadFlags;      // +0x31 the flags the resource manager is asked with (sign-extended)

    // 0x1d0fb0 (__thiscall, no arguments): an empty string with all the key fields cleared. Returns this.
    cTSString *ConstructEmpty();
    // 0x1d0df0 (__thiscall, ret 0x14): builds the loader for the key (resourceType, group, table << 16 | entry, 0) and loads it
    // with the given flags. A load that throws destroys the string again. Returns this.
    cTSString *Construct(uint16_t table, uint16_t entry, uint32_t group, uint8_t loadFlags, uint32_t resourceType);
    // 0x1d0e90 (__thiscall, ret 0x10): the same with flags 0.
    cTSString *Construct(uint16_t table, uint16_t entry, uint32_t group, uint32_t resourceType);
    // 0x1d10d0 (__thiscall, no arguments, result in al): asks the resource manager for the resource of the key as a cIGZString.
    // When found, mLoaded becomes 1 and the text is copied from it (bullets replaced); when not (or when there is no resource
    // manager) the text is cleared. The resource is released either way. Returns whether it was found.
    bool Load();
    // 0x1d1000 (__thiscall, ret 4): copies the text and the key fields of another loader (the reference count stays).
    // Does nothing when `other` is this. Returns this.
    cTSString *Assign(const cTSString *other);
    // 0x1d1050 (slot 16, __thiscall, ret 4): cIGZString's operator=, forwarded to cRZString's.
    cRZString *AssignString(const cIGZString &other);
    // 0x1d1060 (slot 33, __thiscall, ret 4): destroys the string (the vtable goes back to cRZString's); with flags & 1 also frees
    // the 0x34 bytes. Returns this.
    cTSString *DeletingDestructor(uint32_t flags);

    // The destructor is cRZString's, as in the original.
    void Destruct() { mString.Destruct(); }
    // The key the resource manager is asked with.
    const void *ResourceKey() const { return &mResourceType; }
};

static_assert(offsetof(cTSString, mResourceType) == 0x20, "cTSString layout");
static_assert(offsetof(cTSString, mEntry) == 0x28, "cTSString layout");
static_assert(offsetof(cTSString, mLoaded) == 0x30, "cTSString layout");
static_assert(offsetof(cTSString, mLoadFlags) == 0x31, "cTSString layout");
static_assert(sizeof(cTSString) == 0x34, "cTSString layout");

namespace tsstring {

// The cached resource manager (0x9c5b26, __cdecl, no arguments): the persist system's cIGZPersistResourceManager interface, found
// through the service object like the getters of rz/ServiceGetters.h (cache global 0x10f51a8).
constexpr uint32_t kResourceManagerCacheRva = 0x10f51a8;
constexpr uint32_t kResourceManagerFirstId = 0x056b906e;
constexpr uint32_t kResourceManagerSecondId = 0x656b8efc;

inline void *GetResourceManager()
{
    return CachedServiceGetter<kResourceManagerCacheRva, kResourceManagerFirstId, kResourceManagerSecondId>();
}

} // namespace tsstring
