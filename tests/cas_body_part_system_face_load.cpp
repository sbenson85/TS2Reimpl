// Differential test for cITSCASBodyPartSystem slot 4 (0xcf3750, LoadFaceData): the FACE resource load.
//
// The world: the system object (0x168 bytes between canaries) with a real records vector (our STL on the allocator
// service fake) and a heap map header at +0x20 whose erase (0xcdbf10) is a recording stand-in; scripted fakes for
// everything the load reaches:
//   - the directory list (cTSAppCommonImpl slot 5, 0x1d0c60, ours: stood in on both sides) gets 0..4 directories;
//   - cRZFile::PathExists (0x1c1242) and cRZFileName's constructor, GetDrivePath and destructor (0x1c7ef4, 0x1c87c7,
//     0x1c0eb2; ours, stood in on both sides: the real ones resolve paths through the file system) answer from the script;
//   - the persist service (cache 0x10faf6c): slot 3 the resource manager, slot 21 opens a segment (slot 4 its group);
//   - the manager's slot 39 finds the FACE resource or not (setting the out pointer either way, or not);
//   - the resource's stream reads scripted uint32s, strings and floats;
//   - 0xce0b30 (the record adder, original) and the face's slot 64 record their arguments.
// After each call: the event log (texts as their contents), the records vector, the object's bytes, the allocation log
// of the allocator service fake, the heap frees and the canaries.
#include "difftest.h"
#include "code_patch.h"
#include "app/AppDebugger.h"
#include "app/AppInit.h"
#include "cas/cTSCASBodyPartSystem.h"
#include "game/game.h"
#include "rz/cRZFile.h"
#include "rz/cRZFileName.h"
#include "rz/cRZString.h"

#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace nTSCASCore;
using Sys = cTSCASBodyPartSystem;

constexpr uint32_t kSystemVTable = 0xeba13c, kSystemVTable4 = 0xeba17c, kFaceVTable = 0xeba198;
constexpr int kFaceSlots = 89, kSystemSlots = 15, kSystem4Slots = 6;

// Events: a tag and words, texts appended as their characters.
using Event = std::vector<uint32_t>;
std::vector<Event> gLog;
uint8_t *gBase = nullptr;

uint32_t Rel(const void *p)
{
    if (!p) return 0xeeee0000u;
    const uintptr_t d = reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(gBase);
    return d < 0x168 ? static_cast<uint32_t>(d) : 0xeeee0001u;
}
void PushText(Event &e, const char *text)
{
    e.push_back(text ? static_cast<uint32_t>(std::strlen(text)) : 0xffffffffu);
    if (text) {
        for (const char *c = text; *c; ++c) e.push_back(static_cast<uint8_t>(*c));
    }
}

// ---- the allocator service fake (slot 3 allocates, slot 4 frees; both logged with the block's size) -----------------

std::map<void *, uint32_t> gBlocks;
void *__fastcall SvcAlloc(void *, void *, uint32_t size)
{
    void *p = std::malloc(size ? size : 1);
    std::memset(p, 0xcd, size);
    gBlocks[p] = size;
    gLog.push_back({'A', size});
    return p;
}
void __fastcall SvcFree(void *, void *, void *p)
{
    auto it = gBlocks.find(p);
    uint32_t size = 0xdead;
    if (it != gBlocks.end()) {
        size = it->second;
        gBlocks.erase(it);
    }
    gLog.push_back({'F', size});
    std::free(p);
}
void __fastcall SvcOther(void *, void *) { gLog.push_back({'?'}); }
const void *gSvcVt[8] = {&SvcOther, &SvcOther, &SvcOther, reinterpret_cast<const void *>(&SvcAlloc),
                         reinterpret_cast<const void *>(&SvcFree), &SvcOther, &SvcOther, &SvcOther};
const void *gSvcObject[2] = {gSvcVt, nullptr};

// ---- the script ------------------------------------------------------------------------------------------------------

struct Directory {
    std::string path;
    bool exists;
    uint32_t group;
    int lookup;      // 0 found (out set), 1 not found (out null), 2 not found but the out pointer set anyway
    std::vector<uint32_t> words; // the stream's uint32s and floats, in read order
    std::vector<std::string> texts;
    size_t nextWord = 0, nextText = 0; // read positions (vectors: no container frees while frees are recorded)
};
struct Script {
    std::vector<Directory> dirs;
};
Script gScript;
size_t gDir = 0; // the directory the fakes are serving (set by PathExists)

const char *const kWords[] = {"face", "chin", "nose_tip", "brow", "", "a_rather_long_region_name_here", "jaw", "x"};
std::string RandomText(std::mt19937 &rng) { return kWords[rng() % 8]; }

Script MakeScript(uint32_t seed)
{
    std::mt19937 rng(seed);
    Script s;
    const unsigned n = rng() % 5;
    for (unsigned i = 0; i < n; ++i) {
        Directory d;
        d.path = std::string("C:\\dir") + static_cast<char>('a' + i); // short: no heap buffer in the list's strings
        d.exists = rng() % 4 != 0;
        d.group = static_cast<uint32_t>(rng());
        d.lookup = static_cast<int>(rng() % 3);
        if (rng() % 3 == 0) d.lookup = 0;
        d.words.push_back(static_cast<uint32_t>(rng())); // version
        const uint32_t records = rng() % 4;
        d.words.push_back(records);
        for (uint32_t r = 0; r < records; ++r) {
            for (int k = 0; k < 4; ++k) d.texts.push_back(RandomText(rng));
        }
        const uint32_t morphs = rng() % 4;
        d.words.push_back(morphs);
        for (uint32_t m = 0; m < morphs; ++m) {
            d.words.push_back(static_cast<uint32_t>(rng()));
            d.words.push_back(static_cast<uint32_t>(rng()));
            d.words.push_back(static_cast<uint32_t>(rng())); // float bits
            d.words.push_back(static_cast<uint32_t>(rng()));
            d.texts.push_back(RandomText(rng));
            d.texts.push_back(RandomText(rng));
        }
        s.dirs.push_back(d);
    }
    return s;
}
Script gRunning; // the copy the current side consumes

// ---- stand-ins for our replacements and the image's callees -------------------------------------------------------------

struct DirectoryNode {
    DirectoryNode *mNext;
    DirectoryNode *mPrev;
    std::string mText;
};

uint32_t __cdecl DirectoriesStand(uint32_t query, uint32_t list, uint32_t c, uint32_t d, uint32_t e)
{
    gLog.push_back({0x1d0c60, query, c, d, e});
    auto *header = reinterpret_cast<appdebugger::ListHeader *>(static_cast<uintptr_t>(list));
    auto *sentinel = static_cast<DirectoryNode *>(header->mHead);
    for (const Directory &dir : gRunning.dirs) {
        auto *node = static_cast<DirectoryNode *>(appdebugger::AllocateListNodes(1, 0));
        ::new (&node->mText) std::string(dir.path);
        node->mNext = sentinel;
        node->mPrev = sentinel->mPrev;
        sentinel->mPrev->mNext = node;
        sentinel->mPrev = node;
        ++header->mSize;
    }
    return 1;
}

bool __cdecl PathExistsStand(const cIGZString &path)
{
    const auto &text = reinterpret_cast<const cRZString &>(path).mString;
    Event e{0x1c1242};
    PushText(e, text.c_str());
    gLog.push_back(e);
    for (size_t i = 0; i < gRunning.dirs.size(); ++i) {
        if (gRunning.dirs[i].path == text) {
            gDir = i;
            return gRunning.dirs[i].exists;
        }
    }
    return false;
}

cRZFileName *__fastcall FileNameConstructStand(cRZFileName *self, void *, const cRZString *path)
{
    Event e{0x1c7ef4};
    PushText(e, path->mString.c_str());
    gLog.push_back(e);
    self->mDrive.Construct("C:");
    self->mDir.Construct("\\");
    self->mName.Construct((path->mString.substr(3) + "_name").c_str());
    self->mExt.Construct();
    self->mFull.Construct(path->mString.c_str());
    return self;
}
cRZString *__fastcall DrivePathStand(cRZFileName *self, void *, cRZString *out)
{
    gLog.push_back({0x1c87c7});
    out->Construct((self->mFull.mString + "\\").c_str());
    return out;
}
void __fastcall FileNameDestructStand(cRZFileName *self, void *)
{
    gLog.push_back({0x1c0eb2});
    self->mFull.Destruct();
    self->mExt.Destruct();
    self->mName.Destruct();
    self->mDir.Destruct();
    self->mDrive.Destruct();
}

void __fastcall EraseNames(void *self, void *, void *tree, void *root)
{
    gLog.push_back({0xcdbf10, Rel(self), Rel(tree), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(root))});
}

void __fastcall AddRecordStand(void *self, void *, const char *a, const char *b, const char *c, const char *d)
{
    Event e{0xce0b30, Rel(self)};
    for (const char *t : {a, b, c, d}) PushText(e, t);
    gLog.push_back(e);
}

void __fastcall MorphStand(void *self, void *, uint32_t a, uint32_t b, const char *s1, const char *s2, float x, float y)
{
    uint32_t xb, yb;
    std::memcpy(&xb, &x, 4);
    std::memcpy(&yb, &y, 4);
    Event e{64, Rel(self), a, b, xb, yb};
    PushText(e, s1);
    PushText(e, s2);
    gLog.push_back(e);
}

// ---- the persist service, manager, segment, resource and stream fakes ----------------------------------------------------

template <typename F> const void *Fn(F f) { return reinterpret_cast<const void *>(f); }
void __fastcall Trap(void *, void *) { gLog.push_back({0x7a7}); }

const void *gManagerVt[48];
const void *gManager[1] = {gManagerVt};
const void *gPersistVt[32];
const void *gPersist[1] = {gPersistVt};
const void *gSegmentVt[8];
const void *gSegment[1] = {gSegmentVt};
const void *gResourceVt[8];
const void *gResource[1] = {gResourceVt};
const void *gStreamVt[32];
const void *gStream[1] = {gStreamVt};

void *__fastcall PersistManager(void *, void *)
{
    gLog.push_back({3});
    return gManager;
}
uint32_t __fastcall OpenSegment(void *, void *, uint32_t iid, void **out, uint32_t mode, const char *drive, const char *name,
                                uint32_t f1, uint32_t f2)
{
    Event e{21, iid, *out == nullptr ? 0u : 0xbadu, mode, f1, f2};
    PushText(e, drive);
    PushText(e, name);
    gLog.push_back(e);
    *out = gSegment;
    return 1;
}
uint32_t __fastcall SegmentGroup(void *, void *)
{
    gLog.push_back({0x5004});
    return gRunning.dirs[gDir].group;
}
uint32_t __fastcall SegmentRelease(void *, void *)
{
    gLog.push_back({0x5002});
    return 0;
}
uint8_t __fastcall GetResource(void *, void *, const uint32_t *key, void **out, uint32_t flags)
{
    gLog.push_back({39, key[0], key[1], key[2], key[3], *out == nullptr ? 0u : 0xbadu, flags});
    const int lookup = gRunning.dirs[gDir].lookup;
    if (lookup != 1) *out = gResource;
    return lookup == 0;
}
uint32_t __fastcall ResourceQuery(void *, void *, uint32_t iid, void **out)
{
    gLog.push_back({0x6000, iid, *out == nullptr ? 0u : 0xbadu});
    *out = gStream;
    return 1;
}
uint32_t __fastcall ResourceDone(void *, void *)
{
    gLog.push_back({0x6006});
    return 1;
}
uint32_t __fastcall ResourceRelease(void *, void *)
{
    gLog.push_back({0x6002});
    return 0;
}
uint32_t NextWord()
{
    Directory &d = gRunning.dirs[gDir];
    return d.nextWord < d.words.size() ? d.words[d.nextWord++] : 0;
}
uint8_t __fastcall ReadUint32(void *, void *, uint32_t *out)
{
    gLog.push_back({0x7013}); // not the previous value: the original reads the morph numbers into stack slots it never
                              // initialises (ebp-0xa4, ebp-0xa0, ebp-0x9c, ebp-0x98), so the first morph's are garbage
    *out = NextWord();
    return 1;
}
uint8_t __fastcall ReadFloat(void *, void *, float *out)
{
    gLog.push_back({0x7021}); // as ReadUint32
    const uint32_t w = NextWord();
    std::memcpy(out, &w, 4);
    return 1;
}
uint8_t __fastcall ReadString(void *, void *, cIGZString *out)
{
    Directory &d = gRunning.dirs[gDir];
    auto &target = reinterpret_cast<cRZString *>(out)->mString;
    Event e{0x7024};
    PushText(e, target.c_str());
    gLog.push_back(e);
    if (d.nextText < d.texts.size()) target.assign(d.texts[d.nextText++]);
    else target.clear();
    return 1;
}
uint32_t __fastcall StreamRelease(void *, void *)
{
    gLog.push_back({0x7002});
    return 0;
}

void BuildFakes()
{
    for (auto &p : gManagerVt) p = Fn(&Trap);
    for (auto &p : gPersistVt) p = Fn(&Trap);
    for (auto &p : gSegmentVt) p = Fn(&Trap);
    for (auto &p : gResourceVt) p = Fn(&Trap);
    for (auto &p : gStreamVt) p = Fn(&Trap);
    gPersistVt[3] = Fn(&PersistManager);
    gPersistVt[21] = Fn(&OpenSegment);
    gSegmentVt[4] = Fn(&SegmentGroup);
    gSegmentVt[2] = Fn(&SegmentRelease);
    gManagerVt[39] = Fn(&GetResource);
    gResourceVt[0] = Fn(&ResourceQuery);
    gResourceVt[6] = Fn(&ResourceDone);
    gResourceVt[2] = Fn(&ResourceRelease);
    gStreamVt[13] = Fn(&ReadUint32);
    gStreamVt[21] = Fn(&ReadFloat);
    gStreamVt[24] = Fn(&ReadString);
    gStreamVt[2] = Fn(&StreamRelease);
}

// ---- the object ------------------------------------------------------------------------------------------------------

struct Side {
    uint8_t front[32];
    alignas(16) uint8_t object[0x168];
    uint8_t back[32];
    const void *systemVt[kSystemSlots];
    const void *system4Vt[kSystem4Slots];
    const void *faceVt[kFaceSlots];
    TreeHeader::Node namesHead;
    TreeHeader names;
    Sys *S() { return reinterpret_cast<Sys *>(object); }
};
Side gSides[2];

struct SavedWord {
    uint32_t *at;
    uint32_t value;
    SavedWord(uint32_t rva) : at(GameAddress<uint32_t>(rva)), value(*at) {}
    ~SavedWord() { *at = value; }
};

} // namespace

DIFF_TEST(cTSCASBodyPartSystem_LoadFaceData, 0xcf3750)
{
    using Fn4 = void(__thiscall *)(void *);
    auto orig = ctx.Original<Fn4>(0xcf3750);
    BuildFakes();
    SavedWord cache(0x10c95d4), guard(0x10c95d8), persist(0x10faf6c);
    *GameAddress<const void *>(0x10c95d4) = gSvcObject;
    *GameAddress<int32_t>(0x10c95d8) = INT32_MIN;
    *GameAddress<const void *>(0x10faf6c) = gPersist;
    shape::StandIn dirs(0x1d0c60, &appinit::CommonImplSlot5, &DirectoriesStand);
    shape::StandIn exists(0x1c1242, &cRZFile::PathExists, &PathExistsStand);
    shape::StandIn fnCtor(0x1c7ef4, &cRZFileName::Construct, &FileNameConstructStand);
    shape::StandIn fnDrive(0x1c87c7, &cRZFileName::GetDrivePath, &DrivePathStand);
    shape::StandIn fnDtor(0x1c0eb2, &cRZFileName::Destruct, &FileNameDestructStand);
    shape::CodePatch erase(0xcdbf10, Fn(&EraseNames));
    shape::CodePatch add(0xce0b30, Fn(&AddRecordStand));

    for (uint32_t seed = 1; seed <= 400; ++seed) {
        gScript = MakeScript(seed);
        std::vector<Event> logs[2];
        std::vector<uint32_t> states[2];
        std::vector<size_t> frees[2];
        bool canaries[2];
        for (int k = 0; k < 2; ++k) {
            Side &s = gSides[k];
            std::mt19937 rng(seed * 31 + 7);
            std::memset(s.front, 0x5a, sizeof(s.front));
            std::memset(s.back, 0xa5, sizeof(s.back));
            for (auto &b : s.object) b = static_cast<uint8_t>(rng());
            std::memcpy(s.systemVt, ctx.image.At<void>(kSystemVTable), sizeof(s.systemVt));
            std::memcpy(s.system4Vt, ctx.image.At<void>(kSystemVTable4), sizeof(s.system4Vt));
            std::memcpy(s.faceVt, ctx.image.At<void>(kFaceVTable), sizeof(s.faceVt));
            s.faceVt[64] = Fn(&MorphStand);
            Sys *sys = s.S();
            *reinterpret_cast<const void **>(reinterpret_cast<uint8_t *>(sys) + 0x0) = s.systemVt;
            *reinterpret_cast<const void **>(reinterpret_cast<uint8_t *>(sys) + 0x4) = s.system4Vt;
            *reinterpret_cast<const void **>(reinterpret_cast<uint8_t *>(sys) + 0x1c) = s.faceVt;
            gBase = s.object;
            // The records (null or a vector with 0..4 records, capacity up to 6) and the name map (null or a header).
            using Records = std::vector<SurfaceRecord, CasAlloc<SurfaceRecord>>;
            Records *records = nullptr;
            if (rng() % 4) {
                records = new Records();
                const unsigned cap = rng() % 7, size = cap ? rng() % (cap + 1) : 0;
                records->reserve(cap);
                for (unsigned i = 0; i < size; ++i) {
                    SurfaceRecord r;
                    for (auto &t : r.mText) t = RandomText(rng);
                    records->push_back(r);
                }
            }
            sys->mRecords = records;
            const uint32_t token = rng() % 1000;
            s.namesHead = {reinterpret_cast<TreeHeader::Node *>(static_cast<uintptr_t>(0x1110000u + token)),
                           reinterpret_cast<TreeHeader::Node *>(static_cast<uintptr_t>(0x2220000u + token)),
                           reinterpret_cast<TreeHeader::Node *>(static_cast<uintptr_t>(0x3330000u + token))};
            s.names = {&s.namesHead, rng() % 30};
            sys->mTree20 = rng() % 4 ? &s.names : nullptr;

            gRunning = gScript;
            gDir = 0;
            gLog.clear();
            gLog.reserve(4096);
            {
                difftest::FreeRecorder rec;
                if (k == 0) orig(sys->Iface());
                else sys->Iface()->LoadFaceData();
                rec.Stop();
                std::vector<size_t> sizes(rec.Count() + 1);
                sizes.resize(rec.SortedSizes(sizes.data(), sizes.size()));
                frees[k] = sizes;
            }
            logs[k] = gLog;
            auto &d = states[k];
            d.push_back(*reinterpret_cast<const void *const *>(reinterpret_cast<const uint8_t *>(sys) + 0x0) == s.systemVt);
            d.push_back(*reinterpret_cast<const void *const *>(reinterpret_cast<const uint8_t *>(sys) + 0x4) == s.system4Vt);
            d.push_back(*reinterpret_cast<const void *const *>(reinterpret_cast<const uint8_t *>(sys) + 0x1c) == s.faceVt);
            d.push_back(sys->mRecords == records);
            if (records) {
                d.push_back(static_cast<uint32_t>(records->size()));
                d.push_back(static_cast<uint32_t>(records->capacity()));
            }
            d.push_back(sys->mTree20 == nullptr ? 0u : sys->mTree20 == &s.names ? 1u : 2u);
            d.push_back(s.names.mSize);
            d.push_back(s.namesHead.mLeft == &s.namesHead);
            d.push_back(s.namesHead.mParent == &s.namesHead);
            d.push_back(s.namesHead.mRight == &s.namesHead);
            for (uint32_t off = 0; off < 0x168; ++off) {
                if (off < 8 || (off >= 0x1c && off < 0x28)) continue; // vtables, the two pointers compared above
                d.push_back(s.object[off]);
            }
            canaries[k] = true;
            for (int i = 0; i < 32; ++i) canaries[k] = canaries[k] && s.front[i] == 0x5a && s.back[i] == 0xa5;
            delete records;
        }
        ++ctx.cases;
        if (logs[0] != logs[1]) {
            size_t i = 0;
            while (i < logs[0].size() && i < logs[1].size() && logs[0][i] == logs[1][i]) ++i;
            ctx.Fail("seed %u: events differ at %zu of %zu/%zu (original %x, ours %x)", seed, i, logs[0].size(), logs[1].size(),
                     i < logs[0].size() ? logs[0][i][0] : 0u, i < logs[1].size() ? logs[1][i][0] : 0u);
        }
        if (states[0] != states[1]) ctx.Fail("seed %u: object state differs", seed);
        if (frees[0] != frees[1]) ctx.Fail("seed %u: heap frees differ (%zu, %zu)", seed, frees[0].size(), frees[1].size());
        if (!canaries[0] || !canaries[1]) ctx.Fail("seed %u: canaries overwritten", seed);
    }
}
