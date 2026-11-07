// Differential tests for cRZFile (stage 1: everything that works on an open handle, plus
// construction, destruction and reference counting). Each case gives two objects identical state
// and their own copy of the same file in the sandbox: one carries the original vtable (so every
// call, including calls the class makes through its own vtable, runs original code), the other a
// copy whose slots point at our functions (so the whole call graph is ours). Random sequences of
// operations run on both; after every step the results, the read data, the object fields
// (including buffer contents), the OS file pointer and the file size must match, and the files
// must be byte-identical at the end.
#include "difftest.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"

#include <windows.h>

#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr int kSlots = 34;

template <typename T> void *Addr(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance member pointers only");
    void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

using SetPathChar = void (cRZFile::*)(const char *);
using SetPathString = void (cRZFile::*)(const cIGZString &);
using GetPathString = void (cRZFile::*)(cIGZString &) const;
using CopyFile = bool (cRZFile::*)(const void *, uint8_t);
using CopyPath = bool (cRZFile::*)(const cIGZString &, uint8_t);
// Set by the OurVTable tests: ours is then rzface::cRZFile's table (cRZFile::OwnVTable()), not the member-address copy.
const void *gFace = nullptr;


// The original vtable with every slot we reimplement pointing at our function.
struct OurVTable {
    void *slots[kSlots];
    explicit OurVTable(const difftest::Context &ctx)
    {
        std::memcpy(slots, ctx.image.At(cRZFile::kVTable), sizeof(slots));
        slots[0] = Addr(&cRZFile::QueryInterface);
        slots[1] = Addr(&cRZFile::AddRef);
        slots[2] = Addr(&cRZFile::Release);
        slots[5] = Addr(&cRZFile::Close);
        slots[6] = Addr(&cRZFile::Position);
        slots[7] = Addr(&cRZFile::Length);
        slots[8] = Addr(&cRZFile::SetLength);
        slots[9] = Addr(&cRZFile::SeekToBegin);
        slots[10] = Addr(&cRZFile::SeekToEnd);
        slots[11] = Addr(&cRZFile::SeekToRelativePosition);
        slots[12] = Addr(&cRZFile::SeekToPosition);
        slots[13] = Addr(&cRZFile::Seek);
        slots[14] = Addr(&cRZFile::Read);
        slots[15] = Addr(&cRZFile::ReadWithCount);
        slots[16] = Addr(&cRZFile::Write);
        slots[17] = Addr(&cRZFile::WriteWithCount);
        slots[18] = Addr(&cRZFile::Flush);
        slots[19] = Addr(&cRZFile::SysFlush);
        slots[26] = Addr(static_cast<GetPathString>(&cRZFile::GetPath));
        slots[28] = Addr(static_cast<SetPathChar>(&cRZFile::SetPath));
        slots[29] = Addr(static_cast<SetPathString>(&cRZFile::SetPath));
        slots[30] = Addr(&cRZFile::GetOpenModes);
        slots[31] = Addr(&cRZFile::GetBuffering);
        slots[32] = Addr(&cRZFile::SetBuffering);
        slots[33] = Addr(&cRZFile::DeletingDestructor);
        slots[3] = Addr(&cRZFile::Open);
        slots[20] = Addr(&cRZFile::Remove);
        slots[21] = Addr(&cRZFile::Rename);
        slots[22] = Addr(&cRZFile::Exists);
        slots[23] = Addr(static_cast<CopyFile>(&cRZFile::Copy));
        slots[24] = Addr(static_cast<CopyPath>(&cRZFile::Copy));
    }
};

template <typename Fn> Fn Slot(cRZFile *f, int slot)
{
    return reinterpret_cast<Fn>(static_cast<void *const *>(f->mVTable)[slot]);
}

std::string Describe(const cRZFile &f)
{
    char buf[400];
    std::snprintf(buf, sizeof(buf),
                  "open %d cached %d access %u refs %u error 0x%x len %u pos %d fpos %d rsize %u rbuf %zu/%zu rstart %d rlen %d "
                  "wsize %u wbuf %zu/%zu wstart %d wlen %d",
                  f.mOpen, f.mLengthCached, f.mAccess, f.mRefCount, f.mError, f.mCachedLength, f.mPosition, f.mFilePosition,
                  f.mReadBufferSize, f.mReadBuffer.size(), f.mReadBuffer.capacity(), f.mReadBufferStart, f.mReadBufferLength,
                  f.mWriteBufferSize, f.mWriteBuffer.size(), f.mWriteBuffer.capacity(), f.mWriteBufferStart, f.mWriteBufferLength);
    return buf;
}

// Everything observable about an object apart from its vtable and handle value.
bool SameState(const cRZFile &a, const cRZFile &b)
{
    return a.mPath.mString == b.mPath.mString && a.mOpen == b.mOpen && a.mLengthCached == b.mLengthCached
        && (a.mHandle == INVALID_HANDLE_VALUE) == (b.mHandle == INVALID_HANDLE_VALUE) && a.mAccess == b.mAccess
        && a.mCreation == b.mCreation && a.mSharing == b.mSharing && a.mRefCount == b.mRefCount && a.mError == b.mError
        && a.mUnknownString.mString == b.mUnknownString.mString && a.mCachedLength == b.mCachedLength
        && a.mPosition == b.mPosition && a.mFilePosition == b.mFilePosition && a.mReadBufferSize == b.mReadBufferSize
        && a.mReadBuffer == b.mReadBuffer && a.mReadBuffer.capacity() == b.mReadBuffer.capacity()
        && a.mReadBufferStart == b.mReadBufferStart && a.mReadBufferLength == b.mReadBufferLength
        && a.mWriteBufferSize == b.mWriteBufferSize && a.mWriteBuffer == b.mWriteBuffer
        && a.mWriteBuffer.capacity() == b.mWriteBuffer.capacity() && a.mWriteBufferStart == b.mWriteBufferStart
        && a.mWriteBufferLength == b.mWriteBufferLength;
}

DWORD OsPosition(void *h)
{
    return SetFilePointer(h, 0, nullptr, FILE_CURRENT);
}

std::vector<char> FileBytes(const std::wstring &path)
{
    std::vector<char> bytes;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return bytes;
    DWORD size = GetFileSize(h, nullptr), read = 0;
    bytes.resize(size);
    if (size) ReadFile(h, bytes.data(), size, &read, nullptr);
    CloseHandle(h);
    bytes.resize(read);
    return bytes;
}

void WriteFileBytes(const std::wstring &path, const std::vector<char> &bytes)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD written = 0;
    if (!bytes.empty()) WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(h);
}

int32_t PickBufferSize(std::mt19937 &rng)
{
    static const int32_t sizes[] = {-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 16, 17, 64, 100, 512, 4096, 0x1f4000, 0x1f4001, -2, -100};
    return rng() % 4 == 0 ? static_cast<int32_t>(rng() % 3000) : sizes[rng() % std::size(sizes)];
}

uint32_t PickLength(std::mt19937 &rng)
{
    switch (rng() % 6) {
    case 0: return 0;
    case 1: return 1 + rng() % 3;
    case 2: return rng() % 64;
    case 3: return rng() % 600;
    default: return rng() % 9000;
    }
}

int32_t PickOffset(std::mt19937 &rng, int32_t position)
{
    switch (rng() % 7) {
    case 0: return 0;
    case 1: return position;
    case 2: return -1 - static_cast<int32_t>(rng() % 100);
    case 3: return position + static_cast<int32_t>(rng() % 64) - 32;
    default: return static_cast<int32_t>(rng() % 24000);
    }
}

// One object of each kind, open on identical files.
struct Pair {
    unsigned char rawA[sizeof(cRZFile)], rawB[sizeof(cRZFile)];
    cRZFile *a = reinterpret_cast<cRZFile *>(rawA);
    cRZFile *b = reinterpret_cast<cRZFile *>(rawB);
    std::wstring pathA, pathB;

    Pair(difftest::Context &ctx, const OurVTable &ours, const std::vector<char> &content, uint32_t access, uint32_t id)
    {
        pathA = difftest::SandboxPath(L"a" + std::to_wstring(id) + L".bin");
        pathB = difftest::SandboxPath(L"b" + std::to_wstring(id) + L".bin");
        WriteFileBytes(pathA, content);
        WriteFileBytes(pathB, content);
        ctx.Original<cRZFile *(__thiscall *)(void *)>(0x1c0d1d)(a);
        b->Construct();
        b->mVTable = gFace ? gFace : ours.slots;
        DWORD rights = (access & 1 ? GENERIC_READ : 0) | (access & 2 ? GENERIC_WRITE : 0);
        a->mHandle = difftest::SandboxCreateFile(pathA.substr(difftest::SandboxDir().size()), rights, OPEN_EXISTING);
        b->mHandle = CreateFileW(pathB.c_str(), rights, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        for (cRZFile *f : {a, b}) {
            f->mOpen = true;
            f->mAccess = access;
            f->mCreation = 2;
            f->mSharing = 1;
        }
    }
};

// Runs one random operation through the vtable of both objects; returns false on a mismatch.
bool Step(difftest::Context &ctx, Pair &p, std::mt19937 &rng, std::string &what)
{
    cRZFile *a = p.a, *b = p.b;
    int op = rng() % 24;
    char buf[160];
    bool same = true;
    auto both = [&](auto call) {
        SetLastError(0x4321);
        auto ra = call(a);
        SetLastError(0x4321);
        auto rb = call(b);
        same &= ra == rb;
        return ra;
    };
    switch (op) {
    case 0: case 1: case 2: case 3: { // ReadWithCount / Read
        uint32_t n = PickLength(rng);
        bool withCount = op < 3;
        std::vector<char> da(n + 8, 'x'), db(n + 8, 'x');
        uint32_t ca = n, cb = n;
        bool ra, rb;
        SetLastError(0x4321);
        ra = withCount ? Slot<bool(__thiscall *)(void *, void *, uint32_t &)>(a, 15)(a, da.data(), ca)
                       : Slot<bool(__thiscall *)(void *, void *, uint32_t)>(a, 14)(a, da.data(), n);
        SetLastError(0x4321);
        rb = withCount ? Slot<bool(__thiscall *)(void *, void *, uint32_t &)>(b, 15)(b, db.data(), cb)
                       : Slot<bool(__thiscall *)(void *, void *, uint32_t)>(b, 14)(b, db.data(), n);
        same = ra == rb && ca == cb && da == db;
        std::snprintf(buf, sizeof(buf), "%s(%u) -> %d/%d count %u/%u", withCount ? "ReadWithCount" : "Read", n, ra, rb, ca, cb);
        break;
    }
    case 4: case 5: case 6: case 7: { // WriteWithCount / Write
        uint32_t n = PickLength(rng);
        bool withCount = op < 6;
        std::vector<char> data(n + 1);
        for (auto &c : data) c = static_cast<char>(rng());
        uint32_t ca = n, cb = n;
        SetLastError(0x4321);
        bool ra = withCount ? Slot<bool(__thiscall *)(void *, const void *, uint32_t &)>(a, 17)(a, data.data(), ca)
                            : Slot<bool(__thiscall *)(void *, const void *, uint32_t)>(a, 16)(a, data.data(), n);
        SetLastError(0x4321);
        bool rb = withCount ? Slot<bool(__thiscall *)(void *, const void *, uint32_t &)>(b, 17)(b, data.data(), cb)
                            : Slot<bool(__thiscall *)(void *, const void *, uint32_t)>(b, 16)(b, data.data(), n);
        same = ra == rb && ca == cb;
        std::snprintf(buf, sizeof(buf), "%s(%u) -> %d/%d count %u/%u", withCount ? "WriteWithCount" : "Write", n, ra, rb, ca, cb);
        break;
    }
    case 8: case 9: case 10: { // Seek
        int32_t off = PickOffset(rng, a->mPosition);
        uint32_t origin = rng() % 9 == 0 ? 3 : rng() % 3;
        int32_t r = both([&](cRZFile *f) { return Slot<int32_t(__thiscall *)(void *, int32_t, uint32_t)>(f, 13)(f, off, origin); });
        std::snprintf(buf, sizeof(buf), "Seek(%d, %u) -> %d", off, origin, r);
        break;
    }
    case 11: {
        int slot = 9 + rng() % 4;
        int32_t arg = PickOffset(rng, a->mPosition);
        int32_t r = both([&](cRZFile *f) {
            return slot < 11 ? Slot<int32_t(__thiscall *)(void *)>(f, slot)(f) : Slot<int32_t(__thiscall *)(void *, int32_t)>(f, slot)(f, arg);
        });
        std::snprintf(buf, sizeof(buf), "seek slot %d(%d) -> %d", slot, arg, r);
        break;
    }
    case 12: case 13: { // SetBuffering
        int32_t rs = PickBufferSize(rng), ws = PickBufferSize(rng);
        bool r = both([&](cRZFile *f) { return Slot<bool(__thiscall *)(void *, int32_t, int32_t)>(f, 32)(f, rs, ws); });
        std::snprintf(buf, sizeof(buf), "SetBuffering(%d, %d) -> %d", rs, ws, r);
        break;
    }
    case 14: {
        int32_t ra[2] = {7, 7}, rb[2] = {7, 7};
        uintptr_t xa = Slot<uintptr_t(__thiscall *)(void *, int32_t *, int32_t *)>(a, 31)(a, &ra[0], &ra[1]);
        uintptr_t xb = Slot<uintptr_t(__thiscall *)(void *, int32_t *, int32_t *)>(b, 31)(b, &rb[0], &rb[1]);
        same = ra[0] == rb[0] && ra[1] == rb[1] && xa == reinterpret_cast<uintptr_t>(&ra[1]) && xb == reinterpret_cast<uintptr_t>(&rb[1]);
        std::snprintf(buf, sizeof(buf), "GetBuffering -> %d,%d / %d,%d", ra[0], ra[1], rb[0], rb[1]);
        break;
    }
    case 15: {
        uint32_t va[3] = {9, 9, 9}, vb[3] = {9, 9, 9};
        bool ra = Slot<bool(__thiscall *)(void *, uint32_t *, uint32_t *, uint32_t *)>(a, 30)(a, &va[0], &va[1], &va[2]);
        bool rb = Slot<bool(__thiscall *)(void *, uint32_t *, uint32_t *, uint32_t *)>(b, 30)(b, &vb[0], &vb[1], &vb[2]);
        same = ra == rb && std::memcmp(va, vb, sizeof(va)) == 0;
        std::snprintf(buf, sizeof(buf), "GetOpenModes -> %d/%d", ra, rb);
        break;
    }
    case 16: {
        uint32_t r = both([&](cRZFile *f) { return Slot<uint32_t(__thiscall *)(void *)>(f, 7)(f); });
        std::snprintf(buf, sizeof(buf), "Length -> %u", r);
        break;
    }
    case 17: {
        int32_t r = both([&](cRZFile *f) { return Slot<int32_t(__thiscall *)(void *)>(f, 6)(f); });
        std::snprintf(buf, sizeof(buf), "Position -> %d", r);
        break;
    }
    case 18: {
        bool r = both([&](cRZFile *f) { return Slot<bool(__thiscall *)(void *)>(f, 18)(f); });
        std::snprintf(buf, sizeof(buf), "Flush -> %d", r);
        break;
    }
    case 19: {
        bool r = both([&](cRZFile *f) { return Slot<bool(__thiscall *)(void *)>(f, 19)(f); });
        std::snprintf(buf, sizeof(buf), "SysFlush -> %d", r);
        break;
    }
    case 20: {
        uint32_t len = rng() % 3 == 0 ? 0 : rng() % 30000;
        bool r = both([&](cRZFile *f) { return Slot<bool(__thiscall *)(void *, uint32_t)>(f, 8)(f, len); });
        std::snprintf(buf, sizeof(buf), "SetLength(%u) -> %d", len, r);
        break;
    }
    default: { // the non-virtual helpers, original against ours directly
        int which = rng() % 8;
        int32_t ra = 0, rb = 0;
        uint32_t n = PickLength(rng);
        std::vector<char> da(n + 8, 'x'), db(n + 8, 'x');
        for (size_t k = 0; k < da.size(); ++k) da[k] = db[k] = static_cast<char>(k * 7);
        int32_t off = PickOffset(rng, a->mFilePosition);
        uint32_t origin = rng() % 5;
        SetLastError(rng() % 2 ? ERROR_ACCESS_DENIED : rng() % 2 ? ERROR_ALREADY_EXISTS : 0x4321);
        DWORD lastError = GetLastError();
        switch (which) {
        case 0: ctx.Original<void(__thiscall *)(void *)>(0x1c0f8f)(a); b->ClearReadBuffer(); break;
        case 1: ctx.Original<void(__thiscall *)(void *)>(0x1c0f9b)(a); b->ClearWriteBuffer(); break;
        case 2: ra = ctx.Original<bool(__thiscall *)(void *)>(0x1c142b)(a); SetLastError(lastError); rb = b->FlushWriteBuffer(); break;
        case 3: ra = ctx.Original<int32_t(__thiscall *)(void *, void *, uint32_t)>(0x1c2bc9)(a, da.data(), n); SetLastError(lastError); rb = b->RawRead(db.data(), n); break;
        case 4: ra = ctx.Original<int32_t(__thiscall *)(void *, int32_t, uint32_t)>(0x1c2c0a)(a, off, origin); SetLastError(lastError); rb = b->RawSeek(off, origin); break;
        case 5: ra = ctx.Original<bool(__thiscall *)(void *)>(0x1c12c1)(a); SetLastError(lastError); rb = b->FillReadBuffer(); break;
        case 6: {
            // As WriteWithCount would: only with a write buffer.
            if (a->mWriteBufferSize == 0) break;
            int32_t len = rng() % 4 == 0 ? -static_cast<int32_t>(rng() % 5) : static_cast<int32_t>(n);
            ra = ctx.Original<bool(__thiscall *)(void *, const void *, int32_t)>(0x1c1312)(a, da.data(), len);
            SetLastError(lastError);
            rb = b->BufferedWrite(db.data(), len);
            break;
        }
        default: ctx.Original<void(__thiscall *)(void *)>(0x1c3429)(a); SetLastError(lastError); b->SetErrorFromLastError(); break;
        }
        same = ra == rb && da == db;
        std::snprintf(buf, sizeof(buf), "helper %d (n %u, off %d, origin %u) -> %d/%d", which, n, off, origin, ra, rb);
        break;
    }
    }
    what = buf;
    ++ctx.cases;
    if (!same) return false;
    if (!SameState(*a, *b)) {
        what += "\n      original: " + Describe(*a) + "\n      ours:     " + Describe(*b);
        return false;
    }
    if (a->mOpen && (OsPosition(a->mHandle) != OsPosition(b->mHandle) || GetFileSize(a->mHandle, nullptr) != GetFileSize(b->mHandle, nullptr))) {
        what += " (OS file pointer or size differs)";
        return false;
    }
    return true;
}

void CloseBoth(difftest::Context &ctx, Pair &p, const char *label)
{
    SetLastError(0x4321);
    bool ra = Slot<bool(__thiscall *)(void *)>(p.a, 5)(p.a);
    SetLastError(0x4321);
    bool rb = Slot<bool(__thiscall *)(void *)>(p.b, 5)(p.b);
    ++ctx.cases;
    if (ra != rb || !SameState(*p.a, *p.b)) ctx.Fail("%s: Close differs: %s / %s", label, Describe(*p.a).c_str(), Describe(*p.b).c_str());
    // Closed: the open-only slots must refuse.
    for (int slot : {5, 18}) {
        ++ctx.cases;
        if (Slot<bool(__thiscall *)(void *)>(p.a, slot)(p.a) != Slot<bool(__thiscall *)(void *)>(p.b, slot)(p.b)) ctx.Fail("%s: closed slot %d", label, slot);
    }
    ++ctx.cases;
    if (Slot<int32_t(__thiscall *)(void *)>(p.a, 6)(p.a) != Slot<int32_t(__thiscall *)(void *)>(p.b, 6)(p.b)
        || Slot<int32_t(__thiscall *)(void *, int32_t, uint32_t)>(p.a, 13)(p.a, 5, 0) != Slot<int32_t(__thiscall *)(void *, int32_t, uint32_t)>(p.b, 13)(p.b, 5, 0)) {
        ctx.Fail("%s: closed Position/Seek differ", label);
    }
    char c[4];
    uint32_t n = 4;
    ++ctx.cases;
    if (Slot<bool(__thiscall *)(void *, void *, uint32_t &)>(p.a, 15)(p.a, c, n) != Slot<bool(__thiscall *)(void *, void *, uint32_t &)>(p.b, 15)(p.b, c, n)
        || Slot<bool(__thiscall *)(void *, const void *, uint32_t &)>(p.a, 17)(p.a, c, n) != Slot<bool(__thiscall *)(void *, const void *, uint32_t &)>(p.b, 17)(p.b, c, n)
        || Slot<bool(__thiscall *)(void *, uint32_t)>(p.a, 8)(p.a, 3) != Slot<bool(__thiscall *)(void *, uint32_t)>(p.b, 8)(p.b, 3)) {
        ctx.Fail("%s: closed read/write/SetLength differ", label);
    }
    ++ctx.cases;
    if (FileBytes(p.pathA) != FileBytes(p.pathB)) ctx.Fail("%s: file contents differ after close", label);
}

void Destroy(difftest::Context &ctx, Pair &p)
{
    ctx.Original<void(__thiscall *)(void *)>(0x1c0e6d)(p.a);
    p.b->Destruct();
}

} // namespace

DIFF_TEST(cRZFile_Operations, 0x1c2c97)
{
    OurVTable ours(ctx);
    std::mt19937 rng(0xF11E);
    for (uint32_t run = 0; run < 400; ++run) {
        std::vector<char> content(rng() % 5 == 0 ? 0 : rng() % 20000);
        for (auto &c : content) c = static_cast<char>(rng());
        uint32_t access = 1 + rng() % 3; // read, write, read/write
        Pair p(ctx, ours, content, access, run);
        // Start from a random buffering configuration, as callers set it right after opening.
        if (rng() % 4) {
            int32_t rs = PickBufferSize(rng), ws = PickBufferSize(rng);
            Slot<bool(__thiscall *)(void *, int32_t, int32_t)>(p.a, 32)(p.a, rs, ws);
            Slot<bool(__thiscall *)(void *, int32_t, int32_t)>(p.b, 32)(p.b, rs, ws);
        }
        std::string history, what;
        bool ok = true;
        for (int step = 0; step < 60 && ok; ++step) {
            ok = Step(ctx, p, rng, what);
            history += "\n    " + what;
            if (history.size() > 1500) history.erase(0, history.size() - 1500);
        }
        if (!ok) {
            ctx.Fail("run %u (access %u, %zu bytes): mismatch; last steps:%s", run, access, content.size(), history.c_str());
            difftest::SandboxCloseFile(p.a->mHandle);
            CloseHandle(p.b->mHandle);
            continue;
        }
        CloseBoth(ctx, p, ("run " + std::to_string(run)).c_str());
        Destroy(ctx, p);
    }
    difftest::EmptySandbox();
}

DIFF_TEST(cRZFile_ConstructDestruct, 0x1c0c07)
{
    OurVTable ours(ctx);
    auto ctorString = ctx.Original<cRZFile *(__thiscall *)(void *, const cIGZString &)>(0x1c0c07);
    auto ctorChar = ctx.Original<cRZFile *(__thiscall *)(void *, const char *)>(0x1c0c8b);
    auto ctorDefault = ctx.Original<cRZFile *(__thiscall *)(void *)>(0x1c0d1d);
    auto deleting = ctx.Original<cRZFile *(__thiscall *)(void *, uint32_t)>(0x1c0ef9);
    const char *paths[] = {"", "x", "C:\\some\\path\\file.package", "a much longer path that does not fit in the small string buffer.bin"};
    for (int i = 0; i < 4; ++i) {
        const char *text = paths[i];
        for (int kind = 0; kind < 4; ++kind) {
            cRZString s{};
            s.Construct(text);
            auto *a = static_cast<cRZFile *>(::operator new(sizeof(cRZFile)));
            auto *b = static_cast<cRZFile *>(::operator new(sizeof(cRZFile)));
            std::memset(a, 0xCD, sizeof(cRZFile));
            std::memset(b, 0xCD, sizeof(cRZFile));
            cRZFile *ra, *rb;
            if (kind == 0) ra = ctorString(a, *s.AsInterface()), rb = b->Construct(*s.AsInterface());
            else if (kind == 1) ra = ctorChar(a, text), rb = b->Construct(text);
            else if (kind == 2) ra = ctorChar(a, nullptr), rb = b->Construct(static_cast<const char *>(nullptr));
            else ra = ctorDefault(a), rb = b->Construct();
            ++ctx.cases;
            if (ra != a || rb != b || a->mVTable != b->mVTable || a->mHandle != b->mHandle || !SameState(*a, *b)) {
                ctx.Fail("constructor %d('%s'): %s / %s", kind, text, Describe(*a).c_str(), Describe(*b).c_str());
            }
            // Destroy: half plainly, half through the deleting destructor (flags 0 then manual free, or 1).
            uint32_t flags = (i + kind) % 3 == 0 ? 0 : 1;
            if ((i + kind) % 2 == 0) {
                ctx.Original<void(__thiscall *)(void *)>(0x1c0e6d)(a);
                b->Destruct();
                ++ctx.cases;
                if (a->mVTable != b->mVTable) ctx.Fail("Destruct leaves a different vtable");
                ::operator delete(a);
                ::operator delete(b);
            } else {
                ++ctx.cases;
                if (deleting(a, flags) != a || b->DeletingDestructor(flags) != b) ctx.Fail("DeletingDestructor return");
                if (!(flags & 1)) {
                    ::operator delete(a);
                    ::operator delete(b);
                }
            }
            s.Destruct();
        }
    }
    // Destroying an open file closes it (and flushes pending buffered writes).
    for (uint32_t run = 0; run < 40; ++run) {
        std::vector<char> content(run * 37);
        Pair p(ctx, ours, content, 3, 1000 + run);
        int32_t ws = static_cast<int32_t>(run * 10);
        Slot<bool(__thiscall *)(void *, int32_t, int32_t)>(p.a, 32)(p.a, 0, ws);
        Slot<bool(__thiscall *)(void *, int32_t, int32_t)>(p.b, 32)(p.b, 0, ws);
        char data[50] = "pending data";
        Slot<bool(__thiscall *)(void *, const void *, uint32_t)>(p.a, 16)(p.a, data, sizeof(data));
        Slot<bool(__thiscall *)(void *, const void *, uint32_t)>(p.b, 16)(p.b, data, sizeof(data));
        Destroy(ctx, p);
        ++ctx.cases;
        if (FileBytes(p.pathA) != FileBytes(p.pathB)) ctx.Fail("destroying open file %u: contents differ", run);
    }
    difftest::EmptySandbox();
}

// Deletions through slot 33 become observable: a vtable copy whose deleting destructor records the
// call (and still frees).
int gDeleted = 0;
void *__fastcall RecordingDelete(void *self, void *, uint32_t flags)
{
    ++gDeleted;
    if (flags & 1) ::operator delete(self);
    return self;
}

DIFF_TEST(cRZFile_RefCountAndPath, 0x1c2aa0)
{
    OurVTable ours(ctx);
    void *origSlots[kSlots], *ourSlots[kSlots];
    std::memcpy(origSlots, ctx.image.At(cRZFile::kVTable), sizeof(origSlots));
    std::memcpy(ourSlots, ours.slots, sizeof(ourSlots));
    origSlots[33] = ourSlots[33] = reinterpret_cast<void *>(&RecordingDelete);
    std::mt19937 rng(9);
    for (int i = 0; i < 3000; ++i) {
        auto *a = static_cast<cRZFile *>(::operator new(sizeof(cRZFile)));
        auto *b = static_cast<cRZFile *>(::operator new(sizeof(cRZFile)));
        a->Construct();
        b->Construct();
        a->mVTable = origSlots;
        b->mVTable = ourSlots;
        uint32_t refs = rng() % 5;
        a->mRefCount = b->mRefCount = refs;
        uint32_t riid = i % 3 == 0 ? cRZFile::kGZIID_cIGZFile : i % 3 == 1 ? 1u : static_cast<uint32_t>(rng());
        void *pa = nullptr, *pb = nullptr;
        bool qa = Slot<bool(__thiscall *)(void *, uint32_t, void **)>(a, 0)(a, riid, &pa);
        bool qb = Slot<bool(__thiscall *)(void *, uint32_t, void **)>(b, 0)(b, riid, &pb);
        ++ctx.cases;
        if (qa != qb || (pa == a) != (pb == b) || (pa == nullptr) != (pb == nullptr) || a->mRefCount != b->mRefCount) ctx.Fail("QueryInterface(0x%08x)", riid);
        uint32_t ra = Slot<uint32_t(__thiscall *)(void *)>(a, 1)(a), rb = Slot<uint32_t(__thiscall *)(void *)>(b, 1)(b);
        ++ctx.cases;
        if (ra != rb) ctx.Fail("AddRef %u vs %u", ra, rb);
        // Release until deleted.
        for (;;) {
            gDeleted = 0;
            ra = Slot<uint32_t(__thiscall *)(void *)>(a, 2)(a);
            int da = gDeleted;
            gDeleted = 0;
            rb = Slot<uint32_t(__thiscall *)(void *)>(b, 2)(b);
            int db = gDeleted;
            ++ctx.cases;
            if (ra != rb || da != db) {
                ctx.Fail("Release from %u: %u (deleted %d) vs %u (deleted %d)", refs, ra, da, rb, db);
                break;
            }
            if (da) break;
        }
    }
    // Paths: SetPath(char*), SetPath(cIGZString), GetPath(cIGZString) through the slots.
    const char *texts[] = {"", "file", "D:\\x\\y.dat", "a path long enough to need a heap allocation in std::string"};
    for (const char *t : texts) {
        for (const char *u : texts) {
            cRZFile *a = static_cast<cRZFile *>(::operator new(sizeof(cRZFile)));
            cRZFile *b = static_cast<cRZFile *>(::operator new(sizeof(cRZFile)));
            a->Construct(u);
            b->Construct(u);
            a->mVTable = ctx.image.At(cRZFile::kVTable);
            b->mVTable = gFace ? gFace : ours.slots;
            cRZString s{}, oa{}, ob{};
            s.Construct(t);
            oa.Construct("old");
            ob.Construct("old");
            Slot<void(__thiscall *)(void *, const char *)>(a, 28)(a, t);
            Slot<void(__thiscall *)(void *, const char *)>(b, 28)(b, t);
            Slot<void(__thiscall *)(void *, const char *)>(a, 28)(a, nullptr);
            Slot<void(__thiscall *)(void *, const char *)>(b, 28)(b, nullptr);
            ++ctx.cases;
            if (a->mPath.mString != b->mPath.mString) ctx.Fail("SetPath(char*) '%s'", t);
            Slot<void(__thiscall *)(void *, const cIGZString &)>(a, 29)(a, *s.AsInterface());
            Slot<void(__thiscall *)(void *, const cIGZString &)>(b, 29)(b, *s.AsInterface());
            Slot<void(__thiscall *)(void *, cIGZString &)>(a, 26)(a, *oa.AsInterface());
            Slot<void(__thiscall *)(void *, cIGZString &)>(b, 26)(b, *ob.AsInterface());
            ++ctx.cases;
            if (a->mPath.mString != b->mPath.mString || oa.mString != ob.mString || oa.mString != t) ctx.Fail("SetPath/GetPath '%s'", t);
            ctx.Original<void(__thiscall *)(void *)>(0x1c0e6d)(a);
            b->Destruct();
            ::operator delete(a);
            ::operator delete(b);
            s.Destruct();
            oa.Destruct();
            ob.Destruct();
        }
    }
}

// Stage 2: Open and the path operations, in both of the original's branches: with the game's table
// of wide file functions filled in (pointed at the sandboxed functions) and empty (ANSI imports).
// Each side works in its own sandbox directory holding the same files, including non-ASCII names
// and .lnk shortcuts made with the shell's IShellLink; after every operation the results, the
// object state and both directories (names and contents) must match.
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <map>

namespace {

const uint32_t kWideGlobals[] = {0x10cb5e0, 0x10cb5e4, 0x10cb5e8, 0x10cb5ec, 0x10cb5f0, 0x10cb5f4};
const char *const kWideNames[] = {"CreateFileW", "CopyFileW", "MoveFileExW", "FindFirstFileW", "DeleteFileW", "GetFileAttributesW"};

void SetWideMode(difftest::Context &ctx, bool wide)
{
    for (int i = 0; i < 6; ++i) *ctx.image.At<void *>(kWideGlobals[i]) = wide ? difftest::SandboxFunction(kWideNames[i]) : nullptr;
}

std::string Utf8(const std::wstring &w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    s.resize(n - 1);
    return s;
}

std::map<std::wstring, std::vector<char>> Listing(const std::wstring &dir)
{
    std::map<std::wstring, std::vector<char>> out;
    WIN32_FIND_DATAW d;
    HANDLE h = FindFirstFileW((dir + L"*").c_str(), &d);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        std::wstring n = d.cFileName;
        if (n == L"." || n == L"..") continue;
        // Shortcut files (under any name, once renamed or copied) embed their absolute target path,
        // which differs between the sides: compare them by their header only.
        std::vector<char> bytes = FileBytes(dir + n);
        bool lnk = bytes.size() >= 4 && std::memcmp(bytes.data(), "L   ", 4) == 0;
        out[n] = lnk ? std::vector<char>{'l'} : bytes;
    } while (FindNextFileW(h, &d));
    FindClose(h);
    return out;
}

void MakeShortcut(const std::wstring &lnk, const std::wstring &target)
{
    IShellLinkW *link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void **>(&link)))) return;
    link->SetPath(target.c_str());
    IPersistFile *file = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&file)))) {
        file->Save(lnk.c_str(), TRUE);
        file->Release();
    }
    link->Release();
}

const wchar_t *const kNames[] = {L"a.bin", L"b file.dat", L"über.txt", L"日本.bin", L"missing.bin",
                                 L"sub\\none.bin", L"link.lnk", L"dangling-free.LNK", L"c.bin"};

void Populate(const std::wstring &dir, std::mt19937 &rng)
{
    CreateDirectoryW(dir.c_str(), nullptr);
    for (const wchar_t *n : {L"a.bin", L"b file.dat", L"über.txt", L"日本.bin"}) {
        if (rng() % 4 == 0) continue;
        std::vector<char> bytes(rng() % 300);
        for (auto &c : bytes) c = static_cast<char>(rng());
        WriteFileBytes(dir + n, bytes);
    }
    MakeShortcut(dir + L"link.lnk", dir + L"a.bin");
    MakeShortcut(dir + L"dangling-free.LNK", dir + L"b file.dat");
}

struct Side {
    std::wstring dir;
    cRZFile *file;
    cRZFile *source; // for Copy(cIGZFile*): carries the original vtable on both sides
    cRZString arg;   // a path argument
};

void SetPath(cRZString &s, const std::wstring &dir, const wchar_t *name)
{
    s.mString = name[0] ? Utf8(dir + name) : std::string();
}

} // namespace

DIFF_TEST(cRZFile_Paths, 0x1c285d)
{
    OurVTable ours(ctx);
    CoInitialize(nullptr);
    std::mt19937 rng(0xF11E5);
    for (int run = 0; run < 160; ++run) {
        bool wide = run % 2 == 0;
        SetWideMode(ctx, wide);
        uint32_t seed = static_cast<uint32_t>(rng());
        Side sides[2];
        for (int k = 0; k < 2; ++k) {
            Side &sd = sides[k];
            sd.dir = difftest::SandboxPath((k ? L"B" : L"A") + std::to_wstring(run) + L"\\");
            std::mt19937 same(seed);
            Populate(sd.dir, same);
            sd.file = static_cast<cRZFile *>(::operator new(sizeof(cRZFile)));
            sd.source = static_cast<cRZFile *>(::operator new(sizeof(cRZFile)));
            sd.file->Construct();
            sd.source->Construct();
            sd.file->mVTable = k ? (gFace ? gFace : static_cast<const void *>(ours.slots)) : ctx.image.At(cRZFile::kVTable);
            sd.source->mVTable = ctx.image.At(cRZFile::kVTable);
            sd.arg.Construct();
        }
        std::string history;
        bool ok = true;
        for (int step = 0; step < 14 && ok; ++step) {
            const wchar_t *name = kNames[rng() % std::size(kNames)];
            const wchar_t *other = kNames[rng() % std::size(kNames)];
            int op = rng() % 9;
            uint32_t a1 = rng() % 4, a2 = rng() % 7, a3 = rng() % 4;
            uint8_t overwrite = static_cast<uint8_t>(rng() % 3);
            uint32_t result[2] = {};
            for (int k = 0; k < 2; ++k) {
                Side &sd = sides[k];
                cRZFile *f = sd.file;
                if (op != 0 && op != 8) SetPath(f->mPath, sd.dir, name); // keep the path while open
                SetPath(sd.arg, sd.dir, other);
                SetPath(sd.source->mPath, sd.dir, other);
                SetLastError(0x4321);
                switch (op) {
                case 0: case 1: // Open (twice in a row sometimes: already open returns true)
                    if (op == 1 && f->mOpen) Slot<bool(__thiscall *)(void *)>(f, 5)(f);
                    if (!f->mOpen) SetPath(f->mPath, sd.dir, name);
                    result[k] = Slot<bool(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(f, 3)(f, a1, a2, a3);
                    break;
                case 2: result[k] = Slot<bool(__thiscall *)(void *)>(f, 22)(f); break;
                case 3: result[k] = Slot<bool(__thiscall *)(void *)>(f, 20)(f); break;
                case 4: result[k] = Slot<bool(__thiscall *)(void *, const cIGZString &)>(f, 21)(f, *sd.arg.AsInterface()); break;
                case 5: result[k] = Slot<bool(__thiscall *)(void *, const cIGZString &, uint8_t)>(f, 24)(f, *sd.arg.AsInterface(), overwrite); break;
                case 6: result[k] = Slot<bool(__thiscall *)(void *, const void *, uint8_t)>(f, 23)(f, sd.source, overwrite); break;
                case 7: // Length of a closed file (by path)
                    if (f->mOpen) Slot<bool(__thiscall *)(void *)>(f, 5)(f);
                    result[k] = Slot<uint32_t(__thiscall *)(void *)>(f, 7)(f);
                    break;
                default: { // read through an opened file, then close
                    char buf[64] = {};
                    uint32_t n = sizeof(buf);
                    result[k] = Slot<bool(__thiscall *)(void *, void *, uint32_t &)>(f, 15)(f, buf, n) ? 1000 + n + static_cast<uint8_t>(buf[0]) : 0;
                    Slot<bool(__thiscall *)(void *)>(f, 5)(f);
                    break;
                }
                }
            }
            char line[200];
            std::snprintf(line, sizeof(line), "\n    %s op %d name '%ls' other '%ls' args %u,%u,%u overwrite %u -> %u / %u", wide ? "W" : "A", op, name,
                          other, a1, a2, a3, overwrite, result[0], result[1]);
            history += line;
            ++ctx.cases;
            cRZFile *a = sides[0].file, *b = sides[1].file;
            // Paths differ by the directory: compare the rest of the state with B's path rewritten.
            std::string pathB = b->mPath.mString, dirA = Utf8(sides[0].dir), dirB = Utf8(sides[1].dir);
            if (pathB.compare(0, dirB.size(), dirB) == 0) b->mPath.mString = dirA + pathB.substr(dirB.size());
            bool same = result[0] == result[1] && SameState(*a, *b);
            b->mPath.mString = pathB;
            if (!same || Listing(sides[0].dir) != Listing(sides[1].dir)) {
                ctx.Fail("run %d: mismatch after:%s\n      original: %s\n      ours:     %s", run, history.c_str(), Describe(*a).c_str(), Describe(*b).c_str());
                ok = false;
            }
        }
        for (int k = 0; k < 2; ++k) {
            Side &sd = sides[k];
            if (sd.file->mOpen) Slot<bool(__thiscall *)(void *)>(sd.file, 5)(sd.file);
            ctx.Original<void(__thiscall *)(void *)>(0x1c0e6d)(sd.file);
            sd.source->Destruct();
            sd.arg.Destruct();
            ::operator delete(sd.file);
            ::operator delete(sd.source);
        }
    }
    SetWideMode(ctx, false);
    CoUninitialize();
    difftest::EmptySandbox();
}

DIFF_TEST(cRZFile_ShortcutHelpers, 0x1c3235)
{
    auto origW = ctx.Original<bool(__cdecl *)(const std::u16string &, std::u16string *)>(0x1c3235);
    auto origA = ctx.Original<int32_t(__cdecl *)(const char *, char *, int32_t)>(0x1c3059);
    CoInitialize(nullptr);
    std::wstring dir = difftest::SandboxPath(L"lnk\\");
    CreateDirectoryW(dir.c_str(), nullptr);
    WriteFileBytes(dir + L"target.bin", {'x'});
    MakeShortcut(dir + L"good.lnk", dir + L"target.bin");
    MakeShortcut(dir + L"UPPER.LNK", dir + L"target.bin");
    WriteFileBytes(dir + L"broken.lnk", {'n', 'o', 't', ' ', 'a', ' ', 'l', 'i', 'n', 'k'});
    const wchar_t *names[] = {L"good.lnk", L"UPPER.LNK", L"broken.lnk", L"absent.lnk", L"target.bin", L"noext", L"dir.lnk\\x", L".lnk"};
    for (const wchar_t *n : names) {
        std::wstring full = dir + n;
        std::u16string path(reinterpret_cast<const char16_t *>(full.c_str()));
        std::u16string ta = u"unchanged", tb = u"unchanged";
        bool ra = origW(path, &ta), rb = cRZFile::ResolveShortcutW(path, &tb);
        ++ctx.cases;
        if (ra != rb || ta != tb) ctx.Fail("ResolveShortcutW(%ls): %d vs %d", n, ra, rb);
        std::string narrow(full.size(), '?');
        for (size_t i = 0; i < full.size(); ++i) narrow[i] = static_cast<char>(full[i]);
        for (int32_t size : {260, 5, 0}) {
            char ba[300], bb[300];
            std::memset(ba, 'z', sizeof(ba));
            std::memset(bb, 'z', sizeof(bb));
            int32_t xa = origA(narrow.c_str(), size ? ba : nullptr, size), xb = cRZFile::ResolveShortcutA(narrow.c_str(), size ? bb : nullptr, size);
            ++ctx.cases;
            if (xa != xb || std::memcmp(ba, bb, sizeof(ba)) != 0) ctx.Fail("ResolveShortcutA(%ls, %d): %d vs %d", n, size, xa, xb);
        }
    }
    CoUninitialize();
    difftest::EmptySandbox();
}

// Slot 27 (GetPathCStr, shared with cRZTextFile and other classes): the c_str() of the path string at
// +8. The original returns a pointer into the object for short strings (SSO buffer) and the heap
// pointer otherwise; both sides must agree on which, and on the text.
DIFF_TEST(cRZFile_GetPathCStr, 0x1c1c80)
{
    auto orig = ctx.Original<const char *(__thiscall *)(void *)>(0x1c1c80);
    const char *texts[] = {"", "a", "0123456789abcde", "0123456789abcdef", "a path long enough to need a heap allocation in std::string"};
    for (const char *t : texts) {
        cRZFile *a = static_cast<cRZFile *>(::operator new(sizeof(cRZFile)));
        cRZFile *b = static_cast<cRZFile *>(::operator new(sizeof(cRZFile)));
        a->Construct(t);
        b->Construct(t);
        const char *ra = orig(a), *rb = b->GetPathCStr();
        auto inA = [&](const char *p) { return p >= reinterpret_cast<const char *>(a) && p < reinterpret_cast<const char *>(a) + sizeof(cRZFile); };
        auto inB = [&](const char *p) { return p >= reinterpret_cast<const char *>(b) && p < reinterpret_cast<const char *>(b) + sizeof(cRZFile); };
        ++ctx.cases;
        if (inA(ra) != inB(rb) || (inA(ra) && ra - reinterpret_cast<const char *>(a) != rb - reinterpret_cast<const char *>(b)) ||
            std::strcmp(ra, rb) != 0 || std::strcmp(ra, t) != 0)
            ctx.Fail("GetPathCStr '%s'", t);
        ctx.Original<void(__thiscall *)(void *)>(0x1c0e6d)(a);
        b->Destruct();
        ::operator delete(a);
        ::operator delete(b);
    }
}

// Our own vtable (lane QD; the image-free program's): the operations and path tests again with ours being
// rzface::cRZFile's table, every slot called by number; none of its 34 slots is an image address.
DIFF_TEST(cRZFile_OurVTable, 0x1c2c97)
{
    const void *const *table = static_cast<const void *const *>(cRZFile::OwnVTable());
    for (int i = 0; i < kSlots; ++i) {
        ++ctx.cases;
        if (reinterpret_cast<uintptr_t>(table[i]) - ctx.image.Base() < ctx.image.Size()) ctx.Fail("our vtable slot %d points into the image", i);
    }
    gFace = table;
    DiffTest_cRZFile_Operations(ctx);
    DiffTest_cRZFile_Paths(ctx);
    gFace = nullptr;
}
