// pixelformat_check: the nGZGraphic4 pixel formats running with no game image (docs/roadmap.md, milestone A).
//
// Builds our manager over our own 56 static formats (src/gz/PixelFormat*.cpp: our classes, our vtables,
// our static objects and table), runs the exercise in pixel_format_report.h through the objects' vtables,
// and compares the text it produces with the reference file generated from the original's objects by the
// difftest (tests/pixel_format_report.cpp, tests/fixtures/pixelformat_report.txt).
//
// This program does not link the harness, does not map or open the original image, and has no way to
// reach one: GameAddress (src/game/game.cpp) is not linked, so any pixel format code that still needed
// the image would fail to link. The only file it reads is the reference.
//
//   pixelformat_check [reference file] [--print]
//     reference file: defaults to the committed fixture in the source tree this was built from.
//     --print:        also writes the report to stdout.
// Exit status: 0 when the report equals the reference, 1 on a mismatch, 2 when the reference can't be read.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <vector>

#include "gz/PixelFormatsManager.h"
#include "pixel_format_report.h"

#ifndef PIXELFORMAT_REFERENCE
#define PIXELFORMAT_REFERENCE "pixelformat_report.txt"
#endif

namespace {

std::vector<std::string> Lines(const std::string &text)
{
    std::vector<std::string> lines;
    std::string current;
    for (char c : text) {
        if (c == '\r') continue; // the reference may have been checked out with CRLF
        if (c == '\n') {
            lines.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) lines.push_back(current);
    return lines;
}

} // namespace

int main(int argc, char **argv)
{
    std::string referencePath = PIXELFORMAT_REFERENCE;
    bool print = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--print") == 0) print = true;
        else referencePath = argv[i];
    }

    // The manager, on raw storage as the game's constructor sees it.
    alignas(8) unsigned char storage[nGZGraphic4::cPixelFormatsManagerDX::kSize];
    auto *manager = reinterpret_cast<nGZGraphic4::cPixelFormatsManagerDX *>(storage);
    manager->Construct();
    std::string report = pfreport::BuildReport(manager);
    manager->Destruct();

    if (print) std::fputs(report.c_str(), stdout);

    std::ifstream in(referencePath, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "pixelformat_check: cannot read the reference file %s\n", referencePath.c_str());
        return 2;
    }
    std::string reference((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    std::vector<std::string> ours = Lines(report), theirs = Lines(reference);
    size_t shown = 0, mismatches = 0;
    for (size_t i = 0; i < ours.size() || i < theirs.size(); ++i) {
        const std::string a = i < theirs.size() ? theirs[i] : "<missing>";
        const std::string b = i < ours.size() ? ours[i] : "<missing>";
        if (a == b) continue;
        ++mismatches;
        if (shown++ < 5) std::fprintf(stderr, "line %zu differs\n  reference: %s\n  ours:      %s\n", i + 1, a.c_str(), b.c_str());
    }
    if (mismatches) {
        std::fprintf(stderr, "pixelformat_check: FAILED, %zu of %zu lines differ from %s\n", mismatches, theirs.size(), referencePath.c_str());
        return 1;
    }
    std::printf("pixelformat_check: OK, %zu lines match %s (%d formats, no game image involved)\n", ours.size(), referencePath.c_str(), pfreport::kIdCount);
    return 0;
}
