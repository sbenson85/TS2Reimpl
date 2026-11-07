// cInputEditFilter (lane MC): src/gz/cInputEditFilter.h. Built for speed, as the game's (matchcheck: /O2).
#include "gz/cInputEditFilter.h"

#include "game/game.h"
#include "game/vtable.h"
#include "ifaces/cIGZWin.h"

namespace {

constexpr uint32_t kUnknownIid = 1;
constexpr uint32_t kCloseMessage = 6;
constexpr uint32_t kKeyMessage = 4;
constexpr uint32_t kEscape = 0x1b;
constexpr uint32_t kBackQuote = 0x60;

// The message handed to the window's parent.
void ToParent(ifaces::cIGZWin *window, cInputEditFilter::Message *message)
{
    static_cast<ifaces::cIGZWin *>(window->GetParent())->DoMessage(message);
}

} // namespace

bool cInputEditFilter::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kUnknownIid && riid != kIid) return false;
    *out = this;
    using AddRefFn = uint32_t(__thiscall *)(void *);
    VirtualCall<AddRefFn, 1>(this);
    return true;
}

bool cInputEditFilter::Filter(ifaces::cIGZWin *window, Message *message)
{
    if (message->type == kCloseMessage) {
        ToParent(window, message);
        return false;
    }
    if (message->type != kKeyMessage || (message->key != kEscape && message->key != kBackQuote)) return false;
    ToParent(window, message);
    return true;
}

cInputEditFilter *cInputEditFilter::DeletingDestructor(uint32_t flags)
{
    mRef.mVTable = GameAddress<void>(kRefVTable);
    mVTable = GameAddress<void>(kVTable);
    mRef.Destruct();
    if (flags & 1) ::operator delete(this, sizeof(*this));
    return this;
}
