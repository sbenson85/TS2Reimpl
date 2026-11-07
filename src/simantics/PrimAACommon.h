#pragma once
// Helpers shared by the lane AA primitive handlers (PrimAA70.cpp, PrimAA6D.cpp): vtable calls, param reads, the
// operand reader, string-table loaders, reference holders, handle tags and the two vector shapes the handlers touch.
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "rz/RZAutoRef.h"
#include "rz/cRZString.h"
#include "simantics/PrimAA.h"
#include "simantics/PrimS.h"
#include "simantics/PrimXCommon.h"
#include "ts/cTSString.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace primAA {

using primX::Byte;
using primX::Call;
using primX::Dword;
using primX::Raise;
using primX::Text;
using primX::Word;

constexpr uint32_t kTextKey = cTSString::kStringTableType; // the last argument of every string-table loader
constexpr uint32_t kDefaultGroup = 0x7fd46cd0; // the group the loaders use when the locale gives none
constexpr uint32_t kIidA = 0xfa1c39f7;         // the two identifiers slot 9 of an object takes with an output holder
constexpr uint32_t kIidB = 0xfa1c39ed;
constexpr uint32_t kIidDialog = 0x20732180;    // the interface id the dialog service's slots take with an output pointer

// Bit `bit` of the flag word at param offset `off` (the accessors 0x57e6xx..0x57e8xx test one bit of a byte or of the
// sign-extended word at +0xe; bits above 7 live in the next byte).
inline bool Bit(const uint8_t *p, size_t off, int bit)
{
    return ((Word(p, off) >> bit) & 1) != 0;
}

// The operand read: slot 7 of the secondary subobject (this + 4), seven stack arguments, the value in ax.
inline int16_t Read(PrimAAObject *self, uint32_t scope, uint32_t id)
{
    void *sub = &self->mSecondary;
    return Call<int16_t>(sub, 7, scope, id, 0u, 0u, 0u, 0u, 0u);
}

// The data word accessor (slot 19 of the primary vtable, 0x4c): a 16-bit value in ax.
inline int16_t DataWord(PrimAAObject *self, uint32_t index)
{
    return static_cast<int16_t>(Call<uint32_t>(self, 19, index));
}

// A string-table loader object (cTSString: vtable 0xda5128, 0x34 bytes): a cRZString that fills itself from the string tables
// when it is constructed. The destructor is the cRZString one, run only for an object that was constructed.
class Loader {
public:
    Loader() = default;
    Loader(const Loader &) = delete;
    Loader &operator=(const Loader &) = delete;
    ~Loader() noexcept
    {
        if (mAlive) text().Destruct();
    }
    void Construct(uint32_t table, uint32_t entry, uint32_t group, uint32_t flag)
    {
        loader().Construct(static_cast<uint16_t>(table), static_cast<uint16_t>(entry), group, static_cast<uint8_t>(flag), kTextKey);
        mAlive = true;
    }
    // Copies the text and the key fields of another loader (cTSString::Assign, 0x1d1000).
    void Assign(const Loader &other) { loader().Assign(&other.loader()); }
    cTSString &loader() { return *reinterpret_cast<cTSString *>(mBytes); }
    const cTSString &loader() const { return *reinterpret_cast<const cTSString *>(mBytes); }
    cRZString &text() { return loader().mString; }
    bool empty() { return text().mString.empty(); }
    const char *c_str() { return text().mString.c_str(); }

private:
    alignas(4) uint8_t mBytes[0x34] = {};
    bool mAlive = false;
};

// Copies the text of `from` into the cRZString-layout object `to` (0x26be10).
inline void AssignText(cRZString &to, cRZString &from)
{
    reinterpret_cast<SString *>(&to)->AssignText(reinterpret_cast<const SString *>(&from));
}

// Loads (table, entry, group, 1) and copies it into `to`: the loader is a temporary of the original's frame.
inline void LoadInto(Text &to, uint32_t table, uint32_t entry, uint32_t group)
{
    Loader loaded;
    loaded.Construct(table, entry, group, 1);
    AssignText(to.mText, loaded.text());
}

// The holder with a virtual base (4 bytes): constructed empty (0x57c290 with 0), reset before it is refilled
// (0x2296b4), released through the virtual base when the frame leaves (0x210cfc).
struct VBRef {
    RZAutoRef r{nullptr};
    VBRef() = default;
    VBRef(const VBRef &) = delete;
    VBRef &operator=(const VBRef &) = delete;
    ~VBRef() noexcept { r.ReleaseVirtualBase(); }
    void **out() { return &r.mObject; }
    void *get() const { return r.mObject; }
};

// A plain reference the handler keeps: AddRef'd when it takes the object (slot 1), released (slot 2) when replaced
// or dropped (0x57ca80 / 0x142080: nothing when it is the same object).
struct UnkRef {
    void *p = nullptr;
    UnkRef() = default;
    explicit UnkRef(void *object) : p(object)
    {
        if (p) Call<uint32_t>(p, 1);
    }
    UnkRef(const UnkRef &) = delete;
    UnkRef &operator=(const UnkRef &) = delete;
    void Assign(void *object)
    {
        if (object == p) return;
        void *old = p;
        if (object) Call<uint32_t>(object, 1);
        p = object;
        if (old) Call<uint32_t>(old, 2);
    }
    ~UnkRef()
    {
        if (p) Call<uint32_t>(p, 2);
    }
};

// A pointer slot an out-parameter fills (the reference is released, slot 2, when the frame leaves; the old object
// is released and the slot cleared before it is refilled).
struct OutRef {
    void *p = nullptr;
    OutRef() = default;
    OutRef(const OutRef &) = delete;
    OutRef &operator=(const OutRef &) = delete;
    // 0x57cee0 (and the inlined copies): clears the slot, then releases the old object. Returns the slot.
    void **Reset()
    {
        if (p) {
            void *old = p;
            p = nullptr;
            Call<uint32_t>(old, 2);
        }
        return &p;
    }
    ~OutRef()
    {
        if (p) Call<uint32_t>(p, 2);
    }
};

// The game's vector of 16-bit values (first, last, end; elements allocated through the allocator service) as the
// handlers touch it: 0x5a9a10 clears it, 0x5799b0 appends, 0x5a9480 releases the buffer.
struct ShortVec {
    int16_t *first = nullptr;
    int16_t *last = nullptr;
    int16_t *end = nullptr;
    ShortVec() = default;
    ShortVec(const ShortVec &) = delete;
    ShortVec &operator=(const ShortVec &) = delete;
    size_t size() const { return static_cast<size_t>(last - first); }
    void Clear() { last = first; }
    void PushBack(const int16_t &v);
    void Tidy();
    ~ShortVec() { Tidy(); }
};
static_assert(sizeof(ShortVec) == 12, "ShortVec layout");

// The 0x14-byte record of the dialog request vector: {object, flag, scale, a, b} (built by 0x57c6a0).
struct Attr {
    void *object;
    uint8_t flag;
    float scale;
    uint32_t a;
    uint32_t b;
};
static_assert(sizeof(Attr) == 0x14, "Attr layout");

// The vector of Attr records (first, last, end).
struct AttrVec {
    Attr *first = nullptr;
    Attr *last = nullptr;
    Attr *end = nullptr;
    AttrVec() = default;
    AttrVec(const AttrVec &) = delete;
    AttrVec &operator=(const AttrVec &) = delete;
    void PushBack(const Attr &v);
    // 0x57c590 (the copy made for a by-value argument): a buffer of exactly the elements, copied.
    void CopyFrom(const AttrVec &other);
    void Tidy();
    ~AttrVec() { Tidy(); }
};
static_assert(sizeof(AttrVec) == 12, "AttrVec layout");

// What the callee receives for a vector passed by value (three words; the callee releases it).
struct AttrVecWords {
    Attr *first;
    Attr *last;
    Attr *end;
};

// 0x5a9830 / 0x194be0: bytes from the allocator service (slot 3), the cached service in the function-local static
// the Free helper (0x197100) also uses.
void *AllocateThroughAllocatorService(size_t bytes);

// A new, empty string-keyed property set: our cGZPropertySetString (the original builds the game's, vtable 0xd9f718).
// Created with a count of zero; the creator takes a reference (slot 1) and drops it (slot 2) when done.
void *NewPropertySet();

// The value holders slot 3 of a property set takes: vtable 0xda3d48 with a dword (57c640), vtable 0xd9c670 with a byte
// (57c620). Their destructor (0x14c180) only rewrites the vtable pointer.
struct PropInt {
    const void *vtable;
    uint32_t value;
    explicit PropInt(int32_t v);
};
struct PropBool {
    const void *vtable;
    uint8_t value;
    explicit PropBool(bool v);
};
static_assert(sizeof(PropInt) == 8, "PropInt layout");
static_assert(sizeof(PropBool) == 8, "PropBool layout");

// Builds a cRZString copy of `source` (0x141eb3 on a heap block of 0x20 bytes) the way the handlers hand text to
// property setters: the receiver owns it (reference count 0, the receiver takes one).
cRZString *NewStringCopy(cRZString &source);

} // namespace primAA
