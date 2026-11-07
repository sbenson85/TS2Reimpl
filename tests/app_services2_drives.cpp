// Differential tests for the application path lookup and the CD search of src/app/AppServices2.*: 0x1b1454 (module
// path copy), 0xc76294 (primary slot 13) and 0xc76196 (face slot 13, FindCDDrive).
//
// The Windows calls go through import slots (GetLogicalDriveStringsA, GetDriveTypeA, GetModuleFileNameA) or the
// game's cached GetModuleFileNameW pointer, all pointed at scripted fakes; the object is a block with a fake primary
// vtable whose slot 14 (the CD check) logs and answers from a script.
#define NOMINMAX
#include "app/AppServices2.h"
#include "difftest.h"
#include "game/game.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t kCopyModulePath = 0x1b1454, kResolve = 0xc76294, kFindCD = 0xc76196;
constexpr uint32_t kIatGetLogicalDriveStringsA = 0xd9a248, kIatGetDriveTypeA = 0xd9a23c, kIatGetModuleFileNameA = 0xd9a16c;
constexpr uint32_t kResolvedByte = 0x10caeb5, kWidePointer = 0x10caeb8;

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

class Slots {
public:
    explicit Slots(difftest::Context &c) : ctx(c) {}
    ~Slots()
    {
        for (auto it = saved.rbegin(); it != saved.rend(); ++it) {
            if (it->second.size == 1) *ctx.image.At<uint8_t>(it->first) = static_cast<uint8_t>(it->second.value);
            else *ctx.image.At<uint32_t>(it->first) = it->second.value;
        }
    }
    void Set(uint32_t rva, void *value)
    {
        saved.push_back({rva, {4, *ctx.image.At<uint32_t>(rva)}});
        *ctx.image.At<void *>(rva) = value;
    }
    void SetByte(uint32_t rva, uint8_t value)
    {
        saved.push_back({rva, {1, *ctx.image.At<uint8_t>(rva)}});
        *ctx.image.At<uint8_t>(rva) = value;
    }

private:
    struct Old {
        int size;
        uint32_t value;
    };
    difftest::Context &ctx;
    std::vector<std::pair<uint32_t, Old>> saved;
};

// ---- the scripted drives ----

std::string gDriveList;          // "C:\\\0D:\\\0" (the final terminator is added)
std::string gTypesByLetter;      // 26 characters: the type of drive 'a'..'z' as a digit; others get 3
std::vector<uint32_t> gCdAnswers; // slot 14 results, in call order

DWORD WINAPI FakeLogicalDrives(DWORD size, char *buffer)
{
    Log("drives(%u)", size);
    DWORD needed = static_cast<DWORD>(gDriveList.size());
    if (size < needed + 1) return needed + 1; // too small: the size needed with the terminator, as Windows does
    std::memcpy(buffer, gDriveList.data(), needed);
    buffer[needed] = 0;
    return needed;
}

UINT WINAPI FakeDriveType(const char *root)
{
    Log("type(%s)", root);
    unsigned char c = static_cast<unsigned char>(root[0]);
    if (c >= 'a' && c <= 'z') return static_cast<UINT>(gTypesByLetter[c - 'a'] - '0');
    return 3;
}

// ---- the object ----

constexpr size_t kSize = sizeof(cGZFileSystem);
void *gFakeVTable[24];
size_t gCdCalls = 0;
const cGZFileSystem *gCurrent = nullptr;

uint32_t __fastcall FakeCdCheck(cGZFileSystem *self, void *)
{
    Log("cdcheck(%s)", self->mCDAppDrive.mString.c_str());
    uint32_t answer = gCdCalls < gCdAnswers.size() ? gCdAnswers[gCdCalls] : 0;
    ++gCdCalls;
    return answer;
}

struct Block {
    alignas(8) unsigned char raw[kSize];
    explicit Block(uint32_t salt)
    {
        std::memset(raw, 0x5a, sizeof(raw));
        gFakeVTable[14] = reinterpret_cast<void *>(&FakeCdCheck);
        *reinterpret_cast<void **>(raw) = gFakeVTable;
        char text[64];
        auto make = [&](cRZString &s, const char *label, uint32_t k) {
            std::snprintf(text, sizeof(text), "%s-%u", label, salt + k);
            std::string longText = std::string(text) + " - a text for this slot that is long enough to be on the heap";
            s.Construct((k % 3 == 0) ? "" : (k % 3 == 1) ? text : longText.c_str());
        };
        cGZFileSystem &f = fs();
        make(f.mAppDrive, "ad", 1);
        make(f.mAppDirectory, "adir", 2);
        make(f.mAppName, "an", 3);
        make(f.mAppExtension, "ae", 4);
        make(f.mDataDirectory, "dd", 5);
        make(f.mPlugInDirectory, "pd", 6);
        make(f.mCDAppDrive, "cad", 7);
        make(f.mCDAppDirectory, "cadir", 8);
        make(f.mCDDataDirectory, "cdd", 9);
        make(f.mCDPlugInDirectory, "cpd", 10);
    }
    ~Block()
    {
        cGZFileSystem &f = fs();
        for (cRZString *s : {&f.mAppDrive, &f.mAppDirectory, &f.mAppName, &f.mAppExtension, &f.mDataDirectory, &f.mPlugInDirectory, &f.mCDAppDrive, &f.mCDAppDirectory, &f.mCDDataDirectory,
                             &f.mCDPlugInDirectory})
            s->Destruct();
    }
    cGZFileSystem &fs() { return *reinterpret_cast<cGZFileSystem *>(raw); }
    void *face() { return raw + cGZFileSystem::kFace; }

    std::string Dump()
    {
        cGZFileSystem &f = fs();
        std::string out;
        for (const cRZString *s : {&f.mAppDrive, &f.mAppDirectory, &f.mAppName, &f.mAppExtension, &f.mDataDirectory, &f.mPlugInDirectory, &f.mCDAppDrive, &f.mCDAppDirectory, &f.mCDDataDirectory,
                                   &f.mCDPlugInDirectory})
            out += std::to_string(s->mString.size()) + ":" + s->mString + "|";
        char buf[64];
        std::snprintf(buf, sizeof(buf), "flags %u %u %u cd %u ", f.mDataDirectorySet, f.mPlugInDirectorySet, f.mAppDirectorySet, f.mCDResolved);
        out += buf;
        // Bytes outside the strings and the four flags keep their fill.
        static const struct {
            size_t from, to;
        } owned[] = {{0, 4}, {0x20, 0x23}, {0x24, 0xe4}, {0x104, 0x105}, {0x108, 0x1a8}};
        for (size_t i = 0; i < sizeof(raw); ++i) {
            bool own = false;
            for (auto &r : owned) own |= i >= r.from && i < r.to;
            if (!own && raw[i] != 0x5a) out += " stray@" + std::to_string(i);
        }
        return out;
    }
};

} // namespace

// 0xc76196: the CD search.
DIFF_TEST(AppServices2_FindCDDrive, kFindCD)
{
    Slots slots(ctx);
    slots.Set(kIatGetLogicalDriveStringsA, reinterpret_cast<void *>(&FakeLogicalDrives));
    slots.Set(kIatGetDriveTypeA, reinterpret_cast<void *>(&FakeDriveType));
    auto original = ctx.Original<uint32_t(__thiscall *)(void *)>(kFindCD);
    std::mt19937 rng(0x5eed);
    const std::vector<std::string> lists = {std::string("C:\\\0", 4),
                                            std::string("C:\\\0D:\\\0", 8),
                                            std::string("A:\\\0C:\\\0D:\\\0E:\\\0Z:\\\0", 20),
                                            std::string("c:\\\0d:\\\0e:\\\0", 12),
                                            std::string("1:\\\0~:\\\0\xe9:\\\0D:\\\0", 16),
                                            std::string("E:\\\0", 4),
                                            std::string("a:\\\0b:\\\0c:\\\0d:\\\0", 16),
                                            std::string("", 0),
                                            std::string("C:\\\0D:\\\0E:\\\0F:\\\0G:\\\0H:\\\0I:\\\0J:\\\0K:\\\0L:\\\0M:\\\0N:\\\0", 48)};
    for (const std::string &list : lists) {
        for (int round = 0; round < 40; ++round) {
            gDriveList = list;
            gTypesByLetter.clear();
            for (int i = 0; i < 26; ++i) gTypesByLetter += static_cast<char>('0' + (rng() % 4 == 0 ? 5 : rng() % 4 == 0 ? 2 : 3 + (rng() & 1)));
            gCdAnswers.clear();
            for (int i = 0; i < 8; ++i) gCdAnswers.push_back(rng() % 3 == 0 ? (rng() & 1 ? 0x100u : 0x1u) : (rng() & 1 ? 0xffffff01u : 0u));
            std::string results[2], dumps[2], logs[2];
            uint32_t rets[2];
            for (int side = 0; side < 2; ++side) {
                Block block(round);
                gLog.clear();
                gCdCalls = 0;
                rets[side] = (side == 0 ? original(block.face()) : static_cast<uint32_t>(block.fs().cGZFileSystem::FindCDDrive())) & 0xff;
                dumps[side] = block.Dump();
                logs[side] = Joined();
            }
            ++ctx.cases;
            if (rets[0] != rets[1] || dumps[0] != dumps[1] || logs[0] != logs[1])
                ctx.Fail("cd drive list #%zu round %d: ret %u/%u\n  orig %s %s\n  ours %s %s", static_cast<size_t>(&list - &lists[0]), round, rets[0], rets[1], dumps[0].c_str(), logs[0].c_str(), dumps[1].c_str(),
                         logs[1].c_str());
        }
    }
}

namespace {

std::string gModulePath;     // what GetModuleFileNameW answers (narrowed)
std::string gModulePathAnsi; // what GetModuleFileNameA answers

DWORD WINAPI FakeModuleFileNameW(void *module, wchar_t *buffer, DWORD size)
{
    Log("GetModuleFileNameW(%p, %u)", module, size);
    DWORD n = static_cast<DWORD>(gModulePath.size() < size ? gModulePath.size() : size);
    for (DWORD i = 0; i < n; ++i) buffer[i] = static_cast<unsigned char>(gModulePath[i]);
    return n;
}

DWORD WINAPI FakeModuleFileNameA(void *module, char *buffer, DWORD size)
{
    Log("GetModuleFileNameA(%p, %u)", module, size);
    DWORD n = static_cast<DWORD>(gModulePathAnsi.size() < size ? gModulePathAnsi.size() : size);
    std::memcpy(buffer, gModulePathAnsi.data(), n);
    return n;
}

const char *const kModulePaths[] = {"C:\\Program Files\\EA GAMES\\The Sims 2\\TSBin\\Sims2EP9.exe",
                                    "D:\\a.exe",
                                    "\\\\server\\share\\dir\\x.exe",
                                    "relative\\x.exe",
                                    "x",
                                    "C:",
                                    "C:\\",
                                    "C:/mixed/slashes\\here/tool.v2.exe",
                                    "weird.name.with.dots.tar.gz",
                                    "C:\\a\\b\\c\\d\\e\\f\\g\\h\\i\\j\\k\\l\\m\\n\\o\\p\\q\\r\\s\\t\\u\\v\\w\\x\\y\\z\\and a much longer directory name here\\file name with spaces.extension",
                                    "C:\\dir.with.dot\\noext"};

} // namespace

// 0x1b1454: the module path into a caller's buffer.
DIFF_TEST(AppServices2_CopyModulePath, kCopyModulePath)
{
    Slots slots(ctx);
    slots.SetByte(kResolvedByte, 1);
    slots.Set(kWidePointer, reinterpret_cast<void *>(&FakeModuleFileNameW));
    slots.Set(kIatGetModuleFileNameA, reinterpret_cast<void *>(&FakeModuleFileNameA));
    auto original = ctx.Original<uint32_t(__cdecl *)(char *, uint32_t)>(kCopyModulePath);
    std::vector<std::string> paths(std::begin(kModulePaths), std::end(kModulePaths));
    paths.push_back(std::string(259, 'p'));
    paths.push_back(std::string(260, 'p'));
    paths.push_back(std::string(261, 'p'));
    paths.push_back("");
    for (const std::string &path : paths) {
        for (int viaAnsi = 0; viaAnsi < 2; ++viaAnsi) {
            gModulePath = viaAnsi ? "" : path;
            gModulePathAnsi = path;
            for (uint32_t capacity : {0u, 1u, 5u, 10u, 59u, 60u, 100u, 0x104u}) {
                unsigned char buffers[2][0x120];
                uint32_t rets[2];
                std::string logs[2];
                for (int side = 0; side < 2; ++side) {
                    std::memset(buffers[side], 0x77, sizeof(buffers[side]));
                    gLog.clear();
                    char *buffer = reinterpret_cast<char *>(buffers[side]);
                    rets[side] = side == 0 ? original(buffer, capacity) : as2::CopyModulePath(buffer, capacity);
                    logs[side] = Joined();
                }
                ++ctx.cases;
                if (rets[0] != rets[1] || std::memcmp(buffers[0], buffers[1], sizeof(buffers[0])) != 0 || logs[0] != logs[1])
                    ctx.Fail("copy \"%.40s\" ansi %d capacity %u: ret %u/%u\n  orig %s\n  ours %s", path.c_str(), viaAnsi, capacity, rets[0], rets[1], logs[0].c_str(), logs[1].c_str());
            }
        }
    }
}

// 0xc76294: the application paths.
DIFF_TEST(AppServices2_ResolveApplicationPaths, kResolve)
{
    Slots slots(ctx);
    slots.SetByte(kResolvedByte, 1);
    slots.Set(kWidePointer, reinterpret_cast<void *>(&FakeModuleFileNameW));
    slots.Set(kIatGetModuleFileNameA, reinterpret_cast<void *>(&FakeModuleFileNameA));
    auto original = ctx.Original<void(__thiscall *)(void *)>(kResolve);
    for (const char *path : kModulePaths) {
        for (int viaAnsi = 0; viaAnsi < 2; ++viaAnsi) {
            gModulePath = viaAnsi ? "" : path;
            gModulePathAnsi = path;
            for (uint32_t salt : {0u, 1u, 2u}) {
                std::string dumps[2], logs[2];
                for (int side = 0; side < 2; ++side) {
                    Block block(salt);
                    gLog.clear();
                    if (side == 0) original(&block.fs());
                    else block.fs().cGZFileSystem::ResolveApplicationPaths();
                    dumps[side] = block.Dump();
                    logs[side] = Joined();
                }
                ++ctx.cases;
                if (dumps[0] != dumps[1] || logs[0] != logs[1])
                    ctx.Fail("resolve \"%.40s\" ansi %d salt %u:\n  orig %s %s\n  ours %s %s", path, viaAnsi, salt, dumps[0].c_str(), logs[0].c_str(), dumps[1].c_str(), logs[1].c_str());
            }
        }
    }
}
