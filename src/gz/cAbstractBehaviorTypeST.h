#pragma once
// nGZEvent::cAbstractBehaviorTypeT<I, nGZEvent::cRefSingleThreaded> (lane NZ): the event behaviour template the scene
// graph's nodes derive from. It is the multi-threaded template (gz/cAbstractBehaviorTypeMT.h, same Traits) with plain
// count arithmetic: AddRef / Release add and subtract without RZAtomicAdd, and OnEvent's active-bit sync adds
// 0x80000000 directly. Every other slot (Notify / Ask through the connection manager, Connect / Disconnect, the event
// registry slots 12-14) has the same body in both, so this derives from it and replaces those three.
//
// The scene graph's instantiations (cICompositionTreeNode's: Release 0xa38d30, OnEvent 0xa37c70, NotifyListeners
// 0xa38de0, AskListeners 0xa377b0, Connect / Disconnect 0xa37660 / 0xa37740, GetEventManager 0xa37680 with cell
// 0x10f5f98, GetEventAttribute 0xa37280) share their registry: 0x263f30, whose count (slot 12, 0x25f1b0) and id list
// (slot 13, 0x259610) bodies are folded across the node family.
//
// The template declares the virtual destructor: it lands at the slot after Iface's last one, where the game's
// deleting destructors are (Iface must not declare one).
#include "gz/cAbstractBehaviorTypeMT.h"

#include <cstdint>

namespace nGZEvent {

template <class Iface, class Traits>
class cAbstractBehaviorTypeST : public cAbstractBehaviorTypeMT<Iface, Traits> {
    using Base = cAbstractBehaviorTypeMT<Iface, Traits>;

public:
    cAbstractBehaviorTypeST() = default;
    virtual ~cAbstractBehaviorTypeST() = default; // the deleting destructor: the slot after Iface's

    // shared 0x3aecf0 (-> 0x14c930): the low 31 bits count.
    uint32_t AddRef() override
    {
        ++this->mRefCount;
        return this->mRefCount & behavior_mt::kCountMask;
    }

    // The last reference disconnects the object from everything (when there is a manager), then deletes it through
    // its virtual (deleting) destructor; otherwise the count goes down.
    uint32_t Release() override
    {
        if ((this->mRefCount & behavior_mt::kCountMask) == 1) {
            if (Base::GetEventManager()) Base::GetEventManager()->DisconnectAll(this);
            delete this;
            return 0;
        }
        --this->mRefCount;
        return this->mRefCount & behavior_mt::kCountMask;
    }

    // 0xa37c70 (the composition instantiation's): the connection manager's cConnectionChangedEvent keeps the active
    // bit in step. Classes extend it (their own slot 3 calls this first).
    void OnEvent(uint32_t type, uint32_t, void *, uint32_t) override
    {
        if (type == cConnectionChangedEvent::kType) SyncActive();
    }

protected:
    // OnEvent's cConnectionChangedEvent (0x5b51e806): the active bit follows whether anyone listens to this object.
    void SyncActive()
    {
        const bool listened = Base::GetEventManager()->HasConnectionsFrom(this);
        if (static_cast<uint8_t>(this->mRefCount >> 31) != static_cast<uint8_t>(listened))
            this->mRefCount += behavior_mt::kActiveBit;
    }
};

} // namespace nGZEvent
