#pragma once
// Owned XC entries of the "container", "registration", "computed" and "other" kinds, and value slot 104 (lane HF;
// docs/subsystems/startup.md "Done in lane HF"). Hosted, every object stays in the image's storage, where game code
// reads it; each entry runs in its own slot (the host's skip list) and registers its destructor thunk, when it has
// one, with BoundAtExit(), so the exit list keeps the original's LIFO order. Standalone, each becomes a plain C++
// static of its type (the containers, the arrays) or a constant (the computed values).
//
// The containers are our STL's, with the image's layout (the library is byte-matched: the head-node allocators
// `_Buyheadnode` and the destructors `_Erase_head` / `_Tidy` the image's entries and thunks call are in
// db/library-matches.jsonl). The image's entries write only some words of each container: the element count is
// constant-initialised (zero in the file). Ours builds the container and stores exactly those words.
#include "app/WinMainStart.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

struct _OSVERSIONINFOA; // <windows.h>

namespace staticinit {

// A trivially destructible element of N bytes (the library matches' Blob4<N>): what these containers hold is never
// read here, only allocated and freed.
template <size_t N> struct Blob {
    static_assert(N % 4 == 0);
    uint32_t words[N / 4];
    bool operator==(const Blob &) const = default;
    bool operator<(const Blob &other) const { return words[0] < other.words[0]; }
};
struct BlobHash {
    template <size_t N> size_t operator()(const Blob<N> &b) const { return b.words[0]; }
};

// An element holding a cRZString at `NameOffset` and a key word beside it (`Size` bytes). Its destructor is the
// cRZString teardown 0x1d8450 (appctor::DestroyElement, verified: the string's vtable stored, the buffer freed, the
// string emptied), what the image's node frees run on each element: slot 512's list (0x1d5e50 -> 0x1d8450), slot 526's
// tree (0x998e73 -> 0x9626b2, the same two steps inlined), slot 645's (0x1ba5f7 -> 0x1baba2, the string at +4).
template <uint32_t NameOffset, uint32_t Size> struct NamedRecord {
    static constexpr uint32_t kKeyOffset = NameOffset == 0 ? 0x20 : 0;
    alignas(4) unsigned char bytes[Size];
    NamedRecord(const char *text, uint32_t key); // (tests) cRZString::Construct(text), the key, the rest zero
    NamedRecord(const NamedRecord &) = delete;
    NamedRecord &operator=(const NamedRecord &) = delete;
    ~NamedRecord();
    uint32_t Key() const { return *reinterpret_cast<const uint32_t *>(bytes + kKeyOffset); }
    bool operator==(const NamedRecord &other) const { return Key() == other.Key(); }
    bool operator<(const NamedRecord &other) const { return Key() < other.Key(); }
};
struct NamedRecordHash {
    template <uint32_t O, uint32_t S> size_t operator()(const NamedRecord<O, S> &r) const { return r.Key(); }
};

// Slot 729's element (28 bytes): a std::string and a reference (a cIGZUnknown *, Release'd when not null). Its
// destructor (0xb0d300, run by the list's _Tidy 0xbc5290): the reference released, then the string destroyed.
struct NamedRef {
    std::string name;
    void *ref; // a cIGZUnknown
    NamedRef(const char *text, void *object) : name(text), ref(object) {}
    NamedRef(const NamedRef &) = delete;
    NamedRef &operator=(const NamedRef &) = delete;
    ~NamedRef();
    bool operator==(const NamedRef &other) const { return name == other.name; }
};
struct NamedRefHash {
    size_t operator()(const NamedRef &r) const { return r.name.size(); }
};

// The container types, by the image's library instantiations (node sizes: the head node each entry allocates).
using IntSet = std::set<int>;                               // node 0x14; 0x1c01a2 / 0x5dd3e0, 0x1c024b
using IntIntMap = std::map<int, int>;                       // node 0x18; 0x213c20 / 0x5b4550, 0x5b7d14
using FloatMap12 = std::map<float, Blob<12>>;               // node 0x20; inlined / 0x1d9650
using FloatMap4 = std::map<float, Blob<4>>;                 // node 0x18; inlined / 0x1d1fd0
using NamedSet44 = std::set<NamedRecord<0, 0x2c>>;          // node 0x3c; 0x90ef4d / 0x998de0
using KeyedNameSet36 = std::set<NamedRecord<4, 0x24>>;      // node 0x34; 0x1ba147 / 0x1ba561
using NamedWordHash36 = std::unordered_set<NamedRecord<0, 0x24>, NamedRecordHash>; // list node 0x2c; 0x97df6d / 0x14416d + 0x1d97d0
using NamedRefHash28 = std::unordered_set<NamedRef, NamedRefHash>; // list node 0x24; inlined / 0xbc5150

enum class HFKind : uint8_t {
    Empty,         // the entry is `push ebp; mov ebp, esp; pop ebp; ret` (slots 106, 107): nothing
    IntSet,        // the tree kinds: the head node stored at +0 (count at +4 left), atexit(thunk): ~_Tree
    IntIntMap,
    FloatMap12,
    FloatMap4,
    NamedSet44,
    KeyedNameSet36,
    NamedWordHash36,    // the hash kinds (0x20 bytes), atexit(thunk): ~_Hash. 512's entry calls the constructor (every word);
                   // 729's is inlined and leaves the list's count (+8) to the file
    NamedRefHash28,
    Vector16,      // atexit-only: thunk = a std::vector<Blob<16>>'s destructor (slot 522)
    Vectors84And564, // atexit-only: thunk = vector<Blob<564>> at +0xc destroyed, then vector<Blob<84>> at +0 (slot 231)
    NameTable,     // NameValueTable::Construct(source) (ours, verified), atexit(thunk): the vector's destructor
    StringPairs,   // `count` std::pair<const std::string, std::string> built in order, destroyed last first
    RZStrings,     // `count` cRZString (cRZString::Construct()), destroyed last first (cRZString::Destruct)
    Tangent,       // float target = (float)tan(constant)
    ReciprocalLog, // float target = 1.0f / (float)log(constant)
    CpuFeatures,   // target = CpuFeatureFlags()
    TimerSection,  // the cRZTimer section: {mapping, view} of the 8-byte named mapping "cRZTimer<pid>"
    GuidText,      // 16 bytes = hexparse::ParseGuidText(text) (0x4f8eb0 in the image's entries), no destructor
};

struct HFStatic {
    uint32_t slot;
    uint32_t imageRva; // the image's initialiser
    uint32_t storage;  // the object / target (RVA)
    HFKind kind;
    uint32_t count;    // elements (arrays)
    uint32_t source;   // NameTable: the {name, value} table (RVA)
    double constant;   // Tangent, ReciprocalLog
    uint32_t thunk;    // the image's destructor thunk (RVA), 0 when the entry registers none
    const char *text = nullptr; // GuidText: the text (the image's entry passes the same characters from .rdata)
    uint32_t textRva = 0;       // GuidText: where the image's entry takes it from
};

size_t HFObjectSize(const HFStatic &e); // the bytes the entry and its thunk write from `storage`

// The CPU feature flags of 0xee0d1 (slot 765): 1; 3 when the CPU has SSE (CPUID 1, EDX bit 25) unless the system is
// Windows 95 (platform 1, minor version 0); | 4 with 3DNow! (CPUID 0x80000001, EDX bit 31). The two numbers are what
// GetVersionExA reported (the entry asks it, then calls this).
uint32_t CpuFeatureFlags(uint32_t platformId, uint32_t minorVersion);
// 0xee0d1: the flags as the slot-765 entry stores them: GetVersionExA (an OSVERSIONINFOA, 0x94 bytes), then
// CpuFeatureFlags(platform, minor version). The overload taking the query is the same with another GetVersionExA.
using GetVersionExFn = int(__stdcall *)(::_OSVERSIONINFOA *info);
uint32_t __cdecl DetectCpuFeatures();
uint32_t __cdecl DetectCpuFeatures(GetVersionExFn getVersion);
// 0xee07f: whether CPUID exists: the EFLAGS bits that changed when the ID flag (bit 21) was toggled (0x200000 when it
// can be; EFLAGS restored).
uint32_t __cdecl CpuidAvailable();
// 0xee0a9: CPUID with EAX = leaf, the four registers stored in regs (EAX, EBX, ECX, EDX). The image's version leaves ECX
// as its caller had it, ours sets it to 0: the same for every leaf without sub-leaves, the only ones the flags read
// (0, 1, 0x80000001).
void __cdecl CpuidQuery(uint32_t regs[4], uint32_t leaf);

void __cdecl InitHF(const void *hfStatic);
const HFStatic *HFStatics(size_t *count);
const winmainstart::OwnedInitialiser *OwnedHF(size_t *count);

} // namespace staticinit
