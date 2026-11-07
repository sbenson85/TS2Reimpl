// nRZGraphic4::cSurfaceLockAsGZBuffer (lane VV, src/gz/cSurfaceLockAsGZBuffer.h): the constructor and destructor, called by
// the image's users on their own stack storage. With both ours, every such buffer carries our vtable, so the image's
// vtable 0xda0ca8 and its slot bodies (the deleting destructor 0x18a420 among them) are reached by nothing. Also the
// missing-texture drawing nGZSceneGraph::DrawMissingTextureName 0xae8310 (src/sg/DrawMissingTextureName.h), the buffer's
// one user in the scene graph.
#include "gz/cSurfaceLockAsGZBuffer.h"
#include "hook/hooks.h"
#include "sg/DrawMissingTextureName.h"

#include <new>

namespace {

using nGZGraphic4::cILockableSurface;
using nRZGraphic4::cSurfaceLockAsGZBuffer;

// 0x18a2a0: thiscall (surface), returns this.
cSurfaceLockAsGZBuffer *__fastcall Construct(cSurfaceLockAsGZBuffer *self, void *, cILockableSurface *surface)
{
    return new (self) cSurfaceLockAsGZBuffer(surface);
}

// 0x18a330: thiscall, the complete-object destructor (no free).
void __fastcall Destruct(cSurfaceLockAsGZBuffer *self)
{
    self->cSurfaceLockAsGZBuffer::~cSurfaceLockAsGZBuffer();
}

const HookSpec kHooks[] = {
    {"nRZGraphic4_cSurfaceLockAsGZBuffer_Construct", 0x18a2a0, 32, 0x2f604b2e, reinterpret_cast<void *>(&Construct)},
    {"nRZGraphic4_cSurfaceLockAsGZBuffer_Destruct", 0x18a330, 32, 0xc30bea96, reinterpret_cast<void *>(&Destruct)},
    {"nGZSceneGraph_DrawMissingTextureName", 0xae8310, 32, 0xcf1ef20b,
     reinterpret_cast<void *>(&nGZSceneGraph::DrawMissingTextureName)},
};
REGISTER_HOOKS(kHooks);

} // namespace
