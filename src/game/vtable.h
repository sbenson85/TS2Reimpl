#pragma once
#include <cstdint>
#include <bit>
#include <type_traits>

// Calls through an object's own vtable, as the original code does with `call [eax+N]`: slot N/4 of
// the vtable at offset 0 of `object` (pass the subobject for secondary bases). Use this rather than
// C++ virtual calls when the object may be a game object carrying a game vtable.
//   VirtualSlot<uint32_t(__thiscall *)(void *)>(this, 1)(this);   // AddRef
template <typename Fn> __forceinline Fn VirtualSlot(const void *object, int slot)
{
    return reinterpret_cast<Fn>((*static_cast<void *const *const *>(object))[slot]);
}

// Views a game object (or subobject: pass the address its vtable pointer sits at) as one of the
// generated interfaces (`#include "ifaces/<Name>.h"`, tools/ifacegen.py), so calls through its vtable
// are named virtual calls: As<ifaces::PersonInputHolder>(holder)->IsWaiting(). Prefer this to
// VirtualSlot once the slot has a method fact in db/methods/.
template <typename Iface> __forceinline Iface *As(const void *object)
{
    return static_cast<Iface *>(const_cast<void *>(object));
}

// VirtualCall<Fn, kSlot>(object, args...): the same call as VirtualSlot<Fn>(object, kSlot)(object, args...)
// for a slot known at compile time, written as a real C++ virtual call so that MSVC emits the original's
// `call [reg+off]` (a call through a function pointer always loads it into a register first). The
// pointer is viewed as a class whose slot kSlot has Fn's signature (kSlot pure virtuals in front of it);
// nothing is ever constructed from these classes. These helpers are __forceinline: the original has no such
// helpers, and size-built files (/O1, `*.size.cpp`) would otherwise call them out of line. Results narrower than 32 bits are declared as 32-bit
// and truncated: MSVC loads the slot into a register for a call returning bool/char, and the callee
// leaves the value in al either way.
namespace vtable_detail {
template <int N> struct Pad : Pad<N - 1> {
    virtual void pad(Pad *) = 0; // one more slot (a distinct signature, so no override)
};
template <> struct Pad<0> {};

template <typename R, bool = std::is_integral_v<R>> struct WideOf {
    using type = R;
};
template <typename R> struct WideOf<R, true> {
    using type = std::conditional_t<(sizeof(R) < 4), uint32_t, R>;
};
template <typename R> using Wide = typename WideOf<R>::type;

template <int N, typename Fn> struct Slot;
template <int N, typename R, typename T, typename... A> struct Slot<N, R(__thiscall *)(T, A...)> : Pad<N> {
    using Result = R;
    virtual Wide<R> __thiscall call(A...) = 0;
};
template <int N, typename R, typename T, typename... A> struct Slot<N, R(__stdcall *)(T, A...)> : Pad<N> {
    using Result = R;
    virtual Wide<R> __stdcall call(A...) = 0;
};
template <int N, typename R, typename T, typename... A> struct Slot<N, R(__cdecl *)(T, A...)> : Pad<N> {
    using Result = R;
    virtual Wide<R> __cdecl call(A...) = 0;
};
} // namespace vtable_detail

template <typename Fn, auto kSlot, typename... Args>
__forceinline typename vtable_detail::Slot<static_cast<int>(kSlot), Fn>::Result VirtualCall(const void *object, Args &&...args)
{
    using S = vtable_detail::Slot<static_cast<int>(kSlot), Fn>;
    using R = typename S::Result;
    if constexpr (std::is_same_v<R, bool>) // al as it is, like a bool-returning call: not normalised to 0/1
        return std::bit_cast<bool>(static_cast<uint8_t>(
            static_cast<S *>(const_cast<void *>(object))->call(static_cast<Args &&>(args)...)));
    else
        return static_cast<R>(static_cast<S *>(const_cast<void *>(object))->call(static_cast<Args &&>(args)...));
}

// RawSlot<N, R>(object, args...): VirtualCall of slot N as a __thiscall returning R (default uint32_t) and taking the
// arguments' own types, for slots nobody has named yet. Still a raw slot call (tools/slotcalls.py lists them,
// tools/guard.py warns on new ones): once a slot's purpose is known, give it a method fact and call it by name
// (docs/lane/write.md). Not __forceinline, like the file-local copies it replaced (their code must not change).
template <int S, class R = uint32_t, class... A> R RawSlot(const void *object, A... a)
{
    return VirtualCall<R(__thiscall *)(void *, A...), S>(object, a...);
}
