#include "misc/SmallVirtuals.h"

#include <cstring>
#include <new>
#include <string>
#include <unordered_map>

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"

// Speed-built small slots (src/misc/SmallVirtuals.h).

namespace {

// A slot returning bool: MSVC loads it into a register (`mov eax, [eax+N]; call eax`), as the original does.
template <int N, class R, class... A> struct NarrowSlot : vtable_detail::Pad<N> {
    virtual R __thiscall call(A...) = 0;
};

} // namespace

uint32_t nGZEvent::cNullEvent::GetEventType() const
{
    return kEventType;
}

void *nGZEvent::cNullEvent::CloneInto(void *dest) const
{
    auto *event = static_cast<cNullEvent *>(dest);
    event->mVTable = GameAddress<void>(kVTable);
    return event;
}

uint32_t nTSSG::cTSSGObjectsFilter::IsIncluded(void *node) const
{
    return !static_cast<NarrowSlot<48, bool, uint32_t> *>(node)->call(0xf);
}

void nTSSG::cBasePolygon::NotifyOwner()
{
    VirtualCall<void(__thiscall *)(void *, uint32_t), 36>(mOwner, mId);
    VirtualCall<void(__thiscall *)(void *, uint32_t), 36>(mOwner, mId);
    VirtualCall<void(__thiscall *)(void *, uint32_t), 36>(mOwner, mId);
}

uint32_t nTSSG::cObjectImposterFilter::IsIncluded(void *node) const
{
    if (!node || !static_cast<NarrowSlot<25, bool, uint32_t> *>(node)->call(0xe519c92c)) return 1;
    return VirtualCall<uint32_t(__thiscall *)(void *), 26>(node) == 0;
}

uint32_t nRZSceneGraph::cBranchNodesIIDFilter::IsIncluded(void *node) const
{
    return !static_cast<NarrowSlot<25, bool, uint32_t> *>(node)->call(mIID);
}

void nRZSceneGraph::cBranchNodesIIDFilter::Destruct()
{
    mVTable = &ts2_rva_e7cf54;
}

nRZSceneGraph::cBranchNodesIIDFilter *nRZSceneGraph::cBranchNodesIIDFilter::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cBranchNodesIIDFilter));
    return this;
}

uint32_t nRZSceneGraph::cBranchNodesMaskBitAndIIDFilter::IsIncluded(void *node) const
{
    if (static_cast<NarrowSlot<48, bool, uint32_t> *>(node)->call(mMaskBit)) return 2;
    return !static_cast<NarrowSlot<25, bool, uint32_t> *>(node)->call(mIID);
}

void nRZSceneGraph::cBranchNodesMaskBitAndIIDFilter::Destruct()
{
    mVTable = GameAddress<void>(kVTable);
}

nRZSceneGraph::cBranchNodesMaskBitAndIIDFilter *nRZSceneGraph::cBranchNodesMaskBitAndIIDFilter::DeletingDestructor(
    uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cBranchNodesMaskBitAndIIDFilter));
    return this;
}

cTSDirectoryCache *cTSDirectoryCache::DeletingDestructor(uint32_t flags)
{
    mVTable = GameAddress<void>(kVTable);
    OriginalFunction<void(__thiscall *)(void *), 0x1f4aa0>()(mEntries);
    using StringMap = std::unordered_map<std::string, std::string>;
    static_assert(sizeof(StringMap) == sizeof(mEntries), "unordered_map layout");
    reinterpret_cast<StringMap *>(mEntries)->~StringMap();
    if (flags & 1) ::operator delete(this, sizeof(cTSDirectoryCache));
    return this;
}

const char *nGZGraphic4::cError::GetMessage() const
{
    switch (mCode) {
    case 0: return "kInternalDriverError";   // the image's 0xd9cd74
    case 1: return "kDirect3DCreateFailure"; // 0xd9cd8c
    case 2: return "kNoAdapters";            // 0xd9cda4
    default: return "UNKNOWN";               // 0xd9ca34
    }
}

// cBranchNodesIIDFilter's own vtable (the image's 0xe7cf54 order: IsIncluded, the deleting destructor), what
// ts2_rva_e7cf54 stands for in an image-free build (standalone/owned_statics.cpp).
namespace {
template <typename T> const void *Member(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance members only");
    const void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}
} // namespace

extern "C" const void *ts2_own_vt_e7cf54[2] = {
    Member(&nRZSceneGraph::cBranchNodesIIDFilter::IsIncluded),         // 0 (0xaeacb0)
    Member(&nRZSceneGraph::cBranchNodesIIDFilter::DeletingDestructor), // 1 (0xaea800)
};

// cError's own vtable (the image's 0xd9cd70 order: GetMessage), what ts2_rva_d9cd70 stands for in an image-free build.
extern "C" const void *ts2_own_vt_d9cd70[1] = {
    Member(&nGZGraphic4::cError::GetMessage), // 0 (0x15a900)
};
