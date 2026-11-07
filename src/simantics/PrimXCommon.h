#pragma once
// Helpers shared by the lane X primitive handlers (PrimX*.cpp): vtable calls, param field reads, the operand reader,
// text locals and the string-table loader.
#include "game/original.h"
#include "game/vtable.h"
#include "rz/cRZString.h"
#include "simantics/PrimX.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace primX {

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

// The operand read: slot 7 of the secondary subobject (this + 4), seven stack arguments (scope, id, destination,
// float destination, a zeroed dword, clear flag, arg 7), the value in ax.
inline int16_t Read(PrimXObject *self, uint32_t scope, uint32_t id)
{
    void *sub = &self->mSecondary;
    return Call<int16_t>(sub, 7, scope, id, 0u, 0u, 0u, 0u, 0u);
}

// A plain cRZString local, constructed empty and destroyed when the handler leaves.
struct Text {
    cRZString mText;
    Text() { mText.Construct(); }
    explicit Text(const char *s) { mText.Construct(s); }
    Text(const Text &) = delete;
    Text &operator=(const Text &) = delete;
    ~Text() noexcept { mText.Destruct(); }
    const char *c_str() const { return mText.mString.c_str(); }
    std::string &str() { return mText.mString; }
    // The original appends (data, length) through the std::string member 0x1603f0.
    void Append(const char *p, size_t n) { mText.mString.append(p, n); }
    void AppendText(const Text &o) { mText.mString.append(o.c_str(), o.mText.mString.size()); }
    void AppendLiteral(const char *s) { mText.mString.append(s, std::strlen(s)); }
};

// A reference the handler keeps in its frame: released (slot 2) when it is dropped or the frame unwinds.
struct HeldRef {
    void *mObject = nullptr;
    HeldRef() = default;
    HeldRef(const HeldRef &) = delete;
    HeldRef &operator=(const HeldRef &) = delete;
    // 0x57cee0: clears the pointer, then releases the old object.
    void Reset()
    {
        if (mObject) {
            void *old = mObject;
            mObject = nullptr;
            Call<uint32_t>(old, 2);
        }
    }
    ~HeldRef()
    {
        if (mObject) Call<uint32_t>(mObject, 2);
    }
};

} // namespace primX
