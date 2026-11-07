#include "ui/cHTMLElement.h"

#include <new>

#include "game/game.h"

// The original's HTML module keeps frame pointers (HitTest opens with `push ebp; mov ebp, esp`).
#pragma optimize("y", off)

cHTMLElement *cHTMLElement::DeletingDestructor(uint32_t flags)
{
    mVTable = GameAddress<void>(kVTable);
    if (flags & 1) ::operator delete(this, sizeof(cHTMLElement));
    return this;
}

int32_t cHTMLElement::HitTest(cHTMLElement **hit, int32_t x, int32_t y)
{
    if (mBounds.Contains(x, y)) {
        if (VirtualCall<bool(__thiscall *)(void *), 6>(this)) { // accepts hits?
            *hit = this;
            return 2;
        }
        return 1;
    }
    return 0;
}
