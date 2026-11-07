#include "hook/hooks.h"
#include "sg/cLevelInfo.h"

namespace {

// The factory (the director's for class 0xed534136) and both constructors: with them ours no game object carries the
// class's vtables (0xe76444 / 0xe7648c, referenced by the two constructors alone), so every slot and the deleting
// destructor 0xab01a0 are ours through them. The copy 0xab0120 has game callers (cTSTextureRecipeImageData); the pixel
// buffer's resize 0xab41b0 is called by the copy and Read alone, so it is ours through them.
const HookSpec kHooks[] = {
    {"nGZSceneGraph_cLevelInfo_Create", 0xab15f0, 32, 0xa42f448a,
     reinterpret_cast<void *>(&nGZSceneGraph::cLevelInfo::Create)},
    {"nGZSceneGraph_cLevelInfo_Construct", 0xaaff70, 32, 0x0f51ed82, const_cast<uint8_t *>(&cLevelInfo_Constructor)},
    {"nGZSceneGraph_cLevelInfo_ConstructSized", 0xaafe10, 32, 0x1f7620ce,
     const_cast<uint8_t *>(&cLevelInfo_SizedConstructor)},
    {"nGZSceneGraph_cLevelInfo_Assign", 0xab0120, 32, 0x630390fa,
     reinterpret_cast<void *>(&nGZSceneGraph::cLevelInfoAdapters::Assign)},
};
REGISTER_HOOKS(kHooks);

} // namespace
