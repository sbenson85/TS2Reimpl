// Differential test for cGZFontSys::DetectKind 0x910b35 (lane PD, rendering R5b): the kind of a font file from its
// extension (.ttf 1, .ttc / .otf 4, .pfb 2, case-insensitive) or, for any other extension, from the first 32 bytes of the
// file ("%!PS" anywhere 2, 00 01 00 00 at the start 1, "ttcf" at the start 4).
//
// The file is a fake: cRZFile::Open (0x1c285d), Read (0x1c2c6e) and Close (0x1c0faa) stand in on both sides (the
// constructor and destructor run for real: the image's on one side, ours on the other); Open and Read answer from the case
// (success or failure, a content of 32 bytes or a short write), and every call is logged with the file's path and its
// arguments. The extension helper 0x1c1e0e is the image's on both sides. Compared: the kind, the call log, and the heap
// balance of each side.
#include "code_patch.h"
#include "difftest.h"

#include "gz/cGZFontSys.h"
#include "rz/cRZFile.h"

#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

struct Case {
    std::string path;
    bool opens = true;
    bool reads = true;
    uint32_t written = 32; // how many bytes Read writes
    uint8_t content[32] = {};
} gCase;

std::vector<std::string> gLog;

void Log(const std::string &line)
{
    difftest::NotCounted bookkeeping;
    gLog.push_back(line);
}

uint32_t __fastcall StandOpen(cRZFile *file, void *, uint32_t access, uint32_t creation, uint32_t sharing)
{
    difftest::NotCounted bookkeeping;
    Log("Open '" + file->mPath.mString + "' " + std::to_string(access) + "," + std::to_string(creation) + "," + std::to_string(sharing));
    return gCase.opens ? 1 : 0;
}

uint32_t __fastcall StandRead(cRZFile *file, void *, void *dst, uint32_t length)
{
    difftest::NotCounted bookkeeping;
    Log("Read '" + file->mPath.mString + "' " + std::to_string(length));
    if (length <= sizeof(gCase.content)) std::memcpy(dst, gCase.content, gCase.written < length ? gCase.written : length);
    return gCase.reads ? 1 : 0;
}

uint32_t __fastcall StandClose(cRZFile *file, void *)
{
    difftest::NotCounted bookkeeping;
    Log("Close '" + file->mPath.mString + "'");
    return 1;
}

// One side: the kind and the log, with the heap balance.
std::string RunSide(difftest::Context &ctx, bool ours)
{
    static uint8_t block[cGZFontSys::kSize];
    gLog.clear();
    gLog.reserve(8);
    FontSysString path(gCase.path.c_str());
    difftest::AllocationCounter heap;
    uint32_t kind;
    if (ours) kind = reinterpret_cast<cGZFontSys *>(block)->DetectKind(*path.s.AsInterface());
    else kind = ctx.Original<uint32_t(__thiscall *)(void *, const cIGZString *)>(0x910b35)(block, path.s.AsInterface());
    std::string result = "kind " + std::to_string(kind);
    for (const std::string &line : gLog) result += "; " + line;
    result += "; blocks " + std::to_string(heap.Blocks());
    return result;
}

void Fill(std::mt19937 &g, uint8_t (&content)[32])
{
    // Letters: no '%', no zero byte, so a pattern is only where the case puts it.
    for (uint8_t &b : content) b = static_cast<uint8_t>('A' + g() % 50);
}

} // namespace

DIFF_TEST(GZFontSys_DetectKind, 0x910b35)
{
    shape::StandIn open(0x1c285d, &cRZFile::Open, &StandOpen);
    shape::StandIn read(0x1c2c6e, &cRZFile::Read, &StandRead);
    shape::StandIn close(0x1c0faa, &cRZFile::Close, &StandClose);
    std::mt19937 g(910);

    const char *const dirs[] = {"", "c:\\fonts\\", "D:\\Data.v2\\Fonts\\", "e:/fonts/", ".\\"};
    const char *const stems[] = {"arial", "Times New", "a", "x.y", "TAHOMA"};
    const char *const exts[] = {".ttf", ".TTF", ".Ttf", ".ttc", ".TtC", ".otf", ".OTF", ".pfb", ".PFB", ".pfM", ".fon", ".txt", "", ".",
                                ".ttfx", ".tt", ".ttcc", ".otfx", ".pfbb", "ttf", ".thisextensionisquitelongindeed", ".a.ttf", ".ttf.fon"};
    // Header contents: where each pattern goes (-1: nowhere).
    const char psText[] = "%!PS";
    const uint8_t sfnt[4] = {0, 1, 0, 0};
    const char collection[] = "ttcf";
    for (int i = 0; i < 6000; ++i) {
        gCase = Case();
        gCase.path = std::string(dirs[g() % 5]) + stems[g() % 5] + exts[g() % (sizeof(exts) / sizeof(exts[0]))];
        gCase.opens = g() % 8 != 0;
        gCase.reads = g() % 8 != 0;
        gCase.written = g() % 6 == 0 ? g() % 33 : 32;
        Fill(g, gCase.content);
        const int ps = g() % 3 == 0 ? static_cast<int>(g() % 34) - 2 : -1; // -2, -1: nowhere; 29..31: cut off
        const int sf = g() % 3 == 0 ? static_cast<int>(g() % 6) - 1 : -1;  // 0 at the start, 1..4 elsewhere
        const int tc = g() % 3 == 0 ? static_cast<int>(g() % 6) - 1 : -1;
        if (tc >= 0) std::memcpy(gCase.content + tc, collection, g() % 9 == 0 ? 3 : 4);
        if (sf >= 0) std::memcpy(gCase.content + sf, sfnt, g() % 9 == 0 ? 3 : 4);
        if (ps >= 0) {
            const uint32_t room = 32 - ps;
            std::memcpy(gCase.content + ps, psText, room < 4 ? room : (g() % 9 == 0 ? 3 : 4));
        }
        // One more near-miss: the pattern with one byte changed.
        if (g() % 7 == 0) {
            const uint32_t at = g() % 32;
            gCase.content[at] = static_cast<uint8_t>(g());
        }
        const std::string a = RunSide(ctx, false), b = RunSide(ctx, true);
        ++ctx.cases;
        if (a != b)
            ctx.Fail("case %d '%s' open %d read %d written %u:\n    original: %s\n    ours:     %s", i, gCase.path.c_str(), gCase.opens,
                     gCase.reads, gCase.written, a.c_str(), b.c_str());
    }
}
