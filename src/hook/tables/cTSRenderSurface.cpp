#include "hook/hooks.h"
#include "sg/cTSRenderSurface.h"

namespace {

using A = nTSSG::cTSRenderSurfaceAdapters;

// The three constructors (the dynamic render strategy's slot 8 builds them inline after `new`): every surface the game
// makes is ours, so its slots, destructors and ReparseShaders are in effect through callers. The non-virtual members the
// original strategy still calls are hooked (db/status/cTSRenderSurface.jsonl).
const HookSpec kHooks[] = {
    {"nTSSG_cTSRenderBackbufferSurface_Construct", 0x36eb10, 32, 0xeb5ca363, reinterpret_cast<void *>(&A::ConstructBackbuffer)},
    {"nTSSG_cTSRenderTargetSurface_Construct", 0x36ebf0, 32, 0x978192a1, reinterpret_cast<void *>(&A::ConstructTarget)},
    {"nTSSG_cTSRenderTextureSurface_Construct", 0x36ec70, 32, 0x4c1b760b, reinterpret_cast<void *>(&A::ConstructTexture)},
    {"nTSSG_cTSRenderSurfaceImpl_Bind", 0x375a90, 32, 0x3cda7973, reinterpret_cast<void *>(&A::Bind)},
    {"nTSSG_cTSRenderTargetSurface_Reset", 0x3759a0, 32, 0x075e9d75, reinterpret_cast<void *>(&A::Reset)},
    {"nTSSG_cTSRenderBackbufferSurface_Init", 0x3730e0, 26, 0xecb2ce13, reinterpret_cast<void *>(&A::InitBackbuffer)},
    {"nTSSG_cTSRenderTargetSurface_Init", 0x373100, 32, 0xbb565147, reinterpret_cast<void *>(&A::InitTarget)},
    {"nTSSG_cTSRenderTextureSurface_Init", 0x373250, 32, 0xe7c49d6c, reinterpret_cast<void *>(&A::InitTexture)},
    {"nTSSG_cTSRenderBackbufferSurface_Configure", 0x373390, 32, 0x2fc4390e, reinterpret_cast<void *>(&A::ConfigureBackbuffer)},
    {"nTSSG_cTSRenderTargetSurface_Configure", 0x3733f0, 32, 0x396be25e, reinterpret_cast<void *>(&A::ConfigureTarget)},
    {"nTSSG_cTSRenderTextureSurface_Configure", 0x373450, 32, 0xffc119ff, reinterpret_cast<void *>(&A::ConfigureTexture)},
    {"nTSSG_cTSRenderTextureSurface_SetExternal", 0x3734d0, 32, 0xf9baac5c, reinterpret_cast<void *>(&A::SetExternal)},
    {"nTSSG_cTSRenderTargetSurface_ReleaseSurfaces", 0x375950, 32, 0x4942654f, reinterpret_cast<void *>(&A::ReleaseSurfaces)},
    {"nTSSG_cTSRenderTextureSurface_ResetTexture", 0x375990, 11, 0xcd0bc227, reinterpret_cast<void *>(&A::ResetTexture)},
    // The target vtable's own slots (see cTSRenderSurfaceAdapters): IsValid, Create, Destroy, the deleting destructor.
    {"nTSSG_cTSRenderTargetSurface_IsValid", 0x3736e0, 32, 0x07ad8ac6, reinterpret_cast<void *>(&A::IsValidTarget)},
    {"nTSSG_cTSRenderTargetSurface_Create", 0x372790, 32, 0xe43106fe, reinterpret_cast<void *>(&A::CreateTarget)},
    {"nTSSG_cTSRenderTargetSurface_Destroy", 0x3757b0, 20, 0xd5783c23, reinterpret_cast<void *>(&A::DestroyTarget)},
    {"nTSSG_cTSRenderTargetSurface_DeletingDestructor", 0x36fbf0, 32, 0xcb92ed50, reinterpret_cast<void *>(&A::DeleteTarget)},
};
REGISTER_HOOKS(kHooks);

} // namespace
