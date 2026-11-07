#include "hook/hooks.h"
#include "gz/cShaderProgramsDX.h"

namespace {

// The getter is the shader programs' switch (docs/standalone-shape.md): the graphic system's slot 19 jumps to it and
// 0x18ddf0 caches its answer, so the manager, and every program and inputs object it makes, is ours; every slot is in
// effect through its callers.
const HookSpec kHooks[] = {
    {"nGZGraphic4_cShaderProgramsManagerDX_Instance", 0x1758c0, 32, 0x267d9f8e,
     reinterpret_cast<void *>(&nGZGraphic4::cShaderProgramsManagerDX::Instance)},
    // A fragment linker released and emptied (lane RB).
    {"nGZGraphic4_cShaderProgramsManagerDX_ReleaseLinker", 0x176e40, 32, 0xb8f836cf,
     MemberAddress(&nGZGraphic4::cShaderProgramsManagerDX::ReleaseLinker)},
};
REGISTER_HOOKS(kHooks);

} // namespace
