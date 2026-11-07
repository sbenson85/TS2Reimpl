#pragma once
// The grid's matrices of cGZLegacyControl (lane NH, foundations F6 leftovers): the sparse tables the Grid handler keeps on
// its window (the cells, the columns' and the rows' descriptions). Replaces the original constructor 0x85d3bc and the two
// setters 0x8640a4 (a cell) and 0x86422c (a column or row description); lane FI: the matrices' destructors 0x85d901
// (cells) and 0x85d928 (descriptions), and the cell's constructor 0x85d5f0, copy 0x85d59d and assignment 0x85d8b2.
//
// Layout (0x20 bytes, decoded from the three functions and the holders that free them): a std::map of lines at +0 (head
// node pointer, size), the number of values stored at +8, the accepted line range at +0xc / +0x10 and entry range at +0x14 /
// +0x18, a byte at +0x1c that nothing here sets. Both maps are instances of the library's std::map<int, ...> (MSVC 14.40,
// the toolchain's own: their nodes are the allocator's, the head node is the node type's size): a line is {its index again, a
// map of entries}, an entry {its index again, the value}. The original's instantiations are named by node size (Blob4<N>)
// in db/names.jsonl; ours are the same shapes, so the original's holder destructors free ours (tests/gz_legacy_handlers.cpp
// destroys the matrices the grid handler made through them).
#include "rz/cRZString.h"

#include <climits>
#include <cstdint>
#include <map>

namespace legacygrid {

// What a matrix keeps per cell (the original's cell record, 0x44 bytes): the image or text resource, the flags, the text and
// the font. Copying follows the original's copy 0x85d59d (the text through the string copy constructor, which starts a
// reference count of 0) and assignment 0x85d8b2 (the text through assign(data, size), never to itself; the string's own
// vtable and count stay).
struct GridCell {
    uint32_t mGroup = 0;    // +0
    uint32_t mInstance = 0; // +4
    uint32_t mFlags = 0;    // +8
    union {
        cRZString mText; // +0xc
    };
    uint32_t mFontId = 0;        // +0x2c
    uint32_t mFontWords[5] = {}; // +0x30 .. +0x40
    GridCell() { mText.Construct(); }
    GridCell(const GridCell &other)
        : mGroup(other.mGroup), mInstance(other.mInstance), mFlags(other.mFlags), mFontId(other.mFontId)
    {
        mText.Construct(other.mText, 0, uint32_t(-1));
        for (int i = 0; i < 5; ++i) mFontWords[i] = other.mFontWords[i];
    }
    GridCell &operator=(const GridCell &other)
    {
        mGroup = other.mGroup;
        mInstance = other.mInstance;
        mFlags = other.mFlags;
        if (&mText.mString != &other.mText.mString) mText.mString.assign(other.mText.mString.c_str(), other.mText.mString.size());
        mFontId = other.mFontId;
        for (int i = 0; i < 5; ++i) mFontWords[i] = other.mFontWords[i];
        return *this;
    }
    ~GridCell() { mText.Destruct(); }
    // 0x85d5f0 (the default constructor), 0x85d59d (the copy) and 0x85d8b2 (the assignment): the hooks' thiscall adapters.
    static GridCell *__fastcall Construct(GridCell *memory, void *);
    static GridCell *__fastcall Copy(GridCell *memory, void *, const GridCell *other);
    static GridCell *__fastcall Assign(GridCell *self, void *, const GridCell *other);
};
static_assert(sizeof(GridCell) == 0x44, "GridCell layout");

// A column's or row's description: "first,last,value".
struct GridLineData {
    int32_t mFirst = 0;
    int32_t mLast = 0;
    int32_t mValue = 0;
};
static_assert(sizeof(GridLineData) == 12, "GridLineData layout");

// A stored value with its index (the original keeps the index twice: as the map's key and first in the value).
template <class Value> struct MatrixEntry {
    int32_t mIndex;
    Value mValue;
    MatrixEntry(int32_t index, const Value &value) : mIndex(index), mValue(value) {}
};

template <class Value> struct MatrixLine {
    int32_t mIndex;
    std::map<int32_t, MatrixEntry<Value>> mEntries;
    explicit MatrixLine(int32_t index) : mIndex(index) {}
};

template <class Value> class GridMatrix {
public:
    // 0x85d3bc: empty, every index accepted.
    GridMatrix() : mCount(0), mLineMin(kLowest), mLineMax(INT_MAX), mEntryMin(kLowest), mEntryMax(INT_MAX) {}

    // 0x8640a4 (the cells: `entry` is the column, `line` the row) and 0x86422c (a column description: entry = its first
    // number, line 0; a row description: entry 0, line = its first number): false when an index is outside the accepted
    // ranges. The value is stored under (line, entry): a new one counts, an old one is overwritten after being copied to
    // *previous (when that is given).
    bool Set(int32_t entry, int32_t line, const Value *value, Value *previous);

    // 0x85d901 (cells) / 0x85d928 (descriptions): a matrix whose byte at +0x1c is set is emptied first (its count and
    // the byte back to 0); then the lines and their entries go, and the head node.
    ~GridMatrix()
    {
        if (mFlag) Clear();
    }
    void Clear()
    {
        mLines.clear();
        mCount = 0;
        mFlag = 0;
    }
    // The hooks' adapter (thiscall, the deleting destructor's flag on the stack): destroys the matrix, frees it when bit 0
    // of `flags` is set, and returns it.
    static void *__fastcall Destroy(GridMatrix *self, void *, uint32_t flags);

    using Lines = std::map<int32_t, MatrixLine<Value>>;
    Lines mLines;         // +0
    uint32_t mCount;      // +8: the values stored
    int32_t mLineMin;     // +0xc
    int32_t mLineMax;     // +0x10
    int32_t mEntryMin;    // +0x14
    int32_t mEntryMax;    // +0x18
    uint8_t mFlag = 0;    // +0x1c

private:
    static constexpr int32_t kLowest = INT_MIN + 1; // 0x80000001
};

// The two kinds the grid uses (Set is defined, and instantiated for them, in cGZLegacyGridMatrix.size.cpp).
using CellMatrix = GridMatrix<GridCell>;
using LineMatrix = GridMatrix<GridLineData>;
static_assert(sizeof(CellMatrix) == 0x20 && sizeof(LineMatrix) == 0x20, "matrix layout");
static_assert(offsetof(CellMatrix, mCount) == 8 && offsetof(CellMatrix, mFlag) == 0x1c, "matrix layout");

// 0x85d3bc (the hook's adapter; thiscall, no stack arguments): constructs the matrix in `memory` and returns it. Both kinds
// share the original's constructor (and its 0x20-byte head node); the cell kind is built.
void *__fastcall ConstructGridMatrix(void *memory, void *);

} // namespace legacygrid
