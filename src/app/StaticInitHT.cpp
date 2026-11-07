// Owned XC entries of lane HT (src/app/StaticInitHT.h).
#include "app/StaticInitHT.h"

#include "app/ReporterDump.h"
#include "app/StaticInit.h"
#include "game/game.h"
#include "game/vtable.h"
#include "rz/COMDirectors.h"
#include "rz/GeneralAllocator.h"
#include "rz/MiscHelpersA.h"
#include "rz/RZCOMDllDirectorShape.h"
#include "rz/RZBlockPool.h"
#include "rz/StaticGetters.h"
#include "rz/cRZThread.h"

#include <windows.h>

#include <array>
#include <cstring>
#include <iterator>
#include <locale>
#include <memory>
#include <new>
#include <utility>

namespace staticinit {

// ---- The types' constructors and destructors ----

void Record7c::Construct()
{
    first.Construct();
    second.Construct();
}

void Record7c::Destruct()
{
    second.Destruct();
    first.Destruct();
}

MotiveCurve::MotiveCurve()
    : vtable(GameAddress(kVTable)), words{}, baseIndex(-1), face(GameAddress(kFaceVTable)), index(-1)
{
}

MotiveCurve::~MotiveCurve() { VirtualCall<void *(__thiscall *)(void *, uint32_t), kSlotDestruct>(this, 0u); }

void MotiveCurveSet::Construct()
{
    vtable = GameAddress(kBaseVTable);
    ::new (static_cast<void *>(&curves)) std::vector<MotiveCurve>();
    word10 = 0;
    curves.resize(kCurves);
    vtable = GameAddress(kVTable);
}

void MotiveCurveSet::Destruct()
{
    // 0x416c60 stores the array's vtable, then 0x47e2e0 the base's: the second store is what stays.
    vtable = GameAddress(kBaseVTable);
    std::destroy_at(&curves);
}

MapRecord::MapRecord()
{
    std::memset(bytes, 0, sizeof(bytes));
    At<cRZString>(kName)->Construct();
    At<cRZString>(kText)->Construct();
    *At<const void *>(kBuffer) = GameAddress(kBufferVTable);
}

MapRecord::~MapRecord()
{
    *At<const void *>(kBuffer) = GameAddress(kBufferVTable);
    std::destroy_at(At<std::vector<uint8_t>>(kBufferBytes));
    std::destroy_at(At<std::vector<Row>>(kRows));
    At<cRZString>(kText)->Destruct();
    At<cRZString>(kName)->Destruct();
}

ListRecord::ListRecord()
{
    std::memset(bytes, 0, sizeof(bytes));
    reinterpret_cast<cRZString *>(bytes + kName)->Construct();
}

ListRecord::~ListRecord() { reinterpret_cast<cRZString *>(bytes + kName)->Destruct(); }

void ActionLog::Flush()
{
    for (cRZString *line = lines.mFirst; line != lines.mLast; ++line)
        VirtualCall<void(__thiscall *)(void *, const char *), kOutSlotWrite>(out, line->mString.c_str());
    if (lines.mFirst == lines.mLast) return;
    for (cRZString *line = lines.mFirst; line != lines.mLast; ++line)
        VirtualCall<void *(__thiscall *)(void *, uint32_t), cRZStringVector::kSlotDeletingDestructor>(line, 0u);
    lines.mLast = lines.mFirst;
}

void ActionLog::Destruct()
{
    vtable = GameAddress(kVTable);
    if (out) {
        Flush();
        VirtualCall<void(__thiscall *)(void *), kOutSlotClose>(out);
        VirtualCall<void(__thiscall *)(void *), kOutSlotFlush>(out);
        if (out) VirtualCall<void *(__thiscall *)(void *, uint32_t), kOutSlotDelete>(out, 1u);
        out = nullptr;
    }
    flag4 = 0;
    name.Destruct();
    lines.Tidy();
    std::destroy_at(&counts);
}

namespace {

// A vector element of N bytes with a cRZString at +0: its destructor is the string's teardown (0x468fd0, 0x4ac010
// inline it: the vtable stored, the text released).
template <size_t N> struct StringRow {
    alignas(4) uint8_t bytes[N];
    StringRow() = delete;
    ~StringRow() { reinterpret_cast<cRZString *>(bytes)->Destruct(); }
};
using PairVectors = std::vector<std::vector<uint64_t>>;
using PointerVector = std::vector<void *, ProcessHeapAllocator<void *>>;
using CountMap = std::unordered_map<uint32_t, uint32_t>;
using RecordList = std::list<ListRecord>;
static_assert(sizeof(PairVectors) == 12 && sizeof(std::vector<uint64_t>) == 12 && sizeof(PointerVector) == 12);
static_assert(sizeof(RecordList) == 8 && sizeof(CountMap) == 0x20);
static_assert(sizeof(std::locale) == 8, "the classic locale: its implementation pointer at +4, as 0x1b5324 reads it");
static_assert(offsetof(ActionLog, counts) == 0x10 && offsetof(ActionLog, lines) == 0x30 && offsetof(ActionLog, name) == 0x3c &&
              offsetof(ActionLog, out) == 0x5c);

constexpr uint32_t kRecordCount = 401, kCurveSetCount = 14;
constexpr uint32_t kIatFreeLibrary = 0xd9a120;

// The image's import slots (as the original calls them).
template <typename Fn> Fn Import(uint32_t iatRva) { return *GameAddress<Fn>(iatRva); }
constexpr uint32_t kWorkerShutdownFlag = WorkerThreads::kShutdown;

// Slot order. Entry, storage and thunk from db/initializers.jsonl and the code of each (what it stores and calls);
// the tests check each against the image's entry and thunk.
constexpr HTStatic kHT[] = {
    {3, 0x138323, 0x10cb098, HTKind::GeneralAllocator, 0xd90712, 0, 0},
    {8, 0x138385, 0x10cb5bc, HTKind::RZStd, 0, 0, 0},
    {11, 0x13e046, 0x10df6cc, HTKind::ClassicLocale, 0xd950a4, 0, 0},
    {12, 0x14094a, 0x1103054, HTKind::PointerVector, 0xd98a0a, 0, 0},
    {99, 0x138317, 0x10cb000, HTKind::DbgHelpUnload, 0xd90708, 0, 0},
    {100, 0x13830b, 0x10caff4, HTKind::ModuleHandle, 0xd906fe, 0, 0},
    {101, 0x138363, 0xff2464, HTKind::VTableStore, 0xd90726, 0, 0xda3c98},
    {135, 0x138880, 0x10cca58, HTKind::RecordArray, 0xd90e40, kRecordCount, 0},
    {136, 0x1388b0, 0x10d8c94, HTKind::RZStringVector, 0xd90e60, 0, 0},
    {137, 0x1388c0, 0x10d8ca0, HTKind::RZStringVector, 0xd90e70, 0, 0},
    {166, 0x139260, 0x10d9114, HTKind::PairVectors, 0xd918a0, 0, 0},
    {218, 0x13a970, 0x10da808, HTKind::MotiveCurveSets, 0xd92940, kCurveSetCount, 0},
    {220, 0x13aa10, 0x10da9f4, HTKind::Strings40, 0xd92a80, 0, 0},
    {223, 0x13aa30, 0x10dab00, HTKind::RecordMap, 0xd92be0, 0, 0},
    {224, 0x13aad0, 0x10daae8, HTKind::Strings828, 0xd92bf0, 0, 0},
    {233, 0x13b4a0, ActionLog::kStatic, HTKind::ActionLog, 0xd92fd0, 0, 0},
    {502, 0x13e0d0, 0x10e6930, HTKind::ModuleHandle, 0xd9512a, 0, 0},
    {523, 0x13e386, 0x10f4f20, HTKind::ReleaseRefs, 0xd9528b, 0, 0},
    {525, 0x13e3c6, 0x10f4fc0, HTKind::ModuleHandle, 0xd9529a, 0, 0},
    {527, 0x13e405, 0x10f4ff0, HTKind::NamedList, 0xd952ba, 0, 0},
    {529, 0x13e424, kWorkerShutdownFlag, HTKind::WorkerShutdown, 0xd952dc, 0, 0},
    {536, 0x13e475, 0x10f59e8, HTKind::CallbackThread, 0xd953aa, 0, 0},
    {564, 0x13e870, 0x1069ffc, HTKind::VTableStore, 0xd95b80, 0, 0xe687c0},
    {640, 0x13f626, 0x1077cf8, HTKind::VTableStore, 0xd969c4, 0, 0xe81100},
    {766, 0x1408d0, 0x10fff78, HTKind::MadCowDirector, 0xd98a00, 0, 0},
};

// Builds our container and stores the words of it the image's entry stores (the others stay the file's: a list's
// size, a hash's list size). Nodes and buckets point at the head node, never at the container, so the words can be
// stored anywhere; ownership moves to the storage (the local is not destroyed).
template <typename T> void BuildWords(void *storage, uint32_t keptWords)
{
    alignas(T) unsigned char local[sizeof(T)];
    ::new (static_cast<void *>(local)) T();
    uint32_t words[sizeof(T) / 4];
    std::memcpy(words, local, sizeof(T));
    uint32_t *target = static_cast<uint32_t *>(storage);
    for (size_t i = 0; i < std::size(words); ++i) {
        if (!(keptWords >> i & 1)) target[i] = words[i];
    }
}
constexpr uint32_t kListSizeWord = 1u << 1, kHashListSizeWord = 1u << 2;

template <typename T> void Destroy(uint32_t storage) { std::destroy_at(GameAddress<T>(storage)); }

void DestroyHT(const HTStatic &e)
{
    switch (e.kind) {
    case HTKind::GeneralAllocator: GameAddress<GeneralAllocator>(e.storage)->Shutdown(); break;
    case HTKind::ClassicLocale: Destroy<std::locale>(e.storage); break;
    case HTKind::PointerVector: Destroy<PointerVector>(e.storage); break;
    case HTKind::DbgHelpUnload: GameAddress<appbase::DbgHelpTable>(e.storage)->Unload(); break;
    case HTKind::ModuleHandle: {
        auto *module = GameAddress<HMODULE>(e.storage);
        // 0x992516 (slot 525) zeroes the word unconditionally, the others only after FreeLibrary: the same (null in
        // the other branch).
        if (*module) {
            Import<BOOL(WINAPI *)(HMODULE)>(kIatFreeLibrary)(*module);
            *module = nullptr;
        }
        break;
    }
    case HTKind::VTableStore: *GameAddress<const void *>(e.storage) = GameAddress(e.aux); break;
    case HTKind::RecordArray: {
        auto *records = GameAddress<Record7c>(e.storage);
        for (uint32_t i = e.count; i != 0;) records[--i].Destruct();
        break;
    }
    case HTKind::RZStringVector: GameAddress<cRZStringVector>(e.storage)->Tidy(); break;
    case HTKind::PairVectors: Destroy<PairVectors>(e.storage); break;
    case HTKind::MotiveCurveSets: {
        auto *sets = GameAddress<MotiveCurveSet>(e.storage);
        for (uint32_t i = e.count; i != 0;) sets[--i].Destruct();
        break;
    }
    case HTKind::Strings40: Destroy<std::vector<StringRow<40>>>(e.storage); break;
    case HTKind::RecordMap: Destroy<RecordMap>(e.storage); break;
    case HTKind::Strings828: Destroy<std::vector<StringRow<0x828>>>(e.storage); break;
    case HTKind::ActionLog: GameAddress<ActionLog>(e.storage)->Destruct(); break;
    case HTKind::ReleaseRefs: {
        // 0x98742c: each 16-byte entry's reference (+0xc, never null here) released and cleared, then the vector
        // emptied (last = first; nothing freed: slot 522's thunk frees the buffer later).
        auto *span = GameAddress<uint8_t *>(e.storage);
        uint8_t *const last = span[1]; // read once, as the original does
        for (uint8_t *entry = span[0]; entry != last; entry += 16) {
            void **ref = reinterpret_cast<void **>(entry + 0xc);
            VirtualCall<uint32_t(__thiscall *)(void *), 2>(*ref);
            *ref = nullptr;
        }
        if (span[0] != span[1]) span[1] = span[0];
        break;
    }
    case HTKind::NamedList: Destroy<RecordList>(e.storage); break;
    case HTKind::WorkerShutdown:
        *GameAddress<uint8_t>(e.storage) = 1;
        WorkerThreads::Shutdown(-1);
        break;
    case HTKind::CallbackThread: GameAddress<cRZCallbackThread>(e.storage)->Destruct(); break;
    default: break;
    }
}

// atexit takes no argument: one thunk per entry, by its index in the table.
template <size_t I> void __cdecl HTThunk() { DestroyHT(kHT[I]); }

using Thunk = void(__cdecl *)();
template <size_t... I> constexpr std::array<Thunk, sizeof...(I)> HTThunks(std::index_sequence<I...>) { return {&HTThunk<I>...}; }
constexpr auto kHTThunks = HTThunks(std::make_index_sequence<std::size(kHT)>());

constexpr auto kOwned = [] {
    std::array<winmainstart::OwnedInitialiser, std::size(kHT)> out{};
    size_t n = 0;
    for (const auto &e : kHT) out[n++] = {e.slot, e.imageRva, &InitHT, &e};
    return out;
}();

constexpr bool SortedBySlot()
{
    for (size_t i = 1; i < std::size(kHT); ++i) {
        if (kHT[i - 1].slot >= kHT[i].slot) return false;
    }
    return true;
}
static_assert(SortedBySlot(), "lane HT's XC entries: one per slot, in slot order");

} // namespace

size_t HTObjectSize(const HTStatic &e)
{
    switch (e.kind) {
    case HTKind::GeneralAllocator: return sizeof(GeneralAllocator);
    case HTKind::RZStd: return sizeof(cRZStd);
    case HTKind::ClassicLocale: return sizeof(std::locale);
    case HTKind::DbgHelpUnload: return sizeof(appbase::DbgHelpTable);
    case HTKind::ModuleHandle:
    case HTKind::VTableStore: return 4;
    case HTKind::WorkerShutdown: return 1;
    case HTKind::RecordArray: return e.count * sizeof(Record7c);
    case HTKind::MotiveCurveSets: return e.count * sizeof(MotiveCurveSet);
    case HTKind::RecordMap: return sizeof(RecordMap);
    case HTKind::ActionLog: return sizeof(ActionLog);
    case HTKind::NamedList: return sizeof(RecordList);
    case HTKind::CallbackThread: return sizeof(cRZCallbackThread);
    case HTKind::MadCowDirector: return sizeof(rz::cCOMDirectorT<comdir::kDir_MadCow>);
    default: return 12; // the vectors
    }
}

void __cdecl InitHT(const void *htStatic)
{
    const auto &e = *static_cast<const HTStatic *>(htStatic);
    void *storage = GameAddress(e.storage);
    switch (e.kind) {
    case HTKind::GeneralAllocator:
        static_cast<GeneralAllocator *>(storage)->Construct(nullptr, 0, 1, 0, nullptr, nullptr);
        break;
    case HTKind::RZStd: static_cast<cRZStd *>(storage)->Construct(7, 0, 0); break;
    case HTKind::RecordArray:
        for (uint32_t i = 0; i < e.count; ++i) static_cast<Record7c *>(storage)[i].Construct();
        break;
    case HTKind::MotiveCurveSets:
        for (uint32_t i = 0; i < e.count; ++i) static_cast<MotiveCurveSet *>(storage)[i].Construct();
        break;
    case HTKind::RecordMap: BuildWords<RecordMap>(storage, kHashListSizeWord); break;
    case HTKind::ActionLog: {
        auto *log = static_cast<ActionLog *>(storage);
        BuildWords<CountMap>(&log->counts, kHashListSizeWord);
        log->lines.mFirst = log->lines.mLast = log->lines.mEnd = nullptr;
        log->name.Construct();
        break;
    }
    case HTKind::NamedList: BuildWords<RecordList>(storage, kListSizeWord); break;
    case HTKind::CallbackThread: static_cast<cRZCallbackThread *>(storage)->Construct(); break;
    case HTKind::MadCowDirector: {
        // comdir::InitMadCow registers its destructor through gRegisterExitHandler: bound to ours for the call.
        int (*const saved)(void (*)()) = gRegisterExitHandler;
        gRegisterExitHandler = BoundAtExit();
        comdir::InitMadCow();
        gRegisterExitHandler = saved;
        return;
    }
    default: break; // atexit-only: the object is the file's
    }
    if (e.thunk) BoundAtExit()(kHTThunks[&e - kHT]);
}

const HTStatic *HTStatics(size_t *count)
{
    *count = std::size(kHT);
    return kHT;
}

const winmainstart::OwnedInitialiser *OwnedHT(size_t *count)
{
    *count = kOwned.size();
    return kOwned.data();
}

} // namespace staticinit
