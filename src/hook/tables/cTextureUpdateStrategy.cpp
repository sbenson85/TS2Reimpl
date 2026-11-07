#include "hook/hooks.h"
#include "sg/cTextureUpdateStrategy.h"

namespace {

// The texture manager makes our strategies (src/sg/cTextureManager.cpp); the plain strategy's Update is hooked because
// the video strategy's Update (0x405130, still the game's) calls it directly.
const HookSpec kHooks[] = {
    {"nRZSceneGraph_cTextureUpdateStrategy_Update", 0xaed030, 32, 0xd3851b36,
     MemberAddress(&nRZSceneGraph::cTextureUpdateStrategy::FillWhite)},
};
REGISTER_HOOKS(kHooks);

} // namespace
