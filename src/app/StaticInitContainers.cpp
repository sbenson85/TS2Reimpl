// Owned XC entries of lane HF (src/app/StaticInitContainers.h): empty containers of our STL, two arrays, atexit-only
// vectors, a name table, four computed floats, the CPU feature flags and the cRZTimer section.
#include "app/StaticInitContainers.h"

#include "app/AppCtor.h"
#include "app/StaticInit.h"
#include "game/game.h"
#include "gz/cIGZUnknown.h"
#include "misc/HexParse.h"
#include "rz/cRZString.h"
#include "std/StdInstances.h"

#include <windows.h>
#include <intrin.h>

#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>
#include <string>
#include <utility>

#pragma comment(lib, "user32.lib") // wsprintfA, as the image's entry 0x1383b4

namespace staticinit {

template <uint32_t NameOffset, uint32_t Size> NamedRecord<NameOffset, Size>::NamedRecord(const char *text, uint32_t key)
{
    std::memset(bytes, 0, Size);
    reinterpret_cast<cRZString *>(bytes + NameOffset)->Construct(text);
    std::memcpy(bytes + kKeyOffset, &key, 4);
}
template <uint32_t NameOffset, uint32_t Size> NamedRecord<NameOffset, Size>::~NamedRecord()
{
    appctor::DestroyElement(nullptr, reinterpret_cast<cRZString *>(bytes + NameOffset));
}
template struct NamedRecord<0, 0x24>;
template struct NamedRecord<0, 0x2c>;
template struct NamedRecord<4, 0x24>;
static_assert(sizeof(NamedRecord<0, 0x24>) == 0x24 && sizeof(NamedRecord<0, 0x2c>) == 0x2c && sizeof(NamedRef) == 0x1c);

NamedRef::~NamedRef()
{
    if (ref) static_cast<cIGZUnknown *>(ref)->Release();
}

namespace {

constexpr HFKind E = HFKind::Empty;

// Slot order. Entry, storage and thunk from db/initializers.jsonl; the kind from the entry's and the thunk's code (what
// they allocate, store and call); the tests check each against the image's entry and thunk.
constexpr HFStatic kHF[] = {
    {104, 0x1383b4, 0x10cb638, HFKind::TimerSection, 0, 0, 0, 0xd9074b},
    {106, 0x138440, 0, E, 0, 0, 0, 0},
    {107, 0x138450, 0, E, 0, 0, 0, 0},
    {109, 0x138455, 0x10cb748, HFKind::StringPairs, 3, 0, 0, 0xd9075f},
    {230, 0x13ab60, 0x10dac2c, HFKind::FloatMap12, 0, 0, 0, 0xd92e00},
    {231, 0x13b5a0, 0x10dacc4, HFKind::Vectors84And564, 0, 0, 0, 0xd930d0},
    {286, 0x13c4b0, 0x10db07c, HFKind::FloatMap4, 0, 0, 0, 0xd945a0},
    {317, 0x13c790, 0x10dbf20, HFKind::GuidText, 0, 0, 0, 0, "13ae91e7-b825-4559-82a3-0ead8e8dd7fd", 0xdd7a98},
    {318, 0x13c7f0, 0x10dbf30, HFKind::GuidText, 0, 0, 0, 0, "c309c93a-2c22-4c85-aa56-b0b6ef2cada0", 0xdd7ac0},
    {319, 0x13c7d0, 0x10dbf40, HFKind::GuidText, 0, 0, 0, 0, "d5f7826a-2667-410d-aa08-4ae890d8fda8", 0xdd7ae8},
    {323, 0x13ca98, 0x10dc2f4, HFKind::GuidText, 0, 0, 0, 0, "00000006-0000-0000-0000-000000000000", 0xddee68},
    {324, 0x13caaa, 0x10dc304, HFKind::GuidText, 0, 0, 0, 0, "00000005-0000-0000-0000-000000000000", 0xdce84c},
    {363, 0x13cfde, 0x10dcdc4, HFKind::Tangent, 0, 0, 0x1.654322p-4, 0}, // the double at 0xde97c0 (5 degrees, a float)
    {478, 0x13dd00, 0x10de980, HFKind::NameTable, 0, 0xdf99d0, 0, 0xd94dd5}, // live 2, buy 3, build 4
    {499, 0x13e09d, 0x10e6894, HFKind::IntIntMap, 0, 0, 0, 0xd950f6},
    {500, 0x13e080, 0x10e689c, HFKind::IntIntMap, 0, 0, 0, 0xd950ea},
    {506, 0x13e1bc, 0x10ecd04, HFKind::IntIntMap, 0, 0, 0, 0xd951a5},
    {512, 0x13e35e, 0x10f4eb8, HFKind::NamedWordHash36, 0, 0, 0, 0xd95277},
    {513, 0x13e293, 0x10f4ed8, HFKind::IntSet, 0, 0, 0, 0xd95223},
    {514, 0x13e324, 0x10f4ee0, HFKind::IntSet, 0, 0, 0, 0xd9525f},
    {515, 0x13e341, 0x10f4ee8, HFKind::IntSet, 0, 0, 0, 0xd9526b},
    {516, 0x13e2b0, 0x10f4ef0, HFKind::IntSet, 0, 0, 0, 0xd9522f},
    {517, 0x13e2ea, 0x10f4ef8, HFKind::IntSet, 0, 0, 0, 0xd95247},
    {518, 0x13e2cd, 0x10f4f00, HFKind::IntSet, 0, 0, 0, 0xd9523b},
    {519, 0x13e307, 0x10f4f08, HFKind::IntSet, 0, 0, 0, 0xd95253},
    {520, 0x13e259, 0x10f4f10, HFKind::IntSet, 0, 0, 0, 0xd9520b},
    {521, 0x13e276, 0x10f4f18, HFKind::IntSet, 0, 0, 0, 0xd95217},
    {522, 0x13e24d, 0x10f4f20, HFKind::Vector16, 0, 0, 0, 0xd95201},
    {524, 0x13e3a0, 0x10f4fbc, HFKind::ReciprocalLog, 0, 0, 2.0, 0}, // the double at 0xda76a0
    {526, 0x13e3d2, 0x10f4fe8, HFKind::NamedSet44, 0, 0, 0, 0xd952a4},
    {534, 0x13e48b, 0x10f59d8, HFKind::IntSet, 0, 0, 0, 0xd953b4},
    {645, 0x13f668, 0x10fae60, HFKind::KeyedNameSet36, 0, 0, 0, 0xd96a15},
    {729, 0x1403f0, 0x10fd3cc, HFKind::NamedRefHash28, 0, 0, 0, 0xd98720},
    {733, 0x140544, 0x10fd558, HFKind::RZStrings, 12, 0, 0, 0xd98828},
    {765, 0xd8e6d1, 0x10ffafc, HFKind::CpuFeatures, 0, 0, 0, 0},
    {798, 0x140ec0, 0x1107e1c, HFKind::Tangent, 0, 0, 0x1.6dd624p-13, 0}, // the double at 0xebba20 (0.01 degrees)
};

// The cRZTimer section (0x10cb638): a per-process named mapping of 8 bytes and its view.
struct TimerSection {
    HANDLE mapping;
    uint32_t *view;
};

using StringPair = std::pair<const std::string, std::string>;
static_assert(sizeof(StringPair) == 0x30 && sizeof(cRZString) == 0x20);
static_assert(sizeof(IntSet) == 8 && sizeof(IntIntMap) == 8 && sizeof(FloatMap12) == 8 && sizeof(NamedSet44) == 8);
static_assert(sizeof(NamedWordHash36) == 0x20 && sizeof(NamedRefHash28) == 0x20 && sizeof(std::vector<Blob<16>>) == 12);

// Builds our container and stores the words of it the image's entry stores: a tree's head node (word 0; the count,
// word 1, stays the file's zero); every word of a hash but its list's count (word 2). The container's nodes and
// bucket array point at its head node, never at the container, so the words can be stored anywhere. Ownership moves
// to the storage: the local copy is not destroyed.
template <typename T> void BuildWords(uint32_t storage, uint32_t keptWords)
{
    alignas(T) unsigned char local[sizeof(T)];
    ::new (static_cast<void *>(local)) T();
    uint32_t words[sizeof(T) / 4];
    std::memcpy(words, local, sizeof(T));
    uint32_t *target = GameAddress<uint32_t>(storage);
    for (size_t i = 0; i < std::size(words); ++i) {
        if (!(keptWords >> i & 1)) target[i] = words[i];
    }
}
constexpr uint32_t kTreeCount = 1u << 1, kHashListCount = 1u << 2;

template <typename T> void Destroy(uint32_t storage) { std::destroy_at(GameAddress<T>(storage)); }

void InitTimerSection(uint32_t storage)
{
    auto *section = GameAddress<TimerSection>(storage);
    char name[32];
    wsprintfA(name, "cRZTimer%08x", GetCurrentProcessId());
    section->mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 8, name);
    if (!section->mapping) {
        section->view = nullptr;
        return;
    }
    const DWORD error = GetLastError();
    section->view = static_cast<uint32_t *>(MapViewOfFile(section->mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));
    if (error != ERROR_ALREADY_EXISTS) {
        // ORIGINAL-BUG: the view is not checked; a failed MapViewOfFile faults here, as in the original.
        section->view[0] = 0;
        section->view[1] = 0;
    }
}

// What the image's thunk does: the object's destructor, here ours.
void DestroyHF(const HFStatic &e)
{
    switch (e.kind) {
    case HFKind::IntSet: Destroy<IntSet>(e.storage); break;
    case HFKind::IntIntMap: Destroy<IntIntMap>(e.storage); break;
    case HFKind::FloatMap12: Destroy<FloatMap12>(e.storage); break;
    case HFKind::FloatMap4: Destroy<FloatMap4>(e.storage); break;
    case HFKind::NamedSet44: Destroy<NamedSet44>(e.storage); break;
    case HFKind::KeyedNameSet36: Destroy<KeyedNameSet36>(e.storage); break;
    case HFKind::NamedWordHash36: Destroy<NamedWordHash36>(e.storage); break;
    case HFKind::NamedRefHash28: Destroy<NamedRefHash28>(e.storage); break;
    case HFKind::Vector16: Destroy<std::vector<Blob<16>>>(e.storage); break;
    case HFKind::Vectors84And564:
        Destroy<std::vector<Blob<564>>>(e.storage + 0xc);
        Destroy<std::vector<Blob<84>>>(e.storage);
        break;
    case HFKind::NameTable:
        // The image's thunk is the vector's _Tidy (0x731ee4): the elements destroyed (_Destroy_range 0x72e80d, the
        // strings), then the buffer freed.
        Destroy<std::vector<NameValueTable::Entry>>(e.storage);
        break;
    case HFKind::StringPairs: {
        auto *pairs = GameAddress<StringPair>(e.storage);
        for (uint32_t i = e.count; i != 0;) std::destroy_at(&pairs[--i]);
        break;
    }
    case HFKind::RZStrings: {
        auto *strings = GameAddress<cRZString>(e.storage);
        for (uint32_t i = e.count; i != 0;) strings[--i].Destruct();
        break;
    }
    case HFKind::TimerSection: {
        auto *section = GameAddress<TimerSection>(e.storage);
        if (section->view) UnmapViewOfFile(section->view);
        if (section->mapping) CloseHandle(section->mapping);
        break;
    }
    default: break;
    }
}

// atexit takes no argument: one thunk per entry, by its index in the table.
template <size_t I> void __cdecl HFThunk() { DestroyHF(kHF[I]); }

using Thunk = void(__cdecl *)();
template <size_t... I> constexpr std::array<Thunk, sizeof...(I)> HFThunks(std::index_sequence<I...>) { return {&HFThunk<I>...}; }
constexpr auto kHFThunks = HFThunks(std::make_index_sequence<std::size(kHF)>());

constexpr auto kOwned = [] {
    std::array<winmainstart::OwnedInitialiser, std::size(kHF)> out{};
    size_t n = 0;
    for (const auto &e : kHF) out[n++] = {e.slot, e.imageRva, &InitHF, &e};
    return out;
}();

constexpr bool SortedBySlot()
{
    for (size_t i = 1; i < std::size(kHF); ++i) {
        if (kHF[i - 1].slot >= kHF[i].slot) return false;
    }
    return true;
}
static_assert(SortedBySlot(), "lane HF's XC entries: one per slot, in slot order");

} // namespace

size_t HFObjectSize(const HFStatic &e)
{
    switch (e.kind) {
    case HFKind::Empty: return 0;
    case HFKind::NamedWordHash36:
    case HFKind::NamedRefHash28: return 0x20;
    case HFKind::Vector16:
    case HFKind::NameTable: return 12;
    case HFKind::Vectors84And564: return 24;
    case HFKind::GuidText: return 16;
    case HFKind::StringPairs: return e.count * sizeof(StringPair);
    case HFKind::RZStrings: return e.count * sizeof(cRZString);
    case HFKind::Tangent:
    case HFKind::ReciprocalLog:
    case HFKind::CpuFeatures: return 4;
    default: return 8; // the trees, the timer section
    }
}

// CpuFeatureFlags and the CPU detection functions: src/app/StaticInitCpu.size.cpp (size-built, as the image's).

void __cdecl InitHF(const void *hfStatic)
{
    const auto &e = *static_cast<const HFStatic *>(hfStatic);
    switch (e.kind) {
    case HFKind::Empty: return;
    case HFKind::IntSet: BuildWords<IntSet>(e.storage, kTreeCount); break;
    case HFKind::IntIntMap: BuildWords<IntIntMap>(e.storage, kTreeCount); break;
    case HFKind::FloatMap12: BuildWords<FloatMap12>(e.storage, kTreeCount); break;
    case HFKind::FloatMap4: BuildWords<FloatMap4>(e.storage, kTreeCount); break;
    case HFKind::NamedSet44: BuildWords<NamedSet44>(e.storage, kTreeCount); break;
    case HFKind::KeyedNameSet36: BuildWords<KeyedNameSet36>(e.storage, kTreeCount); break;
    case HFKind::NamedWordHash36: BuildWords<NamedWordHash36>(e.storage, 0); break; // its entry calls the list's constructor (0x5d3547): the count too
    case HFKind::NamedRefHash28: BuildWords<NamedRefHash28>(e.storage, kHashListCount); break;
    case HFKind::Vector16:
    case HFKind::Vectors84And564: break; // constant-initialised (zero in the file): only the destructor
    case HFKind::NameTable:
        GameAddress<NameValueTable>(e.storage)->Construct(GameAddress<const NameValueEntry>(e.source));
        break;
    case HFKind::StringPairs: {
        auto *pairs = GameAddress<StringPair>(e.storage);
        for (uint32_t i = 0; i < e.count; ++i) ::new (static_cast<void *>(&pairs[i])) StringPair();
        break;
    }
    case HFKind::RZStrings: {
        auto *strings = GameAddress<cRZString>(e.storage);
        for (uint32_t i = 0; i < e.count; ++i) strings[i].Construct();
        break;
    }
    case HFKind::Tangent: *GameAddress<float>(e.storage) = static_cast<float>(std::tan(e.constant)); return;
    case HFKind::ReciprocalLog: *GameAddress<float>(e.storage) = 1.0f / static_cast<float>(std::log(e.constant)); return;
    case HFKind::CpuFeatures: *GameAddress<uint32_t>(e.storage) = DetectCpuFeatures(); return;
    case HFKind::TimerSection: InitTimerSection(e.storage); break;
    case HFKind::GuidText: hexparse::ParseGuidText(GameAddress(e.storage), e.text); return;
    }
    if (e.thunk) BoundAtExit()(kHFThunks[&e - kHF]);
}

const HFStatic *HFStatics(size_t *count)
{
    *count = std::size(kHF);
    return kHF;
}

const winmainstart::OwnedInitialiser *OwnedHF(size_t *count)
{
    *count = kOwned.size();
    return kOwned.data();
}

} // namespace staticinit
