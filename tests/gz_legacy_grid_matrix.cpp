// Difftests for the grid's matrices (lane NH, foundations F6 leftovers): src/gz/cGZLegacyGridMatrix.size.cpp.
//
// LegacyGridMatrix_Construct: the constructor 0x85d3bc against ours on memory filled with a marker: all 0x20 bytes
// (padding included), the head node it allocates (its words with the pointers to itself named), the allocation counts; the
// matrix is then taken apart by the original's destructor 0x85d901 and nothing may be left allocated.
//
// LegacyGridMatrix_SetCell: the cell setter 0x8640a4 against ours. Both sides build the matrix with the original
// constructor (so only the setter differs), narrow some of its index ranges, and make a run of Set calls on small
// indices (so cells are replaced often), with or without a previous-value out. Compared after every call: the result,
// the previous cell (group, instance, flags, the text with its length and capacity, the font words), the matrix header
// and, at the end, every line and entry node in order (keys, colours, nil flags, values) and the allocation counts. The
// cells' texts come from a pool of empty, short, 15 / 16-character and long strings, and the out cell starts with one
// of them as well (the assignment keeps or grows its buffer). The original's destructor 0x85d901 takes the matrix apart
// (it frees ours: one heap) and nothing may be left allocated.
//
// LegacyGridMatrix_SetEntry: the same for the column and row descriptions, 0x86422c (the matrices destroyed by 0x85d928).
//
// LegacyGridMatrix_Destroy (lane FI): the matrices' deleting destructors 0x85d901 (cells) and 0x85d928 (descriptions)
// against ours, on heap matrices filled by our setter, with the byte at +0x1c set or not and the flag word's bit 0 set
// (the block freed too) or not (other bits random). Compared: the result, the blocks and bytes freed and, when the block
// stays, its words after the destructor (the count and the byte at +0x1c; the head pointer as left or not).
//
// LegacyGridMatrix_Cell (lane FI): the cell's constructor 0x85d5f0, copy 0x85d59d and assignment 0x85d8b2 against ours.
// Constructor and copy on memory filled with a marker (one byte past the cell checked), the assignment onto a filled
// cell, sometimes itself. Compared: the result, both cells (DumpCell: the string's vtable, count, text, length and
// capacity included), the marker and the blocks allocated.
//
// What a mutant of the setter must not survive: swapped keys (entry / line), a bound off by one or unsigned, a missing
// count, a missing overwrite or previous copy, a previous written when null.
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/cGZLegacyGridMatrix.h"
#include "rz/cRZString.h"

#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace legacy_grid_matrix_test {

using legacygrid::CellMatrix;
using legacygrid::GridCell;
using legacygrid::GridLineData;
using legacygrid::LineMatrix;
using svcfx::Blob;

constexpr uint32_t kConstruct = 0x85d3bc, kSetCell = 0x8640a4, kSetEntry = 0x86422c;
constexpr uint32_t kCellDestroy = 0x85d901, kLineDestroy = 0x85d928;
constexpr uint32_t kCellConstruct = 0x85d5f0, kCellCopy = 0x85d59d, kCellAssign = 0x85d8b2;

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

using ConstructFn = void *(__fastcall *)(void *, void *);
using DestroyFn = void(__fastcall *)(void *, void *, uint32_t);
using DeletingFn = void *(__fastcall *)(void *, void *, uint32_t);
using CellNewFn = void *(__fastcall *)(void *, void *);
using CellCopyFn = void *(__fastcall *)(void *, void *, const void *);
using SetCellFn = bool(__fastcall *)(void *, void *, int32_t, int32_t, const void *, void *);

const uint8_t *Bytes(uint32_t p) { return reinterpret_cast<const uint8_t *>(uintptr_t(p)); }
uint32_t Word(const uint8_t *b, uint32_t off)
{
    uint32_t v;
    std::memcpy(&v, b + off, 4);
    return v;
}

uint32_t HashText(const char *text)
{
    uint32_t h = 2166136261u;
    for (; *text; ++text) h = (h ^ uint8_t(*text)) * 16777619u;
    return h;
}

// ---- dumps -------------------------------------------------------------------------------------------------------------

void DumpCell(Blob &b, const GridCell &cell)
{
    b.push_back(cell.mGroup);
    b.push_back(cell.mInstance);
    b.push_back(cell.mFlags);
    b.push_back(cell.mText.mVTable == GameAddress<void>(cRZString::kVTable));
    b.push_back(cell.mText.mRefCount);
    b.push_back(HashText(cell.mText.ToChar()));
    b.push_back(cell.mText.Strlen());
    b.push_back(uint32_t(cell.mText.mString.capacity()));
    b.push_back(cell.mFontId);
    for (uint32_t w : cell.mFontWords) b.push_back(w);
}

void DumpValue(Blob &b, const uint8_t *node, bool cell)
{
    for (uint32_t off : {0x18u, 0x1cu, 0x20u}) b.push_back(Word(node, off)); // line data, or group, instance, flags
    if (!cell) return;
    const cRZString *text = reinterpret_cast<const cRZString *>(node + 0x24);
    b.push_back(Word(node, 0x24)); // the string's vtable
    b.push_back(text->mRefCount);
    b.push_back(HashText(text->ToChar()));
    b.push_back(text->Strlen());
    b.push_back(uint32_t(text->mString.capacity()));
    for (uint32_t off = 0x44; off <= 0x58; off += 4) b.push_back(Word(node, off));
}

// In order: a node's colour, nil flag, key and second key; a line's entry tree below it.
void DumpTree(Blob &b, const uint8_t *node, bool nested, bool cell)
{
    if (node[0xd]) return;
    DumpTree(b, Bytes(Word(node, 0)), nested, cell);
    b.push_back(0xa0000000u | (node[0xc] << 8) | node[0xd]);
    b.push_back(Word(node, 0x10));
    b.push_back(Word(node, 0x14));
    if (nested) {
        const uint8_t *head = Bytes(Word(node, 0x18));
        b.push_back(Word(node, 0x1c));
        b.push_back(head[0xc]);
        b.push_back(head[0xd]);
        DumpTree(b, Bytes(Word(head, 4)), false, cell);
    } else {
        DumpValue(b, node, cell);
    }
    DumpTree(b, Bytes(Word(node, 8)), nested, cell);
}

void DumpMatrix(Blob &b, const uint8_t *matrix, bool cell)
{
    for (uint32_t off = 4; off < 0x1c; off += 4) b.push_back(Word(matrix, off));
    b.push_back(matrix[0x1c]);
    const uint8_t *head = Bytes(Word(matrix, 0));
    b.push_back(head[0xc]);
    b.push_back(head[0xd]);
    DumpTree(b, Bytes(Word(head, 4)), true, cell);
}

void DumpCounts(Blob &b, const difftest::AllocationCounter &c)
{
    // The blocks and bytes still allocated; not the number of allocations made on the way: the original builds a line's or
    // a cell's value on its stack and copies it into the node (a temporary head node, a temporary string), ours builds it in
    // the node. Those temporaries are freed before the call returns.
    b.push_back(uint32_t(c.Blocks()));
    b.push_back(uint32_t(c.Bytes()));
}

// ---- Construct ---------------------------------------------------------------------------------------------------------

DIFF_TEST(LegacyGridMatrix_Construct, 0x85d3bc)
{
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    for (uint32_t seed = 1; seed <= 40; ++seed) {
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            uint8_t memory[0x30];
            std::memset(memory, 0x30 + seed, sizeof memory);
            void *result;
            {
                difftest::AllocationCounter counter;
                if (side)
                    result = legacygrid::ConstructGridMatrix(memory, nullptr);
                else
                    result = ctx.Original<ConstructFn>(kConstruct)(memory, nullptr);
                counter.Stop();
                results[side].push_back(result == memory);
                DumpCounts(results[side], counter);
            }
            for (uint32_t i = 8; i < sizeof memory; ++i) results[side].push_back(memory[i]); // padding and the untouched tail
            const uint8_t *head = Bytes(Word(memory, 0));
            for (uint32_t off = 0; off < 0x10; off += 4) {
                const uint32_t w = Word(head, off);
                // The colour and nil bytes only: the two bytes after them are padding nothing sets.
                results[side].push_back(off == 0xc ? (w & 0xffff) : w == uint32_t(uintptr_t(head)) ? 0xaaaaaaaau : w);
            }
            results[side].push_back(Word(memory, 4));
            // The destructor takes it apart (the flag 0: the block itself stays).
            difftest::AllocationCounter counter;
            ctx.Original<DestroyFn>(kCellDestroy)(memory, nullptr, 0);
            counter.Stop();
            results[side].push_back(uint32_t(counter.Blocks()));
        }
        if (!svcfx::Same(ctx, results[0], results[1], "LegacyGridMatrix_Construct", seed)) break;
    }
}

// ---- the setters -------------------------------------------------------------------------------------------------------

const char *const kTexts[] = {"",
                              "a",
                              "label",
                              "fifteen chars!!",
                              "sixteen chars!!!",
                              "a rather longer text than the small buffer holds, over forty",
                              "with,commas:and{braces}",
                              "x"};

void FillCell(GridCell &cell)
{
    cell.mGroup = gRng();
    cell.mInstance = gRng();
    cell.mFlags = gRng();
    cell.mText.FromChar(kTexts[Draw(sizeof(kTexts) / sizeof(kTexts[0]))]);
    if (Draw(8) == 0) cell.mText.mString.assign("ab\0cd", 5); // an embedded NUL: copies go by length, not to the first NUL
    cell.mFontId = gRng();
    for (uint32_t &w : cell.mFontWords) w = gRng();
}

void NarrowRanges(uint8_t *matrix)
{
    if (Draw(3) != 0) return;
    const int32_t lineLow = -int32_t(Draw(3)), lineHigh = int32_t(Draw(4));
    const int32_t entryLow = -int32_t(Draw(3)), entryHigh = int32_t(Draw(4));
    std::memcpy(matrix + 0xc, &lineLow, 4);
    std::memcpy(matrix + 0x10, &lineHigh, 4);
    std::memcpy(matrix + 0x14, &entryLow, 4);
    std::memcpy(matrix + 0x18, &entryHigh, 4);
}

int32_t Index()
{
    switch (Draw(12)) {
    case 0: return INT32_MIN;
    case 1: return INT32_MAX;
    case 2: return -5;
    default: return int32_t(Draw(6)) - 2;
    }
}

DIFF_TEST(LegacyGridMatrix_SetCell, kSetCell)
{
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            gRng.seed(seed);
            alignas(8) uint8_t memory[0x20];
            std::memset(memory, 0xcd, sizeof memory);
            ctx.Original<ConstructFn>(kConstruct)(memory, nullptr);
            NarrowRanges(memory);
            auto *matrix = reinterpret_cast<CellMatrix *>(memory);
            difftest::AllocationCounter counter;
            const uint32_t steps = 1 + Draw(24);
            for (uint32_t step = 0; step < steps; ++step) {
                GridCell cell, previous;
                FillCell(cell);
                FillCell(previous);
                const bool wantPrevious = Draw(3) != 0;
                const int32_t column = Index(), row = Index();
                const uint32_t previousGroup = previous.mGroup;
                bool ok;
                if (side)
                    ok = matrix->Set(column, row, &cell, wantPrevious ? &previous : nullptr);
                else
                    ok = ctx.Original<SetCellFn>(kSetCell)(memory, nullptr, column, row, &cell, wantPrevious ? &previous : nullptr);
                results[side].push_back(ok);
                results[side].push_back(previous.mGroup != previousGroup);
                DumpCell(results[side], previous);
                DumpCell(results[side], cell);
            }
            counter.Stop();
            DumpCounts(results[side], counter);
            DumpMatrix(results[side], memory, true);
            difftest::AllocationCounter destroy;
            ctx.Original<DestroyFn>(kCellDestroy)(memory, nullptr, 0);
            destroy.Stop();
            results[side].push_back(uint32_t(destroy.Blocks() + counter.Blocks()));
        }
        if (!svcfx::Same(ctx, results[0], results[1], "LegacyGridMatrix_SetCell", seed)) break;
    }
}

DIFF_TEST(LegacyGridMatrix_SetEntry, kSetEntry)
{
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            gRng.seed(seed);
            alignas(8) uint8_t memory[0x20];
            std::memset(memory, 0xcd, sizeof memory);
            ctx.Original<ConstructFn>(kConstruct)(memory, nullptr);
            NarrowRanges(memory);
            auto *matrix = reinterpret_cast<LineMatrix *>(memory);
            difftest::AllocationCounter counter;
            const uint32_t steps = 1 + Draw(24);
            for (uint32_t step = 0; step < steps; ++step) {
                const GridLineData line = {int32_t(gRng()), int32_t(gRng()), int32_t(gRng())};
                // The out is 12 bytes in a marked buffer: a byte past it, or any write when it is null, shows.
                uint8_t out[20];
                std::memset(out, 0x5a, sizeof out);
                const bool wantPrevious = Draw(3) != 0;
                const int32_t first = Index(), second = Index();
                bool ok;
                if (side)
                    ok = matrix->Set(first, second, &line, wantPrevious ? reinterpret_cast<GridLineData *>(out + 4) : nullptr);
                else
                    ok = ctx.Original<SetCellFn>(kSetEntry)(memory, nullptr, first, second, &line,
                                                            wantPrevious ? out + 4 : nullptr);
                results[side].push_back(ok);
                for (uint8_t byte : out) results[side].push_back(byte);
            }
            counter.Stop();
            DumpCounts(results[side], counter);
            DumpMatrix(results[side], memory, false);
            difftest::AllocationCounter destroy;
            ctx.Original<DestroyFn>(kLineDestroy)(memory, nullptr, 0);
            destroy.Stop();
            results[side].push_back(uint32_t(destroy.Blocks() + counter.Blocks()));
        }
        if (!svcfx::Same(ctx, results[0], results[1], "LegacyGridMatrix_SetEntry", seed)) break;
    }
}

// ---- the destructors ---------------------------------------------------------------------------------------------------

template <class Matrix, class Value> void FillMatrix(Matrix *matrix, void (*fill)(Value &))
{
    const uint32_t steps = Draw(14);
    for (uint32_t step = 0; step < steps; ++step) {
        Value value;
        fill(value);
        matrix->Set(int32_t(Draw(5)) - 1, int32_t(Draw(4)) - 1, &value, nullptr);
    }
}

void FillLine(GridLineData &line) { line = {int32_t(gRng()), int32_t(gRng()), int32_t(gRng())}; }

DIFF_TEST(LegacyGridMatrix_Destroy, kCellDestroy)
{
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        Blob results[2];
        const bool cells = seed % 2 != 0;
        for (int side = 0; side < 2; ++side) {
            gRng.seed(seed);
            uint8_t *memory = static_cast<uint8_t *>(::operator new(0x20));
            ctx.Original<ConstructFn>(kConstruct)(memory, nullptr);
            if (cells)
                FillMatrix(reinterpret_cast<CellMatrix *>(memory), &FillCell);
            else
                FillMatrix(reinterpret_cast<LineMatrix *>(memory), &FillLine);
            if (Draw(3) == 0) memory[0x1c] = uint8_t(1 + Draw(255));
            const uint32_t flags = Draw(2) | (Draw(3) == 0 ? gRng() & ~1u : 0);
            const uint32_t head = Word(memory, 0);
            difftest::AllocationCounter counter;
            void *result;
            if (side)
                result = cells ? CellMatrix::Destroy(reinterpret_cast<CellMatrix *>(memory), nullptr, flags)
                               : LineMatrix::Destroy(reinterpret_cast<LineMatrix *>(memory), nullptr, flags);
            else
                result = ctx.Original<DeletingFn>(cells ? kCellDestroy : kLineDestroy)(memory, nullptr, flags);
            counter.Stop();
            results[side].push_back(result == memory);
            results[side].push_back(uint32_t(counter.Blocks()));
            results[side].push_back(uint32_t(counter.Bytes()));
            if (!(flags & 1)) {
                results[side].push_back(Word(memory, 0) == head);
                for (uint32_t off = 4; off < 0x1c; off += 4) results[side].push_back(Word(memory, off));
                results[side].push_back(memory[0x1c]); // +0x1d..0x1f: padding nothing writes (the heap's leftovers)
                ::operator delete(memory);
            }
        }
        if (!svcfx::Same(ctx, results[0], results[1], "LegacyGridMatrix_Destroy", seed)) break;
    }
}

// ---- the cell's constructor, copy and assignment -----------------------------------------------------------------------

DIFF_TEST(LegacyGridMatrix_Cell, kCellCopy)
{
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        Blob results[2];
        const uint32_t op = seed % 3;
        for (int side = 0; side < 2; ++side) {
            gRng.seed(seed);
            alignas(8) uint8_t memory[sizeof(GridCell) + 4];
            std::memset(memory, int(0x40 + seed % 64), sizeof memory);
            GridCell source;
            FillCell(source);
            GridCell *cell = reinterpret_cast<GridCell *>(memory);
            if (op == 2) {
                ::new (memory) GridCell;
                FillCell(*cell);
            }
            const GridCell *other = op == 2 && Draw(6) == 0 ? cell : &source;
            difftest::AllocationCounter counter;
            void *result;
            if (side) {
                switch (op) {
                case 0: result = GridCell::Construct(cell, nullptr); break;
                case 1: result = GridCell::Copy(cell, nullptr, other); break;
                default: result = GridCell::Assign(cell, nullptr, other); break;
                }
            } else {
                switch (op) {
                case 0: result = ctx.Original<CellNewFn>(kCellConstruct)(cell, nullptr); break;
                case 1: result = ctx.Original<CellCopyFn>(kCellCopy)(cell, nullptr, other); break;
                default: result = ctx.Original<CellCopyFn>(kCellAssign)(cell, nullptr, other); break;
                }
            }
            counter.Stop();
            results[side].push_back(result == cell);
            DumpCounts(results[side], counter);
            DumpCell(results[side], *cell);
            DumpCell(results[side], source);
            for (uint32_t i = sizeof(GridCell); i < sizeof memory; ++i) results[side].push_back(memory[i]);
            cell->~GridCell();
        }
        if (!svcfx::Same(ctx, results[0], results[1], "LegacyGridMatrix_Cell", seed)) break;
    }
}

} // namespace legacy_grid_matrix_test
