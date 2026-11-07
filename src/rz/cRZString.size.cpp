#include "rz/cRZString.h"

#include "game/game.h"
#include "game/vtable.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

namespace {

// The game uses the CRT's strncmp and _strnicmp (ASCII path unless the locale was changed).
int Compare(const char *a, const char *b, uint32_t len, bool caseSensitive)
{
    return caseSensitive ? std::strncmp(a, b, len) : _strnicmp(a, b, len);
}

// Lowercases in place with the CRT's _strlwr, which (like the game's) stops at the first NUL.
void Lower(std::string &s)
{
#pragma warning(suppress : 4996) // the game uses _strlwr; _strlwr_s behaves the same but isn't what it calls
    _strlwr(s.data());
}

int32_t ToIndex(size_t pos)
{
    return pos == std::string::npos ? -1 : static_cast<int32_t>(pos);
}

} // namespace

cRZString *cRZString::Construct()
{
    new (&mString) std::string();
    mRefCount = 0;
    mVTable = VTable();
    return this;
}

cRZString *cRZString::Construct(const char *text)
{
    new (&mString) std::string(text);
    mRefCount = 0;
    mVTable = VTable();
    return this;
}

cRZString *cRZString::Construct(const cIGZString &other)
{
    uint32_t len = other.Strlen();
    new (&mString) std::string(other.Data(), len);
    mRefCount = 0;
    mVTable = VTable();
    return this;
}

cRZString *cRZString::Construct(const cRZString &other, uint32_t pos, uint32_t count)
{
    new (&mString) std::string(other.mString, pos, count);
    mRefCount = 0;
    mVTable = VTable();
    return this;
}

void cRZString::Destruct()
{
    mVTable = VTable();
    mString.~basic_string();
}

bool cRZString::QueryInterface(uint32_t riid, void **ppvObj)
{
    if (riid != kGZIID_cIGZUnknown && riid != kGZIID_cIGZString) return false;
    *ppvObj = this;
    AsInterface()->AddRef();
    return true;
}

uint32_t cRZString::AddRef()
{
    return ++mRefCount;
}

uint32_t cRZString::Release()
{
    if (mRefCount > 1) return --mRefCount;
    // Scalar deleting destructor, slot 33 (after cIGZString's 33 slots), with "delete" flag.
    using DeletingDtor = void *(__thiscall *)(cRZString *, uint32_t);
    (*reinterpret_cast<DeletingDtor *const *>(this))[33](this, 1);
    return 0;
}

void cRZString::FromChar(const char *src, uint32_t len)
{
    if (src) mString.assign(src, len);
    else mString.clear();
}

void cRZString::FromChar(const char *src)
{
    if (src) mString.assign(src, std::strlen(src));
    else mString.clear();
}

bool cRZString::IsEqual(const char *other, uint32_t len, bool caseSensitive) const
{
    if (len == UINT32_MAX) len = static_cast<uint32_t>(std::strlen(other));
    if (len != mString.size()) return false;
    return Compare(mString.c_str(), other, len, caseSensitive) == 0;
}

bool cRZString::IsEqual(const char *other, bool caseSensitive) const
{
    return AsInterface()->IsEqual(other, static_cast<uint32_t>(std::strlen(other)), caseSensitive);
}

bool cRZString::IsEqual(const cIGZString &other, bool caseSensitive) const
{
    uint32_t len = other.Strlen();
    if (len != mString.size()) return false;
    return Compare(mString.c_str(), other.Data(), len, caseSensitive) == 0;
}

bool cRZString::IsEqualForward(const cIGZString &other, bool caseSensitive) const
{
    return VirtualCall<bool(__thiscall *)(const void *, const cIGZString *, bool), 12>(this, &other, caseSensitive);
}

void *cRZString::DeletingDestruct(uint32_t flags)
{
    if (flags & 2) {
        // The compiler's `eh vector destructor iterator` (0x824e6f) with the destructor 0x141193: last element first.
        uint32_t *block = reinterpret_cast<uint32_t *>(this) - 1;
        for (uint32_t i = *block; i != 0;) this[--i].Destruct();
        if (flags & 1) ::operator delete[](block, *block * sizeof(cRZString) + sizeof(uint32_t));
        return block;
    }
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cRZString));
    return this;
}

int32_t cRZString::CompareTo(const char *other, uint32_t len, bool caseSensitive) const
{
    if (len == UINT32_MAX) len = static_cast<uint32_t>(std::strlen(other));
    uint32_t size = static_cast<uint32_t>(mString.size());
    int result = Compare(mString.c_str(), other, len < size ? len : size, caseSensitive);
    return result != 0 ? result : static_cast<int32_t>(size - len);
}

int32_t cRZString::CompareTo(const char *other, bool caseSensitive) const
{
    return AsInterface()->CompareTo(other, static_cast<uint32_t>(std::strlen(other)), caseSensitive);
}

int32_t cRZString::CompareTo(const cIGZString &other, bool caseSensitive) const
{
    uint32_t len = other.Strlen();
    uint32_t size = static_cast<uint32_t>(mString.size());
    int result = Compare(mString.c_str(), other.Data(), len < size ? len : size, caseSensitive);
    return result != 0 ? result : static_cast<int32_t>(size - len);
}

cRZString *cRZString::Assign(const cIGZString &other)
{
    AsInterface()->Copy(other);
    return this;
}

void cRZString::Copy(const cIGZString &other)
{
    uint32_t len = other.Strlen();
    const char *data = other.Data();
    AsInterface()->FromChar(data, len);
}

void cRZString::Resize(uint32_t size)
{
    mString.resize(size, '\0');
}

cRZString *cRZString::Append(const cIGZString &other)
{
    uint32_t len = other.Strlen();
    mString.append(other.ToChar(), len);
    return this;
}

cRZString *cRZString::Append(const char *other, uint32_t len)
{
    mString.append(other, len);
    return this;
}

cRZString *cRZString::Append(const char *other)
{
    if (other) mString.append(other, std::strlen(other));
    return this;
}

cRZString *cRZString::Insert(uint32_t pos, const cIGZString &other)
{
    uint32_t len = other.Strlen();
    mString.insert(pos, other.ToChar(), len);
    return this;
}

cRZString *cRZString::Insert(uint32_t pos, const char *other, uint32_t len)
{
    mString.insert(pos, other, len);
    return this;
}

// Replaces `count` characters at `pos` with `other` read as a C string (up to its first NUL).
cRZString *cRZString::Replace(uint32_t pos, const cIGZString &other)
{
    const char *text = other.ToChar();
    uint32_t count = other.Strlen();
    mString.replace(pos, count, text);
    return this;
}

cRZString *cRZString::Replace(uint32_t pos, const char *other, uint32_t count)
{
    mString.replace(pos, count, other);
    return this;
}

cRZString *cRZString::Erase(uint32_t pos, uint32_t count)
{
    mString.erase(pos, count);
    return this;
}

// Case-sensitive searches use the needle as a C string. Case-insensitive ones search lowercased
// copies of both strings (each lowercased only up to its first NUL).
int32_t cRZString::Find(const cIGZString &other, uint32_t pos, bool caseSensitive) const
{
    if (caseSensitive) return ToIndex(mString.find(other.ToChar(), pos));
    std::string self = mString;
    uint32_t len = other.Strlen();
    std::string needle(other.Data(), len);
    Lower(self);
    Lower(needle);
    return ToIndex(self.find(needle.c_str(), pos));
}

int32_t cRZString::Find(const char *other, uint32_t pos, bool caseSensitive) const
{
    if (caseSensitive) return ToIndex(mString.find(other, pos));
    std::string self = mString;
    std::string needle(other, std::strlen(other));
    Lower(self);
    Lower(needle);
    return ToIndex(self.find(needle.c_str(), pos));
}

int32_t cRZString::RFind(const cIGZString &other, uint32_t pos, bool caseSensitive) const
{
    if (caseSensitive) return ToIndex(mString.rfind(other.ToChar(), pos));
    std::string self = mString;
    uint32_t len = other.Strlen();
    std::string needle(other.Data(), len);
    Lower(self);
    Lower(needle);
    return ToIndex(self.rfind(needle.c_str(), pos));
}

int32_t cRZString::RFind(const char *other, uint32_t pos, bool caseSensitive) const
{
    if (caseSensitive) return ToIndex(mString.rfind(other, pos));
    std::string self = mString;
    std::string needle(other, std::strlen(other));
    Lower(self);
    Lower(needle);
    return ToIndex(self.rfind(needle.c_str(), pos));
}

cRZString *cRZString::Sprintf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    AsInterface()->SprintfVaList(fmt, args);
    va_end(args);
    return this;
}

// Uses the CRT's legacy _vsnprintf family like the game: they return -1 (not the needed length)
// when the output doesn't fit, and don't NUL-terminate output of exactly `count` characters.
cRZString *cRZString::SprintfVaList(const char *fmt, va_list args)
{
#pragma warning(push)
#pragma warning(disable : 4996) // _vsnprintf is what the game calls
    char buf[256];
    int n = _vsnprintf_l(buf, sizeof(buf), fmt, nullptr, args);
    if (n >= static_cast<int>(sizeof(buf))) {
        // Exactly 256 characters: format straight into the string.
        mString.resize(static_cast<size_t>(n) + 1, '\0');
        int r = _vsnprintf(mString.data(), mString.size(), fmt, args);
        mString.resize(static_cast<size_t>(r), '\0');
    } else if (n >= 0) {
        mString.assign(buf, static_cast<size_t>(n));
    } else {
        // Didn't fit: grow from max(1.25 * strlen(fmt), 2 * current size), doubling, below 1,000,000.
        size_t len = std::strlen(fmt);
        size_t capacity = std::max(len + (len >> 2), mString.size() * 2);
        int r = n;
        while (capacity < 1000000) {
            mString.resize(capacity, '\0');
            r = _vsnprintf(mString.data(), mString.size(), fmt, args);
            capacity *= 2;
            if (r >= 0) break;
        }
        if (r >= 0) mString.resize(static_cast<size_t>(r), '\0');
    }
#pragma warning(pop)
    return this;
}

bool cRZString::StartsWith(const char *text, uint32_t len, bool caseSensitive) const
{
    if (caseSensitive) {
        if (len > mString.size()) return false;
        return std::memcmp(mString.data(), text, len) == 0;
    }
    // The original builds two cRZString temporaries (a copy of this, and one from text/len), lowercases
    // both and compares them case-sensitively through the second one's vtable.
    alignas(cRZString) unsigned char rawSelf[sizeof(cRZString)], rawText[sizeof(cRZString)];
    cRZString *self = reinterpret_cast<cRZString *>(rawSelf);
    cRZString *prefix = reinterpret_cast<cRZString *>(rawText);
    self->Construct(*this, 0, 0xFFFFFFFF);
    Lower(self->mString);
    prefix->Construct();
    prefix->mString.assign(text, len);
    Lower(prefix->mString);
    bool result = self->StartsWith(*prefix->AsInterface(), true);
    prefix->Destruct();
    self->Destruct();
    return result;
}

bool cRZString::StartsWith(const cIGZString &other, bool caseSensitive) const
{
    uint32_t len = other.Strlen();
    const char *text = other.ToChar();
    return StartsWith(text, len, caseSensitive);
}

// Our vtable's slots (rzface::cRZString, the header): each runs the cRZString member, inlined where it is defined here.
namespace rzface {

namespace {
::cRZString *Self(cRZString *face) { return reinterpret_cast<::cRZString *>(face); }
const ::cRZString *Self(const cRZString *face) { return reinterpret_cast<const ::cRZString *>(face); }
} // namespace

bool cRZString::QueryInterface(uint32_t riid, void **ppvObj)
{
    [[msvc::forceinline_calls]] return Self(this)->QueryInterface(riid, ppvObj);
}
uint32_t cRZString::AddRef() { return Self(this)->AddRef(); }
uint32_t cRZString::Release() { [[msvc::forceinline_calls]] return Self(this)->Release(); }
void cRZString::FromChar(const char *src) { [[msvc::forceinline_calls]] Self(this)->FromChar(src); }
void cRZString::FromChar(const char *src, uint32_t len) { [[msvc::forceinline_calls]] Self(this)->FromChar(src, len); }
const char *cRZString::ToChar() const { return Self(this)->ToChar(); }
const char *cRZString::Data() const { return Self(this)->Data(); }
uint32_t cRZString::Strlen() const { return Self(this)->Strlen(); }
bool cRZString::IsEmpty() const { return Self(this)->IsEmpty(); }
bool cRZString::IsEqual(const cIGZString *other, bool caseSensitive) const
{
    [[msvc::forceinline_calls]] return Self(this)->IsEqual(*other, caseSensitive);
}
bool cRZString::IsEqual(const cIGZString &other, bool caseSensitive) const
{
    [[msvc::forceinline_calls]] return Self(this)->IsEqualForward(other, caseSensitive);
}
bool cRZString::IsEqual(const char *other, bool caseSensitive) const
{
    [[msvc::forceinline_calls]] return Self(this)->IsEqual(other, caseSensitive);
}
bool cRZString::IsEqual(const char *other, uint32_t len, bool caseSensitive) const
{
    [[msvc::forceinline_calls]] return Self(this)->IsEqual(other, len, caseSensitive);
}
int32_t cRZString::CompareTo(const cIGZString &other, bool caseSensitive) const
{
    [[msvc::forceinline_calls]] return Self(this)->CompareTo(other, caseSensitive);
}
int32_t cRZString::CompareTo(const char *other, bool caseSensitive) const
{
    [[msvc::forceinline_calls]] return Self(this)->CompareTo(other, caseSensitive);
}
int32_t cRZString::CompareTo(const char *other, uint32_t len, bool caseSensitive) const
{
    [[msvc::forceinline_calls]] return Self(this)->CompareTo(other, len, caseSensitive);
}
cIGZString &cRZString::operator=(const cIGZString &other)
{
    [[msvc::forceinline_calls]] return *Self(this)->Assign(other)->AsInterface();
}
void cRZString::Copy(const cIGZString &other) { [[msvc::forceinline_calls]] Self(this)->Copy(other); }
void cRZString::Resize(uint32_t size) { [[msvc::forceinline_calls]] Self(this)->Resize(size); }
cIGZString *cRZString::Append(const char *other)
{
    [[msvc::forceinline_calls]] return Self(this)->Append(other)->AsInterface();
}
cIGZString *cRZString::Append(const char *other, uint32_t len)
{
    [[msvc::forceinline_calls]] return Self(this)->Append(other, len)->AsInterface();
}
cIGZString *cRZString::Append(const cIGZString &other)
{
    [[msvc::forceinline_calls]] return Self(this)->Append(other)->AsInterface();
}
cIGZString *cRZString::Insert(uint32_t pos, const char *other, uint32_t len)
{
    [[msvc::forceinline_calls]] return Self(this)->Insert(pos, other, len)->AsInterface();
}
cIGZString *cRZString::Insert(uint32_t pos, const cIGZString &other)
{
    [[msvc::forceinline_calls]] return Self(this)->Insert(pos, other)->AsInterface();
}
cIGZString *cRZString::Replace(uint32_t pos, const char *other, uint32_t len)
{
    [[msvc::forceinline_calls]] return Self(this)->Replace(pos, other, len)->AsInterface();
}
cIGZString *cRZString::Replace(uint32_t pos, const cIGZString &other)
{
    [[msvc::forceinline_calls]] return Self(this)->Replace(pos, other)->AsInterface();
}
cIGZString *cRZString::Erase(uint32_t start, uint32_t end)
{
    [[msvc::forceinline_calls]] return Self(this)->Erase(start, end)->AsInterface();
}
int32_t cRZString::Find(const char *other, uint32_t pos, bool caseSensitive) const
{
    [[msvc::forceinline_calls]] return Self(this)->Find(other, pos, caseSensitive);
}
int32_t cRZString::Find(const cIGZString &other, uint32_t pos, bool caseSensitive) const
{
    [[msvc::forceinline_calls]] return Self(this)->Find(other, pos, caseSensitive);
}
int32_t cRZString::RFind(const char *other, uint32_t pos, bool caseSensitive) const
{
    [[msvc::forceinline_calls]] return Self(this)->RFind(other, pos, caseSensitive);
}
int32_t cRZString::RFind(const cIGZString &other, uint32_t pos, bool caseSensitive) const
{
    [[msvc::forceinline_calls]] return Self(this)->RFind(other, pos, caseSensitive);
}
// Slot 31 (__cdecl, `this` on the stack): formats through this string's own slot 32, as cRZString::Sprintf.
cIGZString *cRZString::Sprintf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    SprintfVaList(fmt, args);
    va_end(args);
    return this;
}
cIGZString *cRZString::SprintfVaList(const char *fmt, va_list args)
{
    [[msvc::forceinline_calls]] return Self(this)->SprintfVaList(fmt, args)->AsInterface();
}
void *cRZString::DeletingDestructor(uint32_t flags)
{
    [[msvc::forceinline_calls]] return Self(this)->DeletingDestruct(flags);
}

} // namespace rzface
