#pragma once
// Helpers shared by the standalone-shape difftests (docs/standalone-shape.md, "Tests"; docs/lane/test.md).
//
// A class in the standalone shape is tested on two routes besides the original side:
// - the vtable route: our object built by our constructor, the function called through our own vtable, in the slot where the
//   game's vtable of that face holds the original RVA (SlotOf: a slot-order mistake calls the wrong function);
// - the adapter route: an object with the game's vtables (built by the original constructor, as when the constructor hook is
//   off) and the RVA hook's adapter called, with an arity-matched trap wherever the game's vtable holds that RVA (TrapFor: an
//   adapter that dispatched virtually lands there; in game it would be its own hook, an endless loop).
// Vtable words are the only bytes normalised (VtCode): the game's, ours and the test's copy of face f compare as one code.
//
// What stays in each fixture: the face table (offsets, game vtable RVAs, slot counts), how the side's object is built, which
// vtables the object carries (copies with fakes patched over them), the trap's side effect, and the Pair loop. This header has
// the pieces every fixture wrote the same way. Fixtures using it: tests/ts_lot_info_shape.h, tests/ts_neighborhood_shape.h,
// tests/ts_family.h + ts_family_world.cpp, tests/ts_family_manager.cpp, tests/ts_neighbor.cpp; traps only: ts_sim_dna,
// ts_relationship_table / _entry, rz_system_service, gz_property_set_resource, gz_property_set_uint32_binary (and see
// docs/lane/test.md). Also the code addresses of our members (MemberCode, VirtualMemberCode, CodeOf) that stand-ins patch
// (tests/code_patch.h: CodePatch, StandIn).
#include "game/game.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

namespace shape {

// ---- words and faces ----

inline uint32_t P(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// The dword at `offset` in `object`.
inline uint32_t Word(const void *object, uint32_t offset)
{
    return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(object) + offset);
}

// The vtable word of the face at `offset` (assignable).
inline void **&VTableAt(void *object, uint32_t offset)
{
    return *reinterpret_cast<void ***>(static_cast<uint8_t *>(object) + offset);
}

// The game's vtable at `rva`.
inline void **GameVTable(uint32_t rva) { return GameAddress<void *>(rva); }

// The slot of the game's vtable at `vtableRva` (of `slots` slots) that holds the function at `rva`; ~0u when none does.
inline unsigned SlotOf(uint32_t vtableRva, unsigned slots, uint32_t rva)
{
    void *const *game = GameVTable(vtableRva);
    const void *target = GameAddress<void>(rva);
    for (unsigned s = 0; s < slots; ++s)
        if (game[s] == target) return s;
    return ~0u;
}

// Our class's vtables, read once from an object our constructor built on a scratch buffer (then destroyed): `face[f]` is
// the word our constructor stores at `offset[f]`, `destroyed` the +0 word our destructor leaves (a base class's vtable).
template <unsigned N> struct OurTables {
    void **face[N];
    void **destroyed;
};
template <class T, unsigned N> const OurTables<N> &Ours(const uint32_t (&offset)[N])
{
    static OurTables<N> v{};
    static bool done = false;
    if (!done) {
        alignas(8) static uint8_t buffer[sizeof(T)];
        T *object = ::new (buffer) T;
        for (unsigned f = 0; f < N; ++f) v.face[f] = VTableAt(buffer, offset[f]);
        object->T::~T();
        v.destroyed = VTableAt(buffer, 0);
        done = true;
    }
    return v;
}

// ---- vtable words ----

// Lists of per-face vtables for FaceOf / VtCode: anything indexable by face giving a pointer or a dword, or GameTables (the
// game's vtables from their RVAs).
struct GameTables {
    const uint32_t *rva;
    void **operator[](unsigned face) const { return GameVTable(rva[face]); }
};
inline uint32_t AsWord(const void *p) { return P(p); }
inline uint32_t AsWord(uint32_t w) { return w; }

// The face whose vtable (in any of the lists) `word` is; ~0u when none.
template <class... Lists> unsigned FaceOf(uint32_t word, unsigned faces, const Lists &...lists)
{
    for (unsigned f = 0; f < faces; ++f)
        if (((word == AsWord(lists[f])) || ...)) return f;
    return ~0u;
}

constexpr uint32_t kFaceCode = 0xd0000000u;

// A vtable word as a code: kFaceCode | f when it is face f's vtable in any of the lists, otherwise the word as is.
template <class... Lists> uint32_t VtCode(uint32_t word, unsigned faces, const Lists &...lists)
{
    const unsigned f = FaceOf(word, faces, lists...);
    return f == ~0u ? word : kFaceCode | f;
}

// ---- code addresses of our members ----
//
// MemberCode(&Class::Member): the address of the body of one of our non-virtual member functions (for CodePatch / StandIn
// in tests/code_patch.h, or to compare with what a call site reaches). An MSVC member pointer's first word is the code
// address for every inheritance kind; with several bases the pointer is two words (code, this adjustment) and wider for
// virtual bases, and only the first word is read. (src/hook/hooks.h's MemberAddress accepts single-word pointers only.)
//
// Virtual members: the first word of `&Class::VirtualFn` is not the body but the compiler's vcall thunk (`mov eax, [ecx];
// jmp [eax + slot * 4]`, lane FM), which dispatches through whatever object it is called on; patching it does not stand in
// for the body. MemberCode refuses those (it prints the member's thunk and aborts the run: no test can mean to patch a
// thunk). For a virtual member use VirtualMemberCode(&Class::VirtualFn, vtable): it reads the slot from the thunk and
// returns `vtable[slot]`, the body that vtable holds (pass our class's vtable of the face that declares the member, e.g.
// from Ours<T>(offsets).face[f]; with several bases the slot is relative to that face's vtable).
//
// Caveat: a non-virtual member whose whole body compiles to `mov eax, [ecx]; jmp [eax + n]` (a virtual tail call) looks
// like a thunk and is refused too; none is known. Our builds don't link incrementally, so there is no jump stub in front.

// The vtable byte offset a vcall thunk at `code` jumps through; false when the bytes are not a vcall thunk.
inline bool VcallThunkOffset(const void *code, uint32_t *offset)
{
    const auto *b = static_cast<const uint8_t *>(code);
    if (b[0] != 0x8b || b[1] != 0x01 || b[2] != 0xff) return false; // mov eax, [ecx]; jmp ...
    switch (b[3]) {
    case 0x20: *offset = 0; return true;                                            // jmp [eax]
    case 0x60: *offset = uint32_t(int32_t(int8_t(b[4]))); return true;              // jmp [eax + disp8]
    case 0xa0: std::memcpy(offset, b + 4, 4); return true;                          // jmp [eax + disp32]
    default: return false;
    }
}

template <class M> void *MemberCodeWord(M member)
{
    static_assert(std::is_member_function_pointer_v<M>, "MemberCode takes a member function pointer");
    static_assert(sizeof(M) >= sizeof(void *), "member pointer narrower than a code address");
    void *code;
    std::memcpy(&code, &member, sizeof(code));
    return code;
}

// The body of one of our non-virtual members (aborts on a virtual one: see above).
template <class M> void *MemberCode(M member)
{
    void *code = MemberCodeWord(member);
    uint32_t offset;
    if (VcallThunkOffset(code, &offset)) {
        std::fprintf(stderr, "MemberCode: %p is a vcall thunk (slot %u), not a body; use VirtualMemberCode\n", code, offset / 4);
        std::fflush(stderr);
        std::abort();
    }
    return code;
}

// The body `vtable` holds for one of our virtual members (aborts when the member is not virtual).
template <class M> void *VirtualMemberCode(M member, void *const *vtable)
{
    void *code = MemberCodeWord(member);
    uint32_t offset;
    if (!VcallThunkOffset(code, &offset)) {
        std::fprintf(stderr, "VirtualMemberCode: %p is not a vcall thunk; use MemberCode\n", code);
        std::fflush(stderr);
        std::abort();
    }
    return vtable[offset / 4];
}

// A code address of a function pointer or (via MemberCode) a non-virtual member pointer.
template <class F> void *CodeOf(F f)
{
    if constexpr (std::is_member_function_pointer_v<F>)
        return MemberCode(f);
    else if constexpr (std::is_pointer_v<F>)
        return const_cast<void *>(reinterpret_cast<const void *>(f));
    else
        static_assert(std::is_pointer_v<F>, "CodeOf takes a function, member or data pointer");
}

// ---- calls ----

// The stack words of an argument list (what the callee's `ret N` pops, N / 4).
template <class... A> constexpr unsigned Words() { return (0u + ... + unsigned((sizeof(A) + 3) / 4)); }

// A __thiscall function pointer type's `this` type and stack words.
template <class Fn> struct Sig;
template <class R, class S, class... A> struct Sig<R(__thiscall *)(S, A...)> {
    using Self = S;
    using Result = R;
    static constexpr unsigned kWords = Words<A...>();
};

// The face at `offset` of `object`, as the `this` type of Fn.
template <class Fn> typename Sig<Fn>::Self SelfAt(void *object, uint32_t offset)
{
    return reinterpret_cast<typename Sig<Fn>::Self>(static_cast<uint8_t *>(object) + offset);
}

// Calls `fn` and then `after()`, returning what `fn` returned (void allowed).
template <class F, class After> auto ThenAfter(F &&fn, After &&after)
{
    if constexpr (std::is_void_v<decltype(fn())>) {
        fn();
        after();
    } else {
        auto r = fn();
        after();
        return r;
    }
}

// ---- traps ----
//
// An arity-matched trap: a __fastcall stand-in for a __thiscall slot that pops `words` stack words, as the slot's `ret N`
// does (a trap of the wrong arity corrupts the stack when it is hit). What a hit does is the fixture's: `Hit(self, words)`
// (count it, log it) and its result is the trap's return value. Count<counter> is the usual hit.
using TrapHit = uint32_t (*)(void *self, unsigned words);
template <unsigned> using StackWord = uint32_t;

constexpr unsigned kMaxTrapWords = 7; // cTSPersistSystem has seven-word slots (lane GD)

template <TrapHit Hit, unsigned... I> uint32_t __fastcall TrapOf(void *self, void *, StackWord<I>...)
{
    return Hit(self, sizeof...(I));
}
template <TrapHit Hit, unsigned... I> void *TrapAddress(std::integer_sequence<unsigned, I...>)
{
    return reinterpret_cast<void *>(&TrapOf<Hit, I...>);
}
template <TrapHit Hit, unsigned... W> void *PickTrap(unsigned words, std::integer_sequence<unsigned, W...>)
{
    void *const traps[] = {TrapAddress<Hit>(std::make_integer_sequence<unsigned, W>())...};
    return traps[words < sizeof...(W) ? words : sizeof...(W) - 1];
}
// The trap popping `words` stack words (kMaxTrapWords for more).
template <TrapHit Hit> void *TrapFor(unsigned words)
{
    return PickTrap<Hit>(words, std::make_integer_sequence<unsigned, kMaxTrapWords + 1>());
}

// The usual hit: counts in `counter` and returns the new count.
template <auto &Counter> uint32_t Count(void *, unsigned)
{
    return uint32_t(++Counter);
}

// A forwarding trap: the hit, then the call goes on to `gForwardTo` (what the copy held in the slot: the side's own function
// or a fixture fake) with the same arguments, and its result is returned. For slots whose body legitimately re-enters its own
// slot (cTSFamily's InsertKeyIntoGroup1): installed on every side, the legitimate calls count alike and an adapter that
// dispatched virtually counts once more. One target at a time.
inline void *gForwardTo = nullptr;
template <TrapHit Hit, unsigned... I> uint32_t __fastcall ForwardOf(void *self, void *, StackWord<I>... w)
{
    Hit(self, sizeof...(I));
    return reinterpret_cast<uint32_t(__fastcall *)(void *, void *, StackWord<I>...)>(gForwardTo)(self, nullptr, w...);
}
template <TrapHit Hit, unsigned... I> void *ForwardAddress(std::integer_sequence<unsigned, I...>)
{
    return reinterpret_cast<void *>(&ForwardOf<Hit, I...>);
}
template <TrapHit Hit, unsigned... W> void *PickForward(unsigned words, std::integer_sequence<unsigned, W...>)
{
    void *const traps[] = {ForwardAddress<Hit>(std::make_integer_sequence<unsigned, W>())...};
    return traps[words < sizeof...(W) ? words : sizeof...(W) - 1];
}
template <TrapHit Hit> void *ForwardTrapFor(unsigned words)
{
    return PickForward<Hit>(words, std::make_integer_sequence<unsigned, kMaxTrapWords + 1>());
}

} // namespace shape
