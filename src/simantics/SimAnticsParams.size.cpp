#include "simantics/SimAnticsParams.h"

#include "game/game.h"
#include "game/vtable.h"
#include "rz/RZFixedAllocator.h"

#include <cstring>

namespace {

using StreamPutFn = void(__thiscall *)(void *, uint32_t);
using StreamGetFn = void(__thiscall *)(void *, void *);
using StreamErrorFn = uint8_t(__thiscall *)(void *);
using VersionFn = uint8_t(__thiscall *)(void *);
using ConvertFn = void(__thiscall *)(void *);

// The stream's slot 18 is an error flag read from al; "ok" is al == 0.
bool StreamOk(void *stream)
{
    return VirtualCall<StreamErrorFn, XPrimParam::kStreamErrorSlot>(stream) == 0;
}

void *VTableOf(uint32_t rva)
{
    return GameAddress<void>(rva);
}

RZFixedAllocatorBase *Pool()
{
    return GameAddress<RZFixedAllocatorBase>(XPrimParam::kPoolAllocator);
}

constexpr uint32_t kRunPrivateTreeVTable = 0xdc923c;

} // namespace

// Rows from the byte index table at 0x46b8c4 and the jump table at 0x46b7d0 (each case stores its class's
// vtable; the cases that preset the version byte store it right after).
const XPrimOpcodeEntry kXPrimOpcodeTable[60] = {
    {0x00, 0xdc93f4, 0}, // IdleParam
    {0x01, 0xdc9368, 0}, // GenericSimCallParam
    {0x02, 0xdc941c, 0}, // ExpressionParam
    {0x03, 0xdc92dc, 0}, // FindBestActionParam
    {0x07, 0xdc94a8, 0}, // UpdateParam
    {0x08, 0xdc94bc, 0}, // RandomParam
    {0x0a, 0xdc92b4, 0}, // TutorialParam
    {0x0b, 0xdc946c, 0}, // DistanceToParam
    {0x0c, 0xdc9494, 0}, // DirectionToParam
    {0x0d, 0xdc93e0, 0}, // PushActionParam
    {0x0e, 0xdc94e4, 0}, // FindFunctionalObjectParam
    {0x0f, 0xdc9444, 0}, // TreeBreakParam
    {0x10, 0xdc94f8, 1}, // FindGoodLocationParam
    {0x11, 0xdc9408, 0}, // IdleForInputParam
    {0x12, 0xdc9318, 0}, // KillObjectParam
    {0x13, 0xdc9548, 0}, // MakeNewCharacterParam
    {0x14, 0xdc950c, 0}, // CallFunctionalTreeParam
    {0x16, 0xdc9390, 1}, // TurnTowardsParam
    {0x17, 0xdc93b8, 0}, // PlaySoundParam
    {0x19, 0xdc9304, 0}, // BudgetParam
    {0x1a, 0xdc94d0, 1}, // NewRelationshipParam
    {0x1b, 0xdc9480, 0}, // GotoRelativeParam
    {0x1c, 0xdc932c, 0}, // CallNamedTreeParam
    {0x1d, 0xdc92a0, 0}, // SetMotiveDeltaParam
    {0x1e, 0xdc92c8, 0}, // GosubActionParam
    {0x1f, 0xdc9354, 1}, // SetToNextParam
    {0x20, 0xdc928c, 0}, // TestObjectTypeParam
    {0x21, 0xdc93cc, 0}, // Find5WorstMotivesParam
    {0x22, 0xdc9278, 0}, // UIEffectParam
    {0x23, 0xdc9264, 0}, // UserEventParam
    {0x24, 0xdc937c, 0}, // DialogParam
    {0x25, 0xdc9250, 0}, // TestInteractingWithParam
    {0x2a, 0xdc9458, 0}, // CreateObjectParam
    {0x2d, 0xdc9520, 1}, // GotoRoutingSlotParam
    {0x2e, 0xdc9534, 1}, // SnapParam
    {0x30, 0xdc93a4, 0}, // KillSoundsParam
    {0x31, 0xdc92f0, 0}, // NotifyStackObjectParam
    {0x32, 0xdc9340, 0}, // MakeActionStringParam
    {0x33, 0xdc9624, 0}, // ManageInventoryParam
    {0x69, 0xdc955c, 0}, // AnimateObjParam
    {0x6a, 0xdc9570, 0}, // AnimateSimParam
    {0x6b, 0xdc9584, 1}, // AnimateOverlayParam
    {0x6c, 0xdc9598, 0}, // AnimateStopParam
    {0x6d, 0xdc95ac, 3}, // MaterialChangeParam
    {0x6e, 0xdc95c0, 1}, // LookAtParam
    {0x6f, 0xdc95d4, 0}, // ChangeLightParam
    {0x70, 0xdc95fc, 0}, // EffectParam
    {0x71, 0xdc95e8, 1}, // SnapIntoParam
    {0x72, 0xdc9610, 0}, // LocoParam
    {0x73, 0xdc9638, 0}, // DebugParam
    {0x74, 0xdc964c, 0}, // NewReachParam
    {0x75, 0xdc9660, 0}, // AgeParam
    {0x76, 0xdc9430, 0}, // ArrayOperationParam
    {0x77, 0xdc9674, 0}, // MessageParam
    {0x78, 0xdc9688, 0}, // RayTraceParam
    {0x79, 0xdc969c, 0}, // ChangeOutfitParam
    {0x7a, 0xdc96b0, 0}, // OnTimerParam
    {0x7b, 0xdc96c4, 0}, // CinematicParam
    {0x7c, 0xdc96d8, 0}, // WantSatisfyParam
    {0x7d, 0xdc96ec, 0}, // FollowSimParam
};

// ----- memory ------------------------------------------------------------------------------------------

void *XPrimParam::AllocateBlock(uint32_t)
{
    // The original wrapper pushes the size, loads the pool into ecx and jumps to the pool's Allocate slot.
    RZFixedAllocatorBase *pool = Pool();
    void *block = pool->mCurrent->freeList;
    if (!block) block = pool->TakeFreePage();
    // The pool's current page is read again: TakeFreePage moved it.
    RZFixedPage *page = pool->mCurrent;
    page->freeList = *static_cast<void **>(block);
    ++page->used;
    return block;
}

void XPrimParam::FreeBlock(void *block) noexcept
{
    if (block) reinterpret_cast<RZFastFixed0x18 *>(Pool())->Free(block);
}

// ----- construction ------------------------------------------------------------------------------------

XPrimParam *XPrimParam::Construct()
{
    mVTable = VTableOf(kBaseVTable);
    std::memset(mData, 0, sizeof(mData));
    mVersion = 0;
    return this;
}

XPrimParam *XPrimParam::CopyConstruct(const XPrimParam &o)
{
    mVTable = VTableOf(kBaseVTable);
    mVersion = o.mVersion;
    std::memcpy(mData, o.mData, sizeof(mData));
    return this;
}

XPrimParam *__stdcall XPrimParam::BuildForOpcode(uint32_t opcode)
{
    const int16_t op = static_cast<int16_t>(opcode);
    // Opcodes without a primitive (the default case, also reached by everything above 0x7d) build the
    // plain XPrimParam when below 0x100 (signed, so negatives too) and RunPrivateTreeParam otherwise.
    uint32_t vtable = kRunPrivateTreeVTable;
    uint8_t version = 0;
    bool plain = op < 0x100;
    if (static_cast<uint32_t>(static_cast<int32_t>(op)) <= 0x7d) {
        for (const XPrimOpcodeEntry &e : kXPrimOpcodeTable) {
            if (e.opcode == op) {
                vtable = e.vtable;
                version = e.version;
                plain = false;
                break;
            }
        }
    }
    void *block = AllocateBlock(kSize);
    if (!block) return nullptr;
    XPrimParam *param = static_cast<XPrimParam *>(block)->Construct();
    if (plain) return param;
    param->mVTable = VTableOf(vtable);
    if (version) param->mVersion = version;
    return param;
}

// ----- serialisation -----------------------------------------------------------------------------------

bool XPrimParam::Write(void *stream)
{
    uint8_t version = VirtualCall<VersionFn, kVersionSlot>(this);
    // The original pushes the stack slot that held `this` after overwriting its low byte with the version:
    // the stream's byte slot receives this with the low byte replaced (it only uses the low byte).
    const uint32_t arg = (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this)) & ~0xffu) | version;
    VirtualCall<StreamPutFn, kStreamByteSlot>(stream, arg);
    for (uint32_t off = 4; off <= 0x12; off += 2)
        VirtualCall<StreamPutFn, kStreamWordSlot>(stream, Word(off));
    return StreamOk(stream);
}

bool XPrimParam::Read(void *stream, int32_t version)
{
    if (version >= static_cast<int32_t>(0xffff8005))
        VirtualCall<StreamGetFn, kStreamByteSlot>(stream, &mVersion);
    else
        mVersion = 0;
    for (uint32_t off = 4; off <= 0xa; off += 2)
        VirtualCall<StreamGetFn, kStreamWordSlot>(stream, &Word(off));
    if (version >= static_cast<int32_t>(0xffff8003)) {
        for (uint32_t off = 0xc; off <= 0x12; off += 2)
            VirtualCall<StreamGetFn, kStreamWordSlot>(stream, &Word(off));
    } else {
        Dword(0xc) = 0;
        Dword(0x10) = 0;
    }
    return StreamOk(stream);
}

bool XPrimParam::ReadRunPrivateTree(void *stream, int32_t version)
{
    bool ok = Read(stream, version);
    if (mVersion < 1) {
        Byte(0x10) &= 1;
        mVersion = 1;
    }
    return ok;
}

bool XPrimParam::ReadBudget(void *stream, int32_t version)
{
    bool ok = Read(stream, version);
    if (mVersion < 1) {
        Word(8) &= 0xfff7;
        mVersion = 1;
    }
    return ok;
}

bool XPrimParam::ReadMakeActionString(void *stream, int32_t version)
{
    bool ok = Read(stream, version);
    // Four conversion steps, each run for every version below its own number and falling through to the
    // next; the version byte is only set from step 2 on.
    const uint8_t from = mVersion;
    if (from < 1) {
        Word(6) &= 0xfeff;
    }
    if (from < 2) {
        Word(0x12) = Byte(8);
        Byte(8) = 0;
        mVersion = 2;
    }
    if (from < 3) {
        Word(6) &= 0xefff;
        mVersion = 3;
    }
    if (from < 4) {
        Word(6) &= 0xdfff;
        mVersion = 4;
    }
    return ok;
}

bool XPrimParam::ReadDialog(void *stream, int32_t version)
{
    bool ok = Read(stream, version);
    if (mVersion < 1) {
        uint8_t c = Byte(6);
        Byte(6) = 0;
        Word(0x11) = c;
        mVersion = 1;
    }
    return ok;
}

bool XPrimParam::ReadThenConvert(void *stream, int32_t version)
{
    bool ok = Read(stream, version);
    VirtualCall<ConvertFn, kConvertSlot>(this);
    return ok;
}

// ----- conversions (slot 2) ----------------------------------------------------------------------------
// The classes with a "Version0" variant copy themselves into a temporary of the old layout, then rebuild
// their operand bytes from it. The temporary's vtable is never used, so a plain copy stands in for it.

void XPrimParam::ConvertSetToNext()
{
    if (mVersion < 1) {
        Byte(0xc) &= 0xfc;
        Dword(0xd) = 0;
        mVersion = 1;
    }
}

void XPrimParam::ConvertTurnTowards()
{
    if (mVersion < 1) {
        XPrimParam old;
        old.CopyConstruct(*this);
        Byte(0xe) = old.Byte(0xc);
        Dword(0xa) = 0;
        mVersion = 1;
    }
}

void XPrimParam::ConvertFindGoodLocation()
{
    if (mVersion < 1) {
        Dword(0x10) = 0;
        mVersion = 1;
    }
}

void XPrimParam::ConvertGotoRoutingSlot()
{
    if (mVersion < 1) {
        XPrimParam old;
        old.CopyConstruct(*this);
        Word(0xd) = old.Word(0xb);
        Word(0xf) = old.Word(0xd);
        Byte(0x11) = old.Byte(0xf);
        Dword(9) = 0;
        mVersion = 1;
    }
}

void XPrimParam::ConvertSnap()
{
    if (mVersion < 1) {
        XPrimParam old;
        old.CopyConstruct(*this);
        Word(0xe) = old.Word(0xc);
        Dword(0xa) = 0;
        mVersion = 1;
    }
}

void XPrimParam::ConvertAnimateOverlay()
{
    if (mVersion < 1) {
        XPrimParam old;
        old.CopyConstruct(*this);
        Byte(0xe) = old.Byte(0xe);
        Byte(0xf) = old.Byte(0x10);
        Word(0x10) = 0;
        mVersion = 1;
    }
}

void XPrimParam::ConvertSnapInto()
{
    if (mVersion < 1) {
        XPrimParam old;
        old.CopyConstruct(*this);
        Byte(0xf) = old.Byte(0xd);
        Dword(0xb) = 0;
        mVersion = 1;
    }
}

void XPrimParam::ConvertMaterialChange()
{
    const uint8_t from = mVersion;
    if (from < 1) {
        Byte(0x11) &= 0xfb;
        mVersion = 1;
    }
    if (from < 2) {
        Byte(0x11) &= 0xf7;
        mVersion = 2;
    }
    if (from < 3) {
        Byte(0x11) &= 0xef;
        mVersion = 3;
    }
}

void XPrimParam::ConvertLookAt()
{
    if (mVersion < 1) {
        XPrimParam old;
        old.CopyConstruct(*this);
        const int32_t half = static_cast<int16_t>(old.Word(8)) / 2;
        Byte(8) = static_cast<uint8_t>(half);
        Byte(9) = static_cast<uint8_t>(half);
        if (Byte(4) & 8) Byte(0x13) |= 2;
        mVersion = 1;
    }
}

void XPrimParam::ConvertManageInventory()
{
    const uint8_t from = mVersion;
    if (from < 1) {
        // Regroups the bits of byte +4: bits 0-1 stay; bit 2 moves to bit 3, bits 3-5 to bits 4-6 ... as the
        // original's byte arithmetic does (all steps are byte-wide).
        const uint8_t v = Byte(4);
        uint8_t low = v & 3;
        uint8_t d = static_cast<uint8_t>(((v >> 3) & 7) | static_cast<uint8_t>((v >> 7) << 3));
        d = static_cast<uint8_t>(d + d);
        d |= static_cast<uint8_t>((v >> 2) & 1);
        d = static_cast<uint8_t>(d << 3);
        d |= low;
        Byte(4) = d;
        mVersion = 1;
    } else if (from >= 2) {
        return;
    }
    if (!(Byte(4) & 8)) Byte(0xd) = 0xc;
    mVersion = 2;
}

void XPrimParam::ConvertNewRelationship()
{
    if (mVersion >= 1) return;
    XPrimParam old;
    old.CopyConstruct(*this);
    // Flags: bit 2 of byte +5 copies old +6 bit 2, bit 1 copies old +6 bit 1.
    if (old.Byte(6) & 4) Byte(5) |= 4;
    else Byte(5) &= 0xfb;
    if (old.Byte(6) & 2) Byte(5) |= 2;
    else Byte(5) &= 0xfd;
    Byte(4) = old.Byte(4);
    switch (old.Byte(5)) {
    case 0:
        if (Byte(5) & 2) {
            Byte(6) = 0x12;
            Word(7) = 0x1f;
        } else {
            Byte(6) = 3;
            Word(7) = 0xb;
        }
        Byte(9) = 0xa;
        Word(0xa) = 0;
        break;
    case 1:
        Byte(6) = 0xa;
        Word(7) = 0;
        if (Byte(5) & 2) {
            Byte(9) = 0x12;
            Word(0xa) = 0x1f;
        } else {
            Byte(9) = 3;
            Word(0xa) = 0xb;
        }
        break;
    case 2:
        Byte(6) = 0xa;
        Word(7) = 0;
        Byte(9) = 0x19;
        Word(0xa) = old.Byte(7);
        break;
    case 3:
        Byte(6) = 0x19;
        Word(7) = old.Byte(7);
        Byte(9) = 0xa;
        Word(0xa) = 0;
        break;
    default:
        Byte(6) = 3;
        Word(7) = 0xb;
        Byte(9) = 0xa;
        Word(0xa) = 0;
        Byte(5) &= 0xfd;
        break;
    }
    if (old.Byte(6) & 1) Byte(5) |= 1;
    else Byte(5) &= 0xfe;
    Byte(0xc) = old.Byte(8);
    Word(0xd) = old.Word(0xa);
    mVersion = 1;
}
