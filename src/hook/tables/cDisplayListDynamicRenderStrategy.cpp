#include "hook/hooks.h"
#include "sg/cDisplayListDynamicRenderStrategy.h"

namespace {

const HookSpec kHooks[] = {
    {"nTSSG_cDisplayListDynamicRenderStrategy_vf8_3", 0x3703e0, 12, 0x1c1c9ea7, MemberAddress(&nTSSG::cDisplayListDynamicRenderStrategyFace::ForwardSlot3)},
    {"nTSSG_cDisplayListDynamicRenderStrategy_vf8_6", 0x373690, 19, 0x9c20b8cc, MemberAddress(&nTSSG::cDisplayListDynamicRenderStrategyFace::ForwardSlot6)},
    {"nTSSG_cDisplayListDynamicRenderStrategy_vf8_7", 0x3703d0, 12, 0x01ab8ec3, MemberAddress(&nTSSG::cDisplayListDynamicRenderStrategyFace::ForwardSlot7)},
    {"nTSSG_cDisplayListDynamicRenderStrategy_vf8_9", 0x370380, 32, 0x6c9b1bb5, MemberAddress(&nTSSG::cDisplayListDynamicRenderStrategyFace::Register)},
    {"nTSSG_cDisplayListDynamicRenderStrategy_vf8_10", 0x373de0, 32, 0xf284e5cb, MemberAddress(&nTSSG::cDisplayListDynamicRenderStrategyFace::Unregister)},
    {"nTSSG_cDisplayListDynamicRenderStrategy_vf3", 0x36fb80, 32, 0x9593d2e7, MemberAddress(&nTSSG::cDisplayListDynamicRenderStrategy::DeletingDestructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
