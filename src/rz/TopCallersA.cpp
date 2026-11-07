#include "rz/TopCallersA.h"

#include "game/game.h"
#include "game/vtable.h"
#include "rz/cRZRefCount.h"

namespace nTSWorld {

uint32_t cWorldDBMessageData::Release()
{
    uint32_t count = mRefCount - 1;
    mRefCount = count;
    if (count != 0) return count;
    // The original writes its vtable, then frees the block with operator delete (0x824624 -> free).
    mVTable = GameAddress(kVTable);
    ::operator delete(this);
    return 0;
}

} // namespace nTSWorld

uint8_t __cdecl SetWindowEnabled(void *window, uint32_t id, uint32_t enabled)
{
    // The original passes full dwords (`id` to slot 39, `enabled` to slot 83), but compares only
    // the low byte of the child's slot-82 result (al) against `enabled`.
    using GetChild = void *(__thiscall *)(void *, uint32_t);
    using Query = uint8_t(__thiscall *)(void *, uint32_t);
    using Set = void(__thiscall *)(void *, uint32_t, uint32_t);

    void *child = VirtualCall<GetChild, 39>(window, id);
    if (!child) return 0;
    if (VirtualCall<Query, 82>(child, 2) != static_cast<uint8_t>(enabled)) VirtualCall<Set, 83>(child, 2, enabled);
    return 1;
}

cRZMemPtrIteratorU16 *cRZMemPtrIteratorU16::Copy() const
{
    auto *copy = static_cast<cRZMemPtrIteratorU16 *>(::operator new(sizeof(cRZMemPtrIteratorU16)));
    if (!copy) return nullptr;
    copy->mVTable = GameAddress(kVTable);
    copy->mField4 = 0;
    copy->mField8 = mField8;
    copy->mFieldC = mFieldC;
    return copy;
}

namespace nGZCommandParser {

void cBlockCommand::StoreAndQuery(void *object, uint32_t value)
{
    mValue = value;
    mObject = object;
    // Only the low byte of the object's slot-43 result (al) is stored.
    mFlag = static_cast<uint8_t>(VirtualCall<uint32_t(__thiscall *)(void *), 43>(object));
}

} // namespace nGZCommandParser
