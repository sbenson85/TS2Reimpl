#pragma once
#include "game/vtable.h"
#include "rz/RZAutoRef.h"

#include <cstdint>

// Helpers shared by the person primitives (lane Y).

// A call through slot `slot` of `obj`'s vtable, __thiscall, the arguments pushed as written.
template <typename R, typename... A> inline R PCall(const void *obj, int slot, A... args)
{
    return VirtualSlot<R(__thiscall *)(void *, A...)>(obj, slot)(const_cast<void *>(obj), args...);
}

[[noreturn]] inline void PRaise(int16_t code)
{
    throw code;
}

// A reference the original keeps in its frame. While the frame is in a state with an unwind action a throw releases it
// (0x145d50: null check, slot 2, noexcept); the explicit releases the code writes out move the frame to a state
// without one.
struct PHeld {
    void *mObject = nullptr;
    bool mArmed = false;
    PHeld() = default;
    PHeld(const PHeld &) = delete;
    PHeld &operator=(const PHeld &) = delete;
    ~PHeld() noexcept
    {
        if (mArmed) reinterpret_cast<RZAutoRef *>(&mObject)->ReleaseUnguarded();
    }
    // The explicit release (null check, then slot 2), after which the unwind no longer releases.
    void ReleaseNow()
    {
        mArmed = false;
        if (mObject) PCall<void>(mObject, 2);
    }
};
