// cTSObject's three reference vectors (+0x248, +0x254, +0x260): ReloadReferences (0x531320) and the check it runs
// first, ResyncReferenceSource (0x53ea00). Field map in src/ts/cTSObject.h; slot and field names are tentative.
#include "ts/cTSObject.h"
#include "game/vtable.h"
#include "simantics/cTSBehaviorTree.h"

#include <cstdint>
#include <vector>

namespace {

template <typename T> T &At(const void *base, intptr_t offset)
{
    return *reinterpret_cast<T *>(reinterpret_cast<uintptr_t>(base) + offset);
}

// The interface the reference tables are asked for (the +0x240 object's slot 17).
constexpr uint32_t kReferenceTableIid = 0xfba4da51;

// Definition fields (the +0x238 object's slot 3).
constexpr uint32_t kFieldTableId = 0x14;
constexpr uint32_t kFieldSource = 0x2c;

using NoArgRet32 = uint32_t(__thiscall *)(void *);
using NoArgPtr = void *(__thiscall *)(void *);
using NoArgVoid = void(__thiscall *)(void *);
using OneArgRet32 = uint32_t(__thiscall *)(void *, uint32_t);
using OneArgByte = uint8_t(__thiscall *)(void *, uint32_t);
using OneArgVoid = void(__thiscall *)(void *, uint32_t);
using LookupFn = void(__thiscall *)(void *, int32_t id, uint32_t iid, void **out);
using FillFn = void(__thiscall *)(void *, uint32_t id, RefPtrVector *, RefPtrVector *, RefPtrVector *);
using TakeListFn = void(__thiscall *)(void *, uint32_t id, RefPtrVector *list);

// A counted reference held for the length of a call (AddRef slot 1, Release slot 2).
class CountedRef {
public:
    CountedRef() = default;
    // Takes `object` and adds a reference of its own, as the original's holders constructed from a pointer do.
    explicit CountedRef(void *object) : mObject(object)
    {
        if (mObject) VirtualCall<NoArgRet32, 1>(mObject);
    }
    CountedRef(const CountedRef &) = delete;
    CountedRef &operator=(const CountedRef &) = delete;
    ~CountedRef() { Reset(); }

    void *Get() const { return mObject; }
    // For a lookup that stores an already counted pointer: drops the current one first.
    void **Out()
    {
        Reset();
        return &mObject;
    }

private:
    void Reset()
    {
        if (void *const old = mObject) {
            mObject = nullptr;
            VirtualCall<NoArgRet32, 2>(old);
        }
    }

    void *mObject = nullptr;
};

// The definition's field (slot 3), as the full dword the original tests.
uint32_t DefinitionField(void *definition, uint32_t field)
{
    return VirtualCall<OneArgRet32, 3>(definition, field);
}

bool Empty(const RefPtrVector &v)
{
    return v.mFirst == v.mLast;
}

} // namespace

void cTSObject::ResyncReferenceSource()
{
    if (VirtualCall<NoArgRet32, 98>(this) != 2) return;
    if (!Empty(At<RefPtrVector>(this, 0x248)) || !Empty(At<RefPtrVector>(this, 0x254)) || !Empty(At<RefPtrVector>(this, 0x260))) return;
    void *const definition = At<void *>(this, 0x238);
    if (!definition || DefinitionField(definition, kFieldTableId) == 0) return;
    void *const owner = At<void *>(this, 0x240);
    if (!owner) return;

    const CountedRef other(VirtualCall<NoArgPtr, 32>(owner));
    if (!other.Get()) return;
    void *const otherDefinition = VirtualSlot<NoArgPtr>(other.Get(), 4)(other.Get());
    if (!otherDefinition) return;
    if (DefinitionField(definition, kFieldSource) == DefinitionField(otherDefinition, kFieldSource)) return;

    const uint16_t id = static_cast<uint16_t>(DefinitionField(definition, kFieldTableId));
    CountedRef ours;
    VirtualCall<LookupFn, 17>(owner, static_cast<int16_t>(id), kReferenceTableIid, ours.Out());
    if (!ours.Get() || VirtualSlot<OneArgByte>(ours.Get(), 9)(ours.Get(), id) == 0) return;
    CountedRef theirs;
    VirtualSlot<LookupFn>(other.Get(), 17)(other.Get(), static_cast<int16_t>(id), kReferenceTableIid, theirs.Out());
    if (!theirs.Get() || VirtualSlot<OneArgByte>(theirs.Get(), 9)(theirs.Get(), id) == 0) return;

    // Their list (slot 11) replaces ours unless the two have the same length.
    const uint32_t theirLength = static_cast<RefPtrVector *>(VirtualSlot<NoArgPtr>(theirs.Get(), 11)(theirs.Get()))->Size();
    const uint32_t ourLength = static_cast<RefPtrVector *>(VirtualSlot<NoArgPtr>(ours.Get(), 11)(ours.Get()))->Size();
    if (ourLength == theirLength) return;
    // The original reads our table's vtable before asking for their list again.
    void *const *const ourVTable = *static_cast<void *const *const *>(ours.Get());
    RefPtrVector *const list = static_cast<RefPtrVector *>(VirtualSlot<NoArgPtr>(theirs.Get(), 11)(theirs.Get()));
    reinterpret_cast<TakeListFn>(ourVTable[10])(ours.Get(), id, list);
}

void cTSObject::ReloadReferences(bool keepStates)
{
    ResyncReferenceSource();
    RefPtrVector &refsA = At<RefPtrVector>(this, 0x248);
    RefPtrVector &refsB = At<RefPtrVector>(this, 0x254);
    RefPtrVector &refsC = At<RefPtrVector>(this, 0x260);

    // The first vector's entries report a state word (slot 4) that goes back to the new entry at the same position.
    std::vector<uint16_t> states;
    states.reserve(refsA.Size());
    if (keepStates)
        for (uint32_t i = 0; i < refsA.Size(); ++i)
            if (void *const ref = refsA.mFirst[i]) states.push_back(static_cast<uint16_t>(VirtualCall<NoArgRet32, 4>(ref)));
    refsA.Clear();
    refsB.Clear();
    refsC.Clear();

    void *const definition = At<void *>(this, 0x238);
    if (definition && DefinitionField(definition, kFieldTableId) != 0) {
        CountedRef table;
        const uint16_t id = static_cast<uint16_t>(DefinitionField(definition, kFieldTableId));
        void *const owner = At<void *>(this, 0x240);
        VirtualCall<LookupFn, 17>(owner, static_cast<int16_t>(id), kReferenceTableIid, table.Out());
        // ORIGINAL-BUG: neither the owner nor the table is checked for null (a missing one faults).
        VirtualSlot<FillFn>(table.Get(), 8)(table.Get(), id, &refsA, &refsB, &refsC);
        if (keepStates)
            for (uint32_t i = 0; i < refsA.Size() && i < states.size(); ++i)
                if (void *const ref = refsA.mFirst[i]) VirtualCall<OneArgVoid, 8>(ref, states[i]);
    }

    int16_t *const data = &At<int16_t>(this, 0xa6); // the object data words
    data[0x43] = static_cast<int16_t>(VirtualCall<NoArgRet32, 66>(this));
    data[0x37] = static_cast<int16_t>(VirtualCall<NoArgRet32, 67>(this));
    VirtualCall<NoArgVoid, 329>(this);
}
