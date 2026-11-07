// nGZSceneGraph::cViewerRefNodeRecursive (lane VF, scenegraph N3r). See the header.
#include "sg/cViewerRefNodeRecursive.h"

#include "sg/SGBehaviorCopies.h" // the cell the game's copies share

#include <new>

namespace nGZSceneGraph {

static_assert(sizeof(cViewerRefNodeRecursive) == cViewerRefNodeRecursive::kSize, "cViewerRefNodeRecursive size");

nGZEvent::cIConnectionManager *&cViewerRefNodeRecursiveBehaviorTraits::EventManagerCell() { return ts2_rva_10f6424; }

nGZEvent::cEventAttributeMap *cViewerRefNodeRecursiveBehaviorTraits::Registry() { return TransformNodeEventRegistry(); }

// (The original's operator new throws on failure, so its null branch is dead.)
cViewerRefNodeRecursive *__cdecl cViewerRefNodeRecursive::Create() { return new cViewerRefNodeRecursive; }

cViewerRefNodeRecursive *cViewerRefNodeRecursive::Construct() { return ::new (this) cViewerRefNodeRecursive; }

void cViewerRefNodeRecursive::Destruct() { this->cViewerRefNodeRecursive::~cViewerRefNodeRecursive(); }

cViewerRefNodeRecursive *cViewerRefNodeRecursive::DeletingDestructor(uint32_t flags)
{
    this->cViewerRefNodeRecursive::~cViewerRefNodeRecursive();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

bool cViewerRefNodeRecursive::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == kIid || iid == kBaseIid) {
        *out = static_cast<cIViewerRefNodeRecursive *>(this);
        AddRef();
        return true;
    }
    return Base::QueryInterface(iid, out);
}

void cViewerRefNodeRecursive::Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context)
{
    cICompositionTreeNode *clone = nullptr;
    if (context) {
        context->FindClone(this, kIid, &clone);
    } else {
        clone = new cViewerRefNodeRecursive;
        clone->AddRef();
    }
    if (mode != 2) {
        auto *const cloneImpl = &static_cast<cViewerRefNodeRecursive *>(clone)->mImpl;
        mImpl.CloneInto(this, clone, cloneImpl, mode, context);
    }
    clone->QueryInterface(iid, out);
    if (clone) clone->Release();
}

} // namespace nGZSceneGraph
