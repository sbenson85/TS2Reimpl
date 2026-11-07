// nGZGraphic4::cTextureAtlas (lane OL, src/gz/cTextureAtlas.h): the packer's functions, each hooked at its own RVA (the
// scenegraph's render-target textures and other classes call them too).
#include "hook/hooks.h"
#include "gz/cTextureAtlas.h"

#include <new>

namespace {

using nGZGraphic4::cTextureAtlas;

// 0x18bb40: the constructor (all zeros), called on storage inside other objects.
cTextureAtlas *__fastcall ConstructAtlas(cTextureAtlas *self)
{
    return new (self) cTextureAtlas();
}

const HookSpec kHooks[] = {
    {"cTextureAtlas_Construct", 0x18bb40, 32, 0xd895900f, reinterpret_cast<void *>(&ConstructAtlas)},
    {"cTextureAtlas_AllocateIn", 0x18bb80, 32, 0x89acc073, MemberAddress(&cTextureAtlas::AllocateIn)},
    {"cTextureAtlas_AddNode", 0x18bd40, 32, 0xe9efb195, MemberAddress(&cTextureAtlas::AddNode)},
    {"cTextureAtlas_Allocate", 0x18bdf0, 32, 0x2745d79e, MemberAddress(&cTextureAtlas::Allocate)},
    {"cTextureAtlas_Free", 0x18be20, 32, 0xac00322d, MemberAddress(&cTextureAtlas::Free)},
    {"cTextureAtlas_Init", 0x18bf00, 32, 0xeaa1bf38, MemberAddress(&cTextureAtlas::Init)},
    {"cTextureAtlas_Clear", 0x18bf30, 32, 0xafdae9db, MemberAddress(&cTextureAtlas::Clear)},
};
REGISTER_HOOKS(kHooks);

} // namespace
