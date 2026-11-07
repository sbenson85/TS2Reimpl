#include "ts/cTSResourceFactoryForIGZSerializable.h"
#include "game/game.h"
#include "game/vtable.h"
#include <new>

// The generic persist factory (vtable 0xda6e90). Create is the cdecl creator 0x1f6740, COMDirectorData registers it
// for the factory class id 0xaba91d51.

cTSResourceFactoryForIGZSerializable *cTSResourceFactoryForIGZSerializable::Create()
{
    auto *factory = static_cast<cTSResourceFactoryForIGZSerializable *>(::operator new(sizeof(cTSResourceFactoryForIGZSerializable)));
    if (factory) {
        factory->mVTable = GameAddress(kVTable);
        factory->mRefCount = 0;
    }
    return factory;
}

void *cTSResourceFactoryForIGZSerializable::DeletingDestructor(uint32_t flags)
{
    mVTable = GameAddress(kVTable);
    if (flags & 1) ::operator delete(this, sizeof(*this));
    return this;
}

bool cTSResourceFactoryForIGZSerializable::CreateInstanceByType3(uint32_t type, uint32_t riid, void **out)
{
    // Slot 4 (CreateInstanceByType) with two zero arguments: the original's own call, through this's vtable.
    // A bool-returning slot is loaded into a register and called through it (write.md, "A fact's ret decides the call form").
    using CreateInstanceFn = bool(__thiscall *)(void *, uint32_t, uint32_t, void **, uint32_t, uint32_t);
    return VirtualSlot<CreateInstanceFn>(this, 4)(this, type, riid, out, 0u, 0u);
}
