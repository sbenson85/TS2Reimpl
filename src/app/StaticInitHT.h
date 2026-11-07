#pragma once
// The last image-run XC entries but the CRT's (lane HT; docs/subsystems/startup.md "Done in lane HT"): what lanes
// HC, HD and HF left because a destructor or constructor was not ours, and a few the kind tables missed. Hosted, every
// object stays in the image's storage (game code reads it there); each entry runs in its own slot (the host's skip
// list) and registers its thunk, when the image's entry registers one, with BoundAtExit(), so the exit list keeps the
// original's LIFO order. Standalone each becomes a plain C++ static of its type.
#include "app/WinMainStart.h"
#include "rz/cRZCmdLine.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <unordered_map>
#include <vector>

namespace staticinit {

enum class HTKind : uint8_t {
    GeneralAllocator, // 3: GeneralAllocator::Construct(no core, 0, can free, ...); thunk: Shutdown
    RZStd,            // 8: cRZStd::Construct(7, 0, 0); no thunk
    ClassicLocale,    // 11: atexit-only; thunk: std::locale's destructor (the STL's classic locale, 0x1b5324)
    PointerVector,    // 12: atexit-only; thunk: a vector<void *> on the process heap destroyed (0xc6c66a)
    DbgHelpUnload,    // 99: atexit-only; thunk: DbgHelpTable::Unload (0x1bd611)
    ModuleHandle,     // 100, 502, 525: atexit-only; thunk: FreeLibrary of the cached module at `storage` when set,
                      // the handle zeroed after
    VTableStore,      // 101, 564, 640: atexit-only; thunk: the inlined destructor, `aux` (a vtable RVA) stored in the object
    RecordArray,      // 135: `count` Record7c built in order; thunk: destroyed last first
    RZStringVector,   // 136, 137: atexit-only; thunk: vector<cRZString>::_Tidy (0x1441eb)
    PairVectors,      // 166: atexit-only; thunk: a vector of vectors of 8-byte elements destroyed
    MotiveCurveSets,  // 218: `count` MotiveCurveSet (16 curves each) built in order; thunk: destroyed last first
    Strings40,        // 220: atexit-only; thunk: a vector of 40-byte {cRZString, 8 bytes} destroyed
    RecordMap,        // 223: an unordered_map<uint32_t, MapRecord> (the head node and buckets); thunk: ~_Hash
    Strings828,       // 224: atexit-only; thunk: a vector of 0x828-byte {cRZString, ...} destroyed
    ActionLog,        // 233: SimDoNodeActionLog's members (hash, vector, string); thunk: its destructor
    ReleaseRefs,      // 523: atexit-only; thunk: each 16-byte entry's reference (+0xc) released, the vector emptied
    NamedList,        // 527: a std::list<ListRecord> (its head node); thunk: ~list
    WorkerShutdown,   // 529: atexit-only; thunk: the shutdown flag set, WorkerThreads::Shutdown(-1)
    CallbackThread,   // 536: cRZCallbackThread::Construct; thunk: Destruct
    MadCowDirector,   // 766: the MadCow COM director (comdir::InitMadCow, its thunk registered with BoundAtExit)
};

struct HTStatic {
    uint32_t slot;
    uint32_t imageRva; // the image's initialiser
    uint32_t storage;  // the object (RVA)
    HTKind kind;
    uint32_t thunk;    // the image's exit thunk (RVA), 0 when the entry registers none
    uint32_t count;    // elements (arrays)
    uint32_t aux;      // VTableStore: the vtable's RVA
};

// ---- The element and object types (layouts from the image's constructors and destructors) ----

// Slot 135's records (0x7c bytes, 401 of them at 0x10cca58: the pool slot 134's RecordPoolReset zeroes first): two
// cRZStrings and 0x3c bytes neither the constructor (0x26bc20) nor the destructor (0x257860) touches.
struct Record7c {
    cRZString first;  // +0x00
    cRZString second; // +0x20
    uint8_t rest[0x3c];
    void Construct();  // 0x26bc20: both strings constructed empty (cRZString::Construct())
    void Destruct();   // 0x257860: the second string's teardown, then the first's
};
static_assert(sizeof(Record7c) == 0x7c);

// Slot 218's motive curves (cTSMotiveCurve, 0x2c bytes): built by the default constructor the vector's growth runs
// (0x47df80: the base 0x54b890, then the two faces' vtables and -1 at +0x28); destroyed through their own vtable
// (slot 14, the scalar deleting destructor, with 0), as the set's destructor (0x47e260) does.
struct MotiveCurve {
    static constexpr uint32_t kBaseVTable = 0xdca4a0;      // the base, stored by 0x54b890
    static constexpr uint32_t kVTable = 0xdca504;          // cTSMotiveCurve
    static constexpr uint32_t kInterfaceVTable = 0xdca4e4; // cITSMotiveCurve at +0x24 (overwritten at once)
    static constexpr uint32_t kFaceVTable = 0xdca548;      // cTSMotiveCurve's cITSMotiveCurve face at +0x24
    static constexpr int kSlotDestruct = 14;
    const void *vtable;    // +0x00
    uint32_t words[7];     // +0x04 zero
    int32_t baseIndex;     // +0x20 -1
    const void *face;      // +0x24
    int32_t index;         // +0x28 -1
    MotiveCurve();
    ~MotiveCurve();
};
static_assert(sizeof(MotiveCurve) == 0x2c);

// Slot 218's sets (cTSMotiveCurveArray<16>, 0x14 bytes, 14 of them at 0x10da808): constructor 0x436430 (the base
// cTSMotiveCurveSet 0x47e000: its vtable, an empty vector of curves resized to 16 (0x47eb80), a zero word; then the
// array's vtable), destructor 0x416c60 (the array's vtable, then the base's 0x47e2e0: its vtable and the vector
// destroyed, 0x47e260).
struct MotiveCurveSet {
    static constexpr uint32_t kBaseVTable = 0xdca568; // cTSMotiveCurveSet
    static constexpr uint32_t kVTable = 0xdc3adc;     // cTSMotiveCurveArray<16>
    static constexpr uint32_t kCurves = 16;
    const void *vtable;               // +0x00
    std::vector<MotiveCurve> curves;  // +0x04
    uint32_t word10;                  // +0x10
    void Construct();
    void Destruct();
};
static_assert(sizeof(MotiveCurveSet) == 0x14);

// Slot 223's mapped value (0x7c bytes, under a uint32_t key). Its members, at:
//   +0x04, +0x28 a cRZString; +0x48 a vector of 20-byte rows (freed by 0x2220c8); +0x60 an object of 0x1c bytes with
//   a vtable (0xdcc124) and a byte vector at +0x10 (its destructor 0x4acfe0 stores the vtable, frees the vector);
//   the other words plain.
// The destructor (0x4ad0f0) tears them down last first. Kept as bytes: the members are torn down explicitly, in that
// order, as the image's code does (cRZString has no destructor of its own here).
struct alignas(4) MapRecord {
    static constexpr uint32_t kBufferVTable = 0xdcc124;
    static constexpr uint32_t kName = 0x04, kText = 0x28, kRows = 0x48, kBuffer = 0x60, kBufferBytes = 0x70;
    struct Row {
        uint32_t words[5];
    };
    uint8_t bytes[0x7c];
    MapRecord();                  // (tests) every word zero, the strings constructed, the buffer's vtable
    MapRecord(const MapRecord &) = delete;
    MapRecord &operator=(const MapRecord &) = delete;
    ~MapRecord();
    template <typename T> T *At(uint32_t offset) { return reinterpret_cast<T *>(bytes + offset); }
};
static_assert(sizeof(MapRecord) == 0x7c);
using RecordMap = std::unordered_map<uint32_t, MapRecord>;
static_assert(sizeof(RecordMap) == 0x20);

// Slot 527's list elements (0x38 bytes; nodes of 0x40): a cRZString at +8, torn down by 0x999001 (flags 0).
struct alignas(4) ListRecord {
    static constexpr uint32_t kName = 0x08;
    uint8_t bytes[0x38];
    ListRecord(); // (tests) zero words, the string constructed
    ListRecord(const ListRecord &) = delete;
    ListRecord &operator=(const ListRecord &) = delete;
    ~ListRecord();
};
static_assert(sizeof(ListRecord) == 0x38);

// SimDoNodeActionLog (the static at 0x1014a18, 0x60 bytes; vtable 0xdd047c, 15 slots still the image's). The XC entry
// builds only the hash, the vector and the string (the rest is constant-initialised); the exit thunk (0xd92fd0) is the
// destructor.
struct ActionLog {
    static constexpr uint32_t kVTable = 0xdd047c;
    static constexpr uint32_t kStatic = 0x1014a18;
    // The output's slots (an object the log writes to, held at +0x5c).
    static constexpr int kOutSlotClose = 3, kOutSlotWrite = 14, kOutSlotDelete = 21, kOutSlotFlush = 23;
    const void *vtable;                                 // +0x00
    uint8_t flag4;                                      // +0x04 (cleared by the destructor)
    uint8_t bytes5[11];                                 // +0x05
    std::unordered_map<uint32_t, uint32_t> counts;      // +0x10
    cRZStringVector lines;                              // +0x30
    cRZString name;                                     // +0x3c
    void *out;                                          // +0x5c
    // 0x526c30 (thiscall): each line written to the output (its slot 14, the text), then each destroyed in place (the
    // string's own slot 33, flags 0) and the vector emptied (nothing freed).
    void Flush();
    void Destruct(); // the thunk 0xd92fd0
};
static_assert(sizeof(ActionLog) == 0x60);

// The three worker threads at 0x10f4fd0 (cRZThread *, the deleting destructor is their slot 13), their suspended flags
// at 0x10f4fdc, the shutdown flag 0x10f4fdf, three busy bytes at 0x10f4fe0 and the lock at 0x10f4ff8.
struct WorkerThreads {
    static constexpr uint32_t kThreads = 0x10f4fd0, kSuspended = 0x10f4fdc, kShutdown = 0x10f4fdf, kBusy = 0x10f4fe0;
    static constexpr uint32_t kLock = 0x10f4ff8;
    static constexpr uint32_t kSleepMicroseconds = 250000;
    // 0x99936e (cdecl): until no thread is left (or `seconds` from now have passed, unless -1), under the lock: each
    // suspended thread resumed, each one no longer running deleted; then a quarter second's sleep (0x1b9f95). The busy
    // bytes are 1 meanwhile. Returns the number deleted.
    static uint32_t __cdecl Shutdown(int32_t seconds);
};

void __cdecl InitHT(const void *htStatic);
const HTStatic *HTStatics(size_t *count);
size_t HTObjectSize(const HTStatic &e); // the bytes the entry and its thunk write from `storage`
const winmainstart::OwnedInitialiser *OwnedHT(size_t *count);

} // namespace staticinit
