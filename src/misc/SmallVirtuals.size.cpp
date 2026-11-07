#include "misc/SmallVirtuals.h"

#include <new>

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"

// Size-built small slots (src/misc/SmallVirtuals.h).

namespace {

// Slot 31 of the game's string (cIGZString): a printf-style format into the string (cdecl, varargs).
struct FormatSlot : vtable_detail::Pad<31> {
    virtual uint32_t __cdecl Format(const char *format, ...) = 0;
};

} // namespace

bool cGameTipFilter::IsIncluded(void *tip) const
{
    if (!tip) return false;
    uint32_t category = VirtualCall<uint32_t(__thiscall *)(void *), 4>(tip);
    return (mCategoryMask & (1u << category)) != 0;
}

bool cTSWinProcUITool::cSearchFilter::IsIncluded(void *object)
{
    if (!object || VirtualCall<int32_t(__thiscall *)(void *), 78>(object) != mType) return false;
    ++mCount;
    if (mIndex >= 0 && mIndex + 1 != mCount) return false;
    return true;
}

bool cTSAnchorFilter::IsIncluded(void *object) const
{
    return object && VirtualCall<int32_t(__thiscall *)(void *), 78>(object) == mType &&
           VirtualCall<bool(__thiscall *)(void *), 87>(object);
}

cTSUserToolTerrainBrush *cTSUserToolTerrainBrush::DeletingDestructor(uint32_t flags)
{
    OriginalFunction<void(__thiscall *)(cTSUserToolTerrainBrush *), 0x79bcb0>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cTSUserToolTerrainBrush));
    return this;
}

void cTSUserToolTerrainBrush::SetPosition(const Point *point)
{
    mPosition.x = point->x;
    mPosition.y = point->y;
    mLastPosition = mPosition;
}

void cRZSocketError::SetError(int32_t systemCode)
{
    int32_t code = OriginalFunction<int32_t(__cdecl *)(int32_t), 0x999e59>()(systemCode);
    mSystemCode = systemCode;
    mCode = code;
}

uint32_t cRZSocketError::Describe(void *text)
{
    const char *message = OriginalFunction<const char *(__thiscall *)(cRZSocketError *), 0x99a0d0>()(this);
    return static_cast<FormatSlot *>(text)->Format(GameAddress<const char>(0xe5e748), mSystemCode, message);
}

cTSUISimCtrl *cTSUISimCtrl::DeletingDestructor(uint32_t flags)
{
    OriginalFunction<void(__thiscall *)(cTSUISimCtrl *), 0x77f779>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cTSUISimCtrl));
    return this;
}

void cTSUISimCtrl::SetSim(void *sim, bool refresh)
{
    mSim.Assign(sim);
    if (refresh) VirtualCall<void(__thiscall *)(void *), 3>(this);
}
