#include "std/StdGameTypes.h"

#include <windows.h>

#include <cstring>
#include <memory>
#include <new>

namespace StdGameTypes {

FwRefList::EmplaceOp *__fastcall ConstructFwRefEmplaceOp(FwRefList::EmplaceOp *self, void *, FwRefList::NodeAlloc &alloc,
                                                        const FwRef &value)
{
    [[msvc::forceinline_calls]] return ::new (self) FwRefList::EmplaceOp(alloc, value);
}

void __fastcall DestroyFwRefEmplaceOp(FwRefList::EmplaceOp *self)
{
    [[msvc::forceinline_calls]] std::destroy_at(self);
}

RegionMapAccess::Node *RegionMapAccess::EraseNode(Node *where)
{
    [[msvc::forceinline_calls]] return erase(const_iterator(where, _Get_scary()))._Ptr;
}

RegionMapAccess::TempNode *__fastcall ConstructRegionMapTempNode(RegionMapAccess::TempNode *self, void *,
                                                                RegionMapAccess::NodeAlloc &alloc, RegionMapAccess::Node *head,
                                                                const RegionMapAccess::value_type &value)
{
    [[msvc::forceinline_calls]] return ::new (self) RegionMapAccess::TempNode(alloc, head, value);
}

void *__cdecl ProcessHeapAllocate(size_t bytes)
{
    return HeapAlloc(GetProcessHeap(), 0, bytes);
}

void __cdecl ProcessHeapDeallocate(void *block, size_t)
{
    HeapFree(GetProcessHeap(), 0, block);
}

FwServicePair *__fastcall DeleteFwServicePair(FwServicePair *self, void *, uint32_t flags)
{
    std::destroy_at(self);
    if (flags & 1) ::operator delete(self, sizeof(FwServicePair));
    return self;
}

FwPriorityPair *__fastcall DeleteFwPriorityPair(FwPriorityPair *self, void *, uint32_t flags)
{
    std::destroy_at(self);
    if (flags & 1) ::operator delete(self, sizeof(FwPriorityPair));
    return self;
}

void __cdecl FreeFwServiceMapNode(std::allocator<FwServiceMapAccess::Node> &alloc, FwServiceMapAccess::Node *node)
{
    [[msvc::forceinline_calls]] FwServiceMapAccess::Node::_Freenode(alloc, node);
}

void __cdecl FreeFwRefListNode(FwRefList::NodeAlloc &alloc, FwRefList::Node *node)
{
    [[msvc::forceinline_calls]] FwRefList::Node::_Freenode(alloc, node);
}

void __cdecl FreeFwPriorityNode(std::allocator<FwPriorityNode> &alloc, FwPriorityNode *node)
{
    [[msvc::forceinline_calls]] FwPriorityNode::_Freenode(alloc, node);
}

void ByteVector::ResizeReallocate(size_t newSize, const void *)
{
    constexpr size_t kMax = 0x7fffffff;
    if (newSize > kMax) std::_Xlength_error("vector too long");
    const size_t oldSize = static_cast<size_t>(mLast - mFirst);
    const size_t oldCapacity = static_cast<size_t>(mEnd - mFirst);
    size_t newCapacity;
    if (oldCapacity > kMax - oldCapacity / 2) {
        newCapacity = kMax;
    } else {
        newCapacity = oldCapacity + oldCapacity / 2;
        if (newCapacity < newSize) newCapacity = newSize;
    }
    std::allocator<char> alloc;
    char *block = alloc.allocate(newCapacity);
    std::memset(block + oldSize, 0, newSize - oldSize);
    if (oldSize != 0) std::memcpy(block, mFirst, oldSize);
    if (mFirst) alloc.deallocate(mFirst, oldCapacity);
    mFirst = block;
    mLast = block + newSize;
    mEnd = block + newCapacity;
}

size_t StringAccess::FindLastOfText(const char *text, size_t offset) const
{
    return find_last_of(text, offset);
}

} // namespace StdGameTypes
