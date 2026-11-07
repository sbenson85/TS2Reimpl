#include "hook/hooks.h"
#include "compgeom/Frustum.h"

// nGZCompGeom's frustum shape cFrustumShape and its cull tests (lane PV, src/compgeom/Frustum.h). Standalone shape: the
// director's factory hands out our shape classes, so the vtables' slots (QueryInterface, AddRef, Release, Clone,
// GetClassID, the data accessors, the destructor) are in effect through callers (trap: the Create hook). The Frustum
// members and the two plane helpers are hooked at their RVAs: the scene graph's original cull code calls them.
namespace {

const HookSpec kHooks[] = {
    {"Frustum_Create", 0x197740, 32, 0xf3a0b273, reinterpret_cast<void *>(&nGZCompGeom::cFrustumShape::Create)},
    {"Frustum_FromView", 0x1975a0, 32, 0x40cc6efe, MemberAddress(&nGZCompGeom::Frustum::FromView)},
    {"Frustum_ClassifyBoxAt", 0x197520, 32, 0xa309bfbd, MemberAddress(&nGZCompGeom::Frustum::ClassifyBoxAt)},
    {"Frustum_ClassifyBoxInFrame", 0x1974a0, 32, 0xa309bfbd, MemberAddress(&nGZCompGeom::Frustum::ClassifyBoxInFrame)},
    {"Frustum_ClassifyBox", 0x197330, 32, 0x0b08d2b8, MemberAddress(&nGZCompGeom::Frustum::ClassifyBox)},
    {"PlaneBoxSide", 0x1a28c0, 32, 0x55bd5d1c, reinterpret_cast<void *>(&nGZCompGeom::PlaneBoxSide)},
    {"PlaneFrameBoxSide", 0x1a2810, 32, 0x3e1b2109, reinterpret_cast<void *>(&nGZCompGeom::PlaneFrameBoxSide)},
};
REGISTER_HOOKS(kHooks);

} // namespace
