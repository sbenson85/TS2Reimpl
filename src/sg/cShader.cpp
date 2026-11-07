// nGZSceneGraph::cShader, part 1 (lane RT). See the header.
#include "sg/cShader.h"

#include "game/game.h"
#include "game/owned.h"
#include "rz/RZFastFixedAllocators.h"
#include "rz/RZFixedAllocator.h"
#include "simantics/cTSBehaviorTree.h"
#include "game/vtable.h"
#include "gz/GraphicServices.h"
#include "rz/cRZStd.h"
#include "rz/cRZString.h"
#include "misc/UtilitiesE.h"
#include "sg/cStringPool.h"
#include "sg/SGVectorBlocks.h"
#include "sg/cTextureManager.h"

#include <corecrt.h>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>
#include <xutility>

extern "C" int __cdecl _purecall();

namespace nGZSceneGraph {

namespace {

// cRZStd's report slot (vtable 0xda3e6c slot 2), as MiscHelpersA.cpp and cDeviceStateDXValidate.cpp view it.
struct ReportView {
    virtual void __cdecl Slot0(const char *, ...) = 0;
    virtual void Slot1() = 0;
    virtual void __cdecl Report(const char *group, uint32_t level, const char *format, ...) = 0;
};

constexpr const char kShaderFile[] = "C:\\dev\\sims2remaster\\DL\\GZFrame\\SceneDB\\GZSceneGraph\\source\\GZSceneGraphShader.cpp";

void SayShader(uint32_t line, const char *format, uint32_t pass)
{
    cRZStd raw;
    cRZStd *log = raw.Construct(8, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(kShaderFile)), line);
    reinterpret_cast<ReportView *>(log)->Report("Shader validation", 4u, format, pass);
}

void SayShader(uint32_t line, const char *format)
{
    cRZStd raw;
    cRZStd *log = raw.Construct(8, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(kShaderFile)), line);
    reinterpret_cast<ReportView *>(log)->Report("Shader validation", 4u, format);
}

// The vector's allocate, as RefPtrVector's (blocks of 0x1000 bytes and more padded and 32-byte aligned, the real pointer in
// the dword before the block; ShaderPassVec::Destroy and RefPtrVector::Destroy undo it).
cIShaderPart **AllocatePartBlock(uint32_t bytes)
{
    if (bytes < 0x1000) return static_cast<cIShaderPart **>(::operator new(bytes));
    if (bytes + 0x23 <= bytes) std::_Xbad_alloc();
    void *const raw = ::operator new(bytes + 0x23);
    if (!raw) _invalid_parameter_noinfo_noreturn(); // operator new does not return null; the library's check stays
    void **const block = reinterpret_cast<void **>((reinterpret_cast<uintptr_t>(raw) + 0x23) & ~static_cast<uintptr_t>(0x1f));
    block[-1] = raw;
    return reinterpret_cast<cIShaderPart **>(block);
}

// Nibble n of a key (the keys are arrays of dwords of eight 4-bit fields).
uint32_t Nibble(const ShaderKey *k, uint32_t n) { return (k->w[n >> 3] >> ((n & 7) * 4)) & 0xf; }

// Release a counted part and clear the member first (the original stores the null before the call).
void ReleaseMember(cIShaderPart *&member)
{
    cIShaderPart *part = member;
    if (part) {
        member = nullptr;
        part->Release();
    }
}

void ResetAll(const ShaderPartVec &vec)
{
    for (cIShaderPart **p = vec.mBegin; p != vec.mEnd; ++p) (*p)->Reset();
}

} // namespace

// 0xaa3d20
bool ShaderPartVecDiffer(const ShaderPartVec *a, const ShaderPartVec *b)
{
    const size_t sizeA = reinterpret_cast<const char *>(a->mEnd) - reinterpret_cast<const char *>(a->mBegin);
    const size_t sizeB = reinterpret_cast<const char *>(b->mEnd) - reinterpret_cast<const char *>(b->mBegin);
    if ((sizeA ^ sizeB) & ~size_t(3)) return true;
    for (cIShaderPart *const *p = a->mBegin, *const *q = b->mBegin; p != a->mEnd; ++p, ++q)
        if (*p != *q) return true;
    return false;
}

// 0x98bf40: Thomas Wang's 32-bit integer mix.
uint32_t ShaderHashMix(uint32_t x)
{
    x = x + ~(x << 15);
    x = x ^ (x >> 10);
    x = x * 9;
    x = x ^ (x >> 6);
    x = x + ~(x << 11);
    return x ^ (x >> 16);
}

// 0x98c220: a 64-bit mix of (high:low) whose low dword is the result. The first step adds the all-ones low dword and the
// complement of the low dword as the high one (as the image's code does).
uint32_t ShaderHashCombine(uint32_t low, uint32_t high)
{
    uint64_t x = (uint64_t(high) << 32 | low) + (uint64_t(~low) << 32 | 0xffffffffu);
    x ^= x >> 22;
    x += ~(x << 13);
    x ^= x >> 8;
    x += x << 3;
    x ^= x >> 15;
    x += ~(x << 27);
    return uint32_t(x ^ (x >> 31));
}

// 0xaa4980
ShaderPartVec *ShaderPartVec::CopyConstruct(const ShaderPartVec *from)
{
    mBegin = mEnd = mCap = nullptr;
    const size_t count = size_t(from->mEnd - from->mBegin);
    if (count == 0) return this;
    // (A count above 0x3fffffff is a length error in the original; no valid vector has one.)
    const uint32_t bytes = uint32_t(count * sizeof(cIShaderPart *));
    cIShaderPart **const block = AllocatePartBlock(bytes);
    mBegin = block;
    mEnd = block;
    mCap = block + count;
    cIShaderPart **to = block;
    for (cIShaderPart *const *p = from->mBegin; p != from->mEnd; ++p, ++to) {
        *to = *p;
        if (*to) (*to)->AddRef();
    }
    mEnd = to;
    return this;
}

// 0xaa4ae0
ShaderPass *ShaderPass::CopyConstruct(const ShaderPass *from)
{
    const auto counted = [](cIShaderPart *&to, cIShaderPart *part) {
        to = part;
        if (part) part->AddRef();
    };
    mApply = from->mApply;
    mB4 = from->mB4;
    mB5 = from->mB5;
    mB6 = from->mB6;
    mB7 = from->mB7;
    counted(mP8, from->mP8);
    counted(mPc, from->mPc);
    counted(mP10, from->mP10);
    mV14.CopyConstruct(&from->mV14);
    counted(mP20, from->mP20);
    mV24.CopyConstruct(&from->mV24);
    mV30.CopyConstruct(&from->mV30);
    mB3c = from->mB3c; // the byte after (+0x3f) is not copied
    mB3d = from->mB3d;
    mB3e = from->mB3e;
    m40 = from->m40;
    m44 = from->m44;
    counted(mP48, from->mP48);
    counted(mP4c, from->mP4c);
    counted(mP50, from->mP50);
    counted(mP54, from->mP54);
    mV58.CopyConstruct(&from->mV58);
    for (int i = 0; i < 16; ++i) cTextureHandle::At(&mHandles[i])->CopyFrom(cTextureHandle::At(&from->mHandles[i]));
    return this;
}

// 0xaa6b60: let go of everything the pass holds, keeping the pass itself.
void ShaderPass::Reset()
{
    if (mP8) mP8->Reset(); // not cleared
    ReleaseMember(mPc);
    ReleaseMember(mP10);
    ResetAll(mV14);
    ReleaseMember(mP20);
    ResetAll(mV24);
    ReleaseMember(mP48);
    ReleaseMember(mP4c);
    ReleaseMember(mP50);
    ReleaseMember(mP54);
    ResetAll(mV58);
    for (uint32_t &handle : mHandles) {
        if (handle == 0xffffffffu) continue; // cInvalidDwordTag::IsInvalid (0x18c080)
        void *texture = cTextureHandle::At(&handle)->Get();
        static_cast<cIShaderPart *>(texture)->Slot4();
    }
}

// 0xaa6510
bool ShaderPass::Equals(const ShaderPass *o) const
{
    if (mB4 != o->mB4 || mB5 != o->mB5 || mB6 != o->mB6 || mB7 != o->mB7) return false;
    if (mB3c != o->mB3c || mB3d != o->mB3d || mB3e != o->mB3e) return false;
    if (mP8 != o->mP8 || mPc != o->mPc || mP10 != o->mP10) return false;
    if (ShaderPartVecDiffer(&mV14, &o->mV14)) return false;
    if (mP20 != o->mP20) return false;
    if (ShaderPartVecDiffer(&mV24, &o->mV24)) return false;
    if (ShaderPartVecDiffer(&mV30, &o->mV30)) return false;
    return m40 == o->m40;
}

// 0xaa6e50
void ShaderPass::EndState(void *ctx)
{
    for (cIShaderPart **p = mV30.mBegin; p != mV30.mEnd; ++p) (*p)->EndState(ctx);
}

// 0xaa4ea0
ShaderPass *ShaderPass::Destruct(uint32_t flags)
{
    for (int i = 15; i >= 0; --i) cTextureHandle::At(&mHandles[i])->Destroy();
    reinterpret_cast<RefPtrVector *>(&mV58)->Destroy();
    for (cIShaderPart *part : {mP54, mP50, mP4c, mP48})
        if (part) part->Release();
    reinterpret_cast<RefPtrVector *>(&mV30)->Destroy();
    reinterpret_cast<RefPtrVector *>(&mV24)->Destroy();
    if (mP20) mP20->Release();
    reinterpret_cast<RefPtrVector *>(&mV14)->Destroy();
    for (cIShaderPart *part : {mP10, mPc, mP8})
        if (part) part->Release();
    if (flags & 1) ::operator delete(this, sizeof(ShaderPass));
    return this;
}

namespace {

constexpr uint32_t kMaxPasses = 0xffffffffu / sizeof(ShaderPass); // 0x18f9c18

} // namespace

// 0xaa4ce0
void cShader::PassVec::Destroy()
{
    ShaderPass *first = mBegin;
    if (!first) return;
    for (ShaderPass *p = first; p != mEnd; ++p) p->Destruct(0);
    vectorblocks::Free(first, mCap);
    mBegin = nullptr;
    mEnd = nullptr;
    mCap = nullptr;
}

// 0xaa47f0
ShaderPass *cShader::PassVec::FillDefault(ShaderPass *first, uint32_t count, PassVec *)
{
    for (; count != 0; --count, ++first) {
        first->mApply = nullptr;
        first->mB4 = 1;
        first->mB5 = first->mB6 = first->mB7 = 0;
        first->mP8 = first->mPc = first->mP10 = nullptr;
        first->mV14 = ShaderPartVec{};
        first->mP20 = nullptr;
        first->mV24 = ShaderPartVec{};
        first->mV30 = ShaderPartVec{};
        first->mB3c = first->mB3d = first->mB3e = 0; // the pad byte +0x3f is left as it was
        first->m40 = first->m44 = 0;
        first->mP48 = first->mP4c = first->mP50 = first->mP54 = nullptr;
        first->mV58 = ShaderPartVec{};
        for (uint32_t &handle : first->mHandles) handle = cTextureHandle::kNone;
    }
    return first;
}

// 0xaa45e0
void cShader::PassVec::Reallocate(uint32_t size, void *)
{
    if (size > kMaxPasses) std::_Xlength_error("vector too long");
    const uint32_t oldSize = uint32_t(int32_t(reinterpret_cast<intptr_t>(mEnd) - reinterpret_cast<intptr_t>(mBegin)) / int32_t(sizeof(ShaderPass)));
    const uint32_t oldCap = uint32_t(int32_t(reinterpret_cast<intptr_t>(mCap) - reinterpret_cast<intptr_t>(mBegin)) / int32_t(sizeof(ShaderPass)));
    uint32_t newCap;
    if (oldCap > kMaxPasses - (oldCap >> 1)) {
        newCap = kMaxPasses;
    } else {
        newCap = oldCap + (oldCap >> 1);
        if (newCap < size) newCap = size;
    }
    const uint32_t bytes = newCap * uint32_t(sizeof(ShaderPass));
    ShaderPass *const block = vectorblocks::Allocate<ShaderPass>(bytes);
    FillDefault(block + oldSize, size - oldSize, this);
    ShaderPass *to = block;
    for (ShaderPass *p = mBegin; p != mEnd; ++p, ++to) to->CopyConstruct(p);
    if (mBegin) {
        for (ShaderPass *p = mBegin; p != mEnd; ++p) p->Destruct(0);
        vectorblocks::Free(mBegin, mCap);
    }
    mBegin = block;
    mEnd = block + size;
    mCap = reinterpret_cast<ShaderPass *>(reinterpret_cast<uint8_t *>(block) + bytes);
}

TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x98>, 10f6654); // cShader's pool (PoolNew / PoolDelete)

// 0xaa4d90
void *cShader::PoolNew(size_t)
{
    return reinterpret_cast<RZFixedAllocatorBase *>(&ts2_rva_10f6654)->Alloc();
}

// 0xaa4dc0
void cShader::PoolDelete(void *block)
{
    if (block) reinterpret_cast<RZFastFixed0x98 *>(&ts2_rva_10f6654)->Free(block);
}

// Slot 12, 0xaa65e0. The text pieces are the game's strings.
void cShader::DumpBody(void *stream, uint32_t indent)
{
    const std::string pad(indent, ' ');
    const auto text = [&](const char *s) { RawSlot<14, void>(stream, s); };
    text(pad.c_str());
    text("Layer:");
    RawSlot<8, void>(stream, mInfo.m84);
    text(" Passes:");
    RawSlot<8, void>(stream, mInfo.mCount);
    text("\n");
    cRZString local;
    local.Construct();
    for (uint32_t index = 0; index < mInfo.mCount; ++index) {
        const ShaderPass &pass = mInfo.mFirstPass[index];
        // The three program handles (held in the pass's part pointers' places) and what each is called.
        const struct {
            uint32_t handle;
            const char *label;
        } blocks[3] = {{uint32_t(reinterpret_cast<uintptr_t>(pass.mPc)), " preprocess vertex program disassembly:\n"},
                       {uint32_t(reinterpret_cast<uintptr_t>(pass.mP50)), " vertex program disassembly:\n"},
                       {uint32_t(reinterpret_cast<uintptr_t>(pass.mP54)), " pixel program disassembly:\n"}};
        for (const auto &block : blocks) {
            if (!block.handle) continue;
            text(pad.c_str());
            text("   Pass ");
            RawSlot<9, void>(stream, index);
            text(block.label);
            void *manager = nGZGraphic4::GetShaderProgramsManagerCached();
            RawSlot<11, void>(manager, &local, block.handle);
            RawSlot<15, void>(stream, &local);
        }
    }
    local.Destruct();
}

// 0xaa4a40. The base constructor (0xaa4960: its table, a zero count, a clear enabled byte) is shared and not hooked; its
// writes are made here, the three padding bytes after the enabled byte and those after +0x90 are left alone.
cShader *cShader::Construct()
{
    mRefCount = 0;
    mEnabled = 0;
    *reinterpret_cast<const void *const **>(this) = kOwnVTable;
    std::memset(&mPasses, 0, sizeof(mPasses));
    std::memset(mRanges, 0xff, sizeof(mRanges));
    mInfo.mFirstPass = nullptr;
    mInfo.m84 = 0;
    mInfo.mCount = 0;
    std::memset(mInfo.mFlags, 0, sizeof(mInfo.mFlags));
    mInfo.m90 = 0;
    mHash = 0;
    return this;
}

// 0xaa4fa0
cShader *cShader::Create()
{
    void *block = PoolNew(sizeof(cShader));
    return block ? static_cast<cShader *>(block)->Construct() : nullptr;
}

// 0xaa4d70: the vtable is set for the pass vector's sake; the base destructor (0xaa4cd0) only sets its own vtable.
void cShader::DestructBody()
{
    mPasses.Destroy();
    *reinterpret_cast<const void *const **>(this) = kOwnBaseVTable; // the base class's table (0xaa4cd0)
}

// Slot 13, 0xaa4e60. Bit 2 of the flags means the memory is not ours to give back (the free then does nothing).
cShader *cShader::DeleteBody(uint32_t flags)
{
    DestructBody();
    if ((flags & 1) && !(flags & 4)) PoolDelete(this);
    return this;
}

// 0xaa6e80. The device and the part lists are other classes' objects, called through their slots (device: 9 set a mode,
// 11/14/16/18 take one object or value, 17 binds (index, table entry, object, 0), 27 and 28 validate).
// The original's catch handler (0xaa70e0, line 0x23a: "...failed with exception '%s' when validating device state for
// pass %d") is not reproduced: nothing in our code throws there.
bool ShaderPass::Validate(void *device, uint32_t index, const uint32_t *table, const ShaderKey *key, bool report)
{
    ShaderKey scratch;
    const uint32_t *mapped = mP8 ? mP8->Slot9(&scratch, key) : reinterpret_cast<const uint32_t *>(key);
    ShaderKey local;
    std::memcpy(&local, mapped, sizeof(local));
    if (mPc) {
        RawSlot<14, uint32_t>(device, mPc);
        for (cIShaderPart **p = mV14.mBegin; p != mV14.mEnd; ++p) (*p)->Slot12(device, &local);
        RawSlot<9, uint32_t>(device, 2u);
        RawSlot<16, uint32_t>(device, mP48);
        void *list = RawSlot<12, void *>(mP48);
        const uint32_t count = RawSlot<4, uint32_t>(list);
        for (uint32_t i = 0; i < count; ++i)
            RawSlot<17, uint32_t>(device, i, table[i + 0x10], RawSlot<11, uint32_t>(mP48, i), 0u);
        if (!RawSlot<27, bool>(device, mP10, uint32_t(report))) {
            if (report) SayShader(0x211, "...failed validating preprocess vertex program for pass %d\n", index);
            return false;
        }
        RawSlot<16, uint32_t>(device, mP10);
        const uint32_t entry = mB6 ? table[0] : table[0x10];
        RawSlot<17, uint32_t>(device, 0u, entry, RawSlot<11, uint32_t>(mP10, 0u), 0u);
    }
    RawSlot<11, uint32_t>(device, mP20);
    for (cIShaderPart **p = mV24.mBegin; p != mV24.mEnd; ++p) (*p)->Slot12(device, &local);
    if (mB5) {
        RawSlot<16, uint32_t>(device, mP4c);
        void *list = RawSlot<12, void *>(mP4c);
        const uint32_t count = RawSlot<4, uint32_t>(list);
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t entry = mB6 ? table[i] : table[i + 0x10];
            RawSlot<17, uint32_t>(device, i, entry, RawSlot<11, uint32_t>(mP4c, i), 0u);
        }
    }
    RawSlot<18, uint32_t>(device, mB6 ? table[0x20] : table[0x21]);
    if (!RawSlot<28, bool>(device, uint32_t(report))) {
        if (report) SayShader(0x234, "...failed validating device state for pass %d\n", index);
        return false;
    }
    return true;
}

// Slot 10, 0xaa6c80
bool cShader::ValidateBody(const ShaderKey *key1, const ShaderKey *key2, const uint32_t *table, void *ctx)
{
    // Only the third dword of the keys is compared; the nibble tests read the keys themselves.
    if (key1->w[2] != key2->w[2]) return false;
    for (uint32_t i = 1; i < 13; ++i) {
        const uint32_t d2 = Nibble(key2, i) - Nibble(key2, i - 1);
        const uint32_t lo = mRanges[2 * i], hi = mRanges[2 * i + 1];
        if (int32_t(lo) < 0) {
            if (d2 != Nibble(key1, i) - Nibble(key1, i - 1)) return false;
        } else if (d2 < lo || d2 > hi) {
            return false;
        }
    }
    if (ctx && RawSlot<35, bool>(ctx) && table) {
        void *device = RawSlot<20, void *>(ctx);
        RawSlot<9, uint32_t>(device, 3u);
        for (uint32_t i = 0; i < uint32_t((mPasses.mEnd - mPasses.mBegin)); ++i)
            if (!mPasses.mBegin[i].Validate(device, i, table, key2, false)) return false;
    }
    return true;
}

// Slot 0, 0xaa6950 (the base class's 0xaa6920 answers the ids 1 and 0x9c544810).
bool cShader::QueryInterfaceBody(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == kIidA || iid == kIidB) {
        *out = this;
        return true;
    }
    if (iid == kIidCounted || iid == 1 || iid == 0x9c544810) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

// Slot 3, 0xaa6b30
void cShader::ResetPassesBody()
{
    for (ShaderPass *pass = mPasses.mBegin; pass != mPasses.mEnd; ++pass) pass->Reset();
}

// Slot 7, 0xaa6470
bool cShader::IsEqualBody(cShader *other)
{
    void *answer = other; // the original's out slot is its own argument slot: unwritten, it still holds the other
    if (!other->QueryInterface(kIidA, &answer)) return false;
    cShader *o = static_cast<cShader *>(answer);
    if (o == this) return true;
    for (int i = 0; i < 26; ++i)
        if (mRanges[i] != o->mRanges[i]) return false;
    if (mInfo.mCount != o->mInfo.mCount) return false;
    const int32_t count = int32_t(mInfo.mCount);
    for (int32_t i = 0; i < count; ++i)
        if (!mInfo.mFirstPass[i].Equals(&o->mInfo.mFirstPass[i])) return false;
    return true;
}

// Slot 11, 0xaa69e0: make this shader the renderer's current one and run each pass's apply function.
void cShader::ApplyBody(ShaderRenderCtx *ctx, ShaderFrame *frame)
{
    // The flag handed to the apply functions is a dword of the original's stack: the argument slot that held the
    // context pointer, whose low byte was overwritten with the flag. The functions read only the byte.
    uint32_t flag = uint32_t(reinterpret_cast<uintptr_t>(ctx)) & ~0xffu;
    uint8_t same;
    cShader *previous = ctx->mShader;
    if (this != previous) {
        flag |= 1;
        if (previous) {
            ShaderInfo *info = previous->GetInfo();
            if (info->m90) {
                for (uint32_t i = 0; i < info->mCount; ++i) info->mFirstPass[i].EndState(ctx);
            }
        }
        ++ctx->mShaderChanges;
        ctx->mShader = this;
        ctx->mPassCount += mInfo.mCount;
        same = 0;
    } else {
        ctx->mPassCount += mInfo.mCount;
        same = mInfo.mCount == 1 && ctx->m294 == frame->m0c && ctx->m290 == *frame->m2c && !(frame->m16 & 8) &&
               !((frame->m38 >> 1) & 1);
    }
    ctx->mSame = same;
    ShaderPass *pass = mInfo.mFirstPass;
    uint32_t count = mInfo.mCount;
    uint32_t out = 0;
    // ORIGINAL-BUG: a shader with no passes loops 2^32 times (the count is decremented before it is tested).
    do {
        pass->mApply(pass, ctx, frame, &out, flag);
        ++pass;
    } while (--count != 0);
    ctx->m294 = frame->m0c;
    ctx->m290 = *frame->m2c;
}

// Slot 8, 0xaa5030
bool cShader::BuildBody(void *ctx, const ShaderInitView *init, const uint32_t *table, const ShaderKey *key, bool report)
{
    std::memcpy(&mRanges[2], init->mRanges, sizeof(init->mRanges));
    // Slot 10 with the key twice and no context: the support flags are consistent.
    if (!Slot10(const_cast<ShaderKey *>(key), const_cast<ShaderKey *>(key), const_cast<uint32_t *>(table), nullptr)) {
        if (report) SayShader(0x342, "...failed due to inconsistent shader support flags\n");
        return false;
    }
    if (ctx && RawSlot<35, bool>(ctx) && table) {
        void *const device = RawSlot<20, void *>(ctx);
        const uint32_t wanted = uint32_t((init->mDescEnd - init->mDescBegin) / ShaderInitView::kDescriptorBytes);
        const uint32_t current = uint32_t(mPasses.mEnd - mPasses.mBegin);
        if (wanted < current) {
            ShaderPass *const newEnd = mPasses.mBegin + wanted;
            for (ShaderPass *p = newEnd; p != mPasses.mEnd; ++p) p->Destruct(0);
            mPasses.mEnd = newEnd;
        } else if (wanted > current) {
            if (wanted <= uint32_t(mPasses.mCap - mPasses.mBegin)) {
                // Room: construct the extra passes (the original's helper returns the new end).
                mPasses.mEnd = PassVec::FillDefault(mPasses.mEnd, wanted - current, &mPasses);
            } else {
                // No room: the vector's reallocating resize.
                uint8_t scratch = 0;
                mPasses.Reallocate(wanted, &scratch);
            }
        }
        for (uint32_t i = 0; i < uint32_t(mPasses.mEnd - mPasses.mBegin); ++i) {
            if (!mPasses.mBegin[i].Setup(static_cast<nGZGraphic4::cIDeviceDX *>(ctx), i, init, key, report)) return false;
        }
        RawSlot<9, uint32_t>(device, 3u);
        for (uint32_t i = 0; i < uint32_t(mPasses.mEnd - mPasses.mBegin); ++i)
            if (!mPasses.mBegin[i].Validate(device, i, table, key, report)) return false;
    }
    mInfo.mFirstPass = nullptr;
    mInfo.m84 = 0;
    std::memset(mInfo.mFlags, 0, sizeof(mInfo.mFlags));
    mInfo.m90 = 0;
    mInfo.mCount = uint32_t(mPasses.mEnd - mPasses.mBegin);
    mHash = JenkinsHash(mRanges, sizeof(mRanges), 0);
    if (mPasses.mBegin != mPasses.mEnd) {
        mInfo.mFirstPass = mPasses.mBegin;
        mInfo.m84 = mPasses.mBegin->m40;
        mHash = ShaderHashCombine(ShaderHashMix(mPasses.mBegin->m44), ShaderHashMix(mHash));
    }
    for (int32_t i = 0; i < int32_t(mInfo.mCount); ++i) {
        const ShaderPass &pass = mPasses.mBegin[i];
        if (pass.mB3c) mInfo.mFlags[2] = 1;
        if (pass.mB3d) mInfo.mFlags[3] = 1;
        if (pass.mB6) {
            mInfo.mFlags[1] = 1;
            if (pass.mB5) mInfo.mFlags[0] = 1;
        }
        if (pass.mV30.mBegin != pass.mV30.mEnd) mInfo.m90 = 1;
        if (i != 0) mHash = ShaderHashCombine(ShaderHashMix(pass.m44), ShaderHashMix(mHash));
    }
    return true;
}

// Slot 1 (0x146a50, a jump to the shared 0x148fb0)
uint32_t cShader::AddRefBody()
{
    return ++mRefCount;
}

// Slot 2 (0xaa69d0, a jump to the base's Release 0xaa69a0): the last reference lets the cache hold the object go (when it
// is enabled) and deletes it through slot 13.
uint32_t cShader::ReleaseBody()
{
    if (--mRefCount != 0) return mRefCount;
    if (mEnabled) RawSlot<27, uint32_t>(GetCachedQueriedObject(), this);
    Delete(1);
    return 0;
}

// Slot 4 (0x1489f0)
void cShader::SetEnabledBody(uint8_t on)
{
    mEnabled = on;
}

// Slot 5 (0x1480a0)
uint8_t cShader::GetEnabledBody()
{
    return mEnabled;
}

// Slot 6 (0x24bde0)
uint32_t cShader::GetHashBody()
{
    return mHash;
}

// Slot 9 (0xaa6b20)
ShaderInfo *cShader::GetInfoBody()
{
    return &mInfo;
}

// The base table's slot 0 (0xaa6920): the ids 1, 0x9c544810 and 0xfc17e651 give the object, counted.
bool cShader::BaseQueryInterfaceBody(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid != 1 && iid != 0x9c544810 && iid != kIidCounted) return false;
    *out = this;
    AddRef();
    return true;
}

// The base table's slot 3 (0x140fe0)
void cShader::BaseDoNothing() {}

// The base table's slot 13 (0xaa4e40): the base destructor (0xaa4cd0, which sets the base table), then the memory of the
// 0xc-byte base when bit 0 of the flags is set.
cShader *cShader::BaseDeleteBody(uint32_t flags)
{
    *reinterpret_cast<const void *const **>(this) = kOwnBaseVTable;
    if (flags & 1) ::operator delete(this, 0xc);
    return this;
}

// The tables are filled by dynamic initialisers (a member function's address is no constant expression): before any code of
// the program runs, as the image's tables are there before its code.
#pragma warning(disable : 4073) // initializers put in library initialization area: intended
#pragma init_seg(lib)

namespace {
template <typename T> const void *Own(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance members only");
    const void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}
} // namespace

// Slot order as the image's (14 slots; vtable 0xe758dc).
const void *const cShader::kOwnVTable[14] = {
    Own(&cShader::QueryInterfaceBody), // 0 0xaa6950
    Own(&cShader::AddRefBody),         // 1 0x146a50 (shared)
    Own(&cShader::ReleaseBody),        // 2 0xaa69d0
    Own(&cShader::ResetPassesBody),    // 3 0xaa6b30
    Own(&cShader::SetEnabledBody),     // 4 0x1489f0 (shared)
    Own(&cShader::GetEnabledBody),     // 5 0x1480a0 (shared)
    Own(&cShader::GetHashBody),        // 6 0x24bde0 (shared)
    Own(&cShader::IsEqualBody),        // 7 0xaa6470
    Own(&cShader::BuildBody),          // 8 0xaa5030
    Own(&cShader::GetInfoBody),        // 9 0xaa6b20
    Own(&cShader::ValidateBody),       // 10 0xaa6c80
    Own(&cShader::ApplyBody),          // 11 0xaa69e0
    Own(&cShader::DumpBody),           // 12 0xaa65e0
    Own(&cShader::DeleteBody),         // 13 0xaa4e60
};

// The base class's table (0xe758a0): slots 6..12 are the toolchain's _purecall.
const void *const cShader::kOwnBaseVTable[14] = {
    Own(&cShader::BaseQueryInterfaceBody), // 0 0xaa6920
    Own(&cShader::AddRefBody),             // 1 0x148fb0
    Own(&cShader::ReleaseBody),            // 2 0xaa69a0
    Own(&cShader::BaseDoNothing),          // 3 0x140fe0
    Own(&cShader::SetEnabledBody),         // 4 0x1489f0
    Own(&cShader::GetEnabledBody),         // 5 0x1480a0
    reinterpret_cast<const void *>(&_purecall), reinterpret_cast<const void *>(&_purecall),
    reinterpret_cast<const void *>(&_purecall), reinterpret_cast<const void *>(&_purecall),
    reinterpret_cast<const void *>(&_purecall), reinterpret_cast<const void *>(&_purecall),
    reinterpret_cast<const void *>(&_purecall),
    Own(&cShader::BaseDeleteBody),         // 13 0xaa4e40
};

} // namespace nGZSceneGraph
