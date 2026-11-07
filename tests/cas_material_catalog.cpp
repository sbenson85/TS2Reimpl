// Differential test for cTSMaterialCatalog (lane JX; src/cas/cTSMaterialCatalog.h): the class in the standalone shape.
//
// MaterialCatalog_Run: a catalog built by the original constructor (0xd04bd0, game vtables 0xebab54 / 0xebab8c / 0xebaba8 /
// 0xebabe0) or the original creator (0xd06930) against one built by ours (the constructor hook's adapter, or Create), each
// filled with the same state (written through our layout on both sides: the trees are MSVC 14.40 containers like the game's,
// the lists allocator-service vectors) and driven through its own vtables by the same random sequence of operations, then
// destroyed by the last Release (cRZUnknownMT's Release through the +4 face's slot 3: the thunk 0xd0508f, the deleting
// destructor 0xd050c0, the destructor 0xd04e10). Operations (every slot of every face the class defines): QueryInterface
// (0xd06f80; through the +4, +0x1c and +0x20 faces the thunks 0xd06fb1 / 0xd06fb9 / 0xd06fc1) / AddRef / Release, Init
// (0xd069b0; with OpenDefaultSegments 0xd08060; the factory's last Release seen at the game's operator delete), Shutdown
// (0xd07eb0), DoMessage (0xd06250), and the cITSMaterialCatalog slots 3..12: AddMaterial 0xd05140, RemoveEntry 0xd05ec0,
// Load 0xd06ba0 and LoadSegment 0xd06dc0 (their key loader 0xd06d00, and cTSMaterialFilter: IsKeyIncluded 0xd06b70, the
// deleting destructor 0xd050e0, the folded QueryInterface 0xb7b970 / Release 0x72c480), ClearAll 0xd07c50, GetByName
// 0xd071f0, GetByGroup 0xd07340 (both through AppendMatching 0xd07000 / AppendNameMap 0xd07170), AddEntry 0xd05250,
// OpenSegment 0xd05790, CreateSegment 0xd05900.
//
// Every object the catalog reaches is a recording fake (tests/ts_services_fixture.h): the framework (cell 0x10c9378, also the
// message server's source), the persist system (cache 0x10faf6c) and its resource manager, the COM object (the director's
// slot 12), the message server, a message, segments, the new segment CreateSegment makes and its label table, resources and
// their entry faces. Stood in for on both sides: PathExists (0x1c1242), DirectoryExists (0x1c4f94) and the content
// directory list (0x1d0c60, CommonImplSlot5), each logging what it is handed and answering a drawn value. The game's operator
// new / delete (0x824632 / 0x824624) are ours, so nodes either side allocates may be freed by the other's code.
//
// Compared after every operation: the result (al where the slot returns a bool), the event log (with the strings handed to
// fakes), every field (trees in order with their keys; lists by contents, mNames by which group list it names), the
// out-parameter, and every fake's count and words; at the end the counts after the destruction.
//
// Boundaries: lists of 0..4 entries (AddEntry's duplicate search over each position, RemoveEntry's shift from each position,
// the out list's growth from capacity 0..5), 0..3 content directories, key lists of 0..4 keys, name maps without groups,
// null name maps and null lists.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "app/AppInit.h"
#include "app/AppServices2.h"
#include "cas/cTSMaterialCatalog.h"
#include "game/game.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"

#include <cstring>
#include <map>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace matcat_test {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{11, "ddddddd"}, {12, "ddddddd"}, {13, "ddddddd"}, {14, "ddddddd"}, {15, "ddd"}, {16, "ddd"}, {17, "ddd"}, {18, "ddd"}};

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Sys = cTSMaterialCatalog;
using KeyList = std::vector<MaterialKey, GZServiceAllocator<MaterialKey>>;

constexpr uint32_t kTag = 0x7d000000;    // the catalog
constexpr uint32_t kKeyTag = 0x6b000000; // the key pool
constexpr uint32_t kWordsTag = 0x6c000000; // the slot 14 key words

enum : unsigned {
    kFw = 0, kMgr, kSys, kCom, kMsgSrv, kMessage,
    kSeg0 = 6,   // 6..8
    kNewSeg = 9, kLabels = 10,
    kRes0 = 11,  // 11..14
    kEnt0 = 15,  // 15..18
};
constexpr unsigned kSegs = 3, kRes = 4;

MaterialKey gKeys[8];
uint32_t gWords[3][4] = {{1, 2, 3, 4}, {1, 2, 3, 5}, {9, 2, 3, 4}};
std::mt19937 gDraw;
uint32_t Draw(uint32_t n) { return uint32_t(gDraw() % n); }
bool gNoManager;   // the persist system has no resource manager (Init's failure)
bool gNoMsgServer; // the framework answers no message server (Shutdown's quiet case)

const char *const kNames[] = {"Skin", "skin_cres", "SKIN_CRES", "hair", "hair_cres_cres", "", "x_cre"};
constexpr unsigned kNameCount = 7;
const char *const kSeconds[] = {"a", "A", "b", ""};
const char *const kTexts[] = {"t1", "T1", "t2"};
const char *const kDirs[] = {"C:\\Game\\Mat\\", "C:\\Dl\\", "D:\\Ep1\\Data\\"};
const char *const kPaths[] = {"C:\\Game\\Mat\\materials.package", "C:\\Dl\\other.package", "D:\\x.dat"};

void LogKeyAt(uint32_t at)
{
    const auto *k = reinterpret_cast<const MaterialKey *>(uintptr_t(at));
    Ev(k->type);
    Ev(k->group);
    Ev(k->instance);
    Ev(k->resource);
}
void LogText(const char *text, size_t n)
{
    Ev(uint32_t(n));
    for (size_t i = 0; i < n; ++i) Ev(uint8_t(text[i]));
}
void LogRZ(uint32_t at)
{
    const auto *s = reinterpret_cast<const cRZString *>(uintptr_t(at));
    LogText(s->mString.data(), s->mString.size());
}
uint32_t Self(const void *p) { return svcfx::NormOut(P(p)); }

template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}

void HandOut(uint32_t at, unsigned which)
{
    ++F(which).refs;
    *reinterpret_cast<void **>(uintptr_t(at)) = &F(which);
}

// ---- fake slots ----

uint32_t DrawBool(Obj *, uint32_t *) { return 0x100u | Draw(2); }
uint32_t LogKeyBool(Obj *, uint32_t *a)
{
    LogKeyAt(a[0]);
    return 0x100u | Draw(2);
}
uint32_t LogKeyVoid(Obj *, uint32_t *a)
{
    LogKeyAt(a[0]);
    return 0;
}
uint32_t GetService(Obj *, uint32_t *a) // the framework's slot 5: (id, iid, &out)
{
    if (a[0] == 0x056b906e && a[1] == 0x9af5e924) return HandOut(a[2], kSys), 1;
    if (a[0] == 0x04fa845b && a[1] == 0x652294c7 && !gNoMsgServer) return HandOut(a[2], kMsgSrv), 1;
    return 0;
}
uint32_t ManagerOf(Obj *, uint32_t *) { return gNoManager ? 0 : P(&F(kMgr)); } // the persist system's slot 3
uint32_t GetResource(Obj *, uint32_t *a) // the manager's slot 3: (key, iid, &out, 0, 0)
{
    LogKeyAt(a[0]);
    const unsigned draw = Draw(5);
    if (draw == 0) return 0x200;
    HandOut(a[2], kRes0 + Draw(kRes));
    return draw == 1 ? 0x100 : 1; // now and then a refusal that still hands out a counted resource
}
uint32_t FindSegment(Obj *, uint32_t *a) // the manager's slot 34: (key, &segment), counted
{
    LogKeyAt(a[0]);
    if (Draw(4) == 0) return 0x200;
    HandOut(a[1], kSeg0 + Draw(kSegs));
    return 1;
}
uint32_t KeyInto(Obj *self, uint32_t *a) // a resource's slot 3: the key into *a[0]
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), self->value, sizeof(MaterialKey));
    return a[0];
}
uint32_t KeyFrom(Obj *self, uint32_t *a) // a resource's slot 4: the key taken
{
    LogKeyAt(a[0]);
    std::memcpy(self->value, reinterpret_cast<const void *>(uintptr_t(a[0])), sizeof(MaterialKey));
    return 1;
}
uint32_t Kind(Obj *self, uint32_t *) { return Draw(4) == 0 ? Draw(3) : self->value[4]; } // a resource's slot 12
uint32_t Text(Obj *self, uint32_t *) { return P(kTexts[self->value[5]]); }             // a resource's slot 13
uint32_t Words(Obj *self, uint32_t *) { return P(gWords[self->value[6]]); }            // a resource's slot 14
uint32_t EntryFace(Obj *self, uint32_t *a) // a resource's slot 0: (iid, &out): its entry face, counted
{
    if (Draw(5) == 0) return 0;
    HandOut(a[1], kEnt0 + (self->id - 200 - kRes0));
    return 1;
}
uint32_t Group(Obj *self, uint32_t *) { return self->value[0]; } // an entry's slot 4
uint32_t NameOut(Obj *self, uint32_t *a)                           // an entry's slot 5: its name by value
{
    return P(::new (reinterpret_cast<void *>(uintptr_t(a[0]))) GZString(kNames[self->value[1]]));
}
uint32_t SecondOut(Obj *self, uint32_t *a) // an entry's slot 6
{
    return P(::new (reinterpret_cast<void *>(uintptr_t(a[0]))) GZString(kSeconds[self->value[2]]));
}
uint32_t NewGuid(Obj *, uint32_t *a) // the COM object's slot 7
{
    *reinterpret_cast<uint32_t *>(uintptr_t(a[0])) = 0x1000 + Draw(3);
    return 1;
}
uint32_t ClassObject(Obj *, uint32_t *a) // the COM object's slot 3: (class, iid, &out)
{
    if (Draw(4) == 0) return 0x200;
    HandOut(a[2], kNewSeg);
    return 0x101;
}
uint32_t SegmentFace(Obj *, uint32_t *a) // the new segment's slot 0: (iid, &out)
{
    if (Draw(4) == 0) return 0x200;
    HandOut(a[1], kSeg0 + Draw(kSegs));
    return 0x101;
}
uint32_t LogString1(Obj *, uint32_t *a) // (cIGZString *)
{
    LogRZ(a[0]);
    return 0x100u | Draw(2);
}
uint32_t LogLabel(Obj *, uint32_t *a) // the label table's slot 3: (type, cIGZString *)
{
    LogRZ(a[1]);
    return 1;
}
uint32_t OpenSeg(Obj *, uint32_t *a) // the persist system's slot 22: (iid, &out, mode, directory, name, 1, 1)
{
    LogRZ(a[3]);
    LogRZ(a[4]);
    const unsigned draw = Draw(4);
    if (draw == 0) return 0x200;
    HandOut(a[1], kSeg0 + Draw(kSegs));
    return draw == 1 ? 0x200 : 0x101;
}
uint32_t MessageFace(Obj *, uint32_t *a) // the message's slot 0
{
    if (Draw(3) == 0) return 0;
    HandOut(a[1], kMessage);
    return 1;
}
uint32_t MessageType(Obj *, uint32_t *) { return Draw(3) ? 0xcde2cc2d : 0x1234u; }

// The key lists (the manager's slot 18 and a segment's slot 19, (keys, filter)): the filter asked about the material type
// and two others, counted and released through its own slots; then keys from the pool appended, the count answered. The
// filter is held (one more reference) until the operation returns: then its count is logged and the hold released, which
// deletes it on both sides.
void *gHeldFilter;
size_t gKeyListRange;                     // its entry in the fixture's ranges
constexpr uint32_t kListTag = 0x6d000000; // the last key list handed out
uint32_t __fastcall KeyListOf(Obj *self, void *, KeyList *keys, void *filter)
{
    if (!gHeldFilter) {
        gHeldFilter = filter;
        Slot<uint32_t>(filter, 1);
    }
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(uint32_t(keys->size()));
    Ev(reinterpret_cast<const uint32_t *>(filter)[1]); // its count
    static const uint32_t kTypes[] = {0x4c697e5a, 0, 0x4c697e5b};
    for (uint32_t type : kTypes) {
        const uint32_t key[4] = {type, 1, 2, 3};
        Ev(Slot<uint32_t>(filter, 3, static_cast<const uint32_t *>(key)) & 0xff);
    }
    void *q = nullptr;
    static const uint32_t kIids[] = {1, 0x8865cd89, 0x12345678};
    const uint32_t ok = Slot<uint32_t>(filter, 0, kIids[Draw(3)], &q) & 0xff;
    Ev(ok);
    Ev(q == filter);
    if (ok) Ev(Slot<uint32_t>(filter, 2));
    const unsigned n = Draw(5);
    for (unsigned i = 0; i < n; ++i) keys->push_back(gKeys[Draw(8)]);
    // The keys' block (allocator-service heap: another address on each side) is logged by offset (the loader hands each
    // key's address on to GetResource).
    svcfx::gRanges[gKeyListRange] = {uintptr_t(keys->data()), uintptr_t(keys->data() + keys->size()), kListTag};
    return n;
}

// The manager's slot 20, (kind, type, factory): the factory by its contents and whether it is the one the last call got.
const void *gLastFactory;
const void *gFactory;
void __cdecl GameDelete(void *p, size_t size)
{
    if (p && p == gFactory) {
        Ev(0xdead0000u | uint32_t(size));
        gFactory = nullptr;
    }
    ::operator delete(p);
}
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
uint32_t __fastcall RegisterFactory(Obj *self, void *, uint32_t kind, uint32_t type, const uint32_t *factory)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(20);
    Ev(kind);
    Ev(type);
    Ev(factory == gLastFactory);
    gLastFactory = factory;
    gFactory = factory;
    Ev(factory[0] - P(GameAddress<void>(0)));
    Ev(factory[1] - P(GameAddress<void>(0)));
    Ev(factory[2]);
    return Draw(3) == 0 ? 0x300u : 1u;
}

// ---- stand-ins ----

bool __cdecl StandPathExists(const cIGZString &path)
{
    Ev(0x1c1242);
    LogRZ(P(&path));
    return Draw(3) != 0;
}
bool __cdecl StandDirectoryExists(cRZString *path)
{
    Ev(0x1c4f94);
    LogRZ(P(path));
    return Draw(3) != 0;
}
unsigned gDirCount;
uint32_t __cdecl StandDirectories(uint32_t option, uint32_t list, uint32_t a, uint32_t b, uint32_t c)
{
    Ev(0x1d0c60);
    Ev(option);
    Ev(a);
    Ev(b);
    Ev(c);
    auto *dirs = reinterpret_cast<GZStringList *>(uintptr_t(list));
    for (unsigned i = 0; i < gDirCount; ++i) dirs->push_back(GZString(kDirs[(i + gDirCount) % 3]));
    return 1;
}

// ---- the world for a seed ----

void ConfigureFakes(uint32_t seed)
{
    std::mt19937 r(seed * 7 + 3);
    svcfx::ResetAll();
    svcfx::AddRange(gKeys, sizeof(gKeys), kKeyTag);
    svcfx::AddRange(gWords, sizeof(gWords), kWordsTag);
    gKeyListRange = svcfx::gRanges.size();
    svcfx::AddRange(nullptr, 0, kListTag);
    svcfx::gFrameworkIndex = kCom;
    F(kFw).Set(5, "pdp", 0, &GetService);
    F(kSys).Set(3, 0, 0, &ManagerOf);
    F(kSys).Set(22, "dpdppdd", 0, &OpenSeg);
    F(kMgr).Set(3, "pdp--", 0, &GetResource);
    F(kMgr).Set(8, "p", 0u);
    F(kMgr).Set(10, "p", 0, &LogKeyVoid);
    F(kMgr).Set(11, "p", 0, &LogKeyBool);
    F(kMgr).Set(13, "pp", 0, &LogKeyBool);
    F(kMgr).Set(18, 2, 0u);
    F(kMgr).table[18] = reinterpret_cast<void *>(&KeyListOf);
    F(kMgr).Set(20, 3, 0u);
    F(kMgr).table[20] = reinterpret_cast<void *>(&RegisterFactory);
    F(kMgr).Set(27, "p", 0, &DrawBool);
    F(kMgr).Set(34, "pp", 0, &FindSegment);
    gLastFactory = nullptr;
    gFactory = nullptr;
    F(kCom).Set(3, "pdp", 0, &ClassObject);
    F(kCom).Set(7, "p", 0, &NewGuid);
    F(kMsgSrv).Set(5, "pd", 1u);
    F(kMsgSrv).Set(6, "pd", 1u);
    F(kMessage).Set(0, "dp", 0, &MessageFace);
    F(kMessage).Set(4, 0, 0, &MessageType);
    for (unsigned i = 0; i < kSegs; ++i) {
        Obj &o = F(kSeg0 + i);
        o.Set(18, 1, 0u);
        o.Set(19, 2, 0u);
        o.table[19] = reinterpret_cast<void *>(&KeyListOf);
        o.Set(26, "p", 0, &LogKeyBool);
    }
    Obj &made = F(kNewSeg);
    made.Set(0, "dp", 0, &SegmentFace);
    made.Set(3, 0, &F(kSeg0 + 2));
    made.Set(4, 0, 0x100u);
    made.Set(6, "dd", 0u);
    made.Set(9, "p", 0, &LogString1);
    made.Set(12, 0, &F(kLabels));
    made.Set(15, "dd", 0u);
    made.Set(16, "d", 0u);
    made.Set(28, "d", 0u);
    made.Set(29, "d", 0u);
    F(kLabels).Set(3, "dp", 0, &LogLabel);
    static const uint32_t kTypes[] = {0, 0x4c697e5a, 0x1234};
    for (unsigned i = 0; i < kRes; ++i) {
        Obj &o = F(kRes0 + i);
        const MaterialKey key = {kTypes[r() % 3], r() % 3 ? 0u : 7u, r() % 3 ? uint32_t(r() % 6) : 0u, uint32_t(r() % 2)};
        std::memcpy(o.value, &key, sizeof(key));
        o.value[4] = uint32_t(r() % 3); // kind
        o.value[5] = uint32_t(r() % 3); // text
        o.value[6] = uint32_t(r() % 3); // key words
        o.Set(0, "dp", 0, &EntryFace);
        o.Set(3, "p", 0, &KeyInto);
        o.Set(4, "p", 0, &KeyFrom);
        o.Set(12, 0, 0, &Kind);
        o.Set(13, 0, 0, &Text);
        o.Set(14, 0, 0, &Words);
        Obj &e = F(kEnt0 + i);
        e.value[0] = r() % 4 ? uint32_t(1 + r() % 2) : 0u; // group
        e.value[1] = uint32_t(r() % kNameCount);
        e.value[2] = uint32_t(r() % 4);
        e.Set(3, 0, &o);
        e.Set(4, 0, 0, &Group);
        e.Set(5, "p", 0, &NameOut);
        e.Set(6, "p", 0, &SecondOut);
    }
}

// The key pool: the material type, others.
void MakeKeys(uint32_t seed)
{
    std::mt19937 r(seed * 5 + 11);
    static const uint32_t kTypes[] = {0x4c697e5a, 0x4c697e5a, 0x12345678};
    for (MaterialKey &k : gKeys) k = {kTypes[r() % 3], uint32_t(r() % 3), uint32_t(r() % 6), uint32_t(r() % 2)};
}

bool gNullDefault; // this seed has no default segment

// The catalog's state (the same on both sides: written through our layout). Names are filed lowercased with "_cres", as the
// catalog files them, plus now and then one it would not make.
std::string FiledName(std::mt19937 &r)
{
    static const char *const kFiled[] = {"skin_cres", "hair_cres_cres", "_cres", "x_cre_cres", "Odd"};
    return kFiled[r() % 5];
}
void Populate(Sys *s, uint32_t seed)
{
    std::mt19937 r(seed * 13 + 1);
    s->mCount = r() % 5;
    if (!gNullDefault) s->mDefaultSegment.Assign(reinterpret_cast<cIGZUnknown *>(&F(kSeg0 + r() % kSegs)));
    if (r() % 4 == 0) s->mMessages.insert(0x1000 + r() % 3);
    if (r() % 6 == 0) s->mMessages.clear();
    std::vector<MaterialList *> lists;
    const unsigned groups = r() % 4;
    for (unsigned g = 0; g < groups; ++g) {
        const uint32_t group = r() % 4;
        if (s->mGroups.count(group)) continue;
        if (r() % 5 == 0) {
            s->mGroups[group] = nullptr;
            continue;
        }
        auto *names = new MaterialNameMap;
        s->mGroups[group] = names;
        const unsigned n = r() % 3;
        for (unsigned i = 0; i < n; ++i) {
            const std::string name = FiledName(r);
            if (names->count(name)) continue;
            if (r() % 6 == 0) {
                (*names)[name] = nullptr;
                continue;
            }
            auto *list = new MaterialList{};
            const unsigned entries = r() % 5;
            for (unsigned k = 0; k < entries; ++k) list->PushBack(&F(kEnt0 + r() % kRes));
            (*names)[name] = list;
            lists.push_back(list);
        }
    }
    const unsigned named = r() % 3;
    for (unsigned i = 0; i < named; ++i) {
        const std::string name = FiledName(r);
        s->mNames[name] = lists.empty() || r() % 4 == 0 ? nullptr : lists[r() % lists.size()];
    }
}

// ---- dumping ----

uint32_t Word(const void *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(m) + off); }

void DumpList(Blob &b, const MaterialList *v)
{
    if (!v) {
        b.push_back(0xffffffffu);
        return;
    }
    b.push_back(v->Size());
    b.push_back(uint32_t(v->mEnd - v->mFirst));
    for (void **p = v->mFirst; p != v->mLast; ++p) b.push_back(Self(*p));
}
void DumpName(Blob &b, const std::string &s)
{
    b.push_back(uint32_t(s.size()));
    for (char c : s) b.push_back(uint8_t(c));
}

bool gCreator;
void Dump(Blob &b, Sys *s, bool ours, bool fresh)
{
    static const uint32_t kFaces[4][2] = {{0, Sys::kVTable}, {4, Sys::kVTable4}, {0x1c, Sys::kCatalogVTable}, {0x20, Sys::kTargetVTable}};
    for (const auto &f : kFaces) {
        const bool game = Word(s, f[0]) == P(GameAddress<void>(f[1]));
        b.push_back(ours ? !game : game);
    }
    for (uint32_t off = 8; off < 0x18; off += 4) b.push_back(Word(s, off));
    b.push_back(Word(s, 0x18) & 0xff); // mRunning (its padding is the heap's under the creator)
    // mCount is never written by the constructor: 0xc5 fill, or the heap's under the creator.
    b.push_back(fresh && gCreator ? 0 : s->mCount);
    std::map<const MaterialList *, uint32_t> index;
    b.push_back(uint32_t(s->mGroups.size()));
    for (const auto &[group, names] : s->mGroups) {
        b.push_back(group);
        if (!names) {
            b.push_back(0xeeeeeeeeu);
            continue;
        }
        b.push_back(uint32_t(names->size()));
        for (const auto &[name, list] : *names) {
            DumpName(b, name);
            if (list) index.emplace(list, uint32_t(index.size()));
            DumpList(b, list);
        }
    }
    b.push_back(uint32_t(s->mNames.size()));
    for (const auto &[name, list] : s->mNames) {
        DumpName(b, name);
        const auto it = index.find(list);
        b.push_back(!list ? 0xffffffffu : it == index.end() ? 0xbad0bad0u : it->second);
    }
    b.push_back(uint32_t(s->mMessages.size()));
    for (uint32_t id : s->mMessages) b.push_back(id);
    b.push_back(Self(s->mDefaultSegment.Get()));
}

// ---- driving a catalog ----

Sys *Build(difftest::Context &ctx, Blob &b, bool ours)
{
    if (gCreator) {
        void *made = ours ? static_cast<void *>(Sys::Create()) : ctx.Original<void *(__cdecl *)()>(0xd06930)();
        b.push_back(made != nullptr);
        return static_cast<Sys *>(made);
    }
    void *memory = ::operator new(Sys::kSize);
    std::memset(memory, 0xc5, Sys::kSize);
    void *r = ours ? static_cast<void *>(cTSMaterialCatalogAdapters::Construct(memory, nullptr))
                   : ctx.Original<void *(__thiscall *)(void *)>(0xd04bd0)(memory);
    b.push_back(r == memory);
    return static_cast<Sys *>(memory);
}

const uint32_t kIids[] = {1, Sys::kIid, 0x287fb697 /* cIGZSystemService */, 0x12345678};
const std::string kFilterNames[] = {"a", "B", ""};

void RunOp(Blob &b, Sys *s, unsigned op, std::mt19937 &r)
{
    void *const primary = s;
    void *const face = reinterpret_cast<uint8_t *>(s) + 0x1c;
    MaterialList out{};
    const unsigned prefill = r() % 4 == 0 ? 1 + r() % 5 : 0; // a list the getters empty first (capacity kept)
    for (unsigned i = 0; i < prefill; ++i) out.PushBack(&F(kEnt0 + r() % kRes));
    // Now and then an entry changes its group, name or second name (collisions in AddEntry / RemoveEntry).
    if (r() % 3 == 0) {
        Obj &e = F(kEnt0 + r() % kRes);
        switch (r() % 3) {
        case 0: e.value[0] = r() % 3; break;
        case 1: e.value[1] = uint32_t(r() % kNameCount); break;
        default: e.value[2] = uint32_t(r() % 4); break;
        }
    }
    const std::string *nameFilter = r() % 2 ? &kFilterNames[r() % 3] : nullptr;
    const uint32_t *keyFilter = r() % 2 ? gWords[r() % 3] : nullptr;
    switch (op) {
    case 0: { // QueryInterface on a face (+0, the thunks +4 0xd06fb1, +0x1c 0xd06fb9, +0x20 0xd06fc1), the result released
        static const uint32_t kFaceAt[] = {0, 4, 0x1c, 0x20};
        void *at = reinterpret_cast<uint8_t *>(s) + kFaceAt[r() % 4];
        void *q = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        const uint32_t ok = Slot<uint32_t>(at, 0, kIids[r() % 4], &q) & 0xff;
        b.push_back(ok);
        b.push_back(Self(q));
        if (ok) b.push_back(Slot<uint32_t>(q, 2));
        break;
    }
    case 1: { // Init; now and then without a resource manager; 0..3 content directories
        gLastFactory = nullptr;
        gNoManager = r() % 5 == 0;
        gDirCount = r() % 4;
        b.push_back(Slot<uint32_t>(primary, 8) & 0xff);
        gNoManager = false;
        break;
    }
    case 2: // Shutdown, now and then without a message server
        gNoMsgServer = r() % 3 == 0;
        b.push_back(Slot<uint32_t>(primary, 9) & 0xff);
        gNoMsgServer = false;
        break;
    case 3: { // AddMaterial: a resource with or without a group, an instance, a type; a segment named or not
        Obj &resource = F(kRes0 + r() % kRes);
        if (r() % 3 == 0) resource.value[1] = 0;
        if (r() % 3 == 0) resource.value[2] = 0;
        if (r() % 3 == 0) resource.value[0] = 0;
        void *segment = r() % 2 ? static_cast<void *>(&F(kSeg0 + r() % kSegs)) : nullptr;
        b.push_back(Slot<uint32_t>(face, 3, static_cast<void *>(&resource), segment) & 0xff);
        break;
    }
    case 4: { // RemoveEntry
        Obj &resource = F(kRes0 + r() % kRes);
        if (r() % 4 == 0) resource.value[1] = r() % 2 ? 0 : 5;
        if (r() % 4 == 0) resource.value[2] = r() % 2 ? 0 : 4;
        b.push_back(Slot<uint32_t>(face, 4, static_cast<void *>(&F(kEnt0 + (&resource - &F(kRes0))))) & 0xff);
        break;
    }
    case 5: b.push_back(Slot<uint32_t>(face, 5)); break; // Load
    case 6: { // LoadSegment, a segment or none
        void *segment = r() % 4 ? static_cast<void *>(&F(kSeg0 + r() % kSegs)) : nullptr;
        b.push_back(Slot<uint32_t>(face, 6, segment));
        break;
    }
    case 7: Slot<void>(face, 7); break; // ClearAll
    case 8: { // GetByName
        static const std::string kAsked[] = {"skin", "Skin_CRES", "hair_cres", "", "odd", std::string("ab\0_cres", 8)};
        Slot<void>(face, 8, &out, &kAsked[r() % 6], nameFilter, keyFilter);
        break;
    }
    case 9: { // GetByGroup: group 0 (all), a filed group, or another; with or without a name
        static const std::string kAsked[] = {"skin", "HAIR_cres_cres", "x_cre"};
        const uint32_t group = r() % 5;
        const std::string *name = r() % 2 ? &kAsked[r() % 3] : nullptr;
        Slot<void>(face, 9, &out, group, name, nameFilter, keyFilter);
        break;
    }
    case 10: { // AddEntry, now and then none
        void *entry = r() % 8 ? static_cast<void *>(&F(kEnt0 + r() % kRes)) : nullptr;
        Slot<void>(face, 10, entry);
        break;
    }
    case 11:
    case 12: { // OpenSegment / CreateSegment
        void *segment = nullptr;
        const char *path = op == 11 ? kPaths[r() % 3] : kDirs[r() % 3];
        b.push_back(Slot<uint32_t>(face, op, path, &segment) & 0xff);
        b.push_back(Self(segment));
        if (segment) b.push_back(Slot<uint32_t>(segment, 2));
        break;
    }
    case 13: { // DoMessage (the +0x20 face), a message or none
        void *target = reinterpret_cast<uint8_t *>(s) + 0x20;
        void *message = r() % 6 ? static_cast<void *>(&F(kMessage)) : nullptr;
        b.push_back(Slot<uint32_t>(target, 3, message) & 0xff);
        break;
    }
    default: break;
    }
    DumpList(b, &out);
    out.ReleaseAndFree();
}

Blob RunSide(difftest::Context &ctx, uint32_t seed, bool ours)
{
    ConfigureFakes(seed);
    MakeKeys(seed);
    gDraw.seed(seed * 17 + 5);
    Blob b;
    Sys *s = Build(ctx, b, ours);
    svcfx::AddRange(s, Sys::kSize, kTag);
    b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    svcfx::gEv.clear();
    Dump(b, s, ours, true);
    Populate(s, seed);
    Slot<uint32_t>(s, 1); // counted twice: QueryInterface's references come and go without destroying it
    Slot<uint32_t>(s, 1);
    std::mt19937 r(seed * 31 + 7);
    const unsigned ops = 2 + unsigned(r() % 14);
    for (unsigned i = 0; i < ops; ++i) {
        // Every slot alike, with Shutdown / ClearAll (which empty everything) rarer.
        unsigned op = unsigned(r() % 14);
        if ((op == 2 || op == 7) && r() % 2) op = 8 + unsigned(r() % 3);
        char label[64];
        std::snprintf(label, sizeof(label), "seed %u %s op %u slot %u", seed, ours ? "ours" : "orig", i, op);
        ctx.Mark(label);
        if (svcfx::gTrace) fprintf(stderr, "%s\n", label);
        b.push_back(0xa0000000u | op);
        svcfx::gRanges[gKeyListRange] = {0, 0, kListTag};
        RunOp(b, s, op, r);
        if (gHeldFilter) {
            b.push_back(0xf1170000u | reinterpret_cast<const uint32_t *>(gHeldFilter)[1]);
            b.push_back(Slot<uint32_t>(gHeldFilter, 2));
            gHeldFilter = nullptr;
        }
        b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
        svcfx::gEv.clear();
        Dump(b, s, ours, false);
        svcfx::CaptureFakes(b, kValueKinds);
    }
    // The catalog's lists are freed by ClearAll only (the destructor leaks them, as the original): emptied through
    // ClearAll (slot 7, each side its own), so the fakes' counts at the end are comparable and nothing leaks into the next seed.
    Slot<void>(reinterpret_cast<uint8_t *>(s) + 0x1c, 7);
    b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    svcfx::gEv.clear();
    // The last Release destroys it (cRZUnknownMT's Release deletes at a count of 1 or below).
    while (Word(s, 8) > 1) b.push_back(Slot<uint32_t>(s, 2));
    b.push_back(Slot<uint32_t>(s, 2));
    b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    svcfx::gEv.clear();
    svcfx::CaptureFakes(b, kValueKinds);
    return b;
}

} // namespace matcat_test

DIFF_TEST(MaterialCatalog_Run, 0xd04bd0)
{
    using namespace matcat_test;
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    globals.Set(0x10c9378, &F(kFw));         // the framework (GetServiceObject, src/rz/Singleton_20ddfb.h)
    globals.Set(svcfx::kPersistCell, &F(kSys)); // the persist system (CachedServiceGetter 0xb390c0)
    shape::CodePatch gameNew(0x824632, reinterpret_cast<const void *>(&GameNew));
    shape::CodePatch gameDelete(0x824624, reinterpret_cast<const void *>(&GameDelete));
    shape::StandIn pathExists(0x1c1242, &cRZFile::PathExists, &StandPathExists);
    shape::StandIn dirExists(0x1c4f94, &as2::DirectoryExists, &StandDirectoryExists);
    shape::StandIn dirs(0x1d0c60, &appinit::CommonImplSlot5, &StandDirectories);
    char traceFlag[4];
    svcfx::gTrace = GetEnvironmentVariableA("TS2_TEST_TRACE", traceFlag, sizeof(traceFlag)) != 0;
    for (uint32_t seed = 0; seed < 2000; ++seed) {
        gCreator = seed % 5 == 4;
        gNullDefault = seed % 4 == 1;
        const Blob original = RunSide(ctx, seed, false);
        const Blob ours = RunSide(ctx, seed, true);
        if (!svcfx::Same(ctx, original, ours, "run", seed) && ctx.failures.size() <= 3) {
            size_t i = 0;
            while (i < original.size() && i < ours.size() && original[i] == ours[i]) ++i;
            size_t op = 0; // the operation the difference is in
            for (size_t k = 0; k < i; ++k)
                if ((original[k] & 0xffffff00u) == 0xa0000000u) op = k;
            fprintf(stderr, "seed %u: op at %zu slot %u\n", seed, op, original[op] & 0xff);
            for (size_t k = i > 120 && op < i - 120 ? i - 120 : op; k < i + 8 && k < original.size() && k < ours.size(); ++k)
                fprintf(stderr, "%5zu %08x %08x\n", k, original[k], ours[k]);
        }
    }
}
