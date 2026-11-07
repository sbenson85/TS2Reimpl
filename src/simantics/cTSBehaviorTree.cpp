#include "simantics/cTSBehaviorTree.h"

#include "game/game.h"
#include "game/vtable.h"
#include "rz/RZFastFixedAllocators.h"

#include <corecrt.h>
#include <cstring>
#include <new>
#include <string>

namespace {

using AddRefFn = uint32_t(__thiscall *)(void *);
using ReleaseFn = uint32_t(__thiscall *)(void *);
using NoArgU32Fn = uint32_t(__thiscall *)(void *);
using NoArgU8Fn = uint8_t(__thiscall *)(void *);
using GetNodeFn = void *(__thiscall *)(void *, int32_t);
using StreamErrorFn = uint8_t(__thiscall *)(void *);
using StreamPtrFn = void(__thiscall *)(void *, void *);
using StreamBlockFn = void(__thiscall *)(void *, void *, uint32_t);
using StreamPutFn = void(__thiscall *)(void *, uint32_t);
using NodeReadFn = void(__thiscall *)(void *, void *, int32_t);
using NodeWriteFn = void(__thiscall *)(void *, void *);
using NodeSetFn = void(__thiscall *)(void *, uint32_t);
using NodeCopyParamToFn = void *(__thiscall *)(void *, void *);
using NodeCopyParamFromFn = void(__thiscall *)(void *, const void *);
using NodeGetParamFn = uint8_t *(__thiscall *)(void *);
using ValidatorFn = uint8_t(__thiscall *)(void *, void *);
using ProviderGetTreeFn = void *(__thiscall *)(void *, uint32_t);
using AnalyseFn = void(__thiscall *)(void *, void *);
using ExportNodeFn = void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, void *);

constexpr int kAddRefSlot = 1;
constexpr int kReleaseSlot = 2;

// Slots of the node (cITSBehaviorNode).
constexpr int kNodeGetOpcodeSlot = 3;
constexpr int kNodeSetOpcodeSlot = 5;
constexpr int kNodeGetTrueSlot = 6;
constexpr int kNodeGetFalseSlot = 8;
constexpr int kNodeSetTrueSlot = 10;
constexpr int kNodeSetFalseSlot = 11;
constexpr int kNodeCopyParamToSlot = 15;
constexpr int kNodeGetParamSlot = 16;
constexpr int kNodeCopyParamFromSlot = 17;
constexpr int kNodeWriteSlot = 20;
constexpr int kNodeReadSlot = 21;

// Slots of a stream (reader or writer).
constexpr int kStreamByteSlot = 5;
constexpr int kStreamWordSlot = 6;
constexpr int kStreamDwordSlot = 8;
constexpr int kStreamBlockSlot = 17;
constexpr int kStreamErrorSlot = 18;

// Slots of the sink ExportTo writes to.
constexpr int kSinkBeginSlot = 26;
constexpr int kSinkVersionSlot = 7;
constexpr int kSinkTypeSlot = 11;
constexpr int kSinkArgsSlot = 13;
constexpr int kSinkLocalsSlot = 15;
constexpr int kSinkFlagsSlot = 17;
constexpr int kSinkCacheSlot = 19;
constexpr int kSinkTreeVersionSlot = 9;
constexpr int kSinkNodeSlot = 24;

constexpr int kProviderGetTreeSlot = 23;
constexpr int kValidatorSlot = 8;
constexpr int kNameAssignSlot = 16;

constexpr uint32_t kNodeVTable = 0xdc8fb4;
constexpr uint32_t kNodePool = 0x1012908;
constexpr uint32_t kStringVTable = 0xd9b470;
constexpr uint32_t kNameBytes = 0x40;
constexpr uint32_t kMaxNodes = 0xfffb;

// A reference held on a cIGZUnknown-style object for the duration of a scope (AddRef when non-null at
// construction, Release when non-null at destruction), as the original's unwind frames do.
struct Held {
    void *p;
    explicit Held(void *obj) : p(obj)
    {
        if (p) VirtualCall<AddRefFn, kAddRefSlot>(p);
    }
    ~Held()
    {
        if (p) VirtualCall<ReleaseFn, kReleaseSlot>(p);
    }
    Held(const Held &) = delete;
    Held &operator=(const Held &) = delete;
};

bool StreamOk(void *stream)
{
    return VirtualCall<StreamErrorFn, kStreamErrorSlot>(stream) == 0;
}

// One block from the cTSBehaviorNode pool (the pool's Allocate slot, written inline against TakeFreePage as
// XPrimParam::AllocateBlock does), initialised the way the tree's code does: vtable, count/opcode/targets/operand
// zero. The reference count is still 0.
cTSBehaviorNode *NewNode()
{
    RZFixedAllocatorBase *pool = GameAddress<RZFixedAllocatorBase>(kNodePool);
    void *block = pool->mCurrent->freeList;
    if (!block) block = pool->TakeFreePage();
    RZFixedPage *page = pool->mCurrent; // read again: TakeFreePage moved it
    page->freeList = *static_cast<void **>(block);
    ++page->used;
    cTSBehaviorNode *node = static_cast<cTSBehaviorNode *>(block);
    node->mVTable = GameAddress<void>(kNodeVTable);
    node->mRefCount = 0;
    node->mOpcode = 0;
    node->mTrue = 0;
    node->mFalse = 0;
    node->mParam = nullptr;
    return node;
}

// push_back of an AddRef'd node pointer: the room-left path is inline in the original; growing is the shared
// reallocating function.
void PushNode(RefPtrVector &v, void *node)
{
    if (v.mLast != v.mEnd) {
        *v.mLast = node;
        VirtualCall<AddRefFn, kAddRefSlot>(node);
        ++v.mLast;
        return;
    }
    v.EmplaceReallocate(v.mLast, &node);
}

} // namespace

// ----- RefPtrVector --------------------------------------------------------------------------------

void RefPtrVector::Clear() noexcept
{
    void **first = mFirst;
    void **last = mLast;
    if (first == last) return;
    for (void **p = first; p != last; ++p)
        if (*p) VirtualCall<ReleaseFn, kReleaseSlot>(*p);
    mLast = mFirst;
}

void RefPtrVector::Destroy() noexcept
{
    void **first = mFirst;
    if (!first) return;
    for (void **p = first; p != mLast; ++p)
        if (*p) VirtualCall<ReleaseFn, kReleaseSlot>(*p);
    uint32_t bytes = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mEnd) - reinterpret_cast<uintptr_t>(mFirst)) & ~3u;
    void *block = first;
    if (bytes >= 0x1000) {
        // Large blocks were allocated 32-byte aligned with the real pointer stored just before them.
        block = first[-1];
        if (reinterpret_cast<uintptr_t>(first) - reinterpret_cast<uintptr_t>(block) - 4 > 0x1f) _invalid_parameter_noinfo_noreturn();
    }
    ::operator delete(block);
    mFirst = nullptr;
    mLast = nullptr;
    mEnd = nullptr;
}

// ----- construction ----------------------------------------------------------------------------------

cTSBehaviorTree *cTSBehaviorTree::Create()
{
    void *block = ::operator new(kSize);
    if (!block) return nullptr;
    cTSBehaviorTree *t = static_cast<cTSBehaviorTree *>(block);
    t->mVTable2 = GameAddress<void>(kBaseVTable);
    std::memset(t->mKey, 0, sizeof(t->mKey));
    t->mVTable = GameAddress<void>(kVTable);
    t->mVTable2 = GameAddress<void>(kVTable2);
    t->mRefCount = 0;
    t->mVersion = 0;
    t->mNodeCount = 0;
    t->mType = 0;
    t->mArgCount = 0;
    t->mLocalCount = 0;
    t->mFlags = 0;
    t->mCacheFlags = 0;
    t->mTreeVersion = 0xffff8009;
    t->mNodes.mFirst = t->mNodes.mLast = t->mNodes.mEnd = nullptr;
    t->mOwned38 = nullptr;
    t->mOwned3C = nullptr;
    // The original zeroes the whole 16-byte small-string buffer (the library constructor only clears the first byte).
    std::memset(reinterpret_cast<uint8_t *>(t) + 0x44, 0, 16);
    t->mName.Construct();
    return t;
}

cTSBehaviorTree *cTSBehaviorTree::DeletingDestructor(uint32_t flags)
{
    mVTable = GameAddress<void>(kVTable);
    mVTable2 = GameAddress<void>(kVTable2);
    mName.Destruct();
    if (mOwned3C) VirtualCall<ReleaseFn, kReleaseSlot>(mOwned3C);
    if (mOwned38) VirtualCall<ReleaseFn, kReleaseSlot>(mOwned38);
    mNodes.Destroy();
    mVTable2 = GameAddress<void>(kBaseVTable);
    if (flags & 1) ::operator delete(this);
    return this;
}

// ----- interface -------------------------------------------------------------------------------------

bool cTSBehaviorTree::QueryInterface(uint32_t iid, void **out)
{
    void *p = this;
    if (iid != kIidTree && iid != kIidSerializable) {
        p = reinterpret_cast<uint8_t *>(this) + 4;
        if (iid != kIidUnknown && iid != kIidPersistResource) return false;
    }
    *out = p;
    VirtualCall<AddRefFn, kAddRefSlot>(p);
    return true;
}

uint32_t cTSBehaviorTree::GetClassId() const
{
    return kClassId;
}

// The format word compares as a signed 16-bit value against 0x8002 and 0x8001; the result is built in eax
// from the zero-extended word, with only al replaced, so the bits 8..15 of the format word survive (callers
// read al, or ax at most; they are reproduced anyway).
uint32_t cTSBehaviorTree::GetArgCount() const
{
    uint32_t w = static_cast<uint16_t>(mVersion);
    if (mVersion >= static_cast<int16_t>(0x8002)) return (w & ~0xffu) | mArgCount;
    if (mVersion == static_cast<int16_t>(0x8001)) return mArgCount > 4 ? 4u : mArgCount;
    return (w & ~0xffu) | 8u;
}

uint32_t cTSBehaviorTree::GetLocalCount() const
{
    uint32_t w = static_cast<uint16_t>(mVersion);
    if (mVersion >= static_cast<int16_t>(0x8002)) return (w & ~0xffu) | mLocalCount;
    if (mVersion == static_cast<int16_t>(0x8001)) return mLocalCount > 4 ? 4u : mLocalCount;
    return w & ~0xffu;
}

void cTSBehaviorTree::SetLocalCount(uint32_t v)
{
    mLocalCount = static_cast<uint8_t>(v);
}

void cTSBehaviorTree::SetFlags(uint32_t v)
{
    mFlags = static_cast<uint8_t>(v);
}

uint16_t cTSBehaviorTree::GetNodeCount() const
{
    return mNodeCount;
}

void *cTSBehaviorTree::GetNode(uint32_t index) const
{
    if (index >= mNodes.Size()) return nullptr;
    return mNodes.mFirst[index];
}

void *cTSBehaviorTree::SetName(void *name)
{
    return VirtualCall<void *(__thiscall *)(void *, void *), kNameAssignSlot>(&mName, name);
}

// ----- nodes -----------------------------------------------------------------------------------------

bool cTSBehaviorTree::AddNode(uint32_t opcode, uint32_t trueTarget, uint32_t falseTarget, const XPrimParam *param)
{
    if (mNodes.Size() >= kMaxNodes) return false;
    cTSBehaviorNode *node = NewNode();
    ++node->mRefCount; // the slot-1 AddRef of the new block (0x469a20)
    void *n = node;
    VirtualCall<NodeSetFn, kNodeSetOpcodeSlot>(n, opcode);
    VirtualCall<NodeSetFn, kNodeSetTrueSlot>(n, trueTarget);
    VirtualCall<NodeSetFn, kNodeSetFalseSlot>(n, falseTarget);
    VirtualCall<NodeCopyParamFromFn, kNodeCopyParamFromSlot>(n, param);
    PushNode(mNodes, n);
    mNodeCount = static_cast<uint16_t>(mNodes.Size());
    VirtualCall<ReleaseFn, kReleaseSlot>(n);
    return true;
}

void cTSBehaviorTree::ClearNodes()
{
    mNodes.Clear();
    mNodeCount = static_cast<uint16_t>(mNodes.Size());
}

bool cTSBehaviorTree::IsJumpTarget(int32_t index)
{
    if (index < 0) return false;
    if (index >= static_cast<int16_t>(VirtualCall<NoArgU32Fn, kGetNodeCountSlot>(this))) return false;
    int32_t count = static_cast<int16_t>(VirtualCall<NoArgU32Fn, kGetNodeCountSlot>(this));
    if (0 >= count) return false;
    for (int32_t i = 0; i < count;) {
        if (i != index) {
            void *node = VirtualCall<GetNodeFn, kGetNodeSlot>(this, i);
            if (static_cast<uint16_t>(VirtualCall<NoArgU32Fn, kNodeGetTrueSlot>(node)) == static_cast<uint32_t>(index)) return true;
            node = VirtualCall<GetNodeFn, kGetNodeSlot>(this, i);
            if (static_cast<uint16_t>(VirtualCall<NoArgU32Fn, kNodeGetFalseSlot>(node)) == static_cast<uint32_t>(index)) return true;
        }
        ++i;
        count = static_cast<int16_t>(VirtualCall<NoArgU32Fn, kGetNodeCountSlot>(this));
    }
    return false;
}

uint8_t cTSBehaviorTree::CheckNode(int16_t index)
{
    void *node = VirtualCall<GetNodeFn, kGetNodeSlot>(this, index);
    Held hold(node);
    if (!node) return 1;
    int16_t opcode = static_cast<int16_t>(VirtualCall<NoArgU32Fn, kNodeGetOpcodeSlot>(node));
    if (opcode < 0x100) {
        // A primitive: the owned validator decides, and its al is the result.
        return VirtualCall<ValidatorFn, kValidatorSlot>(mOwned3C, node);
    }
    // A subroutine call: no node may jump to the error exit (0xfffe).
    int32_t i = 0;
    if (0 >= static_cast<int16_t>(VirtualCall<NoArgU32Fn, kGetNodeCountSlot>(this))) return 1;
    do {
        void *n = VirtualCall<GetNodeFn, kGetNodeSlot>(this, i);
        Held holdN(n);
        if (static_cast<uint16_t>(VirtualCall<NoArgU32Fn, kNodeGetTrueSlot>(n)) == 0xfffe) return 0;
        if (static_cast<uint16_t>(VirtualCall<NoArgU32Fn, kNodeGetFalseSlot>(n)) == 0xfffe) return 0;
        ++i;
    } while (i < static_cast<int16_t>(VirtualCall<NoArgU32Fn, kGetNodeCountSlot>(this)));
    return 1;
}

// ----- analysis and export ---------------------------------------------------------------------------

void cTSBehaviorTree::AnalyseFlags(void *provider)
{
    mFlags = (mFlags & 0xf9) | 1;
    int32_t count = static_cast<int16_t>(mNodeCount);
    if (count <= 0) return;
    for (int32_t i = 0; i < count;) {
        void *node = static_cast<uint32_t>(i) < mNodes.Size() ? mNodes.mFirst[i] : nullptr;
        int16_t opcode = static_cast<int16_t>(VirtualCall<NoArgU32Fn, kNodeGetOpcodeSlot>(node));
        uint32_t idx = static_cast<uint32_t>(static_cast<int32_t>(opcode) - 2);
        int cls = 4; // everything the switch does not name goes to the subroutine test
        if (idx <= 0x7c) {
            if (opcode == 2) cls = 0;
            else if (opcode == 20 || opcode == 28) cls = 1;
            else if (opcode == 50) cls = 2;
            else if (opcode == 126) cls = 3;
        }
        switch (cls) {
        case 0: {
            // Opcode 2: the operand bytes decide. Byte +0xa (12, 28 and 29 only) then byte +9 (anything in 3..21
            // except 8, 14, 15 and 16).
            uint8_t *param = VirtualCall<NodeGetParamFn, kNodeGetParamSlot>(node);
            uint32_t a = static_cast<uint32_t>(param[0xa]) - 12;
            if (a <= 0x11 && (a == 0 || a == 16 || a == 17)) {
                uint32_t b = static_cast<uint32_t>(param[9]) - 3;
                if (b <= 0x12 && b != 5 && b != 11 && b != 12 && b != 13) mFlags |= 2;
            }
            break;
        }
        case 1:
            mFlags |= 6;
            break;
        case 2:
            mFlags |= 4;
            break;
        case 3:
            mFlags |= 2;
            break;
        default:
            if (opcode > 0xff) {
                void *tree = VirtualCall<ProviderGetTreeFn, kProviderGetTreeSlot>(provider, static_cast<uint16_t>(opcode));
                Held hold(tree);
                if (tree) {
                    uint8_t flags = VirtualCall<NoArgU8Fn, kFlagsSlot>(tree);
                    if (!(flags & 1)) {
                        VirtualCall<AnalyseFn, kAnalyseSlot>(tree, provider);
                        flags = VirtualCall<NoArgU8Fn, kFlagsSlot>(tree);
                    }
                    uint8_t b = mFlags;
                    if (flags & 4) {
                        b |= 4;
                        mFlags = b;
                    }
                    if (flags & 2) {
                        b |= 2;
                        mFlags = b;
                    }
                }
            }
            break;
        }
        if ((mFlags & 6) == 6) return;
        ++i;
    }
}

void *cTSBehaviorTree::ExportTo(void *sink)
{
    VirtualCall<NoArgU32Fn, kSinkBeginSlot>(sink);
    VirtualCall<StreamPutFn, kSinkVersionSlot>(sink, static_cast<uint16_t>(mVersion));
    VirtualCall<StreamPutFn, kSinkTypeSlot>(sink, mType);
    VirtualCall<StreamPutFn, kSinkArgsSlot>(sink, mArgCount);
    VirtualCall<StreamPutFn, kSinkLocalsSlot>(sink, mLocalCount);
    VirtualCall<StreamPutFn, kSinkFlagsSlot>(sink, static_cast<uint8_t>(mFlags & 0xfe));
    VirtualCall<StreamPutFn, kSinkCacheSlot>(sink, mCacheFlags);
    VirtualCall<StreamPutFn, kSinkTreeVersionSlot>(sink, mTreeVersion);
    if (0 >= static_cast<int16_t>(mNodeCount)) return nullptr;
    int32_t i = 0;
    do {
        void *node = VirtualCall<GetNodeFn, kGetNodeSlot>(this, i);
        Held hold(node);
        void *const *sinkVTable = *static_cast<void *const *const *>(sink); // read before the node calls
        alignas(4) uint8_t param[XPrimParam::kSize];
        void *copy = VirtualCall<NodeCopyParamToFn, kNodeCopyParamToSlot>(node, param);
        uint32_t falseTarget = static_cast<uint16_t>(VirtualCall<NoArgU32Fn, kNodeGetFalseSlot>(node));
        uint32_t trueTarget = static_cast<uint16_t>(VirtualCall<NoArgU32Fn, kNodeGetTrueSlot>(node));
        uint32_t opcode = static_cast<uint16_t>(VirtualCall<NoArgU32Fn, kNodeGetOpcodeSlot>(node));
        reinterpret_cast<ExportNodeFn>(sinkVTable[kSinkNodeSlot])(sink, opcode, trueTarget, falseTarget, copy);
        ++i;
    } while (i < static_cast<int16_t>(mNodeCount));
    return nullptr;
}

// ----- serialisation ---------------------------------------------------------------------------------

bool cTSBehaviorTree::Write(void *stream)
{
    // The name goes out as a zero-filled 0x40-byte block.
    struct Block {
        uint8_t *p;
        ~Block() { ::operator delete(p); }
    } block{static_cast<uint8_t *>(::operator new(kNameBytes))};
    std::memset(block.p, 0, kNameBytes);
    // ORIGINAL-BUG: the copy uses the name's full length, not min(length, 0x40): a name longer than 0x40 bytes
    // writes past the end of the 0x40-byte block (heap overflow). Names come from 0x40-byte fields, so only
    // a name set later through SetName can trigger it.
    std::memcpy(block.p, mName.mString.c_str(), mName.mString.size());
    VirtualCall<StreamBlockFn, kStreamBlockSlot>(stream, block.p, kNameBytes);
    VirtualCall<StreamPutFn, kStreamWordSlot>(stream, 0xffff8009u);
    VirtualCall<StreamPutFn, kStreamWordSlot>(stream, mNodeCount);
    VirtualCall<StreamPutFn, kStreamByteSlot>(stream, mType);
    VirtualCall<StreamPutFn, kStreamByteSlot>(stream, mArgCount);
    VirtualCall<StreamPutFn, kStreamByteSlot>(stream, mLocalCount);
    VirtualCall<StreamPutFn, kStreamByteSlot>(stream, mFlags);
    VirtualCall<StreamPutFn, kStreamDwordSlot>(stream, mTreeVersion);
    VirtualCall<StreamPutFn, kStreamByteSlot>(stream, mCacheFlags);
    if (mNodes.Size() != 0) {
        uint32_t i = 0;
        do {
            if (!StreamOk(stream)) break;
            void *node = mNodes.mFirst[i];
            VirtualCall<NodeWriteFn, kNodeWriteSlot>(node, stream);
            ++i;
        } while (i < mNodes.Size());
    }
    return StreamOk(stream);
}

bool cTSBehaviorTree::Read(void *stream)
{
    mNodes.Clear();
    // The name field: 0x40 bytes. The original runs strlen over its stack buffer; if the field has no NUL, the
    // scan continues into four uninitialised bytes before the (zeroed) temporary string, so this assumes a NUL
    // (a name field always has one).
    struct {
        char text[kNameBytes];
        char after[4];
    } name;
    std::memset(name.after, 0, sizeof(name.after));
    VirtualCall<StreamBlockFn, kStreamBlockSlot>(stream, name.text, kNameBytes);
    {
        std::string tmp(name.text, std::strlen(name.text));
        mName.mString.assign(tmp.data(), tmp.size());
    }
    VirtualCall<StreamPtrFn, kStreamWordSlot>(stream, &mVersion);
    if (mVersion > 0) {
        // The old format has no signature: the first word is the node count.
        mNodeCount = static_cast<uint16_t>(mVersion);
        mType = 0;
        mArgCount = 4;
        mLocalCount = 4;
        mFlags = 0;
        mCacheFlags = 0;
        mTreeVersion = 0;
    } else {
        VirtualCall<StreamPtrFn, kStreamWordSlot>(stream, &mNodeCount);
        VirtualCall<StreamPtrFn, kStreamByteSlot>(stream, &mType);
        VirtualCall<StreamPtrFn, kStreamByteSlot>(stream, &mArgCount);
        VirtualCall<StreamPtrFn, kStreamByteSlot>(stream, &mLocalCount);
        VirtualCall<StreamPtrFn, kStreamByteSlot>(stream, &mFlags);
        VirtualCall<StreamPtrFn, kStreamDwordSlot>(stream, &mTreeVersion);
        if (mVersion >= static_cast<int16_t>(0x8009))
            VirtualCall<StreamPtrFn, kStreamByteSlot>(stream, &mCacheFlags);
        else
            mCacheFlags = 0;
        if (mVersion < static_cast<int16_t>(0x8008)) mFlags = 0;
        mFlags &= 0xfe;
    }
    if (0 < static_cast<int16_t>(mNodeCount)) {
        int32_t i = 0;
        do {
            if (!StreamOk(stream)) break;
            cTSBehaviorNode *node = NewNode();
            ++node->mRefCount; // 0x469a20, the slot-1 AddRef called directly
            void *n = node;
            Held local(nullptr);
            local.p = n; // owns the reference just taken; Released at scope exit
            PushNode(mNodes, n);
            void *element = mNodes.mFirst[i];
            VirtualCall<NodeReadFn, kNodeReadSlot>(element, stream, static_cast<int32_t>(mVersion));
            ++i;
        } while (i < static_cast<int16_t>(mNodeCount));
    }
    return StreamOk(stream);
}
