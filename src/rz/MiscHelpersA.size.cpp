// The size-built (/O1) members of src/rz/MiscHelpersA.h.
#include "rz/MiscHelpersA.h"

#include "game/game.h"

cTSAudioAmbienceGlobalPrimitiveBase *cTSAudioAmbienceGlobalPrimitiveBase::ConstructWithArg(uint32_t arg)
{
    mField4 = 0;
    mArg = arg;
    mVTable = GameAddress(kVTable);
    return this;
}
