#include "misc/DeletingDestructors.h"

#include <new>

#include "game/game.h"
#include "game/original.h"

// Speed-built deleting destructors (src/misc/DeletingDestructors.h).

cBranchNodesNoFilter *cBranchNodesNoFilter::DeletingDestructor(uint32_t flags)
{
    mVTable = GameAddress<void>(kVTable);
    if (flags & 1) ::operator delete(this, sizeof(cBranchNodesNoFilter));
    return this;
}

nGZEvent::cIEventHeader *nGZEvent::cIEventHeader::DeletingDestructor(uint32_t flags)
{
    mVTable = &ts2_rva_d9ca58;
    if (flags & 1) ::operator delete(this, sizeof(cIEventHeader));
    return this;
}

nTSCASCore::cNameMap *nTSCASCore::cNameMap::DeletingDestructor(uint32_t flags)
{
    mVTable = GameAddress<void>(kVTable);
    if (flags & 1) ::operator delete(this, sizeof(cNameMap));
    return this;
}

nTSCASCore::cTSCASLockedObjectManager *nTSCASCore::cTSCASLockedObjectManager::DeletingDestructor(uint32_t flags)
{
    OriginalFunction<void(__thiscall *)(cTSCASLockedObjectManager *), 0xcfe450>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cTSCASLockedObjectManager));
    return this;
}

nPathPlanner::cAStarSearch *nPathPlanner::cAStarSearch::DeletingDestructor(uint32_t flags)
{
    OriginalFunction<void(__thiscall *)(cAStarSearch *), 0x20ab60>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cAStarSearch));
    return this;
}

cSLTokenFieldBase *cSLTokenFieldBase::DeletingDestructor(uint32_t flags)
{
    OriginalFunction<void(__thiscall *)(cSLTokenFieldBase *), 0x9b9bb0>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cSLTokenFieldBase));
    return this;
}

cTSDelauneyMesh *cTSDelauneyMesh::DeletingDestructor(uint32_t flags)
{
    OriginalFunction<void(__thiscall *)(cTSDelauneyMesh *), 0xb545f0>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cTSDelauneyMesh));
    return this;
}

cTSSliceImposterCreator *cTSSliceImposterCreator::DeletingDestructor(uint32_t flags)
{
    OriginalFunction<void(__thiscall *)(cTSSliceImposterCreator *), 0xb51300>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cTSSliceImposterCreator));
    return this;
}
