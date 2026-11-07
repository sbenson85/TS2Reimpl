#pragma once
// A cBaseParser (or a ParserNamespace) as built, in a form two copies can be compared in (lane HP): the object's bytes
// with every word that holds a heap pointer replaced by 0, followed by what those pointers lead to (read at raw
// offsets, not through our struct types). And a teardown that frees what the constructor allocated.
#include "gz/CommandParser.h"
#include "gz/cBaseParser.h"
#include "gz_file_source.h"

#include <cstdint>
#include <cstring>
#include <list>
#include <memory>
#include <vector>

namespace parserform {

inline uint32_t Word(const uint8_t *p, size_t off)
{
    uint32_t v;
    std::memcpy(&v, p + off, 4);
    return v;
}
inline const uint8_t *Ptr(uint32_t word) { return reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(word)); }
inline void Append(std::vector<uint8_t> &out, uint32_t v)
{
    const auto *p = reinterpret_cast<const uint8_t *>(&v);
    out.insert(out.end(), p, p + 4);
}

// The pointer words of an object: blanked in the byte copy (each form below describes them).
struct Blanks {
    std::vector<size_t> offsets;
    void Add(size_t off, size_t words = 1)
    {
        for (size_t i = 0; i < words; ++i) offsets.push_back(off + 4 * i);
    }
};

// A hash table (CommandTable / VariableTable layout) at `at`: maxLoad bits, size, mask, bucket count; the sentinel
// linked to itself; the bucket vector's size and capacity in words and whether every bucket word is the sentinel.
inline void TableForm(std::vector<uint8_t> &out, Blanks &blanks, const uint8_t *object, size_t off)
{
    const uint8_t *at = object + off;
    const uint32_t head = Word(at, 4);
    Append(out, Word(at, 0));
    Append(out, Word(at, 8));
    Append(out, Word(at, 0x18));
    Append(out, Word(at, 0x1c));
    Append(out, head != 0 && Word(Ptr(head), 0) == head && Word(Ptr(head), 4) == head);
    const uint32_t first = Word(at, 0xc), last = Word(at, 0x10), end = Word(at, 0x14);
    Append(out, (last - first) / 4);
    Append(out, (end - first) / 4);
    uint32_t allHead = first != 0;
    for (uint32_t p = first; first && p < last; p += 4) allHead &= Word(Ptr(p), 0) == head;
    Append(out, allHead);
    blanks.Add(off + 4);
    blanks.Add(off + 0xc, 3);
}

// A NamePool at `off`: its sentinel chunk linked to itself, count, used.
inline void PoolForm(std::vector<uint8_t> &out, Blanks &blanks, const uint8_t *object, size_t off)
{
    const uint32_t head = Word(object, off);
    Append(out, head != 0 && Word(Ptr(head), 0) == head && Word(Ptr(head), 4) == head);
    blanks.Add(off);
}

// A std::list at `off`: an empty ring on its sentinel (the size is compared with the bytes).
inline void ListForm(std::vector<uint8_t> &out, Blanks &blanks, const uint8_t *object, size_t off)
{
    const uint32_t head = Word(object, off);
    Append(out, head != 0 && Word(Ptr(head), 0) == head && Word(Ptr(head), 4) == head);
    blanks.Add(off);
}

inline void NamespaceForms(std::vector<uint8_t> &out, Blanks &blanks, const uint8_t *object, size_t off)
{
    TableForm(out, blanks, object, off);
    TableForm(out, blanks, object, off + 0x20);
    PoolForm(out, blanks, object, off + 0x40);
}

inline std::vector<uint8_t> Finish(const uint8_t *object, size_t size, const Blanks &blanks, std::vector<uint8_t> forms)
{
    std::vector<uint8_t> out(object, object + size);
    for (size_t off : blanks.offsets) std::memset(out.data() + off, 0, 4);
    out.insert(out.end(), forms.begin(), forms.end());
    return out;
}

inline std::vector<uint8_t> NamespaceForm(const uint8_t *object, size_t size)
{
    std::vector<uint8_t> forms;
    Blanks blanks;
    NamespaceForms(forms, blanks, object, 0);
    return Finish(object, size, blanks, forms);
}

// A parser built at `object` (`size` bytes compared from it, past the object when the test gives a derived layout).
// The condition stack's element: level and the two flags (the two padding bytes are left out: the original's are
// bytes of its stack frame).
// unwrittenToo: also blank the bytes the constructor does not write (padding, +0x2c, +0xe4, +0xef), for parsers on
// two different heap blocks (from the same junk, the test compares them too).
inline std::vector<uint8_t> ParserForm(const uint8_t *object, size_t size, bool unwrittenToo = false)
{
    std::vector<uint8_t> forms;
    Blanks blanks;
    // The source: its 12 bytes (our cFileSource's vtable words as the game's: tests/gz_file_source.h).
    const uint32_t source = Word(object, 0x1c);
    if (source) {
        uint8_t bytes[12];
        filesource::SourceBytes(Ptr(source), bytes);
        forms.insert(forms.end(), bytes, bytes + 12);
    }
    blanks.Add(0x1c);
    // The evaluator's names: size, capacity, the entries.
    const uint32_t first = Word(object, 0x20), last = Word(object, 0x24), end = Word(object, 0x28);
    Append(forms, last - first);
    Append(forms, end - first);
    if (first && last > first && last - first < 0x1000) forms.insert(forms.end(), Ptr(first), Ptr(last));
    blanks.Add(0x20, 3);
    TableForm(forms, blanks, object, 0x30);
    PoolForm(forms, blanks, object, 0x50);
    NamespaceForms(forms, blanks, object, 0x78);
    // The condition stack.
    const uint32_t cFirst = Word(object, 0x110), cLast = Word(object, 0x114), cEnd = Word(object, 0x118);
    Append(forms, cLast - cFirst);
    Append(forms, cEnd - cFirst);
    for (uint32_t p = cFirst; cFirst && p < cLast && cLast - cFirst < 0x100; p += 8) forms.insert(forms.end(), Ptr(p), Ptr(p) + 6);
    blanks.Add(0x110, 3);
    ListForm(forms, blanks, object, 0x19c);
    ListForm(forms, blanks, object, 0x1a4);
    std::vector<uint8_t> out = Finish(object, size, blanks, forms);
    if (unwrittenToo) {
        const std::pair<size_t, size_t> unwritten[] = {{0x11, 3}, {0x2c, 4}, {0x5d, 3}, {0xe4, 4}, {0xef, 1}, {0x199, 3}};
        for (const auto &[off, n] : unwritten) std::memset(out.data() + off, 0, n);
    }
    return out;
}

// The first offset at which two forms differ (for the failure message), or -1.
inline long FirstDiff(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b)
{
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i)
        if (a[i] != b[i]) return static_cast<long>(i);
    return a.size() == b.size() ? -1 : static_cast<long>(n);
}

// Frees what the constructors allocated (the source block itself; the cFileSource is not released through its
// vtable, whose last release would run the game's destructor).
inline void TearDownTable(nGZCommandParser::CommandTable &t)
{
    std::destroy_at(&t.buckets);
    ::operator delete(t.head);
}
inline void TearDownTable(nGZCommandParser::VariableTable &t)
{
    std::destroy_at(&t.buckets);
    ::operator delete(t.head);
}
inline void TearDownNamespace(nGZCommandParser::ParserNamespace &n)
{
    TearDownTable(n.mVariables);
    TearDownTable(n.mCommands);
    n.mNames.Destruct();
}
inline void TearDownParser(nGZCommandParser::cBaseParser &p)
{
    ::operator delete(p.mSource);
    std::destroy_at(&p.mFunctions);
    TearDownTable(p.mCommands);
    p.mNames.Destruct();
    TearDownNamespace(p.mGlobalNamespace);
    std::destroy_at(&p.mConditions);
    std::destroy_at(&p.mList19C);
    std::destroy_at(&p.mList1A4);
}

} // namespace parserform
