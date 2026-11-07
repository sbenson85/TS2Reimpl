#include "rz/nGZLog.h"

#include "game/game.h"
#include "game/vtable.h"
#include "rz/StopwatchMore.h"
#include "rz/QueryInterfaces.h"
#include "rz/cRZRefCount.h"

#include <cstring>
#include <ctime>
#include <memory>
#include <new>
#include <stdexcept>
#include <windows.h>

namespace nGZLog {
namespace {

// The game's import slots the log code calls through (RVAs of the IAT entries).
constexpr uint32_t kIatGetCurrentThreadId = 0xd9a194, kIatGetCurrentProcess = 0xd9a1b8, kIatGetSystemInfo = 0xd9a1c4;
constexpr uint32_t kIatLoadLibraryA = 0xd9a128, kIatGetProcAddress = 0xd9a124;
constexpr uint32_t kIatGetSystemTimeAsFileTime = 0xd9a2a4;
constexpr uint64_t kEpochDifference = 0x19db1ded53e8000ull; // 1601 to 1970 in 100 ns ticks
using GetSystemTimeAsFileTimeFn = void(WINAPI *)(LPFILETIME);

using GetCurrentThreadIdFn = DWORD(WINAPI *)();
using GetCurrentProcessFn = HANDLE(WINAPI *)();
using GetSystemInfoFn = void(WINAPI *)(LPSYSTEM_INFO);
using LoadLibraryFn = HMODULE(WINAPI *)(const char *);
using GetProcAddressFn = FARPROC(WINAPI *)(HMODULE, const char *);

using AddRefFn = void(__thiscall *)(void *);
using SetNameFn = void(__thiscall *)(void *, const char *);
using SetFormatFn = void(__thiscall *)(void *, uint32_t, const char *);
using StartFn = void(__thiscall *)(void *, void *, void *);

// operator new (the game's is the library's, 0x824632) then the constructor. Like the original, a
// null result skips the constructor and passes null on.
template <typename T> T *NewConstructed()
{
    void *memory = ::operator new(T::kSize);
    return memory ? static_cast<T *>(memory)->Construct() : nullptr;
}

} // namespace

int64_t __cdecl Time64(int64_t *out)
{
    FILETIME fileTime = {0, 0};
    (*GameAddress<GetSystemTimeAsFileTimeFn>(kIatGetSystemTimeAsFileTime))(&fileTime);
    // 100 ns ticks since 1601 to seconds since 1970 (signed, so earlier times are negative).
    const uint64_t ticks = (static_cast<uint64_t>(fileTime.dwHighDateTime) << 32 | fileTime.dwLowDateTime) - kEpochDifference;
    int64_t seconds = static_cast<int64_t>(ticks) / 10000000;
    // Beyond 0x793582aff seconds (the year 3000) the result is -1.
    if (seconds > 0x793582aff) seconds = -1;
    if (out) *out = seconds;
    return seconds;
}

uint32_t __cdecl HashChars(const char *text)
{
    uint32_t hash = 0;
    for (; *text; ++text) hash = hash * 33 + static_cast<uint32_t>(static_cast<int32_t>(static_cast<signed char>(*text)));
    return hash;
}

size_t LogStringHash::operator()(const LogString &key) const
{
    // The original calls slot 5 of the key's own vtable.
    return HashChars(key.AsInterface()->ToChar());
}

RefHolder::RefHolder(const RefHolder &other) : mObject(other.mObject)
{
    if (mObject) VirtualCall<AddRefFn, 1>(mObject);
}

RefHolder::~RefHolder() noexcept
{
    reinterpret_cast<RZAutoRef *>(this)->ReleaseUnguarded();
}

RefHolder &RefHolder::operator=(const RefHolder &other)
{
    reinterpret_cast<RZUnknownSlot *>(this)->Assign(other.mObject);
    return *this;
}

void __cdecl StoreCurrentThreadId(uint32_t *out)
{
    *out = (*GameAddress<GetCurrentThreadIdFn>(kIatGetCurrentThreadId))();
}

cLogTimestamp *cLogTimestamp::Construct()
{
    mStopwatch.Construct(4);
    mStopwatch.Start();
    uint64_t elapsed = 0;
    stopwatch_more::ElapsedAs64(&mStopwatch, nullptr, &elapsed);
    mElapsedHigh = 0;
    mElapsedLow = static_cast<uint32_t>(elapsed);
    int64_t now = 0;
    _time64(&now);
    mTime = static_cast<uint32_t>(now); // only the low half is kept
    mFlag0 = 1;
    mFlag1 = 1;
    mFlag2 = 0;
    return this;
}

cLogProcessInfo *cLogProcessInfo::Construct()
{
    mProcess = static_cast<uint32_t>(reinterpret_cast<uintptr_t>((*GameAddress<GetCurrentProcessFn>(kIatGetCurrentProcess))()));
    SYSTEM_INFO info;
    (*GameAddress<GetSystemInfoFn>(kIatGetSystemInfo))(&info);
    mProcessors = info.dwNumberOfProcessors;
    // imul with a signed 32-bit processor count.
    mScaled = static_cast<int64_t>(static_cast<int32_t>(info.dwNumberOfProcessors)) * 10000;
    mField0 = 0;
    mField4 = 0;
    mKernelTimeLow = 0;
    mKernelTimeHigh = 0;
    mUserTimeLow = 0;
    mUserTimeHigh = 0;
    mHasProcessTimes = 0;
    bool found = false;
    HMODULE kernel = (*GameAddress<LoadLibraryFn>(kIatLoadLibraryA))("kernel32.dll");
    if (kernel) found = (*GameAddress<GetProcAddressFn>(kIatGetProcAddress))(kernel, "GetProcessTimes") != nullptr;
    mHasProcessTimes = found ? 1 : 0;
    return this;
}

cLogRecord *cLogRecord::Construct()
{
    mVTable = GameAddress(kBaseVTable);
    mUnknown.Construct();
    mVTable = GameAddress(kVTable);
    mUnknown.mVTable = GameAddress(kUnknownVTable);
    mText.Construct();
    mText2.Construct();
    return this;
}

cLogFormatterText *cLogFormatterText::Construct()
{
    mVTable = GameAddress(kBaseVTable);
    mUnknown.Construct();
    mVTable = GameAddress(kVTable);
    mUnknown.mVTable = GameAddress(kUnknownVTable);
    mName.Construct();
    for (cRZString &s : mFormats) s.Construct();
    for (cRZString &s : mPresets) s.Construct();
    mOutput.Construct();
    mScratch.Construct();
    mLock.Construct();
    mTimestamp = nullptr;
    mAliases = nullptr;
    return this;
}

void cLogFormatterText::SetAliasMap(AliasMap *aliases)
{
    mAliases = aliases;
    VirtualCall<SetFormatFn, kSlotSetFormat>(this, 0, *GameAddress<const char *>(cLogService::kNoPresetPointerRva));
}

void cLogFormatterText::SetTimestamp(cLogTimestamp *timestamp)
{
    mTimestamp = timestamp;
}

cLogOutputBackEnd *cLogOutputBackEnd::Construct()
{
    mVTable = GameAddress(kVTable);
    mRefCount = 0;
    mStream.mObject = nullptr;
    mFilter = nullptr;
    mFormatter = nullptr;
    mText.Construct();
    mEnabled = 0;
    mFlag = 0;
    mCounter = 0;
    mField40 = 0;
    mInterval = 1;
    return this;
}

cLogCaptionOutputBackEnd *cLogCaptionOutputBackEnd::Construct(const char *text)
{
    mBase.Construct();
    mBase.mVTable = GameAddress(kVTable);
    mCaption.Construct();
    mLock.Construct();
    mBase.mText.mString.assign(text); // the original calls the output back end's SetText (0xc9f4f4) directly
    return this;
}

cLogStdAdapter *cLogStdAdapter::Construct(uint32_t target, const char *name)
{
    // The cRZOStream base (0x1b4f60) and the text stream base (0x1b4fab), both overwritten by the final vtable.
    mField4 = 0;
    mVTable = GameAddress(kStreamBaseVTable);
    mField8 = 1;
    mFieldC = 0;
    mVTable = GameAddress(kTextStreamVTable);
    mVTable = GameAddress(kVTable);
    mTarget = target;
    mName.Construct(name);
    return this;
}

cLogFilterGroupLevels *cLogFilterGroupLevels::Construct()
{
    mVTable = GameAddress(kBaseVTable);
    mUnknown.Construct();
    mVTable = GameAddress(kVTable);
    mUnknown.mVTable = GameAddress(kUnknownVTable);
    mName.Construct();
    new (&mLevels) LevelMap();
    mNoGroupLevel = 0;
    mAnyGroupLevel = 7;
    return this;
}

void cLogRecord::Destruct()
{
    mVTable = GameAddress(kVTable);
    mUnknown.mVTable = GameAddress(kUnknownVTable);
    mText2.Destruct();
    mText.Destruct();
    reinterpret_cast<cRZUnknown *>(&mUnknown)->Destruct();
}

cLogRecord *cLogRecord::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

void cLogFormatterText::Destruct()
{
    mVTable = GameAddress(kVTable);
    mUnknown.mVTable = GameAddress(kUnknownVTable);
    mLock.Destruct();
    mScratch.Destruct();
    mOutput.Destruct();
    for (int i = 4; i >= 0; --i) mPresets[i].Destruct();
    for (int i = 4; i >= 0; --i) mFormats[i].Destruct();
    mName.Destruct();
    reinterpret_cast<cRZUnknown *>(&mUnknown)->Destruct();
}

cLogFormatterText *cLogFormatterText::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

void cLogFilterGroupLevels::Destruct()
{
    mVTable = GameAddress(kVTable);
    mUnknown.mVTable = GameAddress(kUnknownVTable);
    mLevels.~LevelMap();
    mName.Destruct();
    reinterpret_cast<cRZUnknown *>(&mUnknown)->Destruct();
}

cLogFilterGroupLevels *cLogFilterGroupLevels::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

AliasMap *__fastcall DeleteAliasMap(AliasMap *self, void *, uint32_t flags)
{
    self->~AliasMap();
    if (flags & 1) ::operator delete(self, sizeof(AliasMap));
    return self;
}

namespace {

// The shared shape of the helper objects' QueryInterface: a null ppv fails, a matching id hands out this
// (AddRef'd), anything else asks the cRZUnknown base at +4.
bool HelperQueryInterface(void *self, bool matches, uint32_t riid, void **ppv)
{
    if (!ppv) return false;
    if (matches) {
        *ppv = self;
        VirtualCall<AddRefFn, 1>(self);
        return true;
    }
    return reinterpret_cast<RZUnknownQueryInterface *>(static_cast<char *>(self) + 4)->QueryInterfaceUnknown(riid, ppv);
}

} // namespace

bool __fastcall RecordQueryInterface(void *self, void *, uint32_t riid, void **ppv)
{
    return HelperQueryInterface(self, riid == 0xcca124a1, riid, ppv);
}

bool __fastcall FormatterQueryInterface(void *self, void *, uint32_t riid, void **ppv)
{
    return HelperQueryInterface(self, riid == 0xcca124b6, riid, ppv);
}

bool __fastcall FilterQueryInterface(void *self, void *, uint32_t riid, void **ppv)
{
    return HelperQueryInterface(self, riid == 0x2ca92615 || riid == 0xaca92697, riid, ppv);
}

bool __fastcall ServiceQueryInterface(void *self, void *, uint32_t riid, void **ppv)
{
    if (riid != 0x09b69c46) return qi::SystemServiceQueryInterface(self, nullptr, riid, ppv);
    // No null check on ppv; a null this stays null (the cIService subobject address is this + 0x1c).
    *ppv = self ? static_cast<char *>(self) + 0x1c : nullptr;
    VirtualCall<AddRefFn, 1>(self);
    return true;
}

namespace thunks {
bool __fastcall RecordQueryInterfaceFromUnknown(void *self, void *, uint32_t riid, void **ppv)
{
    return RecordQueryInterface(static_cast<char *>(self) - 4, nullptr, riid, ppv);
}
bool __fastcall FormatterQueryInterfaceFromUnknown(void *self, void *, uint32_t riid, void **ppv)
{
    return FormatterQueryInterface(static_cast<char *>(self) - 4, nullptr, riid, ppv);
}
bool __fastcall FilterQueryInterfaceFromUnknown(void *self, void *, uint32_t riid, void **ppv)
{
    return FilterQueryInterface(static_cast<char *>(self) - 4, nullptr, riid, ppv);
}
bool __fastcall ServiceQueryInterfaceFromUnknown(void *self, void *, uint32_t riid, void **ppv)
{
    return ServiceQueryInterface(static_cast<char *>(self) - 4, nullptr, riid, ppv);
}
bool __fastcall ServiceQueryInterfaceFromService(void *self, void *, uint32_t riid, void **ppv)
{
    return ServiceQueryInterface(static_cast<char *>(self) - 0x1c, nullptr, riid, ppv);
}
cLogRecord *__fastcall RecordDeletingFromUnknown(void *self, void *, uint32_t flags)
{
    return reinterpret_cast<cLogRecord *>(static_cast<char *>(self) - 4)->DeletingDestructor(flags);
}
cLogFormatterText *__fastcall FormatterDeletingFromUnknown(void *self, void *, uint32_t flags)
{
    return reinterpret_cast<cLogFormatterText *>(static_cast<char *>(self) - 4)->DeletingDestructor(flags);
}
cLogFilterGroupLevels *__fastcall FilterDeletingFromUnknown(void *self, void *, uint32_t flags)
{
    return reinterpret_cast<cLogFilterGroupLevels *>(static_cast<char *>(self) - 4)->DeletingDestructor(flags);
}
} // namespace thunks

void __fastcall AddAliasText(AliasMap *self, void *, const char *key, const char *value)
{
    if (!value) return;
    LogString lookup(key);
    (*self)[lookup].mString.assign(value); // the library match is named append but this body is assign(const char *)
}

namespace {

cLogService *ServiceOf(void *sub)
{
    return reinterpret_cast<cLogService *>(static_cast<char *>(sub) - cLogService::kServiceSubobjectOffset);
}

using LockFn = void(__thiscall *)(void *);
using UnlockFn = uint32_t(__thiscall *)(void *);

} // namespace

void __fastcall ServiceAddAlias(void *sub, void *, const char *key, const char *value)
{
    AddAliasText(ServiceOf(sub)->mAliases, nullptr, key, value);
}

uint32_t __fastcall ServiceClearOutputs(void *sub, void *)
{
    cLogService *service = ServiceOf(sub);
    void *lock = &service->mLock90;
    VirtualCall<LockFn, 1>(lock);
    service->mOutputs.clear();
    return VirtualCall<UnlockFn, 2>(lock);
}

uint32_t __fastcall ServiceFlushOutputs(void *sub, void *)
{
    cLogService *service = ServiceOf(sub);
    void *lock = &service->mLock90;
    VirtualCall<LockFn, 1>(lock);
    // The end is read again on every pass, as the original does.
    for (RefHolder *it = service->mOutputs.data(); it != service->mOutputs.data() + service->mOutputs.size(); ++it)
        VirtualSlot<LockFn>(it->mObject, 21)(it->mObject);
    return VirtualCall<UnlockFn, 2>(lock);
}

namespace {

using NameFn = const char *(__thiscall *)(void *);
using CallArg1Fn = void(__thiscall *)(void *, uint32_t);
using CallPtr1Fn = void(__thiscall *)(void *, void *);
using QueryFn = uint8_t(__thiscall *)(void *, uint32_t, void **);

void *FindByName(const std::vector<RefHolder> &list, int slot, const char *name)
{
    for (const RefHolder *it = list.data(); it != list.data() + list.size(); ++it) {
        const char *text = VirtualSlot<NameFn>(it->mObject, slot)(it->mObject);
        if (_stricmp(text, name) == 0) return it->mObject;
    }
    return nullptr;
}

void AddRefObject(void *object)
{
    VirtualCall<AddRefFn, 1>(object);
}

void ReleaseObject(void *object)
{
    VirtualCall<AddRefFn, 2>(object);
}

// The output called `name`, held across one call of `call(output)`.
template <typename Call> void WithOutput(cLogService *service, const char *name, Call call)
{
    void *output = FindOutput(service, nullptr, name);
    if (!output) return;
    AddRefObject(output);
    call(output);
    ReleaseObject(output);
}

} // namespace

void *__fastcall FindOutput(cLogService *service, void *, const char *name)
{
    return FindByName(service->mOutputs, 11, name);
}

void *__fastcall FindFilter(cLogService *service, void *, const char *name)
{
    return FindByName(service->mFilters, 7, name);
}

void *__fastcall FindFormatter(cLogService *service, void *, const char *name)
{
    return FindByName(service->mFormatters, 7, name);
}

void __fastcall EraseRef(std::vector<RefHolder> *self, void *, RefHolder **out, RefHolder *where)
{
    self->erase(self->begin() + (where - self->data()));
    *out = where;
}

void __fastcall ServiceSetFormatterFormat(void *sub, void *, const char *name, uint32_t index, const char *format)
{
    cLogService *service = ServiceOf(sub);
    void *formatter = FindFormatter(service, nullptr, name);
    if (!formatter) return;
    AddRefObject(formatter);
    VirtualCall<SetFormatFn, 3>(formatter, index, format);
    ReleaseObject(formatter);
}

void __fastcall ServiceOutputSlot12(void *sub, void *, const char *name, uint32_t arg)
{
    WithOutput(ServiceOf(sub), name, [&](void *output) { VirtualCall<CallArg1Fn, 12>(output, arg); });
}

void __fastcall ServiceOutputSlot16(void *sub, void *, const char *name, uint32_t arg)
{
    WithOutput(ServiceOf(sub), name, [&](void *output) { VirtualCall<CallArg1Fn, 16>(output, arg); });
}

void __fastcall ServiceOutputSlot14(void *sub, void *, const char *name, uint32_t arg)
{
    WithOutput(ServiceOf(sub), name, [&](void *output) { VirtualCall<CallArg1Fn, 14>(output, arg); });
}

void __fastcall ServiceSetOutputFilter(void *sub, void *, const char *name, const char *filter)
{
    cLogService *service = ServiceOf(sub);
    void *output = FindOutput(service, nullptr, name);
    if (!output) return;
    AddRefObject(output);
    // The output's vtable is read once, before the filter is looked up.
    const void *const *vtable = *static_cast<const void *const *const *>(output);
    void *found = FindFilter(service, nullptr, filter);
    reinterpret_cast<CallPtr1Fn>(const_cast<void *>(vtable[9]))(output, found);
    ReleaseObject(output);
}

void __fastcall ServiceSetOutputFormatter(void *sub, void *, const char *name, const char *formatter)
{
    cLogService *service = ServiceOf(sub);
    void *output = FindOutput(service, nullptr, name);
    if (!output) return;
    AddRefObject(output);
    const void *const *vtable = *static_cast<const void *const *const *>(output);
    void *found = FindFormatter(service, nullptr, formatter);
    reinterpret_cast<CallPtr1Fn>(const_cast<void *>(vtable[7]))(output, found);
    ReleaseObject(output);
}

void __fastcall ServiceEnumerateNames(void *sub, void *, void *arg, NameCallback callback)
{
    if (!callback) return;
    cLogService *service = ServiceOf(sub);
    void *lock = &service->mLock28;
    VirtualCall<LockFn, 1>(lock);
    // The table's list: a head node first, each node holding two links and then the name (a cRZString).
    struct NameNode {
        NameNode *next;
        NameNode *prev;
        cRZString name;
    };
    auto head = [&]() { return *reinterpret_cast<NameNode *const *>(reinterpret_cast<const char *>(&service->mNames) + 4); };
    for (NameNode *node = head()->next; node != head(); node = node->next) callback(arg, node->name.mString.c_str());
    VirtualCall<UnlockFn, 2>(lock);
}

uint8_t __fastcall ServiceQueryOutput(void *sub, void *, const char *name, void **ppv)
{
    cLogService *service = ServiceOf(sub);
    void *lock = &service->mLock90;
    VirtualCall<LockFn, 1>(lock);
    uint8_t answered = 0;
    if (void *output = FindOutput(service, nullptr, name)) answered = VirtualCall<QueryFn, 0>(output, 0x49b84afb, ppv);
    VirtualCall<UnlockFn, 2>(lock);
    return answered;
}

bool __fastcall ServiceRemoveOutput(void *sub, void *, const char *name)
{
    cLogService *service = ServiceOf(sub);
    void *lock = &service->mLock90;
    VirtualCall<LockFn, 1>(lock);
    bool removed = false;
    for (RefHolder *it = service->mOutputs.data(); it != service->mOutputs.data() + service->mOutputs.size(); ++it) {
        if (_stricmp(VirtualSlot<NameFn>(it->mObject, 11)(it->mObject), name) == 0) {
            RefHolder *next;
            EraseRef(&service->mOutputs, nullptr, &next, it);
            removed = true;
            break;
        }
    }
    VirtualCall<UnlockFn, 2>(lock);
    return removed;
}

void cLogRecord::SetLevel(uint32_t level, uint32_t arg)
{
    mMessage = 0;
    mLevel = level;
    mGroup = arg;
}

const char *__cdecl LevelName(int32_t level)
{
    uint32_t rva;
    switch (level) {
    case 0: rva = 0x1085d94; break;
    case 2: rva = 0x1085d6c; break;
    case 3: rva = 0x1085d74; break;
    case 4: rva = 0x1085d7c; break;
    case 5: rva = 0x1085d84; break;
    case 6: rva = 0x1085d8c; break;
    case 7: rva = 0x1085d64; break;
    default: return GameAddress<const char>(0xd9b9d0); // an empty string in the game's data
    }
    return *GameAddress<const char *>(rva);
}

int32_t __cdecl ParseLevelName(const char *text)
{
    if (!text) return -1;
    LogString haystack(text);
    // The first of these level texts the argument contains (case-insensitively) wins.
    static const struct {
        uint32_t rva;
        int32_t level;
    } kOrder[] = {{0x1085d6c, 2}, {0x1085d74, 3}, {0x1085d7c, 4}, {0x1085d84, 5}, {0x1085d8c, 6}, {0x1085d94, 0}, {0x1085d64, 7}};
    for (const auto &entry : kOrder)
        if (haystack.Find(*GameAddress<const char *>(entry.rva), 0, false) != -1) return entry.level;
    return -1;
}

const char *__fastcall ServiceLevelName(void *, void *, int32_t level)
{
    return LevelName(level);
}

int32_t __fastcall ServiceParseLevelName(void *, void *, const char *text)
{
    return ParseLevelName(text);
}

void __fastcall ServiceAddFormatter(void *sub, void *, const char *name)
{
    cLogService *service = ServiceOf(sub);
    if (FindFormatter(service, nullptr, name)) return;
    cLogFormatterText *formatter = NewConstructed<cLogFormatterText>();
    VirtualCall<SetNameFn, cLogFormatterText::kSlotSetName>(formatter, name);
    formatter->SetAliasMap(service->mAliases);
    formatter->SetTimestamp(service->mTimestamp);
    AddRefObject(formatter);
    void *held = formatter;
    service->mFormatters.push_back(reinterpret_cast<const RefHolder &>(held));
    ReleaseObject(formatter);
}

void __fastcall ServiceAddFilter(void *sub, void *, const char *name)
{
    cLogService *service = ServiceOf(sub);
    if (FindFilter(service, nullptr, name)) return;
    cLogFilterGroupLevels *filter = NewConstructed<cLogFilterGroupLevels>();
    VirtualCall<SetNameFn, cLogFilterGroupLevels::kSlotSetName>(filter, name);
    AddRefObject(filter);
    void *held = filter;
    service->mFilters.push_back(reinterpret_cast<const RefHolder &>(held));
    ReleaseObject(filter);
}

uint8_t __fastcall ServiceLog(void *sub, void *, uint32_t arg, int32_t level)
{
    cLogService *service = ServiceOf(sub);
    if (level > static_cast<int32_t>(service->mField24)) return 0;
    if (*GameAddress<uint8_t>(cLogService::kLoggingDisabledRva) != 0) return 0;
    alignas(4) uint8_t storage[cLogRecord::kSize];
    cLogRecord *record = reinterpret_cast<cLogRecord *>(storage)->Construct();
    record->SetLevel(static_cast<uint32_t>(level), arg);
    uint8_t answered = 0;
    void *lock = &service->mLock90;
    VirtualCall<LockFn, 1>(lock);
    for (const RefHolder *it = service->mOutputs.data(); it != service->mOutputs.data() + service->mOutputs.size(); ++it) {
        if (VirtualSlot<uint8_t(__thiscall *)(void *, void *)>(it->mObject, 3)(it->mObject, record)) {
            answered = 1;
            break;
        }
    }
    VirtualCall<UnlockFn, 2>(lock);
    record->Destruct();
    return answered;
}

bool cLogService::Init()
{
    if (!mField20) {
        void *framework = reinterpret_cast<void *>(static_cast<uintptr_t>(RZGetGlobal10C9378()));
        VirtualCall<void(__thiscall *)(void *, void *), kFrameworkSlotRegister>(framework, this);
        mField20 = 1;
    }
    return true;
}

uint32_t cLogService::StopOutputThread()
{
    mOutputRunning = 0;
    void *signal = &mSignal;
    VirtualCall<LockFn, 1>(signal);
    void *thread = &mThread;
    VirtualCall<CallArg1Fn, 8>(thread, 0xffffffffu);
    void *sub = reinterpret_cast<uint8_t *>(this) + kServiceSubobjectOffset;
    VirtualCall<LockFn, 29>(sub);
    return VirtualCall<UnlockFn, 2>(this);
}

bool cLogService::Shutdown()
{
    StopOutputThread();
    if (mField20) {
        mField20 = 0;
        void *framework = reinterpret_cast<void *>(static_cast<uintptr_t>(RZGetGlobal10C9378()));
        VirtualCall<void(__thiscall *)(void *, void *), kFrameworkSlotUnregister>(framework, this);
    }
    void *lock = &mLock90;
    VirtualCall<LockFn, 1>(lock);
    mOutputs.clear();
    mFilters.clear();
    mFormatters.clear();
    VirtualCall<UnlockFn, 2>(lock);
    return true;
}

void cLogService::StartOutputThread()
{
    VirtualCall<AddRefFn, kSlotAddRef>(this);
    void *thread = &mThread;
    VirtualCall<SetNameFn, cRZThread::kSlotSetName>(thread, "cGZLogAsynchOutput");
    VirtualCall<StartFn, 2>(thread, GameAddress(kThreadProc), this);
}

cLogService::cLogService() : rz::cRZSystemService(kServiceId, kPriority)
{
    mTimestamp = NewConstructed<cLogTimestamp>();
    mProcessInfo = NewConstructed<cLogProcessInfo>();
    StoreCurrentThreadId(&mThreadId);
    mOutputThreadId = 0;
    mRecordNumber = 0;

    // Room for twice the record count in each of the two lists (the first stays empty).
    if (static_cast<uint32_t>(mRecordCount) * 2 > mQueue.capacity()) mQueue.reserve(static_cast<uint32_t>(mRecordCount) * 2);
    if (static_cast<uint32_t>(mRecordCount) * 2 > mRecords.capacity()) mRecords.reserve(static_cast<uint32_t>(mRecordCount) * 2);

    // The record pool: each record is AddRef'd once and kept by raw pointer.
    for (int32_t i = 0; i < mRecordCount; ++i) {
        cLogRecord *record = NewConstructed<cLogRecord>();
        VirtualCall<AddRefFn, 1>(record);
        void *pointer = record;
        mRecords.push_back(pointer);
    }

    // Format names to format texts (a key added twice keeps the later text).
    mAliases = new (::operator new(sizeof(AliasMap))) AliasMap();
    AddAliasText(mAliases, nullptr, "AllTokens", "recordnum %r, date %D, time %t, elapsed %e, cputime %c, framenum %f, level %L, group %G, fileline %F, message %M, message(single line) %N");
    AddAliasText(mAliases, nullptr, *GameAddress<const char *>(kNoPresetPointerRva), "");
    AddAliasText(mAliases, nullptr, "OldDebug", "%L: %G\n%M\n");
    AddAliasText(mAliases, nullptr, "OldTimestampOn", "%t %G: %M\n");
    AddAliasText(mAliases, nullptr, "OldTimestampOff", "%G: %M\n");
    AddAliasText(mAliases, nullptr, "DebugFormat", "#%r %t %f %L %G\n%F\n%M\n");
    AddAliasText(mAliases, nullptr, "DebugFormatNoFilename", "#%r %t %f %L %G\n%M\n");
    AddAliasText(mAliases, nullptr, "ServerFormat", "#%r %D %t %f\n%L %G\n%F\n%M\n");
    AddAliasText(mAliases, nullptr, "ServerFormatNoFilename", "#%r %D %t %f\n%L %G\n%M\n");
    AddAliasText(mAliases, nullptr, "SingleLine", "#%r %t %c %f %L %G %N\n");
    AddAliasText(mAliases, nullptr, "ShortTitle", "%L %G %N\n");
    AddAliasText(mAliases, nullptr, "MessageOnly", "%M\n");

    // The default formatter: formats 2 and 3 get "DebugFormat", 4 to 6 "DebugFormatNoFilename".
    cLogFormatterText *formatter = NewConstructed<cLogFormatterText>();
    formatter->SetAliasMap(mAliases);
    formatter->SetTimestamp(mTimestamp);
    mFormatter.Assign(formatter);
    VirtualCall<SetNameFn, cLogFormatterText::kSlotSetName>(mFormatter.mObject, "Default");
    VirtualCall<SetFormatFn, cLogFormatterText::kSlotSetFormat>(mFormatter.mObject, 2, "DebugFormat");
    VirtualCall<SetFormatFn, cLogFormatterText::kSlotSetFormat>(mFormatter.mObject, 3, "DebugFormat");
    VirtualCall<SetFormatFn, cLogFormatterText::kSlotSetFormat>(mFormatter.mObject, 4, "DebugFormatNoFilename");
    VirtualCall<SetFormatFn, cLogFormatterText::kSlotSetFormat>(mFormatter.mObject, 5, "DebugFormatNoFilename");
    VirtualCall<SetFormatFn, cLogFormatterText::kSlotSetFormat>(mFormatter.mObject, 6, "DebugFormatNoFilename");
    mFormatters.push_back(reinterpret_cast<const RefHolder &>(mFormatter));

    // The default filter.
    cLogFilterGroupLevels *filter = NewConstructed<cLogFilterGroupLevels>();
    mFilter.Assign(filter);
    VirtualCall<SetNameFn, cLogFilterGroupLevels::kSlotSetName>(mFilter.mObject, "Default");
    mFilters.push_back(reinterpret_cast<const RefHolder &>(mFilter));

    mRunning = true;
    mOutputRunning = 1;
    StartOutputThread();
}

// ---- the faces ----------------------------------------------------------------------------------------------------

bool cLogService::QueryInterface(uint32_t riid, void **ppv) { return ServiceQueryInterface(this, nullptr, riid, ppv); }
uint32_t cLogService::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cLogService::Release() { return rz::cRZSystemService::Release(); }

// cIService: the bodies are the free functions on this face (they keep the original's RVAs and hooks).
#define LOG_FACE static_cast<cIService *>(this), nullptr
const char *cLogService::LevelName(int32_t level) { return ServiceLevelName(LOG_FACE, level); }
int32_t cLogService::ParseLevelName(const char *text) { return ServiceParseLevelName(LOG_FACE, text); }
void cLogService::AddAlias(const char *key, const char *value) { ServiceAddAlias(LOG_FACE, key, value); }
void cLogService::AddFormatter(const char *name) { ServiceAddFormatter(LOG_FACE, name); }
void cLogService::SetFormatterFormat(const char *name, uint32_t index, const char *format)
{
    ServiceSetFormatterFormat(LOG_FACE, name, index, format);
}
void cLogService::SetLevelLimit(uint32_t level) { mField24 = level; }
uint8_t cLogService::SetFilterGroupLevel(const char *pattern, const char *group, int32_t level)
{
    return ServiceSetFilterGroupLevel(LOG_FACE, pattern, group, level);
}
void cLogService::AddFilter(const char *name) { ServiceAddFilter(LOG_FACE, name); }
uint8_t cLogService::AddOutput(const char *name, void *output) { return ServiceAddOutput(LOG_FACE, name, output); }
uint8_t cLogService::QueryOutput(const char *name, void **ppv) { return ServiceQueryOutput(LOG_FACE, name, ppv); }
bool cLogService::RemoveOutput(const char *name) { return ServiceRemoveOutput(LOG_FACE, name); }
uint32_t cLogService::ClearOutputs() { return ServiceClearOutputs(LOG_FACE); }
void cLogService::OutputSlot12(const char *name, uint32_t arg) { ServiceOutputSlot12(LOG_FACE, name, arg); }
void cLogService::SetOutputFilter(const char *name, const char *filter) { ServiceSetOutputFilter(LOG_FACE, name, filter); }
void cLogService::SetOutputFormatter(const char *name, const char *formatter)
{
    ServiceSetOutputFormatter(LOG_FACE, name, formatter);
}
void cLogService::OutputSlot16(const char *name, uint32_t arg) { ServiceOutputSlot16(LOG_FACE, name, arg); }
void cLogService::OutputSlot14(const char *name, uint32_t arg) { ServiceOutputSlot14(LOG_FACE, name, arg); }
uint8_t cLogService::SetOutputGroupLevel(const char *pattern, const char *group, int32_t level)
{
    return ServiceSetOutputGroupLevel(LOG_FACE, pattern, group, level);
}
uint32_t cLogService::AddStreamOutput(const char *name, void *stream) { return ServiceAddStreamOutput(LOG_FACE, name, stream); }
uint32_t cLogService::AddFileOutput(const char *name, const char *path, uint8_t append, uint8_t deferOpen)
{
    return ServiceAddFileOutput(LOG_FACE, name, path, append, deferOpen);
}
void cLogService::TimestampedName(cIGZString *name) { ServiceTimestampedName(LOG_FACE, name); }
void cLogService::WriteConfig(void *stream) { ServiceWriteConfig(LOG_FACE, stream); }
void cLogService::EnumerateNames(void *arg, NameCallback callback) { ServiceEnumerateNames(LOG_FACE, arg, callback); }
void cLogService::SetFrame(uint32_t frame) { mFrame = frame; }
uint8_t cLogService::Log(uint32_t arg, int32_t level) { return ServiceLog(LOG_FACE, arg, level); }
void cLogService::Write(const char *file, uint32_t line, const char *group, int32_t level, const char *format, va_list args)
{
    ServiceWrite(LOG_FACE, file, line, group, level, format, args);
}
uint32_t cLogService::FlushOutputs() { return ServiceFlushOutputs(LOG_FACE); }
#undef LOG_FACE

// ---- the hooks' entry points ----------------------------------------------------------------------------------------

cLogService *__fastcall cLogServiceAdapters::Construct(void *memory, void *) { return ::new (memory) cLogService; }
void __fastcall cLogServiceAdapters::Destruct(cLogService *self, void *) { self->cLogService::~cLogService(); }
cLogService *__fastcall cLogServiceAdapters::DeletingDestructor(cLogService *self, void *, uint32_t flags)
{
    self->cLogService::~cLogService();
    if (flags & 1) ::operator delete(self, sizeof(cLogService));
    return self;
}
bool __fastcall cLogServiceAdapters::Init(cLogService *self, void *)
{
    [[msvc::forceinline_calls]] return self->cLogService::Init();
}
bool __fastcall cLogServiceAdapters::Shutdown(cLogService *self, void *)
{
    [[msvc::forceinline_calls]] return self->cLogService::Shutdown();
}
bool __fastcall cLogServiceAdapters::OnTick(cLogService *self, void *, uint32_t arg)
{
    [[msvc::forceinline_calls]] return self->cLogService::OnTick(arg);
}

} // namespace nGZLog
