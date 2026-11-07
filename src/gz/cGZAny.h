#pragma once
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator: storage from the allocator service
#include "gz/cGZPropertySet.h"         // gzany:: type ids and vtables
#include "gz/cIGZAny.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>

class cIGZString;

// The cIGZAny value classes the property sets hold (lane DQ, from lane DM's structure pass; RVAs 1.18.0.187).
// Each is a game vtable plus a payload; members are plain functions hooked onto the vtable entries, so objects
// keep the game's vtables (gzany::k*VTable) and calls into other values go through cIGZAny's slots by name.
//
// cIGZAny slots: 0 DeletingDestructor(flags), 1 Assign(source) (copies only from the same type id, returns 1 in
// al, else 0), 2 Compare(other), 3 Clone(), 4 GetType(), 5 GetTypeName() (the text stored right after the
// class's 6-slot vtable: vtable + 0x18).
//
// Two translation units: AnyUint32 and AnyBoolean (0x14xxxx-0x179xxx) are built for speed (cGZAny.cpp); the
// set's own instantiations of AnySint32 / AnyFloat32 / AnyString / AnyAny (0x1bexxx) and AnyStringSet
// (0x236xxx-0x238xxx) for size (cGZAny.size.cpp). Identical bodies are folded across the family: one deleting
// destructor for AnyUint32 + AnyBoolean (0x14c5f0) and one for AnySint32 + AnyFloat32 + AnyAny (0x1bedcb), one
// Assign for AnySint32 + AnyFloat32 + AnyAny (0x1bf068). Only these classes use the folded bodies.

// The abstract base: just the vtable, 4 bytes. Its slot 0 is the only non-pure slot.
struct cGZAny {
    const void *mVTable; // +0 0xd9c654

    void *DeletingDestructor(uint32_t flags); // 0x14c840: vtable reset to cIGZAny's, delete 4 bytes on flags & 1
    // 0x14c5f0, slot 0 of AnyUint32 and AnyBoolean (folded within the family): the vtable reset to cIGZAny's, delete
    // 8 bytes on flags & 1. AnySint32::DeletingDestructor (0x1bedcb) is the same body built for size.
    void *ValueDeletingDestructor(uint32_t flags);
};
static_assert(sizeof(cGZAny) == 4);

// AnyUint32, AnyBoolean and AnyFloat32 are in the standalone shape (lane GZ; the classes in namespace gz below). The
// global structs of those names are the layout of the objects the game still builds inline with its own vtables
// (some 700 code sites store these three vtables: property-set arguments on the stack, the serializers' values),
// for code that builds or reads them that way (cTSSaveGameInfo, the property-set serializers).
struct AnyUint32 {
    const void *mVTable; // +0 0xd9f534
    uint32_t mValue;     // +4
};
static_assert(sizeof(AnyUint32) == 8);

struct AnyBoolean {
    const void *mVTable; // +0 0xd9c670
    uint8_t mValue;      // +4
    uint8_t mUnused[3];  // +5
};
static_assert(sizeof(AnyBoolean) == 8);

// AnySint32 (vtable 0xda3d48) and the bodies it shares with AnyFloat32 and AnyAny.
struct AnySint32 {
    const void *mVTable; // +0
    int32_t mValue;      // +4

    void *DeletingDestructor(uint32_t flags);   // slot 0, 0x1bedcb (also AnyFloat32's and AnyAny's)
    bool Assign(const void *source);            // slot 1, 0x1bf068 (the dword: also AnyFloat32's and AnyAny's)
    int32_t Compare(const void *other) const;   // slot 2, 0x1befcf: signed
    AnySint32 *Clone() const;                   // slot 3, 0x1bef56
    uint32_t GetType() const;                   // slot 4, 0x1bf5c5
    const char *GetTypeName() const;            // slot 5, 0x1bf5dd
};
static_assert(sizeof(AnySint32) == 8);

struct AnyFloat32 {
    const void *mVTable; // +0 0xda3d70
    float mValue;        // +4
};
static_assert(sizeof(AnyFloat32) == 8);

namespace gz {

// The three scalar values the image's static initialisers leave constant-initialised (docs/subsystems/startup.md,
// "Done in lane GZ"), as real classes: each derives from cIGZAny (the image's cGZAny<Value> between them has no code
// or vtable of its own), our vtable is the compiler's, slot 0 is the compiler's deleting destructor. In the image the
// deleting destructors are one folded body per translation unit: 0x14c5f0 (AnyUint32, AnyBoolean) and 0x1bedcb
// (AnySint32, AnyFloat32, AnyAny), as is AnyFloat32's Assign (0x1bf068, with AnySint32 and AnyAny). Those are ours
// only in our vtables (folded: never hooked). The other slots are hooked through the adapters below, which serve the
// objects the game still builds with its vtables.

// AnyUint32 (vtable 0xd9f534; type id 0xeb61e4f7).
struct AnyUint32 : public cIGZAny { // a struct, as the original (RTTI .?AUAnyUint32@@)
    explicit AnyUint32(uint32_t value) : mValue(value) {}
    AnyUint32(const AnyUint32 &) = default;
    ~AnyUint32() override = default;                     // slot 0, 0x14c5f0 (folded: the vtable reset, delete 8)
    bool Assign(const cIGZAny &source) override;         // slot 1, 0x177b90
    int32_t Compare(const cIGZAny &other) const override; // slot 2, 0x177b70: unsigned -1 / 0 / 1
    cIGZAny *Clone() const override;                     // slot 3, 0x177ab0
    uint32_t GetType() const override;                   // slot 4, 0x179d00
    const char *GetTypeName() const override;            // slot 5, 0x179d10: "AnyUint32"

    uint32_t mValue; // +4
};
static_assert(sizeof(AnyUint32) == 8);

// AnyBoolean (vtable 0xd9c670; type id 0xcba908e1): the value is the byte at +4, compared and copied as a raw byte;
// the three bytes after it are never written (Clone leaves them as operator new returned them) nor read.
struct AnyBoolean : public cIGZAny { // a struct, as the original (RTTI .?AUAnyBoolean@@)
    explicit AnyBoolean(uint8_t value) : mValue(value) {}
    AnyBoolean(const AnyBoolean &) = default;
    ~AnyBoolean() override = default;                    // slot 0, 0x14c5f0 (folded, as AnyUint32's)
    bool Assign(const cIGZAny &source) override;         // slot 1, 0x14e760: the byte only
    int32_t Compare(const cIGZAny &other) const override; // slot 2, 0x14e5b0: unsigned bytes
    cIGZAny *Clone() const override;                     // slot 3, 0x14df70
    uint32_t GetType() const override;                   // slot 4, 0x1522b0
    const char *GetTypeName() const override;            // slot 5, 0x1522c0: "AnyBoolean"

    uint8_t mValue; // +4
};
static_assert(sizeof(AnyBoolean) == 8);

// AnyFloat32 (vtable 0xda3d70; type id 0xabc78708).
struct AnyFloat32 : public cIGZAny { // a struct, as the original (RTTI .?AUAnyFloat32@@)
    explicit AnyFloat32(float value) : mValue(value) {}
    AnyFloat32(const AnyFloat32 &) = default;
    ~AnyFloat32() override = default;                    // slot 0, 0x1bedcb (folded: the vtable reset, delete 8)
    bool Assign(const cIGZAny &source) override;         // slot 1, 0x1bf068 (folded: the dword)
    int32_t Compare(const cIGZAny &other) const override; // slot 2, 0x1befac: -1 when other > this, 1 when this >
                                                          // other, else 0 (NaN on either side compares equal)
    cIGZAny *Clone() const override;                     // slot 3, 0x1bef35
    uint32_t GetType() const override;                   // slot 4, 0x1bf5bf
    const char *GetTypeName() const override;            // slot 5, 0x1bf5d7: "AnyFloat32"

    float mValue; // +4
};
static_assert(sizeof(AnyFloat32) == 8);

// The hooks' entry points (docs/standalone-shape.md step 6): `this` in ecx is an object with the game's vtable or
// ours; each forwards to our member by a qualified (non-virtual) call.
template <class Value> struct AnyAdapters {
    static bool __fastcall Assign(Value *self, void *, const cIGZAny *source);
    static int32_t __fastcall Compare(const Value *self, void *, const cIGZAny *other);
    static cIGZAny *__fastcall Clone(const Value *self, void *);
    static uint32_t __fastcall GetType(const Value *self, void *);
    static const char *__fastcall GetTypeName(const Value *self, void *);
};

template <class Value> bool __fastcall AnyAdapters<Value>::Assign(Value *self, void *, const cIGZAny *source)
{
    [[msvc::forceinline_calls]] return self->Value::Assign(*source);
}

template <class Value> int32_t __fastcall AnyAdapters<Value>::Compare(const Value *self, void *, const cIGZAny *other)
{
    [[msvc::forceinline_calls]] return self->Value::Compare(*other);
}

template <class Value> cIGZAny *__fastcall AnyAdapters<Value>::Clone(const Value *self, void *)
{
    [[msvc::forceinline_calls]] return self->Value::Clone();
}

template <class Value> uint32_t __fastcall AnyAdapters<Value>::GetType(const Value *self, void *)
{
    [[msvc::forceinline_calls]] return self->Value::GetType();
}

template <class Value> const char *__fastcall AnyAdapters<Value>::GetTypeName(const Value *self, void *)
{
    [[msvc::forceinline_calls]] return self->Value::GetTypeName();
}

// Instantiated next to the bodies, in the file built like the original (cGZAny.cpp: speed; cGZAny.size.cpp: size).
extern template struct AnyAdapters<AnyUint32>;
extern template struct AnyAdapters<AnyBoolean>;
extern template struct AnyAdapters<AnyFloat32>;

} // namespace gz

// AnyAny (anonymous namespace in the game, vtable 0xda3e00): how a property set stores a value of any other type,
// as the pointer to a Clone of it. The Any itself does not own the clone: its destructor leaves it alone (the set's
// entry code deletes it), Compare orders the pointers and Clone copies the pointer.
struct AnyAny {
    const void *mVTable; // +0
    const void *mValue;  // +4 the held cIGZAny (a clone)

    int32_t Compare(const void *other) const;   // slot 2, 0x1bef94: the pointers, unsigned
    AnyAny *Clone() const;                      // slot 3, 0x1bef14: shares mValue
    uint32_t GetType() const;                   // slot 4, 0x1bf5b9
    const char *GetTypeName() const;            // slot 5, 0x1bf5d1
};
static_assert(sizeof(AnyAny) == 8);

// AnyString (vtable 0xda3d98): a counted cIGZString (normally a 0x20-byte cRZString). Copies and Assign share the
// string object, so two AnyStrings can alias one text.
struct AnyString {
    const void *mVTable;  // +0
    cIGZString *mString;  // +4 counted; may be null

    AnyString *Construct(const AnyString &source);    // 0x1bec79 (ret 4): copy, AddRef the shared string
    AnyString *Construct(const cIGZString &text);     // 0x1ce6ab (ret 4): a new cRZString copy of text
    AnyString *Construct(const char *text);           // 0x213fbb (ret 4): a new cRZString of text
    void Destruct();                                  // 0x1bed50: Release, vtable reset to cIGZAny's

    void *DeletingDestructor(uint32_t flags);   // slot 0, 0x1bedeb
    bool Assign(const void *source);            // slot 1, 0x1bf097: shares source's string (RZUnknownSlot::Assign)
    int32_t Compare(const void *other) const;   // slot 2, 0x1befeb: case-sensitive CompareTo of the two strings
    AnyString *Clone() const;                   // slot 3, 0x1bef77: shares the string
    uint32_t GetType() const;                   // slot 4, 0x1bf5cb
    const char *GetTypeName() const;            // slot 5, 0x1bf5e3
};
static_assert(sizeof(AnyString) == 8);

// The strings and the set AnyStringSet holds. EA's containers take their storage from the allocator service
// (slot 3 allocates, slot 4 frees; 0x177110 for string buffers, 0x1e7190 for list nodes, 0x1d0879 / 0x1d0903 for the
// bucket vector), and the set hashes with nGZLog::HashChars over the text up to its first NUL (h * 33 + c).
// Otherwise this is MSVC 14.40's unordered_set: 0x20 bytes (max load factor, list head and size, bucket vector,
// mask, bucket count), the same code as ours.
using GZString = std::basic_string<char, std::char_traits<char>, GZServiceAllocator<char>>;
static_assert(sizeof(GZString) == 0x18);

struct GZStringHash {
    size_t operator()(const GZString &text) const;
};

using GZStringSet = std::unordered_set<GZString, GZStringHash, std::equal_to<GZString>, GZServiceAllocator<GZString>>;
static_assert(sizeof(GZStringSet) == 0x20);

// The set's own out-of-line members, as the game calls them (this = the set).
struct GZStringSetOps {
    GZStringSet mSet;

    GZStringSetOps *Construct();                                          // 0x551af0: empty, 8 buckets
    void Destruct();                                                      // 0x551d90
    std::pair<GZStringSet::iterator, bool> Insert(const GZString &text);  // 0x5573a0 (ret 8; /O2 copy)
    std::pair<GZStringSet::iterator, bool> InsertSize(const GZString &text); // 0x236a4b (ret 8; /O1 copy)
    // Kept out of line so that AnyStringSet's members call it as the game's do (0x236b0b). 0x236fc5 is a library
    // match (std::unordered_set::operator=): AnyStringSet::Assign uses the standard operator= directly.
    __declspec(noinline) GZStringSet::iterator Find(const GZString &text); // 0x236b0b (ret 8: result buffer, text)
};
static_assert(sizeof(GZStringSetOps) == 0x20);

// AnyStringSet (vtable 0xdac644), 0x24 bytes: vtable + the set (not 0x10 bytes as cGZPropertySet.h says; its
// deleting destructor frees 0x24, and +8 / +0xc are the set's list head and size).
struct AnyStringSet {
    const void *mVTable; // +0
    GZStringSet mSet;    // +4

    AnyStringSet *Construct(const AnyStringSet &source); // 0x236c35 (ret 4): the set built from source's strings
    void Destruct();                                     // 0x236f02
    bool Contains(const GZString &text) const;           // 0x237c75 (ret 4)

    void *DeletingDestructor(uint32_t flags);   // slot 0, 0x237034
    bool Assign(const void *source);            // slot 1, 0x237679: set assignment
    int32_t Compare(const void *other) const;   // slot 2, 0x237621: 1 when both hold the same strings, else 0
    AnyStringSet *Clone() const;                // slot 3, 0x2375ea
    uint32_t GetType() const;                   // slot 4, 0x238790
    const char *GetTypeName() const;            // slot 5, 0x238796
};
static_assert(sizeof(AnyStringSet) == 0x24);
static_assert(offsetof(AnyStringSet, mSet) == 4);
