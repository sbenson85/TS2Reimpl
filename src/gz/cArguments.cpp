// nGZCommandParser::cArguments (src/gz/cArguments.h). RVAs are 1.18.0.187.
#include "gz/cArguments.h"

#include "gz/CommandParser.h" // ThrowFormatted

#include <algorithm>
#include <cstring>
#include <new>
#include <string>

namespace nGZCommandParser {

namespace {

// The parser's character classes (the game's table at 0xe7d528: bit 1 blanks, bit 2 letters, bit 4 digits; bytes from
// 0x80 up are in none).
bool IsBlank(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
bool IsLetter(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

} // namespace

int cArguments::SwitchOrder::Compare(const char *a, const char *b) const
{
    return caseSensitive ? std::strcmp(a, b) : _stricmp(a, b);
}

cArguments::cArguments() : mPositional(0), mSkipFirst(0), mCaseSensitive(0) {}

// ORIGINAL-BUG: the copy leaves the case flag (+0x15) as the memory held it (the assignment does not copy it).
cArguments::cArguments(const cArguments &other) { *this = other; }

cArguments &cArguments::operator=(const cArguments &other)
{
    if (this == &other) return *this;
    mBuffer = other.mBuffer;
    mPositional = other.mPositional;
    mSkipFirst = other.mSkipFirst; // ORIGINAL-BUG: mCaseSensitive is not copied
    // The pointers into the other's buffer, moved to the same places in ours.
    const char *from = other.mBuffer.data();
    char *to = mBuffer.data();
    mArguments.resize(other.mArguments.size());
    for (size_t i = 0; i < mArguments.size(); ++i) mArguments[i] = to + (other.mArguments[i] - from);
    mSwitches.resize(other.mSwitches.size());
    for (size_t i = 0; i < mSwitches.size(); ++i) {
        const Switch &source = other.mSwitches[i];
        Switch &target = mSwitches[i];
        target.name = to + (source.name - from);
        target.first = source.first;
        target.count = source.count;
        target.used = source.used;
    }
    return *this;
}

cArguments::~cArguments() = default;

void cArguments::Parse(const char *text, uint8_t skipFirst, uint8_t caseSensitive)
{
    mSkipFirst = skipFirst;
    mCaseSensitive = caseSensitive;
    mArguments.clear();
    mArguments.reserve(10);
    mSwitches.clear();
    mSwitches.reserve(10);
    // Room for every word with its NUL (the buffer only grows).
    size_t room = std::strlen(text) + 2;
    if (mBuffer.size() < room) mBuffer.resize(room);
    size_t used = 0;
    mPositional = 0;
    const unsigned char *scan = reinterpret_cast<const unsigned char *>(text);
    for (;;) {
        while (IsBlank(*scan)) ++scan;
        unsigned char first = *scan;
        if (first == 0) break;
        // The word's end: a blank outside parentheses, a quote or an opening parenthesis after its start, the closing
        // quote of a word that starts with one, the closing parenthesis of a word that starts with one.
        const unsigned char *start = scan;
        int32_t depth = 0;
        for (unsigned char c = first; c != 0; c = *++scan) {
            if (c == '"') {
                if (depth == 0 && start != scan) break;
                do {
                    if (*++scan == 0) ThrowFormatted("missing end quote");
                } while (*scan != '"');
                if (depth == 0) break;
            } else if (c == '(') {
                if (depth == 0 && start != scan) break;
                ++depth;
            } else if (c == ')') {
                if (depth == 0) ThrowFormatted("unexpected ')'");
                if (--depth == 0) break;
            } else if (depth == 0 && IsBlank(c)) {
                break;
            }
        }
        if (*scan == 0 && depth != 0) ThrowFormatted("missing ')'");
        size_t length = static_cast<size_t>(scan - start);
        if (first == '"' || first == '(') {
            // The quotes or parentheses are not part of the word.
            ++start;
            ++scan;
            --length;
        }
        if (first == '-' && length == 1) ThrowFormatted("illegal switch string");
        char *word = mBuffer.data() + used;
        mArguments.push_back(word);
        if (first == '-' && IsLetter(start[1])) {
            mSwitches.push_back(Switch{});
            Switch &added = mSwitches.back();
            added.name = word + 1;
            added.first = static_cast<uint32_t>(mArguments.size());
            added.count = 0;
            added.used = 0;
        } else if (mSwitches.empty()) {
            ++mPositional;
        } else {
            ++mSwitches.back().count;
        }
        std::memcpy(word, start, length);
        word[length] = 0;
        used += length + 1;
    }
    mArguments.push_back(mBuffer.data() + used);
    std::stable_sort(mSwitches.begin(), mSwitches.end(), SwitchOrder{mCaseSensitive != 0});
    if (mPositional != 0 && mSkipFirst != 0) --mPositional;
}

bool cArguments::TakeSwitch(const char *name)
{
    auto range = std::equal_range(mSwitches.begin(), mSwitches.end(), name, SwitchOrder{mCaseSensitive != 0});
    if (range.first == range.second) return false;
    if (range.first->count != 0) ThrowFormatted("Not expecting any arguments for switch '%s'", name);
    range.first->used = 1;
    return true;
}

const char *const *cArguments::TakeSwitchArguments(const char *name, uint32_t *count, uint32_t least, uint32_t most)
{
    auto range = std::equal_range(mSwitches.begin(), mSwitches.end(), name, SwitchOrder{mCaseSensitive != 0});
    if (range.first == range.second) return nullptr;
    uint32_t given = range.first->count;
    if (given < least) ThrowFormatted("Expecting at least %d arguments for switch '%s'", least, name);
    if (given > most) ThrowFormatted("Expecting at most %d arguments for switch '%s'", most, name);
    if (count) *count = given;
    range.first->used = 1;
    return mArguments.data() + range.first->first;
}

const char *const *cArguments::TakeSwitchArgumentsExactly(const char *name, uint32_t count)
{
    return TakeSwitchArguments(name, &count, count, count);
}

const char *cArguments::Argument(uint32_t index) { return mArguments[index]; }

void cArguments::GetAll(int32_t *count, const char *const **arguments)
{
    *count = static_cast<int32_t>(mArguments.size()) - 1;
    *arguments = mArguments.data();
}

int32_t cArguments::ArgumentLength(uint32_t index)
{
    return static_cast<int32_t>(mArguments[index + 1] - mArguments[index]) - 1;
}

int32_t cArguments::Count() { return static_cast<int32_t>(mArguments.size()) - 1; }

uint32_t cArguments::PositionalCount() { return mPositional; }

int32_t cArguments::SwitchCount() { return static_cast<int32_t>(mSwitches.size()); }

const char *const *cArguments::Positional(uint32_t *count, uint32_t least, uint32_t most)
{
    uint32_t given = mPositional;
    if (given < least) ThrowFormatted("Expecting at least %d arguments", least);
    if (given > most) ThrowFormatted("Expecting at most %d arguments", most);
    if (count) *count = given;
    if (mPositional == 0) return nullptr;
    return mArguments.data() + (mSkipFirst != 0 ? 1 : 0);
}

const char *const *cArguments::PositionalExactly(uint32_t count) { return Positional(&count, count, count); }

bool cArguments::HasSwitch(const char *name)
{
    SwitchOrder order{mCaseSensitive != 0};
    auto found = std::lower_bound(mSwitches.begin(), mSwitches.end(), name, order);
    return found != mSwitches.end() && !order(name, *found);
}

int32_t cArguments::UnusedSwitchCount()
{
    int32_t unused = 0;
    for (const Switch &s : mSwitches)
        if (!s.used) ++unused;
    return unused;
}

void cArguments::GetUnusedSwitches(NameVector *names)
{
    names->clear();
    for (const Switch &s : mSwitches)
        if (!s.used) names->push_back(s.name);
}

void cArguments::CheckSwitchesUsed()
{
    if (UnusedSwitchCount() == 0) return;
    std::string list;
    for (const Switch &s : mSwitches)
        if (!s.used) list += std::string(" ") + s.name;
    ThrowFormatted("Unhandled switches: %s", list.c_str());
}

// ---- adapters ----

cArguments *__fastcall cArgumentsAdapters::Construct(void *memory, void *) { return ::new (memory) cArguments; }

cArguments *__fastcall cArgumentsAdapters::ConstructCopy(void *memory, void *, const cArguments *other)
{
    return ::new (memory) cArguments(*other);
}

cArguments *__fastcall cArgumentsAdapters::Assign(cArguments *self, void *, const cArguments *other)
{
    [[msvc::forceinline_calls]] return &self->cArguments::operator=(*other);
}

void __fastcall cArgumentsAdapters::Destruct(cArguments *self, void *) { self->cArguments::~cArguments(); }

cArguments *__fastcall cArgumentsAdapters::DeletingDestructor(cArguments *self, void *, uint32_t flags)
{
    self->cArguments::~cArguments();
    if (flags & 1) ::operator delete(self, sizeof(cArguments));
    return self;
}

void __fastcall cArgumentsAdapters::Parse(cArguments *self, void *, const char *text, uint8_t skipFirst, uint8_t caseSensitive)
{
    [[msvc::forceinline_calls]] self->cArguments::Parse(text, skipFirst, caseSensitive);
}

bool __fastcall cArgumentsAdapters::TakeSwitch(cArguments *self, void *, const char *name)
{
    [[msvc::forceinline_calls]] return self->cArguments::TakeSwitch(name);
}

const char *const *__fastcall cArgumentsAdapters::TakeSwitchArguments(cArguments *self, void *, const char *name,
                                                                       uint32_t *count, uint32_t least, uint32_t most)
{
    [[msvc::forceinline_calls]] return self->cArguments::TakeSwitchArguments(name, count, least, most);
}

const char *const *__fastcall cArgumentsAdapters::TakeSwitchArgumentsExactly(cArguments *self, void *, const char *name,
                                                                              uint32_t count)
{
    [[msvc::forceinline_calls]] return self->cArguments::TakeSwitchArgumentsExactly(name, count);
}

const char *__fastcall cArgumentsAdapters::Argument(cArguments *self, void *, uint32_t index)
{
    [[msvc::forceinline_calls]] return self->cArguments::Argument(index);
}

void __fastcall cArgumentsAdapters::GetAll(cArguments *self, void *, int32_t *count, const char *const **arguments)
{
    [[msvc::forceinline_calls]] self->cArguments::GetAll(count, arguments);
}

int32_t __fastcall cArgumentsAdapters::ArgumentLength(cArguments *self, void *, uint32_t index)
{
    [[msvc::forceinline_calls]] return self->cArguments::ArgumentLength(index);
}

int32_t __fastcall cArgumentsAdapters::Count(cArguments *self, void *)
{
    [[msvc::forceinline_calls]] return self->cArguments::Count();
}

int32_t __fastcall cArgumentsAdapters::SwitchCount(cArguments *self, void *)
{
    [[msvc::forceinline_calls]] return self->cArguments::SwitchCount();
}

const char *const *__fastcall cArgumentsAdapters::Positional(cArguments *self, void *, uint32_t *count, uint32_t least,
                                                              uint32_t most)
{
    [[msvc::forceinline_calls]] return self->cArguments::Positional(count, least, most);
}

const char *const *__fastcall cArgumentsAdapters::PositionalExactly(cArguments *self, void *, uint32_t count)
{
    [[msvc::forceinline_calls]] return self->cArguments::PositionalExactly(count);
}

bool __fastcall cArgumentsAdapters::HasSwitch(cArguments *self, void *, const char *name)
{
    [[msvc::forceinline_calls]] return self->cArguments::HasSwitch(name);
}

int32_t __fastcall cArgumentsAdapters::UnusedSwitchCount(cArguments *self, void *)
{
    [[msvc::forceinline_calls]] return self->cArguments::UnusedSwitchCount();
}

void __fastcall cArgumentsAdapters::GetUnusedSwitches(cArguments *self, void *, NameVector *names)
{
    [[msvc::forceinline_calls]] self->cArguments::GetUnusedSwitches(names);
}

void __fastcall cArgumentsAdapters::CheckSwitchesUsed(cArguments *self, void *)
{
    [[msvc::forceinline_calls]] self->cArguments::CheckSwitchesUsed();
}

} // namespace nGZCommandParser
