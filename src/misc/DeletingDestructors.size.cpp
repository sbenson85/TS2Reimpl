#include "misc/DeletingDestructors.h"

#include <new>

#include "game/game.h"
#include "game/original.h"

// Size-built deleting destructors (src/misc/DeletingDestructors.h).

HTTPMessage *HTTPMessage::DeletingDestructor(uint32_t flags)
{
    OriginalFunction<void(__thiscall *)(HTTPMessage *), 0x98d879>()(this);
    if (flags & 1) ::operator delete(this, sizeof(HTTPMessage));
    return this;
}

TickerMessage *TickerMessage::DeletingDestructor(uint32_t flags)
{
    OriginalFunction<void(__thiscall *)(TickerMessage *), 0x9600ac>()(this);
    if (flags & 1) ::operator delete(this, sizeof(TickerMessage));
    return this;
}

void cRZProcessMemoryInfo::Destruct()
{
    mVTable = GameAddress<void>(kBaseVTable);
}

cRZProcessMemoryInfo *cRZProcessMemoryInfo::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cRZProcessMemoryInfo));
    return this;
}

void cTSRampGenerator::Destruct()
{
    mVTable = GameAddress<void>(kBaseVTable);
}

cTSRampGenerator *cTSRampGenerator::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cTSRampGenerator));
    return this;
}

cTSUIDaysOfWeekCtrl *cTSUIDaysOfWeekCtrl::DeletingDestructor(uint32_t flags)
{
    OriginalFunction<void(__thiscall *)(cTSUIDaysOfWeekCtrl *), 0x77f370>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cTSUIDaysOfWeekCtrl));
    return this;
}

cTSUILearnedBehaviorsListCtrl *cTSUILearnedBehaviorsListCtrl::DeletingDestructor(uint32_t flags)
{
    OriginalFunction<void(__thiscall *)(cTSUILearnedBehaviorsListCtrl *), 0x780021>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cTSUILearnedBehaviorsListCtrl));
    return this;
}

cTSUISimViewCtrl *cTSUISimViewCtrl::DeletingDestructor(uint32_t flags)
{
    OriginalFunction<void(__thiscall *)(cTSUISimViewCtrl *), 0x7c3f2c>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cTSUISimViewCtrl));
    return this;
}

cTSUISimpleThumbnail *cTSUISimpleThumbnail::DeletingDestructor(uint32_t flags)
{
    OriginalFunction<void(__thiscall *)(cTSUISimpleThumbnail *), 0x7c5ca4>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cTSUISimpleThumbnail));
    return this;
}

cTSUISkillsCtrl *cTSUISkillsCtrl::DeletingDestructor(uint32_t flags)
{
    OriginalFunction<void(__thiscall *)(cTSUISkillsCtrl *), 0x7c3514>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cTSUISkillsCtrl));
    return this;
}

cTSUITraitsCtrl *cTSUITraitsCtrl::DeletingDestructor(uint32_t flags)
{
    OriginalFunction<void(__thiscall *)(cTSUITraitsCtrl *), 0x7c6a8a>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cTSUITraitsCtrl));
    return this;
}

cTSUIMultiButton *cTSUIMultiButton::DeletingDestructor(uint32_t flags)
{
    OriginalFunction<void(__thiscall *)(cTSUIMultiButton *), 0x7dc59a>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cTSUIMultiButton));
    return this;
}

cITSUIModeObject *cITSUIModeObject::DeletingDestructor(uint32_t flags)
{
    mVTable = GameAddress<void>(kVTable);
    if (flags & 1) ::operator delete(this, sizeof(cITSUIModeObject));
    return this;
}

cTSUIModeAlbum *cTSUIModeAlbum::DeletingDestructor(uint32_t flags) // 0x61aaa8
{
    OriginalFunction<void(__thiscall *)(cTSUIModeAlbum *), 0x61a27c>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cTSUIModeAlbum));
    return this;
}

cTSUIModeBuild *cTSUIModeBuild::DeletingDestructor(uint32_t flags) // 0x61aac7
{
    OriginalFunction<void(__thiscall *)(cTSUIModeBuild *), 0x61a29d>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cTSUIModeBuild));
    return this;
}

cTSUIModeBuy *cTSUIModeBuy::DeletingDestructor(uint32_t flags) // 0x61aae6
{
    OriginalFunction<void(__thiscall *)(cTSUIModeBuy *), 0x61a2cb>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cTSUIModeBuy));
    return this;
}

cTSUIModeCAS *cTSUIModeCAS::DeletingDestructor(uint32_t flags) // 0x61ab05
{
    OriginalFunction<void(__thiscall *)(cTSUIModeCAS *), 0x61a2f9>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cTSUIModeCAS));
    return this;
}

cTSUIModeCinematic *cTSUIModeCinematic::DeletingDestructor(uint32_t flags) // 0x61ab24
{
    OriginalFunction<void(__thiscall *)(cTSUIModeCinematic *), 0x61a3d9>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cTSUIModeCinematic));
    return this;
}

cTSUIModeLive *cTSUIModeLive::DeletingDestructor(uint32_t flags) // 0x61ab43
{
    OriginalFunction<void(__thiscall *)(cTSUIModeLive *), 0x61a67f>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cTSUIModeLive));
    return this;
}

cTSUIModeNeighborhood *cTSUIModeNeighborhood::DeletingDestructor(uint32_t flags) // 0x61ab62
{
    OriginalFunction<void(__thiscall *)(cTSUIModeNeighborhood *), 0x61a796>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cTSUIModeNeighborhood));
    return this;
}

cTSUIModeNone *cTSUIModeNone::DeletingDestructor(uint32_t flags) // 0x61ab81
{
    OriginalFunction<void(__thiscall *)(cTSUIModeNone *), 0x61a817>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cTSUIModeNone));
    return this;
}

cTSUIModeOptions *cTSUIModeOptions::DeletingDestructor(uint32_t flags) // 0x61aba0
{
    OriginalFunction<void(__thiscall *)(cTSUIModeOptions *), 0x61a919>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cTSUIModeOptions));
    return this;
}

cTSUIModePainting *cTSUIModePainting::DeletingDestructor(uint32_t flags) // 0x61abbf
{
    OriginalFunction<void(__thiscall *)(cTSUIModePainting *), 0x61a962>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cTSUIModePainting));
    return this;
}

cTSUIModeReward *cTSUIModeReward::DeletingDestructor(uint32_t flags) // 0x61abde
{
    OriginalFunction<void(__thiscall *)(cTSUIModeReward *), 0x61a9f2>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cTSUIModeReward));
    return this;
}
