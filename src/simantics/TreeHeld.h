#pragma once
#include "game/vtable.h"

#include <cstdint>

// A reference to a game object (cIGZUnknown layout: slot 1 AddRef, slot 2 Release) held by a stack local. The
// interpreter code takes these where the original calls AddRef after fetching an object and Release on every path
// out (including the unwinding of a thrown error code). The pointer is borrowed until Hold/construct; assigning
// releases the old object after adding the new one's reference (the order of the original's holder helpers).
template <typename T> class TreeHeld {
public:
    TreeHeld() = default;
    explicit TreeHeld(T *object) : mObject(object) { AddRefObject(); }
    TreeHeld(const TreeHeld &) = delete;
    TreeHeld &operator=(const TreeHeld &) = delete;
    ~TreeHeld() { ReleaseObject(); }

    T *get() const { return mObject; }
    T *operator->() const { return mObject; }
    explicit operator bool() const { return mObject != nullptr; }

    // Adds a reference to `object` (when non-null) and holds it; the previous object is not released: use only on an
    // empty holder.
    void Hold(T *object)
    {
        mObject = object;
        AddRefObject();
    }
    // Releases now (the holder is empty afterwards).
    void Reset()
    {
        T *old = mObject;
        mObject = nullptr;
        if (old) Release(old);
    }
    // The address for an out-parameter that stores a pointer which already carries its own reference (QueryInterface).
    T **Receive() { return &mObject; }

private:
    using RefFn = uint32_t(__thiscall *)(void *);
    static void Release(T *object) { VirtualCall<RefFn, 2>(object); }
    void AddRefObject()
    {
        if (mObject) VirtualCall<RefFn, 1>(mObject);
    }
    void ReleaseObject()
    {
        if (mObject) Release(mObject);
    }

    T *mObject = nullptr;
};
