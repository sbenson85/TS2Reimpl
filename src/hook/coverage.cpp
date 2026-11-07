// Host side of the first-call coverage (src/hook/coverage.h): reads the site list, registers the handler, arms the
// breakpoints and writes the records to ts2coverage.txt next to the host while the game runs. Off unless
// TS2HOST_COVERAGE is set; nothing here runs otherwise.
//
// ts2coverage.txt, one record per line, in the order the first calls happened (hex without 0x unless noted):
//   # ...                          comments: format, site counts
//   O <rva>                        a site left alone because it starts with our jump (hooked, trapped, detoured)
//   M <rva> <expected> <found>     a site left alone because its byte isn't the expected one
//   R <round> <ms> <label>         a round starts (round 0 at the start; then each "coverage <label>" command)
//   F <rva> <from> <ms> <thread>   the function at <rva> ran for the first time this round; <from> is the return
//                                  address on the stack at its entry: i<rva> in the image, x<address> outside it (our
//                                  code calling an original, or a system DLL calling back); ms since arming (decimal),
//                                  thread id (decimal)
// The writer flushes every 250 ms and at each round, so a run that ends abruptly (a trap, a kill) loses at most the last
// quarter second. tools/coverage.py report reads it.
#include "hook/coverage.h"

#include "hook/log.h"

#include <windows.h>

#include <share.h>

#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace {

coverage::Recorder gRecorder;
bool gOn = false;
FILE *gOut = nullptr;
uintptr_t gBase = 0;
uint32_t gSize = 0;
std::mutex gWriteLock;
size_t gWritten = 0;
std::vector<std::string> gLabels; // by round

LONG CALLBACK CoverageHandler(EXCEPTION_POINTERS *info)
{
    return gRecorder.OnException(info) ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}

void Drain()
{
    std::lock_guard<std::mutex> lock(gWriteLock);
    const size_t n = gRecorder.Count();
    while (gWritten < n) {
        const coverage::Hit &h = gRecorder.At(gWritten);
        const auto rva = static_cast<uint32_t>(h.rva);
        if (rva == 0) break; // not published yet
        if (rva == coverage::kRoundMarker) {
            const char *label = h.from < gLabels.size() ? gLabels[h.from].c_str() : "?";
            std::fprintf(gOut, "R %u %u %s\n", h.from, h.ms, label);
        } else if (h.from >= gBase && h.from < gBase + gSize) {
            std::fprintf(gOut, "F %x i%x %u %u\n", rva, static_cast<unsigned>(h.from - gBase), h.ms, h.thread);
        } else {
            std::fprintf(gOut, "F %x x%08x %u %u\n", rva, h.from, h.ms, h.thread);
        }
        ++gWritten;
    }
    if (gRecorder.Dropped()) std::fprintf(gOut, "# dropped %ld records (capacity)\n", gRecorder.Dropped());
    std::fflush(gOut);
}

DWORD WINAPI CoverageWriter(void *)
{
    for (;;) {
        Sleep(250);
        Drain();
    }
}

} // namespace

bool StartCoverage(uint8_t *base, uint32_t imageSize, const wchar_t *dir)
{
    wchar_t env[MAX_PATH];
    if (!GetEnvironmentVariableW(L"TS2HOST_COVERAGE", env, MAX_PATH)) return false;
    std::wstring listPath = env;
    if (listPath == L"1") listPath = std::wstring(dir) + L"ts2coverage-sites.txt";
    FILE *list = _wfsopen(listPath.c_str(), L"r", _SH_DENYNO);
    if (!list) {
        Log("coverage: cannot read the site list %ls; coverage off", listPath.c_str());
        return false;
    }
    // The list: "<rva> <byte>" per line (hex), '#' comments; tools/coverage.py sites.
    static std::vector<coverage::Site> sites;
    char line[128];
    while (std::fgets(line, sizeof(line), list)) {
        unsigned rva = 0, byte = 0;
        if (line[0] == '#' || sscanf_s(line, "%x %x", &rva, &byte) != 2) continue;
        coverage::Site s{};
        s.rva = rva;
        s.original = static_cast<uint8_t>(byte);
        sites.push_back(s);
    }
    std::fclose(list);
    // Room for every site in round 0 and 15 more rounds; past that, records are counted as dropped.
    const size_t capacity = sites.size() * 16 + 4096;
    auto *hits = static_cast<coverage::Hit *>(VirtualAlloc(nullptr, capacity * sizeof(coverage::Hit), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    gOut = _wfsopen((std::wstring(dir) + L"ts2coverage.txt").c_str(), L"w", _SH_DENYNO);
    if (!hits || !gOut) {
        Log("coverage: cannot allocate the records or open ts2coverage.txt; coverage off");
        return false;
    }
    gBase = reinterpret_cast<uintptr_t>(base);
    gSize = imageSize;
    gRecorder.Init(base, imageSize, sites.data(), sites.size(), hits, capacity);
    gLabels.push_back("start");
    AddVectoredExceptionHandler(1, &CoverageHandler); // first in line, ahead of the fault log
    const coverage::ArmCounts counts = gRecorder.Arm();
    std::fprintf(gOut, "# ts2coverage 1: first calls of original functions (src/hook/coverage.cpp has the format)\n");
    std::fprintf(gOut, "# sites %zu from %ls: armed %u, ours %u, mismatched %u\n", sites.size(), listPath.c_str(), counts.armed,
                 counts.ours, counts.mismatched);
    int shown = 0;
    for (size_t i = 0; i < gRecorder.SiteCount(); ++i) {
        const coverage::Site &s = gRecorder.Sites()[i];
        if (s.state == coverage::kOurs) {
            std::fprintf(gOut, "O %x\n", s.rva);
        } else if (s.state == coverage::kSkipped) {
            const uint8_t found = s.rva < imageSize ? base[s.rva] : 0;
            std::fprintf(gOut, "M %x %02x %02x\n", s.rva, s.original, found);
            if (shown++ < 10) Log("coverage: site 0x%x left alone: expected %02x, found %02x", s.rva, s.original, found);
        }
    }
    std::fprintf(gOut, "R 0 0 start\n");
    std::fflush(gOut);
    Log("coverage: %zu sites from %ls: %u armed, %u ours (hooked, trapped or detoured), %u mismatched; writing ts2coverage.txt",
        sites.size(), listPath.c_str(), counts.armed, counts.ours, counts.mismatched);
    if (HANDLE t = CreateThread(nullptr, 0, &CoverageWriter, nullptr, 0, nullptr)) CloseHandle(t);
    gOn = true;
    return true;
}

bool CoverageRound(const char *label)
{
    if (!gOn) return false;
    {
        std::lock_guard<std::mutex> lock(gWriteLock);
        gLabels.push_back(label); // index = the round Rearm starts (rounds are started from one thread, the control's)
    }
    const uint32_t n = gRecorder.Rearm();
    Log("coverage: round %ld (%s): %u sites re-armed", gRecorder.Round(), label, n);
    Drain();
    return true;
}
