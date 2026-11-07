#include "hook/hooks.h"
#include "ts/cTSWantTreeLoader.h"

namespace {

// LoadTree builds ours; the slot adapters serve the loaders cTSWantTreeManager::vf8 0x554a50 still builds inline with the
// game's vtable 0xdd43c0. cTSWantPropertySetLoader's slots (EndElement 0x5528f0, the deleting destructor 0x552220) have no
// entry: only its vtables 0xdd4370 / 0xdd438c refer to them, stored only by the original StartElement 0x553210 and the
// destructor itself ("in_effect": "callers" under the hooks callers.py names: cTSWantSimulator_Construct & cTSWantTreeLoader_Construct).
const HookSpec kHooks[] = {
    {"cTSWantTreeLoader_LoadTree", 0x5550c0, 32, 0x0d4ce55a, reinterpret_cast<void *>(&cTSWantTreeLoader::LoadTree)},
    {"cTSWantTreeLoader_Construct", 0x551bd0, 32, 0xb93d0bbe, reinterpret_cast<void *>(&cTSWantTreeLoaderAdapters::Construct)},
    {"cTSWantTreeLoader_Reset", 0x556240, 32, 0x9f5a1f0f, reinterpret_cast<void *>(&cTSWantTreeLoaderAdapters::Reset)},
    {"cTSWantTreeLoader_Release", 0x5560b0, 32, 0xa3726aa6, reinterpret_cast<void *>(&cTSWantTreeLoaderAdapters::Release)},
    {"cTSWantTreeLoader_StartElement", 0x553210, 32, 0x806f50cc, reinterpret_cast<void *>(&cTSWantTreeLoaderAdapters::StartElement)},
    {"cTSWantTreeLoader_EndElement", 0x552960, 32, 0x86ee5d68, reinterpret_cast<void *>(&cTSWantTreeLoaderAdapters::EndElement)},
};
REGISTER_HOOKS(kHooks);

} // namespace
