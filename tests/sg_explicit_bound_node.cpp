// Differential tests of nGZSceneGraph::cExplicitBoundNode, the explicit-bound node pilot (lane OB, scenegraph N0 part 3):
// the game's node (its factory 0xa4bc30, its vtables 0xe6c2a4 / 0xe6c3e8 and the template bodies behind them) against
// ours (Create), each side building its own nodes with its own factory. The shape the node keeps and the line it is tested
// with are fakes. See sg_transform_pilot.h for what is driven and compared.
#include "sg_transform_pilot.h"

namespace {

using namespace nGZSceneGraph;

struct Cfg {
    using Interface = cIExplicitBoundNode;
    static constexpr uint32_t kFactory = 0xa4bc30;
    static constexpr uint32_t kSize = cExplicitBoundNode::kSize;
    static constexpr int kLevel = 2;
    static constexpr uint32_t kIid = cIExplicitBoundNode::kIid;
    static constexpr uint32_t kEventCell = cExplicitBoundNodeBehaviorTraits::kEventManagerCell;
    static cICompositionTreeNode *Create() { return cExplicitBoundNode::Create(); }
};
using P = sgtp::Pilot<Cfg>;

} // namespace

DIFF_TEST(SG_ExplicitBoundNode_Factory, Cfg::kFactory) { P::Factory(ctx); }
DIFF_TEST(SG_ExplicitBoundNode_Slots, Cfg::kFactory) { P::Slots(ctx); }
DIFF_TEST(SG_ExplicitBoundNode_Persist, Cfg::kFactory) { P::Persist(ctx); }
DIFF_TEST(SG_ExplicitBoundNode_Clone, 0xa3ae50) { P::Clone(ctx); }
DIFF_TEST(SG_ExplicitBoundNode_Events, Cfg::kFactory) { P::Events(ctx); }
