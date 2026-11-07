// The material manager's iterator classes (lane TI); see the header.
#include "sg/cMaterialIterators.h"

#include "game/game.h"
#include "rz/RZFastFixedAllocators.h"
#include "rz/RZFixedAllocator.h"

namespace nGZSceneGraph {

namespace {

// QueryInterface, AddRef and Release of every one of the three (the bodies 0x178b20, 0x148fb0 and 0x1a84c0 the game's
// vtables share with unrelated classes): only cIGZUnknown (id 1) is answered, counted; the last Release deletes.
template <class T> bool QueryUnknown(T *self, uint32_t riid, void **out)
{
    if (out == nullptr || riid != 1) return false;
    *out = self;
    self->AddRef();
    return true;
}

} // namespace

// ---- cMaterialHandleIterator ----

void *cMaterialHandleIterator::operator new(size_t) noexcept { return reinterpret_cast<RZFixedAllocatorBase *>(&ts2_rva_10f6818)->Alloc(); }

void cMaterialHandleIterator::operator delete(void *block) noexcept { reinterpret_cast<RZFastFixed0x10 *>(&ts2_rva_10f6818)->Free(block); }

cMaterialHandleIterator::cMaterialHandleIterator(const std::vector<void *> *entries, uint32_t index)
    : mCount(0), mIndex(index), mEntries(entries)
{
    if (index < entries->size() && (*entries)[index] == nullptr) Next();
}

cMaterialHandleIterator::cMaterialHandleIterator(const cMaterialHandleIterator &other)
    : mCount(0), mIndex(other.mIndex), mEntries(other.mEntries)
{
}

bool cMaterialHandleIterator::QueryInterface(uint32_t riid, void **out) { return QueryUnknown(this, riid, out); }

uint32_t cMaterialHandleIterator::AddRef() { return ++mCount; }

uint32_t cMaterialHandleIterator::Release()
{
    if (--mCount != 0) return mCount;
    delete this;
    return 0;
}

nGZEvent::cIAttributeIDIterator *cMaterialHandleIterator::Clone() { return new cMaterialHandleIterator(*this); }

bool cMaterialHandleIterator::IsEqual(const cIAttributeIDIterator *other)
{
    return mIndex == static_cast<const cMaterialHandleIterator *>(other)->mIndex;
}

uint32_t cMaterialHandleIterator::Get() { return mIndex; }

void cMaterialHandleIterator::Next()
{
    do {
        ++mIndex;
        if (mIndex >= mEntries->size()) return;
    } while ((*mEntries)[mIndex] == nullptr);
}

// ---- cRenderShaderContextIterator ----

void *cRenderShaderContextIterator::operator new(size_t) noexcept { return reinterpret_cast<RZFixedAllocatorBase *>(&ts2_rva_10f6824)->Alloc(); }

void cRenderShaderContextIterator::operator delete(void *block) noexcept { reinterpret_cast<RZFastFixed0xc *>(&ts2_rva_10f6824)->Free(block); }

cRenderShaderContextIterator::cRenderShaderContextIterator(const MaterialListNode *node) : mCount(0), mNode(node) {}

cRenderShaderContextIterator::cRenderShaderContextIterator(const cRenderShaderContextIterator &other)
    : mCount(0), mNode(other.mNode)
{
}

bool cRenderShaderContextIterator::QueryInterface(uint32_t riid, void **out) { return QueryUnknown(this, riid, out); }

uint32_t cRenderShaderContextIterator::AddRef() { return ++mCount; }

uint32_t cRenderShaderContextIterator::Release()
{
    if (--mCount != 0) return mCount;
    delete this;
    return 0;
}

cIRenderShaderContextIterator *cRenderShaderContextIterator::Clone() { return new cRenderShaderContextIterator(*this); }

bool cRenderShaderContextIterator::IsEqual(const cIRenderShaderContextIterator *other)
{
    return mNode == static_cast<const cRenderShaderContextIterator *>(other)->mNode;
}

MaterialVariantValue cRenderShaderContextIterator::Get()
{
    MaterialVariantValue value;
    value.key[0] = mNode->value[0];
    value.key[1] = mNode->value[1];
    value.key[2] = mNode->value[2];
    value.material = reinterpret_cast<cIGZUnknown *>(static_cast<uintptr_t>(mNode->value[3]));
    return value;
}

void cRenderShaderContextIterator::Next() { mNode = mNode->next; }

// ---- cShaderIterator ----

void *cShaderIterator::operator new(size_t) noexcept { return reinterpret_cast<RZFixedAllocatorBase *>(&ts2_rva_10f6830)->Alloc(); }

void cShaderIterator::operator delete(void *block) noexcept { reinterpret_cast<RZFastFixed0xc *>(&ts2_rva_10f6830)->Free(block); }

cShaderIterator::cShaderIterator(const MaterialListNode *node) : mCount(0), mNode(node) {}

cShaderIterator::cShaderIterator(const cShaderIterator &other) : mCount(0), mNode(other.mNode) {}

bool cShaderIterator::QueryInterface(uint32_t riid, void **out) { return QueryUnknown(this, riid, out); }

uint32_t cShaderIterator::AddRef() { return ++mCount; }

uint32_t cShaderIterator::Release()
{
    if (--mCount != 0) return mCount;
    delete this;
    return 0;
}

cIShaderIterator *cShaderIterator::Clone() { return new cShaderIterator(*this); }

bool cShaderIterator::IsEqual(const cIShaderIterator *other)
{
    return mNode == static_cast<const cShaderIterator *>(other)->mNode;
}

void *cShaderIterator::Get()
{
    // The node's value starts with the listener; it is asked for the shader interface and the answer is returned as is
    // (when the query fails the original returns the uninitialised stack word it passed; ours returns null).
    void *shader = nullptr;
    reinterpret_cast<cIGZUnknown *>(static_cast<uintptr_t>(mNode->value[0]))->QueryInterface(kShaderIid, &shader);
    return shader;
}

void cShaderIterator::Next() { mNode = mNode->next; }

} // namespace nGZSceneGraph
