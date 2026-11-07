// Differential test for src/app/ReportFinish.*: the call-stack section of the exception report
// (0x1bc29e).
//
// The DbgHelp table in the image points at scripted fakes (StackWalk, SymGetSymFromAddr,
// SymGetLineFromAddr, UnDecorateSymbolName); the import table is faked for GetCurrentProcess,
// GetCurrentThread, VirtualQuery and GetModuleFileNameA (the module lookup); the report file is an
// object whose Length (slot 7) and Write (slot 16) slots are recording fakes. Source lines come from
// a real sandbox file. Both sides run on the same script; the log of every call with its arguments
// and everything written are compared.
#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#include "app/ReportFinish.h"
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

constexpr uint32_t kWalk = 0x1bc29e;
constexpr uint32_t kIatGetCurrentThread = 0xd9a190, kIatGetCurrentProcess = 0xd9a1b8, kIatGetModuleFileNameA = 0xd9a16c, kIatVirtualQuery = 0xd9a1cc;
constexpr uint32_t kSystemCodePageCache = 0xff23a4;

std::vector<std::string> gLog;

void Log(const char *fmt, ...)
{
    char buf[1024];
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

uint32_t Fnv(const void *data, size_t n)
{
    uint32_t h = 0x811c9dc5;
    for (size_t i = 0; i < n; ++i) h = (h ^ static_cast<const uint8_t *>(data)[i]) * 16777619u;
    return h;
}

void ExpectSame(difftest::Context &ctx, const char *what, const std::string &a, const std::string &b)
{
    ++ctx.cases;
    if (a != b) {
        size_t at = 0;
        while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
        size_t from = at > 80 ? at - 80 : 0;
        ctx.Fail("%s: differ at %zu\n  original: ...%.300s\n  ours:     ...%.300s", what, at, a.c_str() + from, b.c_str() + from);
    }
}

class Slots {
public:
    explicit Slots(difftest::Context &c) : ctx(c) {}
    ~Slots()
    {
        for (auto it = saved.rbegin(); it != saved.rend(); ++it) *ctx.image.At<void *>(it->first) = it->second;
    }
    void Set(uint32_t rva, void *value)
    {
        void **slot = ctx.image.At<void *>(rva);
        saved.emplace_back(rva, *slot);
        *slot = value;
    }

private:
    difftest::Context &ctx;
    std::vector<std::pair<uint32_t, void *>> saved;
};

// ---- the script ----

struct Frame {
    uint32_t pc, ebp, esp;
};

struct Script {
    std::vector<Frame> frames; // StackWalk call i answers frames[i], then fails
    bool endless = false;      // never fails: cycles frames
    uint32_t seed = 0;
    int symPercent = 100, linePercent = 100, undecPercent = 100;
    uint32_t startLength = 0;
    uint32_t fixedFirst = 0;
    std::string badPath;
    bool moduleNames = true;
} gScript;

// Per-run state of the fakes.
int gWalkCalls, gSymCalls, gLineCalls;
uint32_t gWritten;
std::string gSidePath; // this side's copy of the source file: its name is masked in what is written

// The game's file functions in "wide" mode are the sandbox's, so both sides' file handles are known to it.
void SetWideFileFunctions(difftest::Context &ctx, bool on)
{
    static const uint32_t globals[] = {0x10cb5e0, 0x10cb5e4, 0x10cb5e8, 0x10cb5ec, 0x10cb5f0, 0x10cb5f4};
    static const char *const names[] = {"CreateFileW", "CopyFileW", "MoveFileExW", "FindFirstFileW", "DeleteFileW", "GetFileAttributesW"};
    for (int i = 0; i < 6; ++i) *ctx.image.At<void *>(globals[i]) = on ? difftest::SandboxFunction(names[i]) : nullptr;
}

bool Roll(int kind, int index, int percent)
{
    uint32_t h = gScript.seed ^ (static_cast<uint32_t>(index) * 2654435761u) ^ (static_cast<uint32_t>(kind) * 40503u);
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return static_cast<int>(h % 100) < percent;
}

uint32_t Hash(int kind, int index)
{
    uint32_t h = gScript.seed * 31 + static_cast<uint32_t>(index) * 7919u + static_cast<uint32_t>(kind) * 104729u;
    h ^= h >> 16;
    h *= 0x45d9f3b;
    h ^= h >> 16;
    return h;
}

// A fake PE image the module lookup reads: two sections.
alignas(16) uint8_t gImage[0x1000];

void BuildImage()
{
    std::memset(gImage, 0, sizeof(gImage));
    const uint32_t ntAt = 0x80;
    *reinterpret_cast<uint32_t *>(gImage + 0x3c) = ntAt;
    *reinterpret_cast<uint16_t *>(gImage + ntAt + 6) = 2;
    *reinterpret_cast<uint16_t *>(gImage + ntAt + 0x14) = 0xe0;
    uint8_t *h = gImage + ntAt + 0x18 + 0xe0;
    std::memcpy(h, ".text", 6);
    *reinterpret_cast<uint32_t *>(h + 8) = 0x300;
    *reinterpret_cast<uint32_t *>(h + 0xc) = 0x100;
    *reinterpret_cast<uint32_t *>(h + 0x10) = 0x200;
    h += 0x28;
    std::memcpy(h, ".data", 6);
    *reinterpret_cast<uint32_t *>(h + 8) = 0x100;
    *reinterpret_cast<uint32_t *>(h + 0xc) = 0x500;
    *reinterpret_cast<uint32_t *>(h + 0x10) = 0x400;
}

DWORD_PTR WINAPI FakeGetCurrentProcess()
{
    Log("GetCurrentProcess");
    return 0xffffff01;
}
DWORD_PTR WINAPI FakeGetCurrentThread()
{
    Log("GetCurrentThread");
    return 0xffffff02;
}

SIZE_T WINAPI FakeVirtualQuery(const void *address, MEMORY_BASIC_INFORMATION *info, SIZE_T length)
{
    Log("VirtualQuery(%p, %u)", address, static_cast<unsigned>(length));
    const uint8_t *p = static_cast<const uint8_t *>(address);
    std::memset(info, 0x6b, sizeof(*info));
    if (p >= gImage && p < gImage + 0x1000) {
        info->AllocationBase = gImage;
        return sizeof(*info);
    }
    return 0;
}

DWORD WINAPI FakeGetModuleFileName(void *module, char *buffer, DWORD size)
{
    Log("GetModuleFileNameA(%p, %u)", module, size);
    if (!gScript.moduleNames) return 0;
    const char name[] = "C:\\Games\\Sims 2\\TSBin\\Sims2EP9.exe";
    std::memcpy(buffer, name, sizeof(name));
    return sizeof(name) - 1;
}

BOOL WINAPI FakeStackWalk(DWORD machine, void *process, void *thread, appbase::StackFrame32 *frame, CONTEXT *context, void *readMemory, void *tableAccess,
                          void *moduleBase, void *translate)
{
    int call = gWalkCalls++;
    Log("StackWalk(%x, %p, %p, ctx eip %08x esp %08x ebp %08x hash %08x, read %p, access %p, base %p, translate %p) frame %s", machine, process, thread,
        context->Eip, context->Esp, context->Ebp, Fnv(context, sizeof(*context)), readMemory, tableAccess, moduleBase, translate, Hex(frame, sizeof(*frame)).c_str());
    const size_t n = gScript.frames.size();
    if (!n || (call >= static_cast<int>(n) && !gScript.endless)) return FALSE;
    const Frame &f = gScript.frames[call % n];
    frame->pc.offset = f.pc;
    frame->frame.offset = f.ebp;
    frame->stack.offset = f.esp;
    frame->ret.offset = 0x5a5a;
    return TRUE;
}

const char *const kSymbolNames[] = {"main", "?Foo@@YAXH@Z", "_WinMain@16", "a_really_long_symbol_name_for_the_heap_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
                                    "", "??0cRZString@@QAE@XZ"};

BOOL WINAPI FakeSymFromAddr(void *process, DWORD address, DWORD *displacement, appbase::ImageHelpSymbol *symbol)
{
    int call = gSymCalls++;
    Log("SymGetSymFromAddr(%p, %08x, disp %u, size %x max %x)", process, address, *displacement, symbol->sizeOfStruct, symbol->maxNameLength);
    if (!Roll(1, call, gScript.symPercent)) return FALSE;
    std::strcpy(symbol->name, kSymbolNames[Hash(2, call) % 6]);
    *displacement = Hash(3, call) % 300;
    symbol->address = address - *displacement;
    return TRUE;
}

BOOL WINAPI FakeLineFromAddr(void *process, DWORD address, DWORD *displacement, appbase::ImageHelpLine *line)
{
    int call = gLineCalls++;
    Log("SymGetLineFromAddr(%p, %08x, disp %u, size %x)", process, address, *displacement, line->sizeOfStruct);
    if (!Roll(4, call, gScript.linePercent)) return FALSE;
    line->lineNumber = Hash(5, call) % 12; // the file has 8 lines
    line->fileName = const_cast<char *>(Roll(6, call, 75) ? gSidePath.c_str() : gScript.badPath.c_str());
    line->address = address;
    *displacement = Hash(7, call) % 20;
    return TRUE;
}

DWORD WINAPI FakeUndecorate(const char *name, char *out, DWORD length, DWORD flags)
{
    static int call = 0;
    Log("UnDecorateSymbolName(%s, %u, %u)", name, length, flags);
    ++call;
    if (!Roll(8, static_cast<int>(Fnv(name, std::strlen(name))), gScript.undecPercent)) return 0;
    std::snprintf(out, length, "undecorated %s", name);
    return 1;
}

// The report file.
void *gFileVt[40];
int gLengthCalls;
int32_t gLengthAdjust;
uint32_t __fastcall FakeLength(void *, void *)
{
    // fixedFirst: the first answer is exactly that value (the 200000-byte limit's edges); later ones grow with what was written.
    if (!gLengthCalls++ && gScript.fixedFirst) gLengthAdjust = static_cast<int32_t>(gScript.fixedFirst - (gScript.startLength + gWritten));
    uint32_t r = gScript.startLength + gWritten + gLengthAdjust;
    Log("Length -> %u", r);
    return r;
}
uint8_t __fastcall FakeWrite(void *, void *, const void *data, uint32_t length)
{
    std::string text(static_cast<const char *>(data), length);
    for (size_t at; !gSidePath.empty() && (at = text.find(gSidePath)) != std::string::npos;) text.replace(at, gSidePath.size(), "<PATH>");
    Log("Write len=%u [%s]", length, Hex(text.data(), text.size()).c_str());
    gWritten += length;
    return 1;
}

void WriteBytes(const std::wstring &path, const std::string &bytes)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD written = 0;
    if (!bytes.empty()) WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(h);
}

std::string Narrow(const std::wstring &wide)
{
    char ansi[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, ansi, sizeof(ansi), nullptr, nullptr);
    return ansi;
}

} // namespace

DIFF_TEST(ReportFinish_WriteCallStack, kWalk)
{
    BuildImage();
    Slots slots(ctx);
    slots.Set(kIatGetCurrentProcess, reinterpret_cast<void *>(&FakeGetCurrentProcess));
    slots.Set(kIatGetCurrentThread, reinterpret_cast<void *>(&FakeGetCurrentThread));
    slots.Set(kIatVirtualQuery, reinterpret_cast<void *>(&FakeVirtualQuery));
    slots.Set(kIatGetModuleFileNameA, reinterpret_cast<void *>(&FakeGetModuleFileName));
    int32_t *codePage = ctx.image.At<int32_t>(kSystemCodePageCache);
    const int32_t savedCodePage = *codePage;
    uint8_t *table = ctx.image.At<uint8_t>(appbase::DbgHelpTable::kGlobal);
    uint8_t savedTable[0x74];
    std::memcpy(savedTable, table, sizeof(savedTable));

    std::memcpy(gFileVt, ctx.image.At<void>(cRZTextFile::kVTable), sizeof(gFileVt));
    gFileVt[7] = reinterpret_cast<void *>(&FakeLength);
    gFileVt[16] = reinterpret_cast<void *>(&FakeWrite);

    // The source file the line lookups name: eight lines with blanks to trim.
    SetWideFileFunctions(ctx, true);
    const char *const kSource = "int main()\r\n{\r\n\t  return 0;  \t\r\n\r\n    call(x);\n  \t\n} // end   \r\nlast line without terminator";
    const std::wstring wideGood[2] = {difftest::SandboxPath(L"stack_source_a.txt"), difftest::SandboxPath(L"stack_source_b.txt")};
    WriteBytes(wideGood[0], kSource);
    WriteBytes(wideGood[1], kSource);
    const std::string goodPath[2] = {Narrow(wideGood[0]), Narrow(wideGood[1])};
    const std::string badPath = Narrow(difftest::SandboxPath(L"stack_missing_source.txt"));
    DeleteFileW(difftest::SandboxPath(L"stack_missing_source.txt").c_str());

    std::mt19937 rng(0x1bc29e);
    for (int run = 0; run < 700; ++run) {
        gScript = Script();
        
        gScript.badPath = badPath;
        gScript.seed = rng();
        int mode = run < 14 ? run : static_cast<int>(rng() % 14);
        int frames = static_cast<int>(rng() % 7);
        for (int i = 0; i < frames; ++i) {
            Frame f;
            switch (rng() % 5) {
            case 0: f.pc = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gImage)) + 0x100 + rng() % 0x300; break; // in .text
            case 1: f.pc = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gImage)) + 0x500 + rng() % 0x100; break; // in .data
            case 2: f.pc = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gImage)) + 0x800 + rng() % 0x100; break; // in the image, no section
            case 3: f.pc = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gImage)) + 0x100; break;                  // a section's first byte
            default: f.pc = rng(); break;                                                                             // not in the image
            }
            f.ebp = rng() % 11 == 0 ? 0 : rng() | 1;
            f.esp = rng();
            gScript.frames.push_back(f);
        }
        bool hasSym = true, hasLine = true, hasUndec = true;
        uint8_t version51 = 1;
        switch (mode) {
        case 0: break;
        case 1: gScript.symPercent = 0; break;                                  // symbols never found: module only, the .pdb note once
        case 2: hasSym = false; break;                                          // no SymGetSymFromAddr: minimal note
        case 3: version51 = 0; break;                                           // old DbgHelp: minimal note, still walks
        case 4: version51 = 0; gScript.symPercent = 0; break;                   // old and no symbols: no .pdb note
        case 5: hasLine = false; break;
        case 6: hasUndec = false; break;
        case 7: gScript.linePercent = 0; break;
        case 8: gScript.undecPercent = 0; break;
        case 9: gScript.startLength = 199990 + rng() % 20; break;                // the length limit is crossed during the walk
        case 10: gScript.fixedFirst = 199999 + rng() % 3; break;                 // the first answer at the limit or one either side
        case 11: gScript.moduleNames = false; break;
        case 12: gScript.endless = true; break;                                  // 500 frames at most
        default:
            gScript.symPercent = static_cast<int>(rng() % 101);
            gScript.linePercent = static_cast<int>(rng() % 101);
            gScript.undecPercent = static_cast<int>(rng() % 101);
            hasSym = rng() % 8 != 0;
            hasLine = rng() % 8 != 0;
            hasUndec = rng() % 8 != 0;
            version51 = rng() % 6 != 0;
            break;
        }
        if (mode == 12 && gScript.frames.empty()) gScript.frames.push_back({gScript.seed | 0x10000, 0x1234, 0x5678});
        if (mode == 12) {
            gScript.symPercent = 50;
            gScript.linePercent = 50;
        }
        int32_t charSize = rng() % 5 == 0 ? 2 : 0;
        // The walk may be absent altogether.
        bool hasWalk = rng() % 20 != 0;

        CONTEXT context;
        for (size_t i = 0; i < sizeof(context); ++i) reinterpret_cast<uint8_t *>(&context)[i] = static_cast<uint8_t>(rng());
        if (rng() % 9 == 0) context.Ebp = 0;
        if (rng() % 3 == 0) context.Eip = static_cast<DWORD>(reinterpret_cast<uintptr_t>(gImage)) + 0x100 + rng() % 0x300;

        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            std::memset(table, 0, 0x74);
            *reinterpret_cast<uint8_t *>(table + 8) = version51;
            auto set = [&](uint32_t offset, void *value) { std::memcpy(table + offset, &value, 4); };
            if (hasWalk) set(0x2c, reinterpret_cast<void *>(&FakeStackWalk));
            set(0x30, reinterpret_cast<void *>(0x30303030));
            set(0x34, reinterpret_cast<void *>(0x34343434));
            if (hasSym) set(0x38, reinterpret_cast<void *>(&FakeSymFromAddr));
            if (hasLine) set(0x3c, reinterpret_cast<void *>(&FakeLineFromAddr));
            if (hasUndec) set(0x44, reinterpret_cast<void *>(&FakeUndecorate));
            gLog.clear();
            *codePage = -3;
            gSidePath = goodPath[side];
            gWalkCalls = gSymCalls = gLineCalls = 0;
            gWritten = 0;
            gLengthCalls = 0;
            gLengthAdjust = 0;
            alignas(8) uint8_t raw[appbase::ExceptionReporterObject::kSize];
            std::memset(raw, 0x77, sizeof(raw));
            *reinterpret_cast<void **>(raw + 0xa0) = gFileVt;
            *reinterpret_cast<int32_t *>(raw + 0xa0 + 0x9c) = charSize;
            if (side == 0) ctx.Original<void(__thiscall *)(void *, const CONTEXT *)>(kWalk)(raw, &context);
            else appbase::WriteCallStack(reinterpret_cast<appbase::ExceptionReporterObject *>(raw), nullptr, &context);
            results[side] = "walk " + std::to_string(gWalkCalls) + " sym " + std::to_string(gSymCalls) + " line " + std::to_string(gLineCalls) + " written " +
                            std::to_string(gWritten) + "\n" + Joined();
        }
        ExpectSame(ctx, ("call stack mode " + std::to_string(mode) + " run " + std::to_string(run)).c_str(), results[0], results[1]);
    }

    std::memcpy(table, savedTable, sizeof(savedTable));
    *codePage = savedCodePage;
    SetWideFileFunctions(ctx, false);
}
