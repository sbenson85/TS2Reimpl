#pragma once
#include "game/owned.h"
#include "gz/cIGZString.h"

#include <cstdint>
#include <cstdarg>
#include <cstddef>
#include <string>

// The game's cIGZString implementation: vtable pointer, std::string (MSVC 14.40 STL, identical to
// ours), reference count. Methods are non-virtual here so they can be hooked onto the game's vtable
// entries. Calls the original makes through a vtable (this's or another string's) are made the same
// way, in the same order, so subclasses and other cIGZString implementations behave identically.
// Original vtable: RVA 0xd9b470 (1.18.0.187), 34 slots (cIGZString's 33 + scalar deleting dtor).
//
// The vtable is an owned static (docs/standalone-shape.md, "Owned statics"): our constructors and inline builders
// store VTable(), the game's table in hooked builds (its 34 slots hooked to these members) and rzface::cRZString's
// (below) in an image-free program.
class cRZString {
public:
    static constexpr uint32_t kVTable = 0xd9b470; // the game's; VTable() is its owned symbol
    static const void *VTable();                  // the word our constructors (and inline builders) store
    static const void *OwnVTable();               // rzface::cRZString's: the image-free program's VTable()
    static constexpr uint32_t kGZIID_cIGZUnknown = 0x00000001;
    static constexpr uint32_t kGZIID_cIGZString = 0x089b7dc8;

    // Constructor/destructor bodies, run on raw memory like the game's (refcount starts at 0).
    cRZString *Construct();                                              // 0x144f5b
    cRZString *Construct(const char *text);                              // 0x14116d
    cRZString *Construct(const cIGZString &other);                       // 0x144f2a
    cRZString *Construct(const cRZString &other, uint32_t pos, uint32_t count); // 0x141eb3
    void Destruct();                                                     // 0x141193

    bool QueryInterface(uint32_t riid, void **ppvObj);  // slot 0
    uint32_t AddRef();                                  // slot 1
    uint32_t Release();                                 // slot 2

    void FromChar(const char *src, uint32_t len);       // slot 3
    void FromChar(const char *src);                     // slot 4
    // Slots 5/6 share one function in the game; slot 7 is a 4-byte getter shared with other classes.
    const char *ToChar() const { return mString.c_str(); }
    const char *Data() const { return mString.c_str(); }
    uint32_t Strlen() const { return static_cast<uint32_t>(mString.size()); }
    bool IsEmpty() const { return mString.empty(); }    // slot 8

    bool IsEqual(const char *other, uint32_t len, bool caseSensitive) const;   // slot 9
    bool IsEqual(const char *other, bool caseSensitive) const;                 // slot 10
    bool IsEqual(const cIGZString &other, bool caseSensitive) const;           // slot 12 (the pointer form)
    // Slot 11, the reference form: forwards through this string's own vtable to slot 12. The game's body
    // (0x1ad030, `mov eax, [ecx]; jmp [eax + 0x30]`) is folded with five other classes' slot 11: not hooked.
    bool IsEqualForward(const cIGZString &other, bool caseSensitive) const;
    int32_t CompareTo(const char *other, uint32_t len, bool caseSensitive) const;  // slot 13
    int32_t CompareTo(const char *other, bool caseSensitive) const;                // slot 14
    int32_t CompareTo(const cIGZString &other, bool caseSensitive) const;          // slot 15

    cRZString *Assign(const cIGZString &other);         // slot 16 (operator=)
    void Copy(const cIGZString &other);                 // slot 17
    void Resize(uint32_t size);                         // slot 18
    cRZString *Append(const cIGZString &other);                          // slot 19
    cRZString *Append(const char *other, uint32_t len);                  // slot 20
    cRZString *Append(const char *other);                                // slot 21
    cRZString *Insert(uint32_t pos, const cIGZString &other);            // slot 22
    cRZString *Insert(uint32_t pos, const char *other, uint32_t len);    // slot 23
    cRZString *Replace(uint32_t pos, const cIGZString &other);           // slot 24
    cRZString *Replace(uint32_t pos, const char *other, uint32_t count); // slot 25
    cRZString *Erase(uint32_t pos, uint32_t count);                      // slot 26
    int32_t Find(const cIGZString &other, uint32_t pos, bool caseSensitive) const;   // slot 27
    int32_t Find(const char *other, uint32_t pos, bool caseSensitive) const;         // slot 28
    int32_t RFind(const cIGZString &other, uint32_t pos, bool caseSensitive) const;  // slot 29
    int32_t RFind(const char *other, uint32_t pos, bool caseSensitive) const;        // slot 30
    cRZString *Sprintf(const char *fmt, ...);                                        // slot 31 (__cdecl)
    cRZString *SprintfVaList(const char *fmt, va_list args);                         // slot 32
    // Slot 33 (0x1cbea8), the vector deleting destructor. Bit 1 of `flags`: `this` is the first element of a
    // `new[]` array whose count is the dword before it; every element is destroyed, last first, and with bit 0
    // the block (count, elements) is freed; returns the block. Otherwise this one string is destroyed, freed
    // with bit 0, and `this` returned.
    void *DeletingDestruct(uint32_t flags);

    // Non-virtual prefix tests. 0x1acb54: the first `len` bytes of `text` equal the start of this string
    // (false if len exceeds the size); case-insensitive compares lowercased copies (CRT _strlwr, so NUL
    // ends the text). 0x1acb2c: the same with the other string's Strlen (slot 7) then ToChar (slot 5).
    bool StartsWith(const char *text, uint32_t len, bool caseSensitive) const;
    bool StartsWith(const cIGZString &other, bool caseSensitive) const;

    const cIGZString *AsInterface() const { return reinterpret_cast<const cIGZString *>(this); }
    cIGZString *AsInterface() { return reinterpret_cast<cIGZString *>(this); }

    const void *mVTable;
    std::string mString;
    uint32_t mRefCount;
};

static_assert(sizeof(cRZString) == 0x20, "cRZString layout: vptr, std::string, refcount");
static_assert(offsetof(cRZString, mRefCount) == 0x1c, "cRZString layout: refcount at 0x1c");

namespace rzface {

// cRZString's own vtable (the image-free program's): a cIGZString face over the hook-shaped class (no data; `this` is the string). Each slot runs
// the cRZString member for it (src/rz/cRZString.size.cpp), slot 33 the vector deleting destructor. Namespaced so that
// its RTTI name is still cRZString (tools/matchcheck.py ties our vtable to the game's 0xd9b470).
class cRZString final : public cIGZString {
public:
    bool QueryInterface(uint32_t riid, void **ppvObj) override;
    uint32_t AddRef() override;
    uint32_t Release() override;
    void FromChar(const char *src) override;
    void FromChar(const char *src, uint32_t len) override;
    const char *ToChar() const override;
    const char *Data() const override;
    uint32_t Strlen() const override;
    bool IsEmpty() const override;
    bool IsEqual(const cIGZString *other, bool caseSensitive) const override;
    bool IsEqual(const cIGZString &other, bool caseSensitive) const override;
    bool IsEqual(const char *other, bool caseSensitive) const override;
    bool IsEqual(const char *other, uint32_t len, bool caseSensitive) const override;
    int32_t CompareTo(const cIGZString &other, bool caseSensitive) const override;
    int32_t CompareTo(const char *other, bool caseSensitive) const override;
    int32_t CompareTo(const char *other, uint32_t len, bool caseSensitive) const override;
    cIGZString &operator=(const cIGZString &other) override;
    void Copy(const cIGZString &other) override;
    void Resize(uint32_t size) override;
    cIGZString *Append(const char *other) override;
    cIGZString *Append(const char *other, uint32_t len) override;
    cIGZString *Append(const cIGZString &other) override;
    cIGZString *Insert(uint32_t pos, const char *other, uint32_t len) override;
    cIGZString *Insert(uint32_t pos, const cIGZString &other) override;
    cIGZString *Replace(uint32_t pos, const char *other, uint32_t len) override;
    cIGZString *Replace(uint32_t pos, const cIGZString &other) override;
    cIGZString *Erase(uint32_t start, uint32_t end) override;
    int32_t Find(const char *other, uint32_t pos, bool caseSensitive) const override;
    int32_t Find(const cIGZString &other, uint32_t pos, bool caseSensitive) const override;
    int32_t RFind(const char *other, uint32_t pos, bool caseSensitive) const override;
    int32_t RFind(const cIGZString &other, uint32_t pos, bool caseSensitive) const override;
    cIGZString *Sprintf(const char *fmt, ...) override;
    cIGZString *SprintfVaList(const char *fmt, va_list args) override;
    virtual void *DeletingDestructor(uint32_t flags); // slot 33
};

} // namespace rzface

// The vtable (owned.h, OwnedVTable): `offset ts2_rva_d9b470`, the same one-instruction store as GameAddress(kVTable).
TS2_OWNED_STATIC(const OwnedVTable, d9b470);
inline const void *cRZString::VTable() { return &ts2_rva_d9b470; }

// The empty text the game keeps (an owned static: the image's in the hooked game, owned_statics.cpp's otherwise): what code
// passes where the original names 0xd9b9d0.
TS2_OWNED_STATIC(const char, d9b9d0)[];

// The address of rzface::cRZString's vtable as a link-time constant: the compiler folds the local face's vptr into
// `offset ??_7cRZString@rzface@@6B@` (no object is built).
inline const void *cRZString::OwnVTable()
{
    rzface::cRZString face;
    return *reinterpret_cast<const void *const *>(&face);
}
