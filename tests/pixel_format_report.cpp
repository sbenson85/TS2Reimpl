// The reference for the standalone pixel format check (standalone/pixelformat_check.cpp).
//
// The exercise in standalone/pixel_format_report.h is run on the ORIGINAL's manager and formats (the
// image's own objects and vtables, built by the image's static initialisers and the original manager
// constructor); its text must equal the committed reference file tests/fixtures/pixelformat_report.txt.
// pixelformat_check.exe, which has no original image, produces the same text from our code and compares it
// with that file, so the original remains the reference: if the file is stale (or hand-edited) this test
// fails. The same exercise on our manager must also give the original's text, in this process.
//
// To regenerate the reference after an intentional change of the exercise (never to make a failing
// comparison pass): run with the environment variable TS2_UPDATE_FIXTURES=1; the file is rewritten from the
// original's output and the test reports it.
#include "difftest.h"
#include "gz/PixelFormatStore.h"
#include "gz/PixelFormatsManager.h"
#include "pixel_format_fixture.h"
#include "pixel_format_vtables.h"
#include "../standalone/pixel_format_report.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace nGZGraphic4;

namespace {

struct StoreScope {
    PixelFormatStoreProvider saved;
    explicit StoreScope(PixelFormatStoreProvider p) : saved(CurrentPixelFormatStoreProvider()) { SetPixelFormatStoreProvider(p); }
    ~StoreScope() { SetPixelFormatStoreProvider(saved); }
};

std::vector<std::string> Lines(const std::string &text)
{
    std::vector<std::string> lines;
    std::string current;
    for (char c : text) {
        if (c == '\r') continue;
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

std::string FirstDifference(const std::string &a, const std::string &b)
{
    std::vector<std::string> la = Lines(a), lb = Lines(b);
    for (size_t i = 0; i < la.size() || i < lb.size(); ++i) {
        std::string x = i < la.size() ? la[i] : "<missing>", y = i < lb.size() ? lb[i] : "<missing>";
        if (x != y) return "line " + std::to_string(i + 1) + ": \"" + x.substr(0, 150) + "\" vs \"" + y.substr(0, 150) + "\"";
    }
    return "";
}

DIFF_TEST(PixelFormatReport_Reference, 0x15fc70)
{
    pftest::EnsureStatics(ctx);

    // The id list of the exercise is the manager's: every id resolves to a format in the original, in
    // table order, and none to the unknown format.
    std::vector<cPixelFormat *> table = pftest::OriginalTable(ctx);
    for (int i = 0; i < pfreport::kIdCount; ++i) {
        ++ctx.cases;
        uint32_t id;
        std::memcpy(&id, reinterpret_cast<const uint8_t *>(table[i]) + 4, 4);
        if (id != pfreport::kIds[i] || id != kStaticFormatSpecs[i].id) ctx.Fail("exercise id %d is %08x, the image's table entry has %08x, kStaticFormatSpecs %08x", i, pfreport::kIds[i], id, kStaticFormatSpecs[i].id);
    }

    // The original manager over the image's formats.
    uint8_t *orig = static_cast<uint8_t *>(::operator new(cPixelFormatsManagerDX::kSize));
    std::memset(orig, 0, cPixelFormatsManagerDX::kSize);
    ctx.Original<void *(__thiscall *)(void *)>(0x15d4b0)(orig);
    std::string original = pfreport::BuildReport(orig);
    reinterpret_cast<cPixelFormatsManagerDX *>(orig)->Destruct();
    ::operator delete(orig);

    // Ours, in this process, over our own formats and vtables.
    std::string oursText;
    {
        StoreScope scope(&StandaloneStore);
        uint8_t *mine = static_cast<uint8_t *>(::operator new(cPixelFormatsManagerDX::kSize));
        std::memset(mine, 0, cPixelFormatsManagerDX::kSize);
        reinterpret_cast<cPixelFormatsManagerDX *>(mine)->Construct();
        oursText = pfreport::BuildReport(mine);
        reinterpret_cast<cPixelFormatsManagerDX *>(mine)->Destruct();
        ::operator delete(mine);
    }
    ++ctx.cases;
    if (original != oursText) ctx.Fail("the exercise differs between the original's objects and ours: %s", FirstDifference(original, oursText).c_str());

    // The committed reference is the original's text.
    std::filesystem::path fixture = std::filesystem::path(__FILE__).parent_path() / "fixtures" / "pixelformat_report.txt";
    const char *update = std::getenv("TS2_UPDATE_FIXTURES");
    if (update && update[0] == '1') {
        std::filesystem::create_directories(fixture.parent_path());
        std::ofstream out(fixture, std::ios::binary | std::ios::trunc);
        out << original;
        ctx.Fail("TS2_UPDATE_FIXTURES=1: rewrote %s from the original (%zu bytes); rerun without the variable", fixture.string().c_str(), original.size());
        return;
    }
    std::ifstream in(fixture, std::ios::binary);
    ++ctx.cases;
    if (!in) {
        ctx.Fail("cannot read the committed reference %s", fixture.string().c_str());
        return;
    }
    std::string committed((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (Lines(committed) != Lines(original)) ctx.Fail("tests/fixtures/pixelformat_report.txt is not what the original produces: %s", FirstDifference(committed, original).c_str());
    // The first line carries the format version the standalone check expects.
    ++ctx.cases;
    std::string firstLine = Lines(committed).empty() ? "" : Lines(committed)[0];
    if (firstLine != "pixelformat-report " + std::to_string(pfreport::kReportVersion)) ctx.Fail("reference version line is \"%s\"", firstLine.c_str());
}

} // namespace
