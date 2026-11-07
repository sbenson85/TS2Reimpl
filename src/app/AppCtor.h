#pragma once
// Teardown helpers of the application object's containers and of string lists (RVAs 1.18.0.187). The application class
// itself (constructor 0xc26147, destructors 0xc266b9 / 0xc267d9) is ts::cTSAppFW in ts/cTSAppFW.h (lane FG); its list at
// +0x78 holds the AutoRefElement below.
#include "app/AppBase.h"
#include "app/ExceptionReporter.h"
#include "rz/cRZCmdLine.h"
#include "rz/cRZMutex.h"
#include "rz/RZAutoRef.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <set>

namespace appctor {

// The element of the application's list at +0x78: a counted reference. Its node (12 bytes: next,
// previous, the pointer) is freed by 0x213e55, which releases the object (slot 2, through the
// holder at 0x21450d: no handler, noexcept) before the sized delete; the list's _Tidy (0x217d42)
// and _Free_non_head (0x213db2) are the library's. The names in db/ call it a list of unsigned
// integers: the element has this destructor.
struct AutoRefElement {
    RZAutoRef mRef;
    AutoRefElement() = default;
    explicit AutoRefElement(void *object) : mRef{object} {}
    AutoRefElement(const AutoRefElement &) = delete;
    ~AutoRefElement() noexcept { mRef.ReleaseUnguarded(); }
};
static_assert(sizeof(AutoRefElement) == 4, "AutoRefElement layout");

// One node of the tree-of-lists the cTSAppCommonImpl containers hold (0x20 bytes): the usual
// ordered-container node header (left, parent, right, colour byte, nil byte at +0xd), a key of
// unidentified size at +0x10, and the head of a list of cRZString-sized elements at +0x18.
struct ListNode;
struct TreeNode {
    TreeNode *mLeft;    // +0x00
    TreeNode *mParent;  // +0x04
    TreeNode *mRight;   // +0x08
    uint8_t mColor;     // +0x0c
    uint8_t mIsNil;     // +0x0d
    uint8_t pad0e[2];
    uint32_t mKey[2];   // +0x10
    ListNode *mList;    // +0x18: the list's head node (see ListNode)
    uint32_t mListSize; // +0x1c (never touched by the teardown)
    static constexpr uint32_t kSize = 0x20;
};
static_assert(sizeof(TreeNode) == TreeNode::kSize, "TreeNode size");
static_assert(offsetof(TreeNode, mIsNil) == 0xd, "TreeNode layout");

// A node of the circular doubly linked list at TreeNode::mList: next, previous, then an element
// whose first 0x20 bytes are a cRZString (destroyed through its vtable-independent teardown). 0x2c
// bytes, head node included (the head has the same size).
struct ListNode {
    ListNode *mNext;     // +0x00
    ListNode *mPrevious; // +0x04
    cRZString mText;     // +0x08
    uint32_t mWord28;    // +0x28 (never touched by the teardown)
    static constexpr uint32_t kSize = 0x2c;
};
static_assert(sizeof(ListNode) == ListNode::kSize, "ListNode size");

// 0x1d8450 (__cdecl, two arguments, the first unused): the cRZString teardown for a list element:
// vtable reset to the string's own, heap buffer freed, string emptied.
void __cdecl DestroyElement(void *unused, cRZString *element);

// 0x1d5e50 (__cdecl, two arguments, the first unused): frees the nodes of the list whose head is
// `head` (the head itself stays). The previous node of the head gets a null `next`, which makes the
// chain null-terminated; every element is destroyed (0x1d8450) and its node freed (sized, 0x2c).
void __cdecl DestroyListNodes(void *unused, ListNode *head);

// 0xca49f0 (__thiscall, ret 8, `this` and the first argument unused): destroys the subtree at
// `node` (the container's head node stands for "nil", detected by the nil byte): for each node
// the right subtree, then its list (nodes, then the head, 0x2c) and the node itself (0x20), then it
// continues with the left subtree.
void __fastcall EraseSubtree(void *container, void *edx, void *unused, TreeNode *node);

} // namespace appctor
