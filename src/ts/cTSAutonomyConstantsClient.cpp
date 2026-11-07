#include "ts/cTSAutonomyConstantsClient.h"
#include "game/game.h"

#include <new>

cTSAutonomyConstantsClient *cTSAutonomyConstantsClient::DeletingDestructor(uint32_t flags)
{
    *reinterpret_cast<const void **>(this) = GameAddress(kVTable);
    reinterpret_cast<cTSConstantsClient *>(this)->Destruct();
    if (flags & 1) ::operator delete(this, kObjectSize);
    return this;
}
