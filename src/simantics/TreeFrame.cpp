#include "simantics/TreeFrame.h"

#include "game/game.h"
#include "game/vtable.h"
#include "ifaces/cTSTreeStackElem.h"
#include "ifaces/TreeOwner.h"
#include "ifaces/TreeInStream.h"
#include "ifaces/TreeNameSink.h"
#include "ifaces/TreeOutStream.h"
#include "ifaces/TreeOwnerReader.h"
#include "ifaces/TreeOwnerWriter.h"
#include "ifaces/TreeStreamContext.h"
#include "rz/RZFastFixedAllocators.h"

#include <cstddef>
#include <cstring>

namespace {

using AddRefFn = uint32_t(__thiscall *)(void *);

constexpr int kAddRefSlot = 1;

// A stream version above which the saved frames carry more fields: read from the stream's context object.
constexpr uint32_t kVersionStackObjectId = 0x19;
constexpr uint32_t kVersionTwoWords = 0xa4;

// The version the stream context says it is: stream->slot20 is the context object, whose slot 78 gives a pointer to
// the version number (shared by the frame's and the stack's Read).
uint32_t StreamVersion(void *stream)
{
    void *context = As<ifaces::TreeInStream>(stream)->GetContext();
    return *As<ifaces::TreeStreamContext>(context)->GetVersionAddress();
}

} // namespace

uint32_t TreeStreamVersion(void *stream)
{
    return StreamVersion(stream);
}

cTSTreeStackElem *cTSTreeStackElem::NewBlock()
{
    RZFixedAllocatorBase *pool = GameAddress<RZFixedAllocatorBase>(kPool);
    void *block = pool->mCurrent->freeList;
    if (!block) block = pool->TakeFreePage();
    RZFixedPage *page = pool->mCurrent; // read again: TakeFreePage moved it
    page->freeList = *static_cast<void **>(block);
    ++page->used;
    cTSTreeStackElem *frame = static_cast<cTSTreeStackElem *>(block);
    // The original's stores, in its order; the bytes it skips (+0xe, +0xf, +0x16, +0x17, +0x1f) keep the pool's contents.
    frame->mVTable = GameAddress<void>(kVTable);
    frame->mRefCount = 0;
    *reinterpret_cast<uint32_t *>(&frame->mTreeWord) = 0; // +8 and +0xa
    frame->mNodeIndex = 0;
    frame->mResumeState = 0;
    frame->mLocalCount = 0;
    frame->mArgCount = 0;
    *reinterpret_cast<uint32_t *>(&frame->mWord18) = 0; // +0x18 and +0x1a
    frame->mWord1c = 0;
    frame->mByte1e = 0;
    frame->mOwner = nullptr;
    // The two vectors: raw zeroes, not constructed (the block is raw memory).
    std::memset(static_cast<void *>(&frame->mArgs), 0, sizeof(frame->mArgs));
    std::memset(static_cast<void *>(&frame->mLocals), 0, sizeof(frame->mLocals));
    return frame;
}

bool cTSTreeStackElem::QueryInterface(uint32_t iid, void **out)
{
    if (iid != kIidUnknown && iid != kIidFrame) return false;
    *out = this;
    VirtualCall<AddRefFn, kAddRefSlot>(this);
    return true;
}

int32_t cTSTreeStackElem::GetTreeId()
{
    const uint32_t word = mTreeWord;
    return word == 0xffff ? -1 : static_cast<int32_t>(word & 0x7fff);
}

void cTSTreeStackElem::SetTreeId(uint32_t id)
{
    const uint16_t value = static_cast<uint16_t>(id);
    if (value == 0xffff) {
        mTreeWord = 0xffff;
        return;
    }
    mTreeWord = static_cast<uint16_t>((mTreeWord & 0x8000u) | value);
}

void cTSTreeStackElem::SetFlag16(uint32_t value)
{
    mByte16 = static_cast<uint8_t>(value);
}

bool cTSTreeStackElem::IsPaused()
{
    const uint16_t word = mTreeWord;
    return word != 0xffff && (word & 0x8000u) != 0;
}

void cTSTreeStackElem::SetPaused(uint8_t paused)
{
    const uint16_t word = mTreeWord;
    if (word == 0xffff) return;
    mTreeWord = paused ? static_cast<uint16_t>(word | 0x8000u) : static_cast<uint16_t>(word & 0x7fffu);
}

void cTSTreeStackElem::SetByte1e(uint32_t value)
{
    mByte1e = static_cast<uint8_t>(value);
}

void cTSTreeStackElem::SetArgCount(uint8_t count)
{
    mArgCount = count;
    mArgs.resize(count);
}

void cTSTreeStackElem::SetLocalCount(uint8_t count)
{
    mLocalCount = count;
    mLocals.resize(count);
}

uint32_t cTSTreeStackElem::GetArg(int32_t index)
{
    // ORIGINAL-BUG: only the upper bound is tested (signed), so a negative index reads before the vector.
    if (index < static_cast<int32_t>(mArgCount)) return mArgs.data()[index];
    return 0;
}

uint32_t cTSTreeStackElem::GetLocal(int32_t index)
{
    // ORIGINAL-BUG: as GetArg, no lower bound.
    if (index < static_cast<int32_t>(mLocalCount)) return mLocals.data()[index];
    return 0;
}

void cTSTreeStackElem::SetArg(int32_t index, uint32_t value)
{
    // ORIGINAL-BUG: as GetArg, no lower bound.
    if (index < static_cast<int32_t>(mArgCount)) mArgs.data()[index] = static_cast<uint16_t>(value);
}

void cTSTreeStackElem::SetArgs(int32_t count, const uint16_t *values)
{
    if (!values) return;
    for (int32_t i = 0; i < count; ++i) {
        if (static_cast<uint32_t>(i) < mArgCount) As<ifaces::cTSTreeStackElem>(this)->SetArg(i, values[i]);
    }
}

void cTSTreeStackElem::SetLocal(uint32_t index, uint32_t value)
{
    mLocals.data()[index] = static_cast<uint16_t>(value);
}

uint16_t *cTSTreeStackElem::GetArgAddress(uint32_t index)
{
    return mArgs.data() + index;
}

uint16_t *cTSTreeStackElem::GetLocalAddress(uint32_t index)
{
    return mLocals.data() + index;
}

void cTSTreeStackElem::GetTreeName(void *name)
{
    if (mOwner) {
        // The id is asked through the frame's own slot 9, as the original does.
        const uint32_t id = static_cast<uint16_t>(As<ifaces::cTSTreeStackElem>(this)->GetTreeId());
        As<ifaces::TreeOwner>(mOwner)->GetTreeName(id, name);
        return;
    }
    As<ifaces::TreeNameSink>(name)->SetText("No behavior");
}

bool cTSTreeStackElem::Write(void *stream, void *ownerWriter)
{
    ifaces::TreeOutStream *out = As<ifaces::TreeOutStream>(stream);
    out->PutWord(mStackObjectId);
    out->PutWord(static_cast<uint16_t>(As<ifaces::cTSTreeStackElem>(this)->GetTreeId()));
    out->PutWord(mNodeIndex);
    out->PutByte(mLocalCount);
    out->PutByte(mArgCount);
    out->PutWord(mWord18);
    out->PutWord(mWord1a);
    for (uint32_t i = 0; i < mArgCount; ++i) out->PutWord(mArgs.data()[i]);
    for (uint32_t i = 0; i < mLocalCount; ++i) out->PutWord(mLocals.data()[i]);
    out->PutDword(mResumeState);
    As<ifaces::TreeOwnerWriter>(ownerWriter)->WriteOwner(stream, &mOwner);
    return out->HasError() == 0;
}

bool cTSTreeStackElem::Read(void *stream, void *ownerReader, uint32_t defaultWord)
{
    ifaces::TreeInStream *in = As<ifaces::TreeInStream>(stream);
    const uint32_t version = StreamVersion(stream);
    ifaces::cTSTreeStackElem *self = As<ifaces::cTSTreeStackElem>(this);
    // The three scratch locals of the original share stack slots with its saved registers and its stream argument:
    // the tree id word is read into the low half of the dword that held `this`, the two counts into the low byte of
    // the dword that held the stream pointer. Only the low half or byte is ever used by the callees.
    uint32_t wordSlot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
    uint32_t byteSlot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(stream));
    if (version > kVersionStackObjectId) in->GetWord(&mStackObjectId);
    in->GetWord(reinterpret_cast<uint16_t *>(&wordSlot));
    self->SetTreeId(wordSlot);
    in->GetWordB(&mNodeIndex);
    in->GetByte(reinterpret_cast<uint8_t *>(&byteSlot));
    self->SetLocalCount(static_cast<uint8_t>(byteSlot));
    in->GetByte(reinterpret_cast<uint8_t *>(&byteSlot));
    self->SetArgCount(static_cast<uint8_t>(byteSlot));
    if (version > kVersionTwoWords) {
        in->GetWord(&mWord18);
        in->GetWord(&mWord1a);
    } else {
        mWord18 = static_cast<uint16_t>(defaultWord);
        mWord1a = static_cast<uint16_t>(defaultWord);
    }
    for (uint32_t i = 0; i < mArgCount; ++i) in->GetWord(&mArgs.data()[i]);
    for (uint32_t i = 0; i < mLocalCount; ++i) in->GetWord(&mLocals.data()[i]);
    in->GetDword(&mResumeState);
    mByte16 = 0;
    As<ifaces::TreeOwnerReader>(ownerReader)->ReadOwner(stream, &mOwner);
    return in->HasError() == 0;
}

cTSTreeStackElem *cTSTreeStackElem::Destroy(uint32_t flags)
{
    mVTable = GameAddress<void>(kVTable);
    // The vectors give their storage back and end up empty (locals first, as the original does).
    std::vector<uint16_t>().swap(mLocals);
    std::vector<uint16_t>().swap(mArgs);
    if (flags & 1) {
        // Bit 2 of the flags (placement) calls a function that does nothing; otherwise the block goes back to the pool.
        if (!(flags & 4)) GameAddress<RZFastFixed0x3c>(kPool)->Free(this);
    }
    return this;
}
