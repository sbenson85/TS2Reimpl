#include "rz/cRZFile.h"

#include "game/game.h"
#include "game/vtable.h"
#include "rz/RZTextConvert.h"
#include "rz/WinApiTables.h"

#include <windows.h>
#include <shobjidl.h>

#include <cstring>
#include <new>

namespace {

// Slot numbers of cIGZFile calls the original makes through the vtable.
constexpr int kSlotAddRef = 1;
constexpr int kSlotSeek = 13;
constexpr int kSlotReadWithCount = 15;
constexpr int kSlotWriteWithCount = 17;
constexpr int kSlotDeletingDestructor = 33;
// cIGZString slots used on path arguments.
constexpr int kStringSlotFromChar = 3;
constexpr int kStringSlotData = 6;
constexpr int kStringSlotStrlen = 7;

HANDLE Handle(void *h)
{
    return static_cast<HANDLE>(h);
}

// SetBuffering's size rule: -1 means 0 (unbuffered), sizes are rounded down to even, 2 becomes 4,
// and anything above kMaxBufferSize (including other negative values) is capped.
uint32_t BufferSize(int32_t requested)
{
    uint32_t size = static_cast<uint32_t>(requested != -1 ? requested : 0) & ~1u;
    if (size - 1 <= 2) return 4;
    return size > cRZFile::kMaxBufferSize ? cRZFile::kMaxBufferSize : size;
}

} // namespace

// Construction and destruction ------------------------------------------------------------------

namespace {

void InitFields(cRZFile *f)
{
    f->mVTable = cRZFile::VTable();
    f->mHandle = INVALID_HANDLE_VALUE;
    f->mError = cRZFile::kErrorNone;
    f->mOpen = false;
    f->mLengthCached = false;
    f->mAccess = f->mCreation = f->mSharing = 0;
    f->mRefCount = 0;
    f->mUnknownString.Construct();
    f->mCachedLength = 0;
    f->mPosition = f->mFilePosition = 0;
    f->mReadBufferSize = 0;
    new (&f->mReadBuffer) std::vector<char>();
    f->mReadBufferStart = f->mReadBufferLength = 0;
    f->mWriteBufferSize = 0;
    new (&f->mWriteBuffer) std::vector<char>();
    f->mWriteBufferStart = f->mWriteBufferLength = 0;
}

} // namespace

cRZFile *cRZFile::Construct(const cIGZString &path)
{
    mPath.Construct(path);
    InitFields(this);
    return this;
}

cRZFile *cRZFile::Construct(const char *path)
{
    mPath.Construct();
    InitFields(this);
    if (path) mPath.mString.assign(path);
    return this;
}

cRZFile *cRZFile::Construct()
{
    mPath.Construct();
    InitFields(this);
    return this;
}

void cRZFile::Destruct()
{
    mVTable = VTable();
    if (mOpen) Close(); // a direct call in the original, not through the vtable
    mWriteBuffer.~vector();
    mReadBuffer.~vector();
    mUnknownString.Destruct();
    mPath.Destruct();
}

cRZFile *cRZFile::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cRZFile));
    return this;
}

// cIGZUnknown ------------------------------------------------------------------------------------

bool cRZFile::QueryInterface(uint32_t riid, void **ppvObj)
{
    if (riid != kGZIID_cIGZFile && riid != kGZIID_cIGZUnknown) return false;
    *ppvObj = this;
    VirtualCall<uint32_t(__thiscall *)(void *), kSlotAddRef>(this);
    return true;
}

uint32_t cRZFile::AddRef()
{
    return ++mRefCount;
}

uint32_t cRZFile::Release()
{
    if (mRefCount > 1) return --mRefCount;
    VirtualCall<void *(__thiscall *)(void *, uint32_t), kSlotDeletingDestructor>(this, 1);
    return 0;
}

// Helpers ----------------------------------------------------------------------------------------

void cRZFile::SetErrorFromLastError()
{
    DWORD e = GetLastError();
    if (e == ERROR_ACCESS_DENIED) mError = kErrorAccessDenied;
    else if (e == ERROR_ALREADY_EXISTS) mError = kErrorBusy;
    else mError = e;
}

void cRZFile::ClearReadBuffer()
{
    mReadBufferStart = 0;
    mReadBufferLength = 0;
}

void cRZFile::ClearWriteBuffer()
{
    mWriteBufferStart = 0;
    mWriteBufferLength = 0;
}

bool cRZFile::FlushWriteBuffer()
{
    if (mWriteBufferLength == 0) return true;
    DWORD written = static_cast<DWORD>(mWriteBufferLength);
    bool ok = WriteFile(Handle(mHandle), mWriteBuffer.data(), static_cast<DWORD>(mWriteBufferLength), &written, nullptr) != 0;
    int32_t position;
    if (ok) {
        position = mFilePosition + static_cast<int32_t>(written);
        mWriteBufferLength = 0;
    } else {
        // The original records no error here; it re-reads the OS position and drops the buffer.
        position = static_cast<int32_t>(SetFilePointer(Handle(mHandle), 0, nullptr, FILE_CURRENT));
        mWriteBufferLength = 0;
    }
    mWriteBufferStart = position;
    mFilePosition = position;
    return ok;
}

int32_t cRZFile::RawRead(void *dst, uint32_t length)
{
    DWORD read = length; // the original passes the address of its length argument
    if (ReadFile(Handle(mHandle), dst, length, &read, nullptr)) {
        mFilePosition += static_cast<int32_t>(read);
        return static_cast<int32_t>(read);
    }
    mFilePosition = static_cast<int32_t>(SetFilePointer(Handle(mHandle), 0, nullptr, FILE_CURRENT));
    return -1;
}

int32_t cRZFile::RawSeek(int32_t offset, uint32_t origin)
{
    DWORD method;
    switch (origin) {
    case 0: method = FILE_BEGIN; break;
    case 1: method = FILE_CURRENT; break;
    case 2: method = FILE_END; break;
    default: return mFilePosition;
    }
    DWORD result = SetFilePointer(Handle(mHandle), offset, nullptr, method);
    if (result == INVALID_SET_FILE_POINTER) {
        mFilePosition = static_cast<int32_t>(SetFilePointer(Handle(mHandle), 0, nullptr, FILE_CURRENT));
        SetErrorFromLastError();
    } else {
        mFilePosition = static_cast<int32_t>(result);
    }
    return static_cast<int32_t>(result);
}

bool cRZFile::FillReadBuffer()
{
    if (mReadBuffer.empty()) mReadBuffer.resize(mReadBufferSize);
    mReadBufferStart = mFilePosition;
    int32_t read = RawRead(mReadBuffer.data(), mReadBufferSize);
    bool ok = read >= 0;
    if (!ok) {
        mReadBufferStart = 0;
        read = 0;
    }
    mReadBufferLength = read;
    return ok;
}

bool cRZFile::BufferedWrite(const void *src, int32_t length)
{
    bool ok = true;
    if (length <= 0) return ok;
    if (mWriteBuffer.empty()) mWriteBuffer.resize(mWriteBufferSize);
    if (mWriteBufferLength == 0) mWriteBufferStart = mFilePosition;
    if (mWriteBufferLength + length <= static_cast<int32_t>(mWriteBufferSize)) {
        std::memcpy(mWriteBuffer.data() + mWriteBufferLength, src, length);
        mWriteBufferLength += length;
        return ok;
    }
    auto *p = static_cast<const char *>(src);
    while (ok) {
        int32_t chunk = static_cast<int32_t>(mWriteBufferSize) - mWriteBufferLength;
        if (chunk >= length) chunk = length;
        if (chunk != 0) {
            std::memcpy(mWriteBuffer.data() + mWriteBufferLength, p, chunk);
            mWriteBufferLength += chunk;
            p += chunk;
            length -= chunk;
        }
        if (mWriteBufferLength == static_cast<int32_t>(mWriteBufferSize)) ok = FlushWriteBuffer();
        if (length == 0) break;
    }
    return ok;
}

// cIGZFile ---------------------------------------------------------------------------------------

bool cRZFile::Close()
{
    if (!mOpen) return false;
    FlushWriteBuffer();
    ClearReadBuffer();
    ClearWriteBuffer();
    if (!CloseHandle(Handle(mHandle))) SetErrorFromLastError();
    mHandle = INVALID_HANDLE_VALUE;
    mAccess = mCreation = mSharing = 0;
    mFilePosition = 0; // mPosition is left as it was
    mOpen = false;
    mLengthCached = false;
    return true;
}

int32_t cRZFile::Position() const
{
    return mOpen ? mPosition : -1;
}

uint32_t cRZFile::Length()
{
    if (!mOpen) return LengthOfPath(*mPath.AsInterface());
    if (mLengthCached) return mCachedLength;
    DWORD length = GetFileSize(Handle(mHandle), nullptr);
    if (length == INVALID_FILE_SIZE) {
        SetErrorFromLastError();
        return length;
    }
    // Pending buffered writes: the caller's position counts as the end if it's further.
    if (mWriteBufferLength != 0 && length < static_cast<uint32_t>(mPosition)) length = static_cast<uint32_t>(mPosition);
    if (!(mAccess & 2)) {
        mCachedLength = length;
        mLengthCached = true;
    }
    return length;
}

bool cRZFile::SetLength(uint32_t length)
{
    if (!mOpen) return false;
    ClearReadBuffer();
    FlushWriteBuffer();
    bool ok = SetFilePointer(Handle(mHandle), static_cast<LONG>(length), nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER
        && SetEndOfFile(Handle(mHandle));
    if (!ok) SetErrorFromLastError();
    int32_t position = static_cast<int32_t>(SetFilePointer(Handle(mHandle), 0, nullptr, FILE_CURRENT));
    mFilePosition = position;
    mPosition = position;
    return ok; // the cached length is not invalidated
}

int32_t cRZFile::SeekToBegin()
{
    return VirtualCall<int32_t(__thiscall *)(void *, int32_t, uint32_t), kSlotSeek>(this, 0, 0);
}

int32_t cRZFile::SeekToEnd()
{
    return VirtualCall<int32_t(__thiscall *)(void *, int32_t, uint32_t), kSlotSeek>(this, 0, 2);
}

int32_t cRZFile::SeekToRelativePosition(int32_t offset)
{
    return VirtualCall<int32_t(__thiscall *)(void *, int32_t, uint32_t), kSlotSeek>(this, offset, 1);
}

int32_t cRZFile::SeekToPosition(int32_t position)
{
    return VirtualCall<int32_t(__thiscall *)(void *, int32_t, uint32_t), kSlotSeek>(this, position, 0);
}

int32_t cRZFile::Seek(int32_t offset, uint32_t origin)
{
    if (!mOpen) return -1;
    // Relative seeks are relative to the caller's position, which the OS pointer may not match.
    if (origin == 1 && mFilePosition != mPosition) {
        offset += mPosition;
        origin = 0;
    }
    if (mReadBufferLength != 0 && origin != 2) {
        // With read data buffered, a seek only moves the logical position (if not negative).
        if (origin == 1) {
            origin = 0;
            offset += mPosition;
        }
        if (offset >= 0) {
            mPosition = offset;
            return offset;
        }
    } else if (origin == 1 && offset == 0) {
        return mPosition;
    }
    if (origin == 0 && offset == mPosition) return mPosition;
    FlushWriteBuffer();
    RawSeek(offset, origin);
    mPosition = mFilePosition;
    return mPosition;
}

bool cRZFile::Read(void *dst, uint32_t length)
{
    uint32_t count = length;
    if (!VirtualCall<bool(__thiscall *)(void *, void *, uint32_t &), kSlotReadWithCount>(this, dst, count)) return false;
    return count == length;
}

bool cRZFile::ReadWithCount(void *dst, uint32_t &length)
{
    if (!mOpen) return false;
    if (length == 0) return true;
    if (mWriteBufferLength != 0) FlushWriteBuffer();
    uint32_t remaining = length;

    if (mReadBufferSize == 0) {
        // The original passes &length as the byte count out-parameter.
        BOOL ok = ReadFile(Handle(mHandle), dst, remaining, reinterpret_cast<DWORD *>(&length), nullptr);
        if (ok) {
            mFilePosition += static_cast<int32_t>(length);
        } else {
            mFilePosition = static_cast<int32_t>(SetFilePointer(Handle(mHandle), 0, nullptr, FILE_CURRENT));
            SetErrorFromLastError();
        }
        mPosition = mFilePosition;
        return ok != 0;
    }

    auto *out = static_cast<char *>(dst);
    bool ok = true;
    // Serve what the read buffer already holds.
    if (mPosition >= mReadBufferStart && mPosition < mReadBufferStart + mReadBufferLength) {
        int32_t offset = mPosition - mReadBufferStart;
        int32_t available = mReadBufferLength - offset;
        int32_t take = available < static_cast<int32_t>(remaining) ? available : static_cast<int32_t>(remaining);
        std::memcpy(out, mReadBuffer.data() + offset, take);
        out += take;
        remaining -= take;
        mPosition += take;
    }
    while (remaining != 0) {
        ClearReadBuffer();
        if (mFilePosition != mPosition) {
            ok = RawSeek(mPosition, 0) == mPosition;
            if (!ok) break;
        }
        if (remaining > 2 * mReadBufferSize) {
            // Large reads bypass the buffer (one ReadFile; a short read is not retried).
            int32_t read = RawRead(out, remaining);
            if (read < 0) {
                ok = false;
                break;
            }
            mPosition += read;
            remaining -= read;
            break;
        }
        ok = FillReadBuffer();
        if (!ok) break;
        int32_t take = mReadBufferLength;
        if (take == 0) break; // end of file
        if (take >= static_cast<int32_t>(remaining)) take = static_cast<int32_t>(remaining);
        std::memcpy(out, mReadBuffer.data(), take);
        out += take;
        mPosition += take;
        remaining -= take;
    }
    length -= remaining;
    return ok;
}

bool cRZFile::Write(const void *src, uint32_t length)
{
    uint32_t count = length;
    if (!VirtualCall<bool(__thiscall *)(void *, const void *, uint32_t &), kSlotWriteWithCount>(this, src, count)) return false;
    return count == length;
}

bool cRZFile::WriteWithCount(const void *src, uint32_t &length)
{
    if (!mOpen) return false;
    if (mReadBufferLength != 0) {
        ClearReadBuffer();
        if (mPosition != mFilePosition) RawSeek(mPosition, 0);
    }
    if (mWriteBufferSize != 0) {
        bool ok = BufferedWrite(src, static_cast<int32_t>(length));
        mPosition += static_cast<int32_t>(length);
        return ok;
    }
    BOOL ok = WriteFile(Handle(mHandle), src, length, reinterpret_cast<DWORD *>(&length), nullptr);
    if (ok) {
        mFilePosition += static_cast<int32_t>(length);
    } else {
        mFilePosition = static_cast<int32_t>(SetFilePointer(Handle(mHandle), 0, nullptr, FILE_CURRENT));
        SetErrorFromLastError();
    }
    mPosition = mFilePosition;
    return ok != 0;
}

bool cRZFile::Flush()
{
    if (!mOpen) return false;
    bool ok = FlushWriteBuffer();
    mLengthCached = false;
    return ok;
}

bool cRZFile::SysFlush()
{
    return FlushFileBuffers(Handle(mHandle)) != 0;
}

void cRZFile::GetPath(cIGZString &out) const
{
    VirtualCall<void(__thiscall *)(void *, const char *, uint32_t), kStringSlotFromChar>(&out, mPath.mString.c_str(), static_cast<uint32_t>(mPath.mString.size()));
}

void cRZFile::SetPath(const char *path)
{
    if (path) mPath.mString.assign(path);
}

void cRZFile::SetPath(const cIGZString &path)
{
    void *p = const_cast<cIGZString *>(&path);
    uint32_t length = VirtualCall<uint32_t(__thiscall *)(void *), kStringSlotStrlen>(p);
    const char *data = VirtualCall<const char *(__thiscall *)(void *), kStringSlotData>(p);
    mPath.mString.assign(data, length);
}

bool cRZFile::GetOpenModes(uint32_t *access, uint32_t *creation, uint32_t *sharing) const
{
    if (!mOpen) return false;
    *access = mAccess;
    *creation = mCreation;
    *sharing = mSharing;
    return true;
}

uintptr_t cRZFile::GetBuffering(int32_t *read, int32_t *write) const
{
    *read = static_cast<int32_t>(mReadBufferSize);
    *write = static_cast<int32_t>(mWriteBufferSize);
    return reinterpret_cast<uintptr_t>(write);
}

bool cRZFile::SetBuffering(int32_t readSize, int32_t writeSize)
{
    uint32_t size = BufferSize(readSize);
    if (size < mReadBufferSize) ClearReadBuffer();
    mReadBufferSize = size;
    if (!mReadBuffer.empty()) mReadBuffer.resize(size);

    size = BufferSize(writeSize);
    if (size < mWriteBufferSize) FlushWriteBuffer();
    mWriteBufferSize = size;
    if (!mWriteBuffer.empty()) mWriteBuffer.resize(size);
    return true;
}

// Paths ------------------------------------------------------------------------------------------

namespace {

constexpr int kSlotIsOpen = 4;
constexpr int kSlotClose = 5;
constexpr int kSlotGetPath = 25;
constexpr int kStringSlotAssign = 16;

// The game's table of wide file functions, filled in at startup when they're available.
template <typename Fn> Fn WideFunction(rz::Proc rz::WideFileFunctions::*field)
{
    return reinterpret_cast<Fn>(ts2_rva_10cb5e0.*field);
}
using CreateFileWFn = HANDLE(WINAPI *)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using CopyFileWFn = BOOL(WINAPI *)(LPCWSTR, LPCWSTR, BOOL);
using MoveFileExWFn = BOOL(WINAPI *)(LPCWSTR, LPCWSTR, DWORD);
using FindFirstFileWFn = HANDLE(WINAPI *)(LPCWSTR, LPWIN32_FIND_DATAW);
using DeleteFileWFn = BOOL(WINAPI *)(LPCWSTR);
using GetFileAttributesWFn = DWORD(WINAPI *)(LPCWSTR);
constexpr auto kCreateFileW = &rz::WideFileFunctions::createFile;
constexpr auto kCopyFileW = &rz::WideFileFunctions::copyFile;
constexpr auto kMoveFileExW = &rz::WideFileFunctions::moveFileEx;
constexpr auto kFindFirstFileW = &rz::WideFileFunctions::findFirstFile;
constexpr auto kDeleteFileW = &rz::WideFileFunctions::deleteFile;
constexpr auto kGetFileAttributesW = &rz::WideFileFunctions::getFileAttributes;

const wchar_t *Wide(const std::u16string &s)
{
    return reinterpret_cast<const wchar_t *>(s.c_str());
}

// UTF-16 form of a path (the original's 0x1c2ad8 into a local).
struct WidePath {
    alignas(std::u16string) unsigned char storage[sizeof(std::u16string)];
    std::u16string *str;
    explicit WidePath(const cIGZString &path) : str(RZText::ToUtf16(reinterpret_cast<std::u16string *>(storage), path)) {}
    ~WidePath() { str->~basic_string(); }
    WidePath(const WidePath &) = delete;
    WidePath &operator=(const WidePath &) = delete;
    const wchar_t *c_str() const { return Wide(*str); }
};

// ANSI form of a path: a local cRZString converted to the system code page.
struct AnsiPath {
    cRZString text;
    explicit AnsiPath(const cIGZString &path)
    {
        text.Construct();
        RZText::ConvertStringToString(path, *text.AsInterface(), RZText::kSystemCodePage);
    }
    ~AnsiPath() { text.Destruct(); }
    AnsiPath(const AnsiPath &) = delete;
    AnsiPath &operator=(const AnsiPath &) = delete;
    const char *c_str() const { return text.mString.c_str(); }
};

bool IsOpenSlot(cRZFile *f)
{
    return VirtualCall<bool(__thiscall *)(void *), kSlotIsOpen>(f);
}
void CloseSlot(cRZFile *f)
{
    VirtualCall<bool(__thiscall *)(void *), kSlotClose>(f);
}

// COM entry points looked up in Ole32.dll by the shortcut resolvers.
struct Ole32 {
    HMODULE module = nullptr;
    HRESULT(WINAPI *coInitialize)(LPVOID) = nullptr;
    void(WINAPI *coUninitialize)() = nullptr;
    HRESULT(WINAPI *coCreateInstance)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID *) = nullptr;
    bool Load()
    {
        module = LoadLibraryA("Ole32.dll");
        if (!module) return false;
        coInitialize = reinterpret_cast<decltype(coInitialize)>(GetProcAddress(module, "CoInitialize"));
        coUninitialize = reinterpret_cast<decltype(coUninitialize)>(GetProcAddress(module, "CoUninitialize"));
        coCreateInstance = reinterpret_cast<decltype(coCreateInstance)>(GetProcAddress(module, "CoCreateInstance"));
        return true;
    }
    bool Usable() const { return coInitialize && coUninitialize && coCreateInstance; }
};

// ShellLink and its interfaces, as in the game's data (RVAs 0xe47d50..0xe47d80).
constexpr GUID kClsidShellLink = {0x00021401, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
constexpr GUID kIidShellLinkW = {0x000214F9, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
constexpr GUID kIidShellLinkA = {0x000214EE, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
constexpr GUID kIidPersistFile = {0x0000010B, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};

} // namespace

uint32_t cRZFile::LengthOfPath(const cIGZString &path)
{
    uint32_t length = INVALID_FILE_SIZE;
    if (auto findFirst = WideFunction<FindFirstFileWFn>(kFindFirstFileW)) {
        WIN32_FIND_DATAW data;
        HANDLE h;
        {
            WidePath wide(path);
            h = findFirst(wide.c_str(), &data);
        }
        if (h != INVALID_HANDLE_VALUE) {
            length = data.nFileSizeLow;
            FindClose(h);
        }
    } else {
        AnsiPath ansi(path);
        WIN32_FIND_DATAA data;
        HANDLE h = FindFirstFileA(ansi.c_str(), &data);
        if (h != INVALID_HANDLE_VALUE) {
            length = data.nFileSizeLow;
            FindClose(h);
        }
    }
    return length;
}

bool cRZFile::PathExists(const cIGZString &path)
{
    if (auto getAttributes = WideFunction<GetFileAttributesWFn>(kGetFileAttributesW)) {
        WidePath wide(path);
        return getAttributes(wide.c_str()) != INVALID_FILE_ATTRIBUTES;
    }
    AnsiPath ansi(path);
    return GetFileAttributesA(ansi.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool cRZFile::MovePath(const cIGZString &from, const cIGZString &to)
{
    if (auto moveEx = WideFunction<MoveFileExWFn>(kMoveFileExW)) {
        WidePath wideTo(to);
        WidePath wideFrom(from);
        return moveEx(wideFrom.c_str(), wideTo.c_str(), MOVEFILE_COPY_ALLOWED) != 0;
    }
    AnsiPath ansiFrom(from);
    AnsiPath ansiTo(to);
    return MoveFileA(ansiFrom.c_str(), ansiTo.c_str()) != 0;
}

bool cRZFile::CopyPath(const cIGZString &from, const cIGZString &to, uint8_t overwrite)
{
    BOOL failIfExists = overwrite ^ 1;
    if (auto copy = WideFunction<CopyFileWFn>(kCopyFileW)) {
        WidePath wideTo(to);
        WidePath wideFrom(from);
        return copy(wideFrom.c_str(), wideTo.c_str(), failIfExists) != 0;
    }
    AnsiPath ansiFrom(from);
    AnsiPath ansiTo(to);
    return CopyFileA(ansiFrom.c_str(), ansiTo.c_str(), failIfExists) != 0;
}

bool cRZFile::Exists()
{
    bool exists = PathExists(*mPath.AsInterface());
    if (!exists) SetErrorFromLastError();
    return exists;
}

bool cRZFile::Remove()
{
    if (IsOpenSlot(this)) CloseSlot(this);
    BOOL ok;
    if (auto deleteFile = WideFunction<DeleteFileWFn>(kDeleteFileW)) {
        {
            WidePath wide(*mPath.AsInterface());
            ok = deleteFile(wide.c_str());
        }
        if (!ok) SetErrorFromLastError();
    } else {
        AnsiPath ansi(*mPath.AsInterface());
        ok = DeleteFileA(ansi.c_str());
        if (!ok) SetErrorFromLastError();
    }
    return ok != 0;
}

bool cRZFile::Rename(const cIGZString &newPath)
{
    if (IsOpenSlot(this)) CloseSlot(this);
    bool ok = MovePath(*mPath.AsInterface(), newPath);
    if (!ok) SetErrorFromLastError();
    VirtualCall<void *(__thiscall *)(void *, const cIGZString &), kStringSlotAssign>(&mPath, newPath);
    return ok;
}

bool cRZFile::Copy(const void *sourceFile, uint8_t overwrite)
{
    void *source = const_cast<void *>(sourceFile);
    const cIGZString *sourcePath = VirtualCall<const cIGZString *(__thiscall *)(void *), kSlotGetPath>(source);
    cRZString temp;
    temp.Construct(*sourcePath);
    bool ok = CopyPath(*temp.AsInterface(), *mPath.AsInterface(), overwrite);
    temp.Destruct();
    if (!ok) SetErrorFromLastError();
    return ok;
}

bool cRZFile::Copy(const cIGZString &sourcePath, uint8_t overwrite)
{
    bool ok = CopyPath(sourcePath, *mPath.AsInterface(), overwrite);
    if (!ok) SetErrorFromLastError();
    return ok;
}

bool cRZFile::ResolveShortcutW(const std::u16string &path, std::u16string *target)
{
    size_t dot = path.rfind(u'.');
    if (dot == std::u16string::npos || _wcsicmp(Wide(path) + dot, L".lnk") != 0) return false;
    bool resolved = false;
    Ole32 ole;
    if (!ole.Load()) return false;
    if (ole.Usable() && ole.coInitialize(nullptr) >= 0) {
        IShellLinkW *link = nullptr;
        if (ole.coCreateInstance(kClsidShellLink, nullptr, CLSCTX_INPROC_SERVER, kIidShellLinkW, reinterpret_cast<void **>(&link)) >= 0) {
            IPersistFile *file = nullptr;
            if (link->QueryInterface(kIidPersistFile, reinterpret_cast<void **>(&file)) >= 0) {
                wchar_t buffer[MAX_PATH + 1] = {};
                WIN32_FIND_DATAW data;
                if (file->Load(Wide(path), 0) >= 0 && link->Resolve(nullptr, SLR_NO_UI) >= 0
                    && link->GetPath(buffer, MAX_PATH + 1, &data, 0) == S_OK) {
                    size_t length = 0;
                    while (buffer[length]) ++length;
                    target->assign(reinterpret_cast<const char16_t *>(buffer), length);
                    resolved = true;
                }
                file->Release();
            }
            link->Release();
        }
        ole.coUninitialize();
    }
    FreeLibrary(ole.module);
    return resolved;
}

int32_t cRZFile::ResolveShortcutA(const char *path, char *target, int32_t targetSize)
{
    const char *dot = std::strrchr(path, '.');
    if (!dot || lstrcmpiA(dot, ".lnk") != 0) return 0;
    Ole32 ole;
    if (!ole.Load()) return -1;
    int32_t result = 0;
    if (ole.Usable() && ole.coInitialize(nullptr) >= 0) {
        IShellLinkA *link = nullptr;
        if (ole.coCreateInstance(kClsidShellLink, nullptr, CLSCTX_INPROC_SERVER, kIidShellLinkA, reinterpret_cast<void **>(&link)) >= 0) {
            IPersistFile *file = nullptr;
            if (link->QueryInterface(kIidPersistFile, reinterpret_cast<void **>(&file)) >= 0) {
                int count = lstrlenA(path) + 1;
                auto *wide = static_cast<wchar_t *>(::operator new[](static_cast<size_t>(count) * 2));
                MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, path, count, wide, count);
                char buffer[MAX_PATH + 1] = {};
                WIN32_FIND_DATAA data;
                if (file->Load(wide, 0) >= 0 && link->Resolve(nullptr, SLR_NO_UI) >= 0
                    && link->GetPath(buffer, MAX_PATH + 1, &data, 0) == S_OK) {
                    result = lstrlenA(buffer) + 1;
                    if (target) lstrcpynA(target, buffer, targetSize);
                }
                file->Release();
                ::operator delete[](wide);
            }
            link->Release();
        }
        ole.coUninitialize();
    }
    FreeLibrary(ole.module);
    return result ? result : -1;
}

bool cRZFile::Open(uint32_t access, uint32_t creation, uint32_t sharing)
{
    if (IsOpenSlot(this)) return true;
    DWORD desiredAccess = access << 31;
    if (access & 2) desiredAccess |= GENERIC_WRITE;
    DWORD shareMode = (sharing & 2) ? 3 : (sharing & 1);
    DWORD disposition;
    switch (creation) {
    case 0: disposition = CREATE_NEW; break;
    case 1: disposition = CREATE_ALWAYS; break;
    case 2: disposition = OPEN_EXISTING; break;
    case 3: disposition = OPEN_ALWAYS; break;
    case 4: disposition = TRUNCATE_EXISTING; break;
    default: return false;
    }

    if (auto createFile = WideFunction<CreateFileWFn>(kCreateFileW)) {
        WidePath wide(*mPath.AsInterface());
        std::u16string target;
        if (ResolveShortcutW(*wide.str, &target)) wide.str->swap(target);
        mHandle = createFile(wide.c_str(), desiredAccess, shareMode, nullptr, disposition, 0, nullptr);
    } else {
        AnsiPath ansi(*mPath.AsInterface());
        char target[MAX_PATH];
        int32_t resolved = ResolveShortcutA(ansi.c_str(), target, MAX_PATH);
        const char *name = resolved != 0 && resolved != -1 ? target : ansi.c_str();
        mHandle = CreateFileA(name, desiredAccess, shareMode, nullptr, disposition, 0, nullptr);
    }
    if (mHandle == INVALID_HANDLE_VALUE) {
        SetErrorFromLastError();
        return false;
    }
    mOpen = true;
    // Creating over an existing file succeeds with ERROR_ALREADY_EXISTS, recorded as busy.
    if (disposition == CREATE_ALWAYS || disposition == OPEN_ALWAYS) SetErrorFromLastError();
    mAccess = access;
    mCreation = creation;
    mSharing = sharing;
    int32_t position = static_cast<int32_t>(SetFilePointer(Handle(mHandle), 0, nullptr, FILE_CURRENT));
    mFilePosition = position;
    mPosition = position;
    ClearReadBuffer();
    ClearWriteBuffer();
    mLengthCached = false;
    return true;
}

// Our vtable's slots (rzface::cRZFile, the header): each runs the cRZFile member of the same name, inlined where it is
// defined in this file.
namespace rzface {

namespace {
::cRZFile *Self(cRZFile *face) { return reinterpret_cast<::cRZFile *>(face); }
const ::cRZFile *Self(const cRZFile *face) { return reinterpret_cast<const ::cRZFile *>(face); }
} // namespace

bool cRZFile::QueryInterface(uint32_t riid, void **ppvObj) { [[msvc::forceinline_calls]] return Self(this)->QueryInterface(riid, ppvObj); }
uint32_t cRZFile::AddRef() { return Self(this)->AddRef(); }
uint32_t cRZFile::Release() { [[msvc::forceinline_calls]] return Self(this)->Release(); }
bool cRZFile::Open(uint32_t access, uint32_t creation, uint32_t sharing)
{
    [[msvc::forceinline_calls]] return Self(this)->Open(access, creation, sharing);
}
bool cRZFile::IsOpen() const { return Self(this)->IsOpen(); }
bool cRZFile::Close() { [[msvc::forceinline_calls]] return Self(this)->Close(); }
int32_t cRZFile::Position() const { return Self(this)->Position(); }
uint32_t cRZFile::Length() { [[msvc::forceinline_calls]] return Self(this)->Length(); }
bool cRZFile::SetLength(uint32_t length) { [[msvc::forceinline_calls]] return Self(this)->SetLength(length); }
int32_t cRZFile::SeekToBegin() { [[msvc::forceinline_calls]] return Self(this)->SeekToBegin(); }
int32_t cRZFile::SeekToEnd() { [[msvc::forceinline_calls]] return Self(this)->SeekToEnd(); }
int32_t cRZFile::SeekToRelativePosition(int32_t offset)
{
    [[msvc::forceinline_calls]] return Self(this)->SeekToRelativePosition(offset);
}
int32_t cRZFile::SeekToPosition(int32_t position) { [[msvc::forceinline_calls]] return Self(this)->SeekToPosition(position); }
int32_t cRZFile::Seek(int32_t offset, uint32_t origin) { [[msvc::forceinline_calls]] return Self(this)->Seek(offset, origin); }
bool cRZFile::Read(void *dst, uint32_t length) { [[msvc::forceinline_calls]] return Self(this)->Read(dst, length); }
bool cRZFile::ReadWithCount(void *dst, uint32_t &length) { [[msvc::forceinline_calls]] return Self(this)->ReadWithCount(dst, length); }
bool cRZFile::Write(const void *src, uint32_t length) { [[msvc::forceinline_calls]] return Self(this)->Write(src, length); }
bool cRZFile::WriteWithCount(const void *src, uint32_t &length)
{
    [[msvc::forceinline_calls]] return Self(this)->WriteWithCount(src, length);
}
bool cRZFile::Flush() { [[msvc::forceinline_calls]] return Self(this)->Flush(); }
bool cRZFile::SysFlush() { [[msvc::forceinline_calls]] return Self(this)->SysFlush(); }
bool cRZFile::Remove() { [[msvc::forceinline_calls]] return Self(this)->Remove(); }
bool cRZFile::Rename(const cIGZString &newPath) { [[msvc::forceinline_calls]] return Self(this)->Rename(newPath); }
bool cRZFile::Exists() { [[msvc::forceinline_calls]] return Self(this)->Exists(); }
bool cRZFile::Copy(const cIGZString &sourcePath, uint8_t overwrite)
{
    [[msvc::forceinline_calls]] return Self(this)->Copy(sourcePath, overwrite);
}
bool cRZFile::Copy(const cIGZFile *sourceFile, uint8_t overwrite)
{
    [[msvc::forceinline_calls]] return Self(this)->Copy(static_cast<const void *>(sourceFile), overwrite);
}
void cRZFile::GetPath(cIGZString &out) const { [[msvc::forceinline_calls]] Self(this)->GetPath(out); }
const cIGZString *cRZFile::GetPath() const { return Self(this)->GetPath(); }
const char *cRZFile::GetPathCStr() const { return Self(this)->GetPathCStr(); }
void cRZFile::SetPath(const cIGZString &path) { [[msvc::forceinline_calls]] Self(this)->SetPath(path); }
void cRZFile::SetPath(const char *path) { [[msvc::forceinline_calls]] Self(this)->SetPath(path); }
bool cRZFile::GetOpenModes(uint32_t *access, uint32_t *creation, uint32_t *sharing) const
{
    [[msvc::forceinline_calls]] return Self(this)->GetOpenModes(access, creation, sharing);
}
uintptr_t cRZFile::GetBuffering(int32_t *read, int32_t *write) const
{
    [[msvc::forceinline_calls]] return Self(this)->GetBuffering(read, write);
}
bool cRZFile::SetBuffering(int32_t readSize, int32_t writeSize)
{
    [[msvc::forceinline_calls]] return Self(this)->SetBuffering(readSize, writeSize);
}
void *cRZFile::DeletingDestructor(uint32_t flags) { [[msvc::forceinline_calls]] return Self(this)->DeletingDestructor(flags); }

} // namespace rzface
