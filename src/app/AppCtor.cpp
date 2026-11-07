#include "app/AppCtor.h"

#include <new>

namespace appctor {

// ---- 0x1d8450, 0x1d5e50 ----

void __cdecl DestroyElement(void *, cRZString *element)
{
    element->Destruct();
}

void __cdecl DestroyListNodes(void *, ListNode *head)
{
    head->mPrevious->mNext = nullptr;
    for (ListNode *node = head->mNext; node;) {
        ListNode *next = node->mNext;
        DestroyElement(nullptr, &node->mText);
        ::operator delete(node, ListNode::kSize);
        node = next;
    }
}

// ---- 0xca49f0 ----

void __fastcall EraseSubtree(void *container, void *, void *unused, TreeNode *node)
{
    while (!node->mIsNil) {
        EraseSubtree(container, nullptr, unused, node->mRight);
        TreeNode *left = node->mLeft;
        DestroyListNodes(&node->mList, node->mList);
        ::operator delete(node->mList, ListNode::kSize);
        ::operator delete(node, TreeNode::kSize);
        node = left;
    }
}

} // namespace appctor
