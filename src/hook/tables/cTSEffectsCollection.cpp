#include "hook/hooks.h"
#include "ts/cTSEffectsCollection.h"

// Standalone shape (docs/standalone-shape.md): the constructor hook builds every collection with our vtable (the factory
// 0xbd8820 is ours too). The game's cTSEffectsResource (constructor 0xbd2ba0, not ours) then stores its own vtable over the
// collection face at +4, whose slots 3 and 5-11 are the RVAs below: those entries are adapters, serving the resources and the
// manager's direct calls of slot 3. FindEffectIndex is a non-virtual member (callers: slots 5 and 11).
namespace {

using A = cTSEffectsCollectionAdapters;

const HookSpec kHooks[] = {
    {"cTSEffectsCollection_Construct", 0xbd7210, 32, 0x57624182, reinterpret_cast<void *>(&A::Construct)},
    {"cTSEffectsCollection_Destruct", 0xbd7670, 32, 0xef371bec, reinterpret_cast<void *>(&A::Destruct)},
    {"cTSEffectsCollection_DeletingDestructor", 0xbd7820, 32, 0x30f56013, reinterpret_cast<void *>(&A::DeletingDestructor)},
    {"cTSEffectsCollection_Create", 0xbd8820, 32, 0xe828012d, reinterpret_cast<void *>(&cTSEffectsCollection::Create)},
    {"cTSEffectsCollection_QueryInterface", 0xbd8a40, 32, 0x4d549794, reinterpret_cast<void *>(&A::QueryInterface)},
    {"cTSEffectsCollection_Clear", 0xbd8680, 32, 0x5d46290b, reinterpret_cast<void *>(&A::Clear)},
    {"cTSEffectsCollection_AddEffect", 0xbd7c00, 32, 0x215e5469, reinterpret_cast<void *>(&A::AddEffect)},
    {"cTSEffectsCollection_GetEffect", 0xbd8970, 32, 0x330040e5, reinterpret_cast<void *>(&A::GetEffect)},
    {"cTSEffectsCollection_AddDescription", 0xbd7840, 32, 0x2482d6e5, reinterpret_cast<void *>(&A::AddDescription)},
    {"cTSEffectsCollection_GetDescription", 0xbd8890, 32, 0xb0c42449, reinterpret_cast<void *>(&A::GetDescription)},
    {"cTSEffectsCollection_AddMessageAlias", 0xbd7b40, 32, 0x6cf2a31e, reinterpret_cast<void *>(&A::AddMessageAlias)},
    {"cTSEffectsCollection_AddMessageTrigger", 0xbd7ef0, 32, 0x66cb2f80, reinterpret_cast<void *>(&A::AddMessageTrigger)},
    {"cTSEffectsCollection_SetEffectVariant", 0xbd8b50, 32, 0x61950e53, reinterpret_cast<void *>(&A::SetEffectVariant)},
    {"cTSEffectsCollection_FindEffectIndex", 0xbd87b0, 32, 0x73534135, reinterpret_cast<void *>(&A::FindEffectIndex)},
    {"cTSEffectsCollection_MergeInto", 0xbd7f30, 32, 0x479d3602, reinterpret_cast<void *>(&A::MergeInto)},
    {"cTSEffectsCollection_AddAllTo", 0xbd8080, 32, 0x89965a61, reinterpret_cast<void *>(&A::AddAllTo)},
    {"cTSEffectsCollection_AppendTo", 0xbd8160, 32, 0x68fb2320, reinterpret_cast<void *>(&A::AppendTo)},
};
REGISTER_HOOKS(kHooks);

} // namespace
