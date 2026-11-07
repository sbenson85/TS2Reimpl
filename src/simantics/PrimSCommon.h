#pragma once
// Helpers shared by the lane S primitive handlers (PrimS*.cpp): vtable calls, param field reads, the operand
// reader and the reference holder the handlers keep in their frames.
#include "game/vtable.h"
#include "rz/RZAutoRef.h"
#include "simantics/PrimS.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace primS {

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

// The operand read: slot 7 of the secondary subobject (this + 4), seven stack arguments (scope, id, two outputs,
// three zeros), the value in ax.
inline int16_t Read(PrimSObject *self, uint32_t scope, uint32_t id)
{
    void *sub = &self->mSecondary;
    return Call<int16_t>(sub, 7, scope, id, 0u, 0u, 0u, 0u, 0u);
}

// The reference the original keeps in its frame: released (slot 2) when the handler returns or throws.
struct Held {
    RZAutoRef mRef{nullptr};
    Held() = default;
    Held(const Held &) = delete;
    Held &operator=(const Held &) = delete;
    ~Held() noexcept { mRef.ReleaseUnguarded(); }
};

// Takes the reference the way the original does: AddRef (slot 1) first, the holder only after that.
inline void *AddRefed(void *object)
{
    if (object) Call<uint32_t>(object, 1);
    return object;
}

} // namespace primS
