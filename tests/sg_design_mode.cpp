// The design-mode module (lane 261111o; src/sg/DesignMode.h): each function against the original over drawn worlds of
// fakes. A composition tree of nodes (QueryInterface for the portal face 0xfa1c39ed and the shape-ref face 0xa516cd10
// answering no / yes / writes-but-no; slot 42 the children range with a junk middle field, slot 38 the next sibling;
// a virtual base at +0xc so a node doubles as a portal face), shape-ref faces (slot 93: the shapes range, junk middle
// field, null shapes where the function allows them), shapes (slot 50: the geometry or null), geometries (slot 42: a
// name range of three counted iterators from a pool, slots 4 / 5 / 6 / 2), data lists behind the FindDataList stand-in
// (0xaeb5b0 on both sides: "tsDesignModeEnabled" items over GetCount 0..6 and negative; "tsMaterialsMeshName"
// GetString answering no / yes / writes-but-no with values with and without "_cres"), a shape container (slots 15 / 5),
// an object (slot 9) and the app singleton's object table (0x10cc288; slots 26 / 29). The strings are the game's
// cRZString: the outputs start empty, short or long, and alias each other in some cases; the subset names cover empty,
// case variants, "shadow" (in place, in another case), "_cres", '%' and names over 15 characters.
//
// Compared per case: one ordered log of every fake call (unique ids, the arguments, the heap's block count at the call),
// the result, every fake's count, the strings' texts, the heap's blocks and bytes and the frees' sizes in order. The
// game's operator new / sized delete stand in for ours (the original's strings and list nodes, freed by either side).
#include "code_patch.h"
#include "difftest.h"
#include "sg_lighting_common.h"

#include "rz/cRZString.h"
#include "sg/DesignMode.h"
#include "sg/SGNodeInterfaces.h"
#include "sg/SGUtilities.h"
#include "sg/cShape.h"
#include "sg/cShapeRefNode.h"

#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <utility>

namespace dmfx {

using nGZSceneGraph::cChildRange;
using nGZSceneGraph::cShapeRange;

constexpr uint32_t kPortalIid = 0xfa1c39ed, kShapeRefIid = 0xa516cd10;
constexpr int kNodes = 12, kRefs = 8, kShapes = 12, kGeoms = 8, kLists = 10, kIters = 600, kStrs = 5;

const int32_t kVbTable[2] = {-4, 8};

struct Ref;
struct Node {
    void **vt;
    const int32_t *vbptr; // +4: the virtual base at +0xc
    uint32_t id;
    void **baseVt; // +0xc
    uint32_t refs;
    Node *firstChild, *next, *junk;
    uint32_t portalAnswer, refAnswer; // 0 no, 1 writes and says yes, 2 writes and says no
    Node *face;
    Ref *ref;
};
struct Geom;
struct Shape {
    void **vt;
    uint32_t id, refs;
    Geom *geom;
};
struct Ref {
    void **vt;
    uint32_t id, refs;
    Shape *shapes[6];
    uint32_t count;
};
struct List {
    void **vt;
    const int32_t *vbptr;
    uint32_t id;
    void **baseVt; // +0xc
    uint32_t refs;
    int32_t count;
    uint8_t items[6];
    uint32_t salt;
};
struct Geom {
    void **vt;
    uint32_t id;
    uint8_t names[6];
    uint32_t count;
    List *enabled, *mesh;
    List *slave;   // lane 261111p: "tsDesignModeSlaveSubsets" (set by its tests; null from Draw)
    uint32_t refs; // lane 261111p: slots 1 / 2
};
struct Iter {
    void **vt;
    uint32_t id, refs;
    Geom *geom;
    uint32_t pos;
};
struct Cont {
    void **vt;
    uint32_t id;
    int32_t count;
    Shape *items[6];
};
struct Obj {
    void **vt;
    uint32_t id, answer;
    Node *portal;
};
struct Table {
    void **vt;
    uint32_t id;
};

Node gNodes[kNodes];
Ref gRefs[kRefs];
Shape gShapes[kShapes];
Geom gGeoms[kGeoms];
List gLists[kLists];
Iter gIters[kIters];
uint32_t gIterNext;
Cont gCont;
Obj gObj;
Table gApp, gTable;
uint32_t gObjects[4]; // what the table answers
cRZString gStrs[kStrs]; // the four outputs and the subset
void *gNodeVT[50], *gNodeBaseVT[4], *gRefVT[100], *gShapeVT[56], *gGeomVT[48], *gListVT[30], *gListBaseVT[4],
    *gIterVT[8], *gContVT[20], *gObjVT[300], *gAppVT[30], *gTableVT[32];
difftest::AllocationCounter *gHeap;
uint32_t gJunk;

// ---- lane 261111p's fakes ----
// The framework (0x10c9378; slot 5 hands out the override service: no / yes / writes but no / writes null), the
// service (slots 1 / 2; 8 and 9 fill the candidate vector from an allocator-service block), the candidates (slots 1 /
// 2; 6 a name into the caller's std::string, or a short one there and the name in another returned; 7 usable), the
// object's link (slot 27) and key holder (slot 28).
struct Fw {
    void **vt;
    uint32_t id, mode;
};
struct Svc {
    void **vt;
    uint32_t id, refs;
};
struct Cand {
    void **vt;
    uint32_t id, refs;
    const char *name;
    uint32_t usable, elsewhere;
};
struct Link {
    void **vt;
    uint32_t id;
    void *holder;
};
struct Holder {
    void **vt;
    uint32_t id, key;
};
constexpr int kCands = 6;
Fw gFw;
Svc gSvc;
Cand gCands[kCands];
Link gLink;
Holder gHolder;
void *gFwVT[8], *gSvcVT[12], *gCandVT[10], *gLinkVT[30], *gHolderVT[30];
void *gLogStrVT[34]; // the game's cRZString table with slots 5, 7 and 15 noted

const char *const kSubsets[] = {"",       "wood",       "Wood",         "WOOD",  "frame",  "frame_shadow",
                                "Shadow_frame", "shadow", "fabric", "fabric_cres", "a_subset_name_longer_than_fifteen",
                                "A_SUBSET_NAME_LONGER_THAN_FIFTEEN", "glass%d%s", "_cres", "x", "frameshadowx"};
const char *const kMeshes[] = {"", "mesh", "mesh_cres", "mesh_CRES", "_cres", "a_mesh_name_longer_than_fifteen",
                               "x_cres_y", "cres", "%s_cres%%", "a_mesh_name_longer_than_fifteen_cres"};
const char *const kInits[] = {"", "old", "an_old_value_longer_than_fifteen"};
// lane 261111p: the slave lists' values (GetString while gSlaveMode): ',' absent, first, last, doubled, alone; long
// pieces; format characters; spaces and case kept.
const char *const kSlaveTexts[] = {"",
                                   "wood",
                                   "wood,frame",
                                   ",wood",
                                   "wood,",
                                   "wood,,frame",
                                   ",",
                                   ",,",
                                   ",wood,frame",
                                   "a_slave_name_longer_than_fifteen,another_slave_name_longer_than_fifteen,x",
                                   "%s,%d%n,%%",
                                   "Wood, frame ,GLASS",
                                   "a,b,c,d,e,f,g",
                                   "x,a_slave_name_longer_than_fifteen,"};
bool gSlaveMode;

uint32_t Id(const void *p)
{
    if (!p) return 0;
    for (int i = 0; i < kNodes; ++i)
        if (p == &gNodes[i]) return 0x100 + i;
    for (int i = 0; i < kRefs; ++i)
        if (p == &gRefs[i]) return 0x200 + i;
    for (int i = 0; i < kShapes; ++i)
        if (p == &gShapes[i]) return 0x300 + i;
    for (int i = 0; i < kGeoms; ++i)
        if (p == &gGeoms[i]) return 0x400 + i;
    for (int i = 0; i < kLists; ++i)
        if (p == &gLists[i]) return 0x500 + i;
    for (int i = 0; i < kStrs; ++i)
        if (p == &gStrs[i]) return 0x600 + i;
    if (p >= &gIters[0] && p < &gIters[kIters]) return 0x1000 + uint32_t(static_cast<const Iter *>(p) - gIters);
    for (int i = 0; i < 4; ++i)
        if (p == &gObjects[i]) return 0x700 + i;
    if (p == &gCont) return 0x800;
    if (p == &gObj) return 0x900;
    if (p == &gTable) return 0xa00;
    for (int i = 0; i < kCands; ++i)
        if (p == &gCands[i]) return 0xb00 + i;
    if (p == &gSvc) return 0xc00;
    if (p == &gLink) return 0xd00;
    if (p == &gHolder) return 0xd01;
    if (p == &gFw) return 0xf00;
    return 0xbad00000u | (P(p) & 0xfffff);
}

// Id, but any other address (the strings a caller keeps on its stack: lane 261111p) by the order it was first seen in
// the case (Draw resets it).
const void *gSeen[16];
uint32_t gSeenCount;
uint32_t SeenId(const void *p)
{
    const uint32_t id = Id(p);
    if ((id & 0xfff00000u) != 0xbad00000u) return id;
    for (uint32_t i = 0; i < gSeenCount; ++i)
        if (gSeen[i] == p) return 0xe00 + i;
    if (gSeenCount == std::size(gSeen)) return id;
    gSeen[gSeenCount] = p;
    return 0xe00 + gSeenCount++;
}

// The heap's blocks, and the allocator service's blocks live (counted only under the lane 261111p tests' ScopeP).
int32_t gSvcLive;
void Probe() { L(gHeap ? uint32_t(gHeap->Blocks()) : 0u), L(uint32_t(gSvcLive)); }
uint32_t Junk(uint32_t answer) { return (Mix2(gJunk, answer) & ~0xffu) | answer; }

// Any slot the functions should not reach.
uint32_t __fastcall Stray(void *self, void *) { return L(0xbad), L(Id(self)), 0; }

// ---- the nodes ----
uint32_t __fastcall NodeQI(Node *self, void *, uint32_t iid, void **out)
{
    L(0x100), L(Id(self)), L(iid), L(Id(*out)), Probe();
    uint32_t answer = 0;
    void *face = nullptr;
    if (iid == kPortalIid) answer = self->portalAnswer, face = self->face;
    else if (iid == kShapeRefIid) answer = self->refAnswer, face = self->ref;
    else L(0xbad1);
    if (answer == 0 || !face) return Junk(0);
    *out = face;
    if (iid == kPortalIid) ++self->face->refs;
    else ++self->ref->refs;
    return Junk(answer == 1 ? 1 : 0);
}
uint32_t __fastcall NodeAddRef(Node *self, void *) { return L(0x101), L(Id(self)), ++self->refs; }
uint32_t __fastcall NodeRelease(Node *self, void *) { return L(0x102), L(Id(self)), --self->refs; }
Node *__fastcall NodeNext(Node *self, void *) { return L(0x126), L(Id(self)), self->next; }
cChildRange *__fastcall NodeChildren(Node *self, void *, cChildRange *out)
{
    L(0x12a), L(Id(self)), Probe();
    out->mCurrent = reinterpret_cast<nGZSceneGraph::cICompositionTreeNode *>(self->firstChild);
    out->mBegin = reinterpret_cast<nGZSceneGraph::cICompositionTreeNode *>(self->junk);
    out->mEnd = nullptr;
    return out;
}
uint32_t __fastcall NodeBaseAddRef(uint8_t *base, void *)
{
    Node *const n = reinterpret_cast<Node *>(base - 0xc);
    return L(0x1a1), L(Id(n)), ++n->refs;
}
uint32_t __fastcall NodeBaseRelease(uint8_t *base, void *)
{
    Node *const n = reinterpret_cast<Node *>(base - 0xc);
    return L(0x1a2), L(Id(n)), Probe(), --n->refs;
}

// ---- the shape-ref faces and the shapes ----
uint32_t __fastcall RefRelease(Ref *self, void *) { return L(0x202), L(Id(self)), --self->refs; }
cShapeRange *__fastcall RefShapes(Ref *self, void *, cShapeRange *out)
{
    L(0x25d), L(Id(self)), Probe();
    auto **const shapes = reinterpret_cast<nGZSceneGraph::cIShape **>(self->shapes);
    out->mFirst = shapes;
    out->mCurrent = shapes + 5; // junk: the functions walk from the first
    out->mEnd = shapes + self->count;
    return out;
}
uint32_t __fastcall ShapeAddRef(Shape *self, void *) { return L(0x301), L(Id(self)), Probe(), ++self->refs; }
uint32_t __fastcall ShapeRelease(Shape *self, void *) { return L(0x302), L(Id(self)), Probe(), --self->refs; }
Geom *__fastcall ShapeGeom(Shape *self, void *) { return L(0x332), L(Id(self)), self->geom; }

// ---- the geometries and their name ranges ----
Iter *NewIter(Geom *g, uint32_t pos)
{
    if (gIterNext >= kIters) {
        L(0xbad2);
        gIterNext = 0;
    }
    Iter *const it = &gIters[gIterNext++];
    it->refs = 1, it->geom = g, it->pos = pos;
    return it;
}
Iter **__fastcall GeomNames(Geom *self, void *, Iter **out)
{
    L(0x42a), L(Id(self)), Probe();
    out[0] = NewIter(self, 0);
    out[1] = NewIter(self, self->count / 2);
    out[2] = NewIter(self, self->count);
    return out;
}
uint32_t __fastcall IterRelease(Iter *self, void *) { return L(0x1002), L(Id(self)), L(self->pos), --self->refs; }
uint32_t __fastcall IterEqual(Iter *self, void *, const Iter *other)
{
    L(0x1004), L(Id(self)), L(self->pos), L(Id(other)), Probe();
    return Junk(other && self->pos == other->pos ? 1 : 0);
}
const char *__fastcall IterGet(Iter *self, void *)
{
    L(0x1005), L(Id(self)), L(self->pos);
    return kSubsets[self->geom->names[self->pos % 6]];
}
// The step notes the heap too: the name's copy is still alive when the range is stepped.
uint32_t __fastcall IterNext(Iter *self, void *) { return L(0x1006), L(Id(self)), L(self->pos), Probe(), ++self->pos; }

// The subset string's own table: the image's cRZString with the text (slot 5) and the length (slot 7) noted, so
// how often and where the subset is asked shows.
void *gSubsetVT[64];
const char *__fastcall SubsetText(cRZString *self, void *) { return L(0x5005), self->mString.c_str(); }
uint32_t __fastcall SubsetLength(cRZString *self, void *) { return L(0x5007), uint32_t(self->mString.size()); }
void WatchSubset(cRZString &subset)
{
    std::memcpy(gSubsetVT, cRZString::VTable(), sizeof(gSubsetVT));
    gSubsetVT[5] = reinterpret_cast<void *>(&SubsetText);
    gSubsetVT[7] = reinterpret_cast<void *>(&SubsetLength);
    subset.mVTable = gSubsetVT;
}

// ---- the data lists ----
void *__cdecl StandFindList(void *node, const char *name, void *after)
{
    L(0xeb5b), L(Id(node)), L(Hash(name)), L(Id(after)), Probe();
    Geom *const g = static_cast<Geom *>(node);
    if (std::strcmp(name, "tsDesignModeEnabled") == 0) return g->enabled;
    if (std::strcmp(name, "tsMaterialsMeshName") == 0) return g->mesh;
    if (std::strcmp(name, "tsDesignModeSlaveSubsets") == 0) return g->slave;
    L(0xbad3);
    return nullptr;
}
uint32_t __fastcall ListBaseAddRef(uint8_t *base, void *)
{
    List *const l = reinterpret_cast<List *>(base - 0xc);
    return L(0x5a1), L(Id(l)), ++l->refs;
}
uint32_t __fastcall ListBaseRelease(uint8_t *base, void *)
{
    List *const l = reinterpret_cast<List *>(base - 0xc);
    return L(0x5a2), L(Id(l)), Probe(), --l->refs;
}
uint32_t __fastcall ListCount(List *self, void *) { return L(0x51a), L(Id(self)), uint32_t(self->count); }
const char *__fastcall ListItemName(List *self, void *, int32_t i)
{
    L(0x516), L(Id(self)), L(uint32_t(i));
    return kSubsets[self->items[uint32_t(i) % 6]];
}
// The real one FromChars the value into `out` and says whether the name is there: here no / yes / writes but no.
uint32_t __fastcall ListGetString(List *self, void *, const char *name, cRZString *out)
{
    L(0x512), L(Id(self)), L(Hash(name)), L(SeenId(out)), L(out->Strlen()), Probe();
    const uint32_t h = Mix2(self->salt, Hash(name));
    const uint32_t mode = h % 4;
    if (mode == 0) return Junk(0);
    if (gSlaveMode) out->FromChar(kSlaveTexts[(h >> 8) % std::size(kSlaveTexts)]);
    else out->FromChar(kMeshes[(h >> 8) % std::size(kMeshes)]);
    return Junk(mode == 3 ? 0 : 1);
}

// ---- the container, the object, the singleton's table ----
int32_t __fastcall ContCount(Cont *self, void *) { return L(0x80f), L(Id(self)), Probe(), self->count; }
Shape *__fastcall ContItem(Cont *self, void *, int32_t i)
{
    L(0x805), L(Id(self)), L(uint32_t(i));
    return self->items[uint32_t(i) % 6];
}
uint32_t __fastcall ObjComponent(Obj *self, void *, uint32_t a, uint32_t b, Node **out)
{
    L(0x909), L(Id(self)), L(a), L(b), L(Id(*out)), Probe();
    if (self->answer == 0 || !self->portal) return Junk(0);
    *out = self->portal;
    ++self->portal->refs;
    return Junk(self->answer == 1 ? 1 : 0);
}
Table *__fastcall AppTable(Table *self, void *) { return L(0xa1a), L(Id(self)), &gTable; }
void *__fastcall TableObject(Table *self, void *, uint32_t id)
{
    L(0xa1d), L(Id(self)), L(id);
    const uint32_t h = Mix2(gJunk, id);
    return h % 5 ? &gObjects[h % 4] : nullptr;
}

template <class F> void *V(F f) { return reinterpret_cast<void *>(f); }

void FillTables()
{
    auto fill = [](void **t, size_t n) {
        for (size_t i = 0; i < n; ++i) t[i] = V(&Stray);
    };
    fill(gNodeVT, std::size(gNodeVT)), fill(gNodeBaseVT, 4), fill(gRefVT, std::size(gRefVT)),
        fill(gShapeVT, std::size(gShapeVT)), fill(gGeomVT, std::size(gGeomVT)), fill(gListVT, std::size(gListVT)),
        fill(gListBaseVT, 4), fill(gIterVT, std::size(gIterVT)), fill(gContVT, std::size(gContVT)),
        fill(gObjVT, std::size(gObjVT)), fill(gAppVT, std::size(gAppVT)), fill(gTableVT, std::size(gTableVT));
    gNodeVT[0] = V(&NodeQI), gNodeVT[1] = V(&NodeAddRef), gNodeVT[2] = V(&NodeRelease);
    gNodeVT[38] = V(&NodeNext), gNodeVT[42] = V(&NodeChildren);
    gNodeBaseVT[1] = V(&NodeBaseAddRef), gNodeBaseVT[2] = V(&NodeBaseRelease);
    gRefVT[2] = V(&RefRelease), gRefVT[93] = V(&RefShapes);
    gShapeVT[1] = V(&ShapeAddRef), gShapeVT[2] = V(&ShapeRelease), gShapeVT[50] = V(&ShapeGeom);
    gGeomVT[42] = V(&GeomNames);
    gIterVT[2] = V(&IterRelease), gIterVT[4] = V(&IterEqual), gIterVT[5] = V(&IterGet), gIterVT[6] = V(&IterNext);
    gListVT[18] = V(&ListGetString), gListVT[22] = V(&ListItemName), gListVT[26] = V(&ListCount);
    gListBaseVT[1] = V(&ListBaseAddRef), gListBaseVT[2] = V(&ListBaseRelease);
    gContVT[5] = V(&ContItem), gContVT[15] = V(&ContCount);
    gObjVT[9] = V(&ObjComponent);
    gAppVT[26] = V(&AppTable);
    gTableVT[29] = V(&TableObject);
}

// What a draw may hold: null shapes (the functions that skip them), null geometries (the ones that check).
struct Allow {
    bool nullShapes, nullGeoms;
};

uint32_t SubsetIndex(std::mt19937 &gen) { return gen() % 3 ? 1 + gen() % 9 : gen() % std::size(kSubsets); }

struct World {
    Node *root;
    uint32_t subset;
    uint32_t outs[4]; // which gStrs each output is (aliasing)
};

World Draw(uint32_t seed, uint32_t salt, Allow allow)
{
    std::mt19937 gen(seed * 2654435761u + salt);
    World w{};
    gJunk = gen();
    gIterNext = 0;
    gSeenCount = 0;
    for (int i = 0; i < kIters; ++i) gIters[i] = {gIterVT, 0x1000u + i, 0, nullptr, 0};
    for (int i = 0; i < kLists; ++i) {
        List &l = gLists[i];
        static const int32_t kCounts[] = {0, 1, 2, 3, 4, 6, 6, 5, -1, INT32_MIN};
        l = {gListVT, kVbTable, 0x500u + i, gListBaseVT, gen() % 3, kCounts[gen() % std::size(kCounts)], {}, gen()};
        for (uint8_t &it : l.items) it = uint8_t(SubsetIndex(gen));
    }
    for (int i = 0; i < kGeoms; ++i) {
        Geom &g = gGeoms[i];
        g = {gGeomVT, 0x400u + i, {}, gen() % 7, nullptr, nullptr};
        if (gen() % 5 == 0) g.count = gen() % 2;
        for (uint8_t &n : g.names) n = uint8_t(SubsetIndex(gen));
        g.enabled = gen() % 5 ? &gLists[gen() % kLists] : nullptr;
        g.mesh = gen() % 5 ? &gLists[gen() % kLists] : nullptr;
    }
    for (int i = 0; i < kShapes; ++i)
        gShapes[i] = {gShapeVT, 0x300u + i, gen() % 4,
                      allow.nullGeoms && gen() % 6 == 0 ? nullptr : &gGeoms[gen() % kGeoms]};
    for (int i = 0; i < kRefs; ++i) {
        Ref &r = gRefs[i];
        r = {gRefVT, 0x200u + i, gen() % 3, {}, gen() % 5};
        for (Shape *&s : r.shapes) s = allow.nullShapes && gen() % 6 == 0 ? nullptr : &gShapes[gen() % kShapes];
    }
    // the tree: node 0 the root of the first n; each later one under an earlier one (often the one before: depth)
    const int n = gen() % 6 == 0 ? 1 : 1 + int(gen() % kNodes);
    Node *last[kNodes] = {};
    for (int i = 0; i < kNodes; ++i) {
        Node &d = gNodes[i];
        static const uint32_t kPortal[] = {0, 0, 0, 0, 0, 1, 2};
        static const uint32_t kRef[] = {1, 1, 1, 0, 2};
        d = {gNodeVT, kVbTable, 0x100u + i, gNodeBaseVT, gen() % 4, nullptr, nullptr, &gNodes[gen() % kNodes],
             kPortal[gen() % std::size(kPortal)], kRef[gen() % std::size(kRef)], &gNodes[gen() % kNodes],
             gen() % 6 ? &gRefs[gen() % kRefs] : nullptr};
    }
    for (int j = 1; j < n; ++j) {
        const int parent = gen() % 2 ? j - 1 : int(gen() % j);
        if (last[parent]) last[parent]->next = &gNodes[j];
        else gNodes[parent].firstChild = &gNodes[j];
        last[parent] = &gNodes[j];
    }
    w.root = &gNodes[0];
    w.subset = SubsetIndex(gen);
    // the outputs: four strings, or some of them the same one
    for (uint32_t i = 0; i < 4; ++i) w.outs[i] = i;
    if (gen() % 5 == 0) {
        const uint32_t a = gen() % 4, b = gen() % 4;
        w.outs[a] = w.outs[b];
    }
    // fresh strings each side (a capacity left by the other side would change what this one allocates)
    for (int i = 0; i < 4; ++i) gStrs[i].Destruct(), gStrs[i].Construct(kInits[gen() % std::size(kInits)]);
    gStrs[4].Destruct(), gStrs[4].Construct(kSubsets[w.subset]);
    WatchSubset(gStrs[4]);
    // the container and the object
    gCont = {gContVT, 0x800, 0, {}};
    static const int32_t kContCounts[] = {0, 1, 2, 3, 6, 6, 5, -1, INT32_MIN};
    gCont.count = kContCounts[gen() % std::size(kContCounts)];
    for (Shape *&s : gCont.items) s = allow.nullShapes && gen() % 6 == 0 ? nullptr : &gShapes[gen() % kShapes];
    gObj = {gObjVT, 0x900, gen() % 3, gen() % 6 ? &gNodes[0] : nullptr};
    gApp = {gAppVT, 0xa01}, gTable = {gTableVT, 0xa00};
    return w;
}

uint32_t TextHash(const cRZString &s) { return Hash(s.mString.c_str()) ^ uint32_t(s.mString.size()) * 0x10001u; }

Blob Snapshot(uint32_t result, long long blocks, long long bytes)
{
    Blob o = gLog;
    o.push_back(0xeeee), o.push_back(result);
    for (const Node &n : gNodes) o.push_back(n.refs);
    for (const Ref &r : gRefs) o.push_back(r.refs);
    for (const Shape &s : gShapes) o.push_back(s.refs);
    for (const List &l : gLists) o.push_back(l.refs);
    for (const Geom &g : gGeoms) o.push_back(g.refs);
    for (uint32_t i = 0; i < gIterNext; ++i) o.push_back(gIters[i].refs), o.push_back(gIters[i].pos);
    for (const cRZString &s : gStrs) o.push_back(TextHash(s));
    o.push_back(uint32_t(blocks)), o.push_back(uint32_t(bytes));
    return o;
}

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t size) { ::operator delete(p, size); }

struct Scope {
    svcfx::AllocatorScope allocator;
    shape::CodePatch gameNew{0x824632u, reinterpret_cast<const void *>(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, reinterpret_cast<const void *>(&GameDelete)};
    shape::StandIn findList{0xaeb5b0u, &nGZSceneGraph::FindDataList, &StandFindList};
    uint32_t savedApp;
    Scope() : savedApp(*GameAddress<uint32_t>(0x10cc288))
    {
        FillTables();
        gLog.reserve(1 << 17);
        for (cRZString &s : gStrs) s.Construct();
        *GameAddress<void *>(0x10cc288) = &gApp;
    }
    ~Scope()
    {
        *GameAddress<uint32_t>(0x10cc288) = savedApp;
        for (cRZString &s : gStrs) s.Destruct();
    }
};

// Each seed's two sides (0 the original, 1 ours) compared; `call(side, world)` returns the result word. Returns how
// many cases `hit` said exercised the interesting path (on the original's side).
template <class Call, class Hit>
uint32_t Run(difftest::Context &ctx, const char *what, uint32_t cases, uint32_t salt, Allow allow, Call call, Hit hit)
{
    uint32_t hits = 0;
    for (uint32_t seed = 0; seed < cases; ++seed) {
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            const World w = Draw(seed, salt, allow);
            gLog.clear();
            ctx.Mark(side ? "ours" : "original");
            uint32_t result;
            difftest::AllocationCounter heap;
            difftest::FreeRecorder frees;
            gHeap = &heap;
            result = call(side, w);
            gHeap = nullptr;
            heap.Stop();
            frees.Stop();
            Blob o = Snapshot(result, heap.Blocks(), heap.Bytes());
            o.push_back(uint32_t(frees.Count())), o.push_back(frees.Overflowed());
            for (size_t i = 0; i < frees.Count(); ++i) o.push_back(uint32_t(frees.Size(i)));
            if (side == 0 && hit(result, w)) ++hits;
            outcome[side] = std::move(o);
        }
        ++ctx.cases;
        svcfx::Same(ctx, outcome[0], outcome[1], what, seed);
    }
    if (hits == 0) ctx.Fail("%s: no case reached the matching path", what);
    return hits;
}

bool Any(uint32_t result, const World &) { return result != 0; }

} // namespace dmfx

using namespace dmfx;

// 0x3abe70: the app singleton's object table's object under a sign-extended 16-bit id (high bits junk).
DIFF_TEST(sg_design_mode_object_by_id, 0x3abe70)
{
    Scope scope;
    using Fn = void *(__cdecl *)(uint32_t);
    auto original = ctx.Original<Fn>(0x3abe70);
    auto ours = reinterpret_cast<Fn>(&DesignMode::ObjectById);
    static const uint32_t kIds[] = {0, 1, 0x7fff, 0x8000, 0xffff, 0x12340005, 0xffff8001, 0x0001ffff, 0x80000000};
    Run(ctx, "object by id", 2000, 0x3abe70, {false, false},
        [&](int side, const World &) {
            std::mt19937 gen(gJunk);
            const uint32_t id = gen() % 3 ? kIds[gen() % std::size(kIds)] : gen();
            return Id(side == 0 ? original(id) : ours(id));
        },
        Any);
}

// 0x3aa260: the shapes under a node's children (portal faces skipped), appended to a list already holding 0-2.
DIFF_TEST(sg_design_mode_collect_shapes, 0x3aa260)
{
    Scope scope;
    using Fn = void(__cdecl *)(void *, void *);
    auto original = ctx.Original<Fn>(0x3aa260);
    auto ours = reinterpret_cast<Fn>(&DesignMode::CollectChildShapes);
    Run(ctx, "collect shapes", 3000, 0x3aa260, {true, true},
        [&](int side, const World &w) {
            std::mt19937 gen(gJunk);
            uint32_t size;
            {
                DesignMode::ShapeList list;
                {
                    difftest::NotCounted quiet;
                    for (uint32_t i = gen() % 3; i > 0; --i)
                        list.push_back(fx::CountedRef<nGZSceneGraph::cIShape>(
                            reinterpret_cast<nGZSceneGraph::cIShape *>(&gShapes[gen() % kShapes])));
                }
                L(0xc0), Probe();
                if (side == 0) original(w.root, &list);
                else ours(w.root, &list);
                L(0xc1), L(uint32_t(list.size()));
                for (const auto &e : list) L(Id(e.Get()));
                Probe();
                size = uint32_t(list.size());
            }
            return size;
        },
        [](uint32_t size, const World &) { return size > 2; });
}

// 0x3abe90: the shape's geometry's "tsDesignModeEnabled" list holds the subset (case-blind).
DIFF_TEST(sg_design_mode_is_subset, 0x3abe90)
{
    Scope scope;
    using Fn = bool(__cdecl *)(void *, void *);
    auto original = ctx.Original<Fn>(0x3abe90);
    auto ours = reinterpret_cast<Fn>(&DesignMode::IsDesignModeSubset);
    Run(ctx, "is design-mode subset", 4000, 0x3abe90, {false, true},
        [&](int side, const World &w) {
            std::mt19937 gen(gJunk);
            Shape *const shape = &gShapes[gen() % kShapes];
            // often a subset the geometry's list holds, in another case sometimes
            if (shape->geom && shape->geom->enabled && gen() % 2) {
                const List *l = shape->geom->enabled;
                gStrs[4].FromChar(kSubsets[l->items[gen() % 6]]);
            }
            (void)w;
            return uint32_t(side == 0 ? original(shape, &gStrs[4]) : ours(shape, &gStrs[4]));
        },
        Any);
}

// 0x3ab850: the subset's mesh name from "tsMaterialsMeshName", "_cres" appended when missing.
DIFF_TEST(sg_design_mode_mesh_name, 0x3ab850)
{
    Scope scope;
    using Fn = bool(__cdecl *)(void *, void *, void *);
    auto original = ctx.Original<Fn>(0x3ab850);
    auto ours = reinterpret_cast<Fn>(&DesignMode::MaterialsMeshName);
    Run(ctx, "materials mesh name", 4000, 0x3ab850, {false, true},
        [&](int side, const World &w) {
            std::mt19937 gen(gJunk);
            Shape *const shape = &gShapes[gen() % kShapes];
            cRZString *const out = &gStrs[w.outs[gen() % 4]];
            cRZString *const subset = gen() % 8 ? &gStrs[4] : out; // sometimes the out is the subset
            return uint32_t(side == 0 ? original(shape, subset, out) : ours(shape, subset, out));
        },
        Any);
}

// 0x3aab80: the object's first two design-mode subsets and their mesh names.
DIFF_TEST(sg_design_mode_object_names, 0x3aab80)
{
    Scope scope;
    using Fn = void(__cdecl *)(void *, void *, void *, void *, void *);
    auto original = ctx.Original<Fn>(0x3aab80);
    auto ours = reinterpret_cast<Fn>(&DesignMode::GetObjectDesignModeNames);
    Run(ctx, "object design-mode names", 5000, 0x3aab80, {false, true},
        [&](int side, const World &w) {
            std::mt19937 gen(gJunk);
            Node *const node = gen() % 12 ? &gNodes[gen() % 3] : nullptr;
            cRZString *const s[4] = {&gStrs[w.outs[0]], &gStrs[w.outs[1]], &gStrs[w.outs[2]], &gStrs[w.outs[3]]};
            if (side == 0) original(node, s[0], s[1], s[2], s[3]);
            else ours(node, s[0], s[1], s[2], s[3]);
            return uint32_t(gStrs[w.outs[0]].mString.size() != 0) | uint32_t(gStrs[w.outs[2]].mString.size() != 0) << 1;
        },
        [](uint32_t r, const World &w) { return (r & 2) != 0 && w.outs[0] != w.outs[2]; });
}

// 0x3aaec0: the names of the object's portal face's tree, or the four emptied.
DIFF_TEST(sg_design_mode_portal_names, 0x3aaec0)
{
    Scope scope;
    using Fn = void(__cdecl *)(void *, void *, void *, void *, void *);
    auto original = ctx.Original<Fn>(0x3aaec0);
    auto ours = reinterpret_cast<Fn>(&DesignMode::GetPortalDesignModeNames);
    Run(ctx, "portal design-mode names", 3000, 0x3aaec0, {false, true},
        [&](int side, const World &w) {
            std::mt19937 gen(gJunk);
            Obj *const object = gen() % 10 ? &gObj : nullptr;
            cRZString *const s[4] = {&gStrs[w.outs[0]], &gStrs[w.outs[1]], &gStrs[w.outs[2]], &gStrs[w.outs[3]]};
            if (side == 0) original(object, s[0], s[1], s[2], s[3]);
            else ours(object, s[0], s[1], s[2], s[3]);
            return uint32_t(gStrs[w.outs[0]].mString.size() != 0);
        },
        Any);
}

// 0x3ab990: the first shape in the tree whose geometry has the subset (case-blind).
DIFF_TEST(sg_design_mode_find_shape, 0x3ab990)
{
    Scope scope;
    using Fn = void *(__cdecl *)(void *, void *);
    auto original = ctx.Original<Fn>(0x3ab990);
    auto ours = reinterpret_cast<Fn>(&DesignMode::FindObjectDesignMode);
    Run(ctx, "find object design mode", 5000, 0x3ab990, {true, false},
        [&](int side, const World &) {
            std::mt19937 gen(gJunk);
            Node *const node = gen() % 12 ? &gNodes[gen() % 3] : nullptr;
            return Id(side == 0 ? original(node, &gStrs[4]) : ours(node, &gStrs[4]));
        },
        Any);
}

// 0x3ab2b0: the same search returning the geometry (no null shapes: the original does not skip them).
DIFF_TEST(sg_design_mode_find_geometry, 0x3ab2b0)
{
    Scope scope;
    using Fn = void *(__cdecl *)(void *, void *);
    auto original = ctx.Original<Fn>(0x3ab2b0);
    auto ours = reinterpret_cast<Fn>(&DesignMode::FindGeometryWithSubset);
    Run(ctx, "find geometry with subset", 5000, 0x3ab2b0, {false, false},
        [&](int side, const World &) {
            std::mt19937 gen(gJunk);
            Node *const node = gen() % 12 ? &gNodes[gen() % 3] : nullptr;
            return Id(side == 0 ? original(node, &gStrs[4]) : ours(node, &gStrs[4]));
        },
        Any);
}

// 0x3ab570: over a shape container, the first geometry with the subset.
DIFF_TEST(sg_design_mode_find_listed_geometry, 0x3ab570)
{
    Scope scope;
    using Fn = void *(__cdecl *)(void *, void *);
    auto original = ctx.Original<Fn>(0x3ab570);
    auto ours = reinterpret_cast<Fn>(&DesignMode::FindListedGeometryWithSubset);
    Run(ctx, "find listed geometry", 4000, 0x3ab570, {true, false},
        [&](int side, const World &) {
            std::mt19937 gen(gJunk);
            Cont *const cont = gen() % 10 ? &gCont : nullptr;
            return Id(side == 0 ? original(cont, &gStrs[4]) : ours(cont, &gStrs[4]));
        },
        Any);
}

// 0x3abc40: over a shape container, the first shape with the subset.
DIFF_TEST(sg_design_mode_find_listed_shape, 0x3abc40)
{
    Scope scope;
    using Fn = void *(__cdecl *)(void *, void *);
    auto original = ctx.Original<Fn>(0x3abc40);
    auto ours = reinterpret_cast<Fn>(&DesignMode::FindListedShapeWithSubset);
    Run(ctx, "find listed shape", 4000, 0x3abc40, {true, false},
        [&](int side, const World &) {
            std::mt19937 gen(gJunk);
            Cont *const cont = gen() % 10 ? &gCont : nullptr;
            return Id(side == 0 ? original(cont, &gStrs[4]) : ours(cont, &gStrs[4]));
        },
        Any);
}

// ---- lane 261111p: the slave lists, the material override, the name format, the subset-name set ----
//
// On top of the world above: each geometry's "tsDesignModeSlaveSubsets" list (GetString values from kSlaveTexts while
// gSlaveMode), geometries counted (slots 1 / 2), the framework at 0x10c9378 (saved and restored) and the fakes
// declared at the top, the container's name (slot 13), the object's slots 61 (the link), 124 (its base name), 290 (a
// material given) and 292 (whether it has one for a subset; 0, 1 or only a high bit). Strings the tests hand in may
// carry gLogStrVT, the game's own table with ToChar (5), Strlen (7) and CompareTo (15) noted, so "asked once" and
// "asked each time" differ in the log. Lists and sets handed in start with 0-3 entries; after the call each entry's
// text, count and table are compared.

namespace dmfx {

struct RawStr { // the std::string layout (MSVC 14.40)
    union {
        char buf[16];
        char *ptr;
    };
    uint32_t size, cap;
};
struct CandVec {
    void **begin, **end, **cap;
};

void *gStrGame[34];
uint32_t gObjLinkOn, gHasSalt, gApplied;
const char *gBaseName = "", *gContName = "";
char gAltBuf[128];
RawStr gAltName;

const char *const kBaseNames[] = {"", "base", "Base%s%d", "a_base_name_longer_than_fifteen_chars", "x", "%n%%",
                                  "a_much_longer_base_name_that_goes_on_and_on_past_one_hundred_characters_so_that_"
                                  "the_format_buffer_has_to_grow_x"};
const char *const kContNames[] = {"", "container", "Container_name_longer_than_fifteen", "%s%d", "wood"};

uint32_t VtTag(const void *vt)
{
    if (vt == GameAddress<void>(cRZString::kVTable)) return 1;
    if (vt == gLogStrVT) return 2;
    if (vt == cRZString::VTable()) return 3;
    return 4;
}

template <class C> void LogStrings(const C &c)
{
    L(uint32_t(c.size()));
    for (const cRZString &s : c) L(TextHash(s)), L(s.mRefCount), L(VtTag(s.mVTable));
}

// ---- the noted string slots ----
const char *__fastcall LogToChar(const cRZString *self, void *)
{
    L(0x6005), L(TextHash(*self));
    return reinterpret_cast<const char *(__thiscall *)(const cRZString *)>(gStrGame[5])(self);
}
uint32_t __fastcall LogStrlen(const cRZString *self, void *)
{
    L(0x6007), L(TextHash(*self)), Probe(); // the heap too: the length is asked after the texts are copied
    return reinterpret_cast<uint32_t(__thiscall *)(const cRZString *)>(gStrGame[7])(self);
}
int32_t __fastcall LogCompare(const cRZString *self, void *, const cRZString *other, uint32_t caseSensitive)
{
    L(0x600f), L(TextHash(*self)), L(TextHash(*other)), L(caseSensitive);
    using Fn = int32_t(__thiscall *)(const cRZString *, const cRZString *, uint32_t);
    return reinterpret_cast<Fn>(gStrGame[15])(self, other, caseSensitive);
}

// ---- the allocator service's blocks counted (its table copied with slots 3 / 4 wrapped; gSvcLive in Probe) ----
void *gSvcCountVT[6];
void *gSvcInner[2];
void *__fastcall CountedAlloc(void *self, void *, uint32_t bytes)
{
    void *const p = reinterpret_cast<void *(__thiscall *)(void *, uint32_t)>(gSvcInner[0])(self, bytes);
    if (p) ++gSvcLive;
    return p;
}
void __fastcall CountedFree(void *self, void *, void *mem)
{
    if (mem) --gSvcLive;
    reinterpret_cast<void(__thiscall *)(void *, void *)>(gSvcInner[1])(self, mem);
}

// Names over 15 characters by one (the hidden-return string's capacity boundary), and under; the find-override test
// gives them to the subset and FillCandidates to the candidates.
const char *const kEdgeNames[] = {"exactly_16_chars", "Exactly_16_CHARS", "exactly_15_char", "EXACTLY_15_CHAR",
                                  "exactly_17_chars_"};

// ---- the framework, the service, the candidates ----
void *ServiceAlloc(uint32_t bytes)
{
    void *const service = *GameAddress<void *>(0x10c95d4);
    using Fn = void *(__thiscall *)(void *, uint32_t);
    return reinterpret_cast<Fn>((*static_cast<void ***>(service))[3])(service, bytes);
}

uint32_t __fastcall FwService(Fw *self, void *, uint32_t clsid, uint32_t iid, void **out)
{
    L(0xf05), L(Id(self)), L(clsid), L(iid), L(Id(*out)), Probe();
    switch (self->mode) {
    case 0: return Junk(0);
    case 1: *out = &gSvc, ++gSvc.refs; return Junk(1);
    case 2: *out = &gSvc, ++gSvc.refs; return Junk(0);
    default: *out = nullptr; return Junk(1);
    }
}
uint32_t __fastcall SvcAddRef(Svc *self, void *) { return L(0xc01), Probe(), ++self->refs; }
// The caller's candidate vector while the search runs: one Release in some draws takes its last entry off, so where
// each loop reads the end (once, before it starts) shows.
CandVec *gVec;
int32_t gShrinkAt, gReleaseNo;
uint32_t __fastcall SvcRelease(Svc *self, void *) { return gVec = nullptr, L(0xc02), Probe(), --self->refs; }

// The vector the service hands back: none (no block), or a block from the allocator service of 0-5 candidates
// (counted), some first renamed to a case variant of the subset; null entries only after one that matches (the
// original asks every entry it reaches for its name, null or not).
void FillCandidates(CandVec *v, const char *subset)
{
    L(0xc0f), L(v->begin == nullptr), L(v->end == nullptr), L(v->cap == nullptr);
    std::mt19937 gen(Mix2(gJunk, Hash(subset)));
    for (Cand &c : gCands) {
        if (gen() % 3) continue;
        const char *same[std::size(kSubsets) + std::size(kEdgeNames)];
        uint32_t n = 0;
        for (const char *s : kSubsets)
            if (_stricmp(s, subset) == 0) same[n++] = s;
        for (const char *s : kEdgeNames)
            if (_stricmp(s, subset) == 0) same[n++] = s;
        if (n) c.name = same[gen() % n];
    }
    const uint32_t n = gen() % 6;
    if (n == 0 && gen() % 2) return;
    void **const block = static_cast<void **>(ServiceAlloc(4 * (n ? n : 1)));
    bool matched = false;
    for (uint32_t i = 0; i < n; ++i) {
        if (matched && gen() % 3 == 0) {
            block[i] = nullptr;
            continue;
        }
        Cand *const c = &gCands[gen() % kCands];
        ++c->refs;
        block[i] = c;
        matched = matched || (_stricmp(c->name, subset) == 0 && c->usable != 0);
    }
    v->begin = block, v->end = block + n, v->cap = block + (n ? n : 1);
    gVec = v, gReleaseNo = 0, gShrinkAt = gen() % 3 == 0 ? int32_t(gen() % 8) : -1;
}
uint32_t __fastcall SvcByMesh(Svc *self, void *, CandVec *v, const std::string *mesh, const std::string *subset,
                              uint32_t zero)
{
    L(0xc08), L(Id(self)), L(Hash(mesh->c_str())), L(uint32_t(mesh->size())), L(Hash(subset->c_str())),
        L(uint32_t(subset->size())), L(zero), Probe();
    FillCandidates(v, subset->c_str());
    return Junk(1);
}
uint32_t __fastcall SvcByKey(Svc *self, void *, CandVec *v, uint32_t key, const std::string *container,
                             const std::string *subset, uint32_t zero)
{
    L(0xc09), L(Id(self)), L(key), L(container ? Hash(container->c_str()) : 0xdead),
        L(subset ? Hash(subset->c_str()) : 0xdead), L(zero), Probe();
    FillCandidates(v, subset ? subset->c_str() : "");
    return Junk(1);
}

uint32_t __fastcall CandAddRef(Cand *self, void *) { return L(0xb01), L(Id(self)), Probe(), ++self->refs; }
uint32_t __fastcall CandRelease(Cand *self, void *)
{
    if (gVec && gReleaseNo++ == gShrinkAt && gVec->end > gVec->begin) --gVec->end, L(0xb0e);
    return L(0xb02), L(Id(self)), Probe(), --self->refs;
}
void WriteRaw(RawStr *s, const char *text, char *longBuffer)
{
    const uint32_t n = uint32_t(std::strlen(text));
    if (n > 15) {
        char *const buf = longBuffer ? longBuffer : static_cast<char *>(ServiceAlloc(n + 1));
        std::memcpy(buf, text, n + 1);
        s->ptr = buf, s->cap = n;
    } else {
        std::memcpy(s->buf, text, n + 1);
        s->cap = 15;
    }
    s->size = n;
}
// The name into the caller's string (a long one in an allocator-service buffer), or "zz" there and the name in
// another string returned.
RawStr *__fastcall CandName(Cand *self, void *, RawStr *out)
{
    L(0xb06), L(Id(self)), Probe();
    if (self->elsewhere) {
        WriteRaw(out, "zz", nullptr);
        WriteRaw(&gAltName, self->name, gAltBuf);
        return &gAltName;
    }
    WriteRaw(out, self->name, nullptr);
    return out;
}
uint32_t __fastcall CandUsable(Cand *self, void *) { return L(0xb07), L(Id(self)), self->usable; }

// ---- the object's chain, its names and materials; the container's name; the geometries' counts ----
void *__fastcall ObjLink(Obj *self, void *) { return L(0x93d), L(Id(self)), gObjLinkOn ? &gLink : nullptr; }
void *__fastcall LinkHolder(Link *self, void *) { return L(0xd1b), L(Id(self)), self->holder; }
uint32_t __fastcall HolderKey(Holder *self, void *) { return L(0xd1c), L(Id(self)), self->key; }
const char *__fastcall ObjBaseName(Obj *self, void *) { return L(0x97c), L(Id(self)), gBaseName; }
uint32_t __fastcall ObjApply(Obj *self, void *, Cand *material)
{
    L(0x9122), L(Id(self)), L(Id(material)), L(material ? material->refs : 0), Probe();
    ++gApplied;
    return Junk(1);
}
uint32_t __fastcall ObjHas(Obj *self, void *, const cRZString *subset)
{
    L(0x9124), L(Id(self)), L(SeenId(subset)), L(TextHash(*subset)), L(subset->mRefCount), Probe();
    const uint32_t h = Mix2(gHasSalt, TextHash(*subset)) % 4;    return h == 0 ? 0x100u : h == 1 ? 1u : 0u;
}
const char *__fastcall ContName(Cont *self, void *) { return L(0x80d), L(Id(self)), Probe(), gContName; }
uint32_t __fastcall GeomAddRef(Geom *self, void *) { return L(0x401), L(Id(self)), Probe(), ++self->refs; }
uint32_t __fastcall GeomRelease(Geom *self, void *) { return L(0x402), L(Id(self)), Probe(), --self->refs; }

struct ScopeP : Scope {
    uint32_t savedFramework;
    ScopeP() : savedFramework(*GameAddress<uint32_t>(0x10c9378))
    {
        std::memcpy(gStrGame, GameAddress<void>(cRZString::kVTable), sizeof gStrGame);
        std::memcpy(gLogStrVT, gStrGame, sizeof gLogStrVT);
        gLogStrVT[5] = V(&LogToChar), gLogStrVT[7] = V(&LogStrlen), gLogStrVT[15] = V(&LogCompare);
        for (void *&s : gFwVT) s = V(&Stray);
        for (void *&s : gSvcVT) s = V(&Stray);
        for (void *&s : gCandVT) s = V(&Stray);
        for (void *&s : gLinkVT) s = V(&Stray);
        for (void *&s : gHolderVT) s = V(&Stray);
        gFwVT[5] = V(&FwService);
        gSvcVT[1] = V(&SvcAddRef), gSvcVT[2] = V(&SvcRelease), gSvcVT[8] = V(&SvcByMesh), gSvcVT[9] = V(&SvcByKey);
        gCandVT[1] = V(&CandAddRef), gCandVT[2] = V(&CandRelease), gCandVT[6] = V(&CandName),
        gCandVT[7] = V(&CandUsable);
        gLinkVT[27] = V(&LinkHolder);
        gHolderVT[28] = V(&HolderKey);
        gObjVT[61] = V(&ObjLink), gObjVT[124] = V(&ObjBaseName), gObjVT[290] = V(&ObjApply),
        gObjVT[292] = V(&ObjHas);
        gContVT[13] = V(&ContName);
        gGeomVT[1] = V(&GeomAddRef), gGeomVT[2] = V(&GeomRelease);
        service = *GameAddress<void **>(0x10c95d4);
        serviceVT = *reinterpret_cast<void ***>(service);
        std::memcpy(gSvcCountVT, serviceVT, sizeof gSvcCountVT);
        gSvcInner[0] = gSvcCountVT[3], gSvcInner[1] = gSvcCountVT[4];
        gSvcCountVT[3] = V(&CountedAlloc), gSvcCountVT[4] = V(&CountedFree);
        *reinterpret_cast<void ***>(service) = gSvcCountVT;
    }
    ~ScopeP()
    {
        *reinterpret_cast<void ***>(service) = serviceVT;
        gSvcLive = 0;
        *GameAddress<uint32_t>(0x10c9378) = savedFramework;
    }
    void **service, **serviceVT;
};

// Each case's draw of the fakes above (after the world: the same on both sides).
void SetupP(std::mt19937 &gen)
{
    static const uint32_t kUsable[] = {0, 1, 1, 0x100, 0x80000000u};
    static const uint32_t kKeys[] = {0, 1, 0xffffffffu, 0x80000000u, 0x7fffffffu, 0x12345678u};
    gSvcLive = 0;
    gFw = {gFwVT, 0xf00, gen() % 4};
    gSvc = {gSvcVT, 0xc00, 0};
    for (int i = 0; i < kCands; ++i)
        gCands[i] = {gCandVT, 0xb00u + i, 0, kSubsets[gen() % std::size(kSubsets)], kUsable[gen() % 5],
                     uint32_t(gen() % 4 == 0)};
    *GameAddress<void *>(0x10c9378) = gen() % 8 ? static_cast<void *>(&gFw) : nullptr;
    gHolder = {gHolderVT, 0xd01, kKeys[gen() % std::size(kKeys)]};
    gLink = {gLinkVT, 0xd00, gen() % 6 ? &gHolder : nullptr};
    gObjLinkOn = gen() % 6 != 0;
    gHasSalt = gen();
    gApplied = 0;
    gBaseName = kBaseNames[gen() % std::size(kBaseNames)];
    gContName = kContNames[gen() % std::size(kContNames)];
    for (Geom &g : gGeoms) g.slave = gen() % 5 ? &gLists[gen() % kLists] : nullptr;
}

// After each call: every count of the fakes above.
void AfterP()
{
    L(0xeeef);
    for (const Cand &c : gCands) L(c.refs);
    L(gSvc.refs), L(gApplied), Probe();
}

// 0-2 strings already in a list (not counted).
uint32_t Prefill(std::mt19937 &gen, DesignMode::SubsetList &list)
{
    difftest::NotCounted quiet;
    for (uint32_t i = gen() % 3; i > 0; --i) list.emplace_back().Construct(kInits[gen() % std::size(kInits)]);
    return uint32_t(list.size());
}

// A subset of one of the container's shapes' geometries (or any).
const char *ListedSubset(std::mt19937 &gen)
{
    const Shape *const s = gCont.items[gen() % 6];
    if (gen() % 2 && s && s->geom) return kSubsets[s->geom->names[gen() % 6]];
    return kSubsets[SubsetIndex(gen)];
}

} // namespace dmfx

// 0x3aafa0: the geometry's slave list's value for the subset, split at ',' and appended to a list holding 0-2.
DIFF_TEST(sg_design_mode_slave_subsets, 0x3aafa0)
{
    ScopeP scope;
    using Fn = void(__cdecl *)(void *, const char *, void *);
    auto original = ctx.Original<Fn>(0x3aafa0);
    auto ours = reinterpret_cast<Fn>(&DesignMode::SlaveSubsets);
    Run(ctx, "slave subsets", 6000, 0x3aafa0, {false, false},
        [&](int side, const World &) {
            std::mt19937 gen(gJunk);
            SetupP(gen);
            Geom *const g = gen() % 12 ? &gGeoms[gen() % kGeoms] : nullptr;
            const char *const subset = gen() % 12 ? kSubsets[SubsetIndex(gen)] : nullptr;
            uint32_t added;
            {
                DesignMode::SubsetList list;
                const uint32_t before = Prefill(gen, list);
                gSlaveMode = true;
                L(0xc0), Probe();
                if (side == 0) original(g, subset, &list);
                else ours(g, subset, &list);
                gSlaveMode = false;
                L(0xc1), LogStrings(list), Probe();
                added = uint32_t(list.size()) - before;
            }
            AfterP();
            return added;
        },
        [](uint32_t added, const World &) { return added >= 2; });
}

// 0x3ab1d0: the same on the container's first geometry with the subset, held around it.
DIFF_TEST(sg_design_mode_listed_slave_subsets, 0x3ab1d0)
{
    ScopeP scope;
    using Fn = void(__cdecl *)(void *, const char *, void *);
    auto original = ctx.Original<Fn>(0x3ab1d0);
    auto ours = reinterpret_cast<Fn>(&DesignMode::ListedSlaveSubsets);
    Run(ctx, "listed slave subsets", 5000, 0x3ab1d0, {true, false},
        [&](int side, const World &) {
            std::mt19937 gen(gJunk);
            SetupP(gen);
            Cont *const cont = gen() % 10 ? &gCont : nullptr;
            const char *const subset = gen() % 10 ? ListedSubset(gen) : nullptr;
            uint32_t added;
            {
                DesignMode::SubsetList list;
                const uint32_t before = Prefill(gen, list);
                gSlaveMode = true;
                L(0xc0), Probe();
                if (side == 0) original(cont, subset, &list);
                else ours(cont, subset, &list);
                gSlaveMode = false;
                L(0xc1), LogStrings(list), Probe();
                added = uint32_t(list.size()) - before;
            }
            AfterP();
            return added;
        },
        [](uint32_t added, const World &) { return added >= 2; });
}

// 0x3aa720: the override the framework's service finds for (key, mesh, container name, subset).
DIFF_TEST(sg_design_mode_find_override, 0x3aa720)
{
    ScopeP scope;
    using Fn = void *(__cdecl *)(uint32_t, void *, void *, void *);
    auto original = ctx.Original<Fn>(0x3aa720);
    auto ours = reinterpret_cast<Fn>(&DesignMode::FindMaterialOverride);
    Run(ctx, "find material override", 8000, 0x3aa720, {false, false},
        [&](int side, const World &w) {
            std::mt19937 gen(gJunk);
            SetupP(gen);
            static const uint32_t kKeys[] = {0, 1, 0xffffffffu, 0x80000000u, 0x7fffffffu};
            const uint32_t key = gen() % 3 ? kKeys[gen() % std::size(kKeys)] : gen();
            cRZString *mesh = &gStrs[w.outs[0]];
            mesh->FromChar(gen() % 2 ? kMeshes[gen() % std::size(kMeshes)] : "");
            cRZString *container = &gStrs[w.outs[1]];
            container->FromChar(kContNames[gen() % std::size(kContNames)]);
            cRZString *const subset = &gStrs[4];
            if (gen() % 4 == 0) subset->FromChar(kEdgeNames[gen() % std::size(kEdgeNames)]);
            if (gen() % 10 == 0) mesh = subset;
            if (gen() % 10 == 0) container = subset;
            for (cRZString &s : gStrs) s.mVTable = gLogStrVT;
            L(0xc0), Probe();
            void *const r = side == 0 ? original(key, mesh, container, subset) : ours(key, mesh, container, subset);
            L(0xc1);
            for (cRZString &s : gStrs) s.mVTable = GameAddress<void>(cRZString::kVTable);
            AfterP();
            return Id(r);
        },
        Any);
}

// 0x3abf70: the object's primary and secondary design-mode materials applied from the container.
DIFF_TEST(sg_design_mode_apply_materials, 0x3abf70)
{
    ScopeP scope;
    using Fn = void(__cdecl *)(void *, void *);
    auto original = ctx.Original<Fn>(0x3abf70);
    auto ours = reinterpret_cast<Fn>(&DesignMode::ApplyDesignModeMaterials);
    Run(ctx, "apply design-mode materials", 8000, 0x3abf70, {false, false},
        [&](int side, const World &) {
            std::mt19937 gen(gJunk);
            SetupP(gen);
            // more of the names over 15 characters (the subset's copy for the shape search is a heap block then)
            for (Geom &g : gGeoms)
                for (uint8_t &n : g.names)
                    if (gen() % 4 == 0) n = uint8_t(10 + gen() % 2);
            for (List &l : gLists)
                for (uint8_t &it : l.items)
                    if (gen() % 4 == 0) it = uint8_t(10 + gen() % 2);
            Cont *const cont = gen() % 12 ? &gCont : nullptr;
            Obj *const object = gen() % 12 ? &gObj : nullptr;
            L(0xc0), Probe();
            if (side == 0) original(cont, object);
            else ours(cont, object);
            L(0xc1);
            AfterP();
            return gApplied;
        },
        Any);
}

// 0x3ab7a0: "<base>LOD<lod>" (with "_cres") into raw memory; the flag's high bytes junk.
DIFF_TEST(sg_design_mode_format_name, 0x3ab7a0)
{
    ScopeP scope;
    using Fn = void *(__cdecl *)(void *, void *, int32_t, uint32_t);
    auto original = ctx.Original<Fn>(0x3ab7a0);
    auto ours = reinterpret_cast<Fn>(&DesignMode::FormatMaterialName);
    Run(ctx, "format material name", 4000, 0x3ab7a0, {false, false},
        [&](int side, const World &) {
            std::mt19937 gen(gJunk);
            SetupP(gen);
            Obj *const object = gen() % 8 ? &gObj : nullptr;
            static const int32_t kLods[] = {-1, INT32_MIN, 0, 1, 9, 10, INT32_MAX, -10, 2};
            const int32_t lod = gen() % 4 ? kLods[gen() % std::size(kLods)] : int32_t(gen());
            static const uint32_t kFlags[] = {0, 1, 0x100, 0x101, 0xffffff00u, 0xff};
            const uint32_t cres = kFlags[gen() % std::size(kFlags)];
            alignas(4) unsigned char raw[sizeof(cRZString)];
            std::memset(raw, 0xcd, sizeof raw);
            L(0xc0), Probe();
            void *const r = side == 0 ? original(raw, object, lod, cres) : ours(raw, object, lod, cres);
            cRZString *const s = reinterpret_cast<cRZString *>(raw);
            L(0xc1), L(r == raw), L(TextHash(*s)), L(s->mRefCount), L(VtTag(s->mVTable)),
                L(uint32_t(s->mString.capacity())), Probe();
            const uint32_t length = uint32_t(s->mString.size());
            s->Destruct();
            AfterP();
            return length;
        },
        Any);
}

// 0x3aa420: every subset name under the node inserted into a set holding 0-3 (some with noted tables).
DIFF_TEST(sg_design_mode_collect_names, 0x3aa420)
{
    ScopeP scope;
    using Fn = void(__cdecl *)(void *, void *);
    auto original = ctx.Original<Fn>(0x3aa420);
    auto ours = reinterpret_cast<Fn>(&DesignMode::CollectSubsetNames);
    Run(ctx, "collect subset names", 5000, 0x3aa420, {false, true},
        [&](int side, const World &w) {
            std::mt19937 gen(gJunk);
            SetupP(gen);
            Node *const node = gen() % 3 ? w.root : &gNodes[gen() % 3];
            uint32_t added;
            {
                DesignMode::SubsetNameSet names;
                uint32_t before;
                {
                    difftest::NotCounted quiet;
                    for (uint32_t i = gen() % 4; i > 0; --i) {
                        cRZString s;
                        s.Construct(kSubsets[SubsetIndex(gen)]);
                        if (gen() % 2) s.mVTable = gLogStrVT;
                        names.insert(std::move(s));
                    }
                    before = uint32_t(names.size());
                }
                L(0xc0), Probe();
                if (side == 0) original(node, &names);
                else ours(node, &names);
                L(0xc1), LogStrings(names), Probe();
                added = uint32_t(names.size()) - before;
            }
            AfterP();
            return added;
        },
        [](uint32_t added, const World &) { return added >= 2; });
}
