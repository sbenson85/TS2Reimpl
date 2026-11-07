#pragma once
// Reference holders and small shared helpers of the GenericSimCall cases (opcode 0x01): the holders the original keeps in its
// frame, the cached service getters the cases use, the string-table loader and the resource-key vectors.
#include "game/game.h"
#include "rz/RZAutoRef.h"
#include "rz/ServiceGetters.h"
#include "rz/StaticGetters.h"
#include "rz/StaticGetters2.h"
#include "rz/cRZString.h"
#include "simantics/PrimGenericSimCall.h"
#include "simantics/PrimGenericSimCallOriginals.h"
#include "simantics/PrimXCommon.h"
#include "ts/cTSString.h"

#include <cstdint>
#include <vector>

namespace gscref {

// A reference to an object with a virtual base, released through that base when the case ends (0x57c290 builds it null,
// 0x2296b4 / 0x258c70 / 0x57cf20 clear it, 0x210cfc / 0x1fcd10 release it).
struct VBaseRef {
    RZAutoRef mRef{nullptr};
    VBaseRef() = default;
    VBaseRef(const VBaseRef &) = delete;
    VBaseRef &operator=(const VBaseRef &) = delete;
    ~VBaseRef() noexcept { mRef.ReleaseVirtualBase(); }
    void **Reset() // clears the holder (releasing the old object) and returns the address a query writes to
    {
        mRef.ResetVirtualBase();
        return &mRef.mObject;
    }
    void *Get() const { return mRef.mObject; }
};

// A plain reference (0x57c2f0 builds it, taking a reference on a non-null object; 0x57cf70 / 0x1dae60 clear it; 0x57ca80 /
// 0x4365a0 assign; 0x145d50 / 0x1cbd45 release it).
struct PlainRef {
    RZAutoRef mRef{nullptr};
    PlainRef() = default;
    explicit PlainRef(void *object)
    {
        mRef.mObject = object;
        if (object) primX::Call<uint32_t>(object, 1); // AddRef
    }
    PlainRef(const PlainRef &) = delete;
    PlainRef &operator=(const PlainRef &) = delete;
    ~PlainRef() noexcept { mRef.ReleaseUnguarded(); }
    void **Reset() // clears the holder (releasing the old object) and returns the address a query writes to
    {
        mRef.Reset();
        return &mRef.mObject;
    }
    // 0x57ca80 / 0x4365a0: nothing when it already holds the object; else AddRef the new one, store it, release the old.
    void Assign(void *object)
    {
        if (object == mRef.mObject) return;
        void *const old = mRef.mObject;
        if (object) primX::Call<uint32_t>(object, 1);
        mRef.mObject = object;
        if (old) primX::Call<uint32_t>(old, 2);
    }
    void *Get() const { return mRef.mObject; }
};

// The query slot (0) of the virtual base of `object` (vbptr at +4, its entry 1 at vbptr+4 the displacement from object+4).
inline bool QueryViaVirtualBase(void *object, uint32_t iid, void **out)
{
    char *base = static_cast<char *>(object);
    char *vbptr = *reinterpret_cast<char **>(base + 4);
    const int32_t displacement = *reinterpret_cast<int32_t *>(vbptr + 4);
    void *sub = base + 4 + displacement;
    return (primX::Call<uint32_t>(sub, 0, iid, out) & 0xff) != 0;
}

// (low word of lo) | (hi << 16), both words sign-extended first as the original does.
inline uint32_t PackWords(int16_t lo, int16_t hi)
{
    return (static_cast<uint32_t>(static_cast<int32_t>(lo)) & 0xffffu) | (static_cast<uint32_t>(static_cast<int32_t>(hi)) << 16);
}

// The cached service behind the getter 0xb390c0 (the persist system's resource interface: look resources up by key) and
// 0xb39110.
inline void *KeyService()
{
    return CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
}
inline void *ResourceService()
{
    return CachedServiceGetter<0x10faf74, 0x4bcf1bb0, 0x0bcf1b58>();
}

// The resource-type words the cases use with the key service.
constexpr uint32_t kKeyLookupType = 0xac598ea8; // the type word the lookup slots take next to the key
constexpr uint32_t kStringTableKey = 0x2026960b; // the last argument of every string-table loader
constexpr uint32_t kDefaultTextGroup = 0x7fd46cd0; // the group the loaders use when no object gives one
constexpr uint32_t kAttributeIid = 0x5a69d3d5;   // the attribute interface (QueryInterface on the object itself)
constexpr uint32_t kComponentIidA = 0xfa1c39f7;   // the "component" holder interface, asked through object slot 9
constexpr uint32_t kComponentIidB = 0xfa1c39ed;
constexpr uint32_t kVirtualBaseIid = 0x1a34f3f4;  // asked through the holder's virtual base

// A string-table loader object (cTSString: vtable 0xda5128, 0x34 bytes): a cRZString that fills itself from the string tables when
// it is constructed (cTSString::Construct without flags, 0x1d0e90; table, entry, group, key). The destructor is the cRZString one.
class Loader {
public:
    Loader(uint32_t table, uint32_t entry, uint32_t group)
    {
        loader().Construct(static_cast<uint16_t>(table), static_cast<uint16_t>(entry), group, kStringTableKey);
    }
    Loader(const Loader &) = delete;
    Loader &operator=(const Loader &) = delete;
    ~Loader() noexcept { text().Destruct(); }
    cTSString &loader() { return *reinterpret_cast<cTSString *>(mBytes); }
    cRZString &text() { return loader().mString; }

private:
    alignas(4) uint8_t mBytes[0x34] = {};
};

// 0x26be10 (__thiscall, ret 4): copies the text of another cRZString-layout object (nothing when it is the same string).
inline void AssignText(cRZString &to, cRZString &from)
{
    if (&to.mString != &from.mString) to.mString.assign(from.mString.c_str(), from.mString.size());
}

} // namespace gscref

namespace gscref {

// 0x23f55f (__cdecl, one argument): the bit for a category index 0..13 (2, 4, 0x40, 0x80, 8, 0x20, 0x10, 0x200, 0x100, 0x400, 0x1000
// for the indices the jump table maps; every other index, including 0, 7, 12 and anything above 13, gives 1).
inline uint32_t CategoryBit(uint32_t index)
{
    static const uint32_t kBits[14] = {1, 0x80, 8, 0x200, 0x20, 0x10, 0x400, 1, 0x100, 2, 4, 0x40, 1, 0x1000};
    return index > 13 ? 1u : kBits[index];
}

// 0x54d830 (__cdecl, one argument, result in al): the key is one of the 0xac598eac family whose last word is 0xb0858c5f.
inline bool IsFamilyKey(const Key16 *key)
{
    return key->w[0] == 0xac598eacu && key->w[3] == 0xb0858c5fu;
}

// 0x54a380 (__cdecl, no arguments): slot 11 of the cached slot-23 object, 0 when there is none (the null pointer is returned
// as it is).
inline uint32_t CachedSlot23Key()
{
    void *const holder = GetCachedSlot23();
    return holder ? primX::Call<uint32_t>(holder, 11) : 0u;
}

} // namespace gscref
