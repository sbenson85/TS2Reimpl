#pragma once

namespace fx {

// A counted interface pointer (the game's cRZAutoRefCount, inlined): AddRef (slot 1) when a pointer is taken in, Release
// (slot 2) when it is dropped. Copy only, so a vector reallocates by copying (AddRef each) and then releasing the old
// elements, as the game's vectors of counted pointers do.
template <class T> class CountedRef {
public:
    CountedRef() = default;
    explicit CountedRef(T *object) : mObject(object)
    {
        if (mObject) mObject->AddRef();
    }
    CountedRef(const CountedRef &other) : CountedRef(other.mObject) {}
    CountedRef &operator=(const CountedRef &other)
    {
        Assign(other.mObject);
        return *this;
    }
    ~CountedRef()
    {
        if (mObject) mObject->Release();
    }
    // Counts the new object, stores it, then releases the old one; nothing for the same pointer.
    void Assign(T *object)
    {
        T *old = mObject;
        if (object == old) return;
        if (object) object->AddRef();
        mObject = object;
        if (old) old->Release();
    }
    // Clears the slot, then releases what it held.
    void Reset()
    {
        T *old = mObject;
        if (old) {
            mObject = nullptr;
            old->Release();
        }
    }
    T *Get() const { return mObject; }
    // The slot itself, for a callee that fills it with a counted pointer (empty it first).
    T **Address() { return &mObject; }
    T *operator->() const { return mObject; }

private:
    T *mObject = nullptr;
};

} // namespace fx
