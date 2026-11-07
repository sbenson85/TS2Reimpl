#define _CRT_SECURE_NO_WARNINGS // the original uses the unchecked _vsnprintf family: so do we

#include "gz/CommandParser.h"

#include "gz/MacroDefinition.h"
#include "gz/cBaseParser.h"
#include "gz/cParserTexts.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "rz/TopCallersA.h"
#include "rz/cRZString.h"
#include "rz/cRZVariant.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <intrin.h>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>
#include <vector>

namespace nGZCommandParser {

namespace {

// The vtable of an object, read once, as the original does before it calls the argument list.
using VTable = const void *const *;
inline VTable VTableOf(const void *object) { return *static_cast<const VTable *>(object); }

// Argument list (cIArguments) slots used by the commands.
using ArgsVoidFn = void(__thiscall *)(void *);                                   // slot 15, 0x3c
using ArgsGetFn = const char *const *(__thiscall *)(void *, uint32_t);           // slot 11, 0x2c

using ArgsFindFn = const char *const *(__thiscall *)(void *, const char *, uint32_t); // slot 3, 0xc

inline void ArgsPrepare(void *args) { VirtualCall<ArgsVoidFn, 15>(args); }
inline const char *const *ArgsGet(void *args, uint32_t count)
{
    return VirtualCall<ArgsGetFn, 11>(args, count);
}

// Parser (cIBaseParser) slots used by the commands.
using ParserTextFn = void(__thiscall *)(void *, const char *);                   // slot 15, 0x3c
using ParserTextIntFn = void(__thiscall *)(void *, const char *, uint32_t);      // slot 12, 0x30
using ParserBoolFn = uint8_t(__thiscall *)(void *, const char *);                // slot 30, 0x78 (al)

// State slots: the variable store (slot 1) and the output object's text writer (slot 14).
using StateSetFn = void(__thiscall *)(void *, const char *, const char *);       // slot 1, +4
using OutputWriteFn = void(__thiscall *)(void *, const char *);                  // slot 14, 0x38

inline void *StateOutput(void *state) { return *reinterpret_cast<void **>(static_cast<char *>(state) + 0x14); }

// The parser state has a virtual base at +8: its offset is the third word of the vbtable that the
// word at +8 points to. Some commands call that base (the scope handling) through its own vtable.
inline void *StateVirtualBase(void *state)
{
    char *s = static_cast<char *>(state);
    const int32_t *vbtable = *reinterpret_cast<const int32_t *const *>(s + 8);
    return s + 8 + vbtable[2];
}
inline VTable StateVirtualBaseVTable(void *state)
{
    char *s = static_cast<char *>(state);
    const int32_t *vbtable = *reinterpret_cast<const int32_t *const *>(s + 8);
    return *reinterpret_cast<const VTable *>(s + vbtable[2] + 8);
}

// The conditional commands keep a stack of entries in the parser state (a vector at +0x110): one
// for the enclosing text and one for each open if. `level` is how many entries the matching endif
// removes; `active` says whether the text of the current branch is being run.
struct ConditionEntry {
    int32_t level;
    uint8_t active;
    uint8_t elseSeen;
    uint8_t pad[2]; // not written by the original's code on some paths: stack garbage, see ElseIf
};
static_assert(sizeof(ConditionEntry) == 8, "ConditionEntry layout");
using ConditionStack = std::vector<ConditionEntry>;
inline ConditionStack &Conditions(void *state) { return *reinterpret_cast<ConditionStack *>(static_cast<char *>(state) + 0x110); }
inline bool TraceOn(void *state) { return static_cast<uint8_t *>(state)[0xec] != 0; }

inline void Write(void *output, const char *text) { VirtualCall<void(__thiscall *)(void *, const char *), 14>(output, text); }

// Indents the trace output: one "   " for each entry of the condition stack beyond `skip`. The
// state is read again at every step. The count is unsigned: a stack with fewer than `skip` entries
// would loop for a very long time; the callers have checked the depth.
void TraceIndent(void *const *stateSlot, uint32_t skip)
{
    uint32_t n = static_cast<uint32_t>(Conditions(*stateSlot).size()) - skip;
    if (n == 0) return;
    uint32_t i = 0;
    do {
        Write(StateOutput(*stateSlot), ts2_rva_dfd044);
        ++i;
        n = static_cast<uint32_t>(Conditions(*stateSlot).size()) - skip;
    } while (i < n);
}

constexpr const char *kFalseWord = ts2_rva_da2d3c; // "false"
constexpr const char *kTrueWord = ts2_rva_da2d44;  // "true"

using ArgsCountFn = int32_t(__thiscall *)(void *);                               // slot 7, 0x1c
using ArgsTextFn = const char *(__thiscall *)(void *, uint32_t);                 // slot 4, 0x10
using CommandVoidFn = void(__thiscall *)(void *);                                // slots 5, 9 of a command
using CommandIntFn = void(__thiscall *)(void *, uint32_t);                       // slot 6 of a command
using ScopeFn = void(__thiscall *)(void *, const char *);                        // slots 20 and 22 of the scope base
using ScopeObjectFn = void(__thiscall *)(void *, void *);                        // slot 16 of the scope base
using ParserIntFn = int32_t(__thiscall *)(void *, const char *);                 // slot 32, 0x80
using ParserTextTextFn = void(__thiscall *)(void *, const char *, const char *); // output object slot 1

constexpr const char *kWrongNumberOfArguments = ts2_rva_db59cc;

using ArgsRangeFn = const char *const *(__thiscall *)(void *, int32_t *, uint32_t, uint32_t); // slot 10, 0x28
using ParserRegisterFn = void(__thiscall *)(void *, void *);                      // slot 4, 0x10
using ParserFlagsFn = void(__thiscall *)(void *, const char *, uint32_t);         // slot 13, 0x34

// A cRZString local, built in place the way the original does (empty, count 0) and released when
// it goes out of scope (the original's frame does that too, also when something throws).
struct LocalString {
    alignas(cRZString) unsigned char raw[sizeof(cRZString)];
    LocalString() { get()->Construct(); }
    ~LocalString() { get()->Destruct(); }
    explicit LocalString(const char *text) { get()->Construct(text); }
    LocalString(const LocalString &) = delete;
    cRZString *get() { return reinterpret_cast<cRZString *>(raw); }
};

constexpr const char *kTrueString = ts2_rva_da4fac;  // what the set-boolean command stores for a true value
constexpr const char *kFalseString = ts2_rva_da4af4; // ... and for a false one
constexpr const char *kNewlineString = ts2_rva_d9e644; // the text the trace command writes after its message

} // namespace


// ---- number formatting ----------------------------------------------------------------------------

namespace {

// The text did not fit: a NUL goes at the last position used (or the first), and the result is false.
bool FormatOverflow(char *buf, int pos)
{
    int at = pos - 1;
    if (at < 0) at = 0;
    buf[at] = 0;
    return false;
}

} // namespace

bool __cdecl FormatDecimal(double value, char *buf, int size, int precision)
{
    int pos = 0, index = 0;
    if (size <= 0) return false;
    int decimalPoint = 0, sign = 0;
    const char *digits = _fcvt(value, 0x40, &decimalPoint, &sign);
    if (sign != 0) buf[pos++] = '-';
    if (1.0 > std::fabs(value)) {
        if (pos >= size) return FormatOverflow(buf, pos);
        buf[pos++] = '0';
    }
    // The digits before the decimal point.
    if (decimalPoint > 0) {
        do {
            char d = digits[index];
            if (d == 0) break;
            if (pos >= size) return FormatOverflow(buf, pos);
            buf[pos++] = d;
            ++index;
        } while (index < decimalPoint);
    }
    // The position after the last nonzero digit still to be written.
    if (digits[index] != 0) {
        int lastNonZero = index;
        for (const char *d = digits + index; *d != 0; ++d)
            if (*d != '0') lastNonZero = static_cast<int>(d - digits) + 1;
        if (lastNonZero > index && precision > 0) {
            if (pos >= size) return FormatOverflow(buf, pos);
            buf[pos++] = '.';
            // A value below 0.1: the zeros between the point and the first digit.
            if (decimalPoint < 0) {
                do {
                    if (pos >= size) return FormatOverflow(buf, pos);
                    buf[pos++] = '0';
                    ++decimalPoint;
                } while (decimalPoint < 0);
            }
            for (int written = 0; written < precision;) {
                if (index >= lastNonZero) break;
                char d = digits[index];
                if (d == 0) break;
                if (pos >= size) {
                    // Out of room in the fraction: the text is cut back to the last digit that is not
                    // a zero, and the result is false.
                    int at = pos - 1;
                    if (at < 0) at = 0;
                    for (;;) {
                        buf[at] = 0;
                        --at;
                        if (at <= 0) break;
                        if (buf[at] != '0') break;
                    }
                    return false;
                }
                buf[pos++] = d;
                ++index;
                ++written;
            }
        }
    }
    if (pos >= size) return FormatOverflow(buf, pos);
    buf[pos] = 0;
    return true;
}

bool __cdecl FormatGeneral(double value, char *buf, int size, int precision)
{
    if (value == 0.0) return FormatDecimal(value, buf, size, precision);
    int32_t exponent = static_cast<int32_t>(std::floor(std::log10(std::fabs(value))));
    if (exponent < precision && exponent >= -4) return FormatDecimal(value, buf, size, precision);
    // Scientific notation: the room kept for the exponent is its digit count, from the exponent as
    // a number (a negative one counts as one digit) plus the 'e' and the sign.
    int digitsOfExponent = 1;
    for (int32_t limit = 10; exponent >= limit; limit *= 10) ++digitsOfExponent;
    double mantissa = std::pow(10.0, static_cast<double>(-exponent)) * value;
    if (!FormatDecimal(mantissa, buf, size - digitsOfExponent - 2, precision - 1)) return false;
    char *end = buf + std::strlen(buf);
    end[0] = 'e';
    end[1] = exponent < 0 ? '-' : '+';
    int32_t magnitude = exponent < 0 ? -exponent : exponent;
    _itoa(magnitude, end + 2, 10);
    return true;
}

bool __cdecl PrintDoubles(char *out, int size, const char *fmt, ...)
{
    char *end = out + size;
    char *p = out;
    va_list ap;
    va_start(ap, fmt);
    char c = *fmt++;
    while (c != 0) {
        size_t advance;
        if (c == '%') {
            double value = va_arg(ap, double);
            ++fmt; // the conversion character
            if (!FormatGeneral(value, p, static_cast<int>(end - p), 6)) {
                va_end(ap);
                return false;
            }
            advance = std::strlen(p);
        } else {
            if (p == end) {
                va_end(ap);
                return false;
            }
            *p = c;
            advance = 1;
        }
        c = *fmt;
        p += advance;
        ++fmt;
    }
    va_end(ap);
    if (p == end) return false;
    *p = 0;
    return true;
}

// ---- name tables ----------------------------------------------------------------------------------

bool KeyRange::EntryBeforeKey(const EnumEntry *entry, uint32_t /*unused*/) const
{
    size_t keyLength = static_cast<size_t>(end - begin);
    if (entry->name.size() != keyLength) return entry->name.size() < keyLength;
    // Same length: the bytes are compared from the last one backwards, as signed chars.
    const char *e = entry->name.c_str() + entry->name.size();
    const char *k = end;
    while (e != entry->name.c_str()) {
        if (k == begin) return false;
        --e;
        --k;
        if (*k > *e) return true;
        if (*k < *e) return false;
    }
    return false;
}

bool KeyRange::KeyBeforeEntry(uint32_t /*unused*/, const EnumEntry *entry) const
{
    size_t keyLength = static_cast<size_t>(end - begin);
    if (keyLength != entry->name.size()) return keyLength < entry->name.size();
    const char *e = entry->name.c_str() + entry->name.size();
    const char *k = end;
    while (k != begin) {
        if (e == entry->name.c_str()) return false;
        --e;
        --k;
        if (*e > *k) return true;
        if (*e < *k) return false;
    }
    return false;
}

namespace {

struct EnumOrder {
    bool operator()(const EnumEntry &entry, const KeyRange &key) const { return key.EntryBeforeKey(&entry, 0); }
    bool operator()(const KeyRange &key, const EnumEntry &entry) const { return key.KeyBeforeEntry(0, &entry); }
};

} // namespace

void __cdecl FindEnumRange(EntryRange *out, const EnumEntry *first, const EnumEntry *last, const void * /*keyWord*/, KeyRange key)
{
    auto range = std::equal_range(first, last, key, EnumOrder{});
    out->first = range.first;
    out->last = range.second;
}

int32_t EnumTable::Lookup(const char *name) const
{
    KeyRange key{name + std::strlen(name), name};
    EntryRange range;
    FindEnumRange(&range, entries.data(), entries.data() + entries.size(), &name, key);
    if (range.first == range.last) ThrowFormatted(ts2_rva_e7d628, name);
    return range.first->value;
}

// ---- the command table of a block -----------------------------------------------------------------

namespace {

using NodeVoidFn = void(__thiscall *)(void *);

bool SameBytes(const char *a, const char *b, uint32_t length)
{
    return length == 0 || std::memcmp(a, b, length) == 0;
}

// The original moves a node in the list with a fixed order of pointer writes, inline in several
// places: `node` (whose successor is `next`) goes just before `target`. The writes are kept in
// order, since they give different lists when the nodes are next to each other.
void MoveBefore(CommandNode *node, CommandNode *next, CommandNode *target)
{
    CommandNode *before = node->prev;
    before->next = next;
    CommandNode *nextPrev = next->prev;
    nextPrev->next = target;
    CommandNode *targetPrev = target->prev;
    targetPrev->next = node;
    target->prev = nextPrev;
    next->prev = before;
    node->prev = targetPrev;
}

// The bucket vector is filled as the original (a vector helper, 0x15fdc0) does it: when it already
// has n elements or more they are all set to `value` and its size does not change; otherwise a block
// of exactly n elements replaces it.
void FillBuckets(std::vector<CommandNode *> &buckets, size_t n, CommandNode *value)
{
    if (buckets.size() >= n) {
        std::fill(buckets.begin(), buckets.end(), value);
        return;
    }
    std::vector<CommandNode *> fresh;
    fresh.reserve(n);
    fresh.assign(n, value);
    buckets.swap(fresh);
}

} // namespace

uint32_t HashName(const char *name, uint32_t length)
{
    uint32_t hash = 0;
    for (uint32_t i = 0; i < length; ++i) {
        uint32_t sum = hash + static_cast<uint32_t>(static_cast<int32_t>(static_cast<signed char>(name[i])));
        hash = sum + (sum < hash ? 1u : 0u);
    }
    return hash;
}

FindResult *CommandTable::Find(FindResult *out, const KeyView *key, uint32_t hash) const
{
    CommandNode *const *bucket = buckets.data() + 2 * (hash & mask);
    CommandNode *last = bucket[1];
    if (last == head) {
        out->position = head;
        out->found = nullptr;
        return out;
    }
    CommandNode *first = bucket[0];
    for (CommandNode *node = last;; node = node->prev) {
        if (key->length == node->keyLength && SameBytes(key->ptr, node->key, key->length)) {
            out->position = node->next;
            out->found = node;
            return out;
        }
        if (node == first) {
            out->position = node;
            out->found = nullptr;
            return out;
        }
    }
}

InsertResult *CommandTable::Insert(InsertResult *out, const KeyView *key)
{
    uint32_t hash = HashName(key->ptr, key->length);
    FindResult where;
    Find(&where, key, hash);
    if (where.found) {
        out->node = where.found;
        out->inserted = 0;
        return out;
    }
    if (size == 0xccccccc) throw std::range_error("list<T> too long");
    auto *node = static_cast<CommandNode *>(::operator new(sizeof(CommandNode)));
    node->key = key->ptr;
    node->keyLength = key->length;
    node->value = nullptr;
    CommandNode *position = where.position;
    // Grow the table first when the new node would pass the load limit: the bucket count then becomes
    // 8 times the old one (while that is below 512 and enough), or the count that is needed.
    float afterInsert = static_cast<float>(size + 1);
    if (afterInsert / static_cast<float>(bucketCount) > maxLoad) {
        float needed = static_cast<float>(std::ceil(static_cast<double>(afterInsert / maxLoad)));
        uint32_t wanted = static_cast<uint32_t>(static_cast<int64_t>(needed));
        uint32_t target = wanted > 8 ? wanted : 8;
        uint32_t request;
        if (bucketCount >= target) request = bucketCount;
        else if (bucketCount >= 0x200) request = target;
        else request = (bucketCount << 3) >= target ? (bucketCount << 3) : target;
        try {
            Rehash(request);
        } catch (...) {
            ::operator delete(node, sizeof(CommandNode));
            throw;
        }
        KeyView nodeKey{node->key, node->keyLength};
        FindResult again;
        Find(&again, &nodeKey, hash);
        position = again.position;
    }
    CommandNode *previous = position->prev;
    ++size;
    node->prev = previous;
    node->next = position;
    previous->next = node;
    position->prev = node;
    CommandNode **bucket = buckets.data() + 2 * (hash & mask);
    CommandNode *first = bucket[0];
    if (first == head) {
        bucket[0] = node;
        bucket[1] = node;
    } else if (first == position) {
        bucket[0] = node;
    } else if (bucket[1] == previous) {
        bucket[1] = node;
    }
    out->node = node;
    out->inserted = 1;
    return out;
}

void CommandTable::Rehash(uint32_t n)
{
    if (n > 0x10000000) throw std::range_error("invalid hash bucket count");
    unsigned long top;
    _BitScanReverse(&top, (n - 1) | 1);
    uint32_t count = 1u << ((top + 1) & 31); // the shift count is taken mod 32, as the x86 shift does
    CommandNode *sentinel = head;
    FillBuckets(buckets, 2 * static_cast<size_t>(count), sentinel);
    bucketCount = count;
    mask = count - 1;
    // Every node goes into the bucket of its hash; nodes with equal keys end up next to each other.
    CommandNode *node = sentinel->next;
    if (node == sentinel) return;
    do {
        CommandNode *next = node->next;
        uint32_t hash = HashName(node->key, node->keyLength);
        CommandNode **bucket = buckets.data() + 2 * (hash & mask);
        CommandNode *first = bucket[0];
        if (first == sentinel) {
            bucket[0] = node;
            bucket[1] = node;
        } else {
            CommandNode *last = bucket[1];
            if (node->keyLength == last->keyLength && SameBytes(node->key, last->key, node->keyLength)) {
                // Equal to the last node: just after it, as the new last.
                CommandNode *after = last->next;
                if (after != node) MoveBefore(node, next, after);
                bucket[1] = node;
            } else {
                // Look for an equal key from the node before the last back to the first.
                CommandNode *cursor = last;
                CommandNode *equal = nullptr;
                if (first != last) {
                    do {
                        cursor = cursor->prev;
                        if (node->keyLength == cursor->keyLength && SameBytes(node->key, cursor->key, node->keyLength)) {
                            equal = cursor;
                            break;
                        }
                    } while (cursor != first);
                }
                if (equal) {
                    MoveBefore(node, next, equal->next);
                } else {
                    MoveBefore(node, next, cursor);
                    bucket[0] = node;
                }
            }
        }
        node = next;
    } while (node != sentinel);
}

void __cdecl CommandTable::ClearNodes(void * /*unused*/, CommandNode *headNode)
{
    headNode->prev->next = nullptr;
    for (CommandNode *node = headNode->next; node;) {
        CommandNode *next = node->next;
        if (node->value) VirtualSlot<NodeVoidFn>(node->value, 2)(node->value); // Release
        ::operator delete(node, sizeof(CommandNode));
        node = next;
    }
}

void CommandTable::Destruct()
{
    buckets.~vector();
    ClearNodes(&head, head);
    ::operator delete(head, sizeof(CommandNode));
}

NamePool *NamePool::Construct()
{
    head = nullptr;
    count = 0;
    head = static_cast<Chunk *>(::operator new(sizeof(Chunk)));
    head->next = head;
    head->prev = head;
    used = 0x1000;
    return this;
}

KeyView *NamePool::Store(KeyView *out, const KeyView *key)
{
    uint32_t length = key->length;
    if (used + length > 0x1000) {
        used = 0;
        auto *block = static_cast<uint8_t *>(::operator new[](0x1000));
        Chunk *sentinel = head;
        if (count == 0x15555555) throw std::range_error("list<T> too long");
        auto *chunk = static_cast<Chunk *>(::operator new(sizeof(Chunk)));
        chunk->block = block;
        ++count;
        Chunk *last = sentinel->prev;
        chunk->next = sentinel;
        chunk->prev = last;
        sentinel->prev = chunk;
        last->next = chunk;
    }
    uint8_t *copy = head->prev->block + used;
    used += length;
    std::memcpy(copy, key->ptr, length);
    out->ptr = reinterpret_cast<const char *>(copy);
    out->length = length;
    return out;
}

void NamePool::Destruct()
{
    while (count != 0) {
        Chunk *last = head->prev;
        ::operator delete(last->block, 1);
        Chunk *after = last->next;
        --count;
        last->prev->next = after;
        after->prev = last->prev;
        ::operator delete(last, sizeof(Chunk));
    }
    used = 0x1000;
    Chunk *sentinel = head;
    sentinel->prev->next = nullptr;
    for (Chunk *chunk = sentinel->next; chunk;) {
        Chunk *next = chunk->next;
        ::operator delete(chunk, sizeof(Chunk));
        chunk = next;
    }
    ::operator delete(head, sizeof(Chunk));
}

void __cdecl FoldName(KeyView *key)
{
    char *p = const_cast<char *>(key->ptr);
    for (uint32_t i = 0; i < key->length; ++i) p[i] = static_cast<char>(p[i] & 0xdf);
}

cBlockCommandBase *cBlockCommandBase::Construct()
{
    mVTable = &ts2_rva_e7d758;
    mParser = nullptr;
    mRefCount = 0;
    mValue = 0;
    // The table: an empty node list (the sentinel points at itself), 8 buckets of two list pointers
    // that all point at the sentinel, a load limit of 1.0.
    mCommands.maxLoad = 0;
    mCommands.head = nullptr;
    mCommands.size = 0;
    mCommands.head = static_cast<CommandNode *>(::operator new(sizeof(CommandNode)));
    mCommands.head->next = mCommands.head;
    mCommands.head->prev = mCommands.head;
    new (&mCommands.buckets) std::vector<CommandNode *>();
    mCommands.mask = 7;
    mCommands.bucketCount = 8;
    mCommands.maxLoad = 1.0f;
    FillBuckets(mCommands.buckets, 0x10, mCommands.head);
    mNames.Construct();
    return this;
}

void cBlockCommandBase::AddCommand(void *command)
{
    const char *name = VirtualCall<const char *(__thiscall *)(void *), 5>(command);
    KeyView key{name, static_cast<uint32_t>(std::strlen(name))};
    if (mFlag == 0) {
        KeyView copy;
        mNames.Store(&copy, &key);
        key = copy;
        FoldName(&key);
    }
    InsertResult inserted;
    mCommands.Insert(&inserted, &key);
    CommandNode *node = inserted.node;
    void *previous = node->value;
    if (command != previous) {
        VirtualCall<NodeVoidFn, 1>(command); // AddRef
        node->value = command;
        if (previous) VirtualCall<NodeVoidFn, 2>(previous); // Release
    }
    VirtualCall<void(__thiscall *)(void *, void *, uint32_t), 3>(command, mParser, mValue); // SetContext
}

namespace {

// The lookup the block and the parser share: the key (folded to upper case first when `fold`) is
// searched in the table; a hit gives its command, which is AddRef'd and released again (a smart
// pointer's pair of calls), a miss null. A folding lookup of 0x80 bytes or more finds nothing.
void *LookupCommand(CommandTable &table, const char *name, uint32_t length, bool fold)
{
    KeyView key{name, length};
    char folded[0x80];
    if (fold) {
        if (length >= 0x80) return nullptr;
        std::memcpy(folded, name, length);
        key.ptr = folded;
        FoldName(&key);
    }
    FindResult where;
    table.Find(&where, &key, HashName(key.ptr, key.length));
    CommandNode *node = where.found ? where.found : table.head;
    if (node == table.head) return nullptr;
    void *command = node->value;
    if (command) VirtualCall<NodeVoidFn, 1>(command); // AddRef
    if (command) VirtualCall<NodeVoidFn, 2>(command); // Release
    return command;
}

} // namespace

void *cBlockCommandBase::FindCommand(const char *name, uint32_t length)
{
    return LookupCommand(mCommands, name, length, mFlag == 0);
}

const char *__cdecl NextToken(uint32_t *length, const char *text)
{
    const char *separators = ts2_rva_107479c.c_str();
    size_t leading = std::strspn(text, separators);
    const char *token = text + leading;
    size_t tokenLength = std::strcspn(token, separators);
    *length = static_cast<uint32_t>(tokenLength);
    return tokenLength == 0 ? nullptr : token;
}

void *cParserState::FindCommand(const char *name, uint32_t length)
{
    char *state = reinterpret_cast<char *>(this);
    int32_t top = (static_cast<int32_t>(*reinterpret_cast<uint32_t *>(state + 0x108) - *reinterpret_cast<uint32_t *>(state + 0x104)) >> 2) - 1;
    while (top >= 0) {
        void *block = (*reinterpret_cast<void ***>(state + 0x104))[top];
        void *found = VirtualCall<void *(__thiscall *)(void *, const char *, uint32_t), 11>(block, name, length);
        if (found) return found;
        --top;
    }
    return LookupCommand(*reinterpret_cast<CommandTable *>(state + 0x30), name, length, state[0x2c] == 0);
}

bool cIfCommand::HandleLine(void *line, uint32_t /*unused*/)
{
    void *state = mState;
    if (Conditions(state).back().active != 0) return true;
    const char *text = VirtualCall<const char *(__thiscall *)(void *), 5>(line);
    uint32_t length;
    const char *token = NextToken(&length, text);
    if (!token) return false;
    // The words that belong to the conditional commands themselves.
    struct Keyword {
        const uint32_t *length; // the game's global holding the word's length
        const char *text;
    };
    static const Keyword kKeywords[] = {{&ts2_rva_10748f4, ts2_rva_e7e500}, {&ts2_rva_1074994, ts2_rva_e7e52c},
                                        {&ts2_rva_1074998, ts2_rva_e7ae7c}, {&ts2_rva_107499c, ts2_rva_e7e534}};
    for (const Keyword &keyword : kKeywords) {
        uint32_t keywordLength = *keyword.length;
        if (length == keywordLength && std::strncmp(token, keyword.text, keywordLength) == 0) return true;
    }
    void *command = static_cast<cParserState *>(state)->FindCommand(token, length);
    if (command) {
        void *meta = nullptr;
        bool isMeta = VirtualCall<uint8_t(__thiscall *)(void *, uint32_t, void **), 0>(command, 0xcae872f1, &meta) != 0;
        if (isMeta) VirtualCall<void(__thiscall *)(void *, const char *), 9>(meta, token + length);
        if (meta) VirtualCall<NodeVoidFn, 2>(meta); // Release
    }
    return false;
}

void cBlockCommandBase::Destruct()
{
    mVTable = &ts2_rva_e7d758;
    mNames.Destruct();
    mCommands.Destruct();
}

cBlockCommandBase *cBlockCommandBase::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cBlockCommandBase));
    return this;
}

cBlockCommandT *cBlockCommandT::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cBlockCommandT));
    return this;
}

cMetaCommand *cMetaCommand::Construct()
{
    mVTable = &ts2_rva_e7d790;
    mRefCount = 0;
    return this;
}

// ---- the throw helper -----------------------------------------------------------------------------

void __cdecl ThrowUnless(bool ok, const char *fmt, ...)
{
    if (ok) return;
    constexpr int kBufferSize = sizeof(ts2_rva_10f6bc8);
    char *message = ts2_rva_10f6bc8; // a 2 KB buffer in the game's data
    va_list ap;
    va_start(ap, fmt);
    // ORIGINAL-BUG: a message of 2048 characters or more is not terminated, and the error built from
    // it then takes its length with strlen, past the buffer.
    _vsnprintf(message, kBufferSize, fmt, ap);
    va_end(ap);
    // ORIGINAL-BUG: the message is used as the format string of the error, and the arguments it can
    // read are the words left on the original's stack (see the header).
    ThrowFormatted(message, reinterpret_cast<uint32_t>(message), static_cast<uint32_t>(kBufferSize), reinterpret_cast<uint32_t>(fmt),
                   reinterpret_cast<uint32_t>(ap), reinterpret_cast<uint32_t>(&ts2_rva_da5060), 0u, 0u, 0u, 0u, 0u, 0xfu, 0u);
}

void __cdecl ThrowIfNull(const void *p, const char *fmt, ...)
{
    if (p) return;
    constexpr int kBufferSize = sizeof(ts2_rva_10fa248);
    char *message = ts2_rva_10fa248; // ThrowUnless's twin has its own 2 KB buffer
    va_list ap;
    va_start(ap, fmt);
    // ORIGINAL-BUG: as ThrowUnless (an unterminated message at 2048 characters, and the message used as the error's
    // format string, its arguments the words the original's frame holds: the same frame as ThrowUnless's).
    _vsnprintf(message, kBufferSize, fmt, ap);
    va_end(ap);
    ThrowFormatted(message, reinterpret_cast<uint32_t>(message), static_cast<uint32_t>(kBufferSize), reinterpret_cast<uint32_t>(fmt),
                   reinterpret_cast<uint32_t>(ap), reinterpret_cast<uint32_t>(&ts2_rva_da5060), 0u, 0u, 0u, 0u, 0u, 0xfu, 0u);
}

// ---- cCommand -------------------------------------------------------------------------------------

cCommand *cCommand::Construct()
{
    mVTable = &ts2_rva_e7d72c;
    mParser = nullptr;
    mRefCount = 0;
    return this;
}

void cCommand::Destruct()
{
    mVTable = &ts2_rva_e7d72c;
}

cCommand *cCommand::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cCommand));
    return this;
}

bool cCommand::QueryInterface(uint32_t iid, void **out)
{
    if (out == nullptr) return false;
    if (iid != 1 && iid != 0xaada39c7) return false;
    *out = this;
    VirtualCall<void(__thiscall *)(void *), 1>(this); // AddRef
    return true;
}

uint32_t cCommand::AddRef()
{
    return static_cast<uint32_t>(++mRefCount);
}

uint32_t cCommand::Release()
{
    uint32_t count = static_cast<uint32_t>(mRefCount) - 1;
    mRefCount = static_cast<int32_t>(count);
    if (count != 0) return count;
    VirtualCall<void(__thiscall *)(void *, uint32_t), 9>(this, 1);
    return 0;
}

void cCommand::SetContext(void *parser, void * /*state*/)
{
    mParser = parser;
}

uint32_t cCommand::GetHelpDefault(uint32_t /*index*/)
{
    return 0;
}

bool cCommand::ReturnsFalse()
{
    return false;
}

uint32_t cCommand::ReturnsZero()
{
    return 0;
}

// ---- cCommandT ------------------------------------------------------------------------------------

void cCommandT::SetContextAdjusted(void *parser, void *stateInterface)
{
    SetContext(parser, stateInterface);
    mState = stateInterface ? static_cast<char *>(stateInterface) - 4 : nullptr;
}

void cCommandT::SetContextExact(void *parser, void *state)
{
    SetContext(parser, state);
    mState = state;
}

cCommandT *cCommandT::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cCommandT));
    return this;
}

// ---- cMetaCommand ---------------------------------------------------------------------------------

void cMetaCommand::Destruct()
{
    mVTable = &ts2_rva_e7d790;
}

cMetaCommand *cMetaCommand::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cMetaCommand));
    return this;
}

bool cMetaCommand::QueryInterface(uint32_t iid, void **out)
{
    if (out == nullptr) return false;
    if (iid != 1 && iid != 0xaada39c7 && iid != 0xcae872f1) return false;
    *out = this;
    VirtualCall<void(__thiscall *)(void *), 1>(this); // AddRef
    return true;
}

uint32_t cMetaCommand::AddRef()
{
    return static_cast<uint32_t>(++mRefCount);
}

uint32_t cMetaCommand::Release()
{
    uint32_t count = static_cast<uint32_t>(mRefCount) - 1;
    mRefCount = static_cast<int32_t>(count);
    if (count != 0) return count;
    VirtualCall<void(__thiscall *)(void *, uint32_t), 11>(this, 1);
    return 0;
}

void cMetaCommand::SetContext(void * /*parser*/, void * /*state*/)
{
}

void cMetaCommandT::SetContextAdjusted(void *parser, void *stateInterface)
{
    SetContext(parser, stateInterface);
    mState = stateInterface ? static_cast<char *>(stateInterface) - 4 : nullptr;
}

cMetaCommandT *cMetaCommandT::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cMetaCommandT));
    return this;
}

// ---- cBlockCommand --------------------------------------------------------------------------------

bool cBlockCommandBase::QueryInterface(uint32_t iid, void **out)
{
    if (out == nullptr) return false;
    if (iid != 1 && iid != 0xaada39c7 && iid != 0xcada39cd) return false;
    *out = this;
    VirtualCall<void(__thiscall *)(void *), 1>(this); // AddRef
    return true;
}

uint32_t cBlockCommandBase::AddRef()
{
    return static_cast<uint32_t>(++mRefCount);
}

uint32_t cBlockCommandBase::Release()
{
    uint32_t count = static_cast<uint32_t>(mRefCount) - 1;
    mRefCount = static_cast<int32_t>(count);
    if (count != 0) return count;
    VirtualCall<void(__thiscall *)(void *, uint32_t), 12>(this, 1);
    return 0;
}

void cBlockCommandT::SetContextAdjusted(void *object, void *stateInterface)
{
    // The base slot 3 (verified in rz/TopCallersA): stores the value and the object, then queries the object.
    reinterpret_cast<nGZCommandParser::cBlockCommand *>(this)->StoreAndQuery(object, reinterpret_cast<uint32_t>(stateInterface));
    mState = stateInterface ? static_cast<char *>(stateInterface) - 4 : nullptr;
}

// ---- name and help strings ------------------------------------------------------------------------

#define NGZ_STRINGS(cls, name, help)                                                                    \
    const char *cls::GetName() const { return ts2_rva_##name; }                                        \
    const char *cls::GetHelp(uint32_t) const { return ts2_rva_##help; }

NGZ_STRINGS(cArrayCreateCommand, e7e9a8, e7e9b4);
NGZ_STRINGS(cAssertCommand, e7dfa0, e7dfa8);
NGZ_STRINGS(cCreateCommand, e7e878, e7e880);
NGZ_STRINGS(cDebugParserCommand, dcde40, e7eb48);
NGZ_STRINGS(cDebuggerBreakCommand, e7ec40, e7ec50);
NGZ_STRINGS(cDefineCommand, e48120, e7e6f8);
NGZ_STRINGS(cDumpParserStateCommand, e7eab8, e7eac0);
NGZ_STRINGS(cElseCommand, e7ae7c, e7e5fc);
NGZ_STRINGS(cElseIfCommand, e7e52c, e7e568);
NGZ_STRINGS(cEndCommand, dbc374, e7de10);
NGZ_STRINGS(cEndDefCommand, e7e73c, e7e770);
NGZ_STRINGS(cEndIfCommand, e7e534, e7e680);
NGZ_STRINGS(cEvalCommand, e7df44, e7df4c);
NGZ_STRINGS(cIfCommand, e7e500, e7e504);
NGZ_STRINGS(cIgnoreErrorsCommand, e7de60, e7de70);
NGZ_STRINGS(cIncludeCommand, e7dc94, e7dc9c);
NGZ_STRINGS(cListCommandsCommand, e7dec8, e7ded0);
NGZ_STRINGS(cLogCommand, e09304, e7ea74);
NGZ_STRINGS(cNamespaceCommand, e7e414, e7e420);
NGZ_STRINGS(cPurgeCommand, e7e46c, e7e474);
NGZ_STRINGS(cSetBCommand, e7e108, e7e110);
NGZ_STRINGS(cSetCCommand, e7e258, e7e260);
NGZ_STRINGS(cSetCommand, e7e02c, e7e030);
NGZ_STRINGS(cSetECommand, e7e07c, e7e088);
NGZ_STRINGS(cSetFCommand, e7e1d8, e7e1e0);
NGZ_STRINGS(cSetICommand, e7e164, e7e16c);
NGZ_STRINGS(cSetV2Command, e7e2bc, e7e2c4);
NGZ_STRINGS(cSetV3Command, e7e32c, e7e334);
NGZ_STRINGS(cSetV4Command, e7e3a0, e7e3a8);
NGZ_STRINGS(cShowArgumentsCommand, e7ebd0, e7ebe0);
NGZ_STRINGS(cSincludeCommand, e7dce4, e7dcf0);
NGZ_STRINGS(cTraceCommand, dd0b2c, e7ea2c);
NGZ_STRINGS(cUndefineCommand, e7e7e4, e7e7f0);
NGZ_STRINGS(cVersionCommand, dbc1a4, e7ec98);
NGZ_STRINGS(cWildIncludeCommand, e7dd60, e7dd70);

#undef NGZ_STRINGS

const char *cAliasCommand::GetName() const
{
    return ts2_rva_daa7c8;
}

const char *cAliasCommand::GetHelp(uint32_t index) const
{
    if (index == 0) return ts2_rva_e7eda0;
    if (index == 1) return ts2_rva_e7ee18;
    return nullptr;
}

// ---- cAliasStubCommand ----------------------------------------------------------------------------

const char *cAliasStubCommand::GetName() const
{
    return mName.c_str();
}

const char *cAliasStubCommand::GetHelp(uint32_t index) const
{
    if (index == 0) return mShortHelp.c_str();
    if (index == 1) return mHelp.c_str();
    return nullptr;
}

void cAliasStubCommand::Destruct()
{
    // The four strings are destroyed from the last to the first, then the base.
    mHelp.~basic_string();
    mShortHelp.~basic_string();
    mTarget.~basic_string();
    mName.~basic_string();
    cCommand::Destruct();
}

cAliasStubCommand *cAliasStubCommand::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cAliasStubCommand));
    return this;
}

void cAliasStubCommand::Execute(cIArguments *args)
{
    // The command line is the stored one with every argument after the first appended, space separated.
    std::string line(mTarget);
    int32_t count = VirtualCall<ArgsCountFn, 7>(args);
    if (count > 1) {
        int32_t i = 1;
        do {
            line.append(ts2_rva_da4ca4, 1);
            const char *text = VirtualCall<ArgsTextFn, 4>(args, static_cast<uint32_t>(i));
            line.append(text, std::strlen(text));
            ++i;
            count = VirtualCall<ArgsCountFn, 7>(args);
        } while (i < count);
    }
    VirtualCall<ParserTextFn, 15>(mParser, line.c_str());
}

// ---- Execute --------------------------------------------------------------------------------------

void cEvalCommand::Execute(cIArguments *args)
{
    ArgsPrepare(args);
    VTable parserVTable = VTableOf(mParser);
    const char *const *a = ArgsGet(args, 1);
    reinterpret_cast<ParserTextFn>(parserVTable[15])(mParser, a[0]);
}

void cIgnoreErrorsCommand::Execute(cIArguments *args)
{
    ArgsPrepare(args);
    VTable parserVTable = VTableOf(mParser);
    const char *const *a = ArgsGet(args, 1);
    uint8_t flag = reinterpret_cast<ParserBoolFn>(parserVTable[30])(mParser, a[0]);
    // The state is read again after the call; its byte at +0xe8 takes the parser's answer.
    *(static_cast<uint8_t *>(mState) + 0xe8) = flag;
}

void cIncludeCommand::Execute(cIArguments *args)
{
    ArgsPrepare(args);
    VTable parserVTable = VTableOf(mParser);
    const char *const *a = ArgsGet(args, 1);
    reinterpret_cast<ParserTextIntFn>(parserVTable[12])(mParser, a[0], 1);
}

void cSincludeCommand::Execute(cIArguments *args)
{
    ArgsPrepare(args);
    VTable parserVTable = VTableOf(mParser);
    const char *const *a = ArgsGet(args, 1);
    reinterpret_cast<ParserTextIntFn>(parserVTable[12])(mParser, a[0], 3);
}

void cTraceCommand::Execute(cIArguments *args)
{
    ArgsPrepare(args);
    void *output = StateOutput(mState);
    const char *const *a = ArgsGet(args, 1);
    VirtualCall<OutputWriteFn, 14>(output, a[0]);
    VirtualCall<OutputWriteFn, 14>(output, kNewlineString);
}

void cSetCommand::Execute(cIArguments *args)
{
    ArgsPrepare(args);
    const char *const *a = ArgsGet(args, 2);
    void *state = mState;
    VirtualCall<StateSetFn, 1>(state, a[0], a[1]);
}

void cSetBCommand::Execute(cIArguments *args)
{
    ArgsPrepare(args);
    const char *const *a = ArgsGet(args, 2);
    uint8_t truth = VirtualCall<ParserBoolFn, 30>(mParser, a[1]);
    // The variable gets one of two fixed strings: the first when the parser says the text is true.
    const char *text = truth ? kTrueString : kFalseString;
    void *state = mState;
    VirtualCall<StateSetFn, 1>(state, a[0], text);
}

void cDebugParserCommand::Execute(cIArguments *args)
{
    // Each option of the command is looked up by name; when the argument list has it, the parser
    // is asked whether its value is true, and the answer goes to a flag byte of the parser state.
    // The parser and the state are read again at every step.
    struct Option {
        const char *name; // the option name
        uint32_t offset; // flag byte in the state
    };
    static const Option kOptions[] = {
        {ts2_rva_e7eb68, 0xea}, {ts2_rva_e7eb78, 0xeb}, {ts2_rva_e7eb14, 0xe9}, {ts2_rva_e7eb84, 0xec}, {ts2_rva_e7eb08, 0xed}, {ts2_rva_e7eb94, 0xee},
    };
    for (const Option &option : kOptions) {
        const char *const *value = VirtualCall<ArgsFindFn, 3>(args, option.name, 1);
        if (value) {
            uint8_t flag = VirtualCall<ParserBoolFn, 30>(mParser, value[0]);
            static_cast<uint8_t *>(mState)[option.offset] = flag;
        }
    }
    ArgsPrepare(args);
}

void cDebuggerBreakCommand::Execute(cIArguments *args)
{
    // Only checks its argument count: `this` is not used.
    ThrowUnless(VirtualCall<ArgsCountFn, 7>(args) == 1, kWrongNumberOfArguments);
}

void cEndCommand::Execute(cIArguments *args)
{
    ThrowUnless(VirtualCall<ArgsCountFn, 7>(args) == 1, kWrongNumberOfArguments);
    // The state keeps the open blocks as a vector of command pointers (begin at +0x104, end at
    // +0x108): the end command closes the last one.
    uint32_t *state = static_cast<uint32_t *>(mState);
    ThrowUnless(state[0x104 / 4] != state[0x108 / 4], ts2_rva_e7de20);
    state = static_cast<uint32_t *>(mState);
    uint32_t *last = reinterpret_cast<uint32_t *>(state[0x108 / 4] - 4);
    void *block = reinterpret_cast<void *>(*last);
    state[0x108 / 4] = reinterpret_cast<uint32_t>(last);
    VirtualCall<CommandVoidFn, 9>(block);
}

void cListCommandsCommand::Execute(cIArguments *args)
{
    ThrowUnless(VirtualCall<ArgsCountFn, 7>(args) == 1, kWrongNumberOfArguments);
    // The state has a list of commands (its sentinel node at +0x34, each node holding a command at
    // +0x10) and a vector of commands (begin at +0x16c, end at +0x170). Each command is asked for
    // its name (slot 5) and its help (slot 6, index 0); the answers are not used.
    char *state = static_cast<char *>(mState);
    char *sentinel = *reinterpret_cast<char **>(state + 0x34);
    char *node = *reinterpret_cast<char **>(sentinel);
    if (node != sentinel) {
        do {
            void *command = *reinterpret_cast<void **>(node + 0x10);
            VirtualCall<CommandVoidFn, 5>(command);
            command = *reinterpret_cast<void **>(node + 0x10);
            VirtualCall<CommandIntFn, 6>(command, 0);
            state = static_cast<char *>(mState);
            node = *reinterpret_cast<char **>(node);
        } while (node != *reinterpret_cast<char **>(state + 0x34));
    }
    void **item = *reinterpret_cast<void ***>(state + 0x16c);
    if (item != *reinterpret_cast<void ***>(state + 0x170)) {
        do {
            VirtualCall<CommandVoidFn, 5>(*item);
            VirtualCall<CommandIntFn, 6>(*item, 0);
            ++item;
        } while (item != *reinterpret_cast<void ***>(static_cast<char *>(mState) + 0x170));
    }
}

void cPurgeCommand::Execute(cIArguments *args)
{
    ArgsPrepare(args);
    const char *const *a = ArgsGet(args, 1);
    const char *name = a[0];
    // The scope name must end with a colon (and have something before it).
    size_t length = std::strlen(name);
    ThrowUnless(length > 1 && name[length - 1] == ':', ts2_rva_e7e4b0, name);
    void *state = mState;
    VTable scopeVTable = StateVirtualBaseVTable(state);
    a = ArgsGet(args, 1);
    void *scope = StateVirtualBase(state);
    reinterpret_cast<ScopeFn>(scopeVTable[22])(scope, a[0]);
}

void cNamespaceCommand::Execute(cIArguments *args)
{
    ArgsPrepare(args);
    void *state = mState;
    void *scope = StateVirtualBase(state);
    reinterpret_cast<ScopeObjectFn>(VTableOf(scope)[16])(scope, this);
    state = mState;
    VTable scopeVTable = StateVirtualBaseVTable(state);
    const char *const *a = ArgsGet(args, 1);
    scope = StateVirtualBase(state);
    reinterpret_cast<ScopeFn>(scopeVTable[20])(scope, a[0]);
}

void cVersionCommand::Execute(cIArguments *args)
{
    const char *const *a = ArgsGet(args, 1);
    int32_t version = VirtualCall<ParserIntFn, 32>(mParser, a[0]);
    char *state = static_cast<char *>(mState);
    *reinterpret_cast<int32_t *>(state + 0xf0) = version;
    state = static_cast<char *>(mState);
    // The state can limit the versions it accepts: a flag at +0x5c, then the lowest (+0x60) and the
    // highest (+0x64).
    if (state[0x5c] != 0) {
        int32_t lowest = *reinterpret_cast<int32_t *>(state + 0x60);
        if (version < lowest) ThrowFormatted(ts2_rva_e7ecbc, version, lowest);
        int32_t highest = *reinterpret_cast<int32_t *>(state + 0x64);
        if (version > highest) ThrowFormatted(ts2_rva_e7ed00, version, highest);
    }
    ArgsPrepare(args);
}

void cShowArgumentsCommand::Execute(cIArguments *args)
{
    void *output = StateOutput(mState);
    VirtualCall<OutputWriteFn, 14>(output, ts2_rva_e7ec08);
    int32_t count = VirtualCall<ArgsCountFn, 7>(args);
    for (int32_t i = 0; i < count;) {
        output = StateOutput(mState);
        VirtualCall<CommandIntFn, 9>(output, static_cast<uint32_t>(i));
        VirtualCall<OutputWriteFn, 14>(output, ts2_rva_da2cd8);
        const char *text = VirtualCall<ArgsTextFn, 4>(args, static_cast<uint32_t>(i));
        VirtualCall<OutputWriteFn, 14>(output, text);
        VirtualCall<OutputWriteFn, 14>(output, kNewlineString);
        ++i;
        count = VirtualCall<ArgsCountFn, 7>(args);
    }
}

void cSetICommand::Execute(cIArguments *args)
{
    ArgsPrepare(args);
    const char *const *a = ArgsGet(args, 2);
    // The number is written over a buffer that starts as the longest 64-bit decimal text.
    char buffer[22] = "-18446744073709551616";
    int32_t value = VirtualCall<ParserIntFn, 32>(mParser, a[1]);
    _itoa(value, buffer, 10);
    void *state = mState;
    VirtualCall<StateSetFn, 1>(state, a[0], buffer);
}

namespace {

using ParserFlagFn = uint8_t(__thiscall *)(void *); // slot 43, 0xac (al)

} // namespace

void cSetECommand::Execute(cIArguments *args)
{
    // sete name value "choice choice ...": sets the variable only when the value is one of the choices.
    ArgsPrepare(args);
    const char *const *a = ArgsGet(args, 3);
    // The choices as a local cArguments (its members called directly, as the original does).
    cArguments choices;
    uint8_t flag = VirtualCall<ParserFlagFn, 43>(mParser);
    choices.cArguments::Parse(a[2], 0, flag);
    int32_t count;
    const char *const *list = choices.cArguments::Positional(reinterpret_cast<uint32_t *>(&count), 1, 0x7fffffff);
    int32_t i = 0;
    for (; i < count; ++i)
        if (std::strcmp(list[i], a[1]) == 0) break;
    ThrowUnless(i != count, ts2_rva_e7e0cc);
    VirtualCall<StateSetFn, 1>(mState, a[0], a[1]);
}

namespace {

// The state's definitions (macros made by `define`): a vector of pointers at +0x6c (begin) / +0x70 (end), sorted by
// name (a std::string at +8 of each definition) in the name-table order (length first, then the bytes from the last).
// The definitions are reference counted through their own vtables; the vector's element copy (0xafcc10) takes a
// reference to the new value (slot 2) and drops the old one (slot 1), and so does the erase below for the last one.
struct DefinitionPointers {
    void **first;
    void **last;
};

// 0xafd520 (ours, gz/MacroDefinition.cpp): std::equal_range of the name [begin, end) over the definitions; `keyWord`
// is passed along and not used (as FindEnumRange's).
DefinitionPointers FindDefinitions(void **first, void **last, const void *keyWord, const char *end, const char *begin)
{
    DefinitionRange found;
    EqualRangeDefinitions(&found, reinterpret_cast<DefinitionRef *>(first), reinterpret_cast<DefinitionRef *>(last), keyWord,
                          DefinitionKey{end, begin});
    return {reinterpret_cast<void **>(found.first), reinterpret_cast<void **>(found.last)};
}

using ArgsHasFn = uint8_t(__thiscall *)(void *, const char *); // slot 1, +4: is the option given? (al)
using DefinitionDropFn = void(__thiscall *)(void *);           // slot 1, +4 of a definition

} // namespace

void cUndefineCommand::Execute(cIArguments *args)
{
    // undefine [-quiet] name: removes a definition. Not while a definition is being recorded; an unknown name throws
    // unless the option is given.
    uint8_t quiet = VirtualCall<ArgsHasFn, 1>(args, ts2_rva_e7e818);
    ArgsPrepare(args);
    uint8_t *state = static_cast<uint8_t *>(mState);
    ThrowUnless(*reinterpret_cast<uint32_t *>(state + 0x11c) == *reinterpret_cast<uint32_t *>(state + 0x120),
                ts2_rva_e7e820);
    const char *const *a = ArgsGet(args, 1);
    const char *name = a[0];
    void **&defsBegin = *reinterpret_cast<void ***>(state + 0x6c);
    void **&defsEnd = *reinterpret_cast<void ***>(state + 0x70);
    DefinitionPointers found = FindDefinitions(defsBegin, defsEnd, a, name + std::strlen(name), name);
    if (found.first == found.last) {
        if (quiet) return;
        ThrowFormatted(ts2_rva_e7e834, name);
    }
    if (state[0xed]) {
        // Echo: the command's name, a space, the definition's name, a newline.
        void *output = StateOutput(state);
        VirtualCall<OutputWriteFn, 14>(output, VirtualCall<ArgsTextFn, 4>(args, 0u));
        VirtualCall<OutputWriteFn, 14>(output, ts2_rva_da4ca4);
        VirtualCall<OutputWriteFn, 14>(output, a[0]);
        VirtualCall<OutputWriteFn, 14>(output, kNewlineString);
    }
    uint8_t *stateNow = static_cast<uint8_t *>(mState);
    void **&end = *reinterpret_cast<void ***>(stateNow + 0x70);
    MoveCountedPointers(found.first + 1, end, found.first);
    if (void *last = end[-1]) VirtualCall<DefinitionDropFn, 1>(last);
    --end;
}

void cElseCommand::Execute(cIArguments *args)
{
    ThrowUnless(VirtualCall<ArgsCountFn, 7>(args) == 1, kWrongNumberOfArguments);
    ThrowUnless(Conditions(mState).size() != 1, ts2_rva_e7e620);
    ThrowUnless(Conditions(mState).back().elseSeen == 0, ts2_rva_e7e640);
    ConditionStack &stack = Conditions(mState);
    // When the enclosing text is being run, the else flips the current branch.
    if (stack.end()[-2].active != 0) {
        ConditionEntry &last = Conditions(mState).back();
        last.active = last.active == 0;
    }
    Conditions(mState).back().elseSeen = 1;
    if (TraceOn(mState)) {
        TraceIndent(&mState, 2);
        void *output = StateOutput(mState);
        const ConditionEntry &last = Conditions(mState).back();
        Write(output, ts2_rva_e7e5c8);
        Write(output, last.active == 0 ? kFalseWord : kTrueWord);
        Write(output, ts2_rva_e7e520);
    }
}

void cElseIfCommand::Execute(cIArguments *args)
{
    ArgsPrepare(args);
    ThrowUnless(Conditions(mState).size() != 1, ts2_rva_e7e58c);
    ThrowUnless(Conditions(mState).back().elseSeen == 0, ts2_rva_e7e5b0);
    if (Conditions(mState).end()[-2].active != 0) {
        ConditionEntry &last = Conditions(mState).back();
        last.active = last.active == 0;
    }
    if (TraceOn(mState)) {
        TraceIndent(&mState, 2);
        void *output = StateOutput(mState);
        const ConditionEntry &last = Conditions(mState).back();
        Write(output, ts2_rva_e7e5c8);
        Write(output, last.active == 0 ? kFalseWord : kTrueWord);
        Write(output, ts2_rva_e7e520);
    }
    // The new entry: one level deeper than the last; when the last is active, the parser evaluates
    // the condition. Quirk (harmless): when the last is not active, the two padding bytes of
    // the new entry are whatever the stack held; here they are zero.
    ConditionEntry entry{};
    entry.level = Conditions(mState).back().level + 1;
    if (Conditions(mState).back().active != 0) {
        VTable parserVTable = VTableOf(mParser);
        const char *const *a = ArgsGet(args, 1);
        entry.active = reinterpret_cast<ParserBoolFn>(parserVTable[30])(mParser, a[0]);
    }
    Conditions(mState).push_back(entry);
    if (TraceOn(mState)) {
        TraceIndent(&mState, 2);
        void *output = StateOutput(mState);
        const ConditionEntry &last = Conditions(mState).back();
        Write(output, ts2_rva_e7e524);
        Write(output, last.active == 0 ? kFalseWord : kTrueWord);
        Write(output, ts2_rva_e7e520);
    }
}

void cIfCommand::Execute(cIArguments *args)
{
    ArgsPrepare(args);
    void *scope = StateVirtualBase(mState);
    reinterpret_cast<ScopeObjectFn>(VTableOf(scope)[17])(scope, this);
    ConditionEntry entry{};
    entry.level = 1;
    if (Conditions(mState).back().active != 0) {
        // The condition is evaluated by the state's virtual base (slot 30), not by the parser.
        VTable scopeVTable = StateVirtualBaseVTable(mState);
        const char *const *a = ArgsGet(args, 1);
        scope = StateVirtualBase(mState);
        entry.active = reinterpret_cast<ParserBoolFn>(scopeVTable[30])(scope, a[0]);
    }
    Conditions(mState).push_back(entry);
    if (TraceOn(mState)) {
        TraceIndent(&mState, 2);
        void *output = StateOutput(mState);
        const ConditionEntry &last = Conditions(mState).back();
        Write(output, ts2_rva_e7e524);
        Write(output, last.active == 0 ? kFalseWord : kTrueWord);
        Write(output, ts2_rva_e7e520);
    }
}

void cEndIfCommand::Execute(cIArguments *args)
{
    ThrowUnless(VirtualCall<ArgsCountFn, 7>(args) == 1, kWrongNumberOfArguments);
    ThrowUnless(Conditions(mState).size() != 1, ts2_rva_e7e698);
    void *scope = StateVirtualBase(mState);
    VirtualCall<CommandVoidFn, 18>(scope);
    ConditionStack &stack = Conditions(mState);
    stack.resize(stack.size() - static_cast<uint32_t>(stack.back().level));
    if (TraceOn(mState)) {
        TraceIndent(&mState, 1);
        Write(StateOutput(mState), ts2_rva_e7e6bc);
    }
}

void cBlockCommandBase::EndBlock()
{
}

uint32_t cNamespaceCommand::EndBlock()
{
    // A tail call in the original: the result is the callee's.
    void *scope = StateVirtualBase(mState);
    return VirtualCall<uint32_t(__thiscall *)(void *), 21>(scope);
}

uint32_t cMetaCommandT::RegisterInScope(void * /*unused*/)
{
    void *scope = StateVirtualBase(mState);
    return reinterpret_cast<uint32_t(__thiscall *)(void *, void *)>(VTableOf(scope)[17])(scope, this);
}

int32_t cElseCommand::ReturnsMinusOne()
{
    return -1;
}

int32_t cEndIfCommand::NegatedBlockWord()
{
    return -*reinterpret_cast<int32_t *>(*reinterpret_cast<char **>(static_cast<char *>(mState) + 0x114) - 8);
}

void cWildIncludeCommand::Execute(cIArguments *args)
{
    ArgsPrepare(args);
    int32_t count = 0;
    const char *const *a = VirtualCall<ArgsRangeFn, 10>(args, &count, 1, 2);
    // One option is understood: the second argument "subfolders" (any case) makes the include recursive.
    uint32_t flags = 1;
    if (count == 2) {
        LocalString option("subfolders");
        if (option.get()->IsEqual(a[1], false)) flags = 5;
    }
    VirtualCall<ParserFlagsFn, 13>(mParser, a[0], flags);
}

void cArrayCreateCommand::Execute(cIArguments *args)
{
    ArgsPrepare(args);
    const char *const *a = ArgsGet(args, 2);
    uint32_t count = static_cast<uint32_t>(VirtualCall<ParserIntFn, 32>(mParser, a[1]));
    // Runs "create <name>(<index> <count>)" through the parser for each index.
    for (uint32_t i = 0; i < count; ++i) {
        LocalString command;
        command.get()->Sprintf(ts2_rva_e7e9f0, ts2_rva_e7e878, a[0], i, count);
        VirtualCall<ParserTextFn, 15>(mParser, command.get()->ToChar());
    }
}

void cAliasCommand::Execute(cIArguments *args)
{
    // The argument list is asked for 2 to 4 arguments (the count goes back through the pointer; the
    // original passes the address of the argument slot of its own frame, which holds `args`).
    int32_t count = reinterpret_cast<int32_t>(args);
    const char *const *a = VirtualCall<ArgsRangeFn, 10>(args, &count, 2, 4);
    LocalString shortHelp, longHelp;
    if (count > 2) shortHelp.get()->mString.assign(a[2], std::strlen(a[2]));
    else shortHelp.get()->Sprintf(ts2_rva_e7eeb8, a[1]);
    if (count > 3) longHelp.get()->mString.assign(a[3], std::strlen(a[3]));
    else longHelp.get()->Sprintf(ts2_rva_e7eec8, a[0], a[1]);

    // The stub is built in place like the original does: the base, the vtable, four strings.
    auto *stub = static_cast<cAliasStubCommand *>(::operator new(sizeof(cAliasStubCommand)));
    const char *shortText = shortHelp.get()->ToChar(), *longText = longHelp.get()->ToChar();
    reinterpret_cast<cCommand *>(stub)->Construct();
    stub->mVTable = &ts2_rva_e7ed4c;
    new (&stub->mName) std::string(a[0], std::strlen(a[0]));
    new (&stub->mTarget) std::string(a[1], std::strlen(a[1]));
    new (&stub->mShortHelp) std::string(shortText, std::strlen(shortText));
    new (&stub->mHelp) std::string(longText, std::strlen(longText));
    VirtualCall<ParserRegisterFn, 4>(mParser, stub);
}

namespace {

using ParserVectorFn = void(__thiscall *)(void *, float *, const char *, uint32_t); // slots 34 to 36
using ParserVector4Fn = void(__thiscall *)(void *, float *, const char *);         // slot 37
using ParserDoubleFn = double(__thiscall *)(void *, const char *);                 // slot 31

// The set-vector commands: the parser reads a vector of `count` floats from the argument text; the
// variable is set to its text "(x,y,...)" (formatted into a stack buffer of the given size).
template <size_t N, size_t BufferSize, int Slot> void SetVector(cCommandT *self, const char *const *a, const char *format)
{
    float v[N];
    VirtualSlot<ParserVectorFn>(self->mParser, Slot)(self->mParser, v, a[1], 1);
    char text[BufferSize];
    bool ok;
    if constexpr (N == 2) ok = PrintDoubles(text, BufferSize, format, double(v[0]), double(v[1]));
    else if constexpr (N == 3) ok = PrintDoubles(text, BufferSize, format, double(v[0]), double(v[1]), double(v[2]));
    else ok = PrintDoubles(text, BufferSize, format, double(v[0]), double(v[1]), double(v[2]), double(v[3]));
    if (!ok) ThrowFormatted(ts2_rva_e7e210); // "INTERNAL BUFFER OVERFLOW"
    void *state = self->mState;
    VirtualCall<StateSetFn, 1>(state, a[0], text);
}

} // namespace

void cSetV2Command::Execute(cIArguments *args)
{
    ArgsPrepare(args);
    const char *const *a = ArgsGet(args, 2);
    SetVector<2, 0x2c, 34>(this, a, ts2_rva_e7e2f8);
}

void cSetV3Command::Execute(cIArguments *args)
{
    ArgsPrepare(args);
    const char *const *a = ArgsGet(args, 2);
    SetVector<3, 0x41, 35>(this, a, ts2_rva_e7e368);
}

void cSetV4Command::Execute(cIArguments *args)
{
    ArgsPrepare(args);
    const char *const *a = ArgsGet(args, 2);
    SetVector<4, 0x55, 36>(this, a, ts2_rva_e7d48c);
}

void cSetCCommand::Execute(cIArguments *args)
{
    // Four floats like the V4 command, but the parser is asked through slot 37 with two arguments.
    ArgsPrepare(args);
    const char *const *a = ArgsGet(args, 2);
    float v[4];
    VirtualCall<ParserVector4Fn, 37>(mParser, v, a[1]);
    char text[0x55];
    if (!PrintDoubles(text, 0x55, ts2_rva_e7d48c, double(v[0]), double(v[1]), double(v[2]), double(v[3])))
        ThrowFormatted(ts2_rva_e7e210);
    void *state = mState;
    VirtualCall<StateSetFn, 1>(state, a[0], text);
}

void cSetFCommand::Execute(cIArguments *args)
{
    ArgsPrepare(args);
    const char *const *a = ArgsGet(args, 2);
    char text[0x20];
    double value = VirtualCall<ParserDoubleFn, 31>(mParser, a[1]);
    if (!FormatGeneral(value, text, 0x20, 6)) ThrowFormatted(ts2_rva_e7e210);
    void *state = mState;
    VirtualCall<StateSetFn, 1>(state, a[0], text);
}

void cLogCommand::Execute(cIArguments *args)
{
    // The arguments are a log level (one or two before the text): the level name is looked up in
    // the game's table of log levels, which throws for a name it does not know. The result is not used.
    int32_t count = reinterpret_cast<int32_t>(args);
    const char *const *a = VirtualCall<ArgsRangeFn, 10>(args, &count, 1, 3);
    if (count == 3) ts2_rva_10f9a30.Lookup(a[1]);
    else if (count == 2) ts2_rva_10f9a30.Lookup(a[0]);
    ArgsPrepare(args);
}

void cAssertCommand::Execute(cIArguments *args)
{
    ArgsPrepare(args);
    VTable parserVTable = VTableOf(mParser);
    const char *const *a = ArgsGet(args, 2);
    if (reinterpret_cast<ParserBoolFn>(parserVTable[30])(mParser, a[0]) != 0) return;
    a = ArgsGet(args, 2);
    ThrowFormatted(ts2_rva_e7dfe8, a[1]);
}

// ---- definitions: enddef, create, dumpparserstate (lane HV) --------------------------------------

namespace {

// A definition (made by `define`, recorded until `enddef`) as the commands see it: reference counted through its own
// vtable (slot 1 releases, slot 2 takes a reference), its name a std::string at +8, its parameter list an argument
// object at +0x20 (cArguments: slot 4 gives argument i, slot 7 their number), its body text at +0x50.
using DefinitionRefFn = void(__thiscall *)(void *); // slots 1 (release) and 2 (reference), +4 and +8
inline const std::string &DefinitionName(const void *definition)
{
    return *reinterpret_cast<const std::string *>(static_cast<const char *>(definition) + 8);
}
inline void *DefinitionParameters(void *definition) { return static_cast<char *>(definition) + 0x20; }
inline std::vector<char> &DefinitionBody(void *definition)
{
    return *reinterpret_cast<std::vector<char> *>(static_cast<char *>(definition) + 0x50);
}
inline void DefinitionAddRef(void *definition) { VirtualCall<DefinitionRefFn, 2>(definition); }
inline void DefinitionRelease(void *definition) { VirtualCall<DefinitionRefFn, 1>(definition); }

// The state's definitions, sorted by name (vector at +0x6c / +0x70), and the one being recorded (+0x15c, held).
inline void **&DefinitionsBegin(void *state) { return *reinterpret_cast<void ***>(static_cast<char *>(state) + 0x6c); }
inline void **&DefinitionsEnd(void *state) { return *reinterpret_cast<void ***>(static_cast<char *>(state) + 0x70); }
inline void *&RecordedDefinition(void *state) { return *reinterpret_cast<void **>(static_cast<char *>(state) + 0x15c); }
inline bool EchoOn(void *state) { return static_cast<uint8_t *>(state)[0xed] != 0; }

// The name-table order (length first, then the bytes from the last one, as signed chars): true when a sorts before b.
bool NameBefore(const std::string &a, const std::string &b)
{
    if (a.size() != b.size()) return a.size() < b.size();
    const char *aBegin = a.data(), *aEnd = aBegin + a.size();
    const char *bBegin = b.data(), *bEnd = bBegin + b.size();
    while (aEnd != aBegin) {
        if (bEnd == bBegin) return false;
        signed char x = *--aEnd, y = *--bEnd;
        if (x < y) return true;
        if (x > y) return false;
    }
    return bEnd != bBegin;
}

// A reference to a definition held by a local (the original's cRZAutoRefCount): taken when made, dropped when it goes
// out of scope (also when something throws).
class HeldDefinition {
public:
    explicit HeldDefinition(void *definition) : mDefinition(definition)
    {
        if (mDefinition) DefinitionAddRef(mDefinition);
    }
    ~HeldDefinition()
    {
        if (mDefinition) DefinitionRelease(mDefinition);
    }
    HeldDefinition(const HeldDefinition &) = delete;
    HeldDefinition &operator=(const HeldDefinition &) = delete;
    void *const *Address() const { return &mDefinition; }

private:
    void *mDefinition;
};

// 0xafc390 (__cdecl: a held reference by value, a format, varargs) as enddef uses it: the reference is the definition
// being recorded. When there is one, the by-value copy's reference (taken by the caller, slot 2) is dropped (slot 1);
// otherwise the message is formatted into the game's buffer 0x10f9a48 (2 KB) and a cError built from that text is
// thrown. The message has no conversions and no arguments come with it, so the text is the message itself (the original
// formats it once more as the error's format: the same text).
void ThrowUnlessRecording(void *definition, const char *message)
{
    if (definition) {
        DefinitionAddRef(definition);
        DefinitionRelease(definition);
        return;
    }
    char *buffer = ts2_rva_10f9a48;
    _snprintf(buffer, 0x800, "%s", message);
    ThrowFormatted(buffer);
}

// The state's virtual base, slot 18 (+0x48): called with no arguments when a definition ends.
using ScopeVoidFn = void(__thiscall *)(void *);

} // namespace

void cEndDefCommand::Execute(cIArguments *args)
{
    // enddef: the definition being recorded is added to the state's definitions, at its place in the name order (before
    // any of the same name), and recording stops.
    ThrowUnless(VirtualCall<ArgsCountFn, 7>(args) == 1, kWrongNumberOfArguments);
    ThrowUnlessRecording(RecordedDefinition(mState), ts2_rva_e7e790);
    void *scope = StateVirtualBase(mState);
    VirtualCall<ScopeVoidFn, 18>(scope);
    void *definition = RecordedDefinition(mState);
    // The body's spare capacity is given back (a copy of exactly its size swapped in).
    std::vector<char> &body = DefinitionBody(definition);
    if (body.capacity() > body.size()) std::vector<char>(body).swap(body);
    void *state = mState;
    void *key = RecordedDefinition(state);
    void **where = std::lower_bound(DefinitionsBegin(state), DefinitionsEnd(state), key,
                                    [](void *entry, void *k) { return NameBefore(DefinitionName(entry), DefinitionName(k)); });
    {
        HeldDefinition held(definition);
        // 0xafd3c0 (ours, gz/MacroDefinition.cpp): std::vector<DefinitionRef>::emplace(where, value); the element's copy
        // takes a reference (slot 2).
        DefinitionRef *inserted;
        reinterpret_cast<DefinitionVector *>(static_cast<char *>(mState) + 0x6c)
            ->Insert(&inserted, reinterpret_cast<DefinitionRef *>(where), reinterpret_cast<const DefinitionRef *>(held.Address()));
    }
    state = mState;
    if (void *recorded = RecordedDefinition(state)) {
        RecordedDefinition(state) = nullptr;
        DefinitionRelease(recorded);
    }
    state = mState;
    if (EchoOn(state)) Write(StateOutput(state), ts2_rva_e7e7b0);
}

namespace {

// A variable scope of the parser state (the root at state+0x78): two hash tables of the CommandTable kind, the
// variables (the node list's sentinel at +4) and the child scopes (sentinel at +0x24). A node: next, prev, the name's
// characters and length (+8, +0xc), then the value: a std::string for a variable, the child scope for a scope.
struct ScopeNode {
    ScopeNode *next;
    ScopeNode *prev;
    const char *name;
    uint32_t nameLength;
};
inline ScopeNode *VariablesHead(void *scope) { return *reinterpret_cast<ScopeNode **>(static_cast<char *>(scope) + 4); }
inline ScopeNode *ChildrenHead(void *scope) { return *reinterpret_cast<ScopeNode **>(static_cast<char *>(scope) + 0x24); }
inline const std::string &VariableValue(const ScopeNode *node)
{
    return *reinterpret_cast<const std::string *>(reinterpret_cast<const char *>(node) + 0x10);
}
inline void *ChildScope(const ScopeNode *node) { return *reinterpret_cast<void *const *>(reinterpret_cast<const char *>(node) + 0x10); }

using ArgsHasOptionFn = uint8_t(__thiscall *)(void *, const char *); // slot 1, +4 (al)

} // namespace

namespace {

// The state's stack of running definitions (+0x11c: a std::vector of PendingCall, cBaseParser.h): the definition
// (held) and the arguments it was created with.
inline PendingCallVector &Frames(void *state) { return *reinterpret_cast<PendingCallVector *>(static_cast<char *>(state) + 0x11c); }

// A definition's recorded lines: a vector of (line number, depth) pairs at +0x5c / +0x60, one per line of the body
// (NUL-separated texts at +0x50); its file name at +0x6c (a std::string) and its first line number at +0x84 (0: not
// from a file).
struct DefinitionLine {
    int32_t number;
    int32_t depth;
};
inline DefinitionLine *LinesBegin(void *definition) { return *reinterpret_cast<DefinitionLine **>(static_cast<char *>(definition) + 0x5c); }
inline DefinitionLine *LinesEnd(void *definition) { return *reinterpret_cast<DefinitionLine **>(static_cast<char *>(definition) + 0x60); }
inline std::string &DefinitionFile(void *definition) { return *reinterpret_cast<std::string *>(static_cast<char *>(definition) + 0x6c); }
inline int32_t DefinitionFirstLine(void *definition) { return *reinterpret_cast<int32_t *>(static_cast<char *>(definition) + 0x84); }

// The state's current file name (a std::string at +0xc8) and line number (+0xe0), the line the last command set
// (+0xe4, negative: none) and the meta-command stack (pointers at +0xf8 / +0xfc).
inline std::string &CurrentFile(void *state) { return *reinterpret_cast<std::string *>(static_cast<char *>(state) + 0xc8); }
inline int32_t &CurrentLine(void *state) { return *reinterpret_cast<int32_t *>(static_cast<char *>(state) + 0xe0); }
inline int32_t LastSetLine(void *state) { return *reinterpret_cast<int32_t *>(static_cast<char *>(state) + 0xe4); }
inline uint32_t MetaDepth(void *state)
{
    char *s = static_cast<char *>(state);
    return static_cast<uint32_t>(*reinterpret_cast<int32_t *>(s + 0xfc) - *reinterpret_cast<int32_t *>(s + 0xf8)) >> 2;
}
inline int32_t ConditionDepth(void *state)
{
    char *s = static_cast<char *>(state);
    return (*reinterpret_cast<int32_t *>(s + 0x114) - *reinterpret_cast<int32_t *>(s + 0x110)) >> 3;
}
inline bool TraceCommands(void *state) { return static_cast<uint8_t *>(state)[0xee] != 0; }

using OutputIntFn = void(__thiscall *)(void *, int32_t); // output object slot 9, +0x24
using ErrorTextFn = const char *(__thiscall *)(const void *); // cError slot 0

} // namespace

void cCreateCommand::Execute(cIArguments *args)
{
    // create name "arguments": runs a definition's lines with its parameters bound to the arguments, in a frame of the
    // state's definition stack. Errors from the lines come back as one error naming the definition and the line.
    ArgsPrepare(args);
    const char *const *a = ArgsGet(args, 2);
    const char *name = a[0];
    DefinitionPointers found = FindDefinitions(DefinitionsBegin(mState), DefinitionsEnd(mState), a, name + std::strlen(name), name);
    ThrowUnless(found.first != found.last, ts2_rva_e7e8a4, name);
    if (TraceCommands(mState)) {
        void *output = StateOutput(mState);
        const char *text0 = a[0], *text1 = a[1];
        Write(output, VirtualCall<ArgsTextFn, 4>(args, 0u));
        Write(output, ts2_rva_da4ca4);
        Write(output, text0);
        Write(output, ts2_rva_db12b4);
        Write(output, text1);
        Write(output, ts2_rva_db12b0);
        Write(output, kNewlineString);
    }
    uint32_t metaDepth = MetaDepth(mState);
    int32_t conditionDepth = ConditionDepth(mState);
    {
        PendingCall frame; // no definition, empty arguments
        Frames(mState).push_back(frame);
    }
    // The new frame holds the definition (a held pointer's assignment: the new one referenced, then the old dropped).
    void **top = &Frames(mState).back().held.definition;
    if (found.first != top) {
        void *definition = *found.first, *old = *top;
        if (definition != old) {
            if (definition) DefinitionAddRef(definition);
            *top = definition;
            if (old) DefinitionRelease(old);
        }
    }
    cArguments *frameArguments = &Frames(mState).back().arguments;
    frameArguments->Parse(a[1], 0, 1);
    void *definition = *found.first;
    frameArguments = &Frames(mState).back().arguments;
    int32_t parameters = VirtualCall<ArgsCountFn, 7>(DefinitionParameters(definition));
    if (parameters != frameArguments->Count()) {
        Frames(mState).pop_back();
        ThrowFormatted(kWrongNumberOfArguments);
    }
    int32_t lineNumber = 0, relativeLine = 0;
    std::string line;
    try {
        const char *text = DefinitionBody(definition).data();
        std::string savedFile(CurrentFile(mState));
        int32_t savedLine = CurrentLine(mState);
        if (DefinitionFirstLine(definition) != 0 && &CurrentFile(mState) != &DefinitionFile(definition))
            CurrentFile(mState).assign(DefinitionFile(definition));
        for (DefinitionLine *entry = LinesBegin(definition); entry != LinesEnd(definition); ++entry) {
            lineNumber = entry->number;
            relativeLine = lineNumber - DefinitionFirstLine(definition);
            int32_t depth = ConditionDepth(mState) - conditionDepth;
            size_t length = std::strlen(text);
            const char *next = text + length;
            // A line runs when the text around it is being run, or when it is a conditional of a nesting level below
            // the one the definition was recorded at (so that the conditionals stay balanced).
            if (Conditions(mState).back().active != 0 || entry->depth < depth) {
                line.assign(text, length);
                cMacroParser::Of(mState)->ExpandParameters(&line); // 0xb06920 (ours, gz/MacroDefinition.cpp)
                if (TraceCommands(mState)) {
                    void *output = StateOutput(mState);
                    const char *expanded = line.c_str();
                    Write(output, ts2_rva_dfd044);
                    VirtualCall<OutputIntFn, 9>(output, relativeLine);
                    Write(output, ts2_rva_da9e7c);
                    Write(output, expanded);
                    Write(output, kNewlineString);
                }
                if (DefinitionFirstLine(definition) != 0) CurrentLine(mState) = lineNumber;
                VirtualCall<ParserTextFn, 15>(mParser, line.c_str());
                int32_t set = LastSetLine(mState);
                if (set >= 0) set -= conditionDepth;
                if (entry->depth == -1000 || set >= 0) entry->depth = set;
            }
            text = next + 1;
        }
        if (&CurrentFile(mState) != &savedFile) CurrentFile(mState).assign(savedFile);
        CurrentLine(mState) = savedLine;
        if (MetaDepth(mState) != metaDepth) ThrowFormatted(ts2_rva_e7e8c0);
    } catch (cError &error) {
        // (The original also builds a cRZString of the message and searches it for a text, with the result unused.)
        if (!Frames(mState).empty()) Frames(mState).pop_back();
        const char *message = VirtualCall<ErrorTextFn, 0>(&error);
        if (DefinitionFirstLine(definition) == 0)
            ThrowFormatted(ts2_rva_e7e954, message, a[0], relativeLine);
        ThrowFormatted(ts2_rva_e7e924, DefinitionFile(definition).c_str(), lineNumber, message, a[0], relativeLine);
    }
    if (!Frames(mState).empty()) Frames(mState).pop_back();
}

void cDumpParserStateCommand::Execute(cIArguments *args)
{
    // dumpparserstate [-variables] [-definitions] [-scope]: writes the parser state to the output.
    if (VirtualCall<ArgsHasOptionFn, 1>(args, ts2_rva_e7eaf4)) {
        // Every variable as 'path:name' = 'value', scope by scope, depth first from the root (the scopes still to visit
        // are a stack: the last child found is visited first). The root's path is empty; a child's is its parent's, ':'
        // and its name.
        struct Pending {
            std::string path;
            void *scope;
        };
        std::vector<Pending> pending(1);
        pending.back().scope = static_cast<char *>(mState) + 0x78;
        while (!pending.empty()) {
            std::string path(pending.back().path);
            void *scope = pending.back().scope;
            pending.pop_back();
            ScopeNode *head = VariablesHead(scope);
            for (ScopeNode *node = head->next; node != head; node = node->next) {
                std::string value(VariableValue(node));
                std::string name(node->name, node->nameLength);
                void *output = StateOutput(mState);
                Write(output, ts2_rva_dddf9c);
                Write(output, path.c_str());
                Write(output, ts2_rva_da9e7c);
                Write(output, name.c_str());
                Write(output, ts2_rva_e7eb00);
                Write(output, value.c_str());
                Write(output, ts2_rva_dddf9c);
                Write(output, kNewlineString);
            }
            head = ChildrenHead(scope);
            for (ScopeNode *node = head->next; node != head; node = node->next) {
                std::string name(node->name, node->nameLength);
                pending.push_back({path + ts2_rva_da9e7c + name, ChildScope(node)});
            }
        }
    }
    if (VirtualCall<ArgsHasOptionFn, 1>(args, ts2_rva_e7eb08)) {
        // Every definition as its name, a separator, then its parameters each after a space, and an end of line.
        void **definition = DefinitionsBegin(mState);
        if (definition != DefinitionsEnd(mState)) {
            do {
                void *output = StateOutput(mState);
                Write(output, DefinitionName(*definition).c_str());
                Write(output, ts2_rva_db12b4);
                void *parameters = DefinitionParameters(*definition);
                int32_t count = VirtualCall<ArgsCountFn, 7>(parameters);
                for (int32_t i = 0; i < count; ++i) {
                    void *def = *definition;
                    output = StateOutput(mState);
                    Write(output, ts2_rva_da4ca4);
                    Write(output, VirtualCall<ArgsTextFn, 4>(DefinitionParameters(def), static_cast<uint32_t>(i)));
                }
                Write(StateOutput(mState), ts2_rva_df732c);
                ++definition;
            } while (definition != DefinitionsEnd(mState));
        }
    }
    if (VirtualCall<ArgsHasOptionFn, 1>(args, ts2_rva_e7eb14)) {
        // The current scope's path (a std::string at +0x134 of the state), quoted.
        void *state = mState;
        const std::string &scopePath = *reinterpret_cast<const std::string *>(static_cast<char *>(state) + 0x134);
        const char *text = scopePath.c_str();
        void *output = StateOutput(state);
        Write(output, ts2_rva_dddf9c);
        Write(output, text);
        Write(output, ts2_rva_d9f86c);
    }
    ArgsPrepare(args);
}

} // namespace nGZCommandParser
