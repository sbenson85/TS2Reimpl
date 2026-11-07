// Differential test for cTSContentRegistry (lane KT, src/ts/cTSContentRegistry.h): the class in the standalone shape.
//
// ContentRegistry_Run: a registry built by the original factory (0xb57de0) or constructor (0xb56dc0, on 0xc5 memory)
// against one built by ours (Create, or placement new), each driven through its own vtables by the same random sequence
// of operations, then destroyed by the last Release (the deleting destructor 0xb57290 behind the +4 thunk 0xb57282).
// Operations: Init (0xb58b40, through the primary face or the cITSContentRegistry thunk 0xb58cd2), Shutdown (0xb5a300,
// thunk 0xb5a32d), QueryInterface (0xb59820; thunks 0xb5985f, 0xb59867) / AddRef / Release on every face, the service accessors,
// UpdateCrc (0xb57c10), SaveAll (0xb59d60), SaveEntry (0xb59e70), ComputeKey (0xb57770), GetVersion (0xb5aad0),
// GetProperty (0xb5a0d0), EntryCount (0xb59800), GetEntryVersion (0xb58410), HasKey (0xb58e70), FindEntryForResource
// (0xb58460), FindEntryForPath (0xb58540), FindKey (0xb588d0), IsPathRegistered (0xb58e90), Rebuild (0xb57480),
// RebuildEntry (0xb5a800), RegisterPath (0xb5a3b0), RegisterSegment (0xb5a6f0), UnregisterSegment (0xb5a750),
// RemoveSegment (0xb57cc0), UnregisterKey (0xb57d80), GetPathForKey (0xb58950), WriteProperties (0xb5ab10) and
// ReadProperties (0xb59870). Through them: the CRC table 0xb58ce0, Normalize 0xb57ac0, LowerCase 0xb57b90, MakePortable
// 0xb58170, ExpandPortable 0xb57e50, Load 0xb58f80, ReadHeader 0xb591e0, ReadKeys 0xb592c0, WriteHeader 0xb59d90,
// WriteKeys 0xb59fc0, Clear 0xb59c40, AddEntry 0xb573b0, AddSegmentKey 0xb59680, the service primer 0xb57380.
//
// Every object the registry reaches is a recording fake (tests/ts_services_fixture.h): the framework (GetServiceObject's
// cell 0x10c9378 and the director's framework: GetSystemService, GetClassObject of the packed-file segment), the resource
// manager, the packages (segments) and their records (header, keys, properties; the serial face reads a scripted stream
// with counts at the loop boundaries: 0, 1, 2, 3, 5). Stood in for on both sides: CommonImplSlot4 (0x1d0c00: the user
// data directory, the data directories by option bit, the registry files), cRZFile::PathExists (0x1c1242), cRZFileName's
// Init / GetDrivePath (0x1c8bb8 / 0x1c87c7: the real ones reach GetCurrentDirectoryW) and the text getter 0xaeb800; on the
// image the file attribute setter 0x1c3505 and the file deleter 0x1c2e25 (game code ours calls). The option mask cell
// 0x10faf94 is set per seed (HighestOptionBit 0xb39470 / HasOptionBits 0xb39490 read it), and the length cell 0x10fc484
// (13; the game's static initialiser 0x13fff0) set. Compared after every operation: the result (the low byte where the
// original returns a bool in al), the outputs, the event log (strings by content), every field (strings by content and
// capacity, the entries with their key maps (list order, buckets, mask), the name map in order, the data directories;
// the never-written +0x21..+0x23 too when built on 0xc5 memory), every fake's count, and the stack balance of every
// slot call (a declaration popping other than the original's `ret N` shows up).
//
// Not compared: the entries' +1..+3 (the default and copy constructors 0xb56ec0 / 0xb56b50 write only the byte: heap
// leftovers); UnregisterKey's eax (no result: al is left over at 0xb57ddd). Not driven: a package with no resources in
// ComputeKey (the original byte-swaps an unwritten stack state, 0xb57770: ORIGINAL-BUG), the slots that read entry 0
// unchecked while there are no entries (FindKey 0xb588d0 and its callers, RegisterPath 0xb5a3b0, AddSegmentKey 0xb59680)
// and those that use the resource manager unchecked while there is none (Rebuild 0xb57480, RebuildEntry 0xb5a800,
// RemoveSegment 0xb57cc0).
#include "code_patch.h"
#include "difftest.h"
#include "rz/PathFileOps.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "app/AppInit.h"
#include "game/game.h"
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator
#include "gz/cGZPersistResourceManager.h" // cGZPersistResourceKey
#include "rz/StaticGetters2.h"            // Dispatch8
#include "rz/cRZFile.h"
#include "rz/cRZFileName.h"
#include "rz/cRZString.h"
#include "ts/cTSContentRegistry.h"

#include <cstring>
#include <map>
#include <new>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace ctr_test {

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Sys = cTSContentRegistry;

constexpr uint32_t kTag = 0x7d000000; // the registry under test

enum : unsigned { kService = 0, kManager, kPrime, kRegFile, kSeg0, kSegCount = 4, kRecord = kSeg0 + kSegCount, kSerial, kPath, kOut };

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }
uint32_t Bool() { return 0x100u | Draw(2); } // a bool in al with a busy upper byte

uint32_t HandOut(uint32_t out, Obj &obj)
{
    ++obj.refs;
    *reinterpret_cast<void **>(uintptr_t(out)) = &obj;
    return 1;
}
void LogText(const std::string &text)
{
    Ev(uint32_t(text.size()));
    for (char c : text) Ev(uint8_t(c));
}
const cRZString *Str(uint32_t at) { return reinterpret_cast<const cRZString *>(uintptr_t(at)); }
void LogString(uint32_t at) { LogText(Str(at)->mString); }
// A pointer argument (a fake, the registry, the stack, a heap block), or an out pointer left at the 0x5e fill.
uint32_t NormPtr(uint32_t p) { return svcfx::NormOut(p); }
void Call(Obj *self, unsigned slot)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(slot);
}

// ---- the world for a seed ----
const char *const kUserDirs[] = {"C:\\Users\\Sim\\EA\\TS2\\", "C:\\Users\\Sim\\EA\\TS2", "", "c:/USERS/sim/ea/ts2/"};
const char *const kDataDirs[] = {"D:\\Games\\TS2\\TSData\\", "D:\\Games\\TS2 EP1\\TSData", "", "D:\\Games\\TS2\\",
                                 "c:/users/sim/ea/ts2/sub/"};
const char *const kRegistryFiles[] = {"C:\\Users\\Sim\\EA\\TS2\\ContentRegistry", "", "c:/users/sim/ea/ts2/Other Registry File"};
const char *const kPaths[] = {
    "%UserDataDir%/downloads/a.package",
    "%userdatadir%/x",
    "%GameDataDir000%/res/b.package",
    "%GameDataDir001%",
    "%GameDataDir031%/d",
    "%GameDataDir1%/e",
    "C:\\Users\\Sim\\EA\\TS2\\Downloads\\A.package",
    "c:/users/sim/ea/ts2/downloads/b.package",
    "D:\\Games\\TS2\\TSData\\Res\\Objects\\objects.package",
    "d:/games/ts2 ep1/tsdata/res/x.package",
    "short",
    "",
    "no percent but long enough to be heap allocated .package",
    "C:\\Users\\Sim\\EA\\TS2",
    "\xc4\xd6\\DIR\\File.PACKAGE",
    "50%",
    "%lone percent at the start", // GetPathForKey's '%' search from position 0
};
constexpr uint32_t kPathCount = sizeof(kPaths) / sizeof(kPaths[0]);
const char *const kNames[] = {"crc", "CRC", "version", "VERSION", "Title", "", "crc "};
const char *const kValues[] = {"1.0", "", "0", "Some Long Value Text Over Fifteen"};
const char *const kHexes[] = {"0123456789abcdefFEDCBA9876543210", "12", "", "zzzz0001", "ffffffffffffffffffffffffffffffffff",
                              "00000001000000020000000300000004", "123456789"};
const char *const kTexts[] = {"", "Name", "crc", "a value with more than fifteen characters"};
const uint32_t kCounts[] = {0, 1, 2, 3, 5};
const uint32_t kSizes[] = {0, 1, 5, 0x7ff, 0x800, 0x801, 0x1400};
const uint32_t kMasks[] = {0x1001, 0x1003, 0x1007, 0x101f, 0x1000, 0x80001001, 0x1023, 0x100f};

struct World {
    bool framework;       // GetServiceObject answers
    bool manager;         // the persist system hands out the resource manager
    bool prime;           // the service 0x2a4915ea is there
    bool segments;        // the director's framework makes segments
    uint32_t mask;        // the option mask (0x10faf94)
    uint32_t have;        // the bits whose data directory the app has
    const char *userDir;
    const char *dirs[32];
    const char *registryFile;
    uint32_t segPath[kSegCount + 1];
    uint32_t segmentCount; // the manager's
};
World gWorld;

// The record being read: 1 header, 2 keys, 3 properties (the key's group).
uint32_t gReading, gField, gStrings;
bool gLastCrc;
std::string gFileName; // the text cRZFileName::Init was given

// ---- stand-ins ----

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
void __cdecl GameFree(void *p) { ::operator delete(p); }

uint32_t BitOf(uint32_t mask)
{
    for (uint32_t i = 0; i < 32; ++i)
        if (mask == 1u << i) return i;
    return 99;
}
uint32_t __cdecl StandCommonSlot4(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    Ev(0x1d0c00);
    Ev(a);
    Ev(NormPtr(b));
    Ev(c);
    Ev(d);
    Ev(e);
    auto *const out = reinterpret_cast<cRZString *>(uintptr_t(b));
    uint32_t result = gRng();
    switch (a) {
    case 0x2bcfce83:
        out->mString.assign(gWorld.userDir);
        result = Bool();
        break;
    case 0x4bcfce5e: {
        const uint32_t bit = BitOf(e);
        const bool have = bit < 32 && (gWorld.have >> bit & 1);
        if (have) out->mString.assign(gWorld.dirs[bit]);
        result = 0x100u | uint32_t(have);
        break;
    }
    case 0xedb70568:
    case 0xedb70c55:
        out->mString.assign(gWorld.registryFile);
        result = Bool();
        break;
    default: break;
    }
    LogString(b);
    return result;
}
bool __cdecl StandPathExists(const cIGZString &path)
{
    Ev(0x1c1242);
    LogString(P(&path));
    return Draw(3) != 0;
}
uint32_t __cdecl StandAttributes(uint32_t path, uint32_t a, uint32_t b)
{
    Ev(0x1c3505);
    LogString(path);
    Ev(a);
    Ev(b);
    return Bool();
}
uint32_t __cdecl StandRemoveFile(uint32_t path)
{
    Ev(0x1c2e25);
    LogString(path);
    return Bool();
}
uint32_t __fastcall StandFileNameInit(cRZFileName *, void *, const cRZString *path)
{
    Ev(0x1c8bb8);
    LogText(path->mString);
    gFileName = path->mString;
    return 1;
}
// The drive and directory: the text up to its last separator (all of it without one).
cRZString *__fastcall StandDrivePath(cRZFileName *, void *, cRZString *out)
{
    Ev(0x1c87c7);
    const size_t last = gFileName.find_last_of("\\/");
    out->Construct(last == std::string::npos ? gFileName.c_str() : gFileName.substr(0, last + 1).c_str());
    return out;
}
uint32_t __fastcall StandDispatch8(void *self)
{
    const uint32_t word = *static_cast<const uint32_t *>(self);
    Ev(0xaeb800);
    Ev(word);
    return P(kTexts[word % 4]);
}

// ---- fake handlers ----

uint32_t GetSystemService(Obj *, uint32_t *a)
{
    if (a[0] == 0x056b906e && a[1] == 0x656b8efc && gWorld.manager) return HandOut(a[2], F(kManager));
    if (a[0] == 0x2a4915ea && a[1] == 0x4a490e00 && gWorld.prime) return HandOut(a[2], F(kPrime));
    return 0;
}
uint32_t GetClassObject(Obj *, uint32_t *a)
{
    if (a[0] != 0x086d4a89 || a[1] != 0x656b8f0c || !gWorld.segments) return 0x200;
    return HandOut(a[2], F(kRegFile));
}
uint32_t SerialOf(Obj *, uint32_t *a) { return HandOut(a[1], F(kSerial)); }
uint32_t BoolResult(Obj *, uint32_t *) { return Bool(); }

void LogKey(uint32_t at)
{
    const auto *words = reinterpret_cast<const uint32_t *>(uintptr_t(at));
    for (int i = 0; i < 4; ++i) Ev(words[i]);
}
uint32_t KeyWord() { return Draw(4) == 0 ? 0x80000000u | Draw(2) : Draw(2); }

// The segments: slot 11 GetPath, 12 SetPath, 19 GetResourceKeyList, 21 GetRecordSize, 22 OpenRecord, 27 ReadRecord.
uint32_t SegmentIndex(const Obj *self) { return self == &F(kRegFile) ? kSegCount : self->id - 200 - kSeg0; }
void __fastcall SegGetPath(Obj *self, void *, uint32_t out)
{
    Call(self, 11);
    Ev(NormPtr(out));
    reinterpret_cast<cRZString *>(uintptr_t(out))->mString.assign(kPaths[gWorld.segPath[SegmentIndex(self)]]);
}
uint32_t __fastcall SegSetPath(Obj *self, void *, uint32_t path)
{
    Call(self, 12);
    LogString(path);
    return Bool();
}
using KeyList = std::vector<cGZPersistResourceKey, GZServiceAllocator<cGZPersistResourceKey>>;
uint32_t __fastcall SegKeyList(Obj *self, void *, uint32_t list, uint32_t filter)
{
    Call(self, 19);
    Ev(filter);
    auto *const keys = reinterpret_cast<KeyList *>(uintptr_t(list));
    Ev(uint32_t(keys->size()));
    // Never none (ORIGINAL-BUG in 0xb57770). The sort 0xb56600 insertion-sorts up to 32 keys (inline) and partitions
    // (0xb55eb0, its median 0xb55e50) above: 32 / 33 are its boundary. Keys are distinct (a package has each once), so
    // the unstable sort's order is defined.
    static const uint32_t kKeyCounts[] = {1, 2, 3, 4, 5, 31, 32, 33, 34, 120};
    const uint32_t n = kKeyCounts[Draw(4) == 0 ? 5 + Draw(5) : Draw(5)];
    const uint32_t first = Draw(64);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t k = (first + i * 37) % 128; // distinct for n <= 128, in shuffled order
        keys->push_back({k % 3 + (k & 64 ? 0x80000000u : 0), (k >> 2) % 2, k, (k >> 3) % 2});
    }
    return n;
}
uint32_t __fastcall SegRecordSize(Obj *self, void *, uint32_t key)
{
    Call(self, 21);
    LogKey(key);
    return kSizes[Draw(7)];
}
uint32_t __fastcall SegOpenRecord(Obj *self, void *, uint32_t key, uint32_t out, uint32_t mode)
{
    Call(self, 22);
    LogKey(key);
    Ev(svcfx::Norm(out));
    Ev(mode);
    if (Draw(6) == 0) return 0x100;
    gReading = reinterpret_cast<const uint32_t *>(uintptr_t(key))[1];
    gField = gStrings = 0;
    gLastCrc = false;
    HandOut(out, F(kRecord));
    return 0x101;
}
uint32_t __fastcall SegReadRecord(Obj *self, void *, uint32_t key, uint32_t dst, uint32_t sizeAt)
{
    Call(self, 27);
    LogKey(key);
    auto *const size = reinterpret_cast<uint32_t *>(uintptr_t(sizeAt));
    Ev(*size);
    if (Draw(4) == 0) *size /= 2;
    auto *const bytes = reinterpret_cast<uint8_t *>(uintptr_t(dst));
    for (uint32_t i = 0; i < *size; ++i) bytes[i] = uint8_t(gRng());
    return Bool();
}

// The serial record: 11 GetFieldUint16, 13 GetFieldUint32, 24 GetFieldString, 48 SetFieldString.
uint32_t __fastcall GetU16(Obj *self, void *, uint32_t dst)
{
    Call(self, 11);
    const uint16_t v = uint16_t(gRng());
    Ev(v);
    *reinterpret_cast<uint16_t *>(uintptr_t(dst)) = v;
    return Bool();
}
uint32_t __fastcall GetU32(Obj *self, void *, uint32_t dst)
{
    Call(self, 13);
    uint32_t v = gRng();
    switch (gReading) {
    case 1: if (gField == 1) v = Draw(3) == 0 ? 0 : 0x7fa7dc7c + Draw(2); break;
    case 2: if (gField == 1) v = kCounts[Draw(5)]; else if (gField > 1) v = KeyWord(); break;
    case 3: if (gField == 0) v = kCounts[Draw(5)]; break;
    default: break;
    }
    ++gField;
    Ev(v);
    *reinterpret_cast<uint32_t *>(uintptr_t(dst)) = v;
    return Bool();
}
uint32_t __fastcall GetText(Obj *self, void *, uint32_t dst)
{
    Call(self, 24);
    Ev(NormPtr(dst));
    const char *text;
    if (gReading == 3) {
        if (gStrings % 2 == 0) {
            text = kNames[Draw(7)];
            gLastCrc = _stricmp(text, "crc") == 0;
        } else {
            text = gLastCrc ? kHexes[Draw(7)] : kValues[Draw(4)];
        }
    } else {
        text = kPaths[Draw(kPathCount)];
    }
    ++gStrings;
    reinterpret_cast<cRZString *>(uintptr_t(dst))->mString.assign(text);
    return Bool();
}
uint32_t __fastcall SetText(Obj *self, void *, uint32_t text)
{
    Call(self, 48);
    LogString(text);
    return Bool();
}

// The resource manager: 34 FindSegmentWithKey, 38 the segments into an array.
uint32_t __fastcall FindSegment(Obj *self, void *, uint32_t key, uint32_t out)
{
    Call(self, 34);
    LogKey(key);
    Ev(svcfx::Norm(out));
    if (Draw(3) == 0) return 0x100;
    HandOut(out, F(kSeg0 + Draw(kSegCount)));
    return 0x101;
}
uint32_t __fastcall SegmentsInto(Obj *self, void *, uint32_t array, uint32_t countAt)
{
    Call(self, 38);
    auto *const count = reinterpret_cast<uint32_t *>(uintptr_t(countAt));
    Ev(*count);
    const uint32_t n = *count < gWorld.segmentCount ? *count : gWorld.segmentCount;
    auto *const list = reinterpret_cast<void **>(uintptr_t(array));
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t k = Draw(kSegCount + 1);
        list[i] = k == kSegCount ? nullptr : static_cast<void *>(&F(kSeg0 + k));
        if (list[i]) ++F(kSeg0 + k).refs;
    }
    *count = n;
    return gWorld.segmentCount;
}

// The out string fake: 4 FromChar, 17 Copy.
void __fastcall OutFromChar(Obj *self, void *, uint32_t text)
{
    Call(self, 4);
    LogText(reinterpret_cast<const char *>(uintptr_t(text)));
}
void __fastcall OutCopy(Obj *self, void *, uint32_t other)
{
    Call(self, 17);
    LogString(other);
}

template <class Fn> void Shim(Obj &obj, unsigned slot, unsigned args, Fn fn)
{
    obj.Set(slot, args);
    obj.table[slot] = reinterpret_cast<void *>(fn);
}

char gPathText[96];

void BuildFakes(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 11);
    svcfx::ResetAll();
    gWorld.framework = rng() % 8 != 0;
    gWorld.manager = rng() % 6 != 0;
    gWorld.prime = rng() % 3 != 0;
    gWorld.segments = rng() % 5 != 0;
    gWorld.mask = kMasks[rng() % 8];
    gWorld.have = rng() % 4 == 0 ? 0xffffffffu : uint32_t(rng());
    gWorld.userDir = kUserDirs[rng() % 4];
    for (const char *&d : gWorld.dirs) d = kDataDirs[rng() % 5];
    gWorld.registryFile = kRegistryFiles[rng() % 3];
    for (uint32_t &p : gWorld.segPath) p = rng() % kPathCount;
    gWorld.segmentCount = rng() % 5;

    Obj &service = F(kService);
    service.Set(5, "ddp", 0, &GetSystemService);
    service.Set(3, "pdp", 0, &GetClassObject);
    svcfx::gFrameworkIndex = kService;
    Obj &manager = F(kManager);
    Shim(manager, 34, 2, &FindSegment);
    manager.Set(35, 0, gWorld.segmentCount);
    Shim(manager, 38, 2, &SegmentsInto);
    manager.Set(30, "p", 0, &BoolResult);
    for (unsigned i = 0; i <= kSegCount; ++i) {
        Obj &seg = F(i == kSegCount ? kRegFile : kSeg0 + i);
        seg.Set(3, 0, 0, &BoolResult);
        seg.Set(4, 0, 0, &BoolResult);
        seg.Set(5, "dd", 0, &BoolResult);
        seg.Set(9, 0, 0, &BoolResult);
        Shim(seg, 11, 1, &SegGetPath);
        Shim(seg, 12, 1, &SegSetPath);
        Shim(seg, 19, 2, &SegKeyList);
        Shim(seg, 21, 1, &SegRecordSize);
        Shim(seg, 22, 3, &SegOpenRecord);
        Shim(seg, 27, 3, &SegReadRecord);
    }
    Obj &record = F(kRecord);
    record.Set(0, "dp", 0, &SerialOf);
    record.Set(6, 0, 0, &BoolResult);
    record.Set(11, 1, 0x1111);
    Obj &serial = F(kSerial);
    Shim(serial, 11, 1, &GetU16);
    Shim(serial, 13, 1, &GetU32);
    Shim(serial, 24, 1, &GetText);
    serial.Set(35, "d", 0, &BoolResult);
    serial.Set(37, "d", 0, &BoolResult);
    Shim(serial, 48, 1, &SetText);
    F(kPath).Set(6, 0, gPathText);
    F(kPath).Set(7, 0, 0u);
    Shim(F(kOut), 4, 1, &OutFromChar);
    Shim(F(kOut), 17, 1, &OutCopy);

    *GameAddress<void *>(0x10c9378) = gWorld.framework ? &F(kService) : nullptr;
    *GameAddress<uint32_t>(0x10faf94) = gWorld.mask;
}

// ---- driving a registry ----

// Every slot is called with its arguments pushed by hand and the stack pointer read back: a body that pops a different
// number of bytes than the caller pushed (a wrong declaration) sets gStackSkew, which each step logs (always 0 for the
// original, whose `ret N` is the reference).
int32_t gStackSkew;
uint32_t CallChecked(void *fn, void *self, const uint32_t *args, uint32_t count)
{
    uint32_t result, before, after;
    __asm {
        mov before, esp
        mov ecx, count
        mov edx, args
    push_next:
        test ecx, ecx
        jz pushed
        dec ecx
        push dword ptr [edx + ecx * 4]
        jmp push_next
    pushed:
        mov ecx, self
        call fn
        mov result, eax
        mov after, esp
        mov esp, before
    }
    if (after != before) gStackSkew = int32_t(after - before);
    return result;
}
template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    static_assert(((sizeof(A) == 4) && ...), "dword arguments only");
    static_assert(sizeof(R) == 4, "dword result");
    const uint32_t args[sizeof...(A) + 1] = {(uint32_t)(uintptr_t)a..., 0};
    return R(CallChecked((*static_cast<void ***>(face))[slot], face, args, uint32_t(sizeof...(A))));
}
uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }

bool gFactory;
void *Build(difftest::Context &ctx, Blob &b, bool ours)
{
    if (gFactory) {
        void *face = ours ? static_cast<void *>(Sys::Create()) : ctx.Original<void *(__cdecl *)()>(0xb57de0)();
        b.push_back(face ? 1 : 0);
        return static_cast<uint8_t *>(face) - 0x1c;
    }
    void *memory = ::operator new(Sys::kSize);
    std::memset(memory, 0xc5, Sys::kSize);
    void *r = ours ? static_cast<void *>(::new (memory) Sys) : ctx.Original<void *(__thiscall *)(void *)>(0xb56dc0)(memory);
    b.push_back(r == memory);
    return memory;
}

uint32_t Word(const uint8_t *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(m + off); }
const uint8_t *At(uint32_t p) { return reinterpret_cast<const uint8_t *>(uintptr_t(p)); }

void DumpString(Blob &b, const uint8_t *at)
{
    b.push_back(Word(at, 0) - P(GameAddress<void>(0)));
    const auto *s = reinterpret_cast<const std::string *>(at + 4);
    b.push_back(uint32_t(s->size()));
    b.push_back(uint32_t(s->capacity()));
    for (char c : *s) b.push_back(uint8_t(c));
    b.push_back(Word(at, 0x1c));
}
void DumpStrings(Blob &b, const uint8_t *at)
{
    const uint32_t first = Word(at, 0), last = Word(at, 4), end = Word(at, 8);
    b.push_back(first != 0);
    b.push_back((last - first) / 0x20);
    b.push_back((end - first) / 0x20);
    for (uint32_t p = first; p != last; p += 0x20) DumpString(b, At(p));
}
// An unordered_map<ContentKey, ContentString> as MSVC lays it out: the load factor, the list in order (key, path), the
// bucket vector (each bucket's first and last node as list positions), the mask and the bucket count.
void DumpKeyMap(Blob &b, const uint8_t *at)
{
    b.push_back(Word(at, 0));
    const uint32_t head = Word(at, 4);
    b.push_back(Word(at, 8));
    std::map<uint32_t, uint32_t> index;
    index[head] = 0xffffffffu;
    uint32_t n = 0;
    for (uint32_t node = Word(At(head), 0); node != head && n < 10000; node = Word(At(node), 0), ++n) {
        index[node] = n;
        for (uint32_t i = 0; i < 4; ++i) b.push_back(Word(At(node), 8 + 4 * i));
        DumpString(b, At(node) + 0x18);
        b.push_back(index.count(Word(At(node), 4)) ? index[Word(At(node), 4)] : 0xeeeeeeeeu);
    }
    const uint32_t first = Word(at, 0xc), last = Word(at, 0x10), end = Word(at, 0x14);
    b.push_back((last - first) / 4);
    b.push_back((end - first) / 4);
    for (uint32_t p = first; p != last; p += 4) {
        const uint32_t node = Word(At(p), 0);
        b.push_back(index.count(node) ? index[node] : 0xdddddddd);
    }
    b.push_back(Word(at, 0x18));
    b.push_back(Word(at, 0x1c));
}
void DumpEntries(Blob &b, const uint8_t *at)
{
    const uint32_t first = Word(at, 0), last = Word(at, 4), end = Word(at, 8);
    b.push_back(first != 0);
    b.push_back((last - first) / 0x4c);
    b.push_back((end - first) / 0x4c);
    for (uint32_t p = first; p != last; p += 0x4c) {
        const uint8_t *const e = At(p);
        b.push_back(e[0]); // +1..+3: heap leftovers (0xb56ec0 / 0xb56b50 write the byte)
        b.push_back(Word(e, 4));
        b.push_back(Word(e, 8));
        DumpString(b, e + 0xc);
        DumpKeyMap(b, e + 0x2c);
    }
}
void DumpNames(Blob &b, const uint8_t *at)
{
    const auto &map = *reinterpret_cast<const RegistryNameMap *>(at);
    b.push_back(uint32_t(map.size()));
    for (const auto &entry : map) {
        DumpString(b, reinterpret_cast<const uint8_t *>(&entry.first));
        for (uint32_t w : entry.second.mWords) b.push_back(w);
    }
}

void Dump(Blob &b, void *object, bool ours)
{
    const auto *m = static_cast<const uint8_t *>(object);
    static const uint32_t kFaces[3][2] = {{0, Sys::kVTable}, {4, Sys::kVTable4}, {0x1c, Sys::kRegistryVTable}};
    for (const auto &f : kFaces) {
        const bool game = Word(m, f[0]) == P(GameAddress<void>(f[1]));
        b.push_back(ours ? !game : game);
    }
    // +0x19..+0x1b (after the running flag) and +0x21..+0x23 (after the initialised flag) are never written: the 0xc5
    // fill, or heap leftovers from the factory.
    for (uint32_t off = 8; off < 0x1c; off += 4) b.push_back(off == 0x18 && gFactory ? m[0x18] : Word(m, off));
    b.push_back(gFactory ? m[0x20] : Word(m, 0x20));
    DumpEntries(b, m + 0x24);
    DumpNames(b, m + 0x30);
    DumpString(b, m + 0x38);
    DumpStrings(b, m + 0x58);
    b.push_back(svcfx::Norm(Word(m, 0x64)));
}

struct Op {
    unsigned kind;
    uint32_t arg, arg2;
};

std::vector<Op> MakeOps(uint32_t seed)
{
    std::mt19937 rng(seed * 31 + 5);
    std::vector<Op> ops;
    const unsigned n = 4 + rng() % 22;
    if (rng() % 5) ops.push_back({0, uint32_t(rng()), 0});
    for (unsigned i = 0; i < n; ++i) ops.push_back({unsigned(rng() % 24), uint32_t(rng()), uint32_t(rng())});
    return ops;
}

const uint32_t kIids[] = {1, Sys::kIid, 0x287fb697, 0x0dc7057a, 0x12345678};
const uint32_t kIndexes[] = {0, 1, 2, 3, 13, 0x10000, 0xffff, 0x10001, 0xffffffff};

ContentKey gKey;
cGZPersistResourceKey gResourceKey;
ContentVersion gVersion;
uint32_t gOutWords[4];
uint8_t gCrcState[16];
uint8_t gCrcData[300];

void MakeKey(uint32_t a)
{
    std::mt19937 rng(a);
    for (uint32_t &w : gKey.mWords) w = rng() % 4 == 0 ? 0x80000000u | rng() % 2 : rng() % 2;
}
void SetPathText(uint32_t a)
{
    const char *text = kPaths[a % kPathCount];
    std::strcpy(gPathText, text);
    uint32_t length = uint32_t(std::strlen(text));
    if ((a >> 8) % 5 == 0 && length) --length; // the length short of the text
    F(kPath).ret[7] = length;
}
// The out string: a real cRZString (by content afterwards) or the fake.
cRZString gOutString;
void *OutTarget(uint32_t a) { return a % 3 == 0 ? static_cast<void *>(&F(kOut)) : static_cast<void *>(&gOutString); }
void LogOut(void *target)
{
    if (target == &gOutString) LogText(gOutString.mString);
}
void *SegmentOrNull(uint32_t a) { return a % 6 == 0 ? nullptr : static_cast<void *>(&F(kSeg0 + a % kSegCount)); }

bool HasEntries(const uint8_t *m) { return Word(m, 0x24) != Word(m, 0x28); }
bool HasManager(const uint8_t *m) { return Word(m, 0x64) != 0; }

void RunOp(Blob &b, void *object, const Op &op)
{
    void *const primary = object;
    void *const registry = Face(object, 0x1c);
    const auto *const m = static_cast<const uint8_t *>(object);
    const uint32_t a = op.arg, c = op.arg2;
    switch (op.kind) {
    case 0: b.push_back(Slot<uint32_t>(a % 2 ? primary : registry, a % 2 ? 8 : 3) & 0xff); break; // Init
    case 1: b.push_back(Slot<uint32_t>(a % 2 ? primary : registry, a % 2 ? 9 : 4) & 0xff); break; // Shutdown
    case 2: { // QueryInterface on a face, AddRef / Release through others
        void *faces[] = {primary, Face(object, 4), registry};
        void *q = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        const bool null = (a >> 12) % 7 == 0;
        const uint32_t ok = Slot<uint32_t>(faces[a % 3], 0, kIids[(a >> 4) % 5], null ? nullptr : &q) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(q)));
        b.push_back(Slot<uint32_t>(faces[(a >> 8) % 3], 1));
        b.push_back(Slot<uint32_t>(faces[(a >> 10) % 3], 2));
        if (ok && !null) b.push_back(Slot<uint32_t>(q, 2));
        break;
    }
    case 3: // the service accessors, OnTick / OnIdle, EntryCount
        b.push_back(Slot<uint32_t>(primary, 3));
        b.push_back(Slot<uint32_t>(primary, 5));
        b.push_back(Slot<uint32_t>(primary, 6) & 0xff);
        b.push_back(Slot<uint32_t>(primary, 12));
        Slot<uint32_t>(primary, 7, a % 2);
        b.push_back(Slot<uint32_t>(primary, 10, c) & 0xff);
        b.push_back(Slot<uint32_t>(primary, 11, c) & 0xff);
        b.push_back(Slot<uint32_t>(Face(object, 4), 5));
        b.push_back(Slot<uint32_t>(registry, 10));
        break;
    case 4: { // UpdateCrc (the primary face's slot 13)
        std::mt19937 rng(a);
        if (a % 4 == 0) for (uint8_t &s : gCrcState) s = uint8_t(rng());
        const uint32_t size = (c % 4 == 0) ? 0 : c % 3 == 0 ? 256 : c % 300;
        for (uint32_t i = 0; i < size; ++i) gCrcData[i] = (c % 3 == 0) ? uint8_t(i) : uint8_t(rng());
        b.push_back(Slot<uint32_t>(primary, 13, gCrcState, gCrcData, size, uint32_t(0x100 | (a >> 4) % 2)) & 0xff);
        for (uint8_t s : gCrcState) b.push_back(s);
        break;
    }
    case 5: // SaveAll / SaveEntry
        if (a % 3 == 0) b.push_back(Slot<uint32_t>(registry, 5) & 0xff);
        else b.push_back(Slot<uint32_t>(registry, 6, kIndexes[(a >> 2) % 9]) & 0xff);
        break;
    case 6: // ComputeKey
        for (uint32_t &w : gOutWords) w = 0xdddddddd;
        b.push_back(Slot<uint32_t>(registry, 7, SegmentOrNull(a), gOutWords) & 0xff);
        for (uint32_t w : gOutWords) b.push_back(w);
        break;
    case 7: { // GetVersion
        void *out = OutTarget(c);
        gOutString.mString.assign("old");
        b.push_back(Slot<uint32_t>(registry, 8, SegmentOrNull(a), out) & 0xff);
        LogOut(out);
        break;
    }
    case 8: { // GetProperty
        void *out = OutTarget(c);
        gOutString.mString.assign("old");
        b.push_back(Slot<uint32_t>(registry, 9, SegmentOrNull(a), kNames[(a >> 4) % 7], out) & 0xff);
        LogOut(out);
        break;
    }
    case 9: // EntryCount, GetEntryVersion
        b.push_back(Slot<uint32_t>(registry, 10));
        gOutWords[0] = gOutWords[1] = 0xdddddddd;
        b.push_back(Slot<uint32_t>(registry, 11, kIndexes[a % 9], gOutWords) & 0xff);
        b.push_back(gOutWords[0]);
        b.push_back(gOutWords[1]);
        break;
    case 10: // HasKey / FindKey (entry 0 read unchecked: 0xb588d0)
        if (!HasEntries(m)) break;
        MakeKey(c);
        if (a % 2) {
            b.push_back(Slot<uint32_t>(registry, 12, &gKey) & 0xff);
        } else {
            gOutWords[0] = 0xdddddddd;
            b.push_back(Slot<uint32_t>(registry, 15, &gKey, gOutWords) & 0xff);
            b.push_back(gOutWords[0]);
        }
        break;
    case 11: // FindEntryForResource
        gResourceKey = {a, c, a ^ c, 7};
        gOutWords[0] = 0xdddddddd;
        b.push_back(Slot<uint32_t>(registry, 13, &gResourceKey, gOutWords) & 0xff);
        b.push_back(gOutWords[0]);
        break;
    case 12: // FindEntryForPath
        SetPathText(a);
        gOutWords[0] = 0xdddddddd;
        b.push_back(Slot<uint32_t>(registry, 14, static_cast<void *>(&F(kPath)), gOutWords) & 0xff);
        b.push_back(gOutWords[0]);
        break;
    case 13: // IsPathRegistered
        SetPathText(a);
        b.push_back(Slot<uint32_t>(registry, 16, static_cast<void *>(&F(kPath))) & 0xff);
        break;
    case 14: // Rebuild (the manager used unchecked: 0xb57480)
        if (!HasManager(m)) break;
        gVersion = {a, uint16_t(c), uint16_t(c >> 16)};
        b.push_back(Slot<uint32_t>(registry, 17, &gVersion) & 0xff);
        break;
    case 15: { // RebuildEntry (the manager used unchecked past the index check: 0xb5a800)
        const uint32_t index = kIndexes[a % 9];
        const uint32_t count = (Word(m, 0x28) - Word(m, 0x24)) / 0x4c;
        if ((index & 0xffff) == 0 && count && !HasManager(m)) break;
        gVersion = {c, uint16_t(a), uint16_t(a >> 16)};
        b.push_back(Slot<uint32_t>(registry, 18, index, &gVersion) & 0xff);
        break;
    }
    case 16: // RegisterPath (entry 0 used unchecked: 0xb5a3b0)
        if (!HasEntries(m)) break;
        SetPathText(a);
        MakeKey(c);
        b.push_back(Slot<uint32_t>(registry, 19, static_cast<void *>(&F(kPath)), &gKey) & 0xff);
        break;
    case 17: { // RegisterSegment (entry 0 used unchecked by AddSegmentKey: 0xb59680)
        void *segment = SegmentOrNull(a);
        if (segment && !HasEntries(m)) break;
        b.push_back(Slot<uint32_t>(registry, 20, segment) & 0xff);
        break;
    }
    case 18: { // UnregisterSegment (FindKey: 0xb588d0)
        void *segment = SegmentOrNull(a);
        if (segment && !HasEntries(m)) break;
        b.push_back(Slot<uint32_t>(registry, 21, segment) & 0xff);
        break;
    }
    case 19: { // RemoveSegment (FindKey, and the manager unchecked: 0xb57cc0)
        void *segment = SegmentOrNull(a);
        if (segment && (!HasEntries(m) || !HasManager(m))) break;
        b.push_back(Slot<uint32_t>(registry, 22, segment) & 0xff);
        break;
    }
    case 20: // UnregisterKey (FindKey: 0xb588d0); no result
        if (!HasEntries(m)) break;
        MakeKey(c);
        Slot<uint32_t>(registry, 23, &gKey);
        break;
    case 21: { // GetPathForKey
        MakeKey(c);
        void *out = OutTarget(a);
        gOutString.mString.assign("old");
        uint8_t literal[4] = {0xcc, 0xcc, 0xcc, 0xcc};
        b.push_back(Slot<uint32_t>(registry, 24, &gKey, out, literal) & 0xff);
        LogOut(out);
        for (uint8_t v : literal) b.push_back(v);
        break;
    }
    case 22: { // WriteProperties
        ContentPropertyIds ids;
        const uint32_t n = kCounts[c % 5];
        for (uint32_t i = 0; i < n; ++i) ids.push_back({(c >> (3 + i)) & 7, (a >> (2 * i)) & 7});
        b.push_back(Slot<uint32_t>(registry, 25, SegmentOrNull(a), &ids) & 0xff);
        break;
    }
    default: { // ReadProperties (the segment used unchecked: 0xb59870), into a vector holding 0-3 pairs
        ContentProperties properties;
        const uint32_t n = a % 4;
        if ((a >> 2) % 2) properties.reserve(n + (a >> 3) % 3);
        for (uint32_t i = 0; i < n; ++i) {
            properties.emplace_back();
            properties.back().first.s.mString.assign(kNames[(c >> i) % 7]);
            properties.back().second.s.mString.assign(kPaths[(c >> (i + 4)) % kPathCount]);
        }
        b.push_back(Slot<uint32_t>(registry, 26, static_cast<void *>(&F(kSeg0 + c % kSegCount)), &properties) & 0xff);
        b.push_back(uint32_t(properties.size()));
        b.push_back(uint32_t(properties.capacity()));
        for (auto &p : properties) {
            DumpString(b, reinterpret_cast<const uint8_t *>(&p.first));
            DumpString(b, reinterpret_cast<const uint8_t *>(&p.second));
        }
        break;
    }
    }
}

} // namespace ctr_test

using namespace ctr_test;

DIFF_TEST(ContentRegistry_Run, 0xb58b40)
{
    // Also reached (see the file comment): 0xb57de0 (factory), 0xb56dc0 (constructor), 0xb57290 (deleting destructor),
    // 0xb57282 (its +4 thunk), 0xb59820 (QueryInterface), 0xb5985f / 0xb59867 (its +4 / +0x1c thunks), 0xb58cd2 /
    // 0xb5a32d (the +0x1c Init / Shutdown thunks), 0xb5a300, 0xb57c10, 0xb59d60, 0xb59e70, 0xb57770, 0xb5aad0, 0xb5a0d0, 0xb59800, 0xb58410, 0xb58e70,
    // 0xb58460, 0xb58540, 0xb588d0, 0xb58e90, 0xb57480, 0xb5a800, 0xb5a3b0, 0xb5a6f0, 0xb5a750, 0xb57cc0, 0xb57d80,
    // 0xb58950, 0xb5ab10, 0xb59870, and the helpers 0xb58ce0, 0xb57ac0, 0xb57b90, 0xb58170, 0xb57e50, 0xb58f80, 0xb591e0,
    // 0xb592c0, 0xb59d90, 0xb59fc0, 0xb59c40, 0xb573b0, 0xb59680, 0xb57380.
    // The original's container instantiations, run on the original side and compared through the dumped containers
    // (each checked reached with a temporary int3 probe): the name map's insert 0xb558c0 (Load's keys, RegisterPath,
    // AddSegmentKey) and erase by name 0xb5b120 (UnregisterKey / UnregisterSegment); the entry vector's resize
    // 0xb56470 / 0xb56a70 (Load) with the entry default constructor 0xb56ec0, the entry copy constructor 0xb56b50 and its
    // key map copy 0xb56c70 (AddEntry's push_back of a temporary); the key map's find 0xb56c10, operator[] 0xb57140,
    // insert 0xb5afd0, rehash 0xb5ae30 and erase 0xb5b060; the properties vector's resize 0xb561e0 /
    // 0xb56ae0 (ReadProperties); ComputeKey's sort 0xb56600 with its partition 0xb55eb0 and median 0xb55e50 (33 keys
    // and more). Not reached, so not verified: the entry vector's growth with elements to move (0xb55a60, 0xb56a00,
    // 0xb568b0's loop, 0xb55c70: after Load the capacity always holds Clear + AddEntry's two entries) and the sort's
    // 0xb56100.
    char traceFlag[4];
    svcfx::gTrace = GetEnvironmentVariableA("TS2_TEST_TRACE", traceFlag, sizeof(traceFlag)) != 0;
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    globals.SetWord(0x10c9378, 0);
    globals.SetWord(0x10faf94, 0);
    globals.SetWord(0x10fc484, 13); // strlen("%UserDataDir%") (the game's static initialiser 0x13fff0)
    globals.SetWord(0x10fb478, *GameAddress<uint32_t>(0x10fb478)); // the CRC table's flag (restored afterwards)
    std::vector<uint8_t> savedTable(0x1000);
    std::memcpy(savedTable.data(), GameAddress<void>(0x10fb480), 0x1000);
    gOutString.Construct();

    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::CodePatch gameFree(0x6c4b50, shape::CodeOf(&GameFree));
    shape::CodePatch attributes(0x1c3505, shape::CodeOf(&StandAttributes));
    shape::StandIn remove(0x1c2e25, &PathDelete, &StandRemoveFile);
    shape::StandIn common(0x1d0c00, &appinit::CommonImplSlot4, &StandCommonSlot4);
    shape::StandIn exists(0x1c1242, &cRZFile::PathExists, &StandPathExists);
    shape::StandIn fileNameInit(0x1c8bb8, &cRZFileName::Init, &StandFileNameInit);
    shape::StandIn drivePath(0x1c87c7, &cRZFileName::GetDrivePath, &StandDrivePath);
    shape::StandIn dispatch(0xaeb800, &Dispatch8, &StandDispatch8);

    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        const std::vector<Op> ops = MakeOps(seed);
        gFactory = seed % 4 == 0;
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            std::memset(gCrcState, 0x5a, sizeof(gCrcState));
            Blob first;
            void *object = Build(ctx, first, side == 1);
            svcfx::AddRange(object, Sys::kSize, kTag);
            Dump(first, object, side == 1);
            first.insert(first.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::gEv.clear();
            results[side].push_back(std::move(first));
            Slot<uint32_t>(object, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                gRng.seed(seed * 1000 + uint32_t(i) + 17);
                if (i < ops.size()) {
                    if (svcfx::gTrace) fprintf(stderr, "seed %u side %d op %u kind %u\n", seed, side, unsigned(i), ops[i].kind);
                    RunOp(b, object, ops[i]);
                    Dump(b, object, side == 1);
                } else {
                    b.push_back(Slot<uint32_t>(object, 2)); // the last release destroys it
                }
                b.push_back(uint32_t(gStackSkew)); // bytes popped beyond (or short of) what was pushed: 0
                gStackSkew = 0;
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
                svcfx::CaptureFakes(b);
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
        }
        for (size_t i = 0; i < results[0].size(); ++i) {
            if (!svcfx::Same(ctx, results[0][i], results[1][i], "ContentRegistry_Run", seed)) {
                if (svcfx::gTrace)
                    for (size_t w = 0; w < results[0][i].size() || w < results[1][i].size(); ++w)
                        fprintf(stderr, "%3u %08x %08x\n", unsigned(w), w < results[0][i].size() ? results[0][i][w] : 0,
                                w < results[1][i].size() ? results[1][i][w] : 0);
                const size_t op = i ? i - 1 : 0;
                ctx.Fail("  at step %u of %u (kind %u)", unsigned(i), unsigned(ops.size() + 2),
                         i && op < ops.size() ? ops[op].kind : 99u);
                seed = 3001;
                break;
            }
        }
    }

    gOutString.Destruct();
    std::memcpy(GameAddress<void>(0x10fb480), savedTable.data(), 0x1000);
}
