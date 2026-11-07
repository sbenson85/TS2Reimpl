// Differential tests for cTSConfigurationManager (src/ts/cTSConfigurationManager.h, lane IO): the constructor, destructor and
// factory, the slots that keep the settings (3, 4, 6, 8, 9, 12-18, QueryInterface on both faces) and the device record's
// helpers (0xccefa0, 0xccf4f0, 0xcd47d0, 0xcd5490, 0xcd4f00, 0xccf930, 0xcce860).
//
// A world is one manager in a guarded buffer (the original's constructor 0xccdf90 builds it on the original side and the
// adapter route, ours on the vtable route) with device records the test builds (tsconfig::Device: options with values and
// typed settings, two real property sets each), a real settings set (cGZPropertySetString) at +0x170, recording fakes for the
// framework (the settings service for Init) and the listener, and the option prefix and the activeDeviceNum name globals set
// to test text. A random sequence of slot calls runs on each side; after each call the result, the out-values, the manager's
// state and every set's contents (key text, value vtable, payload, the key text table, the cursor, the count) are appended to
// a blob, with the fakes' event logs and reference counts. Records Shutdown deletes are checked freed (FreeRecorder).
//
// Routes (tests/shape.h): 0 the original by RVA, 1 our object called through our vtable's slot where the game's holds the
// RVA, 2 the game-built object with an arity-matched trap in that slot of the game's vtable copy, called through the RVA
// hook's adapter where one is kept (SetCurrentDevice 0x7c4ff0) and through our vtable's member where the adapter was dropped
// (every other slot: callers.py found no reference outside our code, so they are in effect through the factory hook).
// Slot 6 (which slots 8 and 9 call through `this`) is a logging forwarder on every route's copy, so a devirtualised self-call
// shows as a missing event.
#include "difftest.h"
#include "free_recorder.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"
#include "gz/cGZPropertySet.h"
#include "ts/cTSConfigurationManager.h"

#include <cctype>
#include <cstring>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

namespace {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{0, "d"}};

using Mgr = cTSConfigurationManager;
using Adapters = cTSConfigurationManagerAdapters;
using tsconfig::Device;
using tsconfig::Option;
using tsconfig::Setting;
using tsconfig::Value;
using svcfx::Blob;
using svcfx::F;

constexpr uint32_t kPrefixCell = 0x10899f0;  // std::string (0x18 bytes)
constexpr uint32_t kFrameworkCell = 0x10c9378; // the service object (src/rz/Singleton_20ddfb.h GetServiceObject)
constexpr uint32_t kDeviceNumCell = 0x10899e0; // const char * (then +4 the list name, +8 the software flag name)
const char *OptionPrefixText() { return GameAddress<const std::string>(kPrefixCell)->c_str(); }
constexpr unsigned kFaces = 2;
constexpr uint32_t kFaceOffset[kFaces] = {0, tsconfig::kUnknownFace};
constexpr uint32_t kGameVt[kFaces] = {0xeb95ac, 0xeb95fc}; // the game's primary and +4 vtables
constexpr unsigned kFaceSlots[kFaces] = {19, 6};
constexpr uint8_t kArity[kFaces][19] = {{2, 0, 0, 0, 0, 0, 1, 3, 1, 1, 0, 1, 1, 2, 2, 3, 2, 1, 2}, {2, 0, 0, 1, 0, 0}};
enum : unsigned { kFramework = 0, kListenerA = 1, kListenerB = 2 };
enum : uint32_t { evSelf = 0x70, evListener = 0x71, evMark = 0x72 };

/// The allocator service (cache 0x10c95d4, guard 0x10c95d8) as a fake on the process heap, restored on exit. In game the
// service is the GeneralAllocator and the CRTs use the process heap, so a block must go back to the allocator it came
// from. The fake keeps the set of blocks it handed out; KERNEL32!HeapFree is wrapped in the original image's import table
// (its CRT's free) and in this executable's (our CRT's). A wrong free counts either way: a service block reaching a CRT's
// free, or a block the service never handed out reaching the service's free. Both sides share the process heap, so a
// wrong free is counted without corrupting anything; every test fails on a nonzero count (Check).
constexpr unsigned kMaxServiceBlocks = 1u << 14;
void *gServiceBlocks[kMaxServiceBlocks];
unsigned gServiceBlockCount = 0;
bool gServiceOverflow = false;
uint32_t gWrongFrees = 0;
bool TakeServiceBlock(void *mem)
{
    for (unsigned i = gServiceBlockCount; i-- > 0;) {
        if (gServiceBlocks[i] != mem) continue;
        gServiceBlocks[i] = gServiceBlocks[--gServiceBlockCount];
        return true;
    }
    return false;
}
void *__fastcall HeapAlloc3(void *, void *, uint32_t size)
{
    void *mem = HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
    if (gServiceBlockCount < kMaxServiceBlocks) gServiceBlocks[gServiceBlockCount++] = mem;
    else gServiceOverflow = true;
    return mem;
}
void __fastcall HeapFree4(void *, void *, void *mem)
{
    if (mem && !TakeServiceBlock(mem)) ++gWrongFrees; // a CRT block freed through the service
    HeapFree(GetProcessHeap(), 0, mem);
}
using HeapFreeFn = BOOL(WINAPI *)(HANDLE, DWORD, LPVOID);
HeapFreeFn gPreviousFree[2];
BOOL WINAPI ImageCrtFree(HANDLE heap, DWORD flags, LPVOID mem)
{
    if (mem && TakeServiceBlock(mem)) ++gWrongFrees; // a service block freed by the original's CRT
    return gPreviousFree[0](heap, flags, mem);
}
BOOL WINAPI OurCrtFree(HANDLE heap, DWORD flags, LPVOID mem)
{
    if (mem && TakeServiceBlock(mem)) ++gWrongFrees; // a service block freed by our CRT
    return gPreviousFree[1](heap, flags, mem);
}
// The import table entry KERNEL32!`name` of the module at `base`.
void **KernelImportSlot(uint8_t *base, const char *name)
{
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (auto *d = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress); d->Name != 0; ++d) {
        if (_stricmp(reinterpret_cast<const char *>(base + d->Name), "KERNEL32.dll") != 0) continue;
        const auto *names = reinterpret_cast<const IMAGE_THUNK_DATA32 *>(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        auto *slots = reinterpret_cast<IMAGE_THUNK_DATA32 *>(base + d->FirstThunk);
        for (; names->u1.AddressOfData != 0; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal)) continue;
            const auto *byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME *>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char *>(byName->Name), name) == 0) return reinterpret_cast<void **>(&slots->u1.Function);
        }
    }
    return nullptr;
}
void WriteSlot(void **slot, void *value)
{
    DWORD old;
    VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old);
    *slot = value;
    VirtualProtect(slot, sizeof(void *), old, &old);
}
class HeapAllocatorScope {
public:
    explicit HeapAllocatorScope(difftest::Context &ctx)
        : ctx_(ctx), savedService_(*GameAddress<void *>(0x10c95d4)), savedGuard_(*GameAddress<int32_t>(0x10c95d8))
    {
        vtable_[3] = reinterpret_cast<void *>(&HeapAlloc3);
        vtable_[4] = reinterpret_cast<void *>(&HeapFree4);
        object_ = vtable_;
        *GameAddress<void *>(0x10c95d4) = &object_;
        *GameAddress<int32_t>(0x10c95d8) = INT32_MIN;
        gServiceBlockCount = 0;
        gServiceOverflow = false;
        gWrongFrees = 0;
        slots_[0] = KernelImportSlot(ctx.image.At<uint8_t>(0), "HeapFree");
        slots_[1] = KernelImportSlot(reinterpret_cast<uint8_t *>(GetModuleHandleW(nullptr)), "HeapFree");
        if (!slots_[0] || !slots_[1]) ctx.Fail("allocator scope: no HeapFree import (image %d, ours %d)", slots_[0] != nullptr, slots_[1] != nullptr);
        void *wrappers[2] = {reinterpret_cast<void *>(&ImageCrtFree), reinterpret_cast<void *>(&OurCrtFree)};
        for (int i = 0; i < 2; ++i) {
            if (!slots_[i]) continue;
            gPreviousFree[i] = reinterpret_cast<HeapFreeFn>(*slots_[i]);
            WriteSlot(slots_[i], wrappers[i]);
        }
    }
    ~HeapAllocatorScope()
    {
        for (int i = 1; i >= 0; --i)
            if (slots_[i]) WriteSlot(slots_[i], reinterpret_cast<void *>(gPreviousFree[i]));
        *GameAddress<void *>(0x10c95d4) = savedService_;
        *GameAddress<int32_t>(0x10c95d8) = savedGuard_;
    }
    HeapAllocatorScope(const HeapAllocatorScope &) = delete;
    HeapAllocatorScope &operator=(const HeapAllocatorScope &) = delete;

    // Fails the test on any wrong free so far (`what` names the test or the case).
    void Check(const char *what)
    {
        if (gWrongFrees) ctx_.Fail("%s: %u block(s) freed by the wrong allocator (service vs CRT)", what, gWrongFrees);
        if (gServiceOverflow) ctx_.Fail("%s: allocator scope's block table overflowed", what);
        gWrongFrees = 0;
        gServiceOverflow = false;
    }

private:
    difftest::Context &ctx_;
    void *vtable_[8] = {};
    void *object_ = nullptr;
    void *savedService_;
    int32_t savedGuard_;
    void **slots_[2]{};
};
int gRoute = 0;
unsigned gTrapHits = 0;
void *gCopy[kFaces][19];
void *const *gOurs[kFaces];

void *const *GameVt(unsigned f) { return shape::GameVTable(kGameVt[f]); }
void LearnOurs()
{
    if (gOurs[0]) return;
    for (unsigned f = 0; f < kFaces; ++f) gOurs[f] = shape::Ours<Mgr>(kFaceOffset).face[f];
}
uint32_t TrapHit(void *, unsigned)
{
    ++gTrapHits;
    svcfx::Ev(0x7eaf);
    return 0;
}
// Slot 6 on every route: logged, then the route's own IsDeviceConfigured.
uint32_t __fastcall LoggedSlot6(Mgr *m, void *, int32_t index)
{
    svcfx::Ev(evSelf);
    svcfx::Ev(6);
    svcfx::Ev(uint32_t(index));
    void *const own = gRoute == 1 ? gOurs[0][6] : GameVt(0)[6];
    return reinterpret_cast<uint32_t(__thiscall *)(Mgr *, int32_t)>(own)(m, index);
}
void InstallVtables(void *object)
{
    for (unsigned f = 0; f < kFaces; ++f) {
        void *const *own = gRoute == 1 ? gOurs[f] : GameVt(f);
        for (unsigned s = 0; s < kFaceSlots[f]; ++s) gCopy[f][s] = own[s];
        shape::VTableAt(object, kFaceOffset[f]) = gCopy[f];
    }
    gCopy[0][6] = reinterpret_cast<void *>(&LoggedSlot6);
}
uint32_t VtCode(unsigned f, const void *word)
{
    if (word == gCopy[f]) return 0x7c00 + f;
    if (word == GameVt(f) || word == gOurs[f]) return 0x7d00 + f;
    return shape::P(word);
}

// ---- the world ----

struct Guarded {
    uint8_t before[16];
    alignas(8) uint8_t bytes[sizeof(Mgr)];
    uint8_t after[16];
};
Guarded gBox;
Mgr *M() { return reinterpret_cast<Mgr *>(gBox.bytes); }

alignas(8) uint8_t gPrefixSaved[0x18];
char gLongPrefix[40];
const char *const kDeviceNumName = "deviceNumberKeyLongerThanSixteen";
const char *const kNames[] = {"Shadows", "Light", "TextureQualityLevel", "", "Fog", "Empty"};
const char *const kSettingNames[] = {"a", "bb", "settingNameLongerThan15", "Shadows", "x"};

// The option prefix global as a std::string (short: in its buffer; long: a pointer to static text).
void SetPrefix(uint32_t pick)
{
    auto *bytes = GameAddress<uint8_t>(kPrefixCell);
    std::memset(bytes, 0, 0x18);
    const char *text = pick % 3 == 0 ? "" : pick % 3 == 1 ? "cfg." : "optionPrefixLongerThanSixteen.";
    const uint32_t size = uint32_t(std::strlen(text));
    if (size < 16) {
        std::memcpy(bytes, text, size + 1);
        *reinterpret_cast<uint32_t *>(bytes + 0x14) = 15;
    } else {
        std::memcpy(gLongPrefix, text, size + 1);
        *reinterpret_cast<char **>(bytes) = gLongPrefix;
        *reinterpret_cast<uint32_t *>(bytes + 0x14) = 39;
    }
    *reinterpret_cast<uint32_t *>(bytes + 0x10) = size;
}

struct Cells {
    Cells()
    {
        std::memcpy(gPrefixSaved, GameAddress<uint8_t>(kPrefixCell), 0x18);
        for (unsigned i = 0; i < 4; ++i) {
            saved[i] = GameAddress<const char *>(kDeviceNumCell)[i];
            GameAddress<const char *>(kDeviceNumCell)[i] = kCellNames[i];
        }
    }
    ~Cells()
    {
        std::memcpy(GameAddress<uint8_t>(kPrefixCell), gPrefixSaved, 0x18);
        for (unsigned i = 0; i < 4; ++i) GameAddress<const char *>(kDeviceNumCell)[i] = saved[i];
    }
    // The four name cells (0x10899e0 device number, +4 device list, +8 software flag, +0xc script error code) get the test's names.
    inline static const char *const kCellNames[4] = {kDeviceNumName, "devList", "softwareFlagKeyName", "scriptErrorCode"};
    const char *saved[4];
};

void PushText(Blob &b, const char *text)
{
    const size_t n = std::strlen(text);
    b.push_back(uint32_t(n));
    for (size_t i = 0; i < n; ++i) b.push_back(uint8_t(text[i]));
}

// A string set by its layout (works for the game's vtable and ours): count, entries (key text, value vtable, payload: a
// boolean's low byte), the key text table, the cursor's index.
void DumpSet(Blob &b, const cGZPropertySetString *set)
{
    b.push_back(0x5e7);
    b.push_back(set->mRefCount);
    b.push_back(uint32_t(set->mEntries.size()));
    for (const cGZPropertyEntry &e : set->mEntries) {
        b.push_back(e.mKey);
        PushText(b, set->mKeyText.data() + e.mKey);
        b.push_back(shape::P(e.mValue.mVTable));
        const bool boolean = e.mValue.mVTable == GameAddress<const void>(gzany::kBooleanVTable);
        if (e.mValue.mVTable == GameAddress<const void>(gzany::kStringVTable)) {
            // A string value: its text (the payload is the counted string's address).
            const auto *text = reinterpret_cast<cIGZString *>(uintptr_t(e.mValue.mValue));
            if (text) PushText(b, VirtualCall<const char *(__thiscall *)(const void *), 5>(text));
            else b.push_back(0xdead);
            continue;
        }
        b.push_back(boolean ? (e.mValue.mValue & 0xff) : e.mValue.mValue);
    }
    b.push_back(uint32_t(set->mKeyText.size()));
    for (char c : set->mKeyText) b.push_back(uint8_t(c));
    // The cursor as an index; one left pointing into storage the entries have since moved out of (GetFirst / GetNext, then
    // a SetProperty that grew the vector) is a dangling pointer on both sides, so only its being stale is compared.
    const cGZPropertyEntry *first = set->mEntries.data();
    const cGZPropertyEntry *cursor = set->mCursor;
    if (!cursor) b.push_back(0xffffu);
    else if (cursor >= first && cursor <= first + set->mEntries.size()) b.push_back(uint32_t(cursor - first));
    else b.push_back(0xfffeu);
}

void DumpDevice(Blob &b, const Device *d)
{
    b.push_back(0xde0);
    b.push_back(d->mTag);
    b.push_back(d->mIndex);
    b.push_back(d->mErrorCode);
    b.push_back(uint32_t(d->mOptions.size()));
    for (const Option &o : d->mOptions) {
        PushText(b, o.mName.c_str());
        b.push_back(o.mSaved);
        b.push_back(o.mCurrent);
        b.push_back(uint32_t(o.mValues.size()));
    }
    DumpSet(b, &d->mVariables);
    DumpSet(b, &d->mApplied);
}

void DumpManager(Blob &b, const Mgr *m)
{
    b.push_back(VtCode(0, shape::VTableAt(const_cast<Mgr *>(m), 0)));
    b.push_back(VtCode(1, shape::VTableAt(const_cast<Mgr *>(m), 4)));
    b.push_back(m->mRefCount);
    b.push_back(uint32_t(m->mCurrentDevice));
    b.push_back(uint32_t(m->mDevices.size()));
    for (const Device *d : m->mDevices) DumpDevice(b, d);
    DumpSet(b, &m->mOverrides);
    b.push_back(m->mSettings.mObject ? 1u : 0u);
    if (m->mSettings.mObject) DumpSet(b, static_cast<const cGZPropertySetString *>(m->mSettings.mObject));
    b.push_back(svcfx::Norm(shape::P(m->mListener.mObject)));
}

// ---- random contents ----

void FillValue(std::mt19937 &r, Value &v)
{
    std::vector<Setting> *lists[] = {&v.mBooleans, &v.mUint32s, &v.mSint32s, &v.mFloats};
    for (auto *list : lists) {
        const unsigned n = r() % 3;
        for (unsigned i = 0; i < n; ++i) list->push_back({kSettingNames[r() % 5], r() % 3 ? r() % 7 : r()});
    }
}

void Prefill(std::mt19937 &r, cGZPropertySetString &set)
{
    const unsigned n = r() % 3;
    for (unsigned i = 0; i < n; ++i) {
        GZServiceString key(kSettingNames[r() % 5]);
        AnyUint32 any{GameAddress<const void>(gzany::kUint32VTable), r() % 9};
        set.cGZPropertySetString::SetProperty(&key, &any);
    }
}

bool gHasEmpty = false; // some record has an option without values (no ApplyOptions then)

Device *MakeDevice(std::mt19937 &r, uint32_t index, bool allowEmpty)
{
    Device *d = new Device;
    d->mIndex = index;
    d->mErrorCode = r() % 3 == 0 ? r() % 3 : 0;
    d->mDevice = nullptr;
    const unsigned options = r() % 5;
    for (unsigned i = 0; i < options; ++i) {
        Option o;
        o.mName = kNames[r() % 5];
        const unsigned values = 1 + r() % 3;
        o.mValues.resize(values);
        for (Value &v : o.mValues) FillValue(r, v);
        o.mSaved = r() % values;
        o.mCurrent = r() % values;
        d->mOptions.push_back(std::move(o));
    }
    if (allowEmpty && r() % 4 == 0) {
        gHasEmpty = true;
        Option empty; // an option without values: slots 13, 14 and 16 skip it (slot 15 is never asked for it)
        empty.mName = kNames[5];
        empty.mSaved = 0;
        empty.mCurrent = 0;
        d->mOptions.push_back(std::move(empty));
    }
    Prefill(r, d->mVariables);
    Prefill(r, d->mApplied);
    return d;
}

cGZPropertySetString *gSettings = nullptr;     // the set at +0x170 at the start
cGZPropertySetString *gNextSettings = nullptr; // what the framework hands Init
std::vector<Device *> gDevices;                // every record built (freed at the end unless Shutdown freed it)

// The framework's slot 5 (clsid, iid, out): the next settings set (counted) when value[0], else nothing.
uint32_t FrameworkQuery(svcfx::Obj *self, uint32_t *args)
{
    if (!self->value[0]) return 0;
    *reinterpret_cast<void **>(uintptr_t(args[2])) = gNextSettings;
    gNextSettings->AddRef();
    return 1;
}
// The listener's slot 9 (key, value): the key's text and the value logged.
uint32_t ListenerNotify(svcfx::Obj *, uint32_t *args)
{
    svcfx::Ev(evListener);
    PushText(svcfx::gEv, reinterpret_cast<const GZServiceString *>(uintptr_t(args[0]))->c_str());
    svcfx::Ev(args[1]);
    return 0;
}

void Build(difftest::Context &ctx, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 7);
    svcfx::ResetAll();
    svcfx::gFrameworkIndex = kFramework;
    F(kFramework).Set(5, "ddp", 0, FrameworkQuery);
    F(kFramework).value[0] = r() % 4 != 0;
    F(kListenerA).Set(9, "pd", 0, ListenerNotify);
    F(kListenerB).Set(9, "pd", 0, ListenerNotify);
    SetPrefix(r());

    std::memset(gBox.before, 0xa5, sizeof(gBox.before));
    std::memset(gBox.after, 0x5a, sizeof(gBox.after));
    std::memset(gBox.bytes, 0xcd, sizeof(gBox.bytes));
    LearnOurs();
    if (gRoute == 1) ::new (gBox.bytes) Mgr;
    else ctx.Original<void *(__thiscall *)(void *)>(0xccdf90)(gBox.bytes);
    InstallVtables(gBox.bytes);
    Mgr *m = M();
    m->mRefCount = r() % 4;
    const unsigned devices = r() % 6 == 0 ? 0 : 1 + r() % 4;
    gDevices.clear();
    gHasEmpty = false;
    for (unsigned i = 0; i < devices; ++i) {
        gDevices.push_back(MakeDevice(r, i, seed % 4 == 1));
        m->mDevices.push_back(gDevices.back());
    }
    m->mCurrentDevice = devices ? int32_t(r() % devices) : 0;
    m->mActiveDevice = int32_t(r() % 3);
    gSettings = new cGZPropertySetString;
    gSettings->mRefCount = 2; // the test holds one
    Prefill(r, *gSettings);
    gNextSettings = new cGZPropertySetString;
    gNextSettings->mRefCount = 1;
    Prefill(r, *gNextSettings);
    m->mSettings.mObject = gSettings;
    Prefill(r, m->mOverrides);
    if (r() % 2) {
        m->mListener.mObject = reinterpret_cast<cIGZUnknown *>(&F(kListenerA));
        F(kListenerA).refs = 1001;
    }
    svcfx::AddRange(gBox.bytes, sizeof(gBox.bytes), 0x10000000);
}

// Runs the function at `rva` the route's way (the face of `this` is `face`).
template <class Fn, class... A> auto Call(difftest::Context &ctx, uint32_t rva, unsigned face, void *adapter, A... args)
{
    void *self = gBox.bytes + kFaceOffset[face];
    if (gRoute == 0) return ctx.Original<Fn>(rva)(self, args...);
    const unsigned slot = shape::SlotOf(kGameVt[face], kFaceSlots[face], rva);
    if (slot == ~0u) return reinterpret_cast<Fn>(adapter)(self, args...);
    // Route 1, and route 2 for a slot whose adapter was dropped (nothing but the game's vtable referenced it: in effect
    // through callers, docs/standalone-shape.md): our vtable's member.
    if (gRoute == 1 || !adapter) return reinterpret_cast<Fn>(gOurs[face][slot])(self, args...);
    void *const kept = gCopy[face][slot];
    gCopy[face][slot] = shape::TrapFor<TrapHit>(kArity[face][slot]);
    auto restore = [&] { gCopy[face][slot] = kept; };
    if constexpr (std::is_void_v<decltype(reinterpret_cast<Fn>(adapter)(self, args...))>) {
        reinterpret_cast<Fn>(adapter)(self, args...);
        restore();
    } else {
        auto result = reinterpret_cast<Fn>(adapter)(self, args...);
        restore();
        return result;
    }
}
#define ADAPTER(name) reinterpret_cast<void *>(&Adapters::name)
void *const kDropped = nullptr; // a slot whose adapter and hook were dropped (see Call)

// One side of the slot sequence for a seed: the blob of everything observed.
Blob RunSlots(difftest::Context &ctx, uint32_t seed, bool withShutdown)
{
    Build(ctx, seed);
    Mgr *m = M();
    std::mt19937 r(seed ^ 0x51ab1e);
    Blob b;
    difftest::FreeRecorder frees;
    const unsigned steps = 4 + r() % 10;
    for (unsigned step = 0; step < steps; ++step) {
        const unsigned count = unsigned(m->mDevices.size());
        unsigned op = r() % 14;
        if (((op >= 1 && op <= 3) || op == 13) && !count) op = 4; // these index the record vector unchecked
        if ((op == 2 || op == 3 || op == 13) && gHasEmpty) op = 6;
        if ((op == 6 || op == 7 || op == 8) && !count) op = 9;
        const uint32_t index = count ? r() % count : 0;
        const char *name = kNames[r() % 6];
        b.push_back(evMark);
        b.push_back(op);
        switch (op) {
        case 0:
            b.push_back(Call<uint8_t(__thiscall *)(void *)>(ctx, 0xcd1ab0, 0, kDropped));
            break;
        case 1:
            b.push_back(Call<uint8_t(__thiscall *)(void *, int32_t)>(ctx, 0xcd1be0, 0, kDropped, int32_t(index)));
            break;
        case 2:
            if (!m->mSettings.mObject) break;
            Call<void(__thiscall *)(void *, int32_t)>(ctx, 0xccf880, 0, kDropped, int32_t(index));
            break;
        case 3:
            if (!m->mSettings.mObject) break;
            Call<void(__thiscall *)(void *, int32_t)>(ctx, 0xccf8e0, 0, kDropped, int32_t(index));
            break;
        case 4: {
            // Out of range now and then: only slot 13 runs with it (it checks), the others get a valid one back first.
            const int32_t current = r() % 5 == 0 ? int32_t(count + r() % 2) : int32_t(index);
            Call<void(__thiscall *)(void *, int32_t)>(ctx, 0x7c4ff0, 0, ADAPTER(SetCurrentDevice), current);
            b.push_back(uint32_t(m->mCurrentDevice));
            if (m->mSettings.mObject)
                Call<void(__thiscall *)(void *, const char *, uint32_t)>(ctx, 0xcd5110, 0, kDropped, name, r() % 4);
            if (uint32_t(m->mCurrentDevice) >= count) m->mCurrentDevice = int32_t(index);
            break;
        }
        case 5:
            if (!m->mSettings.mObject) break;
            Call<void(__thiscall *)(void *, const char *, uint32_t)>(ctx, 0xcd5110, 0, kDropped, name, r() % 4);
            break;
        case 6: {
            uint32_t out = 0xdeadbeef;
            Call<void(__thiscall *)(void *, const char *, uint32_t *)>(ctx, 0xcd1560, 0, kDropped, name, &out);
            b.push_back(out);
            break;
        }
        case 7: {
            uint32_t out = 0xdeadbeef;
            Call<void(__thiscall *)(void *, const char *, uint32_t *)>(ctx, 0xcd1650, 0, kDropped, name, &out);
            b.push_back(out);
            break;
        }
        case 8: {
            // Never for the option without values (the original reads past its values).
            const char *option = kNames[r() % 5];
            AnyUint32 out{GameAddress<const void>(gzany::kUint32VTable), 0x5555};
            b.push_back(Call<uint8_t(__thiscall *)(void *, const char *, const char *, void *)>(
                ctx, 0xcd1740, 0, kDropped, option, kSettingNames[r() % 5], &out));
            b.push_back(out.mValue);
            break;
        }
        case 9: {
            const unsigned pick = r() % 3;
            void *listener = pick == 0 ? nullptr : pick == 1 ? &F(kListenerA) : &F(kListenerB);
            Call<void(__thiscall *)(void *, void *)>(ctx, 0xcd4ec0, 0, kDropped, listener);
            break;
        }
        case 10:
            Call<void(__thiscall *)(void *, const char *, uint32_t)>(ctx, 0xcd1a10, 0, kDropped, name, r());
            break;
        case 11: {
            const uint32_t ids[] = {Mgr::kInterfaceId, 1, 0x12345, Mgr::kClassId};
            const unsigned face = r() % 2;
            void *out = reinterpret_cast<void *>(uintptr_t(0x77));
            const uint32_t id = ids[r() % 4];
            const uint32_t rva = face ? 0xcd3ce7 : 0xcd3cc0;
            void *adapter = kDropped;
            b.push_back(Call<uint8_t(__thiscall *)(void *, uint32_t, void **)>(ctx, rva, face, adapter, id, &out));
            b.push_back(svcfx::NormOut(shape::P(out), 0x77)); // left as set, or the object
            break;
        }
        case 12:
            if (!m->mSettings.mObject || !count) break;
            m->mCurrentDevice = int32_t(index);
            Call<void(__thiscall *)(void *, const char *, uint32_t)>(ctx, 0xcd5110, 0, kDropped, kNames[r() % 5], r() % 3);
            break;
        default:
            if (!m->mSettings.mObject) break;
            Call<void(__thiscall *)(void *, int32_t)>(ctx, 0xccf880, 0, kDropped, int32_t(index));
            break;
        }
        DumpManager(b, m);
    }
    if (withShutdown) {
        b.push_back(Call<uint8_t(__thiscall *)(void *)>(ctx, 0xcd5540, 0, kDropped));
        DumpManager(b, m);
        for (Device *d : gDevices) b.push_back(frees.Freed(d));
    }
    frees.Stop();
    // Every set's count and contents at the end, and the fakes.
    DumpSet(b, gSettings);
    DumpSet(b, gNextSettings);
    svcfx::CaptureFakes(b, kValueKinds);
    b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    b.push_back(gTrapHits);

    // Teardown (not compared): the records, the manager, the sets.
    if (!withShutdown)
        for (Device *d : gDevices) delete d;
    m->mDevices.clear();
    m->mSettings.mObject = nullptr;
    m->mListener.mObject = nullptr;
    ctx.Original<void(__thiscall *)(void *)>(0xccea70)(gBox.bytes);
    delete gSettings;
    delete gNextSettings;
    return b;
}

void Compare(difftest::Context &ctx, const char *what, uint32_t seed, const Blob &a, const Blob &b)
{
    if (a == b) return;
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    ctx.Fail("%s seed %u route %d: differs at %zu of %zu/%zu (orig %08x ours %08x)", what, seed, gRoute, i, a.size(), b.size(),
             i < a.size() ? a[i] : 0, i < b.size() ? b[i] : 0);
}

} // namespace

DIFF_TEST(ConfigManager_Slots, 0xcd5110)
{
    HeapAllocatorScope allocator(ctx);
    svcfx::Globals globals;
    globals.Set(kFrameworkCell, &F(kFramework)); // the service object Init asks (GetServiceObject reads this cell)
    Cells cells;
    for (uint32_t seed = 0; seed < 400; ++seed) {
        const bool shutdown = seed % 3 == 0;
        gRoute = 0;
        gTrapHits = 0;
        const Blob original = RunSlots(ctx, seed, shutdown);
        for (int route = 1; route <= 2; ++route) {
            gRoute = route;
            gTrapHits = 0;
            const Blob ours = RunSlots(ctx, seed, shutdown);
            Compare(ctx, "slots", seed, original, ours);
            ++ctx.cases;
        }
    }
    gRoute = 0;
    allocator.Check("slots");
}

// ---- the device record's helpers, called directly ----

namespace {

Blob RunDevice(difftest::Context &ctx, uint32_t seed, bool original)
{
    std::mt19937 r(seed * 977 + 3);
    svcfx::ResetAll();
    SetPrefix(r());
    Blob b;
    Device *d = MakeDevice(r, r() % 5, false);
    Device *other = MakeDevice(r, 7, false);
    cGZPropertySetString *target = new cGZPropertySetString;
    Prefill(r, *target);
    const unsigned steps = 1 + r() % 6;
    for (unsigned step = 0; step < steps; ++step) {
        unsigned op = r() % 7;
        b.push_back(evMark);
        b.push_back(op);
        switch (op) {
        case 6: {
            // The script's fill, on both sides: the boolean property command's reallocating insert (0xccd500, called by
            // 0xccfca0), which copies the name with 0x315060 into an allocator-service block and only the value's low
            // byte (the rest is the new block's: booleans are dumped and applied by that byte). The record's destructor
            // below then frees the name: through the service on both sides, or the allocator scope counts a wrong free.
            if (d->mOptions.empty() || d->mOptions[0].mValues.empty()) break;
            Value &v = d->mOptions[0].mValues[r() % d->mOptions[0].mValues.size()];
            std::vector<Setting> *list = &v.mBooleans;
            const Setting source{kSettingNames[r() % 5], r() % 7};
            Setting *where = list->data() + (list->empty() ? 0 : r() % (list->size() + 1));
            ctx.Original<void *(__thiscall *)(void *, Setting *, const Setting *)>(0xccd500)(list, where, &source);
            break;
        }
        case 0: {
            if (d->mOptions.empty() || d->mOptions[0].mValues.empty()) break;
            const Value *v = &d->mOptions[0].mValues[r() % d->mOptions[0].mValues.size()];
            if (original) ctx.Original<void(__thiscall *)(const void *, void *)>(0xccefa0)(v, target);
            else v->ApplyTo(target);
            break;
        }
        case 1: {
            const bool saved = r() % 2;
            if (original) ctx.Original<void(__thiscall *)(void *, uint32_t)>(0xccf4f0)(d, saved ? 1u : 0u);
            else d->ApplyOptions(saved);
            break;
        }
        case 2:
            if (original) ctx.Original<void(__thiscall *)(const void *, void *)>(0xcd47d0)(d, target);
            else d->SetDeviceNumber(target);
            break;
        case 3:
            if (original) ctx.Original<void(__thiscall *)(void *, void *)>(0xcd5490)(d, target);
            else d->CopyAppliedSettings(target);
            break;
        case 4:
            if (original) ctx.Original<void(__thiscall *)(void *, void *)>(0xcd4f00)(d, target);
            else d->CopyScriptVariables(target);
            break;
        default:
            if (original) ctx.Original<void(__cdecl *)(void *, void *)>(0xccf930)(&other->mApplied, &d->mVariables);
            else tsconfig::CopyProperties(&other->mApplied, &d->mVariables);
            break;
        }
        DumpDevice(b, d);
        DumpSet(b, target);
    }
    // The record's destructor: the sizes of the blocks it frees.
    {
        difftest::FreeRecorder frees;
        if (original) ctx.Original<void(__thiscall *)(void *)>(0xcce860)(d);
        else d->~Device();
        frees.Stop();
        size_t sizes[256];
        const size_t n = frees.SortedSizes(sizes, 256);
        b.push_back(uint32_t(n));
        for (size_t i = 0; i < n; ++i) b.push_back(uint32_t(sizes[i]));
    }
    ::operator delete(d, sizeof(Device));
    delete other;
    delete target;
    return b;
}

} // namespace

DIFF_TEST(ConfigManager_Device, 0xccf4f0)
{
    HeapAllocatorScope allocator(ctx);
    Cells cells;
    for (uint32_t seed = 0; seed < 600; ++seed) {
        const Blob original = RunDevice(ctx, seed, true);
        const Blob ours = RunDevice(ctx, seed, false);
        gRoute = 0;
        Compare(ctx, "device", seed, original, ours);
        allocator.Check("device");
        ++ctx.cases;
    }
}

// ---- construction, destruction, the factory ----

namespace {

// The manager's bytes with the vtable words coded: the two faces, the cRZString and cRZTextFile vtables (the game's on both
// sides: hook-shaped classes), the overrides set's (the game's or ours).
Blob Bytes(const uint8_t *object)
{
    Blob b;
    for (uint32_t off = 0; off < sizeof(Mgr); off += 4) {
        uint32_t w;
        std::memcpy(&w, object + off, 4);
        if (off == 0 || off == 4) {
            const void *word = reinterpret_cast<const void *>(uintptr_t(w));
            const unsigned f = off / 4;
            w = (word == GameVt(f) || word == gOurs[f]) ? 0x7d00 + f : w;
        } else if (off == 0x14c) {
            const void *word = reinterpret_cast<const void *>(uintptr_t(w));
            static void *ourSetVt = [] {
                cGZPropertySetString s;
                return shape::VTableAt(&s, 0);
            }();
            w = (word == GameAddress<void>(cGZPropertySetString::kOriginalVTable) || word == ourSetVt) ? 0x5e70 : w;
        }
        b.push_back(w);
    }
    return b;
}

// Fills a built manager with heap state for the destructor: strings, options, overrides, counted fakes, a record vector.
void FillForDestructor(std::mt19937 &r, Mgr *m)
{
    m->mLogPath.mString.FromChar(r() % 2 ? "short" : "a log path longer than sixteen characters");
    m->mLine.mString.FromChar(r() % 2 ? "" : "another line longer than sixteen characters");
    const unsigned options = r() % 3;
    for (unsigned i = 0; i < options; ++i) {
        Option o;
        o.mName = kNames[r() % 5];
        o.mValues.resize(r() % 3);
        for (Value &v : o.mValues) FillValue(r, v);
        o.mSaved = 0;
        o.mCurrent = 0;
        m->mOptions.push_back(std::move(o));
    }
    Prefill(r, m->mOverrides);
    if (r() % 2) m->mSettings.mObject = reinterpret_cast<cIGZPropertySetString *>(&F(kListenerA));
    if (r() % 2) m->mListener.mObject = reinterpret_cast<cIGZUnknown *>(&F(kListenerB));
    const unsigned records = r() % 4;
    for (unsigned i = 0; i < records; ++i) m->mDevices.push_back(reinterpret_cast<Device *>(uintptr_t(0x1000 + i)));
}

} // namespace

DIFF_TEST(ConfigManager_Lifetime, 0xccdf90)
{
    HeapAllocatorScope allocator(ctx);
    LearnOurs();
    // The constructor: the original's bytes against ours, on a buffer filled alike. The words the original's constructor
    // leaves as they were (they differ between fills) are what the factory's heap block held: not compared there.
    std::vector<bool> untouched(sizeof(Mgr) / 4, false);
    Blob firstFill;
    for (uint8_t fill : {uint8_t(0xcd), uint8_t(0x00), uint8_t(0x5a)}) {
        alignas(8) static uint8_t a[sizeof(Mgr)], b[sizeof(Mgr)];
        std::memset(a, fill, sizeof(a));
        std::memset(b, fill, sizeof(b));
        ctx.Original<void *(__thiscall *)(void *)>(0xccdf90)(a);
        Mgr *ours = ::new (b) Mgr;
        if (Bytes(a) != Bytes(b)) ctx.Fail("constructor bytes differ (fill %02x)", fill);
        const Blob words = Bytes(a);
        if (firstFill.empty()) firstFill = words;
        for (size_t i = 0; i < words.size(); ++i) untouched[i] = untouched[i] || words[i] != firstFill[i];
        ctx.Original<void(__thiscall *)(void *)>(0xccea70)(a);
        ours->~Mgr();
        ++ctx.cases;
    }
    // The factory: a new object of the size, built; freed through slot 3 of the +4 face.
    {
        void *orig = ctx.Original<void *(__cdecl *)()>(0xcd0360)();
        Mgr *ours = Mgr::Create();
        if (!orig || !ours) ctx.Fail("factory returned null");
        else {
            const Blob x = Bytes(static_cast<uint8_t *>(orig));
            const Blob y = Bytes(reinterpret_cast<uint8_t *>(ours));
            Blob xs = x, ys = y;
            for (size_t i = 0; i < xs.size(); ++i)
                if (untouched[i]) xs[i] = ys[i] = 0;
            for (size_t i = 0; i < xs.size(); ++i)
                if (xs[i] != ys[i]) {
                    ctx.Fail("factory object bytes differ at +0x%zx (orig %08x ours %08x)", i * 4, xs[i], ys[i]);
                    break;
                }
            difftest::FreeRecorder frees;
            ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xccef62)(static_cast<uint8_t *>(orig) + 4, 1);
            frees.Stop();
            const bool origFreed = frees.Freed(orig);
            difftest::FreeRecorder frees2;
            rz::cRZUnknown *face = ours;
            VirtualCall<void *(__thiscall *)(void *, uint32_t), 3>(face, 1u);
            frees2.Stop();
            if (origFreed != frees2.Freed(ours) || !origFreed) ctx.Fail("factory object not freed alike");
        }
        ++ctx.cases;
    }
    // The destructor on a filled object: the original's deleting destructor 0xccef70 (flags 0), ours through slot 3 of our
    // +4 face (the deleting destructor; 0xccef62 is the game's adjustor to 0xccef70) on our object and on a game-built one.
    // Compared: the freed sizes, the fakes' counts and events.
    for (uint32_t seed = 0; seed < 120; ++seed) {
        Blob results[3];
        for (int side = 0; side < 3; ++side) {
            std::mt19937 r(seed * 31 + 5);
            svcfx::ResetAll();
            alignas(8) static uint8_t buffer[sizeof(Mgr)];
            std::memset(buffer, 0xcd, sizeof(buffer));
            if (side == 1) ::new (buffer) Mgr;
            else ctx.Original<void *(__thiscall *)(void *)>(0xccdf90)(buffer);
            Mgr *m = reinterpret_cast<Mgr *>(buffer);
            FillForDestructor(r, m);
            difftest::FreeRecorder frees;
            switch (side) {
            case 0: ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xccef70)(buffer, 0); break;
            case 1: VirtualCall<void *(__thiscall *)(void *, uint32_t), 3>(static_cast<rz::cRZUnknown *>(m), 0u); break;
            default: // our +4 face's slot 3 on the game-built object
                reinterpret_cast<void *(__thiscall *)(void *, uint32_t)>(gOurs[1][3])(buffer + 4, 0u);
                break;
            }
            frees.Stop();
            Blob &out = results[side];
            size_t sizes[256];
            const size_t n = frees.SortedSizes(sizes, 256);
            out.push_back(uint32_t(n));
            for (size_t i = 0; i < n; ++i) out.push_back(uint32_t(sizes[i]));
            svcfx::CaptureFakes(out);
            out.insert(out.end(), svcfx::gEv.begin(), svcfx::gEv.end());
        }
        for (int side = 1; side < 3; ++side)
            if (results[side] != results[0]) ctx.Fail("destructor seed %u side %d differs", seed, side);
        ++ctx.cases;
    }
    allocator.Check("lifetime");
}

// ---- the detection helpers and the active device ----
//
// A manager (ours, built alike on both sides: the functions read and write data members only) with records that carry the
// detection's results: 17 machine strings (11 the driver text, 12 the card name, 13 the PCI text the ids are read from), 16
// numbers, a device fake whose slot 16 hands an info fake whose slot 22 hands a property set (the software flag in it as a
// boolean, a uint32 or absent). Random sequences of ResetForDetection 0xccf290, BuildDescription 0xcd4f20,
// RestoreSavedOptions 0xcd3cf0, Description 0xcd1550, SetActiveDevice 0xcd4880 and RestoreActiveDevice 0xcd2a80, the
// original by RVA on one side, our members on the other; after each, every record's fields, the manager's active device and
// flag, the settings set and the fakes' events.
//
// RestoreActiveDevice's saved sets are the settings set SetActiveDevice wrote, or one built here: the device number (a
// sint32, a uint32 or absent) and the list text ("count;" then one ';'-terminated segment per device: a record's
// description, or six ';'-separated fields "number;card name;...", the description's shape; the image's field delimiter
// is ';' as well). Inputs stay inside what the original survives:
// - the number stays in [0, count) whenever the list's count parses: past it the separator search falls through to the
//   ORIGINAL-BUG path (the position left at a stack address), where both sides throw from the substring (different
//   exception objects);
// - every segment has six fields, so the number field the original scans with a string conversion into a 4-byte integer
//   (ORIGINAL-BUG, src: ScanTokenInto) is at most 3 characters: a longer one overwrites the original's caller's stack.

namespace {

constexpr unsigned kDeviceFake = 3; // fakes 3..6 the devices, 7..10 their info objects
constexpr unsigned kInfoFake = 7;
constexpr unsigned kRecords = 4;
const char *const kCards[] = {"Card One", "card one", "Other;Card", "CARD TWO", ""};
// Descriptions in BuildDescription's shape: six fields, the first a short number (see MakeSaved).
const char *const kDescriptions[] = {"1;Card One;10de;0421;3;drv", "0;a card name longer than sixteen;0;0;0;", "2; x;1;1;1;d"};

struct DetectWorld {
    Mgr *m = nullptr;
    alignas(8) uint8_t buffer[sizeof(Mgr)];
    cGZPropertySetString *props[kRecords] = {};
    cGZPropertySetString *saved = nullptr;
};

void PushMachineString(Device *d, const std::string &text)
{
    tsconfig::StringMember s;
    s.mString.FromChar(text.c_str());
    d->mMachine.PushBackAny(s.mString);
}

std::string HexText(std::mt19937 &r)
{
    static const char kChars[] = "0123456789abcdefABCDEFxz&_";
    const unsigned length = r() % 4 == 0 ? r() % 0x1b : 0x1b + r() % 12;
    std::string s;
    for (unsigned i = 0; i < length; ++i) s.push_back(kChars[r() % (r() % 5 ? 22 : 26)]);
    return s;
}

void BuildDetectWorld(std::mt19937 &r, DetectWorld &w)
{
    w.m = ::new (w.buffer) Mgr;
    Mgr *m = w.m;
    m->mActiveDevice = int32_t(r() % 5) - 1;
    m->mActiveSoftware = uint8_t(r() % 2);
    auto *settings = new cGZPropertySetString;
    settings->mRefCount = 1;
    Prefill(r, *settings);
    m->mSettings.mObject = settings;
    const unsigned count = 1 + r() % kRecords;
    for (unsigned i = 0; i < count; ++i) {
        Device *d = MakeDevice(r, i, false);
        for (unsigned k = 0; k < 17; ++k) {
            std::string text = "m" + std::to_string(k);
            if (k == 11) text = r() % 3 ? "driver 1.2" : "a driver text longer than sixteen";
            if (k == 12) text = kCards[r() % 5];
            if (k == 13) text = HexText(r);
            PushMachineString(d, text);
        }
        for (unsigned k = 0; k < 16; ++k) d->mNumbers.push_back(float(int32_t(r() % 20000) - 2000) / 7.0f);
        d->mUnknown2c = int32_t(r());
        d->mUnknown30 = int32_t(r());
        d->mVendorId = r();
        d->mDeviceId = r();
        d->mCardFound = uint8_t(r() % 2);
        d->mText40 = r() % 2 ? "t40" : "";
        d->mCardVendor = r() % 2 ? "vendor text longer than sixteen" : "v";
        d->mCardName = r() % 2 ? "name" : "";
        d->mText88 = "old 88";
        d->mDescription = kDescriptions[r() % 3];
        const unsigned pick = r() % 4;
        d->mDevice = pick == 0 ? nullptr : &F(kDeviceFake + i);
        auto *props = new cGZPropertySetString;
        props->mRefCount = 1;
        GZServiceString key(Cells::kCellNames[2]);
        if (r() % 3 == 0) {
            AnyBoolean flag{GameAddress<const void>(gzany::kBooleanVTable), uint8_t(r() % 2), {}};
            props->cGZPropertySetString::SetProperty(&key, &flag);
        } else if (r() % 2) {
            AnyUint32 flag{GameAddress<const void>(gzany::kUint32VTable), r() % 3};
            props->cGZPropertySetString::SetProperty(&key, &flag);
        }
        w.props[i] = props;
        F(kDeviceFake + i).Set(16, 0, &F(kInfoFake + i));
        F(kInfoFake + i).Set(22, 0, props);
        m->mDevices.push_back(d);
    }
}

// A saved set for RestoreActiveDevice (see above).
cGZPropertySetString *MakeSaved(std::mt19937 &r, const Mgr *m)
{
    auto *set = new cGZPropertySetString;
    set->mRefCount = 1;
    const unsigned records = unsigned(m->mDevices.size());
    const unsigned count = r() % 6 == 0 ? 0 : 1 + r() % (records + 1);
    const int32_t number = count ? int32_t(r() % count) : int32_t(r() % 3);
    const unsigned numberKind = r() % 6;
    if (numberKind != 0) {
        GZServiceString key(Cells::kCellNames[0]);
        if (numberKind == 1) {
            AnyUint32 any{GameAddress<const void>(gzany::kUint32VTable), uint32_t(number)};
            set->cGZPropertySetString::SetProperty(&key, &any);
        } else {
            AnySint32 any{GameAddress<const void>(gzany::kSint32VTable), number};
            set->cGZPropertySetString::SetProperty(&key, &any);
        }
    }
    const unsigned listKind = r() % 8;
    if (listKind != 0) {
        std::string text = listKind == 1 ? "x" : std::to_string(count);
        text.push_back(';');
        for (unsigned i = 0; i < count; ++i) {
            const unsigned kind = r() % 3;
            const Device *d = m->mDevices[r() % records];
            if (kind == 0) text += d->mDescription;
            else {
                // Six fields like a description; the number field 0 to 3 characters (see ScanTokenInto). A control character
                // is the only text that scans to a small index (the original's index branch).
                const char *const numbers[] = {"0", "1", "2", "3", " 1", "12x", "", "\x01", "\x02", "\x03", "\x01\x01"};
                text += numbers[r() % 11];
                text.push_back(';');
                const std::string card = kCards[r() % 5];
                text += kind == 1 ? card.substr(0, card.find(';')) : std::string("unknown card");
                text += ";a;b;c;";
                text += r() % 2 ? "driver" : "";
            }
            text.push_back(';');
        }
        GZServiceString key(Cells::kCellNames[1]);
        if (listKind == 2) {
            AnyUint32 any{GameAddress<const void>(gzany::kUint32VTable), 5};
            set->cGZPropertySetString::SetProperty(&key, &any);
        } else {
            AnyString any;
            any.Construct(text.c_str());
            set->cGZPropertySetString::SetProperty(&key, &any);
            any.Destruct();
        }
    }
    return set;
}

void DumpRecord(Blob &b, const Device *d)
{
    DumpDevice(b, d);
    b.push_back(uint32_t(d->mUnknown2c));
    b.push_back(uint32_t(d->mUnknown30));
    b.push_back(d->mVendorId);
    b.push_back(d->mDeviceId);
    b.push_back(d->mCardFound);
    for (const std::string *s : {&d->mText40, &d->mCardVendor, &d->mCardName, &d->mText88, &d->mDescription})
        PushText(b, s->c_str());
    b.push_back(uint32_t(d->mMachine.Count()));
}

Blob RunDetect(difftest::Context &ctx, uint32_t seed, bool original)
{
    std::mt19937 r(seed * 7919 + 11);
    svcfx::ResetAll();
    SetPrefix(r());
    DetectWorld w;
    BuildDetectWorld(r, w);
    Mgr *m = w.m;
    Blob b;
    const unsigned steps = 1 + r() % 8;
    for (unsigned step = 0; step < steps; ++step) {
        const unsigned op = r() % 6;
        Device *d = m->mDevices[r() % m->mDevices.size()];
        b.push_back(evMark);
        b.push_back(op);
        switch (op) {
        case 0:
            if (original) ctx.Original<void(__thiscall *)(void *)>(0xccf290)(d);
            else d->ResetForDetection();
            break;
        case 1:
            if (original) ctx.Original<void(__thiscall *)(void *)>(0xcd4f20)(d);
            else d->BuildDescription();
            break;
        case 2: {
            // The settings set (option keys SetOptionValue-style are in it after ApplyOptions) or a fresh one.
            cGZPropertySetString *saved = new cGZPropertySetString;
            saved->mRefCount = 1;
            Prefill(r, *saved);
            const unsigned keys = r() % 5;
            for (unsigned k = 0; k < keys; ++k) {
                std::string name = r() % 4 ? std::string(OptionPrefixText()) : std::string("zz");
                if (r() % 3 == 0)
                    for (char &c : name) c = char(std::toupper(uint8_t(c)));
                name += kNames[r() % 6];
                GZServiceString key(name.c_str());
                if (r() % 4) {
                    AnyUint32 any{GameAddress<const void>(gzany::kUint32VTable), r() % 4};
                    saved->cGZPropertySetString::SetProperty(&key, &any);
                } else {
                    AnySint32 any{GameAddress<const void>(gzany::kSint32VTable), 3};
                    saved->cGZPropertySetString::SetProperty(&key, &any);
                }
            }
            if (original) ctx.Original<void(__thiscall *)(void *, void *)>(0xcd3cf0)(d, saved);
            else d->RestoreSavedOptions(saved);
            DumpSet(b, saved);
            delete saved;
            break;
        }
        case 3: {
            const std::string *text = original ? ctx.Original<std::string *(__thiscall *)(void *)>(0xcd1550)(d)
                                               : d->Description();
            b.push_back(text == &d->mDescription);
            break;
        }
        case 4: {
            const unsigned pick = r() % (kRecords + 1);
            void *device = pick == kRecords ? reinterpret_cast<void *>(uintptr_t(0x4321)) : &F(kDeviceFake + pick);
            if (original) ctx.Original<void(__thiscall *)(void *, void *)>(0xcd4880)(m, device);
            else m->cTSConfigurationManager::SetActiveDevice(device); // adapter dropped: the member
            break;
        }
        default: {
            cGZPropertySetString *saved = r() % 3 == 0 ? nullptr : MakeSaved(r, m);
            cIGZPropertySetString *from = saved ? saved : m->mSettings.mObject;
            if (original) ctx.Original<void(__thiscall *)(void *, void *)>(0xcd2a80)(m, from);
            else m->RestoreActiveDevice(from);
            b.push_back(uint32_t(m->mActiveDevice));
            b.push_back(m->mActiveSoftware);
            delete saved;
            break;
        }
        }
        for (const Device *each : m->mDevices) DumpRecord(b, each);
        b.push_back(uint32_t(m->mActiveDevice));
        b.push_back(m->mActiveSoftware);
        DumpSet(b, static_cast<const cGZPropertySetString *>(m->mSettings.mObject));
    }
    svcfx::CaptureFakes(b);
    b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    for (Device *each : m->mDevices) delete each;
    m->mDevices.clear();
    m->~Mgr();
    for (cGZPropertySetString *props : w.props) delete props;
    return b;
}

} // namespace

DIFF_TEST(ConfigManager_Detection, 0xcd2a80)
{
    HeapAllocatorScope allocator(ctx);
    Cells cells;
    for (uint32_t seed = 0; seed < 800; ++seed) {
        const Blob original = RunDetect(ctx, seed, true);
        const Blob ours = RunDetect(ctx, seed, false);
        gRoute = 0;
        Compare(ctx, "detect", seed, original, ours);
        ++ctx.cases;
    }
    allocator.Check("detect");
}

// ---- slot 5: the detection and configuration ----
//
// The Slots world (Build: a manager with records, a settings set, the framework fake), run through slot 5 0xcd4270 on the
// three routes. The framework (service object cell 0x10c9378) hands the next settings set to Init and a device enumerator
// fake (slot 23 the count, 24 a device fake); each device fake's slot 16 hands an info fake whose slot 22 hands a property
// set; slot 25 (release) is logged. The three original callees still called (DetectProcessor 0xcd0600, the configuration
// script 0xcd3550, WriteConfigLog 0xcd2190: registry, script files and the log file behind them) are redirected on both
// sides to fakes that log their arguments and, from one generator seeded alike on each side, fill the record the way their
// originals' results are read: 17 machine strings and 16 numbers (DetectProcessor), options with values, the error code
// and the software flag in the record's variables (the script; it never throws, see FakeScript). The next settings
// set carries option keys, the software flag and a saved device number and list. Compared after the call: the manager
// (records with every field, sets), the old records freed (Shutdown, through slot 4), each info set, and the event logs.

namespace {

constexpr unsigned kEnumFake = 3;     // the device enumerator
constexpr unsigned kDevFake = 4;      // 4..7 the devices
constexpr unsigned kDevInfoFake = 8;  // 8..11 their info objects
constexpr unsigned kMaxDevices = 4;
enum : uint32_t { evDetect = 0xd0, evScript = 0xd1, evLog = 0xd2 };

std::mt19937 gFakeRng;
cGZPropertySetString *gInfoSets[kMaxDevices];

// A 5-byte jump from an original function's entry to a fake, for the life of the object.
struct EntryRedirect {
    uint8_t *site;
    uint8_t saved[5];
    DWORD protection;
    EntryRedirect(uint32_t rva, const void *target) : site(GameAddress<uint8_t>(rva))
    {
        std::memcpy(saved, site, 5);
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &protection);
        const int32_t relative = int32_t(reinterpret_cast<uintptr_t>(target) - reinterpret_cast<uintptr_t>(site) - 5);
        site[0] = 0xe9;
        std::memcpy(site + 1, &relative, 4);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    ~EntryRedirect()
    {
        std::memcpy(site, saved, 5);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
        DWORD ignored;
        VirtualProtect(site, 5, protection, &ignored);
    }
    EntryRedirect(const EntryRedirect &) = delete;
    EntryRedirect &operator=(const EntryRedirect &) = delete;
};

// 0xcd0600 (__thiscall on the record: index, device).
void __fastcall FakeDetect(Device *record, void *, int32_t index, void *device)
{
    svcfx::Ev(evDetect);
    svcfx::Ev(uint32_t(index));
    svcfx::Ev(svcfx::Norm(shape::P(device)));
    std::mt19937 &r = gFakeRng;
    for (unsigned k = 0; k < 17; ++k) {
        std::string text = "m" + std::to_string(k);
        if (k == 11) text = r() % 3 ? "driver 1.2" : "a driver text longer than sixteen";
        if (k == 12) text = kCards[r() % 5];
        if (k == 13) text = HexText(r);
        PushMachineString(record, text);
    }
    for (unsigned k = 0; k < 16; ++k) record->mNumbers.push_back(float(int32_t(r() % 20000) - 2000) / 7.0f);
    record->mFlags = r() % 16;
}

// 0xcd3550 (__thiscall on the manager: the record).
void __fastcall FakeScript(Mgr *, void *, Device *record)
{
    svcfx::Ev(evScript);
    svcfx::Ev(record->mIndex);
    std::mt19937 &r = gFakeRng;
    const unsigned options = r() % 4;
    for (unsigned i = 0; i < options; ++i) {
        Option o;
        o.mName = kNames[r() % 5];
        // Three values always: RestoreSavedOptions takes the saved index (here 0-2) unchecked, and ApplyOptions then reads it.
        const unsigned values = 3;
        o.mValues.resize(values);
        for (Value &v : o.mValues) FillValue(r, v);
        o.mSaved = r() % values;
        o.mCurrent = r() % values;
        record->mOptions.push_back(std::move(o));
    }
    const unsigned code = r() % 5;
    if (code < 4) {
        GZServiceString key(Cells::kCellNames[3]);
        AnyUint32 any{GameAddress<const void>(gzany::kUint32VTable), code < 2 ? 0u : code - 1};
        record->mVariables.cGZPropertySetString::SetProperty(&key, &any);
    }
    if (r() % 3) {
        GZServiceString key(Cells::kCellNames[2]);
        AnyBoolean any{GameAddress<const void>(gzany::kBooleanVTable), uint8_t(r() % 2), {}};
        record->mVariables.cGZPropertySetString::SetProperty(&key, &any);
    }
    // Never throws: an exception thrown here does not unwind through the original's frames in the harness (it leaves the
    // process as an unhandled 0xe06d7363), so the catch (...) around the script is untested.
}

// 0xcd2190 (__thiscall on the manager).
void __fastcall FakeLog(Mgr *m, void *)
{
    svcfx::Ev(evLog);
    svcfx::Ev(uint32_t(m->mDevices.size()));
}

// The framework's slot 5 (clsid, iid, out): the next settings set for Init (when value[0]), the enumerator for its clsid.
uint32_t DetectFrameworkQuery(svcfx::Obj *self, uint32_t *args)
{
    void **out = reinterpret_cast<void **>(uintptr_t(args[2]));
    if (args[0] == 0x85522803) {
        *out = &F(kEnumFake);
        ++F(kEnumFake).refs;
        return 1;
    }
    if (!self->value[0]) return 0;
    *out = gNextSettings;
    gNextSettings->AddRef();
    return 1;
}
// The enumerator's slot 24 (index): that device's fake.
uint32_t EnumDeviceAt(svcfx::Obj *, uint32_t *args) { return svcfx::P(&F(kDevFake + args[0])); }

// The next settings set's contents for slot 5: option keys, the software flag, a saved device number and list (six-field
// segments, the number field short: see RunDetect's notes on what the original survives).
void FillNextSettings(std::mt19937 &r, unsigned devices)
{
    cGZPropertySetString &set = *gNextSettings;
    const unsigned keys = r() % 4;
    for (unsigned k = 0; k < keys; ++k) {
        std::string name = std::string(OptionPrefixText()) + kNames[r() % 5];
        GZServiceString key(name.c_str());
        AnyUint32 any{GameAddress<const void>(gzany::kUint32VTable), r() % 3};
        set.cGZPropertySetString::SetProperty(&key, &any);
    }
    if (r() % 3) {
        GZServiceString key(Cells::kCellNames[2]);
        AnyBoolean any{GameAddress<const void>(gzany::kBooleanVTable), uint8_t(r() % 2), {}};
        set.cGZPropertySetString::SetProperty(&key, &any);
    }
    if (r() % 4 == 0) return;
    const unsigned count = 1 + r() % (devices + 1);
    {
        GZServiceString key(Cells::kCellNames[0]);
        AnySint32 any{GameAddress<const void>(gzany::kSint32VTable), int32_t(r() % count)};
        set.cGZPropertySetString::SetProperty(&key, &any);
    }
    std::string text = std::to_string(count) + ";";
    for (unsigned i = 0; i < count; ++i) {
        const char *const numbers[] = {"0", "1", "\x01", "\x02", ""};
        text += numbers[r() % 5];
        text += ";";
        const std::string card = kCards[r() % 5];
        text += card.substr(0, card.find(';'));
        text += ";a;b;c;d;";
    }
    GZServiceString key(Cells::kCellNames[1]);
    AnyString any;
    any.Construct(text.c_str());
    set.cGZPropertySetString::SetProperty(&key, &any);
    any.Destruct();
}

Blob RunDetectAll(difftest::Context &ctx, uint32_t seed)
{
    Build(ctx, seed);
    Mgr *m = M();
    std::mt19937 r(seed ^ 0xde7ec7u);
    F(kFramework).Set(5, "ddp", 0, DetectFrameworkQuery);
    F(kFramework).value[0] = 1; // without a settings set the original's RestoreActiveDevice reads through null
    const unsigned devices = r() % (kMaxDevices + 1);
    F(kEnumFake).Set(23, 0, devices);
    F(kEnumFake).Set(24, "d", 0, EnumDeviceAt);
    F(kEnumFake).refs = 0;
    for (unsigned i = 0; i < kMaxDevices; ++i) {
        gInfoSets[i] = new cGZPropertySetString;
        gInfoSets[i]->mRefCount = 1;
        Prefill(r, *gInfoSets[i]);
        F(kDevFake + i).Set(16, 0, &F(kDevInfoFake + i));
        F(kDevFake + i).Set(25, 0);
        F(kDevInfoFake + i).Set(22, 0, gInfoSets[i]);
    }
    FillNextSettings(r, devices);
    m->mActiveDevice = int32_t(r() % (kMaxDevices + 1)) - 1; // RestoreActiveDevice sets it anyway
    gFakeRng.seed(seed * 31 + 1);

    Blob b;
    difftest::FreeRecorder frees;
    Call<void(__thiscall *)(void *)>(ctx, 0xcd4270, 0, kDropped);
    frees.Stop();
    for (Device *d : gDevices) b.push_back(frees.Freed(d));
    DumpManager(b, m);
    for (const Device *d : m->mDevices) {
        DumpRecord(b, d);
        b.push_back(d->mIndex);
        b.push_back(svcfx::Norm(shape::P(d->mDevice)));
        b.push_back(d->mFlags);
        b.push_back(uint32_t(d->mNumbers.size()));
    }
    b.push_back(uint32_t(m->mActiveDevice));
    b.push_back(m->mActiveSoftware);
    for (const cGZPropertySetString *set : gInfoSets) DumpSet(b, set);
    DumpSet(b, gSettings);
    DumpSet(b, gNextSettings);
    svcfx::CaptureFakes(b, kValueKinds);
    b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    b.push_back(gTrapHits);

    // Teardown (not compared): the new records (Shutdown deleted the old), the manager, the sets.
    for (Device *d : m->mDevices) delete d;
    m->mDevices.clear();
    m->mSettings.mObject = nullptr;
    m->mListener.mObject = nullptr;
    ctx.Original<void(__thiscall *)(void *)>(0xccea70)(gBox.bytes);
    delete gSettings;
    delete gNextSettings;
    for (cGZPropertySetString *&set : gInfoSets) {
        delete set;
        set = nullptr;
    }
    return b;
}

} // namespace

DIFF_TEST(ConfigManager_DetectAndConfigure, 0xcd4270)
{
    HeapAllocatorScope allocator(ctx);
    svcfx::Globals globals;
    globals.Set(kFrameworkCell, &F(kFramework));
    Cells cells;
    const EntryRedirect detect(0xcd0600, reinterpret_cast<const void *>(&FakeDetect));
    const EntryRedirect script(0xcd3550, reinterpret_cast<const void *>(&FakeScript));
    const EntryRedirect log(0xcd2190, reinterpret_cast<const void *>(&FakeLog));
    for (uint32_t seed = 0; seed < 400; ++seed) {
        gRoute = 0;
        gTrapHits = 0;
        const Blob original = RunDetectAll(ctx, seed);
        for (int route = 1; route <= 2; ++route) {
            gRoute = route;
            gTrapHits = 0;
            const Blob ours = RunDetectAll(ctx, seed);
            Compare(ctx, "slot5", seed, original, ours);
            ++ctx.cases;
        }
    }
    gRoute = 0;
    allocator.Check("slot5");
}
