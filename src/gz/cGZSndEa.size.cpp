#include "gz/cGZSndEa.h"

#include "game/vtable.h"
#include "rz/cRZRefCount.h"

namespace {

// The original computes (this != 0 ? this + offset : 0) with neg/sbb/and.
void *SubobjectOrNull(void *self, uint32_t offset)
{
    return self ? static_cast<char *>(self) + offset : nullptr;
}

} // namespace

bool cGZSndEa::QueryInterface(uint32_t riid, void **ppvObj)
{
    if (riid == 0x00000001 || riid == 0x206077d2) { // cIGZUnknown, cIGZSnd: this, not masked
        *ppvObj = this;
    } else {
        uint32_t offset;
        switch (riid) {
        case 0x5b34f0af: offset = 0x04; break; // cIGZSndUpdateTarget
        case 0x09d002d2: offset = 0x08; break; // cIGZSndDataSourceHolder
        case 0x4b79c1a9: offset = 0x0c; break; // cIGZSndEaEffects
        case 0xec3a1141: offset = 0x10; break; // cIGZSndGetElapsedTime
        case 0x0c60e969: offset = 0x14; break; // cIGZSndStreamed
        case 0xe9d00a9a: offset = 0x18; break; // cIGZSndCompletionNotification
        default: return false;
        }
        *ppvObj = SubobjectOrNull(this, offset);
    }
    VirtualCall<uint32_t(__thiscall *)(void *), 1>(this); // AddRef
    return true;
}

uint32_t cGZSndEa::AddRef()
{
    return static_cast<uint32_t>(RZAtomicAdd(&mRefCount, 1));
}

uint32_t cGZSndEa::Release()
{
    int32_t count = RZAtomicAdd(&mRefCount, -1);
    if (count != 0) return static_cast<uint32_t>(count);
    cGZSndEa *self = this;
    if (self) VirtualCall<void *(__thiscall *)(void *, uint32_t), 37>(self, 1); // deleting destructor
    return 0;
}
