// Differential tests for the rest of cBaseParser's run path (lane IH; src/gz/cParserRun.cpp): running a text
// (cIBaseParser slot 14, 0xb046b0), a file (slot 12, 0xb04490) and the files a wildcard names (slot 13, 0xb04940; the
// directory scan replaced by stand-ins), registering the built-ins (slot 0, 0xb02880),
// setting a variable (primary slot 1, 0xb06450) and the variable table's insert (0xafd050), and cExpressionFunction's
// constructor body (0xaf7840).
//
// The parsers are built by the original constructor and Reset (tests/gz_base_parser_slots.h). The face slots each
// body calls through the parser's vtable (15 from slot 14; 14 from slot 12; 12 from slot 13; 4, 7, 8 and 24 from slot 0) are replaced,
// in a copy of the face vtable both sides use, by loggers: each test isolates its own body. The game's operator new /
// delete are ours on both sides, the allocator service (the file data vector) is a counting fake.
#define NOMINMAX
#include "code_patch.h"
#include "free_scope.h"
#include "gz_base_parser_slots.h"

#include "app/IniFile.h"
#include "gz/CommandParser.h"
#include "gz/cGZDBSegmentPackedFile.h"
#include "rz/DirectoryScan.h"

#include <malloc.h>

#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace hu;
namespace cp = nGZCommandParser;

namespace {

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

struct Allocators {
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
};

// The allocator service the file data vector uses (cache 0x10c95d4, guard 0x10c95d8 as after its first use): slot 3
// allocates, slot 4 frees; the live count is checked after each side.
int gServiceLive = 0;
uint32_t __fastcall ServiceNop(void *, void *) { return 0; }
void *__fastcall ServiceAlloc(void *, void *, uint32_t n)
{
    ++gServiceLive;
    return std::malloc(n ? n : 1);
}
void __fastcall ServiceFree(void *, void *, void *p)
{
    if (p) --gServiceLive;
    std::free(p);
}
void *gServiceVTable[5] = {reinterpret_cast<void *>(&ServiceNop), reinterpret_cast<void *>(&ServiceNop), reinterpret_cast<void *>(&ServiceNop),
                           reinterpret_cast<void *>(&ServiceAlloc), reinterpret_cast<void *>(&ServiceFree)};
void *gServiceObject = gServiceVTable;

struct ServiceFake {
    void **cache;
    int32_t *guard;
    void *savedCache;
    int32_t savedGuard;
    explicit ServiceFake(difftest::Context &ctx)
        : cache(ctx.image.At<void *>(0x10c95d4)), guard(ctx.image.At<int32_t>(0x10c95d8)), savedCache(*cache), savedGuard(*guard)
    {
        *cache = &gServiceObject;
        *guard = INT32_MIN;
    }
    ~ServiceFake()
    {
        *cache = savedCache;
        *guard = savedGuard;
    }
};

// A copy of the parser's face vtable (cIBaseParser, 51 slots) with some slots replaced; both sides' parsers point at
// the same copy.
struct FaceVTable {
    void *slots[51];
    explicit FaceVTable(difftest::Context &ctx)
    {
        ParserBlock *probe = NewParser(ctx, false);
        std::memcpy(slots, *reinterpret_cast<void **>(probe->Face()), sizeof(slots));
        FreeParser(ctx, probe);
    }
    void Install(ParserBlock *p) { *reinterpret_cast<void **>(p->Face()) = slots; }
};

std::string Text(const char *s) { return s ? std::string(s) : std::string("(null)"); }

// An error that is a cIGZExceptionBase but not a cError.
const char *__fastcall OtherErrorText(const void *, void *) { return "other failure"; }
void *gOtherErrorVTable[1] = {reinterpret_cast<void *>(&OtherErrorText)};
struct OtherError : cIGZExceptionBase {
    uint32_t extra = 7;
};

// ---- slot 14: what the stand-in slot 15 does with each line ----

enum LineAction { kNothing, kThrowError, kSetTextAndThrow, kThrowOther, kOpenBlock, kOpenMeta, kCloseBlock, kCloseMeta, kActionCount };
std::vector<int> gLineActions;
int gLineThrows = 0; // lines that threw this side (the count checks after the text run only if none did or errors are ignored)
size_t gLineIndex = 0;

void __fastcall FakeRunLine(void *face, void *, const char *text)
{
    cp::cBaseParser *p = cp::cBaseParser::FromParserFace(face);
    gCalls.push_back({20, 15, "'" + std::string(text) + "' line " + std::to_string(p->mLine) + " file '" + p->mFileName + "'"});
    const int action = gLineActions.empty() ? kNothing : gLineActions[gLineIndex++ % gLineActions.size()];
    if (action == kThrowError || action == kSetTextAndThrow || action == kThrowOther) ++gLineThrows;
    switch (action) {
    case kThrowError: cp::ThrowFormatted("line '%s' failed", text);
    case kSetTextAndThrow:
        p->mText.mString.assign("text of the failure in '");
        p->mText.mString.append(text);
        p->mText.mString.append("' (100%%)");
        cp::ThrowFormatted("set and thrown");
    case kThrowOther: {
        OtherError error;
        error.mVTable = gOtherErrorVTable;
        throw error;
    }
    case kOpenBlock: p->mOpenBlocks.push_back(reinterpret_cast<void *>(0x1234)); break;
    case kOpenMeta: p->mMetaBlocks.push_back(reinterpret_cast<void *>(0x5678)); break;
    case kCloseBlock:
        if (!p->mOpenBlocks.empty()) p->mOpenBlocks.pop_back();
        break;
    case kCloseMeta:
        if (!p->mMetaBlocks.empty()) p->mMetaBlocks.pop_back();
        break;
    default: break;
    }
}

std::string MakeText(std::mt19937 &g)
{
    static const char *const kLines[] = {"", "echo a", "set x 1", "  indented", "# comment", "a%%b", "x"};
    static const char *const kBreaks[] = {"\n", "\r", "\r\n", "\n\r", "\r\r", "\n\n", "\r\n\r\n"};
    std::string t;
    for (unsigned i = 0, n = g() % 6; i < n; ++i) {
        t += kLines[g() % std::size(kLines)];
        if (g() % 3 == 0) t += std::string(g() % 30, 'w');
        t += kBreaks[g() % std::size(kBreaks)];
    }
    if (g() % 2) t += kLines[g() % std::size(kLines)];
    return t;
}

} // namespace

// Slot 14 (0xb046b0, ecx = the face, ret 0xc): texts of 0..6 lines with every kind of break, run through a stand-in
// slot 15 that logs each line with the line number and file name the parser holds, and per line does nothing, throws
// a cError (with or without setting the parser's text first), throws another error, or opens or closes a block or a
// meta block; blocks and meta blocks may be open before the text, so texts end with either kind left open or closed
// too often (each direction checked to occur). Both settings of "ignore errors" (+0xe8) and "already prefixed"
// (+0x198), file names short, long and with "%%". Compared: the outcome (the rethrown text), the lines run, the text buffer as each side left it, the Form.
DIFF_TEST(cBaseParser_RunText, 0xb046b0)
{
    Allocators allocators;
    FaceVTable vt(ctx);
    vt.slots[15] = reinterpret_cast<void *>(&FakeRunLine);
    std::mt19937 g(0xb046b0);
    static const char *const kFiles[] = {"a.txt", "", "dir/config_with_a_long_name.cfg", "x%%.ini"};
    static const char *const kOldNames[] = {"", "previous.txt", "a previous file name past fifteen"};
    int blocksLeftOpen = 0, blocksOverClosed = 0, metasLeftOpen = 0, metasOverClosed = 0;
    for (int round = 0; round < 600 && ctx.failures.size() < 6; ++round) {
        const std::string text = MakeText(g);
        const char *file = kFiles[g() % std::size(kFiles)];
        const char *oldName = kOldNames[g() % std::size(kOldNames)];
        std::vector<int> actions;
        for (int i = 0, n = 1 + g() % 4; i < n; ++i) actions.push_back(g() % 3 == 0 ? static_cast<int>(g() % kActionCount) : kNothing);
        const uint8_t ignoreErrors = static_cast<uint8_t>(g() % 2);
        const uint8_t prefixed = static_cast<uint8_t>(g() % 4 == 0);
        // Blocks and meta blocks already open before the text, so a text can also close more than it opens.
        const int preOpenBlocks = g() % 3 == 0 ? 1 + static_cast<int>(g() % 2) : 0;
        const int preOpenMetas = g() % 3 == 0 ? 1 + static_cast<int>(g() % 2) : 0;
        if (preOpenBlocks && g() % 2) actions.push_back(kCloseBlock);
        if (preOpenMetas && g() % 2) actions.push_back(kCloseMeta);
        char what[200];
        std::snprintf(what, sizeof(what), "RunText round %d file '%s' ignore %u prefixed %u", round, file, ignoreErrors, prefixed);
        auto setup = [&](Side &s) {
            vt.Install(s.parser);
            cp::cBaseParser *p = s.parser->P();
            p->mFileName.assign(oldName);
            p->mFlagE8 = ignoreErrors;
            p->mField198 = prefixed;
            p->mLine = 77;
            p->mText.mString.assign("old text");
            for (int i = 0; i < preOpenBlocks; ++i) p->mOpenBlocks.push_back(reinterpret_cast<void *>(0x99));
            for (int i = 0; i < preOpenMetas; ++i) p->mMetaBlocks.push_back(reinterpret_cast<void *>(0x98));
        };
        auto run = [&](Side &s, bool orig) {
            std::vector<char> buffer(text.begin(), text.end());
            buffer.push_back(0);
            buffer.push_back('Z'); // past the terminator: must stay
            gLineActions = actions;
            gLineIndex = 0;
            gLineThrows = 0;
            const void *fn = orig ? ctx.image.At(0xb046b0) : reinterpret_cast<const void *>(&cp::cBaseParserAdapters::RunText);
            // How the counts ended on the original's side, for the coverage check below (only when the text ran to
            // the count checks: no line threw, or errors are ignored).
            auto tally = [&] {
                if (!orig || (gLineThrows != 0 && !ignoreErrors)) return;
                cp::cBaseParser *p = s.parser->P();
                const int blocks = static_cast<int>(p->mOpenBlocks.size()) - preOpenBlocks;
                const int metas = static_cast<int>(p->mMetaBlocks.size()) - preOpenMetas;
                if (blocks > 0) ++blocksLeftOpen;
                if (blocks < 0) ++blocksOverClosed;
                if (blocks == 0 && metas > 0) ++metasLeftOpen;
                if (blocks == 0 && metas < 0) ++metasOverClosed;
            };
            try {
                reinterpret_cast<void(__fastcall *)(void *, void *, char *, uint32_t, const char *)>(const_cast<void *>(fn))(
                    s.parser->Face(), nullptr, buffer.data(), static_cast<uint32_t>(text.size()), file);
            } catch (...) {
                tally();
                throw;
            }
            tally();
            s.extra = "buffer '";
            for (char c : buffer) s.extra += c ? c : '0';
            s.extra += "'\n";
            return 0u;
        };
        Compare(ctx, what, setup, [&](Side &s) { return run(s, true); }, [&](Side &s) { return run(s, false); });
    }
    // Both count checks, both directions each (a meta case only counts when the blocks balance: the block check
    // comes first).
    if (blocksLeftOpen < 20 || blocksOverClosed < 20 || metasLeftOpen < 20 || metasOverClosed < 20)
        ctx.Fail("test too weak: texts ending with blocks left open %d, over-closed %d; meta blocks left open %d, over-closed %d",
                 blocksLeftOpen, blocksOverClosed, metasLeftOpen, metasOverClosed);
}

namespace {

// ---- slot 12: the source and the stand-in slot 14 ----

uint32_t gAccepts = 1;        // source slot 3's al (0x100: false in al)
uint32_t gReads = 1;          // source slot 5's al
bool gReadFills = true;       // slot 5 fills the vector (also when it then answers false)
std::string gFileText;        // what slot 5 puts in the vector (a 0 is appended)
std::string gResolved;        // what slot 6 writes to its out string
bool gRunTextThrows = false;

uint32_t __fastcall SourceAccepts(void *, void *, const char *path)
{
    gCalls.push_back({7, 3, Text(path)});
    return gAccepts;
}
using FileData = std::vector<char, GZServiceAllocator<char>>;
uint32_t __fastcall SourceRead(void *, void *, const char *path, FileData *out)
{
    gCalls.push_back({7, 5, Text(path) + " size " + std::to_string(out->size())});
    if (gReadFills) {
        out->assign(gFileText.begin(), gFileText.end());
        out->push_back(0);
    }
    return gReads;
}
void __fastcall SourceResolve(void *, void *, const char *path, const char *current, cRZString *out)
{
    gCalls.push_back({7, 6, Text(path) + " from '" + Text(current) + "' out '" + out->mString + "'"});
    out->mString.assign(gResolved);
}
void *gSourceVTable[8] = {
    reinterpret_cast<void *>(&ServiceNop), reinterpret_cast<void *>(&ServiceNop), reinterpret_cast<void *>(&ServiceNop),
    reinterpret_cast<void *>(&SourceAccepts), reinterpret_cast<void *>(&ServiceNop), reinterpret_cast<void *>(&SourceRead),
    reinterpret_cast<void *>(&SourceResolve), reinterpret_cast<void *>(&ServiceNop),
};
void *gSourceObject[3] = {gSourceVTable, nullptr, nullptr};

void __fastcall FakeRunText(void *face, void *, char *text, uint32_t size, const char *fileName)
{
    std::string s = "size " + std::to_string(static_cast<int32_t>(size)) + " file '" + Text(fileName) + "' text ";
    if (!text) s += "(null)";
    else s += "'" + std::string(text, size) + "' end " + std::to_string(static_cast<unsigned char>(text[size]));
    (void)face;
    gCalls.push_back({21, 14, s});
    if (gRunTextThrows) cp::ThrowFormatted("text of '%s' failed", fileName);
}

} // namespace

// Slot 12 (0xb04490, ecx = the face, ret 8): paths short, long and with "%%", flags 0..3 (and high bits), a source
// that resolves (slot 6) to short, long or empty names, accepts or not (slot 3, al only), reads or not (slot 5, filling
// the vector or not, empty files too), and a slot 14 that may throw. Compared: the outcome, the source and slot 14
// calls (texts by content), the Form; the allocator service's blocks must all be freed on both sides.
DIFF_TEST(cBaseParser_RunFile, 0xb04490)
{
    Allocators allocators;
    ServiceFake service(ctx);
    FaceVTable vt(ctx);
    vt.slots[14] = reinterpret_cast<void *>(&FakeRunText);
    std::mt19937 g(0xb04490);
    static const char *const kPaths[] = {"a.txt", "dir/sub/a_long_file_name.cfg", "", "50%%.txt"};
    static const char *const kResolved[] = {"res.txt", "", "resolved/to/a/path/longer/than/fifteen.txt"};
    static const char *const kTexts[] = {"", "one line", "a\nb\r\nc", "x%%y"};
    static const uint32_t kAccepts[] = {1, 0, 0x100, 0xff01};
    for (int round = 0; round < 500 && ctx.failures.size() < 6; ++round) {
        const char *path = kPaths[g() % std::size(kPaths)];
        const uint32_t flags = g() % 4 | (g() % 5 == 0 ? 0xf0u : 0u);
        gResolved = kResolved[g() % std::size(kResolved)];
        gAccepts = g() % 4 == 0 ? kAccepts[1 + g() % 2] : kAccepts[g() % 2 == 0 ? 0 : 3];
        gReads = g() % 4 == 0 ? (g() % 2 ? 0u : 0x100u) : 1u;
        gReadFills = g() % 5 != 0;
        gFileText = kTexts[g() % std::size(kTexts)];
        if (g() % 6 == 0) gFileText += std::string(5000, 'L');
        gRunTextThrows = g() % 5 == 0;
        char what[200];
        std::snprintf(what, sizeof(what), "RunFile '%s' flags %x accepts %x reads %x fills %d throws %d", path, flags, gAccepts, gReads,
                      gReadFills, gRunTextThrows);
        void *savedSource[2] = {};
        auto setup = [&](Side &s) {
            vt.Install(s.parser);
            s.parser->P()->mFileName.assign("cur.txt");
            s.parser->P()->mText.mString.assign("old text");
        };
        int side = 0;
        // The outcome is captured inside (the parser's own source must be put back either way: its destructor releases
        // it) and compared through extra.
        auto runCapturing = [&](Side &s, bool orig) {
            cp::cBaseParser *p = s.parser->P();
            savedSource[side] = p->mSource;
            p->mSource = reinterpret_cast<cp::cFileSource *>(gSourceObject);
            gServiceLive = 0;
            const void *fn = orig ? ctx.image.At(0xb04490) : reinterpret_cast<const void *>(&cp::cBaseParserAdapters::RunFile);
            const cptest::Outcome o = RunGuarded([&] {
                reinterpret_cast<void(__fastcall *)(void *, void *, const char *, uint32_t)>(const_cast<void *>(fn))(s.parser->Face(), nullptr, path, flags);
                return 0u;
            });
            p->mSource = static_cast<cp::cFileSource *>(savedSource[side++]);
            char b[64];
            std::snprintf(b, sizeof(b), "threw %d ", o.threw);
            s.extra = b + o.detail + "\nservice blocks left " + std::to_string(gServiceLive) + "\n";
            return 0u;
        };
        Compare(ctx, what, setup, [&](Side &s) { return runCapturing(s, true); }, [&](Side &s) { return runCapturing(s, false); });
    }
}

namespace {

// ---- slot 13: stand-ins for the directory scan (0x1c48da build, 0x1c63d4 find, 0x1c5bbd list names, 0x1c5bcb list
// full paths, 0x1c49eb destroy) and a stand-in slot 12 ----

// The game's std::list<cRZString> as the scan's list functions see it.
struct ListNode {
    ListNode *next;
    ListNode *prev;
    cRZString value;
};
struct ListHead {
    ListNode *head;
    uint32_t size;
};

constexpr uint32_t kScanMagic = 0x5ca7f00d;
std::vector<std::string> gScanNames; // what a list stand-in adds (after the directory for the full paths)
std::vector<void *> gNodes;          // the nodes the stand-ins allocated this side (each must be freed by it)
void *gListSentinel = nullptr;       // the list's sentinel node (must be freed too)

// Whether a path is canonical absolute (cRZFileName's constructor would not need the current directory).
struct LocalCheck {
    alignas(cRZString) unsigned char raw[sizeof(cRZString)];
    bool canonical;
    explicit LocalCheck(const char *text)
    {
        auto *s = reinterpret_cast<cRZString *>(raw);
        s->Construct(text);
        canonical = inifile::IsCanonicalAbsolutePathString(s);
        s->Destruct();
    }
};

std::string ScanId(const uint32_t *self) { return self[0] == kScanMagic ? "scan" : "not the scan"; }

void *__fastcall StandScanBuild(uint32_t *self, void *, const cRZString *dir, uint32_t a, uint32_t b, uint32_t c)
{
    gCalls.push_back({30, 0, "'" + dir->mString + "' " + std::to_string(a) + " " + std::to_string(b) + " " + std::to_string(c)});
    std::memset(self, 0, 0x5c);
    self[0] = kScanMagic;
    return self;
}
uint32_t __fastcall StandScanFind(uint32_t *self, void *, cRZString *pattern, uint32_t flag)
{
    gCalls.push_back({30, 1, ScanId(self) + " '" + pattern->mString + "' " + std::to_string(flag)});
    self[1] = 1;
    return 1;
}
void AddNodes(ListHead *list, const std::string &prefix)
{
    gListSentinel = list->head;
    for (size_t i = 0; i < gScanNames.size(); ++i) {
        auto *node = static_cast<ListNode *>(::operator new(sizeof(ListNode)));
        // Every third full path one directory further down.
        const std::string text = prefix.empty() || i % 3 != 2 ? prefix + gScanNames[i] : prefix + "deeper\\" + gScanNames[i];
        node->value.Construct(text.c_str());
        node->next = list->head;
        node->prev = list->head->prev;
        list->head->prev->next = node;
        list->head->prev = node;
        ++list->size;
        gNodes.push_back(node);
    }
}
void __fastcall StandListNames(uint32_t *self, void *, ListHead *list)
{
    gCalls.push_back({30, 2, ScanId(self) + " found " + std::to_string(self[1]) + " list size " + std::to_string(list->size) +
                                 (list->head->next == list->head ? " empty" : " not empty")});
    AddNodes(list, "");
}
void __fastcall StandListPaths(uint32_t *self, void *, const cRZString *dir, ListHead *list, uint32_t flag)
{
    gCalls.push_back({30, 3, ScanId(self) + " found " + std::to_string(self[1]) + " '" + dir->mString + "' list size " +
                                 std::to_string(list->size) + " flag " + std::to_string(flag)});
    AddNodes(list, dir->mString);
}
void __fastcall StandScanDestroy(uint32_t *self, void *)
{
    gCalls.push_back({30, 4, ScanId(self)});
    self[0] = 0xdead;
}

// Over the image's scan functions and ours (src/rz/DirectoryScan.h; the caller builds and destroys it through its adapters).
struct ScanStandIns {
    shape::StandIn build{0x1c48dau, &DirectoryScanAdapters::Construct, &StandScanBuild};
    shape::StandIn find{0x1c63d4u, &DirectoryScan::SetPattern, &StandScanFind};
    shape::StandIn names{0x1c5bbdu, &DirectoryScan::ListFiles, &StandListNames};
    shape::StandIn paths{0x1c5bcbu, &DirectoryScan::ListFilesBelow, &StandListPaths};
    shape::StandIn destroy{0x1c49ebu, &DirectoryScanAdapters::Destruct, &StandScanDestroy};
};

// What the stand-in slot 12 does with each file.
enum FileAction { kFileRuns, kFileThrowsError, kFileThrowsOther, kFileThrowsInt, kFileActionCount };
std::vector<int> gFileActions;
size_t gFileIndex = 0;

void __fastcall FakeRunFile(void *face, void *, const char *path, uint32_t flags)
{
    cp::cBaseParser *p = cp::cBaseParser::FromParserFace(face);
    char b[32];
    std::snprintf(b, sizeof(b), " flags %x", flags);
    gCalls.push_back({22, 12, "'" + Text(path) + "'" + b + " file '" + p->mFileName + "'"});
    const int action = gFileActions.empty() ? kFileRuns : gFileActions[gFileIndex++ % gFileActions.size()];
    switch (action) {
    case kFileThrowsError: cp::ThrowFormatted("cannot run '%s' (50%%%%)", path);
    case kFileThrowsOther: {
        OtherError error;
        error.mVTable = gOtherErrorVTable;
        throw error;
    }
    case kFileThrowsInt: throw 5;
    default: break;
    }
}

} // namespace

// Slot 13 (0xb04940, ecx = the face, ret 8): canonical absolute patterns (the file name constructor resolves anything
// else against the current directory, which the harness has not got) with wildcards, "%%" and long parts, flags 0..7
// (and high bits; bit 0 resolves through the fake source's slot 6 to another pattern), 0..5 files listed by name or
// full path, each run by a stand-in slot 12 that runs it, throws a cError, another cIGZExceptionBase or an int.
// Compared: the outcome (the collected errors' cError), the scan, list and slot 12 calls (texts by content, the scan
// object's identity), the Form; every list node the stand-ins allocated and the list's sentinel must be freed.
DIFF_TEST(cBaseParser_RunFiles, 0xb04940)
{
    Allocators allocators;
    ScanStandIns scan;
    FaceVTable vt(ctx);
    vt.slots[12] = reinterpret_cast<void *>(&FakeRunFile);
    std::mt19937 g(0xb04940);
    static const char *const kPatterns[] = {"C:\\cfg\\*.ini", "d:/Games/The Sims 2/Config/*.*", "C:\\only.txt", "\\\\srv\\share\\x?.cfg",
                                            "C:\\a%%b\\*.c", "C:\\a directory name longer than fifteen\\sub\\*.package", "C:\\"};
    static const char *const kNames[] = {"a.ini", "Long_Config_Name_Over_Fifteen.ini", "50%%.cfg", "x", ""};
    int canonical = 0, failures = 0, recursive = 0;
    for (int round = 0; round < 500 && ctx.failures.size() < 6; ++round) {
        const char *pattern = kPatterns[g() % std::size(kPatterns)];
        const uint32_t flags = g() % 8 | (g() % 5 == 0 ? 0xf0u : 0u);
        gResolved = kPatterns[g() % std::size(kPatterns)];
        {
            LocalCheck check(flags & 1 ? gResolved.c_str() : pattern);
            if (!check.canonical) continue;
        }
        ++canonical;
        if (flags & 4) ++recursive;
        gScanNames.clear();
        for (int i = 0, n = g() % 6; i < n; ++i) gScanNames.push_back(kNames[g() % std::size(kNames)]);
        std::vector<int> actions;
        for (int i = 0, n = 1 + g() % 4; i < n; ++i) actions.push_back(g() % 2 == 0 ? static_cast<int>(g() % kFileActionCount) : kFileRuns);
        for (size_t i = 0; i < gScanNames.size(); ++i)
            if (actions[i % actions.size()] != kFileRuns) {
                ++failures;
                break;
            }
        char what[200];
        std::snprintf(what, sizeof(what), "RunFiles '%s' flags %x resolved '%s' files %zu", pattern, flags, gResolved.c_str(), gScanNames.size());
        void *savedSource[2] = {};
        int side = 0;
        auto setup = [&](Side &s) {
            vt.Install(s.parser);
            s.parser->P()->mFileName.assign("current.cfg");
            s.parser->P()->mText.mString.assign("old text");
        };
        auto runCapturing = [&](Side &s, bool orig) {
            cp::cBaseParser *p = s.parser->P();
            savedSource[side] = p->mSource;
            p->mSource = reinterpret_cast<cp::cFileSource *>(gSourceObject);
            gFileActions = actions;
            gFileIndex = 0;
            gNodes.clear();
            gListSentinel = nullptr;
            const void *fn = orig ? ctx.image.At(0xb04940) : reinterpret_cast<const void *>(&cp::cBaseParserAdapters::RunFiles);
            difftest::BeginFrees();
            const cptest::Outcome o = RunGuarded([&] {
                reinterpret_cast<void(__fastcall *)(void *, void *, const char *, uint32_t)>(const_cast<void *>(fn))(s.parser->Face(), nullptr, pattern, flags);
                return 0u;
            });
            p->mSource = static_cast<cp::cFileSource *>(savedSource[side++]);
            std::string freed = "nodes freed";
            for (void *node : gNodes) freed += difftest::FreedSinceBegin(node) ? " y" : " n";
            freed += gListSentinel ? (difftest::FreedSinceBegin(gListSentinel) ? " sentinel y" : " sentinel n") : " no sentinel";
            char b[64];
            std::snprintf(b, sizeof(b), "threw %d ", o.threw);
            s.extra = b + o.detail + "\n" + freed + "\n";
            return 0u;
        };
        Compare(ctx, what, setup, [&](Side &s) { return runCapturing(s, true); }, [&](Side &s) { return runCapturing(s, false); });
    }
    if (canonical < 300 || failures < 60 || recursive < 100)
        ctx.Fail("test too weak: %d canonical cases, %d with a failing file, %d full-path listings", canonical, failures, recursive);
}

namespace {

// ---- slot 0: the stand-in registration slots ----

std::vector<void *> gRegistered; // what the stand-ins were handed (freed after each case)
int gRegisterIndex = 0;
int gThrowAt = -1;

std::string Words(const uint8_t *o, size_t from, size_t to)
{
    std::string s;
    for (size_t off = from; off < to; off += 4) s += Word(W(o, off)) + ",";
    return s;
}

// A registered object by what it holds. Words nobody writes before the object is registered are left out: the state
// word of the commands (+0xc; cCommand::Construct 0xaf7820 writes +0..+8, 0xb02880 then only the vtable) and of the
// functions (+0xc; 0xaf7840 writes +0..+8, 0xb02880 then only the two vtables), +8 of the meta commands (0xaf7860
// writes +0 and +4), and +0x3c.. of the block command (0xaf7750 does not write the flag at +0x3c; +0x40 is the state).
// The block command's table and pool by content (FormWriter), not by their heap addresses.
std::string Registered(const uint8_t *o)
{
    const size_t size = _msize(const_cast<uint8_t *>(o));
    std::string s = "size " + std::to_string(size) + " ";
    if (size == 0x44) {
        FormWriter f{{}, o};
        f.Table("table", o + 0x10, false);
        char b[48];
        std::snprintf(b, sizeof(b), " pool n%u used %x", W(o, 0x34), W(o, 0x38));
        s += Words(o, 0, 0x10) + f.out + b;
    } else if (size == 0xc) {
        s += Words(o, 0, 8);
    } else {
        s += Words(o, 0, 0xc);
    }
    return s;
}

void Registration(int slot, void *object)
{
    gCalls.push_back({30, slot, Registered(static_cast<const uint8_t *>(object))});
    gRegistered.push_back(object);
    if (gRegisterIndex++ == gThrowAt) cp::ThrowFormatted("registration %d refused", gThrowAt);
}
void __fastcall FakeRegisterCommand(void *, void *, void *command) { Registration(4, command); }
void __fastcall FakeRegisterFunction(void *, void *, void *face) { Registration(7, static_cast<uint8_t *>(face) - 4); }
void __fastcall FakeRestart(void *face, void *)
{
    const cp::cBaseParser *p = cp::cBaseParser::FromParserFace(face);
    gCalls.push_back({30, 8, "initialised " + std::to_string(p->mInitialised) + " case " + std::to_string(p->mCaseSensitive)});
}
void __fastcall FakeSetOutput(void *face, void *, void *output)
{
    const cp::cBaseParser *p = cp::cBaseParser::FromParserFace(face);
    gCalls.push_back({30, 24, Word(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(output))) + " case " + std::to_string(p->mCaseSensitive)});
}

void FreeRegistered()
{
    for (void *o : gRegistered) {
        if (_msize(o) == 0x44) reinterpret_cast<cp::cBlockCommandBase *>(o)->Destruct();
        ::operator delete(o);
    }
    gRegistered.clear();
}

} // namespace

// Slot 0 (0xb02880, ecx = the face, ret 4; al): on a parser not yet initialised and on one that is, with case flags 0,
// 1 and 0xff, and with the registration slot refusing (throwing at) the k-th object. The four face slots it calls are
// stand-ins that log the order, the case flag and initialised byte when the restart and output slots run, and every
// object handed over by content (size, vtables and the fields the constructors wrote). Compared: al, the calls and
// the Form.
DIFF_TEST(cBaseParser_Initialise, 0xb02880)
{
    Allocators allocators;
    FaceVTable vt(ctx);
    vt.slots[4] = reinterpret_cast<void *>(&FakeRegisterCommand);
    vt.slots[7] = reinterpret_cast<void *>(&FakeRegisterFunction);
    vt.slots[8] = reinterpret_cast<void *>(&FakeRestart);
    vt.slots[24] = reinterpret_cast<void *>(&FakeSetOutput);
    std::mt19937 g(0xb02880);
    for (int round = 0; round < 60 && ctx.failures.size() < 6; ++round) {
        const uint8_t initialised = round % 5 == 4 ? static_cast<uint8_t>(1 + g() % 2) : 0;
        static const uint8_t kCases[] = {0, 1, 0xff};
        const uint8_t caseFlag = kCases[g() % 3];
        const int throwAt = round % 3 == 2 ? static_cast<int>(g() % 44) : -1;
        char what[96];
        std::snprintf(what, sizeof(what), "Initialise initialised %u case %u refuse %d", initialised, caseFlag, throwAt);
        auto setup = [&](Side &s) {
            vt.Install(s.parser);
            s.parser->P()->mInitialised = initialised;
        };
        auto run = [&](Side &s, bool orig) {
            gRegisterIndex = 0;
            gThrowAt = throwAt;
            if (orig) // al (the original's eax above it is whatever it held), the argument's upper bytes not read
                return ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0xb02880)(s.parser->Face(), 0xabcd00u | caseFlag) & 0xff;
            using OursFn = bool(__fastcall *)(void *, void *, uint32_t); // the hook's entry gets the whole stack word
            return static_cast<uint32_t>(reinterpret_cast<OursFn>(&cp::cBaseParserAdapters::Initialise)(s.parser->Face(), nullptr, 0xabcd00u | caseFlag));
        };
        Compare(ctx, what, setup, [&](Side &s) { return run(s, true); }, [&](Side &s) { return run(s, false); });
        FreeRegistered();
    }
}

// 0xaf7840 (__thiscall, returns this): cExpressionFunction's constructor body on blocks of 0x10 random bytes.
DIFF_TEST(cExpressionFunction_Construct, 0xaf7840)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xaf7840);
    std::mt19937 g(0xaf7840);
    for (int i = 0; i < 50; ++i) {
        alignas(8) uint8_t a[0x18], b[0x18];
        for (auto &x : a) x = static_cast<uint8_t>(g());
        std::memcpy(b, a, sizeof(a));
        void *ra = orig(a);
        void *rb = reinterpret_cast<cp::cExpressionFunction *>(b)->Construct();
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("Construct %d: returns %p / %p", i, ra, rb);
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("Construct %d: bytes differ", i);
    }
}

namespace {

const char *const kVariableNames[] = {"x", "y", "a:x", "a:b:x", "a:b:c:z", "a_variable_name_longer_than_fifteen", "b:y", "x:", ":x", "a:"};
const char *const kVariableValues[] = {"", "1", "two words", "a value longer than fifteen characters", "50%"};

} // namespace

// Primary slot 1 (0xb06450, ecx = the whole parser, ret 8): sequences of 1..8 assignments (names with and without
// namespace paths, empty path parts and names, long names; values short, long and empty; names repeated, so values
// are replaced), sometimes 20 names into one namespace (the table grows), from the global namespace or inside one or
// two pushed ones, the trace (+0xea) on or off (its text through a fake output's slot 14). Compared: the outputs and
// the Form (every namespace's variables, children and pool).
DIFF_TEST(cBaseParser_SetVariable, 0xb06450)
{
    Allocators allocators;
    std::mt19937 g(0xb06450);
    using SetFn = void(__thiscall *)(void *, const char *, const char *);
    for (int round = 0; round < 400 && ctx.failures.size() < 6; ++round) {
        Fakes fakes;
        FakeObject *output = fakes.Add(8, "out");
        const uint8_t trace = static_cast<uint8_t>(g() % 3 == 0);
        const int pushes = static_cast<int>(g() % 3);
        std::vector<std::pair<std::string, std::string>> ops;
        if (g() % 8 == 0) {
            for (int i = 0; i < 20; ++i) ops.push_back({(g() % 2 ? "n:v" : "v") + std::to_string(g() % 24), kVariableValues[g() % std::size(kVariableValues)]});
        } else {
            for (int i = 0, n = 1 + static_cast<int>(g() % 8); i < n; ++i)
                ops.push_back({kVariableNames[g() % std::size(kVariableNames)], kVariableValues[g() % std::size(kVariableValues)]});
        }
        std::string what = "SetVariable round " + std::to_string(round) + " trace " + std::to_string(trace) + " pushes " + std::to_string(pushes) + ":";
        for (const auto &[n, v] : ops) what += " " + n + "=" + v;
        auto setup = [&](Side &s) {
            static const char *const kSpaces[] = {"outer", "inner"};
            for (int i = 0; i < pushes; ++i) CallFace<void>(ctx.image.At(0xb05770), s.parser->Face(), kSpaces[i]);
            s.parser->P()->mFieldEA = trace;
            s.parser->P()->mOutput = output;
        };
        auto run = [&](Side &s, bool orig) {
            for (const auto &[name, value] : ops) {
                // The name in a buffer scribbled over after the call: the table must hold its own copy (the pool's).
                std::vector<char> buffer(name.begin(), name.end());
                buffer.push_back(0);
                const cptest::Outcome o = RunGuarded([&] {
                    if (orig) ctx.Original<SetFn>(0xb06450)(s.parser->bytes, buffer.data(), value.c_str());
                    else cp::cBaseParserAdapters::SetVariable(s.parser->P(), nullptr, buffer.data(), value.c_str());
                    return 0u;
                });
                std::fill(buffer.begin(), buffer.end(), '#');
                char b[32];
                std::snprintf(b, sizeof(b), "threw %d ", o.threw);
                s.extra += b + o.detail + "\n";
            }
            s.parser->P()->mOutput = nullptr; // the fake is not counted
            return 0u;
        };
        Compare(ctx, what.c_str(), setup, [&](Side &s) { return run(s, true); }, [&](Side &s) { return run(s, false); });
    }
}

// 0xafd050 (__thiscall on a VariableTable, ret 8, returns out): 1..40 names (some repeated) put into the global
// namespace's variable table (past 8 the table grows, by 8 times up to 512 buckets): the node each insert returns (by
// its name and whether it is new) and the table (Form) compared. The names live in the test (the nodes point at them).
DIFF_TEST(cBaseParser_InsertVariable, 0xafd050)
{
    Allocators allocators;
    std::mt19937 g(0xafd050);
    using InsertFn = cp::VariableTable::InsertResult *(__thiscall *)(void *, cp::VariableTable::InsertResult *, const cp::KeyView *);
    std::vector<std::string> names;
    for (int i = 0; i < 700; ++i) names.push_back((i % 3 ? "name" : "n") + std::to_string(i * 7919 % 1000));
    names.push_back("");
    // The table's starting bucket count, set through the original CommandTable::Rehash (0xaf7da0): the growth rule
    // differs below and above 512 buckets, and between 8 times and the count needed.
    static const uint32_t kStartBuckets[] = {0, 0, 1, 2, 100, 256, 300, 600};
    for (int round = 0; round < 300 && ctx.failures.size() < 6; ++round) {
        std::vector<size_t> picks;
        const size_t spread = round % 4 == 0 ? names.size() : 12;
        for (int i = 0, n = 1 + static_cast<int>(g() % (round % 5 == 0 ? 1500 : 40)); i < n; ++i) picks.push_back(g() % spread);
        const uint32_t start = kStartBuckets[g() % std::size(kStartBuckets)];
        const std::string what = "InsertVariable round " + std::to_string(round) + " (" + std::to_string(picks.size()) + " inserts, start " +
                                 std::to_string(start) + ")";
        auto setup = [&](Side &s) {
            if (start) ctx.Original<void(__thiscall *)(void *, uint32_t)>(0xaf7da0)(s.parser->bytes + 0x78, start);
        };
        auto run = [&](Side &s, bool orig) {
            auto *table = reinterpret_cast<cp::VariableTable *>(s.parser->bytes + 0x78);
            for (size_t k : picks) {
                const cp::KeyView key{names[k].c_str(), static_cast<uint32_t>(names[k].size())};
                cp::VariableTable::InsertResult out{nullptr, 0xcc};
                cp::VariableTable::InsertResult *r = orig ? ctx.Original<InsertFn>(0xafd050)(table, &out, &key)
                                                          : cp::cBaseParserAdapters::InsertVariable(table, nullptr, &out, &key);
                s.extra += (r == &out ? "" : "wrong return ") + std::string(out.node->key, out.node->keyLength) + (out.inserted ? "+" : "=") +
                           (out.node->key == key.ptr ? "" : "(key copied)") + " '" + out.node->value + "';";
                if (out.inserted) out.node->value.assign(names[k]); // a value, so the Form shows the nodes apart
            }
            return 0u;
        };
        Compare(ctx, what.c_str(), setup, [&](Side &s) { return run(s, true); }, [&](Side &s) { return run(s, false); });
    }
}
