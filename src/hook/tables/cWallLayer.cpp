#include "hook/hooks.h"
#include "world/cWallLayer.h"

namespace {

using nTSWorld::cWallLayerAdapters;

const HookSpec kHooks[] = {
    {"nTSWorld::cWallLayer::cWallLayer", 0x81ff50, 32, 0xa3cc5847, reinterpret_cast<void *>(&cWallLayerAdapters::Construct)},
    {"nTSWorld::cWallLayer::~cWallLayer", 0x81ffa0, 14, 0x5364671c, reinterpret_cast<void *>(&cWallLayerAdapters::DestructBody)},
    {"nTSWorld::cWallLayer::vf6", 0x820060, 32, 0xd8e78f31, reinterpret_cast<void *>(&cWallLayerAdapters::DeletingDestructor)},
    {"nTSWorld::cWallLayer::Clear", 0x8200b0, 8, 0xaad50f14, reinterpret_cast<void *>(&cWallLayerAdapters::Clear)},
    {"nTSWorld::cWallLayer::GetGZCLSID", 0x8200c0, 6, 0xc922e918, reinterpret_cast<void *>(&cWallLayerAdapters::GetGZCLSID)},
    {"nTSWorld::cWallLayer::QueryInterface", 0x8200d0, 32, 0x4a3e856e, reinterpret_cast<void *>(&cWallLayerAdapters::QueryInterface)},
    {"nTSWorld::cWallLayer::Read", 0x820100, 32, 0xc2ef075c, reinterpret_cast<void *>(&cWallLayerAdapters::Read)},
    {"nTSWorld::cWallLayer::Erase", 0x820240, 32, 0x5a84ac90, reinterpret_cast<void *>(&cWallLayerAdapters::Erase)},
    {"nTSWorld::cWallLayer::SetHalf", 0x820290, 32, 0xdf879d44, reinterpret_cast<void *>(&cWallLayerAdapters::SetHalf)},
    {"nTSWorld::cWallLayer::Has", 0x820300, 32, 0x9ef48ae6, reinterpret_cast<void *>(&cWallLayerAdapters::Has)},
    {"nTSWorld::cWallLayer::Half", 0x820340, 32, 0xcd7d1fa0, reinterpret_cast<void *>(&cWallLayerAdapters::Half)},
    {"nTSWorld::cWallLayer::First", 0x8203a0, 32, 0x785032e9, reinterpret_cast<void *>(&cWallLayerAdapters::First)},
    {"nTSWorld::cWallLayer::Write", 0x8203f0, 32, 0x6421612f, reinterpret_cast<void *>(&cWallLayerAdapters::Write)},
};
REGISTER_HOOKS(kHooks);

} // namespace
