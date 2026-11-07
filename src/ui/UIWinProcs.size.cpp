#include "ui/UIWinProcs.h"
#include "game/vtable.h"
#include "rz/RZFrameworkHelpers.h"

namespace {

template <typename T> T &At(void *self, uintptr_t offset)
{
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(self) + offset);
}

// The std::list nodes in these objects use the MSVC 14.40 layout: next, prev, then the value.
struct ListNode {
    ListNode *next;
    ListNode *prev;
    void *value;
};

// Slot 26 of the cTSWinProcGrid vtable (0x68): a one-argument call, result unused.
void CallSlot26(void *self, uint32_t value)
{
    VirtualCall<void(__thiscall *)(void *, uint32_t), 26>(self, value);
}

// Slot 45 of the cTSWinProcGrid vtable (0xb4): two arguments, the first pushed last, result unused.
void CallSlot45(void *self, uint32_t first, uint32_t second)
{
    VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t), 45>(self, first, second);
}

// Slot 3 (0xc) of the child interface at +8, called with the child as `this`.
void CallChildSlot3(void *self)
{
    void *child = static_cast<uint8_t *>(self) + 8;
    VirtualCall<void(__thiscall *)(void *), 3>(child);
}

// The branch the original takes on the message id (msg+8). The compare chain is: a value above 0x3304 is
// checked against 0x3305, 0x3309, then 0x5504 (the shared test at 0x748b32), 0x5505 and 0x5509; a value up to
// 0x3304 against 0x3304, 0x1004, 0x1005, 0x1009, then 0x2004 (the same shared test), 0x2005 and 0x2009.
enum class Vf39Action { None, Dec, Inc, Pair11, Pair01 };

Vf39Action ClassifyVf39(uint32_t id)
{
    if (id > 0x3304u) {
        if (id == 0x3305u) return Vf39Action::Inc;
        if (id == 0x3309u) return Vf39Action::Pair11;
        if (id == 0x5504u) return Vf39Action::Dec;
        if (id == 0x5505u) return Vf39Action::Inc;
        if (id == 0x5509u) return Vf39Action::Pair01;
        return Vf39Action::None;
    }
    if (id == 0x3304u || id == 0x1004u) return Vf39Action::Dec;
    if (id == 0x1005u) return Vf39Action::Inc;
    if (id == 0x1009u) return Vf39Action::Pair11;
    if (id == 0x2004u) return Vf39Action::Dec;
    if (id == 0x2005u) return Vf39Action::Inc;
    if (id == 0x2009u) return Vf39Action::Pair01;
    return Vf39Action::None;
}

} // namespace

// 0x936a5c (16 bytes, ret 4): mov eax, [esp+4]; test [ecx+0xfc], eax; setne al; ret 4
bool cGZWinGrid::TestFlagsFC(uint32_t mask)
{
    return (At<uint32_t>(this, 0xfc) & mask) != 0;
}

// 0x9529e5 (11 bytes): add ecx, -0xfc; mov eax, [ecx]; jmp [eax+0x14]
uint32_t cGZWinMsgBoxAt252::ForwardToSlot5()
{
    void *window = reinterpret_cast<uint8_t *>(this) - 0xfc;
    return VirtualCall<uint32_t(__thiscall *)(void *), 5>(window);
}

// 0x75de32 (ret 8): push esi; mov esi, ecx; push 0; lea ecx, [esi+0x10]; call 0x142080; the same for [esi+0xc]
// and [esi+8]; mov al, 1; ret 8
bool cTSWinProcGridThumbnail::Vf7(uint32_t, uint32_t)
{
    reinterpret_cast<RZUnknownSlot *>(reinterpret_cast<uint8_t *>(this) + 0x10)->Assign(nullptr);
    reinterpret_cast<RZUnknownSlot *>(reinterpret_cast<uint8_t *>(this) + 0xc)->Assign(nullptr);
    reinterpret_cast<RZUnknownSlot *>(reinterpret_cast<uint8_t *>(this) + 0x8)->Assign(nullptr);
    return true;
}

// 0x748af4 (150 bytes, ret 8): reads the id at [arg2+8] and dispatches (see ClassifyVf39). The first argument
// is not used. Returns al = 1 for the handled ids and al = 0 otherwise; only al is the result.
bool cTSWinProcGrid::Vf39(uint32_t, const void *msg)
{
    const uint32_t id = *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(msg) + 8);
    switch (ClassifyVf39(id)) {
    case Vf39Action::None:
        return false;
    case Vf39Action::Dec:
        CallSlot26(this, At<uint32_t>(this, 0x20) - 1);
        return true;
    case Vf39Action::Inc:
        CallSlot26(this, At<uint32_t>(this, 0x20) + 1);
        return true;
    case Vf39Action::Pair11:
        CallSlot45(this, 1, 1);
        CallChildSlot3(this);
        return true;
    case Vf39Action::Pair01:
        CallSlot45(this, 0, 1);
        CallChildSlot3(this);
        return true;
    }
    return false;
}

// 0x7594a3 (ret 4). The original's path where the flag is clear, or the owner's slot 86 is false, returns
// without setting eax, so the return value is undefined there and the function returns nothing.
void nTSUICAS::cTSUICASComponent::Vf360_8(uint32_t arg)
{
    if (At<uint8_t>(this, 0x68) == 0) return;
    void *owner = reinterpret_cast<uint8_t *>(this) - 0x168;
    if (VirtualCall<uint8_t(__thiscall *)(void *), 86>(owner) == 0) return;

    // The head pointer is read again at the end of each pass, as the original's `cmp esi, [ebx+0x70]` does.
    ListNode *head = At<ListNode *>(this, 0x70);
    ListNode *node = head->next;
    if (node == head) return;
    do {
        void *component = node->value;
        if (component != nullptr && VirtualCall<uint8_t(__thiscall *)(void *), 9>(component) != 0) {
            // The original does not check slot 10's result for null (it reads its vtable at once), so a null
            // result faults there and here alike; no test uses one.
            void *o2 = VirtualCall<void *(__thiscall *)(void *), 10>(component);
            if (VirtualCall<uint8_t(__thiscall *)(void *), 86>(o2) != 0)
                VirtualCall<void(__thiscall *)(void *, uint32_t), 8>(component, arg);
        }
        node = node->next;
    } while (node != At<ListNode *>(this, 0x70));
}
