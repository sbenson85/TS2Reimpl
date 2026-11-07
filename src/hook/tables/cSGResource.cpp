#include "hook/hooks.h"
#include "gz/cSGResource.h"
#include "sg/cSGResource.h"

namespace {

using SGResourceAdapters = nGZSceneGraph::cSGResourceAdapters;

// nGZSceneGraph::cSGResource as a whole class (src/sg/cSGResource.h, lane OE). The game's ~40 derived classes call the
// constructor (as a base part, flag 0: the hook is our compiler's own constructor), the destructor, QueryInterface, Read
// and Write directly. Through callers only: the deleting destructor 0xa3c380, GetTypeInfo 0xa3ce10 and the vtables'
// vtordisp thunks. Bisect: the five class rows together (a derived object would otherwise mix the game's and our vtables).
//
// Below them, the bodies of the virtual base's slots (hook-shaped, this = object + 0x30, src/gz/cSGResource.cpp): our
// class calls them too. The 4-byte bodies (0xa3c3c0, 0xa3c4a0) are too short to hook and are verified by the
// differential test only.
const HookSpec kHooks[] = {
    {"cSGResource_Construct", 0xa3c270, 32, 0x49939120, const_cast<uint8_t *>(&OE_cSGResource_Constructor)},
    {"cSGResource_Destruct", 0xa3c2f0, 32, 0x23fb97a1, reinterpret_cast<void *>(&SGResourceAdapters::Destruct)},
    {"cSGResource_QueryInterface", 0xa3c4d0, 32, 0x177a3799, reinterpret_cast<void *>(&SGResourceAdapters::QueryInterface)},
    {"cSGResource_Read", 0xa3c540, 32, 0xbdf716e9, reinterpret_cast<void *>(&SGResourceAdapters::Read)},
    {"cSGResource_Write", 0xa3ced0, 32, 0x045a834f, reinterpret_cast<void *>(&SGResourceAdapters::Write)},
    {"cSGResource_CompareIdentity", 0xa3c3d0, 32, 0x1cfdd7b1, MemberAddress(&nGZSceneGraph::cSGResourceSubobject::CompareIdentity)},
    {"cSGResource_ForwardKey", 0xa3c6d0, 32, 0x8dffcf09, MemberAddress(&nGZSceneGraph::cSGResourceSubobject::ForwardKey)},
    {"cSGResource_GetVirtualBase", 0xa3c720, 32, 0x5cb20f86, MemberAddress(&nGZSceneGraph::cSGResourceSubobject::GetVirtualBase)},
    {"cSGResource_CopyKeyTo", 0xa3c770, 20, 0x47837a06, MemberAddress(&nGZSceneGraph::cSGResourceSubobject::CopyKeyTo)},
    {"cSGResource_SetFlag", 0xa3c870, 10, 0x82f9d241, MemberAddress(&nGZSceneGraph::cSGResourceSubobject::SetFlag)},
    {"cSGResource_SetResource", 0xa3cdd0, 32, 0x0da7cf68, MemberAddress(&nGZSceneGraph::cSGResourceSubobject::SetResource)},
};
REGISTER_HOOKS(kHooks);

} // namespace
