#include "hook/hooks.h"
#include "world/cVertexLayer.h"

namespace {

using nTSWorld::cFencePostLayerAdapters;
using nTSWorld::cVertexLayerAdapters;

const HookSpec kHooks[] = {
    // cVertexLayer
    {"FUN_a0d146c0", 0xd146c0, 32, 0x06f08153, reinterpret_cast<void *>(&cVertexLayerAdapters::Construct)},
    {"FUN_a0d14760", 0xd14760, 26, 0xe8719eea, reinterpret_cast<void *>(&cVertexLayerAdapters::Destruct)},
    {"nTSWorld::cVertexLayer::vf6", 0xd14780, 32, 0x22046596, reinterpret_cast<void *>(&cVertexLayerAdapters::DeletingDestructor)},
    {"FUN_a0d147a0", 0xd147a0, 32, 0x5a7f0a0b, reinterpret_cast<void *>(&cVertexLayerAdapters::Add)},
    {"nTSWorld::cVertexLayer::GetGZCLSID", 0xd149f0, 6, 0x222da609, reinterpret_cast<void *>(&cVertexLayerAdapters::GetGZCLSID)},
    {"nTSWorld::cVertexLayer::QueryInterface", 0xd14a00, 32, 0xb0840299, reinterpret_cast<void *>(&cVertexLayerAdapters::QueryInterface)},
    {"nTSWorld::cVertexLayer::Read", 0xd14a30, 32, 0xc2ef075c, reinterpret_cast<void *>(&cVertexLayerAdapters::Read)},
    {"FUN_a0d14b70", 0xd14b70, 32, 0xc6308c3b, reinterpret_cast<void *>(&cVertexLayerAdapters::Remove)},
    {"nTSWorld::cVertexLayer::Write", 0xd14c50, 32, 0x7214056f, reinterpret_cast<void *>(&cVertexLayerAdapters::Write)},
    // cFencePostLayer
    {"FUN_a0820530", 0x820530, 32, 0x2667dd2e, reinterpret_cast<void *>(&cFencePostLayerAdapters::Construct)},
    {"FUN_a0820580", 0x820580, 14, 0x0465cc54, reinterpret_cast<void *>(&cFencePostLayerAdapters::Destruct)},
    {"nTSWorld::cFencePostLayer::vf6", 0x820590, 32, 0x457af257, reinterpret_cast<void *>(&cFencePostLayerAdapters::DeletingDestructor)},
    {"FUN_a08205b0", 0x8205b0, 32, 0xb4a4493c, reinterpret_cast<void *>(&cFencePostLayerAdapters::Set)},
    {"nTSWorld::cFencePostLayer::vf7", 0x820710, 8, 0xb068e5c6, reinterpret_cast<void *>(&cFencePostLayerAdapters::Clear)},
    {"nTSWorld::cFencePostLayer::GetGZCLSID", 0x820720, 6, 0x8dadfe2a, reinterpret_cast<void *>(&cFencePostLayerAdapters::GetGZCLSID)},
    {"FUN_a0820730", 0x820730, 32, 0x43984086, reinterpret_cast<void *>(&cFencePostLayerAdapters::Vertices)},
    {"FUN_a08207a0", 0x8207a0, 32, 0x3029b62b, reinterpret_cast<void *>(&cFencePostLayerAdapters::Contains)},
    {"FUN_a0820810", 0x820810, 32, 0xb1bbbd86, reinterpret_cast<void *>(&cFencePostLayerAdapters::ValueOf)},
    {"nTSWorld::cFencePostLayer::QueryInterface", 0x820880, 32, 0x1555aef3, reinterpret_cast<void *>(&cFencePostLayerAdapters::QueryInterface)},
    {"nTSWorld::cFencePostLayer::Read", 0x8208b0, 32, 0xc2ef075c, reinterpret_cast<void *>(&cFencePostLayerAdapters::Read)},
    {"FUN_a08209f0", 0x8209f0, 32, 0x97559935, reinterpret_cast<void *>(&cFencePostLayerAdapters::Remove)},
    {"nTSWorld::cFencePostLayer::Write", 0x820a80, 32, 0x7214056f, reinterpret_cast<void *>(&cFencePostLayerAdapters::Write)},
};
REGISTER_HOOKS(kHooks);

} // namespace
