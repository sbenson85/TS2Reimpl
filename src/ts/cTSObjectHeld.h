#pragma once
// Helpers shared by the cTSObject slot bodies (src/ts/cTSObjectSlots*.cpp): typed access to the object's bytes, the common virtual
// call shapes, and holders for the counted references a slot's frame owns.
#include "game/game.h"
#include "game/vtable.h"
#include "rz/cGZAllocatorService.h"
#include "rz/cRZString.h"
#include "ts/cTSString.h"

#include <cstdint>
#include <string>

namespace tsobject_held {

template <typename T> T &At(const void *base, intptr_t offset)
{
    return *reinterpret_cast<T *>(reinterpret_cast<uintptr_t>(base) + offset);
}

using NoArgRet32 = uint32_t(__thiscall *)(void *);
using NoArgPtr = void *(__thiscall *)(void *);
using OneArgPtr = void *(__thiscall *)(void *, uint32_t);
using NoArgVoid = void(__thiscall *)(void *);
using OneArgRet32 = uint32_t(__thiscall *)(void *, uint32_t);
using OneArgVoid = void(__thiscall *)(void *, uint32_t);
using PairsFn = void(__thiscall *)(void *, int16_t *, uint32_t, uint32_t);

// Drops one counted reference (slot 2).
inline void ReleaseRef(void *object)
{
    VirtualCall<NoArgRet32, 2>(object);
}

// A reference the frame holds (the original's local holder): released when the frame ends, normally or by an exception.
class HeldRef {
public:
    HeldRef() = default;
    HeldRef(const HeldRef &) = delete;
    HeldRef &operator=(const HeldRef &) = delete;
    ~HeldRef()
    {
        if (mObject) ReleaseRef(mObject);
    }
    void *Get() const { return mObject; }
    // Takes over a reference the caller already holds (no AddRef, and the previous object is not released).
    void Adopt(void *object) { mObject = object; }
    // An out pointer for a query; the previous interface is dropped first (null before the release).
    void **Renew()
    {
        if (void *const old = mObject) {
            mObject = nullptr;
            ReleaseRef(old);
        }
        return &mObject;
    }
    // An out pointer for a call that overwrites the holder without dropping what it held (the original's out local is reused).
    void **Out() { return &mObject; }

private:
    void *mObject = nullptr;
};

// A reference to an object with a virtual base (the stream-like objects slot 9 and its service hand out): what is released is the
// virtual base subobject, found through the vbtable the object keeps at +4 (base at object + 4 + vbtable[1]), by its slot 2.
class HeldVirtualBase {
public:
    HeldVirtualBase() = default;
    HeldVirtualBase(const HeldVirtualBase &) = delete;
    HeldVirtualBase &operator=(const HeldVirtualBase &) = delete;
    ~HeldVirtualBase() { Release(); }
    void *Get() const { return mObject; }
    void **Out() { return &mObject; }
    // Releases the base and forgets the object (the original's local is out of its cleanup range from here on).
    void Release()
    {
        if (void *const object = mObject) {
            mObject = nullptr;
            ReleaseBase(object);
        }
    }

    static void ReleaseBase(void *object)
    {
        const int32_t *const vbtable = At<int32_t *>(object, 4);
        ReleaseRef(reinterpret_cast<uint8_t *>(object) + 4 + vbtable[1]);
    }

private:
    void *mObject = nullptr;
};

// A game string built from text for the length of a call (constructed and destroyed in place, as the original's local is).
class HeldString {
public:
    explicit HeldString(const char *text) { Get()->Construct(text); }
    HeldString(const HeldString &) = delete;
    HeldString &operator=(const HeldString &) = delete;
    ~HeldString() { Get()->Destruct(); }
    cRZString *Get() { return reinterpret_cast<cRZString *>(mStorage); }

private:
    alignas(cRZString) unsigned char mStorage[sizeof(cRZString)];
};

// An empty game string (the default constructor's state) for the length of a call; destroyed in place at the end.
class HeldEmptyString {
public:
    HeldEmptyString() { Get()->Construct(); }
    HeldEmptyString(const HeldEmptyString &) = delete;
    HeldEmptyString &operator=(const HeldEmptyString &) = delete;
    ~HeldEmptyString() { Get()->Destruct(); }
    cRZString *Get() { return reinterpret_cast<cRZString *>(mStorage); }
    std::string &Text() { return Get()->mString; }

private:
    alignas(cRZString) unsigned char mStorage[sizeof(cRZString)];
};

// A string loaded from a string table for the length of a call (the original's local: built by the loading constructor, destroyed
// as a plain cRZString).
class HeldTSString {
public:
    HeldTSString(uint16_t table, uint16_t entry, uint32_t group)
    {
        Get()->Construct(table, entry, group, 1, cTSString::kStringTableType);
    }
    HeldTSString(const HeldTSString &) = delete;
    HeldTSString &operator=(const HeldTSString &) = delete;
    ~HeldTSString() { Get()->mString.Destruct(); }
    cTSString *Get() { return reinterpret_cast<cTSString *>(mStorage); }

private:
    alignas(cTSString) unsigned char mStorage[sizeof(cTSString)];
};

// A vector of counted references the original's frame owns (begin, end, capacity): at the end every element is released and
// the storage goes back through the allocator service (0x197100's cache), whether the frame ends normally or by an exception.
struct HeldRefList {
    void **mFirst = nullptr;
    void **mLast = nullptr;
    void **mEnd = nullptr;

    HeldRefList() = default;
    HeldRefList(const HeldRefList &) = delete;
    HeldRefList &operator=(const HeldRefList &) = delete;
    ~HeldRefList()
    {
        if (!mFirst) return;
        for (void **p = mFirst; p != mLast; ++p)
            if (*p) ReleaseRef(*p);
        FreeThroughAllocatorService(mFirst, 0);
        mFirst = mLast = mEnd = nullptr;
    }
};

} // namespace tsobject_held
