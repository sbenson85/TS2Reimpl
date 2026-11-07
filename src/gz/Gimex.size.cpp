// Gimex's codec table and dispatchers (lane LX). Layout and contracts: src/gz/Gimex.h.
#include "gz/GimexFormats.h"

#include "game/game.h"
#include "game/original.h"
#include "ifaces/cIGZFile.h"
#include "rz/cRZCriticalSection.h"

#include <cstring>
#include <new>

namespace gimex {

namespace {

constexpr uint32_t kLock = 0x10e6918;     // the Gimex lock (a static cRZCriticalSection)
constexpr uint32_t kMaxExtension = 0x400; // FindFormatByExtension's buffer: '.', the extension, its NUL

int32_t sCurrentFormat = 0;

// The null codec's entry points (the original's rows point at shared `xor eax, eax; ret N` bodies).
int32_t __stdcall NullIs(ifaces::cIGZFile *)
{
    return 0;
}
int32_t __stdcall NullOpen(Instance **, ifaces::cIGZFile *,const char *, int32_t)
{
    return 0;
}
Info *__stdcall NullInfo(Instance *, int32_t)
{
    return nullptr;
}
int32_t __stdcall NullRead(Instance *, Info *, void *, int32_t)
{
    return 0;
}
int32_t __stdcall NullClose(Instance *)
{
    return 0;
}
int32_t __stdcall NullWrite(Instance *, const Info *, const void *, int32_t)
{
    return 0;
}

struct Table {
    Codec rows[kRows + 1]; // the last row ends the table (no about)

    Table() : rows{}
    {
        // The null codec's about (0x87d9d2: "NULL", its notes from the game's strings) is the codecs' code.
        const Codec null{OriginalAddress<AboutFn, 0x87d9d2>(), &NullIs, &NullOpen, &NullInfo, &NullRead, &NullClose,
                         &NullOpen, &NullWrite, &NullClose};
        for (int32_t i = 0; i < kRows; ++i) rows[i] = null;
        rows[1] = {&fsh::NewAbout, &fsh::Probe,                                               // FSH
                   &fsh::Open, &fsh::GetInfo,
                   &fsh::ReadFrame, &fsh::Close,
                   &fsh::OpenWriter, &fsh::WriteFrame,
                   &fsh::CloseWriter};
        rows[2] = {&jpeg::NewAbout, &jpeg::Probe,                                             // JPEG (libjpeg)
                   &jpeg::Open, &jpeg::GetInfo,
                   &jpeg::ReadFrame, &jpeg::Close,
                   &OpenWriter, &jpeg::WriteFrame,
                   &jpeg::Close};
        rows[3] = {&ijl::NewAbout, &ijl::Probe,                                               // JPEG (Intel IJL)
                   &ijl::Open, &ijl::GetInfo,
                   &ijl::ReadFrame, &ijl::Close,
                   &OpenWriter, &ijl::WriteFrame,
                   &FreeInstance};
        rows[4] = {&png::NewAbout, &png::Probe,                                               // PNG (libpng)
                   &png::Open, &png::GetInfo,
                   &png::ReadFrame, &png::Close,
                   &OpenWriter, &png::WriteFrame,
                   &jpeg::Close};
        rows[5] = {&bmp::NewAbout, &bmp::Probe, &bmp::Open, &bmp::GetInfo, &bmp::ReadFrame, &FreeInstance, // BMP
                   &OpenWriter, &bmp::WriteFrame, &FreeInstance};
        rows[6] = {&tga::NewAbout, &tga::Probe, &tga::Open, &tga::GetInfo, &tga::ReadFrame, &FreeInstance, // TGA
                   &OpenWriter, &tga::WriteFrame, &FreeInstance};
    }
};

const Codec &Current()
{
    return Codecs()[sCurrentFormat];
}

} // namespace

const Codec *Codecs()
{
    static const Table table;
    return table.rows;
}

int32_t CurrentFormat()
{
    return sCurrentFormat;
}

void Lock()
{
    GameAddress<cRZCriticalSection>(kLock)->Lock();
}

void Unlock()
{
    GameAddress<cRZCriticalSection>(kLock)->Unlock();
}

int32_t __cdecl FormatCount()
{
    const Codec *rows = Codecs();
    int32_t count = 0;
    while (rows[count].about) ++count;
    return count;
}

void __stdcall SetFormat(int32_t format)
{
    if (format >= FormatCount() || format < 0) format = 0;
    sCurrentFormat = format;
}

int32_t __cdecl GetFormat()
{
    return sCurrentFormat;
}

About *__stdcall CurrentAbout()
{
    return Current().about();
}

int32_t __stdcall Is(ifaces::cIGZFile *stream)
{
    return Current().is(stream);
}

int32_t __stdcall Open(Instance **out, ifaces::cIGZFile *stream, const char *path, int32_t flag)
{
    return Current().open(out, stream, path, flag);
}

Info *__stdcall GetInfo(Instance *instance, int32_t frame)
{
    return Current().info(instance, frame);
}

bool __stdcall Read(Instance *instance, Info *info, void *pixels, int32_t pitch)
{
    return Current().read(instance, info, pixels, pitch) != 0;
}

bool __stdcall Close(Instance *instance)
{
    return Current().close(instance) != 0;
}

int32_t __stdcall WOpen(Instance **out, ifaces::cIGZFile *stream, const char *path, int32_t flag)
{
    return Current().wopen(out, stream, path, flag);
}

bool __stdcall Write(Instance *instance, const Info *info, const void *pixels, int32_t pitch)
{
    return Current().write(instance, info, pixels, pitch) != 0;
}

bool __stdcall WClose(Instance *instance)
{
    return Current().wclose(instance) != 0;
}

int32_t __cdecl CompareNoCase(const char *a, const char *b)
{
    auto lower = [](char c) -> int32_t {
        return static_cast<uint8_t>(c - 'A') <= 'Z' - 'A' ? c + ('a' - 'A') : c;
    };
    int32_t difference;
    for (;; ++a, ++b) {
        difference = lower(*a) - lower(*b);
        if (*a == 0 || difference != 0) break;
    }
    return difference;
}

int32_t __stdcall FindFormatByExtension(const char *extension, int32_t *slot)
{
    if (*extension == '-' || *extension == '+' || *extension == '.') ++extension;
    int32_t bestScore = 0;
    int32_t bestFormat = 0;
    // ORIGINAL-BUG: the original's buffer is 0x400 bytes and an extension filling it gets its NUL one byte past the
    // end (on the caller's frame); ours has room for that byte.
    char dotted[kMaxExtension + 1];
    dotted[0] = '.';
    if (static_cast<uint8_t>(*extension) > ' ') {
        char *out = dotted + 1;
        while (out < dotted + kMaxExtension) {
            const char c = *extension++;
            if (static_cast<uint8_t>(c) <= ' ' || c == '?') break;
            *out++ = c;
        }
        *out = 0;
        // Once a 4 is found the inner loop's test ends every later pass (the original also tests it here).
        for (int32_t i = 0; i < 8; ++i) {
            for (int32_t format = 1; format < FormatCount() && bestScore < 4; ++format) {
                SetFormat(format);
                About *about = CurrentAbout();
                if (!about) continue;
                if (CompareNoCase(dotted, about->extensions[i]) == 0) {
                    int32_t score = i == 0 ? 2 : 1;
                    if (about->flags & 2) score = i == 0 ? 4 : 3;
                    if (bestScore < score) {
                        bestScore = score;
                        bestFormat = format;
                        if (slot) *slot = i;
                    }
                }
                FreeMemory(about);
            }
        }
    }
    SetFormat(bestFormat);
    return bestFormat;
}

// Not inlined: the codecs' tests put a recording allocator at these two (as at the original's).
__declspec(noinline) uint8_t __stdcall FreeMemory(void *memory)
{
    ::operator delete[](memory);
    return 1;
}

__declspec(noinline) void *__stdcall Allocate(uint32_t size)
{
    return ::operator new[](size);
}

uint64_t __stdcall StreamLength(ifaces::cIGZFile *stream)
{
    return stream->Length();
}

int32_t __stdcall StreamRead(ifaces::cIGZFile *stream, void *buffer, int32_t count)
{
    if (count < 0) return 0;
    uint32_t length = static_cast<uint32_t>(count);
    stream->ReadWithCount(buffer, &length);
    return static_cast<int32_t>(length);
}

bool __stdcall StreamSeek(ifaces::cIGZFile *stream, uint32_t low, int32_t)
{
    return static_cast<uint32_t>(stream->SeekToPosition(static_cast<int32_t>(low))) == low;
}

int32_t __stdcall StreamWrite(ifaces::cIGZFile *stream, const void *buffer, int32_t count)
{
    if (count < 0) return 0;
    uint32_t length = static_cast<uint32_t>(count);
    stream->WriteWithCount(const_cast<void *>(buffer), &length);
    return static_cast<int32_t>(length);
}

uint32_t __cdecl ReadLittleEndian(const void *bytes, int32_t width)
{
    const uint8_t *b = static_cast<const uint8_t *>(bytes);
    if (width == 1) return b[0];
    if (width == 2) return *static_cast<const uint16_t *>(bytes); // x86 reads it unaligned
    if (width == 4) return *static_cast<const uint32_t *>(bytes);
    if (width == 3) return (static_cast<uint32_t>(b[2]) << 8 | b[1]) << 8 | b[0];
    return 0;
}

uint32_t __cdecl ReadBigEndian(const void *bytes, int32_t width)
{
    const uint8_t *b = static_cast<const uint8_t *>(bytes);
    if (width == 1) return b[0];
    if (width == 2) return static_cast<uint32_t>(b[0]) << 8 | b[1];
    if (width == 4) return ((static_cast<uint32_t>(b[0]) << 8 | b[1]) << 8 | b[2]) << 8 | b[3];
    if (width == 3) return (static_cast<uint32_t>(b[0]) << 8 | b[1]) << 8 | b[2];
    return 0;
}

// 0x87f3e4: the writer-open (and a reader-open's allocation) shared by rows 2..6.
int32_t __stdcall OpenWriter(Instance **out, ifaces::cIGZFile *stream, const char *, int32_t)
{
    Instance *instance = static_cast<Instance *>(Allocate(sizeof(Instance)));
    if (instance == nullptr) return 0;
    std::memset(instance, 0, sizeof(Instance));
    instance->stream = stream;
    *out = instance;
    return 1;
}

// 0x87fe5d: the close of rows 5 / 6 and wclose of rows 3 / 5 / 6.
int32_t __stdcall FreeInstance(Instance *instance)
{
    return instance != nullptr ? FreeMemory(instance) : 0;
}

} // namespace gimex
