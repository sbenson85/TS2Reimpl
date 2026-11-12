#include "hook/hooks.h"
#include "sg/cDisplayListCensorRenderStrategy.h"

namespace {

using A = nTSSG::cDisplayListCensorRenderStrategyAdapters;

// The constructor (also run by the dynamic strategy's constructor 0x36e400 on its own `new 0x38`) and the COM factory:
// every censor strategy the game makes is ours, so its slots, destructor and vector code are in effect through callers.
// The vector destructor is hooked because the constructor's unwind funclets still name it
// (db/status/cDisplayListCensorRenderStrategy.jsonl). The dirty-rect members (row TS1h) the dynamic strategy's
// drawing paths call while those are the original's (0x374670 calls CollectDirtyRects, 0x374ad0 ListBoxRects and
// RenderTexture) are hooked; their own callees are in effect through them.
const HookSpec kHooks[] = {
    {"nTSSG_cDisplayListCensorRenderStrategy_Construct", 0x3775c0, 32, 0xc2779009, reinterpret_cast<void *>(&A::Construct)},
    {"nTSSG_cDisplayListCensorRenderStrategy_Create", 0x377ee0, 32, 0x3d6225cf, reinterpret_cast<void *>(&A::Create)},
    {"nTSSG_cDisplayListCensorRenderStrategy_DestroyBoxes", 0x3776c0, 32, 0x54a9fcb5, reinterpret_cast<void *>(&A::DestroyBoxes)},
    {"nTSSG_cDisplayListCensorRenderStrategy_CollectDirtyRects", 0x377f50, 32, 0x27884e73, MemberAddress<&nTSSG::cDisplayListCensorRenderStrategy::CollectDirtyRects>()},
    {"nTSSG_cDisplayListCensorRenderStrategy_ListBoxRects", 0x378020, 32, 0xc5e6cdec, MemberAddress<&nTSSG::cDisplayListCensorRenderStrategy::ListBoxRects>()},
    {"nTSSG_cDisplayListCensorRenderStrategy_RenderTexture", 0x378370, 32, 0x26fb943d, MemberAddress<&nTSSG::cDisplayListCensorRenderStrategy::RenderTexture>()},
};
REGISTER_HOOKS(kHooks);

} // namespace
