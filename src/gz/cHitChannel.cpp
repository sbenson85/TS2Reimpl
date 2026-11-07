#include "gz/cHitChannel.h"

#include "game/vtable.h"

namespace {

// The original computes (this != 0 ? this + offset : 0) with neg/sbb/and.
void *SubobjectOrNull(void *self, uint32_t offset)
{
    return self ? static_cast<char *>(self) + offset : nullptr;
}

} // namespace

bool cHitChannel::QueryInterface(uint32_t riid, void **ppvObj)
{
    if (riid == 0x206077d2) { // cIGZSnd: the primary subobject, not masked
        *ppvObj = this;
    } else {
        uint32_t offset;
        switch (riid) {
        case 0x306077d2: offset = 0x04; break; // cIGZSnd3D
        case 0x5b34f0af: offset = 0x08; break; // cIGZSndUpdateTarget
        case 0xfd584467: offset = 0x0c; break; // cIGZSndRegisterSet
        case 0xaa11a64f: offset = 0x10; break; // cIGZSndViewUpdateTarget
        case 0x09d002d2: offset = 0x14; break; // cIGZSndDataSourceHolder
        case 0xec3a1141: offset = 0x18; break; // cIGZSndGetElapsedTime
        case 0x0c60e969: offset = 0x1c; break; // cIGZSndStreamed
        default: return false;
        }
        *ppvObj = SubobjectOrNull(this, offset);
    }
    VirtualCall<uint32_t(__thiscall *)(void *), 1>(this); // AddRef
    return true;
}

uint32_t cHitChannel::Release()
{
    if (mRefCount > 1) return static_cast<uint32_t>(--mRefCount);
    VirtualCall<void *(__thiscall *)(void *, uint32_t), 37>(this, 1); // deleting destructor
    return 0;
}
