// The speed-profile member of src/misc/StartupObjects.h (lane HQ).
#include "misc/StartupObjects.h"

#include "game/game.h"
#include "game/vtable.h"

#include <cstring>

namespace startupobj {

RecordPoolReset *RecordPoolReset::Construct()
{
    std::memset(GameAddress<void>(kRecords), 0, kRecordBytes);
    uint8_t **objects = GameAddress<uint8_t *>(kObjects); // [0] first, [1] last
    *GameAddress<uint32_t>(kCount) = 0;
    *GameAddress<uint8_t>(kReady) = 1;
    uint8_t *first = objects[0], *last = objects[1];
    if (first != last) {
        for (uint8_t *object = first; object != last; object += 0x20)
            VirtualCall<void *(__thiscall *)(void *, uint32_t), 33>(object, 0u);
        objects[1] = objects[0];
    }
    return this;
}

} // namespace startupobj
