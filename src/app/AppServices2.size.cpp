#include "app/AppServices2.h"

#include "app/AppServices.h"
#include "game/game.h"
#include "game/import.h"
#include "game/vtable.h"
#include "app/ExceptionReporter.h"
#include "gz/GZCOM.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/RZTextConvert.h"
#include "rz/WinApiTables.h"
#include "rz/cGZAllocatorService.h"

#include <windows.h>

#include <cstring>
#include <cwchar>
#include <new>
#include <string>
#include <vector>

// The thread-safe-statics helpers of our CRT, used on the game's guard variables (see
// src/rz/cGZAllocatorService.cpp).
extern "C" __declspec(thread) int _Init_thread_epoch;

namespace as2 {

namespace {

// Game globals.
constexpr uint32_t kGetterObject = 0x10fdfd8;  // TestingCheatsEnabled's cached object
constexpr uint32_t kGetterGuard = 0x10fdfdc;
constexpr uint32_t kSetterObject = 0x10fdfe0;  // SetTestingCheatsEnabled's cached object
constexpr uint32_t kSetterGuard = 0x10fdfe4;
constexpr uint32_t kAnyBooleanVTable = 0xd9c670;

constexpr char kCheatsName[] = "testingCheatsEnabled";

using ThisFn0 = void *(__thiscall *)(void *);

// A service-allocated string built from a literal (the inlined constructor 0x1bec0e: zero the 16 buffer
// bytes and the size and capacity, then assign) and tidied with TidyString, on normal exit and when an
// exception unwinds (the original's unwind action is an equivalent tidy).
struct NamedString {
    RawString mString;

    explicit NamedString(const char *text)
    {
        std::memset(mString.mBuf, 0, sizeof(mString.mBuf));
        mString.mSize = 0;
        mString.mCapacity = 0;
        AssignServiceString(&mString, nullptr, text, static_cast<uint32_t>(std::strlen(text)));
    }
    ~NamedString() { TidyString(&mString, nullptr); }
    void *Address() { return &mString; }
};

// The AnyBoolean (cGZAny<AnyBoolean>) the property-set slots take: the vtable and the value byte.
struct AnyBooleanLocal {
    const void *mVTable;
    uint8_t mValue;
    uint8_t pad[3];
};

// The function-local static of both functions: the object acquired once from the service. Returns
// the cached pointer (read again after the initialisation, as the original does).
void *CachedObject(uint32_t guardRva, uint32_t objectRva)
{
    int *guard = GameAddress<int>(guardRva);
    void **object = GameAddress<void *>(objectRva);
    if (*guard > _Init_thread_epoch) {
        _Init_thread_header(guard);
        if (*guard == -1) {
            try {
                ScopedRef ref;
                AcquireTestingService(&ref, nullptr);
                *object = ref.mObject;
                ref.ReleaseObject();
            } catch (...) {
                _Init_thread_abort(guard);
                throw;
            }
            _Init_thread_footer(guard);
        }
    }
    return *object;
}

} // namespace

// The cached allocator service of 0x1bf795 and 0x1bf84e (first use: GetAllocatorService; no handler).
static void *CachedAllocatorService()
{
    int *guard = &ts2_rva_10c95d8;
    void **service = &ts2_rva_10c95d4;
    if (*guard > _Init_thread_epoch) {
        _Init_thread_header(guard);
        if (*guard == -1) {
            *service = GetAllocatorService();
            _Init_thread_footer(guard);
        }
    }
    return *service;
}

void __stdcall FreeViaAllocatorService(void *mem, uint32_t)
{
    void *service = CachedAllocatorService();
    using FreeFn = void(__thiscall *)(void *, void *);
    VirtualCall<FreeFn, 4>(service, mem);
}

void *__stdcall AllocateViaAllocatorService(uint32_t size, uint32_t)
{
    void *service = CachedAllocatorService();
    using AllocFn = void *(__thiscall *)(void *, uint32_t);
    return VirtualCall<AllocFn, 3>(service, size);
}

void __fastcall AssignServiceString(RawString *str, void *, const char *text, uint32_t length)
{
    if (length > 0x7fffffff) std::_Xlen_string();
    if (length <= 15) {
        str->mSize = length;
        str->mCapacity = 15;
        std::memmove(str->mBuf, text, length);
        str->mBuf[length] = 0;
        return;
    }
    // std::basic_string::_Calculate_growth(length, 15, 0x7fffffff): old capacity is 15, so the old / 2
    // overflow test never fires; the new capacity is the request rounded up to 16n - 1 or 1.5 times 15,
    // whichever is larger.
    uint32_t masked = length | 15;
    uint32_t capacity = masked > 0x7fffffff ? 0x7fffffff : (masked > 22 ? masked : 22);
    char *buffer = static_cast<char *>(AllocateViaAllocatorService(capacity + 1, 0));
    str->mSize = length;
    str->mPtr = buffer;
    str->mCapacity = capacity;
    std::memmove(buffer, text, length);
    buffer[length] = 0;
}

void __fastcall TidyString(RawString *str, void *)
{
    if (str->mCapacity > 15) FreeViaAllocatorService(str->mPtr, str->mCapacity + 1);
    str->mSize = 0;
    str->mCapacity = 15;
    str->mBuf[0] = 0;
}

ScopedRef *__fastcall AcquireTestingService(ScopedRef *ref, void *)
{
    ref->mObject = nullptr;
    void *service = GetServiceObject();
    if (service) {
        // The result of slot 5 is not used.
        using Query = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
        VirtualCall<Query, 5>(service, 0x0ac1887e, 0x6bb22f38, &ref->mObject);
    }
    return ref;
}

uint32_t __fastcall TestingCheatsEnabled(void *, void *)
{
    if (!CachedObject(kGetterGuard, kGetterObject)) return 0;
    // The original's upper bits are not defined (xor al,al / the byte); callers use al.
    bool known;
    {
        NamedString name(kCheatsName);
        void *object = *GameAddress<void *>(kGetterObject);
        void *properties = VirtualCall<ThisFn0, 3>(object);
        using HasFn = uint32_t(__thiscall *)(void *, void *);
        known = static_cast<uint8_t>(VirtualCall<HasFn, 10>(properties, name.Address())) != 0;
    }
    if (!known) return 0;
    AnyBooleanLocal value{GameAddress(kAnyBooleanVTable), 0, {}};
    {
        NamedString name(kCheatsName);
        void *object = *GameAddress<void *>(kGetterObject);
        void *properties = VirtualCall<ThisFn0, 3>(object);
        using GetFn = void(__thiscall *)(void *, void *, void *);
        VirtualCall<GetFn, 9>(properties, name.Address(), &value);
    }
    return value.mValue;
}

void __fastcall SetTestingCheatsEnabled(void *, void *, uint32_t flag)
{
    if (!CachedObject(kSetterGuard, kSetterObject)) return;
    AnyBooleanLocal value{GameAddress(kAnyBooleanVTable), static_cast<uint8_t>(flag), {}};
    NamedString name(kCheatsName);
    void *object = *GameAddress<void *>(kSetterObject);
    void *properties = VirtualCall<ThisFn0, 3>(object);
    using SetFn = void(__thiscall *)(void *, void *, void *);
    VirtualCall<SetFn, 3>(properties, name.Address(), &value);
}

// ---- file search and directory checks ----

namespace {

// The import slots (ts2_rva_d9a1f8, ts2_rva_d9a1f4), the wide directory function table (ts2_rva_10cb614: FindFirstFileW at
// +4) and the wildcard text pointer (ts2_rva_ff2478) are owned statics (AppServices2.h, rz/WinApiTables.h).
constexpr int kSlotFromChar = 3, kSlotText = 5, kSlotData = 6, kSlotStrlen = 7;

uint32_t StringLength(const cIGZString *s)
{
    return VirtualCall<uint32_t(__thiscall *)(const void *), kSlotStrlen>(s);
}

// A new node with both strings built (operator new throws, so the original's null branch is dead).
FindNode *NewFindNode()
{
    auto *node = static_cast<FindNode *>(::operator new(sizeof(FindNode)));
    node->mName.Construct();
    node->mPattern.Construct();
    return node;
}

} // namespace

bool __cdecl ConvertStringFrom(const cIGZString *src, int32_t srcEncoding, cIGZString *dst)
{
    int32_t native = RZText::DefaultEncoding();
    if (srcEncoding != RZText::kDefault) {
        if (srcEncoding == RZText::kSystemCodePage) srcEncoding = RZText::SystemCodePage();
        if (srcEncoding != native) {
            if (srcEncoding == RZText::kUtf16) return false;
            uint32_t length = StringLength(src);
            uint32_t capacity = length * 6;
            char stackBuffer[0x200];
            char *heap = capacity < 0x200 ? nullptr : static_cast<char *>(::operator new[](capacity));
            char *buffer = heap ? heap : stackBuffer;
            buffer[0] = 0;
            const char *text = VirtualCall<const char *(__thiscall *)(const void *), kSlotData>(src);
            bool ok = RZText::Convert(text, length, srcEncoding, buffer, &capacity, native) == length;
            VirtualCall<void(__thiscall *)(void *, const char *, uint32_t), kSlotFromChar>(dst, buffer, capacity);
            ::operator delete[](heap);
            return ok;
        }
    }
    if (src != dst) {
        auto fromChar = VirtualSlot<void(__thiscall *)(void *, const char *, uint32_t)>(dst, kSlotFromChar); // read first
        uint32_t length = StringLength(src);
        const char *text = VirtualCall<const char *(__thiscall *)(const void *), kSlotText>(src);
        fromChar(dst, text, length);
    }
    return true;
}

std::u16string *__cdecl StringToUtf16(void *out, const cIGZString *src)
{
    // The string is initialised in place as the original does: 16 zeroed bytes of buffer, size 0,
    // capacity 7 (not the library constructor, which leaves the buffer alone).
    struct Raw16 {
        char16_t mBuf[8];
        uint32_t mSize;
        uint32_t mCapacity;
    };
    static_assert(sizeof(Raw16) == sizeof(std::u16string), "std::u16string layout");
    auto *raw = static_cast<Raw16 *>(out);
    std::memset(raw->mBuf, 0, sizeof(raw->mBuf));
    raw->mSize = 0;
    raw->mCapacity = 7;
    auto *result = static_cast<std::u16string *>(out);
    uint32_t units = StringLength(src) * 6;
    if (units > 0x200) {
        std::vector<short> buffer(units);
        RZText::ConvertString(*src, buffer.data(), &units, RZText::kUtf16);
        result->assign(reinterpret_cast<const char16_t *>(buffer.data()), units);
    } else {
        short buffer[0x200];
        units = 0x200;
        RZText::ConvertString(*src, buffer, &units, RZText::kUtf16);
        result->assign(reinterpret_cast<const char16_t *>(buffer), units);
    }
    return result;
}

FindNode *__cdecl FindFirst(cRZString *pattern)
{
    // No unwinding: the original has no exception frame, so nothing is cleaned up if a call throws.
    using FindFirstW = HANDLE(__stdcall *)(const wchar_t *, void *);
    using FindFirstA = HANDLE(__stdcall *)(const char *, void *);
    if (FindFirstW wide = reinterpret_cast<FindFirstW>(ts2_rva_10cb614.findFirstFile)) {
        alignas(std::u16string) char storage[sizeof(std::u16string)];
        std::u16string *text = StringToUtf16(storage, pattern->AsInterface());
        WIN32_FIND_DATAW data; // left unset until the call fills it
        HANDLE handle = wide(reinterpret_cast<const wchar_t *>(text->c_str()), &data);
        text->~basic_string();
        if (handle == INVALID_HANDLE_VALUE) return nullptr;
        FindNode *node = NewFindNode();
        uint32_t length = static_cast<uint32_t>(std::wcslen(data.cFileName));
        ConvertBytesToString(data.cFileName, length, RZText::kUtf16, node->mName.AsInterface());
        node->mIsDirectory = (data.dwFileAttributes >> 4) & 1;
        reinterpret_cast<RZStringSlot *>(&node->mPattern.mString)->Assign(pattern->mString);
        node->mHandle = handle;
        return node;
    }
    alignas(cRZString) char storage[sizeof(cRZString)];
    auto *converted = reinterpret_cast<cRZString *>(storage);
    converted->Construct();
    RZText::ConvertStringToString(*pattern->AsInterface(), *converted->AsInterface(), RZText::kSystemCodePage);
    WIN32_FIND_DATAA data;
    HANDLE handle = reinterpret_cast<FindFirstA>(ts2_rva_d9a1f8)(converted->mString.c_str(), &data);
    if (handle == INVALID_HANDLE_VALUE) {
        converted->Destruct();
        return nullptr;
    }
    FindNode *node = NewFindNode();
    node->mName.mString.assign(data.cFileName, std::strlen(data.cFileName));
    ConvertStringFrom(node->mName.AsInterface(), RZText::kSystemCodePage, node->mName.AsInterface());
    node->mIsDirectory = (data.dwFileAttributes >> 4) & 1;
    reinterpret_cast<RZStringSlot *>(&node->mPattern.mString)->Assign(pattern->mString);
    node->mHandle = handle;
    converted->Destruct();
    return node;
}

void __fastcall DestructFindNode(FindNode *node, void *)
{
    node->mPattern.Destruct();
    node->mName.Destruct();
}

void __cdecl CloseFind(FindNode *node)
{
    if (!node) return;
    if (node->mHandle != INVALID_HANDLE_VALUE) reinterpret_cast<BOOL(__stdcall *)(HANDLE)>(ts2_rva_d9a1f4)(node->mHandle);
    DestructFindNode(node, nullptr);
    ::operator delete(node, sizeof(FindNode));
}

bool __cdecl PatternMatches(cRZString *pattern)
{
    FindNode *node = FindFirst(pattern);
    if (!node) return false;
    CloseFind(node);
    return true;
}

bool __cdecl DirectoryExists(cRZString *path)
{
    alignas(cRZString) char storage[sizeof(cRZString)];
    auto *copy = reinterpret_cast<cRZString *>(storage);
    copy->Construct(*path, 0, std::string::npos);
    std::string &text = copy->mString;
    if (text.size() == 0 || (text.back() != '\\' && text.back() != '/')) text.push_back('\\');
    text.append(ts2_rva_ff2478);
    bool found = PatternMatches(copy);
    copy->Destruct();
    return found;
}

namespace {

void AssignText(cRZString &target, const char *text)
{
    target.mString.assign(text, std::strlen(text));
}

} // namespace

uint32_t __cdecl CopyModulePath(char *buffer, uint32_t capacity)
{
    alignas(cRZString) char storage[sizeof(cRZString)];
    auto *path = reinterpret_cast<cRZString *>(storage);
    path->Construct();
    GetModulePath(path->AsInterface());
    uint32_t size = static_cast<uint32_t>(path->mString.size());
    uint32_t result = 0;
    if (capacity > size) {
        std::memcpy(buffer, path->mString.c_str(), size);
        buffer[size] = 0;
        result = size;
    }
    path->Destruct();
    return result;
}

// The shared body of the three directory setters: `directory` and `flag` are the object's fields.
bool SetDirectory(cRZString &directory, uint8_t &flag, const cIGZString *path)
{
    uint32_t length = StringLength(path);
    const char *text = VirtualCall<const char *(__thiscall *)(const void *), kSlotData>(path);
    directory.mString.assign(text, length);
    if (!DirectoryExists(&directory)) return false;
    flag = 1;
    return true;
}

} // namespace as2

// ---- cGZFileSystem's members (src/app/AppServices.h) ----

void cGZFileSystem::ResolveApplicationPaths()
{
    // ORIGINAL-BUG: a module path of 260 characters or more is not copied (CopyModulePath returns 0
    // and leaves the buffer alone), and the split then reads uninitialised stack.
    char path[0x104];
    char drive[4], directory[0x100], name[0x100], extension[0x100];
    as2::CopyModulePath(path, 0x104);
    appbase::SplitPath(path, drive, directory, name, extension);
    as2::AssignText(mAppDrive, drive);
    std::string &driveText = mAppDrive.mString;
    // ORIGINAL-BUG: an empty drive (a UNC path) makes this read the byte before the string's buffer.
    char last = driveText.c_str()[static_cast<ptrdiff_t>(driveText.size()) - 1];
    if (last != '\\' && last != '/') driveText.push_back('\\');
    std::string combined(drive);
    combined.append(directory, std::strlen(directory));
    reinterpret_cast<RZStringSlot *>(&mAppDirectory.mString)->Assign(combined);
    as2::AssignText(mAppName, name);
    as2::AssignText(mAppExtension, extension);
    mAppDirectorySet = 1;
}

bool cGZFileSystem::FindCDDrive()
{
    using LogicalDrivesFn = DWORD(__stdcall *)(DWORD, char *);
    using DriveTypeFn = UINT(__stdcall *)(const char *);
    constexpr uint32_t kIatGetLogicalDriveStringsA = 0xd9a248, kIatGetDriveTypeA = 0xd9a23c;
    bool found = false;
    int32_t count = static_cast<int32_t>(ImportFunction<LogicalDrivesFn, kIatGetLogicalDriveStringsA>()(0, nullptr));
    if (count > 0) {
        char *drives = static_cast<char *>(::operator new[](static_cast<size_t>(count) + 2));
        ImportFunction<LogicalDrivesFn, kIatGetLogicalDriveStringsA>()(static_cast<DWORD>(count) + 1, drives);
        int32_t limit = count - 1;
        int32_t at = 0;
        if (limit > 0) {
            do {
                char *drive = drives + at;
                if (*drive < 0x61) *drive = static_cast<char>(*drive + 0x20); // only the first character, as signed chars
                UINT type = ImportFunction<DriveTypeFn, kIatGetDriveTypeA>()(drive);
                if (type == DRIVE_CDROM || (*drive >= 0x63 && type == DRIVE_REMOVABLE)) {
                    mCDAppDrive.mString.assign(drive); // 0x1adad0 is assign(const char *), not append
                    if (CheckCDDrive()) {
                        found = true;
                        break;
                    }
                }
                while (at < limit && drives[at] != 0) ++at;
                ++at;
            } while (at < limit);
        }
        ::operator delete[](drives);
    }
    if (!found) {
        reinterpret_cast<RZStringSlot *>(&mCDAppDrive.mString)->Assign(mAppDrive.mString);
        reinterpret_cast<RZStringSlot *>(&mCDAppDirectory.mString)->Assign(mAppDirectory.mString);
        reinterpret_cast<RZStringSlot *>(&mCDDataDirectory.mString)->Assign(mDataDirectory.mString);
        reinterpret_cast<RZStringSlot *>(&mCDPlugInDirectory.mString)->Assign(mPlugInDirectory.mString);
    }
    mCDResolved = 1;
    return found;
}

bool cGZFileSystem::SetAppDirectory(const cIGZString *path)
{
    return as2::SetDirectory(mAppDirectory, mAppDirectorySet, path);
}

bool cGZFileSystem::SetDataDirectory(const cIGZString *path)
{
    return as2::SetDirectory(mDataDirectory, mDataDirectorySet, path);
}

bool cGZFileSystem::SetPlugInDirectory(const cIGZString *path)
{
    return as2::SetDirectory(mPlugInDirectory, mPlugInDirectorySet, path);
}
