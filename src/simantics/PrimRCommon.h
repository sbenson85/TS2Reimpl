#pragma once
// Shared helpers of the lane R primitive handlers (PrimR*.cpp): virtual calls by slot, unaligned param reads, the
// operand reader and the reference holders the handlers keep in their frames.
#include "game/vtable.h"
#include "rz/RZAutoRef.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/cRZString.h"
#include "simantics/PrimExpression.h"
#include "simantics/PrimR.h"

#include <cstdint>
#include <cstring>

namespace primr {

// A call through slot `slot` of `obj`'s vtable, __thiscall, the arguments pushed as written.
template <typename R, typename... A> inline R Call(const void *obj, int slot, A... args)
{
    return VirtualSlot<R(__thiscall *)(void *, A...)>(obj, slot)(const_cast<void *>(obj), args...);
}

inline uint8_t Byte(const void *p, size_t off)
{
    return static_cast<const uint8_t *>(p)[off];
}

inline uint16_t Word(const void *p, size_t off)
{
    uint16_t v;
    std::memcpy(&v, static_cast<const uint8_t *>(p) + off, sizeof(v));
    return v;
}

inline uint32_t Dword(const void *p, size_t off)
{
    uint32_t v;
    std::memcpy(&v, static_cast<const uint8_t *>(p) + off, sizeof(v));
    return v;
}

[[noreturn]] inline void Raise(int16_t code)
{
    throw code;
}

constexpr int kReadSlot = 7;

// The operand read: slot 7 of the secondary subobject (this + 4), seven stack arguments (scope, id, the destination
// run, a float destination, three zeros as the callers pass them), the value in ax.
inline int16_t Read(PrimRObject *self, uint32_t scope, uint32_t id, SimDataRef *dest, float **floatDest)
{
    void *sub = &self->mSecondary;
    return Call<int16_t>(sub, kReadSlot, scope, id, dest, floatDest, static_cast<uint32_t *>(nullptr), 0u, 0u);
}

// A reference the original keeps in its frame (a smart pointer built with 0x57c2f0, assigned with 0x142080):
// released (slot 2) when the handler returns or throws.
struct Held {
    RZAutoRef mRef{nullptr};
    Held() = default;
    Held(const Held &) = delete;
    Held &operator=(const Held &) = delete;
    ~Held() noexcept { mRef.ReleaseUnguarded(); }
    void Assign(void *object) { reinterpret_cast<RZUnknownSlot *>(&mRef)->Assign(object); }
    void *Get() const { return mRef.mObject; }
};

// A cRZString on the stack, destroyed (0x141193) when the handler returns or throws.
struct LocalString {
    cRZString mString;
    LocalString() { mString.Construct(); }
    LocalString(const LocalString &) = delete;
    ~LocalString() { mString.Destruct(); }
};

} // namespace primr
