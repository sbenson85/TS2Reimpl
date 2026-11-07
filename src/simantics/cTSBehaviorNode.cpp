#include "simantics/cTSBehaviorNode.h"

#include "game/game.h"
#include "game/vtable.h"
#include "rz/RZFastFixedAllocators.h"

namespace {

using NoArgU32Fn = uint32_t(__thiscall *)(void *);
using GetFlagFn = uint8_t(__thiscall *)(void *);
using ArgFn = void(__thiscall *)(void *, uint32_t);
using DeleteFn = void *(__thiscall *)(void *, uint32_t);
using AddRefFn = uint32_t(__thiscall *)(void *);
using StreamReadFn = void(__thiscall *)(void *, void *);
using StreamWriteFn = void(__thiscall *)(void *, uint32_t);
using StreamErrorFn = uint8_t(__thiscall *)(void *);
using ParamReadFn = void(__thiscall *)(void *, void *, int32_t);
using ParamWriteFn = void(__thiscall *)(void *, void *);
using ConvertFn = void(__thiscall *)(void *);

constexpr int kAddRefSlot = 1;
constexpr int kDeleteSlot = 25;
constexpr int kOperandCountSlot = 12;
constexpr int kGetFlagSlot = 18;
constexpr int kSetFlagSlot = 19;
constexpr int kStreamByteSlot = 5;
constexpr int kStreamWord6Slot = 6;
constexpr int kStreamWord7Slot = 7;
constexpr int kStreamErrorSlot = 18;
constexpr int kParamReadSlot = 1;
constexpr int kParamWriteSlot = 0;
constexpr int kParamConvertSlot = 2;

bool StreamOk(void *stream)
{
    return VirtualCall<StreamErrorFn, kStreamErrorSlot>(stream) == 0;
}

// The old files stored a jump target in one byte: the top three values are the special targets, kept
// sign-extended in the 16-bit field (0xfd -> 0xfffc, 0xfe -> 0xfffd, 0xff -> 0xfffe).
uint16_t WidenTarget(uint8_t b)
{
    if (b == 0xfe) return 0xfffd;
    if (b == 0xff) return 0xfffe;
    if (b == 0xfd) return 0xfffc;
    return b;
}

} // namespace

bool cTSBehaviorNode::QueryInterface(uint32_t iid, void **out)
{
    if (iid != kIidUnknown && iid != kIidBehaviorNode) return false;
    *out = this;
    VirtualCall<AddRefFn, kAddRefSlot>(this);
    return true;
}

uint32_t cTSBehaviorNode::Release()
{
    if (mRefCount <= 1) {
        VirtualCall<DeleteFn, kDeleteSlot>(this, 1);
        return 0;
    }
    --mRefCount;
    return mRefCount;
}

uint32_t cTSBehaviorNode::GetOpcode()
{
    return mOpcode & 0x7fff;
}

uint16_t *cTSBehaviorNode::GetOpcodeWord()
{
    return &mOpcode;
}

void cTSBehaviorNode::SetOpcode(uint32_t opcode)
{
    uint8_t flag = VirtualCall<GetFlagFn, kGetFlagSlot>(this);
    mOpcode = static_cast<uint16_t>(opcode);
    if (mParam) XPrimParam::FreeBlock(mParam);
    mParam = XPrimParam::BuildForOpcode(mOpcode);
    // The flag goes back through slot 19 as the stack slot that held `this` with its low byte replaced
    // (slot 19 only looks at the low byte).
    const uint32_t arg = (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this)) & ~0xffu) | flag;
    VirtualCall<ArgFn, kSetFlagSlot>(this, arg);
}

uint32_t cTSBehaviorNode::GetTrueTarget()
{
    return mTrue;
}

uint32_t cTSBehaviorNode::GetFalseTarget()
{
    return mFalse;
}

uint16_t *cTSBehaviorNode::GetFalseTargetPtr()
{
    return &mFalse;
}

void cTSBehaviorNode::SetTrueTarget(uint32_t v)
{
    mTrue = static_cast<uint16_t>(v);
}

void cTSBehaviorNode::SetFalseTarget(uint32_t v)
{
    mFalse = static_cast<uint16_t>(v);
}

uint32_t cTSBehaviorNode::GetOperand(int32_t index)
{
    if (index < 0) return 0;
    if (index >= static_cast<int32_t>(VirtualCall<NoArgU32Fn, kOperandCountSlot>(this))) return 0;
    if (static_cast<uint32_t>(index) > 7) return 0;
    return mParam->Word(4 + 2 * static_cast<uint32_t>(index));
}

void cTSBehaviorNode::SetOperand(int32_t index, uint32_t v)
{
    if (index < 0) return;
    if (index >= static_cast<int32_t>(VirtualCall<NoArgU32Fn, kOperandCountSlot>(this))) return;
    if (static_cast<uint32_t>(index) > 7) return;
    mParam->Word(4 + 2 * static_cast<uint32_t>(index)) = static_cast<uint16_t>(v);
}

XPrimParam *cTSBehaviorNode::CopyParamTo(XPrimParam *dest)
{
    dest->CopyConstruct(*mParam);
    return dest;
}

void cTSBehaviorNode::CopyParamFrom(const XPrimParam *src)
{
    // A forward byte loop, as the original: it matters if the two overlap.
    const uint8_t *from = reinterpret_cast<const uint8_t *>(src) + 4;
    uint8_t *to = reinterpret_cast<uint8_t *>(mParam) + 4;
    for (int i = 0; i < 0x10; ++i) to[i] = from[i];
    mParam->mVersion = src->mVersion;
}

bool cTSBehaviorNode::GetFlag()
{
    return (mOpcode >> 15) & 1;
}

void cTSBehaviorNode::SetFlag(uint32_t value)
{
    if (static_cast<uint8_t>(value) != 0) mOpcode |= 0x8000;
    else mOpcode &= 0x7fff;
}

bool cTSBehaviorNode::Write(void *stream)
{
    VirtualCall<StreamWriteFn, kStreamWord6Slot>(stream, mOpcode);
    VirtualCall<StreamWriteFn, kStreamWord7Slot>(stream, mTrue);
    VirtualCall<StreamWriteFn, kStreamWord7Slot>(stream, mFalse);
    VirtualCall<ParamWriteFn, kParamWriteSlot>(mParam, stream);
    return StreamOk(stream);
}

bool cTSBehaviorNode::Read(void *stream, int32_t version)
{
    auto readWord6 = [&](void *dst) { VirtualCall<StreamReadFn, kStreamWord6Slot>(stream, dst); };
    auto readWord7 = [&](void *dst) { VirtualCall<StreamReadFn, kStreamWord7Slot>(stream, dst); };
    auto readByte = [&](void *dst) { VirtualCall<StreamReadFn, kStreamByteSlot>(stream, dst); };
    auto replaceParam = [&]() {
        if (mParam) XPrimParam::FreeBlock(mParam);
        mParam = XPrimParam::BuildForOpcode(mOpcode);
    };

    if (version == 1 || version == static_cast<int32_t>(0xffff8000) || version == static_cast<int32_t>(0xffff8001) ||
        version == static_cast<int32_t>(0xffff8002)) {
        // Oldest layouts: targets are single bytes, the operands are four 16-bit values, read into a
        // fresh object that is then converted (slot 2) from its version-0 form.
        // The byte is read into the stack slot that held the stream argument (so a read that writes
        // nothing leaves the low byte of the stream pointer): the same slot serves both targets.
        uint8_t byteSlot = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(stream));
        readWord6(&mOpcode);
        readByte(&byteSlot);
        mTrue = WidenTarget(byteSlot);
        readByte(&byteSlot);
        mFalse = WidenTarget(byteSlot);
        replaceParam();
        mParam->mVersion = 0;
        for (uint32_t off = 4; off <= 0xa; off += 2) readWord6(&mParam->Word(off));
        mParam->Word(0xc) = 0;
        mParam->Word(0xe) = 0;
        mParam->Word(0x10) = 0;
        mParam->Word(0x12) = 0;
        VirtualCall<ConvertFn, kParamConvertSlot>(mParam);
        return StreamOk(stream);
    }
    if (version >= static_cast<int32_t>(0xffff8007)) {
        readWord6(&mOpcode);
        readWord7(&mTrue);
        readWord7(&mFalse);
    } else if (version >= static_cast<int32_t>(0xffff8003)) {
        uint8_t byteSlot = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(stream));
        readWord6(&mOpcode);
        readByte(&byteSlot);
        mTrue = WidenTarget(byteSlot);
        readByte(&byteSlot);
        mFalse = WidenTarget(byteSlot);
    } else {
        return false;
    }
    replaceParam();
    VirtualCall<ParamReadFn, kParamReadSlot>(mParam, stream, version);
    return StreamOk(stream);
}

uint32_t cTSBehaviorNode::GetClassId()
{
    return 0xeb54985f;
}

cTSBehaviorNode *cTSBehaviorNode::DeletingDestructor(uint32_t flags)
{
    mVTable = GameAddress<void>(kVTable);
    XPrimParam::FreeBlock(mParam);
    if (flags & 1) {
        // Bit 2 marks an object that isn't pool memory (the original calls a do-nothing function).
        if (!(flags & 4)) reinterpret_cast<RZFastFixed0x10 *>(GameAddress<void>(kPoolAllocator))->Free(this);
    }
    return this;
}
