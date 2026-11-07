#pragma once
// Hook table: each entry redirects an original function (by RVA) to a reimplementation with a
// 5-byte JMP. Before patching, the site is checked against a CRC32 of the original bytes (only the
// checksum is stored, never the bytes), so a different game version or a site already patched by
// another mod is skipped instead of corrupted.
//
// Each group of hooks lives in its own file under src/hook/tables/ and registers itself:
//   namespace { const HookSpec kHooks[] = {...}; REGISTER_HOOKS(kHooks); }
// so independent pieces of work never edit the same table. tools/hookspec.py writes the entries.
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

struct HookSpec {
    const char *name;    // also the key in ts2reimpl.ini [Hooks] (name=0 disables it)
    uint32_t rva;        // original function, 1.18.0.187
    uint32_t checkLen;   // bytes covered by checkCrc
    uint32_t checkCrc;   // CRC32 of the original bytes [rva, rva+checkLen)
    void *replacement;
};

// Address of a non-virtual member function, for use as a hook target.
//
// MSVC member pointers: a single-inheritance class's is the code address; a multiple-inheritance class's is
// {code address, this-adjustment} (8 bytes); virtual inheritance adds a vbtable index (12 or 16 bytes, refused).
//
//   MemberAddress(&cFoo::Bar)    single inheritance only (a multiple-inheritance pointer fails to compile, naming
//                                the form below).
//   MemberAddress<&cFoo::Bar>()  single or multiple inheritance. A multiple-inheritance member is accepted when its
//                                this-adjustment is zero: the function takes the object's own address as `this`
//                                (members declared in the class itself, or inherited along its primary base path),
//                                which is what a hooked site passes. A non-zero adjustment (a secondary base's member
//                                reached through a conversion) and a virtual function (the pointer is a vcall thunk,
//                                which would dispatch through the game object's vtable) fail to compile.
//
// The check is at compile time without looking inside the member pointer (MSVC refuses bit_cast of one in a constant
// expression, and any runtime branch on it turns the kHooks entry into a dynamic initializer, which matchcheck can't
// read): the member pointer is a template argument, and MSVC's decorated name of a function instantiated with it
// (__FUNCDNAME__, a constant) spells the adjustment out: `$M<type>H<member's symbol><adjustment>`, followed here by a
// sentinel argument `$0FKFK@` (0x5a5a). An encoded number is a digit '0'-'9' for 1-10, else hex digits 'A'-'P' and
// '@' ("A@" is 0, "BA@" 16); a member function's symbol ends in 'Z', a vcall thunk's is `??_9...`. A name the parse
// doesn't recognise (MSVC hashes decorated names over 4096 characters) fails to compile too: hook an adapter then.
namespace hook_detail {
inline constexpr int kAdjustmentUnreadable = -1, kAdjustmentVirtual = -2;

template <auto Pmf, int Sentinel> constexpr const char *DecoratedName() { return __FUNCDNAME__; }

// The this-adjustment spelled in DecoratedName<Pmf, 0x5a5a>(), or kAdjustmentVirtual / kAdjustmentUnreadable.
constexpr int ThisAdjustmentFromDecoratedName(const char *s)
{
    int n = 0;
    while (s[n]) ++n;
    constexpr char kSentinel[] = "$0FKFK@";
    constexpr int kLen = sizeof(kSentinel) - 1;
    int at = -1;
    for (int i = 0; at < 0 && i + kLen <= n; ++i) {
        int k = 0;
        while (k < kLen && s[i + k] == kSentinel[k]) ++k;
        if (k == kLen) at = i;
    }
    if (at < 2) return kAdjustmentUnreadable;
    for (int i = 0; i + 4 <= at; ++i)
        if (s[i] == '?' && s[i + 1] == '?' && s[i + 2] == '_' && s[i + 3] == '9') return kAdjustmentVirtual;
    const char last = s[at - 1];
    if (last >= '0' && last <= '9') return last - '0' + 1;
    if (last != '@') return kAdjustmentUnreadable;
    int start = at - 2;
    while (start >= 0 && s[start] >= 'A' && s[start] <= 'P') --start;
    if (start == at - 2 || start < 0 || s[start] != 'Z') return kAdjustmentUnreadable; // the symbol ends in 'Z'
    int value = 0;
    for (int i = start + 1; i <= at - 2; ++i) value = value * 16 + (s[i] - 'A');
    return value;
}

template <auto Pmf> constexpr int ThisAdjustment()
{
    return ThisAdjustmentFromDecoratedName(DecoratedName<Pmf, 0x5a5a>());
}
} // namespace hook_detail

template <typename T> void *MemberAddress(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *),
                  "a multiple-inheritance member pointer: use MemberAddress<&C::f>(), which checks its this-adjustment");
    void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

template <auto Pmf> void *MemberAddress()
{
    using T = decltype(Pmf);
    static_assert(std::is_member_function_pointer_v<T>, "MemberAddress<P>() takes a member function pointer");
    static_assert(sizeof(T) == sizeof(void *) || sizeof(T) == 2 * sizeof(void *),
                  "virtual-inheritance (or incomplete-class) member pointers are not supported: hook an adapter");
    if constexpr (sizeof(T) != sizeof(void *)) {
        constexpr int adjustment = hook_detail::ThisAdjustment<Pmf>();
        static_assert(adjustment != hook_detail::kAdjustmentVirtual,
                      "MemberAddress<P>(): a virtual function (the member pointer is a vcall thunk): hook an adapter");
        static_assert(adjustment != hook_detail::kAdjustmentUnreadable,
                      "MemberAddress<P>(): can't read the this-adjustment from the decorated name: hook an adapter");
        static_assert(adjustment == 0,
                      "MemberAddress<P>(): a non-zero this-adjustment (a secondary base's member): hook an adapter");
    }
    auto pmf = Pmf;
    void *p;
    std::memcpy(&p, &pmf, sizeof(p)); // the code address is the first word of both forms
    return p;
}

uint32_t Crc32(const void *data, size_t len);

// Expected identity of the game executable the RVAs belong to.
bool IsSupportedGame(const uint8_t *base);

// Installs every enabled hook whose site matches; returns the number installed. Each hook counts
// its calls, so tests can confirm the game really runs the replacements. Also reports each registered hook to the
// traps (hook/traps.h) as the switch `Hooks.<name>`: 1 when installed, 0 otherwise (disabled by the ini, a site that
// doesn't match, an RVA another hook took), so a trap that rests on a hook arms only while that hook is in.
int InstallHooks(uint8_t *base, const char *iniPath);

// Logs every installed hook's call count if any changed since the last call.
bool LogHookCountsIfChanged();

// Every registered hook, in registration order (unspecified across files; InstallHooks sorts by RVA).
std::vector<const HookSpec *> &RegisteredHooks();

struct HookRegistrar {
    template <size_t N> explicit HookRegistrar(const HookSpec (&specs)[N])
    {
        for (const HookSpec &s : specs) RegisteredHooks().push_back(&s);
    }
};
#define REGISTER_HOOKS(table) const HookRegistrar table##Registrar(table)
