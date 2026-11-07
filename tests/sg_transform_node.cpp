// Differential tests of nGZSceneGraph::cTransformNode, the transform-node pilot (lane OB, scenegraph N0 part 3): the game's
// node (its factory 0xa3a6c0, its vtables 0xe692ac / 0xe693bc and the template bodies behind them) against ours (Create),
// each side building its own nodes with its own factory. See sg_transform_pilot.h for what is driven and compared.
#include "sg_transform_pilot.h"

namespace {

using namespace nGZSceneGraph;

struct Cfg {
    using Interface = cITransformNode;
    static constexpr uint32_t kFactory = 0xa3a6c0;
    static constexpr uint32_t kSize = cTransformNode::kSize;
    static constexpr int kLevel = 0;
    static constexpr uint32_t kIid = cITransformNode::kIid;
    static constexpr uint32_t kEventCell = cTransformNodeBehaviorTraits::kEventManagerCell;
    static cICompositionTreeNode *Create() { return cTransformNode::Create(); }
};
using P = sgtp::Pilot<Cfg>;

} // namespace

DIFF_TEST(SG_TransformNode_Factory, Cfg::kFactory) { P::Factory(ctx); }
DIFF_TEST(SG_TransformNode_Slots, Cfg::kFactory) { P::Slots(ctx); }
DIFF_TEST(SG_TransformNode_Persist, Cfg::kFactory) { P::Persist(ctx); }
DIFF_TEST(SG_TransformNode_Clone, 0xa3ae50) { P::Clone(ctx); }
DIFF_TEST(SG_TransformNode_Events, Cfg::kFactory) { P::Events(ctx); }
