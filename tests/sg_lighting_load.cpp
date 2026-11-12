// Differential tests for nTSSG::cLightingManager's loading of the lighting files (lane 261110r, scenegraph LG3c rest;
// src/sg/cLightingManagerLoad.cpp): ReadLightingResources 0x2d0ae0 and slot 5 LoadLightingFiles 0x2df8a0. Hook shape: the
// original runs on the same object image as ours.
//
// The world (one draw for both sides): the manager is 0x548 random bytes whose file parser (+0x30) is a fake (slot 9 the
// base parser, null on some draws; slot 6 AddFile, slot 7 RunFiles). The base parser has a virtual base like the game's:
// its primary vtable has slot 14 (the script text, its size, the file name), and its cIGZUnknown face (+8, through the
// vbptr at +4) AddRef and Release; each logs the pointer it was called on. The resource manager (the cached service
// 0x10faf6c's slot 3; the cache cell is saved, set and restored) is a fake: slot 39 opens (key, &resource, 0), slot 34
// (key, &name), slot 19 lists the keys. Its answers are drawn per call: an open that fails with a resource written
// (released, never closed) or untouched, a read that fails after a good open, a name answer drawn both ways independently
// of whether the name word is written, sizes 0, 1, around 15 / 16 and 0x1000 and up to 9000 (several keys, a later one
// smaller and larger than the first). The fakes log the words the original zeroes before its calls (the out words on entry),
// and the read fake logs how many of the size + 1 bytes of the buffer it is handed are not zero (the buffer is emptied after
// every key and holds one byte more than the size) before it fills the first size bytes. The parser fake logs the
// script's size bytes by hash and the byte after them. Every fake answer the code reads in the low byte alone carries
// junk above it. The image's operator new / delete (0x824632 / 0x824624) are ours on both sides, so the byte buffer and the
// names share our heap; the counter compares blocks and bytes (the buffer's growth, its sized delete).
//
// LoadLightingFiles' world (namespace files): the option call 0x1d0c60 and cRZFile::PathExists 0x1c1242 are stood in on
// both sides (the call logs its words, the list header and the nodes it finds, and pushes drawn paths: call 0 and call 1
// differ, so the second read's appending shows; PathExists answers from a drawn bit mask); the file parser's slot 6 / 7 log
// the receiver, path, flags and RunFiles; the manager's +0x30 is swapped between two parsers inside the option call and
// the existence check on some draws (the code reads it again for every use). ReadLightingResources (0x2d0ae0) is stood in
// on both sides and logs the key vector's words and keys. The resource manager's slot 19 fills the key vector, or leaves
// it untouched. Allocator service: the cache cell 0x10c95d4 points at a wrapper whose slots 3 / 4 log the size, the block's
// number (allocation order) and the frees, and forward to the scope's service. Its blocks (the list's nodes, the paths'
// strings, the key vector) come from the general allocator's VirtualAlloc cores, which the AllocationCounter never sees: they
// are out of its count on both sides the same way, and compared through that log instead.
//
// Compared: the result, the fakes' log (the pointers by identity), every word of the manager, the keys, the counts and the
// cache cell, and the heap blocks and bytes each side allocated; at the x87's 53-bit and 24-bit precision.
#include "code_patch.h"
#include "difftest.h"
#include "sg_lighting_fakes.h"

#include "app/AppDebugger.h"
#include "rz/cRZString.h"
#include "sg/cLightingManager.h"

#include <cstdint>
#include <cstring>
#include <iterator>
#include <new>
#include <random>
#include <string>

namespace {

using nTSSG::cLightingKey;
using nTSSG::cLightingKeyList;
using nTSSG::cLightingManager;

constexpr uint32_t kManagerSize = 0x548;
constexpr uint32_t kScriptType = 0xadee8d84;
constexpr uint32_t kServiceCache = 0x10faf6c;
constexpr int kMaxKeys = 8;
constexpr int kResources = 4;
constexpr int kNames = 3;

alignas(16) uint8_t gManager[kManagerSize];
cLightingKey gKeys[kMaxKeys];

void *__cdecl GameNew(size_t n) { return ::operator new(n ? n : 1); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

namespace loadfx {

struct Obj {
    void **vt;
    uint32_t size; // a resource's size (set when it is opened)
    uint32_t seed;
};
struct BaseParser {
    void **vt;    // +0 the primary face
    int32_t *vb;  // +4 the vbptr
    void **faceVt; // +8 the cIGZUnknown face (its vtable)
};

Obj gService, gMgr, gFileParser;
Obj gRes[kResources], gName[kNames];
BaseParser gBase;
int32_t gVb[2] = {0, 4};
void *gServiceVT[8], *gMgrVT[48], *gFileParserVT[16], *gResVT[16], *gNameVT[16], *gBaseVT[24], *gFaceVT[4];

// The script of a case.
struct Step {
    bool openYes, openNonNull, readYes, nameYes, nameWrites;
    uint32_t res, size, nameIx;
    uint8_t junk;
};
Step gSteps[kMaxKeys];
bool gHasParser;
uint32_t gOpens, gNames, gKeyCount;
const char *gNameText[] = {"", "lights.txt", "exactly15chars!", "sixteen chars!!!", "a long file name that lives on the heap.txt"};

uint32_t Id(const void *p)
{
    if (!p) return 0;
    for (int i = 0; i < kResources; ++i)
        if (p == &gRes[i]) return 0x100 + i;
    for (int i = 0; i < kNames; ++i)
        if (p == &gName[i]) return 0x200 + i;
    if (p == &gBase) return 0x9b00;
    if (p == &gBase.faceVt) return 0x9b08;
    if (p == &gFileParser) return 0x9a00;
    if (p == &gMgr) return 0xb000;
    if (p == &gService) return 0xb100;
    if (p == gManager) return 0x5000;
    return 0xdead;
}

uint32_t HashBytes(const char *s, uint32_t n)
{
    uint32_t h = 0x811c9dc5u;
    for (uint32_t i = 0; i < n; ++i) h = (h ^ uint8_t(s[i])) * 0x01000193u;
    return h;
}

// ---- the resource manager and its service ----
void *__fastcall ServiceSlot3(Obj *self, void *)
{
    L(0xc3), L(Id(self));
    return &gMgr;
}
uint32_t __fastcall MgrOpen(Obj *self, void *, const cLightingKey *key, void **out, uint32_t third)
{
    const uint32_t n = gOpens++;
    const Step &s = gSteps[n < kMaxKeys ? n : 0];
    L(0x39), L(Id(self)), L(key->mType), L(key->mWord4), L(key->mWord8), L(key->mWordC), L(Id(*out)), L(third);
    Obj *const res = s.openNonNull || s.openYes ? &gRes[s.res % kResources] : nullptr;
    if (res) res->size = s.size, res->seed = n * 7 + 1;
    if (res || s.junk & 1) *out = res; // a failing open may leave the word alone
    return (s.openYes ? 1u : 0u) | (uint32_t(s.junk) << 8);
}
uint32_t __fastcall MgrName(Obj *self, void *, const cLightingKey *key, void **out)
{
    const uint32_t n = gNames++;
    const Step &s = gSteps[n < kMaxKeys ? n : 0];
    L(0x88), L(Id(self)), L(key->mWord4), L(key->mWord8), L(Id(*out));
    if (s.nameWrites) *out = &gName[s.nameIx % kNames];
    return (s.nameYes ? 1u : 0u) | (uint32_t(s.junk) << 8);
}

// ---- the file parser, its base parser and the resources ----
void *__fastcall FileParserGetParser(Obj *self, void *)
{
    L(0x9a09), L(Id(self));
    return gHasParser ? &gBase : nullptr;
}
uint32_t __fastcall FaceAddRef(void *self, void *) { return L(0x9b01), L(Id(self)), 1; }
uint32_t __fastcall FaceRelease(void *self, void *) { return L(0x9b02), L(Id(self)), 1; }
uint32_t __fastcall ParseText(void *self, void *, const char *text, uint32_t size, const char *name)
{
    L(0x9b0e), L(Id(self)), L(size), L(HashBytes(text, size)), L(uint8_t(text[size])), L(Hash(name)),
        L(uint32_t(std::strlen(name)));
    return 0x100;
}
uint32_t __fastcall ResRelease(Obj *self, void *) { return L(0x1002), L(Id(self)), 1; }
uint32_t __fastcall ResClose(Obj *self, void *) { return L(0x1006), L(Id(self)), 0x5500; }
uint32_t __fastcall ResSize(Obj *self, void *) { return L(0x100a), L(Id(self)), L(self->size), self->size; }
uint32_t __fastcall ResRead(Obj *self, void *, char *buffer, uint32_t size)
{
    uint32_t dirty = 0;
    for (uint32_t i = 0; i <= size; ++i) dirty += buffer[i] != 0;
    L(0x1008), L(Id(self)), L(size), L(dirty);
    const Step &s = gSteps[(self->seed - 1) / 7 < kMaxKeys ? (self->seed - 1) / 7 : 0];
    for (uint32_t i = 0; i < size; ++i) buffer[i] = char(1 + (i * 31 + self->seed) % 250);
    return (s.readYes ? 1u : 0u) | (uint32_t(s.junk) << 8);
}
uint32_t __fastcall NameRelease(Obj *self, void *) { return L(0x2002), L(Id(self)), 1; }
uint32_t __fastcall NameFill(Obj *self, void *, cIGZString *text)
{
    cRZString &t = *reinterpret_cast<cRZString *>(text);
    L(0x200b), L(Id(self)), L(uint32_t(t.mString.size())), L(uint32_t(t.mString.capacity()));
    t.mString.assign(gNameText[(self - gName) % std::size(gNameText)]);
    return 1;
}

void FillTables()
{
    gServiceVT[3] = reinterpret_cast<void *>(&ServiceSlot3);
    gMgrVT[39] = reinterpret_cast<void *>(&MgrOpen);
    gMgrVT[34] = reinterpret_cast<void *>(&MgrName);
    gFileParserVT[9] = reinterpret_cast<void *>(&FileParserGetParser);
    gBaseVT[14] = reinterpret_cast<void *>(&ParseText);
    gFaceVT[1] = reinterpret_cast<void *>(&FaceAddRef);
    gFaceVT[2] = reinterpret_cast<void *>(&FaceRelease);
    gResVT[2] = reinterpret_cast<void *>(&ResRelease);
    gResVT[6] = reinterpret_cast<void *>(&ResClose);
    gResVT[8] = reinterpret_cast<void *>(&ResRead);
    gResVT[10] = reinterpret_cast<void *>(&ResSize);
    gNameVT[2] = reinterpret_cast<void *>(&NameRelease);
    gNameVT[11] = reinterpret_cast<void *>(&NameFill);
    gService.vt = gServiceVT, gMgr.vt = gMgrVT, gFileParser.vt = gFileParserVT;
    for (Obj &r : gRes) r.vt = gResVT;
    for (Obj &n : gName) n.vt = gNameVT;
    gBase.vt = gBaseVT, gBase.vb = gVb, gBase.faceVt = gFaceVT;
}

} // namespace loadfx

using loadfx::gSteps;

uint32_t Size(std::mt19937 &gen)
{
    switch (gen() % 11) {
    case 0: return 0;
    case 1: return 1;
    case 2: return 14;
    case 3: return 15;
    case 4: return 16;
    case 5: return gen() % 64;
    case 6: return gen() % 300;
    case 7: return 4094 + gen() % 4;
    case 8: return gen() % 9000;
    case 9: return 2000 + gen() % 3000;
    default: return 30 + gen() % 40;
    }
}

uint32_t OtherType(std::mt19937 &gen)
{
    static const uint32_t kOther[] = {kScriptType + 1, kScriptType - 1, kScriptType ^ 0x80000000u, kScriptType ^ 1u,
                                      kScriptType & 0xffff, 0, 0xffffffffu};
    return gen() % 3 ? kOther[gen() % std::size(kOther)] : uint32_t(gen());
}

// The world of a draw: the manager, the keys (the first `kMaxKeys` at most), the steps.
void DrawRead(uint32_t draw)
{
    std::mt19937 gen(draw * 0x9e3779b9u + 0x51);
    for (uint32_t i = 0; i < kManagerSize; i += 4) PutWord(gManager, i, gen());
    PutWord(gManager, 0x30, P(&loadfx::gFileParser));
    loadfx::gKeyCount = gen() % 3 == 0 ? gen() % 3 : gen() % (kMaxKeys + 1);
    for (uint32_t i = 0; i < kMaxKeys; ++i) {
        gKeys[i] = {gen() % 5 ? kScriptType : OtherType(gen), uint32_t(gen()), uint32_t(gen()), uint32_t(gen())};
        loadfx::Step &s = gSteps[i];
        s.openYes = gen() % 5 != 0;
        s.openNonNull = gen() % 3 != 0;
        s.readYes = gen() % 5 != 0;
        s.nameYes = gen() % 2;
        s.nameWrites = gen() % 3 != 0;
        s.res = gen() % kResources, s.size = Size(gen), s.nameIx = gen() % kNames, s.junk = uint8_t(gen());
    }
    loadfx::gHasParser = gen() % 6 != 0;
    loadfx::gOpens = loadfx::gNames = 0;
}

struct Stands {
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
};

struct Scope {
    svcfx::AllocatorScope allocator;
    SavedGlobals globals;
    Stands stands;
    Scope()
    {
        loadfx::FillTables();
        globals.Save(kServiceCache, 4);
        *GameAddress<void *>(kServiceCache) = &loadfx::gService;
        gLog.reserve(1 << 14);
    }
};

Blob Snapshot(const SavedGlobals &globals, uint32_t result, long long blocks, long long bytes)
{
    Blob o = gLog;
    o.push_back(0xeeee), o.push_back(result);
    for (uint32_t i = 0; i < kManagerSize; i += 4) o.push_back(Word<uint32_t>(gManager, i));
    for (const cLightingKey &k : gKeys)
        for (uint32_t w : {k.mType, k.mWord4, k.mWord8, k.mWordC}) o.push_back(w);
    o.push_back(loadfx::gOpens), o.push_back(loadfx::gNames);
    globals.Append(o);
    o.push_back(uint32_t(blocks)), o.push_back(uint32_t(bytes));
    return o;
}

cLightingManager *Manager() { return reinterpret_cast<cLightingManager *>(gManager); }

// ---- LoadLightingFiles 0x2df8a0 ----
namespace files {

using loadfx::Obj;

Obj gFileParserB; // a second file parser: the manager's +0x30 is swapped between the two by the fakes
constexpr uint32_t kMaxPaths = 5;

// The two option calls' paths (call 0, call 1), a bit mask of the existence answers, which calls swap the parser.
const char *gTexts[] = {"C:\\lights\\a.txt", "b.txt", "exactly15chars!", "sixteen chars!!!",
                        "D:\\a very long lighting directory name that lives on the heap\\c.txt", ""};
uint32_t gPathCount[2], gPathIx[2][kMaxPaths];
uint32_t gExists, gSwapMask, gKeyCount, gKeyJunk;
uint32_t gOptionCalls, gExistsCalls, gAddCalls;
bool gKeysFail; // the key listing leaves the vector untouched

uint32_t FileId(const void *p)
{
    if (p == &loadfx::gFileParser) return 0x9a00;
    if (p == &gFileParserB) return 0x9a01;
    return loadfx::Id(p);
}

// The allocator service as the code sees it (the cache cell 0x10c95d4 points at gAllocObj): its slots 3 and 4 log the size /
// the block's number in allocation order and forward to the scope's service. The blocks come from the general allocator's
// VirtualAlloc cores, which the AllocationCounter does not see: the list's nodes (0x1e7190's allocate, the fake option
// call's pushes), their strings and the key vector are therefore out of its count on both sides, and made observable
// here instead (the sizes and numbers of the allocations and of the frees).
struct Svc {
    void **vt;
};
Svc gAllocObj;
void *gAllocVT[8];
void *gRealSvc, **gRealVT;
void *gBlocks[96];
uint32_t gBlockCount;
uint32_t BlockId(const void *p)
{
    for (uint32_t i = gBlockCount < std::size(gBlocks) ? gBlockCount : uint32_t(std::size(gBlocks)); i-- > 0;)
        if (gBlocks[i] == p) return 0x4300 + i;
    return 0x43ff;
}
void *__fastcall SvcAllocate(Svc *, void *, uint32_t bytes)
{
    void *const p = reinterpret_cast<void *(__thiscall *)(void *, uint32_t)>(gRealVT[3])(gRealSvc, bytes);
    if (gBlockCount < std::size(gBlocks)) gBlocks[gBlockCount] = p;
    L(0x4303), L(bytes), L(0x4300 + gBlockCount++);
    return p;
}
void __fastcall SvcFree(Svc *, void *, void *p)
{
    const uint32_t id = BlockId(p);
    L(0x4304), L(id);
    if (id != 0x43ff) gBlocks[id - 0x4300] = nullptr;
    reinterpret_cast<void(__thiscall *)(void *, void *)>(gRealVT[4])(gRealSvc, p);
}

// The manager's +0x30 changes between the two parsers when the draw says so (the code reads it again for every use).
void MaybeSwap(uint32_t bit)
{
    if (gSwapMask >> (bit & 31) & 1) {
        const uint32_t now = Word<uint32_t>(gManager, 0x30);
        PutWord(gManager, 0x30, now == P(&loadfx::gFileParser) ? P(&gFileParserB) : P(&loadfx::gFileParser));
    }
}

// cTSAppCommonImpl slot 5 (0x1d0c60, both sides): logs its words, the list's header words and its node count, pushes the
// call's paths (GZStrings from the allocator service), and may swap the file parser.
uint32_t __cdecl StandPaths(uint32_t option, uint32_t list, uint32_t c, uint32_t d, uint32_t e)
{
    const uint32_t call = gOptionCalls++;
    auto *const header = reinterpret_cast<appdebugger::ListHeader *>(uintptr_t(list));
    uint32_t nodes = 0;
    for (auto *n = static_cast<uint32_t *>(header->mHead); n && n[0] != P(header->mHead) && nodes < 64; n = reinterpret_cast<uint32_t *>(n[0]))
        ++nodes;
    L(0x1d0c), L(option), L(c), L(d), L(e), L(call), L(BlockId(header->mHead)), L(header->mSize), L(nodes);
    auto *const paths = reinterpret_cast<std::list<GZString, GZServiceAllocator<GZString>> *>(uintptr_t(list));
    for (uint32_t i = 0; i < gPathCount[call & 1]; ++i) paths->push_back(GZString(gTexts[gPathIx[call & 1][i]]));
    MaybeSwap(call);
    return 1 | (call << 8);
}
bool __cdecl StandExists(const cIGZString &path)
{
    const std::string &s = reinterpret_cast<const cRZString &>(path).mString;
    const uint32_t n = gExistsCalls++;
    const bool yes = (gExists >> (n & 31) & 1) != 0;
    L(0x1242), L(Hash(s.c_str())), L(uint32_t(s.size())), L(yes);
    MaybeSwap(n + 8);
    return yes;
}
struct PathStands {
    shape::StandIn paths{0x1d0c60u, &appinit::CommonImplSlot5, &StandPaths};
    shape::StandIn exists{0x1c1242u, &cRZFile::PathExists, &StandExists};
};

// The file parsers' slots 6 (AddFile: path, flags) and 7 (RunFiles).
void __fastcall AddFile(Obj *self, void *, const char *path, uint32_t flags)
{
    L(0x9a06), L(FileId(self)), L(Hash(path)), L(uint32_t(std::strlen(path))), L(flags), L(gAddCalls++);
}
uint32_t __fastcall RunFiles(Obj *self, void *) { return L(0x9a07), L(FileId(self)), 0x7700u | gAddCalls; }

// The resource manager's slot 19 (the keys of a type: the vector and the type) and slot 3 of its service (logs).
uint32_t __fastcall MgrKeys(Obj *self, void *, cLightingKeyList *keys, uint32_t type)
{
    L(0x13), L(FileId(self)), L(P(keys->mBegin)), L(P(keys->mEnd)), L(P(keys->mCapacity)), L(type);
    if (!gKeysFail) {
        auto *const mem = static_cast<cLightingKey *>(reinterpret_cast<void *(__thiscall *)(void *, uint32_t)>(gAllocVT[3])(&gAllocObj, gKeyCount * 16 + 16));
        for (uint32_t i = 0; i < gKeyCount; ++i) mem[i] = {gKeys[i].mType, gKeys[i].mWord4, gKeys[i].mWord8, gKeys[i].mWordC};
        keys->mBegin = mem, keys->mEnd = mem + gKeyCount, keys->mCapacity = mem + gKeyCount + 1;
    }
    return 1 | (gKeyJunk << 8);
}

// ReadLightingResources 0x2d0ae0 (a stand-in on both sides): logs the manager, the vector's words and each key.
void __fastcall StandRead(cLightingManager *self, void *, cLightingKeyList *keys)
{
    L(0x2d0a), L(P(self)), L(BlockId(keys->mBegin)), L(uint32_t(keys->mEnd - keys->mBegin)),
        L(uint32_t(keys->mCapacity - keys->mBegin));
    for (cLightingKey *k = keys->mBegin; k && k != keys->mEnd; ++k) L(k->mType), L(k->mWord4), L(k->mWord8), L(k->mWordC);
}

void DrawFiles(uint32_t draw)
{
    std::mt19937 gen(draw * 0x9e3779b9u + 0x77);
    for (uint32_t i = 0; i < kManagerSize; i += 4) PutWord(gManager, i, gen());
    PutWord(gManager, 0x30, P(&loadfx::gFileParser));
    for (int c = 0; c < 2; ++c) {
        gPathCount[c] = gen() % 4 == 0 ? 0 : gen() % (kMaxPaths + 1);
        for (uint32_t &ix : gPathIx[c]) ix = gen() % std::size(gTexts);
    }
    gExists = gen(), gSwapMask = gen() % 3 == 0 ? gen() : 0;
    gKeyCount = gen() % 3 == 0 ? 0 : 1 + gen() % 4, gKeyJunk = gen() & 0xffffff;
    gKeysFail = gen() % 5 == 0;
    for (uint32_t i = 0; i < kMaxKeys; ++i) gKeys[i] = {gen(), uint32_t(gen()), uint32_t(gen()), uint32_t(gen())};
    gOptionCalls = gExistsCalls = gAddCalls = gBlockCount = 0;
    std::memset(gBlocks, 0, sizeof(gBlocks));
}

} // namespace files

} // namespace

// 0x2d0ae0: 0..7 keys of which most are the script type (others near it: one bit off, the neighbours), each opened, sized,
// read, named and parsed or not as the fakes draw; no parser at all on some draws (the service is still asked first).
DIFF_TEST(sg_lighting_load_read, 0x2d0ae0)
{
    auto original = ctx.Original<void(__thiscall *)(void *, cLightingKeyList *)>(0x2d0ae0);
    Scope scope;
    SidesAtBothPrecisions(ctx, "read", 3000, true, [&](uint32_t draw, int side) {
        DrawRead(draw);
        cLightingKeyList list{gKeys, gKeys + loadfx::gKeyCount, gKeys + kMaxKeys};
        gLog.clear();
        ctx.Mark(side ? "ours" : "original");
        difftest::AllocationCounter heap;
        if (side == 0) original(gManager, &list);
        else Manager()->ReadLightingResources(&list);
        heap.Stop();
        return Snapshot(scope.globals, 0, heap.Blocks(), heap.Bytes());
    });
}

namespace {

struct FilesScope {
    Scope base;
    files::PathStands paths;
    shape::StandIn read{0x2d0ae0u, &cLightingManager::ReadLightingResources, &files::StandRead};
    FilesScope()
    {
        using namespace files;
        gRealSvc = *GameAddress<void *>(0x10c95d4);
        gRealVT = *reinterpret_cast<void ***>(gRealSvc);
        std::memcpy(gAllocVT, gRealVT, 6 * sizeof(void *));
        gAllocVT[3] = reinterpret_cast<void *>(&SvcAllocate);
        gAllocVT[4] = reinterpret_cast<void *>(&SvcFree);
        gAllocObj.vt = gAllocVT;
        *GameAddress<void *>(0x10c95d4) = &gAllocObj; // AllocatorScope puts the real one back
        loadfx::gMgrVT[19] = reinterpret_cast<void *>(&MgrKeys);
        loadfx::gFileParserVT[6] = reinterpret_cast<void *>(&AddFile);
        loadfx::gFileParserVT[7] = reinterpret_cast<void *>(&RunFiles);
        gFileParserB.vt = loadfx::gFileParserVT;
    }
};

} // namespace

// 0x2df8a0: the option's directory list read twice into one list (call 0 with the mask's complement, call 1 with the mask),
// the existing paths added last first after each read, the files run, the script keys listed and read, the key vector and
// the list freed.
DIFF_TEST(sg_lighting_load_files, 0x2df8a0)
{
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x2df8a0);
    FilesScope scope;
    SidesAtBothPrecisions(ctx, "files", 3000, true, [&](uint32_t draw, int side) {
        files::DrawFiles(draw);
        gLog.clear();
        ctx.Mark(side ? "ours" : "original");
        difftest::AllocationCounter heap;
        if (side == 0) original(gManager);
        else Manager()->LoadLightingFiles();
        heap.Stop();
        return Snapshot(scope.base.globals, 0, heap.Blocks(), heap.Bytes());
    });
}
