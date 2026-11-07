#pragma once
// The pieces the cTSPersistSystem difftests (tests/ts_persist_system_*.cpp) each wrote the same way: the seeded mixer and
// its byte answers, the primary object block rebuilt from the seed before each side, the real cRZString-keyed hash tables
// (built by the game's table constructor 0x1d8a50, filled through a try_emplace of the image, walked in list order), the
// game's operator new / delete redirected to ours, and the stand-in sets over the folder listing and cRZFile (both sides:
// the image's and ours, shape::StandIn). What stays in each test: its fakes and their answers, the stand-ins' bodies (what
// they log), its seeds and its comparisons. How to use it: docs/lane/test.md, "Persist-system test world".
//
// Everything is in namespace pworld; a test pulls in what it uses (`using pworld::Mix;` ...) or qualifies it.
#include "code_patch.h"
#include "difftest.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"
#include "ts/DirectoryListing.h"

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace pworld {

// ---- seeded answers ----

// The case's seed and the running call count the answers mix in (both reset by the test before each side).
inline uint32_t gSeed = 0;
inline uint32_t gCalls = 0;

inline uint32_t Mix(uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u + (a << 6) + (a >> 2));
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    return h;
}

// FNV-1a of a text.
inline uint32_t TextHash(const std::string &s)
{
    uint32_t h = 2166136261u;
    for (char c : s) h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
    return h;
}

inline uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

inline std::string Lower(std::string s)
{
    for (char &c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// A byte answer with garbage above it (the callers test al only): 1 unless Mix(gSeed, salt ^ 0x5a5a) % modulus is 0.
inline uint32_t Bit(uint32_t salt, uint32_t modulus = 2)
{
    return (Mix(gSeed, salt) & 0xffffff00u) | (Mix(gSeed, salt ^ 0x5a5a) % modulus != 0 ? 1u : 0u);
}

// ---- the primary object block ----

alignas(16) inline uint8_t gPrimary[0x300];

// Fills the block with bytes from std::mt19937(seed) (the tests' "fill(seed)": the same bytes on both sides).
inline void FillPrimary(uint32_t seed)
{
    std::mt19937 fill(seed);
    for (auto &b : gPrimary) b = static_cast<uint8_t>(fill());
}

// The dword at `offset` of the block.
inline void Put(uint32_t offset, uint32_t value) { std::memcpy(gPrimary + offset, &value, 4); }
inline void Put(uint32_t offset, const void *pointer) { Put(offset, Word(pointer)); }

// ---- the game's cRZString-keyed hash tables (0x20 bytes: +4 the list head node, +8 the size) ----

// A table node as the game lays it out: the list links, the key, the value (0x2c bytes for a dword value).
template <class V> struct TableNode {
    TableNode *next;
    TableNode *prev;
    cRZString key;
    V value;
};
static_assert(sizeof(TableNode<uint32_t>) == 0x2c, "table node");

// Builds an empty table at `table` with the game's constructor 0x1d8a50 (a real one: the function under test hashes, finds
// and inserts through the image's own code). Tables live in the primary block, so the block's fill is overwritten.
inline void BuildTable(difftest::Context &ctx, void *table) { ctx.Original<void(__thiscall *)(void *)>(0x1d8a50)(table); }

// Inserts `key` through the image's try_emplace at `emplaceRva` (0x1d7df0 for the path -> segment maps, 0x1d7390 for
// mPathGroups) and returns the node (new or existing), whose value the caller sets.
template <class V> TableNode<V> *Emplace(difftest::Context &ctx, uint32_t emplaceRva, void *table, const char *key)
{
    cRZString text;
    text.Construct(key);
    struct {
        TableNode<V> *node;
        uint32_t inserted;
    } result{};
    ctx.Original<void *(__thiscall *)(void *, void *, const void *)>(emplaceRva)(table, &result, &text);
    text.Destruct();
    return result.node;
}

// The table's entries in list order, "key=<describe(value)>", then "size <hex>" (heap addresses never appear: compare
// these rather than the table's words, and blank the table's 0x20 bytes in a dump of the block).
template <class V, class Describe> std::vector<std::string> DumpTable(const void *table, Describe describe)
{
    const auto *bytes = static_cast<const uint8_t *>(table);
    std::vector<std::string> out;
    auto *head = *reinterpret_cast<TableNode<V> *const *>(bytes + 4);
    for (auto *n = head->next; n != head; n = n->next) out.push_back(n->key.mString + "=" + describe(n->value));
    char size[24];
    std::snprintf(size, sizeof(size), "size 0x%x", *reinterpret_cast<const uint32_t *>(bytes + 8));
    out.push_back(size);
    return out;
}

// ---- stand-in sets ----

// The game's operator new (0x824632), sized delete (0x824624) and delete[] (0x6c4b50), redirected to ours.
inline void *__cdecl GameNew(size_t n) { return ::operator new(n); }
inline void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
inline void __cdecl GameDeleteArray(void *p) { ::operator delete[](p); }

// A test's patches, removed in reverse order when it ends.
struct Patches {
    std::vector<std::unique_ptr<shape::CodePatch>> image;
    std::vector<std::unique_ptr<shape::StandIn>> both;

    // `stand` over the image's function at `rva` (both sides call the image's).
    template <class Stand> void Image(uint32_t rva, Stand stand) { image.emplace_back(new shape::CodePatch(rva, shape::CodeOf(stand))); }
    // `stand` over the image's function at `rva` and over our replacement of it (a function or a non-virtual member).
    template <class Ours, class Stand> void Both(uint32_t rva, Ours ours, Stand stand) { both.emplace_back(new shape::StandIn(rva, ours, stand)); }

    // The game's operator new, sized delete (and delete[] when `array`) to ours.
    void Heap(bool array = false)
    {
        Image(0x824632, &GameNew);
        Image(0x824624, &GameDelete);
        if (array) Image(0x6c4b50, &GameDeleteArray);
    }

    // The folder listing (0x3c bytes on the caller's stack, thiscall; both sides call the image's): constructor (path,
    // system), first, entry path (out string), kind (4 a folder, 8 a file), next, destructor. A null stand-in leaves that
    // function alone.
    struct ListingStands {
        const void *ctor, *first, *entry, *kind, *next, *dtor;
    };
    void Listing(const ListingStands &l)
    {
        // The listing is ours (DirectoryListing): each stand-in covers the image's method and ours.
        if (l.ctor) Both(0x1f58f0, &DirectoryListingAdapters::Construct, l.ctor);
        if (l.first) Both(0x1f6dd0, &DirectoryListing::First, l.first);
        if (l.entry) Both(0x1f64f0, &DirectoryListing::EntryPath, l.entry);
        if (l.kind) Both(0x1f65c0, &DirectoryListing::EntryKind, l.kind);
        if (l.next) Both(0x1f61e0, &DirectoryListing::Next, l.next);
        if (l.dtor) Both(0x1f6090, &DirectoryListingAdapters::Destruct, l.dtor);
    }

    // cRZFile's members, image and ours (thiscall as __fastcall). A null stand-in leaves that member alone. The constructor
    // stand-in should store the image's cRZFile vtable (cRZFile::kVTable) so that the original's virtual calls reach the
    // image's members (and so these stand-ins).
    struct FileStands {
        const void *construct, *destruct, *open, *length, *read, *write, *close;
    };
    void Files(const FileStands &f)
    {
        using CtorString = cRZFile *(cRZFile::*)(const cIGZString &);
        if (f.construct) Both(0x1c0c07, static_cast<CtorString>(&cRZFile::Construct), f.construct);
        if (f.destruct) Both(0x1c0e6d, &cRZFile::Destruct, f.destruct);
        if (f.open) Both(0x1c285d, &cRZFile::Open, f.open);
        if (f.length) Both(0x1c230b, &cRZFile::Length, f.length);
        if (f.read) Both(0x1c2c6e, &cRZFile::Read, f.read);
        if (f.write) Both(0x1c3bec, &cRZFile::Write, f.write);
        if (f.close) Both(0x1c0faa, &cRZFile::Close, f.close);
    }
};

} // namespace pworld
