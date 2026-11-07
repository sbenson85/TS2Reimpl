// cGZRESIDtoExtensionMap (standalone shape, slot-hooked): the objects are built inline by cGZDBSegmentDirectoryFiles's
// constructors with the game's vtable, so each slot body of the class is hooked at its RVA to an adapter (slots 1, 2
// and 9 are shared bodies, verified elsewhere).
#include "hook/hooks.h"
#include "gz/cGZRESIDtoExtensionMap.h"

#define EXTENSION_MAP_ADAPTER(name) reinterpret_cast<void *>(&cGZRESIDtoExtensionMapAdapters::name)

namespace {

const HookSpec kHooks[] = {
    {"cGZRESIDtoExtensionMap_QueryInterface", 0x9a2690, 32, 0x5b5d01b3, EXTENSION_MAP_ADAPTER(QueryInterface)},
    {"cGZRESIDtoExtensionMap_AddExtension", 0x99f0f0, 32, 0xd3633f1b, EXTENSION_MAP_ADAPTER(AddExtension)},
    {"cGZRESIDtoExtensionMap_GetTypeForExtension", 0x9a0dd0, 32, 0x21f5deac, EXTENSION_MAP_ADAPTER(GetTypeForExtension)},
    {"cGZRESIDtoExtensionMap_GetExtensionForType", 0x9a26c0, 32, 0x49e7a073, EXTENSION_MAP_ADAPTER(GetExtensionForType)},
    {"cGZRESIDtoExtensionMap_Clear", 0x9a0d40, 32, 0x1aa8d3ba, EXTENSION_MAP_ADAPTER(Clear)},
    {"cGZRESIDtoExtensionMap_CopyFrom", 0x99faa0, 32, 0x22a5ce3a, EXTENSION_MAP_ADAPTER(CopyFrom)},
    {"cGZRESIDtoExtensionMap_DeletingDestructor", 0x99ecd0, 32, 0x0750c3e7, EXTENSION_MAP_ADAPTER(DeletingDestructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
