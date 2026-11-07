// cRZUnknownList (lane FC): see rz/cRZUnknownList.h. RVAs are 1.18.0.187.
#include "rz/cRZUnknownList.h"

#include <iterator>
#include <new>

namespace {
constexpr uint32_t kUnknownIid = 1; // cIGZUnknown
}

cRZUnknownList::cRZUnknownList() : mRefCount(0) {}

cRZUnknownList::~cRZUnknownList() = default;

bool cRZUnknownList::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != kIid && riid != kUnknownIid) return false;
    *ppv = this;
    AddRef();
    return true;
}

uint32_t cRZUnknownList::AddRef() { return ++mRefCount; }

uint32_t cRZUnknownList::Release()
{
    if (mRefCount > 1) return --mRefCount;
    delete this;
    return 0;
}

uint32_t cRZUnknownList::Size() const { return static_cast<uint32_t>(mItems.size()); }

bool cRZUnknownList::GetItem(uint32_t index, uint32_t iid, void **out)
{
    if (index >= mItems.size()) return false;
    auto it = mItems.begin();
    for (uint32_t i = 0; it != mItems.end() && i < index; ++i) ++it;
    return it->p->QueryInterface(iid, out);
}

bool cRZUnknownList::ForEach(Visitor *visitor, uint32_t a, uint32_t b)
{
    for (auto it = mItems.begin(); it != mItems.end(); ++it) visitor->Visit(it->p, a, b);
    return true;
}

bool cRZUnknownList::ForEachAgain(Visitor *visitor, uint32_t a, uint32_t b)
{
    for (auto it = mItems.begin(); it != mItems.end(); ++it) visitor->Visit(it->p, a, b);
    return true;
}

bool cRZUnknownList::PrependAll(cIGZUnknownList *other)
{
    std::list<Ref> staged;
    const uint32_t count = other->Size();
    for (uint32_t i = 0; i < count; ++i) {
        cIGZUnknown *item = nullptr;
        if (other->GetItem(i, kUnknownIid, reinterpret_cast<void **>(&item))) {
            Ref holder = Ref::Acquire(item);
            staged.emplace(staged.end(), holder);
            item->Release();
        }
    }
    // The staged elements are copied in (a count each) and the staging list then releases its own.
    mItems.insert(mItems.begin(), staged.begin(), staged.end());
    return true;
}

bool cRZUnknownList::PushFront(cIGZUnknown *item)
{
    Ref holder = Ref::Acquire(item);
    mItems.emplace(mItems.begin(), holder);
    return true;
}

bool cRZUnknownList::AppendAll(cIGZUnknownList *other)
{
    const uint32_t count = other->Size();
    for (uint32_t i = 0; i < count; ++i) {
        cIGZUnknown *item = nullptr;
        if (other->GetItem(i, kUnknownIid, reinterpret_cast<void **>(&item))) {
            Ref holder = Ref::Acquire(item);
            mItems.emplace(mItems.end(), holder);
            item->Release();
        }
    }
    return true;
}

bool cRZUnknownList::PushBack(cIGZUnknown *item)
{
    Ref holder = Ref::Acquire(item);
    mItems.emplace(mItems.end(), holder);
    return true;
}

bool cRZUnknownList::GetFront(uint32_t iid, void **out)
{
    if (mItems.empty()) return false;
    return mItems.front().p->QueryInterface(iid, out);
}

bool cRZUnknownList::GetBack(uint32_t iid, void **out)
{
    if (mItems.empty()) return false;
    return mItems.back().p->QueryInterface(iid, out);
}

bool cRZUnknownList::PopFront(uint32_t iid, void **out)
{
    if (mItems.empty()) return false;
    const bool found = mItems.front().p->QueryInterface(iid, out);
    if (found) mItems.erase(mItems.begin());
    return found;
}

bool cRZUnknownList::PopBack(uint32_t iid, void **out)
{
    if (mItems.empty()) return false;
    const bool found = mItems.back().p->QueryInterface(iid, out);
    if (found) mItems.erase(std::prev(mItems.end()));
    return found;
}

bool cRZUnknownList::Clear()
{
    mItems.clear();
    return true;
}

bool cRZUnknownList::Assign(cIGZUnknownList *other)
{
    Clear();
    return PrependAll(other);
}

bool cRZUnknownList::Clone(cIGZUnknownList **out)
{
    cRZUnknownList *copy = new cRZUnknownList;
    *out = copy;
    copy->AddRef();
    return (*out)->PrependAll(this);
}

// ---- adapters ------------------------------------------------------------------------------------------------------

cRZUnknownList *__fastcall cRZUnknownListAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cRZUnknownList;
}

void __fastcall cRZUnknownListAdapters::Destruct(cRZUnknownList *self, void *)
{
    self->cRZUnknownList::~cRZUnknownList();
}

bool __fastcall cRZUnknownListAdapters::PushBack(cRZUnknownList *self, void *, cIGZUnknown *item)
{
    [[msvc::forceinline_calls]] return self->cRZUnknownList::PushBack(item);
}
