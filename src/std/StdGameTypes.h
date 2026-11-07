#pragma once
#include "gz/Framework.h"
#include "gz/GZCOM.h"
#include "rz/RZBlockPool.h"
#include "rz/RZCOMDllDirectorShape.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <set>
#include <string>
#include <unordered_map>

// The game's out-of-line copies of standard-library members instantiated with game element types (1.18.0.187),
// which only our verified code's originals call (milestone H, package H6). Our callers use the same containers,
// so each replacement is that member of our toolchain's instantiation (MSVC 14.40, the game's headers), reached
// through a thin accessor where the member is protected, or the same few lines where the library's own body is a
// template detail. Element types are the game's as our classes declare them; the comment at each says which
// container it is and who calls it. Calling conventions are the original's: __thiscall unless noted, results
// wider than a register through a hidden pointer.
namespace StdGameTypes {

// ---- std::list<FwRef> (cGZFramework's hook and service lists; 31 callers share the code) ----------------------
struct FwRefList : std::list<FwRef> {
    using Node = std::_List_node<FwRef, void *>;
    using NodeAlloc = std::allocator<Node>;
    using EmplaceOp = std::_List_node_emplace_op2<NodeAlloc>;
};
// 0x141503 (ret 8): the emplace guard's constructor: a 12-byte node from `alloc`, its value a copy of `value`
// (AddRef on a non-null object); the links are left unset. Returns the guard.
FwRefList::EmplaceOp *__fastcall ConstructFwRefEmplaceOp(FwRefList::EmplaceOp *self, void *edx, FwRefList::NodeAlloc &alloc,
                                                        const FwRef &value);
// 0x141f0c: the guard's destructor: a node still held has its value destroyed (Release) and is freed.
void __fastcall DestroyFwRefEmplaceOp(FwRefList::EmplaceOp *self);

using FwServiceMap = std::unordered_map<uint32_t, FwRef>;
// ---- the block pools' region map (RZRegionMap) ----------------------------------------------------------------------
struct RegionMapAccess : RZRegionMap {
    using Node = std::_Tree_node<value_type, void *>;
    // 0x1be496 (ret 4): erases the node `where` (an iterator by value) and returns its successor (RZBlockPool's
    // AllocSmall 0x1bdd69 / AllocLarge 0x1bdeb8, moving a region between the maps).
    Node *EraseNode(Node *where);
    using NodeAlloc = ProcessHeapAllocator<Node>;
    using TempNode = std::_Tree_temp_node<NodeAlloc>;
    // 0x1bdc30 (ret 4 plus the hidden result): insert(value): {the node holding value's key, whether it was inserted};
    // length_error at max_size() (0xaaaaaaa). The only caller is the library's 35-byte insert wrapper 0x1bdcc2.
    std::pair<Node *, bool> Emplace(const value_type &value);
};
// 0x1bdbc9 (ret 0xc): the insert's temporary node: a 24-byte node from the process heap holding a copy of value, its
// three links set to `head`, red, not the nil node. Returns the guard.
RegionMapAccess::TempNode *__fastcall ConstructRegionMapTempNode(RegionMapAccess::TempNode *self, void *edx,
                                                                RegionMapAccess::NodeAlloc &alloc, RegionMapAccess::Node *head,
                                                                const RegionMapAccess::value_type &value);
// 0x1be421 (__cdecl): ProcessHeapAllocator::allocate as the containers call it, with the byte count:
// HeapAlloc(GetProcessHeap(), 0, bytes).
void *__cdecl ProcessHeapAllocate(size_t bytes);
// 0x1be435 (__cdecl): ProcessHeapAllocator::deallocate: HeapFree(GetProcessHeap(), 0, block); the count is unused.
void __cdecl ProcessHeapDeallocate(void *block, size_t count);

// ---- cGZFramework's service containers: rehash, range erase, node frees, element destructors -----------------------
// The rehash 0x143dc2 and the range erase 0x14424b are library matches and have no entry. The node frees and element
// destructors that follow are the framework containers'.
struct FwServiceMapAccess : FwServiceMap {
    using Node = std::_List_node<value_type, void *>;
};
using FwServicePair = FwServiceMap::value_type;                         // std::pair<const uint32_t, FwRef>, 8 bytes
using FwPriorityMap = std::multimap<int32_t, FwServiceEntry>;          // cGZFramework +0x84
using FwPriorityPair = FwPriorityMap::value_type;                       // std::pair<const int32_t, FwServiceEntry>, 12 bytes
// 0x1421bb / 0x1421e1 (ret 4): the elements' destructors as the node frees call them, in the scalar-deleting form:
// the reference released (slot 2, when non-null), then with flags & 1 the 8 / 12 bytes freed. Return the element.
FwServicePair *__fastcall DeleteFwServicePair(FwServicePair *self, void *edx, uint32_t flags);
FwPriorityPair *__fastcall DeleteFwPriorityPair(FwPriorityPair *self, void *edx, uint32_t flags);
// The node frees (__cdecl (node allocator, node)): the element destroyed, then the node freed.
// 0x1419c3: unordered_map<uint32_t, FwRef>'s 16-byte list node.
void __cdecl FreeFwServiceMapNode(std::allocator<FwServiceMapAccess::Node> &alloc, FwServiceMapAccess::Node *node);
// 0x1419de: std::list<FwRef>'s 12-byte node.
void __cdecl FreeFwRefListNode(FwRefList::NodeAlloc &alloc, FwRefList::Node *node);
// 0x1419f9: multimap<int32_t, FwServiceEntry>'s 28-byte tree node.
using FwPriorityNode = std::_Tree_node<FwPriorityPair, void *>;
void __cdecl FreeFwPriorityNode(std::allocator<FwPriorityNode> &alloc, FwPriorityNode *node);

// ---- std::vector<char> ---------------------------------------------------------------------------------------------
// The game's vector<char> as its layout (first, last, end of storage); 0x1af86f is the reallocating resize of a copy
// built with EH frames (the library's catch(...) around the fill and copy; the /O2 copy 0x152c50 is library code).
struct ByteVector {
    char *mFirst;
    char *mLast;
    char *mEnd;
    // 0x1af86f (ret 8: new size, the value-init tag): resize to newSize (above the capacity, as the library's _Resize
    // calls it): length_error past 0x7fffffff; a new block of the library's geometric growth (capacity * 1.5, at least
    // newSize, at most 0x7fffffff), the new tail zeroed, the old elements copied, the old block freed.
    void ResizeReallocate(size_t newSize, const void *valueInitTag);
};
static_assert(sizeof(ByteVector) == 12, "vector<char> layout");

// ---- std::string --------------------------------------------------------------------------------------------------
// 0x15aa90 (__cdecl, hidden result): operator+(std::string &&left, char right): appends right to left and moves left
// into the result (left is left empty). Caller: nGZGraphic4::cPixelFormatYUV::Construct 0x15d230.
std::string __cdecl AppendCharMove(std::string &&left, char right);
struct StringAccess : std::string {
    // 0x1adba0 (ret 8): find(text, offset) (cRZString::Find 0x1ace61).
    size_t FindText(const char *text, size_t offset) const;
    // 0x1af5d6 (ret 8): find_last_of(text, offset) (the command line's program-path step 0x1aeb57).
    size_t FindLastOfText(const char *text, size_t offset) const;
};

} // namespace StdGameTypes
