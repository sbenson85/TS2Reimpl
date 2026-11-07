#include "gz/GZCOM.h"

#include "app/ReportText.h"
#include "game/game.h"
#include "game/import.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZCOMDirector.h"
#include "rz/RZTextConvert.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/cRZThread.h"
#include "rz/nGZLog.h"

#include <windows.h>

#include <cstdlib>
#include <new>
#include <time.h>
#include <vector>

namespace {

// Imports the game binds in its import address table, called through the table's slot (game/import.h), so a test can
// substitute the calls that load libraries or sleep.
constexpr uint32_t kIatGetModuleHandleA = 0xd9a114;
constexpr uint32_t kIatGetVersion = 0xd9a11c;
constexpr uint32_t kIatGetLastError = 0xd9a118;
constexpr uint32_t kIatFreeLibrary = 0xd9a120;
constexpr uint32_t kIatGetProcAddress = 0xd9a124;
constexpr uint32_t kIatLoadLibraryA = 0xd9a128;
constexpr uint32_t kIatLoadLibraryW = 0xd9a12c;
constexpr uint32_t kIatLocalFree = 0xd9a130;
constexpr uint32_t kIatFormatMessageA = 0xd9a134;
constexpr uint32_t kIatGetModuleFileNameA = 0xd9a16c;

constexpr uint32_t kLoadFailedFormat = 0xd9b8d8;     // "Failed to load the requested dll ..."
constexpr uint32_t kNoDirectorFormat = 0xd9b920;     // "... Failed to aquire GZCOM director ..."
constexpr uint32_t kDirectorFailedFormat = 0xd9b970; // "... GZCOM Director failed initialization ..."
constexpr uint32_t kExportName = 0xd9b7d0;           // "GZDllGetGZCOMDirector"
constexpr uint32_t kKernel32Name = 0xda26dc;         // "Kernel32.dll"
constexpr uint32_t kGetModuleFileNameWName = 0xda2744;
constexpr uint32_t kModuleFileNameWResolved = 0x10caeb5; // byte: the lookup was done
constexpr uint32_t kModuleFileNameWPointer = 0x10caeb8;  // the resolved GetModuleFileNameW

// 0x1b9f95 (__cdecl): SleepEx((microseconds + 999) / 1000, TRUE), through the import slot.
void SleepMicroseconds(uint32_t microseconds) { cRZThread::SleepMicroseconds(microseconds); }
constexpr uint32_t kGuidWaitMicroseconds = 250000;

int64_t GuidTime()
{
    return gGZCOMGuidTimeOverride ? gGZCOMGuidTimeOverride(nullptr) : _time64(nullptr);
}

uint32_t GuidSeconds()
{
    return gGZCOMGuidSecondsOverride ? gGZCOMGuidSecondsOverride() : appbase::SecondsSinceGuidEpoch();
}

// The critical section is locked through its own vtable (slots 1 and 2), on the member itself (+0x24), not through
// the cRZThreadSafeObject pointer.
void Lock(cRZCriticalSection &section)
{
    VirtualCall<int32_t(__thiscall *)(cRZCriticalSection *), 1>(&section);
}
void Unlock(cRZCriticalSection &section)
{
    VirtualCall<int32_t(__thiscall *)(cRZCriticalSection *), 2>(&section);
}

// The SEH wrappers around the library APIs (0x145602, 0x145637, 0x14567e, 0x1456c0): the call runs
// under a handler that swallows any exception (the filter returns 1). They keep no C++ objects.
void GuardedFreeLibrary(void *module)
{
    __try {
        ImportFunction<BOOL(WINAPI *)(void *), kIatFreeLibrary>()(module);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void GuardedGetProcAddress(void **out, void *const *module, const char *name)
{
    __try {
        *out = ImportFunction<void *(WINAPI *)(void *, const char *), kIatGetProcAddress>()(*module, name);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void GuardedLoadLibraryA(void **out, const char *name)
{
    __try {
        *out = ImportFunction<void *(WINAPI *)(const char *), kIatLoadLibraryA>()(name);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void GuardedLoadLibraryW(void **out, const wchar_t *name)
{
    __try {
        *out = ImportFunction<void *(WINAPI *)(const wchar_t *), kIatLoadLibraryW>()(name);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

} // namespace

int64_t (__cdecl *gGZCOMGuidTimeOverride)(int64_t *) = nullptr;
uint32_t (__cdecl *gGZCOMGuidSecondsOverride)() = nullptr;

// ---- text helpers ----

uint32_t ConvertBytesToString(const void *src, uint32_t srcLength, int32_t srcEncoding, cIGZString *dst)
{
    int32_t native = RZText::DefaultEncoding();
    if (srcEncoding != -2) {
        if (srcEncoding == -1) srcEncoding = RZText::SystemCodePage();
        if (srcEncoding != native) {
            // Six output units per input unit is the worst case; small results use the stack.
            uint32_t capacity = srcLength * 6;
            char stackBuffer[0x200];
            char *heapBuffer = capacity < 0x200 ? nullptr : static_cast<char *>(std::malloc(capacity));
            char *buffer = heapBuffer ? heapBuffer : stackBuffer;
            uint32_t length = capacity;
            buffer[0] = 0;
            uint32_t consumed = RZText::Convert(src, srcLength, srcEncoding, buffer, &length, native);
            dst->FromChar(buffer, length);
            std::free(heapBuffer);
            return consumed;
        }
    }
    dst->FromChar(static_cast<const char *>(src), srcLength);
    return srcLength;
}

uint32_t GetModulePath(cIGZString *str)
{
    void *function;
    uint8_t *resolved = GameAddress<uint8_t>(kModuleFileNameWResolved);
    void **cached = GameAddress<void *>(kModuleFileNameWPointer);
    if (!*resolved) {
        HMODULE kernel = ImportFunction<HMODULE(WINAPI *)(const char *), kIatGetModuleHandleA>()(GameAddress<const char>(kKernel32Name));
        if (kernel) {
            function = ImportFunction<void *(WINAPI *)(void *, const char *), kIatGetProcAddress>()(kernel, GameAddress<const char>(kGetModuleFileNameWName));
            *cached = function;
        } else {
            function = *cached;
        }
        *resolved = 1;
    } else {
        function = *cached;
    }
    if (function) {
        wchar_t wide[0x104];
        uint32_t length = reinterpret_cast<DWORD(WINAPI *)(void *, wchar_t *, DWORD)>(function)(nullptr, wide, 0x104);
        if (length) ConvertBytesToString(wide, length, RZText::kUtf16, str);
    }
    if (str->Strlen() == 0) {
        char narrow[0x104];
        uint32_t length = ImportFunction<DWORD(WINAPI *)(void *, char *, DWORD), kIatGetModuleFileNameA>()(nullptr, narrow, 0x104);
        if (length) ConvertBytesToString(narrow, length, RZText::kSystemCodePage, str);
    }
    return str->Strlen();
}

// ---- cGZCOMLibrary ----

cGZCOMLibrary::cGZCOMLibrary(const cIGZString &path) : mInitialized(false), mRefCount(0), mDirector(nullptr)
{
    mPath.Construct(path);
    mModule = nullptr;
}

cGZCOMLibrary::cGZCOMLibrary(const cGZCOMLibrary &other)
    : mInitialized(other.mInitialized), mRefCount(other.mRefCount), mDirector(other.mDirector)
{
    mPath.Construct(other.mPath, 0, 0xffffffff);
    mModule = other.mModule;
}

cGZCOMLibrary::~cGZCOMLibrary()
{
    mPath.Destruct();
}

bool cGZCOMLibrary::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != kIID && riid != 1) return false;
    *ppv = static_cast<cIGZCOMLibrary *>(this);
    AddRef();
    return true;
}

uint32_t cGZCOMLibrary::AddRef()
{
    return static_cast<uint32_t>(++mRefCount);
}

uint32_t cGZCOMLibrary::Release()
{
    return static_cast<uint32_t>(--mRefCount);
}

void cGZCOMLibrary::GetPath(cIGZString *out)
{
    out->Copy(*mPath.AsInterface());
}

cIGZCOMDirector *cGZCOMLibrary::GetDirector()
{
    return mDirector;
}

void *cGZCOMLibrary::GetModule()
{
    return mModule;
}

bool cGZCOMLibrary::IsInitialized()
{
    return mInitialized;
}

bool cGZCOMLibrary::operator<(const cGZCOMLibrary &other) const
{
    // other is asked for its path through its vtable (slot 3) into a temporary string.
    cRZString path;
    path.Construct();
    const_cast<cGZCOMLibrary &>(other).GetPath(path.AsInterface());
    bool less = mPath.mString < path.mString;
    path.Destruct();
    return less;
}

bool cGZCOMLibrary::Unload()
{
    if (mInitialized) {
        if (mDirector) mDirector = nullptr;
        // The path's slot 5 (ToChar) is called and its result passed along, unused.
        mPath.AsInterface()->ToChar();
        GuardedFreeLibrary(mModule);
        mModule = nullptr;
        mInitialized = false;
    }
    return true;
}

bool cGZCOMLibrary::Initialize()
{
    if (mInitialized) return true;

    cRZString converted;
    converted.Construct();
    RZText::ConvertStringToString(*mPath.AsInterface(), *converted.AsInterface(), -1);

    if (static_cast<int32_t>(ImportFunction<DWORD(WINAPI *)(), kIatGetVersion>()()) < 0) {
        // Windows 9x: the ANSI loader.
        GuardedLoadLibraryA(&mModule, converted.mString.c_str());
    } else {
        // The UTF-16 loader (the path converted into len + 1 zeroed units).
        uint32_t length = static_cast<uint32_t>(mPath.mString.size());
        uint32_t units = length;
        std::vector<short> wide(length + 1);
        RZText::ConvertString(*mPath.AsInterface(), wide.data(), &units, RZText::kUtf16);
        wide[units] = 0;
        GuardedLoadLibraryW(&mModule, reinterpret_cast<const wchar_t *>(wide.data()));
    }

    cRZString message;
    bool result;
    if (mModule == nullptr) {
        // The failure text is formatted and then dropped (the original keeps it in a local only).
        message.Construct();
        void *buffer = nullptr;
        DWORD error = ImportFunction<DWORD(WINAPI *)(), kIatGetLastError>()();
        ImportFunction<DWORD(WINAPI *)(DWORD, const void *, DWORD, DWORD, void *, DWORD, void *), kIatFormatMessageA>()(
            0x1300, nullptr, error, 0x400, &buffer, 0, nullptr);
        if (buffer) {
            message.Sprintf(GameAddress<const char>(kLoadFailedFormat), converted.mString.c_str(), static_cast<const char *>(buffer));
            ImportFunction<void *(WINAPI *)(void *), kIatLocalFree>()(buffer);
        }
        message.Destruct();
        result = false;
    } else {
        mInitialized = true;
        void *exportFunction = nullptr;
        GuardedGetProcAddress(&exportFunction, &mModule, GameAddress<const char>(kExportName));
        if (exportFunction == nullptr) {
            Unload();
            result = false;
        } else {
            auto *director = reinterpret_cast<cIGZCOMDirector *(__cdecl *)()>(exportFunction)();
            if (director == nullptr) {
                message.Construct();
                message.Sprintf(GameAddress<const char>(kNoDirectorFormat), mPath.mString.c_str());
                Unload();
                message.Destruct();
                result = false;
            } else {
                mDirector = director;
                auto *com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
                if (!director->InitializeCOM(com, *mPath.AsInterface())) {
                    message.Construct();
                    message.Sprintf(GameAddress<const char>(kDirectorFailedFormat), mPath.mString.c_str());
                    Unload();
                    message.Destruct();
                    result = false;
                } else {
                    result = true;
                }
            }
        }
    }
    converted.Destruct();
    return result;
}

// ---- cGZCOM ----

cGZCOM::cGZCOM() : rz::cRZSystemService(kServiceId, kPriority)
{
    mLock = &mCritSect;
    mCritSect.Construct();
    mInitialized = false;
    mField30 = 0;
    mCurrentLibrary = nullptr;
    mFramework = nullptr;
    mDirector = nullptr;
    mRandom.ConstructAt(0xffffffff, 0x144eb9); // the original's call returns to 0x144eb9
    mLastGuidTime = 0;
}

cGZCOM::~cGZCOM()
{
    // The containers go with the members; the section is destroyed here, before the system service base (the
    // original destroys it after: separate memory, the same end state).
    mCritSect.Destruct();
}

bool cGZCOM::QueryInterface(uint32_t riid, void **out)
{
    if (riid == cIGZCOM::kIID) {
        *out = static_cast<cIGZCOM *>(this);
        AddRef();
        return true;
    }
    return rz::cRZSystemService::QueryInterface(riid, out);
}

uint32_t cGZCOM::AddRef()
{
    return rz::cRZSystemService::AddRef();
}

uint32_t cGZCOM::Release()
{
    // The +8 face's slot 4 (the reference count's decrement), through its vtable.
    return RemoveRef();
}

void cGZCOM::SetServiceRunning(bool running)
{
    rz::cRZSystemService::SetServiceRunning(running);
}

cIGZFrameWork *cGZCOM::FrameWork()
{
    return mFramework;
}

uint32_t cGZCOM::GetClassObject(uint32_t classId, uint32_t iid, void **ppv)
{
    Lock(mCritSect);
    auto it = mClasses.find(classId);
    if (it == mClasses.end()) {
        Unlock(mCritSect);
        if (classId == kFrameworkClassId) return reinterpret_cast<cIGZUnknown *>(mFramework)->QueryInterface(iid, ppv);
        return mDirector->GetClassObject(classId, iid, ppv);
    }
    cGZCOMLibrary *library = it->second.mLibrary;
    Unlock(mCritSect);
    return LoadAndGetClassObject(library, classId, iid, ppv);
}

bool cGZCOM::LoadAndGetClassObject(cGZCOMLibrary *library, uint32_t classId, uint32_t iid, void **ppv)
{
    if (!library->IsInitialized() && !library->Initialize()) return false;
    return library->GetDirector()->GetClassObject(classId, iid, ppv);
}

bool cGZCOM::AddLibrary(const cIGZString &path)
{
    cGZCOMLibrary key(path);
    Lock(mCritSect);
    bool added;
    if (mLibraries.find(key) != mLibraries.end()) {
        Unlock(mCritSect);
        added = true;
    } else {
        auto inserted = mLibraries.insert(key);
        Unlock(mCritSect);
        cGZCOMLibrary *library = const_cast<cGZCOMLibrary *>(&*inserted.first);
        if (InitLibrary(library)) {
            cIGZCOMDirector *director = library->GetDirector();
            if (director) director->OnStart(this);
            added = true;
        } else {
            added = false;
        }
    }
    return added;
}

bool cGZCOM::InitLibrary(cGZCOMLibrary *library)
{
    if (!library->Initialize()) return false;
    cIGZCOMDirector *director = library->GetDirector();
    Lock(mCritSect);
    mCurrentLibrary = library;
    director->EnumClassObjects(reinterpret_cast<void *>(&RegisterClassCallback), this);
    Unlock(mCritSect);
    return true;
}

void __cdecl cGZCOM::RegisterClassCallback(uint32_t classId, uint32_t version, cGZCOM *com)
{
    auto it = com->mClasses.find(classId);
    if (it != com->mClasses.end() && it->second.mVersion < version) com->mClasses.erase(it);
    com->mClasses.insert({classId, cGZCOMClassEntry{version, com->mCurrentLibrary}});
}

bool cGZCOM::CanUnloadLibrary(cGZCOMLibrary *library)
{
    if (!library->IsInitialized()) return true;
    return library->GetDirector()->CanUnloadNow();
}

void cGZCOM::FreeUnusedLibraries()
{
    Lock(mCritSect);
    for (auto it = mLibraries.begin(); it != mLibraries.end(); ++it) {
        cGZCOMLibrary *library = const_cast<cGZCOMLibrary *>(&*it);
        if (library->IsInitialized() && CanUnloadLibrary(library)) library->Unload();
    }
    Unlock(mCritSect);
}

void cGZCOM::UnloadLibraries()
{
    Lock(mCritSect);
    for (auto it = mLibraries.begin(); it != mLibraries.end(); ++it) {
        cGZCOMLibrary *library = const_cast<cGZCOMLibrary *>(&*it);
        if (library->IsInitialized()) library->Unload();
    }
    Unlock(mCritSect);
}

bool cGZCOM::RealInit()
{
    if (mInitialized) return true;
    mFramework = static_cast<cIGZFrameWork *>(GetServiceObject());
    mDirector = reinterpret_cast<cIGZCOMDirector *>(GetCOMDirector());
    cRZString path;
    path.Construct();
    GetModulePath(path.AsInterface());
    bool ok = mDirector->InitializeCOM(this, *path.AsInterface());
    if (ok) mInitialized = true;
    path.Destruct();
    return ok;
}

void cGZCOM::RealShutdown()
{
    if (mInitialized) {
        UnloadLibraries();
        mFramework = nullptr;
        mInitialized = false;
    }
}

uint32_t cGZCOM::EnumerateLibraries(void *callback, void *context)
{
    // Not locked. The result register is whatever the last step left; the original ends with eax
    // holding the callback's last return or a stack slot, so nothing is returned.
    auto *each = reinterpret_cast<bool(__cdecl *)(cIGZCOMLibrary *, void *)>(callback);
    if (each) {
        for (auto it = mLibraries.begin(); it != mLibraries.end(); ++it) {
            if (!each(const_cast<cGZCOMLibrary *>(&*it), context)) break;
        }
    }
    return 0;
}

uint32_t cGZCOM::EnumerateLibrariesToArray(void **out, uint32_t *count)
{
    uint32_t size = static_cast<uint32_t>(mLibraries.size());
    if (!out) {
        *count = size;
        return size;
    }
    auto it = mLibraries.begin();
    uint32_t index = 0;
    if (*count > size) *count = size;
    if (*count != 0) {
        while (true) {
            cGZCOMLibrary *library = const_cast<cGZCOMLibrary *>(&*it);
            out[index] = static_cast<cIGZCOMLibrary *>(library);
            library->AddRef();
            ++it;
            ++index;
            if (index >= *count) break;
        }
    }
    return *count;
}

bool cGZCOM::CreateGuid(uint32_t *out)
{
    // GUIDs are (3 random bits << 29) | local seconds since the GUID epoch; one per second: wait (in quarter
    // seconds) until the clock reaches the time the last batch reserved. The comparison is 64-bit against the
    // sign-extended stored time, as the original's (cdq).
    Lock(mCritSect);
    int64_t now = GuidTime();
    while (now < static_cast<int32_t>(mLastGuidTime)) {
        SleepMicroseconds(kGuidWaitMicroseconds);
        now = GuidTime();
    }
    uint32_t high = mRandom.RandomUint32Uniform(8) << 29;
    *out = (GuidSeconds() & 0x1fffffff) | high;
    mLastGuidTime = static_cast<uint32_t>(GuidTime()) + 1;
    Unlock(mCritSect);
    return true;
}

bool cGZCOM::CreateGuids(uint32_t *out, uint32_t count)
{
    // Eight GUIDs per second: the i-th is (i << 29) | ((seconds + i / 8) & 0x1fffffff); the batch reserves
    // (count + 7) / 8 seconds.
    Lock(mCritSect);
    if (count != 0) {
        int64_t now = GuidTime();
        while (now < static_cast<int32_t>(mLastGuidTime)) {
            SleepMicroseconds(kGuidWaitMicroseconds);
            now = GuidTime();
        }
        uint32_t seconds = GuidSeconds();
        for (uint32_t i = 0; i < count; ++i) out[i] = ((i >> 3) + seconds & 0x1fffffff) | i << 29;
        mLastGuidTime = static_cast<uint32_t>(GuidTime()) + ((count + 7) >> 3);
    }
    Unlock(mCritSect);
    return true;
}

// ---- adapters ----

cGZCOM *__fastcall cGZCOMAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cGZCOM;
}
void __fastcall cGZCOMAdapters::Destruct(cGZCOM *self, void *)
{
    [[msvc::forceinline_calls]] self->cGZCOM::~cGZCOM();
}
cGZCOM *__fastcall cGZCOMAdapters::DeletingDestructor(cGZCOM *self, void *, uint32_t flags)
{
    [[msvc::forceinline_calls]] self->cGZCOM::~cGZCOM();
    if (flags & 1) ::operator delete(self, sizeof(cGZCOM));
    return self;
}
bool __fastcall cGZCOMAdapters::QueryInterface(cIGZCOM *self, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return static_cast<cGZCOM *>(self)->cGZCOM::QueryInterface(riid, out);
}
uint32_t __fastcall cGZCOMAdapters::Release(cIGZCOM *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZCOM *>(self)->cGZCOM::Release();
}
uint32_t __fastcall cGZCOMAdapters::GetClassObject(cIGZCOM *self, void *, uint32_t classId, uint32_t iid, void **ppv)
{
    [[msvc::forceinline_calls]] return static_cast<cGZCOM *>(self)->cGZCOM::GetClassObject(classId, iid, ppv);
}
bool __fastcall cGZCOMAdapters::AddLibrary(cIGZCOM *self, void *, const cIGZString &path)
{
    [[msvc::forceinline_calls]] return static_cast<cGZCOM *>(self)->cGZCOM::AddLibrary(path);
}
void __fastcall cGZCOMAdapters::FreeUnusedLibraries(cIGZCOM *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZCOM *>(self)->cGZCOM::FreeUnusedLibraries();
}
bool __fastcall cGZCOMAdapters::CreateGuid(cIGZCOM *self, void *, uint32_t *out)
{
    [[msvc::forceinline_calls]] return static_cast<cGZCOM *>(self)->cGZCOM::CreateGuid(out);
}
bool __fastcall cGZCOMAdapters::CreateGuids(cIGZCOM *self, void *, uint32_t *out, uint32_t count)
{
    [[msvc::forceinline_calls]] return static_cast<cGZCOM *>(self)->cGZCOM::CreateGuids(out, count);
}
bool __fastcall cGZCOMAdapters::RealInit(cIGZCOM *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZCOM *>(self)->cGZCOM::RealInit();
}
void __fastcall cGZCOMAdapters::RealShutdown(cIGZCOM *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZCOM *>(self)->cGZCOM::RealShutdown();
}
void __fastcall cGZCOMAdapters::SetServiceRunning(cIGZCOM *self, void *, bool running)
{
    [[msvc::forceinline_calls]] return static_cast<cGZCOM *>(self)->cGZCOM::SetServiceRunning(running);
}
uint32_t __fastcall cGZCOMAdapters::EnumerateLibraries(cIGZCOM *self, void *, void *callback, void *context)
{
    [[msvc::forceinline_calls]] return static_cast<cGZCOM *>(self)->cGZCOM::EnumerateLibraries(callback, context);
}
uint32_t __fastcall cGZCOMAdapters::EnumerateLibrariesToArray(cIGZCOM *self, void *, void **out, uint32_t *count)
{
    [[msvc::forceinline_calls]] return static_cast<cGZCOM *>(self)->cGZCOM::EnumerateLibrariesToArray(out, count);
}
bool __fastcall cGZCOMAdapters::CanUnloadLibrary(cGZCOM *self, void *, cGZCOMLibrary *library)
{
    [[msvc::forceinline_calls]] return self->cGZCOM::CanUnloadLibrary(library);
}
void __fastcall cGZCOMAdapters::UnloadLibraries(cGZCOM *self, void *)
{
    [[msvc::forceinline_calls]] return self->cGZCOM::UnloadLibraries();
}
bool __fastcall cGZCOMAdapters::InitLibrary(cGZCOM *self, void *, cGZCOMLibrary *library)
{
    [[msvc::forceinline_calls]] return self->cGZCOM::InitLibrary(library);
}
bool __fastcall cGZCOMAdapters::LoadAndGetClassObject(cGZCOM *self, void *, cGZCOMLibrary *library, uint32_t classId, uint32_t iid, void **ppv)
{
    [[msvc::forceinline_calls]] return self->cGZCOM::LoadAndGetClassObject(library, classId, iid, ppv);
}

cGZCOMLibrary *__fastcall cGZCOMLibraryAdapters::Construct(void *memory, void *, const cIGZString &path)
{
    return ::new (memory) cGZCOMLibrary(path);
}
cGZCOMLibrary *__fastcall cGZCOMLibraryAdapters::CopyConstruct(void *memory, void *, const cGZCOMLibrary &other)
{
    return ::new (memory) cGZCOMLibrary(other);
}
void __fastcall cGZCOMLibraryAdapters::Destruct(cGZCOMLibrary *self, void *)
{
    [[msvc::forceinline_calls]] self->cGZCOMLibrary::~cGZCOMLibrary();
}
cGZCOMLibrary *__fastcall cGZCOMLibraryAdapters::DeletingDestructor(cGZCOMLibrary *self, void *, uint32_t flags)
{
    [[msvc::forceinline_calls]] self->cGZCOMLibrary::~cGZCOMLibrary();
    if (flags & 1) ::operator delete(self, sizeof(cGZCOMLibrary));
    return self;
}
bool __fastcall cGZCOMLibraryAdapters::QueryInterface(cIGZCOMLibrary *self, void *, uint32_t riid, void **ppv)
{
    [[msvc::forceinline_calls]] return static_cast<cGZCOMLibrary *>(self)->cGZCOMLibrary::QueryInterface(riid, ppv);
}
uint32_t __fastcall cGZCOMLibraryAdapters::Release(cIGZCOMLibrary *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZCOMLibrary *>(self)->cGZCOMLibrary::Release();
}
void __fastcall cGZCOMLibraryAdapters::GetPath(cIGZCOMLibrary *self, void *, cIGZString *out)
{
    [[msvc::forceinline_calls]] return static_cast<cGZCOMLibrary *>(self)->cGZCOMLibrary::GetPath(out);
}
bool __fastcall cGZCOMLibraryAdapters::Less(const cGZCOMLibrary *self, void *, const cGZCOMLibrary &other)
{
    [[msvc::forceinline_calls]] return self->cGZCOMLibrary::operator<(other);
}
bool __fastcall cGZCOMLibraryAdapters::Unload(cGZCOMLibrary *self, void *)
{
    [[msvc::forceinline_calls]] return self->cGZCOMLibrary::Unload();
}
bool __fastcall cGZCOMLibraryAdapters::Initialize(cGZCOMLibrary *self, void *)
{
    [[msvc::forceinline_calls]] return self->cGZCOMLibrary::Initialize();
}
