#include "world/WorldMessageData.h"

// Speed-built QueryInterface members of the world message data classes (WorldMessageData.h).

namespace nTSWorld {

bool cSupportChanged::QueryInterface(uint32_t riid, void **ppv) { return message_data::QueryInterface(this, kIid, riid, ppv); }
bool cRoofAddSpec::QueryInterface(uint32_t riid, void **ppv) { return message_data::QueryInterface(this, kIid, riid, ppv); }
bool cRoofRemoveSpec::QueryInterface(uint32_t riid, void **ppv) { return message_data::QueryInterface(this, kIid, riid, ppv); }
bool cRoofVisibilityChange::QueryInterface(uint32_t riid, void **ppv) { return message_data::QueryInterface(this, kIid, riid, ppv); }
bool cPoolSpec::QueryInterface(uint32_t riid, void **ppv) { return message_data::QueryInterface(this, kIid, riid, ppv); }
bool cFencePostAddSpec::QueryInterface(uint32_t riid, void **ppv) { return message_data::QueryInterface(this, kIid, riid, ppv); }
bool cWallAddSpec::QueryInterface(uint32_t riid, void **ppv) { return message_data::QueryInterface(this, kIid, riid, ppv); }
bool cWallPatternChange::QueryInterface(uint32_t riid, void **ppv) { return message_data::QueryInterface(this, kIid, riid, ppv); }
bool cWallRemoveSpec::QueryInterface(uint32_t riid, void **ppv) { return message_data::QueryInterface(this, kIid, riid, ppv); }
bool cWaterElevationChange::QueryInterface(uint32_t riid, void **ppv) { return message_data::QueryInterface(this, kIid, riid, ppv); }
bool cFloorElevationChange::QueryInterface(uint32_t riid, void **ppv) { return message_data::QueryInterface(this, kIid, riid, ppv); }
bool cFloorPatternChange::QueryInterface(uint32_t riid, void **ppv) { return message_data::QueryInterface(this, kIid, riid, ppv); }
bool cFloorVisibilityChange::QueryInterface(uint32_t riid, void **ppv) { return message_data::QueryInterface(this, kIid, riid, ppv); }
bool cTerrainElevationChange::QueryInterface(uint32_t riid, void **ppv) { return message_data::QueryInterface(this, kIid, riid, ppv); }

} // namespace nTSWorld
