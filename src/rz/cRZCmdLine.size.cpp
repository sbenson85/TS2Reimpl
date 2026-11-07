#include "rz/cRZCmdLine.h"

#include "game/game.h"
#include "game/vtable.h"
#include "rz/StringKeyParser.h"

#include <cstring>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>

namespace {

// A cRZString local: constructed and destroyed like the original's stack objects (vtable set,
// std::string constructed; destruction = vtable reset + string release, no virtual call).
class Str {
public:
    Str() { S().Construct(); }
    explicit Str(const char *text) { S().Construct(text); }
    explicit Str(const cIGZString &other) { S().Construct(other); }
    Str(const cRZString &other, uint32_t pos, uint32_t count) { S().Construct(other, pos, count); }
    ~Str() { S().Destruct(); }
    Str(const Str &) = delete;
    Str &operator=(const Str &) = delete;

    cRZString &S() { return *reinterpret_cast<cRZString *>(mRaw); }
    std::string &Text() { return S().mString; }
    void Lower() { reinterpret_cast<cRZStringHelpers *>(mRaw)->LowerCase(); } // 0x1ad167

private:
    alignas(cRZString) unsigned char mRaw[sizeof(cRZString)];
};

// The characters the switch parsers treat as switch prefixes: slash, hyphen, backslash (the
// string at 0xda26d4).
constexpr char kSwitchPrefixes[] = "/-\\";

bool IsSpace(char c)
{
    return c == '\n' || c == '\t' || c == ' ';
}

// 0x14205a: assign through (data, size), not through operator=, and not at all to itself.
void AssignString(std::string &dst, const std::string &src)
{
    if (&dst != &src) dst.assign(src.c_str(), src.size());
}

// Whether `c` is one of the switch prefixes (the find in the original's string of prefixes, 0x1adb7f).
bool IsPrefix(const std::string &prefixes, char c)
{
    return prefixes.find(c, 0) < prefixes.size();
}

// The original reads `dest->mVTable[33]` on elements it destroys: the deleting destructor with
// flags 0 (destroy without freeing).
void DestroyElement(cRZString *p)
{
    VirtualCall<void *(__thiscall *)(void *, uint32_t), cRZStringVector::kSlotDeletingDestructor>(p, 0);
}

// A character variant pair for the single-letter switches: lower and upper case of letters, the
// character itself otherwise.
struct CaseVariants {
    char lower;
    char upper;
};

CaseVariants VariantsOf(char c)
{
    if (static_cast<uint8_t>(c - 0x41) <= 0x19) return {static_cast<char>(c + 0x20), c};
    if (static_cast<uint8_t>(c - 0x61) <= 0x19) return {c, static_cast<char>(c - 0x20)};
    return {c, c};
}

// The quote stripping at the end of each token: if the token starts and ends with a double quote,
// the closing quote goes, then (if anything is left) the opening one. A lone quote disappears.
void StripQuotes(std::string &tok)
{
    if (tok.c_str()[0] != '"') return;
    size_t last = tok.size() - 1;
    if (tok.c_str()[last] != '"') return;
    tok.erase(last, 1);
    if (tok.size() != 0) tok.erase(0, 1);
}

} // namespace

// cRZStringVector --------------------------------------------------------------------------------

void cRZStringVector::DestroyRange(cRZString *first, cRZString *last, const void *)
{
    for (cRZString *p = first; p != last; ++p) DestroyElement(p);
}

cRZString *cRZStringVector::UninitializedCopy(const cRZString *first, const cRZString *last, cRZString *dest, const void *)
{
    // The original has no frame here: a throwing copy leaks the elements built so far.
    for (const cRZString *p = first; p != last; ++p, ++dest) dest->Construct(*p, 0, 0xffffffff);
    return dest;
}

cRZString *cRZStringVector::CopyBackward(const cRZString *first, const cRZString *last, cRZString *destEnd)
{
    while (last != first) {
        --last;
        --destEnd;
        AssignString(destEnd->mString, last->mString);
    }
    return destEnd;
}

cRZString *cRZStringVector::CopyForward(const cRZString *first, const cRZString *last, cRZString *dest)
{
    for (const cRZString *p = first; p != last; ++p, ++dest) AssignString(dest->mString, p->mString);
    return dest;
}

cRZString *cRZStringVector::CopyN(const cRZString *first, uint32_t count, cRZString *dest)
{
    for (; count != 0; --count, ++first, ++dest) AssignString(dest->mString, first->mString);
    return dest;
}

cRZString *cRZStringVector::UninitializedCopyN(const cRZString *first, uint32_t count, cRZString *dest, const void *)
{
    // No frame, as in UninitializedCopy: a throwing copy leaks the elements built so far.
    for (; count != 0; --count, ++first, ++dest) dest->Construct(*first, 0, 0xffffffff);
    return dest;
}

void cRZStringVector::AssignCounted(const cRZString *first, uint32_t count)
{
    if (count > static_cast<uint32_t>(mEnd - mFirst)) {
        // The library's clear-and-reserve: the growth is computed from the old capacity before anything is freed.
        if (count > kMaxElements) std::_Xlength_error("vector too long");
        const uint32_t newCapacity = CalculateGrowth(count);
        Tidy();
        cRZString *block = std::allocator<cRZString>().allocate(newCapacity);
        mFirst = block;
        mLast = block;
        mEnd = block + newCapacity;
        mLast = UninitializedCopyN(first, count, mFirst, this);
        return;
    }
    const uint32_t size = static_cast<uint32_t>(mLast - mFirst);
    if (count > size) {
        for (cRZString *p = mFirst; p != mLast; ++p, ++first) AssignString(p->mString, first->mString);
        mLast = UninitializedCopyN(first, count - size, mLast, this);
        return;
    }
    cRZString *newLast = mFirst + count;
    CopyN(first, count, mFirst);
    DestroyRange(newLast, mLast, this);
    mLast = newLast;
}

uint32_t cRZStringVector::CalculateGrowth(uint32_t newSize) const
{
    uint32_t capacity = static_cast<uint32_t>(mEnd - mFirst);
    uint32_t half = capacity >> 1;
    if (capacity > kMaxElements - half) return kMaxElements;
    uint32_t grown = capacity + half;
    return grown < newSize ? newSize : grown;
}

void cRZStringVector::ChangeArray(cRZString *newFirst, uint32_t newSize, uint32_t newCapacity)
{
    if (mFirst) {
        DestroyRange(mFirst, mLast, nullptr);
        std::allocator<cRZString>().deallocate(mFirst, static_cast<size_t>(mEnd - mFirst));
    }
    mFirst = newFirst;
    mLast = newFirst + newSize;
    mEnd = newFirst + newCapacity;
}

void cRZStringVector::Tidy()
{
    if (!mFirst) return;
    DestroyRange(mFirst, mLast, nullptr);
    std::allocator<cRZString>().deallocate(mFirst, static_cast<size_t>(mEnd - mFirst));
    mFirst = nullptr;
    mLast = nullptr;
    mEnd = nullptr;
}

void cRZStringVector::Clear()
{
    if (mFirst == mLast) return;
    DestroyRange(mFirst, mLast, nullptr);
    mLast = mFirst;
}

cRZString *cRZStringVector::PushBack(const cRZString &value)
{
    cRZString *slot = mLast;
    slot->Construct(value, 0, 0xffffffff);
    mLast = slot + 1;
    return slot;
}

cRZString *cRZStringVector::EmplaceReallocate(cRZString *where, const cRZString &value)
{
    uint32_t size = static_cast<uint32_t>(Count());
    if (size == kMaxElements) std::_Xlength_error("vector too long");
    uint32_t newSize = size + 1;
    uint32_t newCapacity = CalculateGrowth(newSize);
    cRZString *newFirst = std::allocator<cRZString>().allocate(newCapacity);
    // The element's offset is taken in whole elements (`and 0xffffffe0`), like the original.
    cRZString *newElem = reinterpret_cast<cRZString *>(reinterpret_cast<char *>(newFirst) + ((reinterpret_cast<char *>(where) - reinterpret_cast<char *>(mFirst)) & ~0x1fu));
    cRZString *builtFirst = newElem + 1; // [builtFirst, newElem + 1) is what the unwinder destroys
    try {
        newElem->Construct(value, 0, 0xffffffff);
        builtFirst = newElem;
        if (where == mLast) {
            UninitializedCopy(mFirst, mLast, newFirst, nullptr);
        } else {
            UninitializedCopy(mFirst, where, newFirst, nullptr);
            builtFirst = newFirst;
            UninitializedCopy(where, mLast, newElem + 1, nullptr);
        }
    } catch (...) {
        DestroyRange(builtFirst, newElem + 1, nullptr);
        std::allocator<cRZString>().deallocate(newFirst, newCapacity);
        throw;
    }
    ChangeArray(newFirst, newSize, newCapacity);
    return newElem;
}

cRZString *cRZStringVector::PushBackAny(const cRZString &value)
{
    if (mLast == mEnd) return EmplaceReallocate(mLast, value);
    return PushBack(value);
}

cRZString **cRZStringVector::Insert(cRZString **out, cRZString *where, const cRZString &value)
{
    if (mLast != mEnd) {
        if (where == mLast) {
            PushBack(value);
        } else {
            // A copy of the value first (it may be an element of this vector), then the last element is
            // copied one slot up, the elements between shift up, and the copy lands in the gap.
            // The copy is destroyed through its vtable (slot 33, flags 0), not by a destructor call.
            alignas(cRZString) unsigned char raw[sizeof(cRZString)];
            cRZString &copy = *reinterpret_cast<cRZString *>(raw);
            copy.Construct(value, 0, 0xffffffff);
            cRZString *last = mLast;
            last->Construct(*(last - 1), 0, 0xffffffff);
            mLast += 1;
            CopyBackward(where, last - 1, last);
            AssignString(where->mString, copy.mString);
            DestroyElement(&copy);
        }
        *out = where;
    } else {
        *out = EmplaceReallocate(where, value);
    }
    return out;
}

cRZString **cRZStringVector::Erase(cRZString **out, cRZString *where)
{
    CopyForward(where + 1, mLast, where);
    DestroyElement(mLast - 1);
    mLast -= 1;
    *out = where;
    return out;
}

// cRZCmdLine: construction ----------------------------------------------------------------------

cRZCmdLine *cRZCmdLine::Construct()
{
    mVTable = GameAddress(kVTable);
    mRefCount = 0;
    mArgs.mFirst = nullptr;
    mArgs.mLast = nullptr;
    mArgs.mEnd = nullptr;
    mText.Construct();
    mEmpty.Construct();
    return this;
}

cRZCmdLine *cRZCmdLine::Construct(const char *text)
{
    mVTable = GameAddress(kVTable);
    mRefCount = 0;
    mArgs.mFirst = nullptr;
    mArgs.mLast = nullptr;
    mArgs.mEnd = nullptr;
    mText.Construct(text);
    mEmpty.Construct();
    Parse(mText, mArgs);
    return this;
}

cRZCmdLine *cRZCmdLine::Construct(cIGZCmdLine *other)
{
    mVTable = GameAddress(kVTable);
    mRefCount = 0;
    mArgs.mFirst = nullptr;
    mArgs.mLast = nullptr;
    mArgs.mEnd = nullptr;
    mText.Construct();
    mEmpty.Construct();
    // The calls go through other's vtable: GetCommandLineText (slot 5), ArgC (3), ArgV (4).
    VirtualCall<bool(__thiscall *)(void *, cIGZString *), 5>(other, mText.AsInterface());
    int32_t count = VirtualCall<int32_t(__thiscall *)(void *), 3>(other);
    for (int32_t i = 0; i < count; ++i) {
        const char *arg = VirtualCall<const char *(__thiscall *)(void *, int32_t), 4>(other, i);
        Str temp(arg);
        mArgs.PushBackAny(temp.S());
    }
    return this;
}

void cRZCmdLine::Destruct()
{
    mVTable = GameAddress(kVTable);
    mEmpty.Destruct();
    mText.Destruct();
    mArgs.Tidy();
}

cRZCmdLine *cRZCmdLine::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

// cRZCmdLine: parsing ---------------------------------------------------------------------------

void cRZCmdLine::Parse(const cRZString &text, cRZStringVector &out)
{
    Str tok;
    Str copy(text, 0, 0xffffffff);
    out.Clear();
    std::string &s = copy.Text();
    if (s.size() == 0) return;

    // Trim whitespace at both ends.
    while (s.size() != 0 && IsSpace(s.c_str()[s.size() - 1])) s.erase(s.size() - 1, 1);
    while (s.size() != 0 && IsSpace(s.c_str()[0])) s.erase(0, 1);
    if (s.size() == 0) return;

    // Split on whitespace outside double quotes (an odd number of quotes so far means inside).
    int32_t size = static_cast<int32_t>(s.size());
    uint32_t tokenStart = 0;
    uint32_t quotes = 0;
    int32_t i = 0;
    for (; i < size; ++i) {
        char c = s.c_str()[i];
        if (!IsSpace(c)) {
            if (c == '"') ++quotes;
            continue;
        }
        if (quotes & 1) continue;
        quotes = 0;
        tok.Text().assign(s.c_str() + tokenStart, i - tokenStart);
        StripQuotes(tok.Text());
        out.PushBackAny(tok.S());
        // Skip the rest of the whitespace run (the trimmed end guarantees a non-space follows).
        while (IsSpace(s.c_str()[i + 1])) ++i;
        tokenStart = i + 1;
    }
    tok.Text().assign(s.c_str() + tokenStart, i - tokenStart);
    StripQuotes(tok.Text());
    out.PushBackAny(tok.S());
}

void cRZCmdLine::RebuildText(cRZStringVector &args, cRZString &text)
{
    text.mString.erase(static_cast<size_t>(0));
    uint32_t count = static_cast<uint32_t>(args.Count());
    if (count == 0) return;
    for (uint32_t i = 0; i < count;) {
        const std::string &arg = args.mFirst[i].mString;
        bool quoted = false;
        size_t length = arg.size();
        if (length != 0) {
            for (size_t k = 0; k < length; ++k) {
                if (IsSpace(arg.c_str()[k])) {
                    text.mString.push_back('"');
                    quoted = true;
                    break;
                }
            }
        }
        text.mString.append(arg);
        if (quoted) text.mString.push_back('"');
        ++i;
        if (i < count) text.mString.push_back(' ');
    }
}

// cRZCmdLine: slots -----------------------------------------------------------------------------

bool cRZCmdLine::QueryInterface(uint32_t riid, void **ppvObj)
{
    if (riid != 1 && riid != kGZIID_cIGZCmdLine) return false;
    *ppvObj = this;
    VirtualCall<uint32_t(__thiscall *)(void *), kSlotAddRef>(this);
    return true;
}

uint32_t cRZCmdLine::AddRef()
{
    return ++mRefCount;
}

uint32_t cRZCmdLine::Release()
{
    if (mRefCount > 1) return --mRefCount;
    VirtualCall<void *(__thiscall *)(void *, uint32_t), kSlotDeletingDestructor>(this, 1);
    return 0;
}

int32_t cRZCmdLine::ArgC() const
{
    return mArgs.Count();
}

const char *cRZCmdLine::ArgV(int32_t index) const
{
    // No lower bound check: a negative index reads before the array.
    const cRZString *arg = index < mArgs.Count() ? mArgs.mFirst + index : nullptr;
    const std::string &s = arg ? arg->mString : mEmpty.mString;
    return s.c_str();
}

cRZString *cRZCmdLine::GetArgument(int32_t index) const
{
    if (index < mArgs.Count()) return mArgs.mFirst + index;
    return const_cast<cRZString *>(&mEmpty);
}

bool cRZCmdLine::GetCommandLineText(cIGZString &out) const
{
    out.Copy(*mText.AsInterface());
    return true;
}

int32_t cRZCmdLine::Find(const cIGZString &text, bool caseSensitive) const
{
    uint32_t count = static_cast<uint32_t>(mArgs.Count());
    Str needle(text);
    Str temp;
    if (!caseSensitive) needle.Lower();
    if (count == 0) return -1;
    for (uint32_t i = 0; i < count; ++i) {
        const std::string &arg = mArgs.mFirst[i].mString;
        bool equal;
        if (caseSensitive) {
            equal = needle.Text() == arg;
        } else {
            AssignString(temp.Text(), arg);
            temp.Lower();
            equal = needle.Text() == temp.Text();
        }
        if (equal) return static_cast<int32_t>(i);
    }
    return -1;
}

int32_t cRZCmdLine::FindSubString(const cIGZString &text, bool caseSensitive) const
{
    uint32_t count = static_cast<uint32_t>(mArgs.Count());
    Str needle(text);
    Str temp;
    if (!caseSensitive) needle.Lower();
    if (count == 0) return -1;
    for (uint32_t i = 0; i < count; ++i) {
        const std::string &arg = mArgs.mFirst[i].mString;
        // An argument matches when the position of the first occurrence is below its length, so an
        // empty needle never matches an empty argument.
        size_t position;
        size_t length;
        if (caseSensitive) {
            position = arg.find(needle.Text(), 0);
            length = arg.size();
        } else {
            AssignString(temp.Text(), arg);
            temp.Lower();
            position = temp.Text().find(needle.Text(), 0);
            length = temp.Text().size();
        }
        if (position < length) return static_cast<int32_t>(i);
    }
    return -1;
}

bool cRZCmdLine::IsSwitchPresent(const cIGZString &sw, cIGZString &param, bool trimQuotes) const
{
    if (sw.Strlen() == 0) return false;
    Str name(sw);
    name.Lower();
    std::string prefixes(kSwitchPrefixes);
    size_t nameLength = name.Text().size();
    for (const cRZString *arg = mArgs.mFirst; arg != mArgs.mLast; ++arg) {
        const std::string &s = arg->mString;
        // "<prefix>name:value": a prefix, the name, a colon and at least one character.
        if (!IsPrefix(prefixes, s.c_str()[0])) continue;
        if (s.size() < nameLength + 3) continue;
        if (s.c_str()[nameLength + 1] != ':') continue;
        Str lowered(*arg, 0, 0xffffffff);
        lowered.Lower();
        size_t k = 0;
        while (k < nameLength && name.Text().c_str()[k] == lowered.Text().c_str()[k + 1]) ++k;
        if (k != nameLength) continue;

        Str value;
        value.Text().append(s, nameLength + 2, std::string::npos);
        if (trimQuotes) {
            std::string &v = value.Text();
            char first = v.c_str()[0];
            char last = v.c_str()[v.size() - 1];
            if ((first == '"' || first == '\'') && (last == '"' || last == '\'')) {
                v.erase(0, 1);
                // ORIGINAL-BUG: a value that is a single quote character (`/name:"`) is erased by
                // the first erase, and the second erase then starts at size - 1 = -1, which throws
                // std::out_of_range.
                v.erase(v.size() - 1, 1);
            }
        }
        param.Copy(*value.S().AsInterface());
        return true;
    }
    return false;
}

bool cRZCmdLine::IsSwitchPresent(const cIGZString &sw) const
{
    Str param;
    // GetIndexOfSwitch and IsSwitchPresent are called through this's vtable, like the original.
    if (VirtualCall<int32_t(__thiscall *)(const void *, const cIGZString *, int32_t), 11>(this, &sw, 0) < 0) {
        // The original returns 1 when the slot's al is non-zero, whatever the byte is.
        uint8_t present = VirtualCall<uint8_t(__thiscall *)(const void *, const cIGZString *, cIGZString *, bool), 8>(this, &sw, param.S().AsInterface(), true);
        return present != 0;
    }
    return true;
}

bool cRZCmdLine::IsSwitchPresent(char sw, cIGZString &param, bool trimQuotes) const
{
    std::string prefixes(kSwitchPrefixes);
    Str value;
    CaseVariants v = VariantsOf(sw);
    bool found = false;
    for (const cRZString *arg = mArgs.mFirst; arg != mArgs.mLast; ++arg) {
        const std::string &s = arg->mString;
        size_t length = s.size();
        if (length < 2) continue;
        if (!IsPrefix(prefixes, s.c_str()[0])) continue;
        char second = s.c_str()[1];
        if (second != v.lower && second != v.upper) continue;
        if (length == 2) {
            value.Text().resize(0, '\0');
        } else {
            const char *data = s.c_str();
            if (trimQuotes && length >= 4 && data[2] == '"' && data[length - 1] == '"') {
                if (length == 4) value.Text().resize(0, '\0');
                else value.Text().append(s, 3, length - 4);
            } else {
                value.Text().append(s, 2, length - 2);
            }
            param.Copy(*value.S().AsInterface());
        }
        found = true;
        break;
    }
    return found;
}

int32_t cRZCmdLine::GetIndexOfSwitch(const cIGZString &sw, int32_t start) const
{
    int32_t first = start >= 0 ? start : 0;
    if (first >= mArgs.Count()) return -1;
    std::string prefixes(kSwitchPrefixes);
    uint32_t count = static_cast<uint32_t>(mArgs.Count());
    for (uint32_t i = static_cast<uint32_t>(first); i < count; ++i) {
        const cRZString *arg = mArgs.mFirst + i;
        uint32_t length = static_cast<uint32_t>(arg->mString.size());
        if (length < 2) continue;
        if (!IsPrefix(prefixes, arg->mString.c_str()[0])) continue;
        if (sw.Strlen() != length - 1) continue;
        Str name(sw);
        name.Lower();
        Str rest(*arg, 0, 0xffffffff);
        rest.Text().erase(0, 1);
        rest.Lower();
        if (rest.Text().find(name.Text(), 0) == 0) return static_cast<int32_t>(i);
    }
    return -1;
}

int32_t cRZCmdLine::GetIndexOfSwitch(char sw, int32_t start) const
{
    int32_t first = start >= 0 ? start : 0;
    if (first >= mArgs.Count()) return -1;
    std::string prefixes(kSwitchPrefixes);
    CaseVariants v = VariantsOf(sw);
    uint32_t count = static_cast<uint32_t>(mArgs.Count());
    for (uint32_t i = static_cast<uint32_t>(first); i < count; ++i) {
        const std::string &s = mArgs.mFirst[i].mString;
        if (s.size() != 2) continue;
        if (!IsPrefix(prefixes, s.c_str()[0])) continue;
        char second = s.c_str()[1];
        if (second == v.lower || second == v.upper) return static_cast<int32_t>(i);
    }
    return -1;
}

bool cRZCmdLine::InsertArgument(const cIGZString &arg, int32_t index)
{
    int32_t count = mArgs.Count();
    int32_t at;
    if (count == 0) {
        at = 0;
    } else if (index < 0) {
        at = 0;
    } else {
        at = index >= count ? count : index;
    }
    cRZString *begin = mArgs.mFirst;
    {
        Str value(arg);
        cRZString *where = begin + at;
        cRZString *result;
        mArgs.Insert(&result, where, value.S());
    }
    RebuildText(mArgs, mText);
    return true;
}

bool cRZCmdLine::EraseArgument(int32_t index)
{
    int32_t count = mArgs.Count();
    if (index >= count) index = count - 1;
    // ORIGINAL-BUG: with no arguments, index is -1 and the erase works on the slot before the
    // (null) array: it destroys the element at 0xffffffe0 and crashes. A negative index is not
    // clamped either.
    cRZString *result;
    mArgs.Erase(&result, mArgs.mFirst + index);
    RebuildText(mArgs, mText);
    return true;
}
