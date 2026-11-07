// Differential tests for the path helpers of src/app/AppServices2.*: the string conversions 0x1b0356 and 0x1c53c2,
// the file search 0x1c54dd with its close (0x1c54b2, 0x1c49cb), the directory checks 0x1c4f79 / 0x1c4f94 and
// the three cGZFileSystem directory setters (0xc76611, 0xc7664e, 0xc766a7).
//
// The search goes through the import slots of FindFirstFileA / FindClose (and the game's optional
// FindFirstFileW pointer), pointed at scripted fakes for the life of a guard; our code reads the same slots.
// Strings are cRZString objects carrying the original vtable.
#define NOMINMAX
#include "app/AppServices.h"
#include "app/AppServices2.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t kConvertFrom = 0x1b0356, kToUtf16 = 0x1c53c2, kFindFirst = 0x1c54dd, kDestructNode = 0x1c49cb, kCloseFind = 0x1c54b2;
constexpr uint32_t kPatternMatches = 0x1c4f79, kDirectoryExists = 0x1c4f94;
constexpr uint32_t kSetApp = 0xc76611, kSetData = 0xc7664e, kSetPlugIn = 0xc766a7;
constexpr uint32_t kIatFindFirstFileA = 0xd9a1f8, kIatFindClose = 0xd9a1f4, kFindFirstFileWPointer = 0x10cb618, kWildcardPointer = 0xff2478;

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

// Points slots of the image at fakes; restores them on destruction.
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

// The script of the fake search.
struct Search {
    bool found = true;
    std::string name = "file.txt";       // ANSI bytes of the found name
    std::u16string wideName = u"file.txt";
    DWORD attributes = 0;
    HANDLE handle = reinterpret_cast<HANDLE>(0x4242);
} gSearch;

std::string Hex(const void *data, size_t bytes)
{
    std::string out;
    char buf[8];
    for (size_t i = 0; i < bytes; ++i) {
        std::snprintf(buf, sizeof(buf), "%02x", static_cast<const unsigned char *>(data)[i]);
        out += buf;
    }
    return out;
}

HANDLE WINAPI FakeFindFirstA(const char *pattern, WIN32_FIND_DATAA *data)
{
    Log("FindFirstFileA(%s)", pattern);
    if (!gSearch.found) return INVALID_HANDLE_VALUE;
    std::memset(data, 0xa5, sizeof(*data));
    data->dwFileAttributes = gSearch.attributes;
    std::memset(data->cFileName, 0, sizeof(data->cFileName));
    std::memcpy(data->cFileName, gSearch.name.c_str(), gSearch.name.size());
    return gSearch.handle;
}

HANDLE WINAPI FakeFindFirstW(const wchar_t *pattern, WIN32_FIND_DATAW *data)
{
    size_t n = std::wcslen(pattern);
    Log("FindFirstFileW(%zu: %s)", n, Hex(pattern, n * 2).c_str());
    if (!gSearch.found) return INVALID_HANDLE_VALUE;
    std::memset(data, 0xa5, sizeof(*data));
    data->dwFileAttributes = gSearch.attributes;
    std::memset(data->cFileName, 0, sizeof(data->cFileName));
    std::memcpy(data->cFileName, gSearch.wideName.c_str(), gSearch.wideName.size() * 2);
    return gSearch.handle;
}

BOOL WINAPI FakeFindClose(HANDLE handle)
{
    Log("FindClose(%p)", handle);
    return TRUE;
}

// A cRZString on the stack (original vtable), destroyed at the end of scope.
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
    cIGZString *I() { return S().AsInterface(); }
};

std::string NodeSummary(const as2::FindNode *node)
{
    if (!node) return "null";
    char buf[64];
    std::snprintf(buf, sizeof(buf), " dir=%u handle=%p", node->mIsDirectory, node->mHandle);
    return "name=" + std::to_string(node->mName.mString.size()) + ":" + node->mName.mString + " pattern=" + std::to_string(node->mPattern.mString.size()) + ":" +
           node->mPattern.mString + buf;
}

const char *const kTexts[] = {"",
                              "plain",
                              "caf\xc3\xa9",
                              "caf\xe9",
                              "\xe2\x82\xac uro \xc3\xbc",
                              "C:\\Games\\The Sims 2\\Nei\xc3\xabr\\",
                              "an ascii string long enough to leave the in-place buffer and then some more text to exceed one hundred characters!!",
                              "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9"
                              "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9"
                              "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9"
                              "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9",
                              "exactly fifteen"};

} // namespace

// 0x1b0356: text from an encoding into a string (in place or into another one).
DIFF_TEST(AppServices2_ConvertStringFrom, kConvertFrom)
{
    auto original = ctx.Original<uint32_t(__cdecl *)(const void *, int32_t, void *)>(kConvertFrom);
    const int32_t encodings[] = {-2, -1, 8, 0, 1252, 437, 932, 1251, 16, 65001, 1};
    for (const char *text : kTexts) {
        for (int32_t encoding : encodings) {
            for (int inPlace = 0; inPlace < 2; ++inPlace) {
                std::string results[2], sources[2];
                uint32_t rets[2];
                for (int side = 0; side < 2; ++side) {
                    Str src(text), dst("previous destination text that is long enough for the heap");
                    cIGZString *target = inPlace ? src.I() : dst.I();
                    if (side == 0) rets[side] = original(src.I(), encoding, target) & 0xff;
                    else rets[side] = as2::ConvertStringFrom(src.I(), encoding, target) & 1 ? 1 : 0;
                    if (side == 0) rets[side] = rets[side] ? 1 : 0;
                    results[side] = dst.S().mString;
                    sources[side] = src.S().mString;
                }
                ++ctx.cases;
                if (rets[0] != rets[1] || results[0] != results[1] || sources[0] != sources[1])
                    ctx.Fail("convert \"%s\" enc %d inPlace %d: ret %u/%u dst \"%s\"/\"%s\" src \"%s\"/\"%s\"", text, encoding, inPlace, rets[0], rets[1], results[0].c_str(), results[1].c_str(),
                             sources[0].c_str(), sources[1].c_str());
            }
        }
    }
}

namespace {
void __fastcall LoggingFromChar(void *, void *, const char *text, uint32_t length)
{
    Log("FromChar(%u:%.*s)", length, static_cast<int>(length), text);
}
} // namespace

// The same-encoding paths call the destination's FromChar once (and not at all for one string): seen through a
// logging vtable copy.
DIFF_TEST(AppServices2_ConvertStringFromCalls, kConvertFrom)
{
    auto original = ctx.Original<uint32_t(__cdecl *)(const void *, int32_t, void *)>(kConvertFrom);
    void *vtable[34];
    std::memcpy(vtable, ctx.image.At<void *>(cRZString::kVTable), sizeof(vtable));
    vtable[3] = reinterpret_cast<void *>(&LoggingFromChar);
    for (const char *text : {"", "plain", "caf\xe9"}) {
        for (int32_t encoding : {-2, 8, -1, 1252, 16}) {
            for (int inPlace = 0; inPlace < 2; ++inPlace) {
                std::string logs[2];
                uint32_t rets[2];
                for (int side = 0; side < 2; ++side) {
                    Str src(text), dst("x");
                    *reinterpret_cast<void **>(&dst.S()) = vtable;
                    *reinterpret_cast<void **>(&src.S()) = vtable;
                    cIGZString *target = inPlace ? src.I() : dst.I();
                    gLog.clear();
                    rets[side] = (side == 0 ? original(src.I(), encoding, target) : as2::ConvertStringFrom(src.I(), encoding, target)) & 1;
                    logs[side] = Joined();
                    src.S().mVTable = ctx.image.At<void>(cRZString::kVTable); // the destructors run the real ones
                    dst.S().mVTable = ctx.image.At<void>(cRZString::kVTable);
                }
                ++ctx.cases;
                if (rets[0] != rets[1] || logs[0] != logs[1]) ctx.Fail("convert calls \"%s\" enc %d inPlace %d: %s / %s", text, encoding, inPlace, logs[0].c_str(), logs[1].c_str());
            }
        }
    }
}

// 0x1c53c2: the UTF-16 form, built in raw memory.
DIFF_TEST(AppServices2_StringToUtf16, kToUtf16)
{
    auto original = ctx.Original<void *(__cdecl *)(void *, const void *)>(kToUtf16);
    std::vector<std::string> texts(std::begin(kTexts), std::end(kTexts));
    for (size_t n : {1, 2, 3, 7, 8, 9, 40, 42, 43, 85, 86, 87, 200, 700}) texts.push_back(std::string(n, 'q') + "\xc3\xa9");
    for (const std::string &text : texts) {
        for (int fill = 0; fill < 2; ++fill) {
            alignas(8) unsigned char blocks[2][sizeof(std::u16string)];
            void *rets[2];
            for (int side = 0; side < 2; ++side) {
                std::memset(blocks[side], fill ? 0xcd : 0x00, sizeof(blocks[side]));
                Str src(text.c_str());
                rets[side] = side == 0 ? original(blocks[0], src.I()) : as2::StringToUtf16(blocks[1], src.I());
            }
            ++ctx.cases;
            auto *a = reinterpret_cast<std::u16string *>(blocks[0]);
            auto *b = reinterpret_cast<std::u16string *>(blocks[1]);
            bool same = rets[0] == blocks[0] && rets[1] == blocks[1] && *a == *b && a->capacity() == b->capacity();
            // Inside the object: the whole 16-byte buffer when the text is in place (stale bytes after the terminator included).
            if (same && a->capacity() <= 7 && std::memcmp(blocks[0], blocks[1], 16) != 0) same = false;
            if (!same) ctx.Fail("utf16 \"%.30s\" fill %d: size %zu/%zu cap %zu/%zu", text.c_str(), fill, a->size(), b->size(), a->capacity(), b->capacity());
            a->~basic_string();
            b->~basic_string();
        }
    }
}

namespace {

// Runs one search on both sides with the same script; compares node, log. The node is closed by our CloseFind.
void CompareFind(difftest::Context &ctx, Slots &slots, const char *pattern, const char *label)
{
    (void)slots;
    std::string summaries[2], logs[2], afterClose[2];
    for (int side = 0; side < 2; ++side) {
        Str path(pattern);
        gLog.clear();
        auto *node = side == 0 ? ctx.Original<as2::FindNode *(__cdecl *)(cRZString *)>(kFindFirst)(&path.S()) : as2::FindFirst(&path.S());
        summaries[side] = NodeSummary(node);
        logs[side] = Joined();
        gLog.clear();
        as2::CloseFind(node);
        afterClose[side] = Joined();
    }
    ++ctx.cases;
    if (summaries[0] != summaries[1] || logs[0] != logs[1] || afterClose[0] != afterClose[1])
        ctx.Fail("%s \"%s\":\n  orig: %s %s %s\n  ours: %s %s %s", label, pattern, summaries[0].c_str(), logs[0].c_str(), afterClose[0].c_str(), summaries[1].c_str(), logs[1].c_str(),
                 afterClose[1].c_str());
}

} // namespace

// 0x1c54dd: the search, with the ANSI import (the game's normal case) and with the optional wide function.
DIFF_TEST(AppServices2_FindFirst, kFindFirst)
{
    Slots slots(ctx);
    slots.Set(kIatFindFirstFileA, reinterpret_cast<void *>(&FakeFindFirstA));
    slots.Set(kIatFindClose, reinterpret_cast<void *>(&FakeFindClose));
    const char *const patterns[] = {"C:\\Games\\*", "C:\\Games\\Sims\xc3\xa9\\*", "", "\xe2\x82\xac\\*", "relative/path/*",
                                    "a long directory name that does not fit in place at all\\and another part of it\\*"};
    const char *const names[] = {"file.txt", "caf\xe9.txt", "", "a name that is longer than fifteen characters.dat", "\xe4\xf6\xfc"};
    const DWORD attributes[] = {0, 0x10, 0x20, 0x30, 0xffffffff, 0x1};
    const HANDLE handles[] = {reinterpret_cast<HANDLE>(0x4242), reinterpret_cast<HANDLE>(1), reinterpret_cast<HANDLE>(0xfffffffe)};
    for (int wide = 0; wide < 2; ++wide) {
        *ctx.image.At<void *>(kFindFirstFileWPointer) = wide ? reinterpret_cast<void *>(&FakeFindFirstW) : nullptr;
        for (const char *pattern : patterns) {
            for (const char *name : names) {
                for (DWORD attr : attributes) {
                    gSearch = Search{};
                    gSearch.name = name;
                    gSearch.wideName = std::u16string(name, name + std::strlen(name)); // bytes as units: enough to vary
                    gSearch.attributes = attr;
                    gSearch.handle = handles[(attr + std::strlen(name)) % 3];
                    CompareFind(ctx, slots, pattern, wide ? "wide" : "ansi");
                }
            }
            gSearch = Search{};
            gSearch.found = false;
            CompareFind(ctx, slots, pattern, wide ? "wide none" : "ansi none");
        }
    }
    *ctx.image.At<void *>(kFindFirstFileWPointer) = nullptr;
}

// 0x1c54b2 and 0x1c49cb: close the search (unless the handle is -1), destroy the strings, free the block.
DIFF_TEST(AppServices2_CloseFind, kCloseFind)
{
    Slots slots(ctx);
    slots.Set(kIatFindClose, reinterpret_cast<void *>(&FakeFindClose));
    auto original = ctx.Original<void(__cdecl *)(as2::FindNode *)>(kCloseFind);
    const char *const names[] = {"", "short", "a name that is longer than fifteen characters.dat"};
    for (const char *name : names) {
        for (const char *pattern : names) {
            for (HANDLE handle : {reinterpret_cast<HANDLE>(0x77), INVALID_HANDLE_VALUE}) {
                std::string logs[2];
                bool freed[2];
                for (int side = 0; side < 2; ++side) {
                    auto *node = static_cast<as2::FindNode *>(::operator new(sizeof(as2::FindNode)));
                    node->mName.Construct(name);
                    node->mPattern.Construct(pattern);
                    node->mHandle = handle;
                    gLog.clear();
                    difftest::FreeRecorder rec;
                    if (side == 0) original(node);
                    else as2::CloseFind(node);
                    rec.Stop();
                    logs[side] = Joined();
                    freed[side] = rec.Freed(node);
                    if (!freed[side]) ::operator delete(node);
                }
                ++ctx.cases;
                if (logs[0] != logs[1] || freed[0] != freed[1] || !freed[0]) ctx.Fail("close \"%s\" \"%s\" handle %p: %s / %s freed %d/%d", name, pattern, handle, logs[0].c_str(), logs[1].c_str(), freed[0], freed[1]);
            }
        }
    }
    {
        // null is ignored (no call, no crash)
        gLog.clear();
        original(nullptr);
        as2::CloseFind(nullptr);
        ++ctx.cases;
        if (!gLog.empty()) ctx.Fail("close null logged %s", Joined().c_str());
    }
}

DIFF_TEST(AppServices2_DestructFindNode, kDestructNode)
{
    auto original = ctx.Original<void(__thiscall *)(as2::FindNode *)>(kDestructNode);
    const char *const names[] = {"", "short", "a name that is longer than fifteen characters.dat"};
    for (const char *name : names) {
        for (const char *pattern : names) {
            alignas(8) unsigned char blocks[2][sizeof(as2::FindNode)];
            for (int side = 0; side < 2; ++side) {
                std::memset(blocks[side], 0xee, sizeof(blocks[side]));
                auto *node = reinterpret_cast<as2::FindNode *>(blocks[side]);
                node->mName.Construct(name);
                node->mPattern.Construct(pattern);
                node->mIsDirectory = 1;
                node->mHandle = reinterpret_cast<void *>(0x99);
                if (side == 0) original(node);
                else as2::DestructFindNode(node, nullptr);
            }
            ++ctx.cases;
            // Heap pointers of the long strings are the only bytes that may differ (and are dead): the fields after
            // destruction are vtable, empty small string, refcount, flag, handle.
            // Only the freed pointers of long strings (bytes 1..3 of the buffer, byte 0 is cleared) may differ.
            bool same = true;
            for (size_t i = 0; i < sizeof(blocks[0]); ++i) {
                bool nameKnown = std::strlen(name) > 15 && i >= 5 && i < 8;
                bool patternKnown = std::strlen(pattern) > 15 && i >= 0x29 && i < 0x2c;
                if (nameKnown || patternKnown) continue;
                if (blocks[0][i] != blocks[1][i]) same = false;
            }
            if (!same) ctx.Fail("destruct \"%s\" \"%s\": blocks differ\n  orig %s\n  ours %s", name, pattern, Hex(blocks[0], sizeof(blocks[0])).c_str(), Hex(blocks[1], sizeof(blocks[1])).c_str());
        }
    }
}

// 0x1c4f79: found or not (the node is closed again).
DIFF_TEST(AppServices2_PatternMatches, kPatternMatches)
{
    Slots slots(ctx);
    slots.Set(kIatFindFirstFileA, reinterpret_cast<void *>(&FakeFindFirstA));
    slots.Set(kIatFindClose, reinterpret_cast<void *>(&FakeFindClose));
    auto original = ctx.Original<uint32_t(__cdecl *)(cRZString *)>(kPatternMatches);
    for (int found = 0; found < 2; ++found) {
        for (const char *pattern : {"C:\\x\\*", "", "a long pattern that does not fit in place \\ *"}) {
            for (HANDLE handle : {reinterpret_cast<HANDLE>(0x31), INVALID_HANDLE_VALUE}) {
                std::string logs[2];
                uint32_t rets[2];
                std::string patterns[2];
                for (int side = 0; side < 2; ++side) {
                    gSearch = Search{};
                    gSearch.found = found != 0;
                    gSearch.handle = handle;
                    Str path(pattern);
                    gLog.clear();
                    rets[side] = (side == 0 ? original(&path.S()) : as2::PatternMatches(&path.S())) & 0xff;
                    logs[side] = Joined();
                    patterns[side] = path.S().mString;
                }
                ++ctx.cases;
                if (rets[0] != rets[1] || logs[0] != logs[1] || patterns[0] != patterns[1])
                    ctx.Fail("matches found %d \"%s\" handle %p: ret %u/%u\n  orig %s\n  ours %s", found, pattern, handle, rets[0], rets[1], logs[0].c_str(), logs[1].c_str());
            }
        }
    }
}

// 0x1c4f94: trailing separator, then the wildcard text from the game's pointer. The pointer is also moved to other texts.
DIFF_TEST(AppServices2_DirectoryExists, kDirectoryExists)
{
    Slots slots(ctx);
    slots.Set(kIatFindFirstFileA, reinterpret_cast<void *>(&FakeFindFirstA));
    slots.Set(kIatFindClose, reinterpret_cast<void *>(&FakeFindClose));
    slots.Set(kWildcardPointer, *ctx.image.At<void *>(kWildcardPointer)); // restores the original on exit
    static const char kOther[] = "*.pkg";
    auto original = ctx.Original<uint32_t(__cdecl *)(cRZString *)>(kDirectoryExists);
    const char *const paths[] = {"C:\\Games", "C:\\Games\\", "C:/Games/", "C:/Games", "", "\\", "/", "C:\\a\\", "x", "a path that is longer than fifteen characters",
                                 "a path that is longer than fifteen characters\\", "C:\\Games\\Sims\xc3\xa9"};
    for (int wildcard = 0; wildcard < 2; ++wildcard) {
        if (wildcard) *ctx.image.At<const char *>(kWildcardPointer) = kOther;
        for (const char *path : paths) {
            for (int found = 0; found < 2; ++found) {
                std::string logs[2], texts[2];
                uint32_t rets[2];
                for (int side = 0; side < 2; ++side) {
                    gSearch = Search{};
                    gSearch.found = found != 0;
                    Str input(path);
                    gLog.clear();
                    rets[side] = (side == 0 ? original(&input.S()) : as2::DirectoryExists(&input.S())) & 0xff;
                    logs[side] = Joined();
                    texts[side] = input.S().mString; // the argument itself is not changed
                }
                ++ctx.cases;
                if (rets[0] != rets[1] || logs[0] != logs[1] || texts[0] != texts[1] || texts[0] != path)
                    ctx.Fail("exists \"%s\" wildcard %d found %d: ret %u/%u\n  orig %s\n  ours %s", path, wildcard, found, rets[0], rets[1], logs[0].c_str(), logs[1].c_str());
            }
        }
    }
}

// The three setters (face = object + 0x1c): the new text, the check and the flag.
namespace {

struct FsBlock {
    alignas(8) unsigned char raw[sizeof(cGZFileSystem)];
    FsBlock()
    {
        std::memset(raw, 0x5a, sizeof(raw));
        fs().mAppDirectory.Construct("old app directory that is long enough for the heap");
        fs().mDataDirectory.Construct("old data");
        fs().mPlugInDirectory.Construct("");
        fs().mDataDirectorySet = fs().mPlugInDirectorySet = fs().mAppDirectorySet = 0;
    }
    ~FsBlock()
    {
        fs().mAppDirectory.Destruct();
        fs().mDataDirectory.Destruct();
        fs().mPlugInDirectory.Destruct();
    }
    cGZFileSystem &fs() { return *reinterpret_cast<cGZFileSystem *>(raw); }
    void *face() { return raw + cGZFileSystem::kFace; }
};

void CompareSetter(difftest::Context &ctx, uint32_t rva, bool (*ours)(cGZFileSystem &, const cIGZString *), const char *label)
{
    Slots slots(ctx);
    slots.Set(kIatFindFirstFileA, reinterpret_cast<void *>(&FakeFindFirstA));
    slots.Set(kIatFindClose, reinterpret_cast<void *>(&FakeFindClose));
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, const cIGZString *)>(rva);
    const char *const paths[] = {"C:\\Games", "C:\\Games\\", "", "x", "a new path that is longer than fifteen characters\\", "C:\\Sims\xc3\xa9"};
    for (const char *path : paths) {
        for (int found = 0; found < 2; ++found) {
            std::string logs[2], dump[2];
            uint32_t rets[2];
            for (int side = 0; side < 2; ++side) {
                gSearch = Search{};
                gSearch.found = found != 0;
                FsBlock block;
                Str input(path);
                gLog.clear();
                rets[side] = (side == 0 ? original(block.face(), input.I()) : static_cast<uint32_t>(ours(block.fs(), input.I()))) & 0xff;
                logs[side] = Joined();
                cGZFileSystem &fs = block.fs();
                char buf[64];
                std::snprintf(buf, sizeof(buf), "flags %u%u%u ", fs.mDataDirectorySet, fs.mPlugInDirectorySet, fs.mAppDirectorySet);
                dump[side] = buf + std::to_string(fs.mAppDirectory.mString.size()) + ":" + fs.mAppDirectory.mString + "|" + fs.mDataDirectory.mString + "|" + fs.mPlugInDirectory.mString;
                // the bytes outside the three strings and the flags are untouched
                for (size_t i = 0; i < sizeof(block.raw); ++i) {
                    bool owned = (i >= 0x20 && i < 0x23) || (i >= 0x44 && i < 0x64) || (i >= 0xa4 && i < 0xe4);
                    if (!owned && block.raw[i] != 0x5a) dump[side] += " stray@" + std::to_string(i);
                }
            }
            ++ctx.cases;
            if (rets[0] != rets[1] || logs[0] != logs[1] || dump[0] != dump[1])
                ctx.Fail("%s \"%s\" found %d: ret %u/%u\n  orig %s %s\n  ours %s %s", label, path, found, rets[0], rets[1], dump[0].c_str(), logs[0].c_str(), dump[1].c_str(), logs[1].c_str());
        }
    }
}

} // namespace

DIFF_TEST(AppServices2_SetAppDirectory, kSetApp)
{
    CompareSetter(ctx, kSetApp, [](cGZFileSystem &fs, const cIGZString *p) { return fs.cGZFileSystem::SetAppDirectory(p); }, "SetAppDirectory");
}
DIFF_TEST(AppServices2_SetDataDirectory, kSetData)
{
    CompareSetter(ctx, kSetData, [](cGZFileSystem &fs, const cIGZString *p) { return fs.cGZFileSystem::SetDataDirectory(p); }, "SetDataDirectory");
}
DIFF_TEST(AppServices2_SetPlugInDirectory, kSetPlugIn)
{
    CompareSetter(ctx, kSetPlugIn, [](cGZFileSystem &fs, const cIGZString *p) { return fs.cGZFileSystem::SetPlugInDirectory(p); }, "SetPlugInDirectory");
}
