#pragma once
#include "game/owned.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace nGZCommandParser {

// One of the command parser's expression functions (the anonymous-namespace classes cEvalPow,
// cEvalSqrt, cEvalStrcmp, ... with RTTI .?AUcEval*@?A0x293d2d98@@; static objects in .data from RVA
// 0x10749ac). Opaque here: the table only stores their addresses.
struct cEvalFunction;

// The expression evaluator's table of built-in names: "true"/"on", "false"/"off", "pi", "rnd", the
// math functions, "strcmp", "strstr" and the angle conversions, each with the object that evaluates
// it, sorted by name so the evaluator can binary-search it. Built by the constructor below from the
// 27-entry static array at RVA 0xe7f590. Instances: the static of initialiser slot 675 (0x10fb30c,
// destroyed by 0xd97450), cBaseParser's member at +0x20 (built in its constructor 0xafd800) and a
// local in cTSWinProcNeighborhood::DoMessage (0x674ddf).
class EvalFunctionTable {
public:
    using Entry = std::pair<const char *, cEvalFunction *>; // 8 bytes

    // The order: strcmp of the names (unsigned bytes), inlined by the original (/Oi).
    struct ByName {
        bool operator()(const Entry &a, const Entry &b) const { return std::strcmp(a.first, b.first) < 0; }
    };

    static constexpr uint32_t kBuiltinsRva = 0xe7f590; // Entry[kBuiltinCount], in .rdata
    static constexpr size_t kBuiltinCount = 27;

    // 0xb094b0 (__thiscall, returns this): resize to 27, copy the static array, std::sort by name.
    EvalFunctionTable();

    std::vector<Entry> mEntries; // +0x0
};

// The hook on 0xb094b0 (this in ecx: __fastcall with an unused edx).
struct EvalFunctionTableAdapters {
    static EvalFunctionTable *__fastcall Construct(void *memory, void *);
};

static_assert(sizeof(EvalFunctionTable::Entry) == 8, "the element layout");
static_assert(sizeof(EvalFunctionTable) == 0xc, "a std::vector");

// The original's out-of-line instantiations for this element type that the toolchain's code
// generation does not reproduce byte for byte (the game's whole-program optimisation inlined
// differently); each forwards to the same MSVC 14.40 STL template it was compiled from.
// (_Med3_unchecked 0xb08930 and _Pop_heap_hole_by_index 0xb08e30 match the toolchain's code: library.)
struct EvalFunctionTableStl {
    using Entry = EvalFunctionTable::Entry;
    using ByName = EvalFunctionTable::ByName;

    // 0xb09010 (__cdecl): std::_Sort_unchecked(first, last, ideal, pred), introsort.
    static void __cdecl SortUnchecked(Entry *first, Entry *last, ptrdiff_t ideal, ByName pred);
    // 0xb08a30 (__cdecl, pair returned through a hidden pointer): std::_Partition_by_median_guess_unchecked.
    static std::pair<Entry *, Entry *> __cdecl PartitionByMedianGuess(Entry *first, Entry *last, ByName pred);
};

// 0xb08f50 (__thiscall on the vector, ret 8): vector<Entry>::_Resize_reallocate<_Value_init_tag>(newSize, tag),
// the reallocating path of resize(): new capacity = max(capacity * 3 / 2, newSize) (max_size when that
// overflows), the old elements moved over, the new ones zeroed, the old block freed. Called only when
// newSize > capacity(). The member is private in the STL, so this view of the vector does the same
// through reserve() and resize().
struct EvalFunctionTableVector {
    std::vector<EvalFunctionTable::Entry> mVector;
    void ResizeReallocate(size_t newSize, const void *valueInitTag);
};

static_assert(sizeof(EvalFunctionTableVector) == 0xc, "a std::vector");

} // namespace nGZCommandParser

// The built-in names (Entry[27] at kBuiltinsRva), an owned static: an image-free program aliases it to our own table
// (ts2_own_eval_builtins, ExpressionEvaluator.h; standalone/owned_statics.cpp).
TS2_OWNED_STATIC(const nGZCommandParser::EvalFunctionTable::Entry, e7f590)[nGZCommandParser::EvalFunctionTable::kBuiltinCount];
