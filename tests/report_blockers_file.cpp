// Differential tests for src/app/ReportBlockers.* text-file line reading: 0x1c408f (a line into a
// string), 0x1c42bf (into a buffer), 0x1bbd40 (line N of a file by path) and the two string
// assignments they end with (0x1acbfb, 0x1ad84c).
//
// The file objects carry a copy of cRZTextFile's vtable whose slots 3, 4, 6, 7, 11, 12 and 14 are
// recording wrappers around the original slots, so the sequence of calls the reader makes (with
// results and the bytes read) is compared as well as its results; the files are real sandbox files.
// Narrow files are read to the end; UTF-16 files are read only as far as the original's byte/unit
// count bug (see ReportBlockers.h) stays inside the data it filled: short lines, and a long tail line
// that is never read.
#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#include "app/ReportBlockers.h"
#include "difftest.h"
#include "game/game.h"
#include "rz/cRZString.h"
#include "rz/cRZTextFile.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kReadLineString = 0x1c408f, kReadLineBuffer = 0x1c42bf, kReadFileLine = 0x1bbd40, kAssignText = 0x1acbfb, kAssignNarrowed = 0x1ad84c;
constexpr int kSlots = 34;

std::vector<std::string> gLog;

void Log(const char *fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    gLog.emplace_back(buf);
}

std::string Joined()
{
    std::string out;
    for (const std::string &s : gLog) out += s + "|";
    return out;
}

std::string Hex(const void *data, size_t n)
{
    std::string out;
    char b[4];
    for (size_t i = 0; i < n; ++i) {
        std::snprintf(b, sizeof(b), "%02x", static_cast<const uint8_t *>(data)[i]);
        out += b;
    }
    return out;
}

void ExpectSame(difftest::Context &ctx, const char *what, const std::string &a, const std::string &b)
{
    ++ctx.cases;
    if (a != b) {
        size_t at = 0;
        while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
        size_t from = at > 60 ? at - 60 : 0;
        ctx.Fail("%s: differ at %zu\n  original: ...%.240s\n  ours:     ...%.240s", what, at, a.c_str() + from, b.c_str() + from);
    }
}

struct Str {
    alignas(cRZString) unsigned char raw[sizeof(cRZString)];
    explicit Str(const char *text = "")
    {
        std::memset(raw, 0xcd, sizeof(raw));
        S().Construct(text);
    }
    ~Str() { S().Destruct(); }
    Str(const Str &) = delete;
    cRZString &S() { return *reinterpret_cast<cRZString *>(raw); }
};

// ---- the recording vtable ----

void *gOriginalSlots[kSlots];
void *gRecordingSlots[kSlots];
uint32_t gReadCount = 0, gFailReadAt = 0; // fail the Nth Read call of a run (1-based; 0 never)

uint32_t __fastcall RecIsOpen(void *self, void *)
{
    uint32_t r = reinterpret_cast<uint32_t(__thiscall *)(void *)>(gOriginalSlots[4])(self);
    Log("IsOpen -> %u", r & 0xff);
    return r;
}
uint32_t __fastcall RecOpen(void *self, void *, uint32_t access, uint32_t creation, uint32_t sharing)
{
    uint32_t r = reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(gOriginalSlots[3])(self, access, creation, sharing);
    Log("Open(%u,%u,%u) -> %u", access, creation, sharing, r & 0xff);
    return r;
}
uint32_t __fastcall RecPosition(void *self, void *)
{
    uint32_t r = reinterpret_cast<uint32_t(__thiscall *)(void *)>(gOriginalSlots[6])(self);
    Log("Position -> %d", static_cast<int32_t>(r));
    return r;
}
uint32_t __fastcall RecLength(void *self, void *)
{
    uint32_t r = reinterpret_cast<uint32_t(__thiscall *)(void *)>(gOriginalSlots[7])(self);
    Log("Length -> %u", r);
    return r;
}
uint32_t __fastcall RecSeekRelative(void *self, void *, int32_t offset)
{
    uint32_t r = reinterpret_cast<uint32_t(__thiscall *)(void *, int32_t)>(gOriginalSlots[11])(self, offset);
    Log("SeekToRelativePosition(%d) -> %d", offset, static_cast<int32_t>(r));
    return r;
}
uint32_t __fastcall RecSeekPosition(void *self, void *, int32_t position)
{
    uint32_t r = reinterpret_cast<uint32_t(__thiscall *)(void *, int32_t)>(gOriginalSlots[12])(self, position);
    Log("SeekToPosition(%d) -> %d", position, static_cast<int32_t>(r));
    return r;
}
uint32_t __fastcall RecRead(void *self, void *, void *dst, uint32_t count)
{
    if (++gReadCount == gFailReadAt) {
        Log("Read(%u) failed by the script", count);
        return 0;
    }
    uint32_t r = reinterpret_cast<uint32_t(__thiscall *)(void *, void *, uint32_t)>(gOriginalSlots[14])(self, dst, count);
    Log("Read(%u) -> %u %s", count, r & 0xff, (r & 0xff) ? Hex(dst, count).c_str() : "");
    return r;
}

void BuildVTable(difftest::Context &ctx)
{
    std::memcpy(gOriginalSlots, ctx.image.At(cRZTextFile::kVTable), sizeof(gOriginalSlots));
    std::memcpy(gRecordingSlots, gOriginalSlots, sizeof(gRecordingSlots));
    gRecordingSlots[3] = reinterpret_cast<void *>(&RecOpen);
    gRecordingSlots[4] = reinterpret_cast<void *>(&RecIsOpen);
    gRecordingSlots[6] = reinterpret_cast<void *>(&RecPosition);
    gRecordingSlots[7] = reinterpret_cast<void *>(&RecLength);
    gRecordingSlots[11] = reinterpret_cast<void *>(&RecSeekRelative);
    gRecordingSlots[12] = reinterpret_cast<void *>(&RecSeekPosition);
    gRecordingSlots[14] = reinterpret_cast<void *>(&RecRead);
}

// ---- files ----

std::string NarrowPath(const std::wstring &wide)
{
    char ansi[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, ansi, sizeof(ansi), nullptr, nullptr);
    return ansi;
}

void WriteBytes(const std::wstring &path, const std::string &bytes)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD written = 0;
    if (!bytes.empty()) WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(h);
}

struct FileSpec {
    std::string bytes;
    bool wide = false;
    int lines = 0; // lines that can be read safely (wide files)
};

const char *const kNarrowTerminators[] = {"\n", "\r\n", "\r", "\n\r", "\r\n", "\n"};
const char *const kWideTerminators[] = {"\n\0", "\r\0\n\0", "\r\0", "\n\0\r\0"};
const size_t kWideTerminatorLength[] = {2, 4, 2, 4};

FileSpec MakeNarrow(std::mt19937 &rng)
{
    static const int lengths[] = {0, 0, 1, 2, 5, 17, 60, 126, 127, 128, 129, 130, 255, 256, 257, 300, 777, 2047, 2048, 2049, 2300};
    FileSpec spec;
    int lines = rng() % 7;
    if (rng() % 12 == 0) lines = 0;
    for (int i = 0; i < lines; ++i) {
        int length = lengths[rng() % (sizeof(lengths) / sizeof(lengths[0]))];
        for (int c = 0; c < length; ++c) {
            // Printable, plus some high-bit bytes; never a CR or LF.
            spec.bytes += rng() % 9 == 0 ? static_cast<char>(0x80 + rng() % 0x80) : static_cast<char>(' ' + rng() % 95);
        }
        if (i + 1 < lines || rng() % 3 != 0) spec.bytes += kNarrowTerminators[rng() % 6];
    }
    if (rng() % 15 == 0) spec.bytes = std::string(1, static_cast<char>(rng() % 256)); // a single byte
    if (rng() % 25 == 0) spec.bytes = "\xef\xbb\xbf" + spec.bytes;                     // a UTF-8 mark: not a text-file mark
    return spec;
}

FileSpec MakeWide(std::mt19937 &rng)
{
    FileSpec spec;
    spec.wide = true;
    spec.bytes = "\xff\xfe";
    int lines = 1 + rng() % 5;
    for (int i = 0; i < lines; ++i) {
        // 1 to 20 characters: twice the count in bytes, doubled again, fits the 0x80-byte read; never empty,
        // so two terminators are never adjacent (they would pair up and the line count would be off).
        int length = 1 + rng() % 20;
        for (int c = 0; c < length; ++c) {
            uint16_t unit = rng() % 8 == 0 ? static_cast<uint16_t>(0x80 + rng() % 0x700) : static_cast<uint16_t>(' ' + rng() % 95);
            spec.bytes.append(reinterpret_cast<const char *>(&unit), 2);
        }
        int t = rng() % 4;
        spec.bytes.append(kWideTerminators[t], kWideTerminatorLength[t]);
        ++spec.lines;
    }
    for (int c = 0; c < 150; ++c) { // the tail is never read: it keeps every chunk at the full 0x80 bytes
        uint16_t unit = 'x';
        spec.bytes.append(reinterpret_cast<const char *>(&unit), 2);
    }
    return spec;
}

// A text file object (original code constructs and destroys it) with the recording vtable.
struct Reader {
    alignas(8) uint8_t raw[cRZTextFile::kSize];
    const void *original;
    Reader(const std::string &path, const void *originalVTable) : original(originalVTable)
    {
        std::memset(raw, 0x6d, sizeof(raw));
        file().Construct(path.c_str());
        file().mVTable = gRecordingSlots;
    }
    ~Reader()
    {
        file().mVTable = original;
        file().Destruct();
    }
    cRZTextFile &file() { return *reinterpret_cast<cRZTextFile *>(raw); }
};

std::string State(cRZTextFile &f)
{
    return "cs " + std::to_string(f.mCharSize) + " bom " + std::to_string(f.mByteOrderMark) + " line " + Hex(f.mLine.data(), f.mLine.size()) + " wide " +
           Hex(f.mWide.data(), f.mWide.size() * 2) + " text " + Hex(f.mText.mString.data(), f.mText.mString.size()) + " pos " + std::to_string(f.mPosition);
}

} // namespace

// Reads every line (or only `limit` of them) of a file through 0x1c408f; the first call opens it.
namespace {

void RunStringReads(difftest::Context &ctx, const char *label, const FileSpec &spec, std::mt19937 &rng, int mode)
{
    std::string path[2] = {NarrowPath(difftest::SandboxPath(L"line_a.txt")), NarrowPath(difftest::SandboxPath(L"line_b.txt"))};
    WriteBytes(difftest::SandboxPath(L"line_a.txt"), spec.bytes);
    WriteBytes(difftest::SandboxPath(L"line_b.txt"), spec.bytes);
    int reads = spec.wide ? spec.lines : 9; // narrow: past the end too
    bool preOpen = mode == 1;
    uint32_t failAt = mode == 2 ? 1 + rng() % 8 : 0;
    int32_t preSeek = mode == 3 ? static_cast<int32_t>(rng() % 6) : -1;
    std::string results[2];
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, cIGZString *)>(kReadLineString);
    for (int side = 0; side < 2; ++side) {
        gLog.clear();
        gReadCount = 0;
        gFailReadAt = 0;
        Reader reader(path[side], ctx.image.At(cRZTextFile::kVTable));
        std::string summary;
        if (preOpen || preSeek >= 0) {
            // Through the original slots (handles our own Open creates are not known to the sandbox).
            reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(gOriginalSlots[3])(&reader.file(), 1, 2, 1);
            if (preSeek >= 0) reinterpret_cast<uint32_t(__thiscall *)(void *, int32_t)>(gOriginalSlots[12])(&reader.file(), preSeek);
        }
        gFailReadAt = failAt; // counted from the first read of the line reader
        gReadCount = 0;
        for (int i = 0; i < reads; ++i) {
            Str out("stale text in the output string, long enough to be on the heap");
            uint32_t r = side == 0 ? original(&reader.file(), out.S().AsInterface()) : appbase::ReadLineToString(&reader.file(), nullptr, out.S().AsInterface());
            summary += "ret " + std::to_string(r & 0xff) + " out " + Hex(out.S().mString.data(), out.S().mString.size()) + " " + State(reader.file()) + "\n" + Joined() + "\n";
            gLog.clear();
        }
        results[side] = summary;
    }
    std::string title = std::string(label) + " mode " + std::to_string(mode) + " failAt " + std::to_string(failAt) + " preSeek " + std::to_string(preSeek);
    ExpectSame(ctx, title.c_str(), results[0], results[1]);
}

} // namespace

DIFF_TEST(ReportBlockers_ReadLineToString_Narrow, kReadLineString)
{
    BuildVTable(ctx);
    std::mt19937 rng(4401);
    for (int run = 0; run < 260; ++run) RunStringReads(ctx, "narrow file", MakeNarrow(rng), rng, run % 4);
    // Fixed shapes around the terminators and the 0x80-byte reads.
    const char *const fixed[] = {"", "\n", "\r", "\r\n", "\n\r", "\n\n", "\r\r", "a", "a\n", "a\r\nb\r\nc", "\r\n\r\n", "\n\r\n\r", "abc\rdef\n\rghi\r\n", "\r\r\n"};
    for (const char *content : fixed)
        for (int mode = 0; mode < 4; ++mode) {
            FileSpec spec;
            spec.bytes = content;
            RunStringReads(ctx, content, spec, rng, mode);
        }
    // Lines right at the edge of a chunk, with each terminator after it.
    for (int length = 125; length <= 131; ++length)
        for (const char *t : {"\n", "\r\n", "\r", "\n\r", ""}) {
            FileSpec spec;
            spec.bytes = std::string(length, 'q') + t + std::string(length, 'r') + t + "end";
            RunStringReads(ctx, "edge", spec, rng, 0);
        }
}

DIFF_TEST(ReportBlockers_ReadLineToString_Wide, kReadLineString)
{
    BuildVTable(ctx);
    std::mt19937 rng(4402);
    // No seeking into the middle of a wide file (mode 3): an odd offset shifts the units, no terminator
    // is found, and the original's unit count then reads past its 0x80-byte buffer.
    for (int run = 0; run < 160; ++run) RunStringReads(ctx, "wide file", MakeWide(rng), rng, run % 3);
}

// ---- 0x1c42bf ----

DIFF_TEST(ReportBlockers_ReadLineToBuffer, kReadLineBuffer)
{
    BuildVTable(ctx);
    std::mt19937 rng(4403);
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, char *, uint32_t *)>(kReadLineBuffer);
    static const uint32_t capacities[] = {0, 1, 2, 3, 10, 100, 0x3f0, 0x3f0};
    for (int run = 0; run < 220; ++run) {
        bool wide = run % 5 == 4;
        FileSpec spec = wide ? MakeWide(rng) : MakeNarrow(rng);
        WriteBytes(difftest::SandboxPath(L"line_a.txt"), spec.bytes);
        WriteBytes(difftest::SandboxPath(L"line_b.txt"), spec.bytes);
        std::string path[2] = {NarrowPath(difftest::SandboxPath(L"line_a.txt")), NarrowPath(difftest::SandboxPath(L"line_b.txt"))};
        int reads = wide ? spec.lines : 7;
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            gReadCount = gFailReadAt = 0;
            Reader reader(path[side], ctx.image.At(cRZTextFile::kVTable));
            std::mt19937 local(run);
            std::string summary;
            for (int i = 0; i < reads; ++i) {
                uint8_t buffer[0x400];
                std::memset(buffer, 0xab, sizeof(buffer));
                uint32_t length = capacities[local() % 8];
                uint32_t r = side == 0 ? original(&reader.file(), reinterpret_cast<char *>(buffer), &length)
                                       : appbase::ReadLineToBuffer(&reader.file(), nullptr, reinterpret_cast<char *>(buffer), &length);
                summary += "ret " + std::to_string(r & 0xff) + " length " + std::to_string(length) + " buffer " + Hex(buffer, length < 0x3f0 ? length + 8 : 0x3f8) + " " +
                           State(reader.file()) + "\n" + Joined() + "\n";
                gLog.clear();
            }
            results[side] = summary;
        }
        ExpectSame(ctx, wide ? "buffer, wide file" : "buffer, narrow file", results[0], results[1]);
    }
}

// ---- 0x1bbd40 ----

// The file is opened by our own cRZFile code on one side and by the original's on the other, and the
// game's slots (original code) then work on the handle: in the game's wide mode the file functions are
// the sandbox's, so both sides' handles are known to it (the ANSI path would use the real CreateFileA).
namespace {

void SetWideFileFunctions(difftest::Context &ctx, bool on)
{
    static const uint32_t globals[] = {0x10cb5e0, 0x10cb5e4, 0x10cb5e8, 0x10cb5ec, 0x10cb5f0, 0x10cb5f4};
    static const char *const names[] = {"CreateFileW", "CopyFileW", "MoveFileExW", "FindFirstFileW", "DeleteFileW", "GetFileAttributesW"};
    for (int i = 0; i < 6; ++i) *ctx.image.At<void *>(globals[i]) = on ? difftest::SandboxFunction(names[i]) : nullptr;
}

} // namespace

DIFF_TEST(ReportBlockers_ReadFileLine, kReadFileLine)
{
    SetWideFileFunctions(ctx, true);
    std::mt19937 rng(4404);
    auto original = ctx.Original<uint32_t(__cdecl *)(const char *, int32_t, cRZString *)>(kReadFileLine);
    for (int run = 0; run < 260; ++run) {
        bool wide = run % 6 == 5;
        bool missing = run % 13 == 7;
        FileSpec spec = wide ? MakeWide(rng) : MakeNarrow(rng);
        WriteBytes(difftest::SandboxPath(L"line_a.txt"), spec.bytes);
        WriteBytes(difftest::SandboxPath(L"line_b.txt"), spec.bytes);
        std::string path[2] = {NarrowPath(difftest::SandboxPath(missing ? L"missing_a.txt" : L"line_a.txt")),
                               NarrowPath(difftest::SandboxPath(missing ? L"missing_b.txt" : L"line_b.txt"))};
        int top = wide ? spec.lines - 1 : 7; // wide: not the tail line
        for (int index = -2; index < top + 1; ++index) {
            std::string results[2];
            for (int side = 0; side < 2; ++side) {
                Str out("stale text in the output string, long enough to be on the heap");
                uint32_t r = side == 0 ? original(path[side].c_str(), index, &out.S()) : appbase::ReadFileLine(path[side].c_str(), index, &out.S());
                results[side] = "ret " + std::to_string(r & 0xff) + " out " + Hex(out.S().mString.data(), out.S().mString.size()) + " size " + std::to_string(out.S().mString.size());
            }
            ExpectSame(ctx, wide ? "file line, wide" : "file line, narrow", results[0], results[1]);
        }
    }
    SetWideFileFunctions(ctx, false);
}

// ---- 0x1acbfb and 0x1ad84c ----

DIFF_TEST(ReportBlockers_AssignText, kAssignText)
{
    auto original = ctx.Original<void(__cdecl *)(const std::string *, cRZString *)>(kAssignText);
    std::mt19937 rng(4405);
    for (int run = 0; run < 400; ++run) {
        std::string text(rng() % 40, 'a');
        for (char &c : text) c = static_cast<char>(1 + rng() % 255);
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            Str dst(std::string((run % 3) * 20, 'z').c_str());
            std::string source = text;
            // Self-assignment: the source is the destination's own string.
            const std::string *src = run % 11 == 0 ? &dst.S().mString : &source;
            (side == 0 ? original : appbase::AssignText)(src, &dst.S());
            results[side] = Hex(dst.S().mString.data(), dst.S().mString.size()) + " cap " + std::to_string(dst.S().mString.capacity());
        }
        ExpectSame(ctx, "assign text", results[0], results[1]);
    }
}

DIFF_TEST(ReportBlockers_AssignNarrowed, kAssignNarrowed)
{
    auto original = ctx.Original<void(__cdecl *)(const std::u16string *, cRZString *)>(kAssignNarrowed);
    std::mt19937 rng(4406);
    static const char16_t special[] = {0, 1, 0x7e, 0x7f, 0x80, 0x81, 0xff, 0x100, 0x7fff, 0x8000, 0xffff, 0xd800};
    for (int run = 0; run < 400; ++run) {
        std::u16string wide;
        int length = (run % 5 == 0) ? 0 : static_cast<int>(rng() % 60);
        for (int i = 0; i < length; ++i) wide += rng() % 3 == 0 ? special[rng() % 12] : static_cast<char16_t>(rng() % 0x90);
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            Str dst(std::string((run % 3) * 20, 'z').c_str());
            (side == 0 ? original : appbase::AssignNarrowed)(&wide, &dst.S());
            results[side] = Hex(dst.S().mString.data(), dst.S().mString.size()) + " size " + std::to_string(dst.S().mString.size()) + " cap " + std::to_string(dst.S().mString.capacity());
        }
        ExpectSame(ctx, "narrowed", results[0], results[1]);
    }
}

