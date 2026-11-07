#include "simantics/TreeStack.h"

#include "game/game.h"
#include "game/vtable.h"
#include "ifaces/TreeInStream.h"
#include "ifaces/TreeOutStream.h"
#include "ifaces/cTSTreeStack.h"
#include "ifaces/cTSTreeStackElem.h"
#include "simantics/TreeHeld.h"

#include <cstddef>

namespace {

using RefFn = uint32_t(__thiscall *)(void *);

constexpr int kAddRefSlot = 1;
constexpr int kReleaseSlot = 2;

void AddRef(void *object)
{
    VirtualCall<RefFn, kAddRefSlot>(object);
}

void Release(void *object)
{
    VirtualCall<RefFn, kReleaseSlot>(object);
}

} // namespace

bool cTSTreeStack::QueryInterface(uint32_t iid, void **out)
{
    if (iid != kIidUnknown && iid != kIidStack) return false;
    *out = this;
    AddRef(this); // slot 1: the shared `return 1`
    return true;
}

void cTSTreeStack::Reset(uint32_t reserve)
{
    Clear();
    // Room for `reserve` frames when the vector is smaller (a request above max_size is the vector's length error).
    if (reserve > mFrames.capacity()) mFrames.reserve(reserve);
}

uint8_t cTSTreeStack::NewFrame(uint32_t iid, void **out)
{
    cTSTreeStackElem *frame = cTSTreeStackElem::NewBlock();
    AddRef(frame);
    const uint8_t ok = As<ifaces::cTSTreeStackElem>(frame)->QueryInterface(iid, out);
    Release(frame);
    return ok;
}

void cTSTreeStack::Push(cTSTreeStackElem *frame, uint32_t, void *)
{
    // A local reference around the push (the original's holder); the vector's own copy takes another.
    TreeFrameRef local(frame);
    mFrames.push_back(local);
}

void cTSTreeStack::Pop(uint32_t, void *)
{
    if (mFrames.empty()) throw kErrorEmpty;
    mFrames.pop_back(); // gives the top frame's reference back first
}

void cTSTreeStack::Clear()
{
    // Gives back every reference and keeps the storage: the end follows the beginning.
    mFrames.clear();
}

cTSTreeStackElem *cTSTreeStack::GetAt(int32_t index)
{
    if (index < 0 || static_cast<uint32_t>(index) >= mFrames.size()) return nullptr;
    return mFrames[static_cast<uint32_t>(index)].get();
}

uint8_t cTSTreeStack::Write(void *stream, void *argument)
{
    ifaces::TreeOutStream *out = As<ifaces::TreeOutStream>(stream);
    const int32_t count = As<ifaces::cTSTreeStack>(this)->Count();
    out->PutDword(static_cast<uint32_t>(count));
    out->PutDword(0);
    for (int32_t i = 0; i < count; ++i)
        As<ifaces::cTSTreeStackElem>(mFrames[static_cast<uint32_t>(i)].get())->Write(stream, argument);
    return out->HasError() == 0;
}

uint8_t cTSTreeStack::Read(void *stream, void *ownerReader, uint32_t defaultWord)
{
    ifaces::TreeInStream *in = As<ifaces::TreeInStream>(stream);
    if (TreeStreamVersion(stream) > 0x19) {
        int32_t count;
        int32_t second;
        in->GetDword(reinterpret_cast<uint32_t *>(&count));
        in->GetDword(reinterpret_cast<uint32_t *>(&second));
        for (int32_t i = 0; i < count; ++i) {
            // A new frame pushed like NewFrame + Push: pool block, a reference while it goes onto the vector (the
            // original builds it inline, with the same stores as NewFrame), then read into the new top frame.
            cTSTreeStackElem *frame = cTSTreeStackElem::NewBlock();
            {
                TreeFrameRef local(frame);
                mFrames.push_back(local);
            }
            As<ifaces::cTSTreeStackElem>(mFrames.back().get())->Read(stream, ownerReader, defaultWord);
        }
    }
    return in->HasError() == 0;
}

cTSTreeStack *cTSTreeStack::Destroy(uint32_t flags)
{
    mVTable = GameAddress<void>(kVTable);
    std::vector<TreeFrameRef>().swap(mFrames); // gives the references back and frees the storage
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}
