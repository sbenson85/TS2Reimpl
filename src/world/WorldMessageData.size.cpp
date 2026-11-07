#include "world/WorldMessageData.h"

// Size-built (/O1) QueryInterface members of the world message data classes (WorldMessageData.h).

namespace nTSWorld {

bool cTerrainRectElevationChange::QueryInterface(uint32_t riid, void **ppv) { return message_data::QueryInterface(this, kIid, riid, ppv); }
bool cAwningUpdateData::QueryInterface(uint32_t riid, void **ppv) { return message_data::QueryInterface(this, kIid, riid, ppv); }

} // namespace nTSWorld
