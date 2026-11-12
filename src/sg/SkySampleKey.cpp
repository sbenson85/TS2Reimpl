// nTSSG::SkySampleKey 0x3a97d0 (lane 261109r, scenegraph LG4g): a sky sample's layer tag. See sg/cLightingManager.h.
#include "sg/cLightingManager.h"

namespace nTSSG {

int32_t __cdecl SkySampleKey(int32_t sample)
{
    const int16_t room = static_cast<int16_t>(sample);
    return room < 0 ? 0x8000 : room;
}

} // namespace nTSSG
