// Differential tests for nGZSceneGraph's skin and CAS model helpers (lane 261111h, scenegraph SU7): FindSkinExtension
// 0xae9f70, FindSkinExtensionByName 0xaea100, AddSkinExtension 0xae9da0, RebindSkin 0xaea1f0, UndoSkin 0xaea360,
// UndoSkinsWithWord 0xaea4b0, UndoAllSkins 0xaea5c0, SetUpSkinning 0xae8fd0, LoadObjectModel 0xae8a20, LoadPersonPart
// 0xae97a0, and the branch filters' constructors 0xaea7a0 / 0xaea7c0.
//
// The world is hand-rolled (tests/cas_body_part_system_bones.cpp's shape): nodes (one vtable for every node interface
// the code asks: object graph, composition tree, transform, shape-ref, resource), extensions with a skin or data-list
// face over a virtual cIExtension base (vbptr {-4, 4}), shapes, scene-node iterators (slot 44's three), the bone
// traversal, the director framework (0x1ae18d, a stand-in on both sides) that creates skins, the traversal and shape-ref
// nodes, the framework service (cell 0x10c9378) handing out the resource manager, the allocator service (cell 0x10c95d4)
// the skin vectors grow through, and ParseKey (0xae6e60, a stand-in on both sides). Every call is noted with object ids
// (nodes 0x100+, extension faces 0x200+ and their bases 0x8200+, shapes 0x300+, iterators 0x500+, the traversal 0x600,
// allocator blocks 0x7000 | index << 8 | offset); at the end every object's count, links, shapes, pairs, refs and names.
// The helpers the functions call (GetNodeTag, FindDataList, RemoveDataLists, ShapeRefOfNodeOrChild, SetUpSkinning) are
// each side's own.
//
// Worlds stay inside the original's assumptions (ORIGINAL-BUG comments in src/sg/SGUtilitiesSkin.cpp): created objects,
// pair holders, skin owners and IK clones are never null where the code dereferences them unchecked; bone tags are
// unique case blind (one IK base clone per item) and null only where no tag comparison can reach them; skin words are
// distinct, a 3 never with a 7 or an 8; FindSkinExtensionByName's names and the skins' names are never null (_stricmp).
#include "code_patch.h"
#include "sgfx.h"

#include "game/game.h"
#include "misc/SmallVirtuals.h"
#include "rz/StaticGetters.h"
#include "rz/StringKeyParser.h"
#include "sg/SGNodeInterfaces.h"
#include "sg/SGUtilities.h"
#include "sg/cDataListExtension.h"
#include "sg/cFindBonesTraversal.h"
#include "sg/cShape.h"
#include "sg/cShapeRefNode.h"
#include "sg/cSkinExtension.h"

#include <array>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace su7 {

using namespace nGZSceneGraph;

constexpr uint32_t kSkinIid = cISkinExtension::kIid;
constexpr uint32_t kListIid = cIDataListExtension::kIid;
constexpr uint32_t kTransformIid = 0xe51c10f5;
constexpr uint32_t kShapeRefIid = cIShapeRefNode::kIid;
constexpr uint32_t kTreeIid = 0x4512dd13;
constexpr uint32_t kShapeRefClass = 0x65245517;

enum Kind : unsigned { kTransform = 1, kShapeRef = 2, kTree = 4 };
enum ExtKind : int { kPlain = 0, kSkin = 1, kList = 2 };

// ---- logging ----
void NoteW(const char *label, const std::vector<uint32_t> &words, const char *text = nullptr, bool hasText = false)
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = label;
    c.words = words;
    if (hasText) {
        c.words.push_back(text != nullptr);
        if (text) c.text = text;
    }
    drawfx::gLog.push_back(std::move(c));
}
void NoteT(const char *label, const char *text, std::initializer_list<uint32_t> words = {})
{
    NoteW(label, std::vector<uint32_t>(words), text, true);
}
uint32_t Bits(float f) { return std::bit_cast<uint32_t>(f); }

struct Text {
    bool set = false;
    char s[32] = {};
    void Put(const char *p)
    {
        set = p != nullptr;
        std::memset(s, 0, sizeof(s));
        if (p) strncpy_s(s, p, _TRUNCATE);
    }
    const char *Get() const { return set ? s : nullptr; }
};

// ---- the objects ----
struct Ext;
struct Node {
    const void *const *vt;
    uint32_t id;
    int32_t refs;
    const char *tag;
    unsigned kinds;
    unsigned qiMiss; // 0 null written, 1 out left alone, 2 this written (counted) and false
    unsigned findMiss; // FindExtension's miss: 0 null written, 1 left alone, 2 a counted extension written and false
    Node *parent, *firstChild, *nextSibling;
    Ext *firstExt;
    void *shapes[16];
    uint32_t shapeCount;
    Node *items[8];
    uint32_t itemCount;
    uint32_t resWord;
    void *added[24];
    uint32_t addedCount;
};

struct Ext {
    const void *const *vt; // the face (skin or data list)
    const int32_t *vbptr;
    const void *const *baseVt; // the virtual base cIExtension, at +8
    uint32_t id;
    int32_t refs;
    int kind;
    Node *owner;
    Ext *next;
    Ext *clone;
    // skin
    Text name;
    uint32_t word;
    uint32_t pairCount;
    void *pairs[16][2];
    uint32_t refCount;
    void *refList[16];
    uint32_t mapCount;
    Text mapName[8], mapKey[8];
    // data list
    const char *listName;
    bool has[4]; // sint, vector3, string, quaternion
    bool quatNoWrite; // GetQuaternion answers true without writing
    bool writeOnFalse; // a false answer still writes
    int32_t sint;
    float vec[3];
    float quat[4];
    const char *text;
};
constexpr int32_t kVbTable[2] = {-4, 4};
Ext *FromBase(const void *base) { return reinterpret_cast<Ext *>(const_cast<char *>(static_cast<const char *>(base)) - 8); }
void *BaseOf(Ext *e) { return e ? static_cast<void *>(&e->baseVt) : nullptr; }

struct Shape {
    const void *const *vt;
    uint32_t id;
    int32_t refs;
    int32_t referrers;
    Shape *clone;
};

struct Iter {
    const void *const *vt;
    uint32_t id;
    int32_t refs;
    Node *tree;
    uint32_t pos;
};

struct Plain {
    const void *const *vt;
    uint32_t id;
    int32_t refs;
};

struct Block {
    void *p;
    uint32_t size;
    bool freed;
};

constexpr unsigned kNodes = 48, kExts = 64, kShapes = 40, kIters = 48;

struct World {
    Node nodes[kNodes];
    unsigned nodeCount;
    Ext exts[kExts];
    unsigned extCount;
    Shape shapes[kShapes];
    unsigned shapeCount;
    Iter iters[kIters];
    unsigned iterCount;
    Plain bones, director, service, manager, allocator;
    Node *boneList[8];
    uint32_t boneCount;
    // what the director creates, in turn
    Ext *skinPool[4];
    unsigned skinPoolAt, skinPoolCount;
    Node *refPool[8];
    unsigned refPoolAt, refPoolCount;
    bool bonesMade;
    // FindExtension's miss mode 2
    Ext *missSkin, *missList;
    // the manager
    unsigned loadMode; // 0 found, 1 not found (null), 2 not found (a counted pointer)
    Node *loaded;
    bool parseResult;
    uint32_t falseWord; // a false answer's word (0 or 0x100: only the low byte is the bool)
    Block blocks[32];
    unsigned blockCount;
};
World W;

// ---- ids ----
uint32_t Id(const void *p)
{
    if (!p) return 0;
    for (unsigned i = 0; i < W.nodeCount; ++i)
        if (p == &W.nodes[i]) return W.nodes[i].id;
    for (unsigned i = 0; i < W.extCount; ++i) {
        if (p == &W.exts[i]) return W.exts[i].id;
        if (p == &W.exts[i].baseVt) return W.exts[i].id | 0x8000;
    }
    for (unsigned i = 0; i < W.shapeCount; ++i)
        if (p == &W.shapes[i]) return W.shapes[i].id;
    for (unsigned i = 0; i < W.iterCount; ++i)
        if (p == &W.iters[i]) return W.iters[i].id;
    for (const Plain *x : {&W.bones, &W.director, &W.service, &W.manager, &W.allocator})
        if (p == x) return x->id;
    for (unsigned i = 0; i < W.blockCount; ++i) {
        const auto *b = static_cast<const char *>(W.blocks[i].p);
        if (p >= b && p < b + W.blocks[i].size + 1) return 0x7000 | i << 8 | uint32_t(static_cast<const char *>(p) - b);
    }
    return 0xdead;
}
// The count of any object of the world (a face's is its extension's).
int32_t *RefsOf(const void *p)
{
    if (!p) return nullptr;
    for (unsigned i = 0; i < W.nodeCount; ++i)
        if (p == &W.nodes[i]) return &W.nodes[i].refs;
    for (unsigned i = 0; i < W.extCount; ++i)
        if (p == &W.exts[i] || p == &W.exts[i].baseVt) return &W.exts[i].refs;
    for (unsigned i = 0; i < W.shapeCount; ++i)
        if (p == &W.shapes[i]) return &W.shapes[i].refs;
    return nullptr;
}
void Count(const void *p, int delta)
{
    if (int32_t *r = RefsOf(p)) *r += delta;
}

// ---- vtables ----
std::array<const void *, 100> gNodeVt, gResVt;
std::array<const void *, 28> gSkinVt, gListVt, gBaseVt;
std::array<const void *, 36> gShapeVt;
std::array<const void *, 8> gIterVt, gDirectorVt, gServiceVt, gManagerVt, gAllocatorVt;
std::array<const void *, 24> gBonesVt;

// generic counting (slots 1 / 2 of nodes, shapes, iterators and the plain objects: all start {vt, id, refs})
uint32_t __fastcall AddRef(Plain *self, void *)
{
    drawfx::Note("AddRef", self->id);
    return uint32_t(++self->refs);
}
uint32_t __fastcall Release(Plain *self, void *)
{
    drawfx::Note("Release", self->id);
    return uint32_t(--self->refs);
}

// ---- nodes ----
uint32_t __fastcall NodeQuery(Node *self, void *, uint32_t iid, void **out)
{
    drawfx::Note("QueryInterface", {self->id, iid, out ? Id(*out) : 0xffffffffu});
    const bool hit = (iid == kTransformIid && (self->kinds & kTransform)) ||
                     (iid == kShapeRefIid && (self->kinds & kShapeRef)) || (iid == kTreeIid && (self->kinds & kTree));
    if (hit) {
        ++self->refs;
        *out = self;
        return 1;
    }
    if (self->qiMiss == 0) *out = nullptr;
    if (self->qiMiss == 2) {
        ++self->refs;
        *out = self;
    }
    return W.falseWord;
}
void __fastcall NodeAddExtension(Node *self, void *, void *ext)
{
    drawfx::Note("AddExtension", {self->id, Id(ext)});
    if (!ext) return;
    Count(ext, 1);
    if (self->addedCount < 24) self->added[self->addedCount++] = ext;
    FromBase(ext)->owner = self;
}
void __fastcall NodeRemoveExtension(Node *self, void *, void *ext)
{
    drawfx::Note("RemoveExtension", {self->id, Id(ext)});
    if (!ext) return;
    Ext *e = FromBase(ext);
    for (Ext **link = &self->firstExt; *link; link = &(*link)->next)
        if (*link == e) {
            *link = e->next;
            e->next = nullptr;
            e->owner = nullptr;
            --e->refs;
            return;
        }
}
uint32_t __fastcall NodeFind(Node *self, void *, uint32_t iid, void **out, void *start)
{
    drawfx::Note("FindExtension", {self->id, iid, Id(start), Id(*out)});
    const int want = iid == kSkinIid ? kSkin : iid == kListIid ? kList : -1;
    for (Ext *e = start ? FromBase(start) : self->firstExt; e; e = e->next)
        if (e->kind == want) {
            ++e->refs;
            *out = e;
            return 1;
        }
    if (self->findMiss == 0) *out = nullptr;
    if (self->findMiss == 2) {
        Ext *e = want == kSkin ? W.missSkin : W.missList;
        if (e) ++e->refs;
        *out = e;
    }
    return W.falseWord;
}
const char *__fastcall NodeTag(Node *self, void *)
{
    drawfx::Note("GetTag", self->id);
    return self->tag;
}
Node *__fastcall NodeParent(Node *self, void *)
{
    drawfx::Note("GetParent", self->id);
    return self->parent;
}
Node *__fastcall NodeNextSibling(Node *self, void *)
{
    drawfx::Note("GetNextSibling", self->id);
    return self->nextSibling;
}
uint32_t *__fastcall NodeChildren(Node *self, void *, uint32_t *out)
{
    drawfx::Note("GetChildren", self->id);
    out[0] = out[1] = drawfx::P(self->firstChild);
    out[2] = 0;
    return out;
}
uint32_t *__fastcall NodeIterate(Node *self, void *, uint32_t *out, uint32_t a, uint32_t filter)
{
    std::vector<uint32_t> w = {self->id, a, filter != 0};
    if (filter) {
        const uint32_t *f = reinterpret_cast<const uint32_t *>(uintptr_t(filter));
        const uint32_t rva = f[0] - drawfx::P(GameAddress<void>(0));
        w.push_back(rva);
        w.push_back(f[1]);
        if (rva == nRZSceneGraph::cBranchNodesMaskBitAndIIDFilter::kVTable) w.push_back(f[2]);
    }
    NoteW("MakeIterator44", w);
    for (unsigned k = 0; k < 3; ++k) {
        if (W.iterCount == kIters) {
            out[k] = 0;
            continue;
        }
        Iter &it = W.iters[W.iterCount];
        it = {gIterVt.data(), 0x500 + W.iterCount, 1, self, k == 2 ? self->itemCount : 0};
        ++W.iterCount;
        out[k] = drawfx::P(&it);
    }
    return out;
}
void __fastcall NodeAddChild(Node *self, void *, Node *child)
{
    drawfx::Note("AddChild", {self->id, Id(child)});
    if (!child) return;
    child->parent = self;
    child->nextSibling = self->firstChild;
    self->firstChild = child;
    ++child->refs;
}
void __fastcall NodeSetTranslation(Node *self, void *, const float *t)
{
    drawfx::Note("SetTranslation", {self->id, Bits(t[0]), Bits(t[1]), Bits(t[2])});
}
void __fastcall NodeSetRotation(Node *self, void *, const float *r)
{
    drawfx::Note("SetRotation", {self->id, Bits(r[0]), Bits(r[1]), Bits(r[2]), Bits(r[3])});
}
uint32_t __fastcall ResSlot64(Node *self, void *)
{
    drawfx::Note("Slot64", self->id);
    return self->resWord;
}
uint32_t *__fastcall NodeShapes(Node *self, void *, uint32_t *out)
{
    drawfx::Note("GetShapes", self->id);
    if (self->shapeCount) {
        out[0] = out[1] = drawfx::P(&self->shapes[0]);
        out[2] = drawfx::P(&self->shapes[self->shapeCount]);
    } else {
        out[0] = out[1] = out[2] = 0;
    }
    return out;
}
void __fastcall NodeAddShape(Node *self, void *, void *shape)
{
    drawfx::Note("AddShape", {self->id, Id(shape)});
    if (self->shapeCount == 16) return;
    self->shapes[self->shapeCount++] = shape;
    Count(shape, 1);
}
void __fastcall NodeRemoveShape(Node *self, void *, void *shape)
{
    drawfx::Note("RemoveShape", {self->id, Id(shape)});
    for (uint32_t i = 0; i < self->shapeCount; ++i)
        if (self->shapes[i] == shape) {
            for (uint32_t k = i + 1; k < self->shapeCount; ++k) self->shapes[k - 1] = self->shapes[k];
            --self->shapeCount;
            Count(shape, -1);
            return;
        }
}
uint32_t __fastcall NodeShapeCount(Node *self, void *)
{
    drawfx::Note("ShapeCount", self->id);
    return self->shapeCount;
}
uint32_t __fastcall NodeHasShape(Node *self, void *, void *shape)
{
    drawfx::Note("HasShape", {self->id, Id(shape)});
    for (uint32_t i = 0; i < self->shapeCount; ++i)
        if (self->shapes[i] == shape) return 1;
    return W.falseWord;
}

// ---- extensions: the base ----
uint32_t __fastcall BaseAddRef(void *self, void *)
{
    Ext *e = FromBase(self);
    drawfx::Note("AddRef", e->id | 0x8000);
    return uint32_t(++e->refs);
}
uint32_t __fastcall BaseRelease(void *self, void *)
{
    Ext *e = FromBase(self);
    drawfx::Note("Release", e->id | 0x8000);
    return uint32_t(--e->refs);
}
void *__fastcall BaseClone(void *self, void *)
{
    Ext *e = FromBase(self);
    drawfx::Note("Clone", e->id | 0x8000);
    return BaseOf(e->clone);
}
Node *__fastcall BaseOwner(void *self, void *)
{
    Ext *e = FromBase(self);
    drawfx::Note("GetOwner", e->id | 0x8000);
    return e->owner;
}
void *__fastcall BaseNext(void *self, void *)
{
    Ext *e = FromBase(self);
    drawfx::Note("GetNext", e->id | 0x8000);
    return BaseOf(e->next);
}

// ---- the skin face ----
void __fastcall SkinSetName(Ext *self, void *, const char *name)
{
    NoteT("SetName", name, {self->id});
    self->name.Put(name);
}
void __fastcall SkinSetWord(Ext *self, void *, uint32_t word)
{
    drawfx::Note("SetWord", {self->id, word});
    self->word = word;
}
void *__fastcall SkinPairFirst(Ext *self, void *, uint32_t i)
{
    drawfx::Note("GetPairFirst", {self->id, i});
    return i < self->pairCount ? self->pairs[i][0] : nullptr;
}
void *__fastcall SkinPairSecond(Ext *self, void *, uint32_t i)
{
    drawfx::Note("GetPairSecond", {self->id, i});
    return i < self->pairCount ? self->pairs[i][1] : nullptr;
}
void __fastcall SkinAddPair(Ext *self, void *, void *a, void *b)
{
    drawfx::Note("AddPair", {self->id, Id(a), Id(b)});
    if (self->pairCount == 16) return;
    self->pairs[self->pairCount][0] = a;
    self->pairs[self->pairCount][1] = b;
    ++self->pairCount;
    Count(a, 1);
    Count(b, 1);
}
void __fastcall SkinErasePair(Ext *self, void *, uint32_t i)
{
    drawfx::Note("ErasePair", {self->id, i});
    if (i >= self->pairCount) return;
    Count(self->pairs[i][0], -1);
    Count(self->pairs[i][1], -1);
    for (uint32_t k = i + 1; k < self->pairCount; ++k) {
        self->pairs[k - 1][0] = self->pairs[k][0];
        self->pairs[k - 1][1] = self->pairs[k][1];
    }
    --self->pairCount;
}
void __fastcall SkinSetPairSecond(Ext *self, void *, uint32_t i, void *b)
{
    drawfx::Note("SetPairSecond", {self->id, i, Id(b)});
    if (i >= self->pairCount) return;
    Count(b, 1);
    Count(self->pairs[i][1], -1);
    self->pairs[i][1] = b;
}
void __fastcall SkinMapName(Ext *self, void *, const char *name, const char *key)
{
    NoteT("MapName name", name, {self->id});
    NoteT("MapName key", key, {self->id});
    if (self->mapCount == 8) return;
    self->mapName[self->mapCount].Put(name);
    self->mapKey[self->mapCount].Put(key);
    ++self->mapCount;
}
void *__fastcall SkinGetRef(Ext *self, void *, uint32_t i)
{
    drawfx::Note("GetRef", {self->id, i});
    return i < self->refCount ? self->refList[i] : nullptr;
}
void __fastcall SkinPushRef(Ext *self, void *, void *ref)
{
    drawfx::Note("PushRef", {self->id, Id(ref)});
    if (self->refCount == 16) return;
    self->refList[self->refCount++] = ref;
    Count(ref, 1);
}
void __fastcall SkinEraseRef(Ext *self, void *, uint32_t i)
{
    drawfx::Note("EraseRef", {self->id, i});
    if (i >= self->refCount) return;
    Count(self->refList[i], -1);
    for (uint32_t k = i + 1; k < self->refCount; ++k) self->refList[k - 1] = self->refList[k];
    --self->refCount;
}
const char *__fastcall SkinName(Ext *self, void *)
{
    drawfx::Note("GetName", self->id);
    return self->name.Get();
}
uint32_t __fastcall SkinWord(Ext *self, void *)
{
    drawfx::Note("GetWord", self->id);
    return self->word;
}
uint32_t __fastcall SkinPairCount(Ext *self, void *)
{
    drawfx::Note("GetPairCount", self->id);
    return self->pairCount;
}
const char *__fastcall SkinMappedName(Ext *self, void *, uint32_t j)
{
    drawfx::Note("GetMappedName", {self->id, j});
    return j < self->mapCount ? self->mapName[j].Get() : nullptr;
}
const char *__fastcall SkinMappedKey(Ext *self, void *, uint32_t j)
{
    drawfx::Note("GetMappedKey", {self->id, j});
    return j < self->mapCount ? self->mapKey[j].Get() : nullptr;
}
uint32_t __fastcall SkinMapCount(Ext *self, void *)
{
    drawfx::Note("GetNameMapCount", self->id);
    return self->mapCount;
}
uint32_t __fastcall SkinRefCount(Ext *self, void *)
{
    drawfx::Note("GetRefCount", self->id);
    return self->refCount;
}

// ---- the data-list face ----
const char *__fastcall ListName(Ext *self, void *)
{
    drawfx::Note("GetListName", self->id);
    return self->listName;
}
uint32_t __fastcall ListSint32(Ext *self, void *, const char *name, int32_t *out)
{
    NoteT("GetSint32", name, {self->id});
    if (self->has[0] || self->writeOnFalse) *out = self->has[0] ? self->sint : self->sint + 100;
    return self->has[0] ? 1 : W.falseWord;
}
uint32_t __fastcall ListVector3(Ext *self, void *, const char *name, float *out)
{
    NoteT("GetVector3", name, {self->id});
    if (self->has[1] || self->writeOnFalse)
        for (int i = 0; i < 3; ++i) out[i] = self->vec[i];
    return self->has[1] ? 1 : W.falseWord;
}
uint32_t __fastcall ListString(Ext *self, void *, const char *name, void *out)
{
    NoteT("GetString", name, {self->id});
    if (self->has[2] || self->writeOnFalse) {
        const char *text = self->text ? self->text : "";
        using FromCharFn = void(__thiscall *)(void *, const char *, uint32_t);
        reinterpret_cast<FromCharFn>((*static_cast<void ***>(out))[3])(out, text, uint32_t(std::strlen(text)));
    }
    return self->has[2] ? 1 : W.falseWord;
}
uint32_t __fastcall ListQuaternion(Ext *self, void *, const char *name, float *out)
{
    NoteT("GetQuaternion", name, {self->id});
    if ((self->has[3] && !self->quatNoWrite) || (!self->has[3] && self->writeOnFalse))
        for (int i = 0; i < 4; ++i) out[i] = self->quat[i];
    return self->has[3] ? 1 : W.falseWord;
}

// ---- shapes ----
uint32_t __fastcall ShapeClone(Shape *self, void *, uint32_t mode, uint32_t iid, void **out, void *context)
{
    drawfx::Note("ShapeClone", {self->id, mode, iid, Id(*out), Id(context)});
    if (self->clone) ++self->clone->refs;
    *out = self->clone;
    return self->clone ? 1 : W.falseWord;
}
uint32_t __fastcall ShapeReferrers(Shape *self, void *)
{
    drawfx::Note("GetReferrerCount", self->id);
    return uint32_t(self->referrers);
}
void __fastcall ShapeBind(Shape *self, void *, const char *material, const char *subset)
{
    NoteT("BindMaterial material", material, {self->id});
    NoteT("BindMaterial subset", subset, {self->id});
}
void __fastcall ShapeBindAll(Shape *self, void *, uint32_t material)
{
    NoteT("BindMaterialToAllSubsets", reinterpret_cast<const char *>(uintptr_t(material)), {self->id});
}

// ---- iterators ----
uint32_t __fastcall IterEqual(Iter *self, void *, const Iter *other)
{
    drawfx::Note("IsEqual", {self->id, Id(other)});
    return other && self->pos == other->pos ? 1 : W.falseWord;
}
Node *__fastcall IterGet(Iter *self, void *)
{
    drawfx::Note("IterGet", self->id);
    return self->pos < self->tree->itemCount ? self->tree->items[self->pos] : nullptr;
}
void __fastcall IterNext(Iter *self, void *)
{
    drawfx::Note("IterNext", self->id);
    ++self->pos;
}

// ---- the bone traversal ----
void __fastcall BonesTraverse(Plain *self, void *, void *node)
{
    drawfx::Note("Traverse", {self->id, Id(node)});
}
void __fastcall BonesSetFilter(Plain *self, void *, int32_t filter)
{
    drawfx::Note("SetFilter", {self->id, uint32_t(filter)});
}
void __fastcall BonesSetUntagged(Plain *self, void *, uint32_t on)
{
    drawfx::Note("SetCollectUntagged", {self->id, on & 0xff});
}
Node *__fastcall BonesAt(Plain *self, void *, uint32_t i)
{
    drawfx::Note("At", {self->id, i});
    return i < W.boneCount ? W.boneList[i] : nullptr;
}
uint32_t __fastcall BonesCount(Plain *self, void *)
{
    drawfx::Note("Count", self->id);
    return W.boneCount;
}

// ---- the framework, the service, the manager, the allocator ----
uint32_t __fastcall DirectorCreate(Plain *, void *, uint32_t cls, uint32_t iid, void **out)
{
    drawfx::Note("GetClassObject", {cls, iid, Id(*out)});
    void *made = nullptr;
    if (cls == cSkinExtension::kClassId && W.skinPoolAt < W.skinPoolCount) made = W.skinPool[W.skinPoolAt++];
    else if (cls == cFindBonesTraversal::kClassId && !W.bonesMade) made = &W.bones, W.bonesMade = true;
    else if (cls == kShapeRefClass && W.refPoolAt < W.refPoolCount) made = W.refPool[W.refPoolAt++];
    if (made == &W.bones) ++W.bones.refs;
    else Count(made, 1);
    *out = made;
    return made ? 1 : 0;
}
void *__cdecl StandFramework()
{
    drawfx::Note("framework", 0);
    return &W.director;
}
uint32_t __fastcall ServiceGet(Plain *self, void *, uint32_t id, uint32_t iid, void **out)
{
    drawfx::Note("GetSystemService", {self->id, id, iid, Id(*out)});
    ++W.manager.refs;
    *out = &W.manager;
    return 1;
}
uint32_t __fastcall ManagerLoad(Plain *self, void *, const uint32_t *key, uint32_t iid, void **out, uint32_t a, uint32_t b)
{
    drawfx::Note("Load", {self->id, key[0], key[1], key[2], key[3], iid, Id(*out), a, b});
    if (W.loadMode == 1) {
        *out = nullptr;
        return W.falseWord;
    }
    ++W.loaded->refs;
    *out = W.loaded;
    return W.loadMode == 0 ? 1 : W.falseWord;
}
void *__fastcall AllocatorAllocate(Plain *self, void *, uint32_t size)
{
    drawfx::Note("Allocate", {self->id, size});
    if (W.blockCount == 32) return nullptr;
    difftest::NotCounted bookkeeping;
    void *p = std::malloc(size ? size : 1);
    W.blocks[W.blockCount++] = {p, size, false};
    return p;
}
void __fastcall AllocatorFree(Plain *self, void *, void *p)
{
    drawfx::Note("Free", {self->id, Id(p)});
    for (unsigned i = 0; i < W.blockCount; ++i)
        if (W.blocks[i].p == p) W.blocks[i].freed = true;
}
bool __cdecl ParseStand(StringKeyParser::Result *r, const char *text, uint32_t preset, const char *ext)
{
    NoteT("ParseKey", text, {r->value, r->id, r->hash24, r->crc32, preset, ext != nullptr});
    uint32_t h = 2166136261u;
    for (const char *s = text; s && *s; ++s) h = (h ^ uint8_t(*s)) * 16777619u;
    r->value = preset;
    r->id = h;
    r->hash24 = h >> 8 | 0xff000000u;
    r->crc32 = h ^ 0x5a5a5a5a;
    return W.parseResult;
}

void *__cdecl ImageNew(size_t size) { return ::operator new(size); }
void __cdecl ImageSizedDelete(void *p, size_t size) { ::operator delete(p, size); }

#define SU7_SLOT(table, i, fn) table[i] = reinterpret_cast<const void *>(&fn)
void SetUpVtables()
{
    gNodeVt = {};
    SU7_SLOT(gNodeVt, 0, NodeQuery);
    SU7_SLOT(gNodeVt, 1, AddRef);
    SU7_SLOT(gNodeVt, 2, Release);
    SU7_SLOT(gNodeVt, 22, NodeAddExtension);
    SU7_SLOT(gNodeVt, 23, NodeRemoveExtension);
    SU7_SLOT(gNodeVt, 24, NodeFind);
    SU7_SLOT(gNodeVt, 26, NodeTag);
    SU7_SLOT(gNodeVt, 34, NodeParent);
    SU7_SLOT(gNodeVt, 38, NodeNextSibling);
    SU7_SLOT(gNodeVt, 42, NodeChildren);
    SU7_SLOT(gNodeVt, 44, NodeIterate);
    SU7_SLOT(gNodeVt, 45, NodeAddChild);
    SU7_SLOT(gNodeVt, 64, NodeSetTranslation);
    SU7_SLOT(gNodeVt, 65, NodeSetRotation);
    SU7_SLOT(gNodeVt, 93, NodeShapes);
    SU7_SLOT(gNodeVt, 94, NodeAddShape);
    SU7_SLOT(gNodeVt, 95, NodeRemoveShape);
    SU7_SLOT(gNodeVt, 98, NodeShapeCount);
    SU7_SLOT(gNodeVt, 99, NodeHasShape);
    gResVt = gNodeVt;
    SU7_SLOT(gResVt, 64, ResSlot64);
    gResVt[65] = nullptr;

    gBaseVt = {};
    SU7_SLOT(gBaseVt, 1, BaseAddRef);
    SU7_SLOT(gBaseVt, 2, BaseRelease);
    SU7_SLOT(gBaseVt, 15, BaseClone);
    SU7_SLOT(gBaseVt, 18, BaseOwner);
    SU7_SLOT(gBaseVt, 20, BaseNext);

    gSkinVt = {};
    SU7_SLOT(gSkinVt, 0, SkinSetName);
    SU7_SLOT(gSkinVt, 1, SkinSetWord);
    SU7_SLOT(gSkinVt, 3, SkinPairFirst);
    SU7_SLOT(gSkinVt, 5, SkinPairSecond);
    SU7_SLOT(gSkinVt, 6, SkinAddPair);
    SU7_SLOT(gSkinVt, 7, SkinErasePair);
    SU7_SLOT(gSkinVt, 8, SkinSetPairSecond);
    SU7_SLOT(gSkinVt, 9, SkinMapName);
    SU7_SLOT(gSkinVt, 10, SkinGetRef);
    SU7_SLOT(gSkinVt, 11, SkinPushRef);
    SU7_SLOT(gSkinVt, 12, SkinEraseRef);
    SU7_SLOT(gSkinVt, 13, SkinName);
    SU7_SLOT(gSkinVt, 14, SkinWord);
    SU7_SLOT(gSkinVt, 15, SkinPairCount);
    SU7_SLOT(gSkinVt, 16, SkinMappedName);
    SU7_SLOT(gSkinVt, 17, SkinMappedKey);
    SU7_SLOT(gSkinVt, 18, SkinMapCount);
    SU7_SLOT(gSkinVt, 19, SkinRefCount);

    gListVt = {};
    SU7_SLOT(gListVt, 12, ListName);
    SU7_SLOT(gListVt, 14, ListSint32);
    SU7_SLOT(gListVt, 17, ListVector3);
    SU7_SLOT(gListVt, 18, ListString);
    SU7_SLOT(gListVt, 19, ListQuaternion);

    gShapeVt = {};
    SU7_SLOT(gShapeVt, 1, AddRef);
    SU7_SLOT(gShapeVt, 2, Release);
    SU7_SLOT(gShapeVt, 15, ShapeClone);
    SU7_SLOT(gShapeVt, 31, ShapeReferrers);
    SU7_SLOT(gShapeVt, 34, ShapeBind);
    SU7_SLOT(gShapeVt, 35, ShapeBindAll);

    gIterVt = {};
    SU7_SLOT(gIterVt, 1, AddRef);
    SU7_SLOT(gIterVt, 2, Release);
    SU7_SLOT(gIterVt, 4, IterEqual);
    SU7_SLOT(gIterVt, 5, IterGet);
    SU7_SLOT(gIterVt, 6, IterNext);

    gBonesVt = {};
    SU7_SLOT(gBonesVt, 1, AddRef);
    SU7_SLOT(gBonesVt, 2, Release);
    SU7_SLOT(gBonesVt, 3, BonesTraverse);
    SU7_SLOT(gBonesVt, 16, BonesSetFilter);
    SU7_SLOT(gBonesVt, 17, BonesSetUntagged);
    SU7_SLOT(gBonesVt, 21, BonesAt);
    SU7_SLOT(gBonesVt, 22, BonesCount);

    gDirectorVt = {};
    SU7_SLOT(gDirectorVt, 3, DirectorCreate);
    gServiceVt = {};
    SU7_SLOT(gServiceVt, 1, AddRef);
    SU7_SLOT(gServiceVt, 2, Release);
    SU7_SLOT(gServiceVt, 5, ServiceGet);
    gManagerVt = {};
    SU7_SLOT(gManagerVt, 1, AddRef);
    SU7_SLOT(gManagerVt, 2, Release);
    SU7_SLOT(gManagerVt, 3, ManagerLoad);
    gAllocatorVt = {};
    SU7_SLOT(gAllocatorVt, 1, AddRef);
    SU7_SLOT(gAllocatorVt, 2, Release);
    SU7_SLOT(gAllocatorVt, 3, AllocatorAllocate);
    SU7_SLOT(gAllocatorVt, 4, AllocatorFree);
}
#undef SU7_SLOT

// ---- building worlds ----
void ResetWorld()
{
    for (unsigned i = 0; i < W.blockCount; ++i) std::free(W.blocks[i].p);
    W.blockCount = 0;
    for (Node &n : W.nodes) n = {};
    for (Ext &e : W.exts) e = Ext{};
    for (Shape &s : W.shapes) s = {};
    for (Iter &it : W.iters) it = {};
    W.nodeCount = W.extCount = W.shapeCount = W.iterCount = 0;
    W.bones = {gBonesVt.data(), 0x600, 1};
    W.director = {gDirectorVt.data(), 0x610, 1};
    W.service = {gServiceVt.data(), 0x620, 1};
    W.manager = {gManagerVt.data(), 0x630, 1};
    W.allocator = {gAllocatorVt.data(), 0x640, 1};
    W.boneCount = 0;
    W.skinPoolAt = W.skinPoolCount = W.refPoolAt = W.refPoolCount = 0;
    W.bonesMade = false;
    W.missSkin = W.missList = nullptr;
    W.loadMode = 0;
    W.loaded = nullptr;
    W.parseResult = true;
    W.falseWord = 0;
}

struct Gen {
    std::mt19937 g;
    explicit Gen(uint32_t seed) : g(seed * 0x9e3779b1u + 0x57u) {}
    unsigned R(unsigned n) { return unsigned(g() % n); }
    bool OneIn(unsigned n) { return g() % n == 0; }
    template <class T, size_t N> T Pick(const T (&a)[N]) { return a[g() % N]; }
    float F() { return float(int(g() % 20001) - 10000) / 64.0f; }
};

Node &NewNode(unsigned kinds, const char *tag, bool resource = false)
{
    Node &n = W.nodes[W.nodeCount];
    n = {};
    n.vt = resource ? gResVt.data() : gNodeVt.data();
    n.id = 0x100 + W.nodeCount;
    n.refs = 1;
    n.kinds = kinds;
    n.tag = tag;
    ++W.nodeCount;
    return n;
}
Ext &NewExt(int kind)
{
    Ext &e = W.exts[W.extCount];
    e = Ext{};
    e.vt = kind == kSkin ? gSkinVt.data() : gListVt.data();
    e.vbptr = kVbTable;
    e.baseVt = gBaseVt.data();
    e.id = 0x200 + W.extCount;
    e.refs = 1;
    e.kind = kind;
    ++W.extCount;
    return e;
}
Shape &NewShape(Gen &gen)
{
    Shape &s = W.shapes[W.shapeCount];
    s = {gShapeVt.data(), 0x300 + W.shapeCount, 1, int32_t(gen.R(4)), nullptr};
    ++W.shapeCount;
    return s;
}
void Attach(Node &n, Ext &e)
{
    Ext **link = &n.firstExt;
    while (*link) link = &(*link)->next;
    *link = &e;
    e.owner = &n;
}
void GiveShape(Node &n, void *shape)
{
    n.shapes[n.shapeCount++] = shape;
    Count(shape, 1);
}

const char *const kSkinNames[] = {"Body", "body", "BODY", "bod", "bodyx", ""};
const char *const kTagPool[] = {"spine", "Spine", "head", "neck", "arm_l", "pelvis", "", nullptr};
const char *const kBoneBase[] = {"spine", "head", "neck", "arm_l", "pelvis", "hand_r"};
const char *const kBoneUpper[] = {"Spine", "Head", "NECK", "Arm_L", "pelvis", "HAND_R"};
const char *const kMaterials[] = {"mat_a", "Mat_A", "skin_01", "", nullptr};
const char *const kSubsets[] = {"body", "hair", "", nullptr};
const char *const kTreeLists[] = {"bone_adjust", "Bone_Adjust", "bone_adjus", "other"};
const char *const kItemLists[] = {"tsDynamicsBehavior.s", "TSDYNAMICSBEHAVIOR.S", "IKTargetMorphBase", "IKTargetMorphDelta",
                                  "other", "IKTargetMorphBase"};
const char *const kBoneLists[] = {"IKTargetMorphBase", "IKTargetMorphDelta", "ikTargetMorphDelta", "other"};
const char *const kAdjustTexts[] = {"spine", "Spine", "head", "hand_r", "nothere", ""};

void FillList(Gen &gen, Ext &e, const char *name, const uint32_t *types, unsigned typeCount)
{
    e.listName = name;
    for (bool &h : e.has) h = gen.R(4) != 0;
    e.quatNoWrite = gen.OneIn(5);
    e.writeOnFalse = gen.OneIn(4);
    e.sint = int32_t(typeCount ? types[gen.R(typeCount)] : gen.R(10));
    for (float &f : e.vec) f = gen.F();
    for (float &f : e.quat) f = gen.F();
    e.text = gen.Pick(kAdjustTexts);
}

// The skins' words for SetUpSkinning's worlds: distinct, a 3 never with a 7 or an 8.
unsigned PickWords(Gen &gen, uint32_t *words, unsigned want)
{
    static const uint32_t kWords[] = {1, 2, 3, 4, 5, 7, 8};
    unsigned n = 0;
    for (unsigned tries = 0; n < want && tries < 40; ++tries) {
        const uint32_t w = gen.Pick(kWords);
        bool ok = true;
        for (unsigned i = 0; i < n; ++i)
            if (words[i] == w || (w == 3 && (words[i] == 7 || words[i] == 8)) || (words[i] == 3 && (w == 7 || w == 8)))
                ok = false;
        if (ok) words[n++] = w;
    }
    return n;
}

// Bones: `n` transform nodes, tags distinct case blind (some upper case), null tags allowed when `nullTags`.
void MakeBones(Gen &gen, unsigned n, bool nullTags, bool shapeRefs)
{
    bool used[6] = {};
    W.boneCount = 0;
    for (unsigned i = 0; i < n; ++i) {
        unsigned k = gen.R(6);
        while (used[k]) k = (k + 1) % 6;
        used[k] = true;
        const char *tag = gen.OneIn(3) ? kBoneUpper[k] : kBoneBase[k];
        if (nullTags && gen.OneIn(3)) tag = nullptr;
        Node &b = NewNode(kTransform | kTree, tag);
        b.qiMiss = gen.R(3);
        if (shapeRefs && gen.OneIn(4)) {
            b.kinds |= kShapeRef;
            for (unsigned s = gen.R(3); s; --s) GiveShape(b, &NewShape(gen));
        } else if (shapeRefs && gen.OneIn(3)) {
            Node &c = NewNode(gen.OneIn(4) ? kTree : kShapeRef | kTree, "child");
            c.qiMiss = gen.R(3);
            c.parent = &b;
            b.firstChild = &c;
            for (unsigned s = gen.R(3); s; --s) GiveShape(c, &NewShape(gen));
        }
        W.boneList[W.boneCount++] = &b;
    }
}

// Lists on a bone (what the IK pass removes).
void BoneLists(Gen &gen, Node &bone)
{
    for (unsigned k = gen.R(3); k; --k) {
        Ext &e = NewExt(gen.OneIn(4) ? kPlain : kList);
        FillList(gen, e, gen.Pick(kBoneLists), nullptr, 0);
        Attach(bone, e);
    }
}

// The tree SetUpSkinning reads: bone_adjust lists, items with dynamics and IK lists, their clones.
void MakeTree(Gen &gen, Node &tree, const uint32_t *words, unsigned wordCount, bool lists, bool plain)
{
    uint32_t types[10];
    unsigned typeCount = 0;
    for (unsigned i = 0; i < wordCount; ++i) types[typeCount++] = words[i];
    types[typeCount++] = 7;
    types[typeCount++] = 8;
    types[typeCount++] = 6;
    if (lists)
        for (unsigned k = gen.R(4); k; --k) {
            Ext &e = NewExt(gen.OneIn(5) ? kPlain : kList);
            FillList(gen, e, gen.Pick(kTreeLists), nullptr, 0);
            Attach(tree, e);
        }
    tree.itemCount = 0;
    for (unsigned k = gen.R(7); k; --k) {
        const char *tag = plain ? (gen.OneIn(2) ? "" : nullptr) : gen.Pick(kTagPool);
        if (!plain && gen.OneIn(4)) tag = gen.Pick(kBoneUpper);
        Node &item = NewNode(gen.OneIn(4) ? kTree : kTransform | kTree, tag);
        item.qiMiss = gen.R(3);
        if (lists) {
            Ext *baseClone = nullptr;
            for (unsigned l = gen.R(5); l; --l) {
                Ext &e = NewExt(gen.OneIn(6) ? kPlain : kList);
                FillList(gen, e, gen.Pick(kItemLists), types, typeCount);
                const bool delta = std::strcmp(e.listName, "IKTargetMorphDelta") == 0;
                const bool base = std::strcmp(e.listName, "IKTargetMorphBase") == 0;
                if (base) {
                    e.clone = &NewExt(kList); // never null: the delta loop compares against it
                    e.clone->listName = "IKTargetMorphBase";
                    baseClone = e.clone;
                } else if (delta) {
                    const unsigned c = gen.R(3);
                    e.clone = c == 0 ? nullptr : c == 1 || !baseClone ? &NewExt(kList) : baseClone;
                    if (e.clone && e.clone != baseClone) e.clone->listName = "IKTargetMorphDelta";
                } else if (!gen.OneIn(5)) {
                    e.clone = &NewExt(kList);
                    e.clone->listName = "clone";
                }
                Attach(item, e);
            }
        }
        if (tree.itemCount < 8) tree.items[tree.itemCount++] = &item;
    }
}

void SkinPool(unsigned n)
{
    for (unsigned i = 0; i < n; ++i) {
        Ext &e = NewExt(kSkin);
        e.refs = 0;
        W.skinPool[W.skinPoolCount++] = &e;
    }
}
void RefPool(unsigned n)
{
    for (unsigned i = 0; i < n; ++i) {
        Node &r = NewNode(kShapeRef | kTree, nullptr);
        r.refs = 0;
        W.refPool[W.refPoolCount++] = &r;
    }
}

void SetCells(drawfx::Cells &cells)
{
    cells.Set(0x10c9378, &W.service);
    cells.Set(0x10c95d4, &W.allocator);
    cells.Set(0x10c95d8, uint32_t(INT_MIN));
}

// Every object's state.
void NoteWorld()
{
    for (unsigned i = 0; i < W.nodeCount; ++i) {
        const Node &n = W.nodes[i];
        std::vector<uint32_t> w = {n.id, uint32_t(n.refs), Id(n.parent), Id(n.firstChild), Id(n.nextSibling),
                                   Id(n.firstExt), n.shapeCount};
        for (uint32_t k = 0; k < n.shapeCount; ++k) w.push_back(Id(n.shapes[k]));
        w.push_back(n.addedCount);
        for (uint32_t k = 0; k < n.addedCount; ++k) w.push_back(Id(n.added[k]));
        NoteW("node", w);
    }
    for (unsigned i = 0; i < W.extCount; ++i) {
        const Ext &e = W.exts[i];
        std::vector<uint32_t> w = {e.id, uint32_t(e.refs), Id(e.owner), Id(e.next), e.word, e.pairCount};
        for (uint32_t k = 0; k < e.pairCount; ++k) w.push_back(Id(e.pairs[k][0])), w.push_back(Id(e.pairs[k][1]));
        w.push_back(e.refCount);
        for (uint32_t k = 0; k < e.refCount; ++k) w.push_back(Id(e.refList[k]));
        w.push_back(e.mapCount);
        std::string text = e.name.set ? e.name.s : "(null)";
        for (uint32_t k = 0; k < e.mapCount; ++k) {
            text += "|";
            text += e.mapName[k].set ? e.mapName[k].s : "(null)";
            text += "=";
            text += e.mapKey[k].set ? e.mapKey[k].s : "(null)";
        }
        NoteW("ext", w, text.c_str(), true);
    }
    for (unsigned i = 0; i < W.shapeCount; ++i) drawfx::Note("shape", {W.shapes[i].id, uint32_t(W.shapes[i].refs)});
    for (unsigned i = 0; i < W.iterCount; ++i) drawfx::Note("iterator", {W.iters[i].id, uint32_t(W.iters[i].refs)});
    drawfx::Note("plain", {uint32_t(W.bones.refs), uint32_t(W.manager.refs), uint32_t(W.service.refs),
                           uint32_t(W.allocator.refs), W.skinPoolAt, W.refPoolAt, W.bonesMade});
    for (unsigned i = 0; i < W.blockCount; ++i) drawfx::Note("block", {i, W.blocks[i].size, W.blocks[i].freed});
}

// The standard check: seeds worlds, each side runs, the logs compared.
template <class Build, class Run> void Check(difftest::Context &ctx, const char *what, unsigned seeds, Build &&build, Run &&run)
{
    SetUpVtables();
    sgfx::CheckSides(
        ctx, what, seeds,
        [&](uint32_t seed, drawfx::Cells &cells) {
            ResetWorld();
            SetCells(cells);
            Gen gen(seed);
            build(seed, gen);
        },
        [&](bool original, std::mt19937 &) {
            run(original);
            NoteWorld();
        });
    ResetWorld();
}

// ---- the find tests' world ----
struct FindCase {
    Node *node;
    Ext *after;
    uint32_t word;
    const char *name;
};
FindCase gFind;
void BuildFind(Gen &gen)
{
    Node &node = NewNode(kTree, "n");
    node.findMiss = gen.R(3);
    W.falseWord = gen.OneIn(3) ? 0x100 : 0;
    Ext *skins[8];
    unsigned skinCount = 0;
    for (unsigned k = gen.R(7); k; --k) {
        const unsigned r = gen.R(4);
        Ext &e = NewExt(r < 2 ? kSkin : r == 2 ? kList : kPlain);
        if (e.kind == kSkin) {
            e.word = 1 + gen.R(3);
            e.name.Put(gen.Pick(kSkinNames));
            skins[skinCount++] = &e;
        } else {
            e.listName = "list";
        }
        Attach(node, e);
    }
    Ext &miss = NewExt(kSkin);
    miss.word = 1;
    miss.name.Put("body");
    W.missSkin = &miss;
    gFind = {&node, skinCount && !gen.OneIn(3) ? skins[gen.R(skinCount)] : nullptr, gen.R(4),
             gen.Pick(kSkinNames)};
}

} // namespace su7

using namespace su7;

DIFF_TEST(SG_UtilitiesSkin_FindSkinExtension, 0xae9f70)
{
    using Fn = void *(__cdecl *)(void *, uint32_t, void *);
    const Fn original = ctx.Original<Fn>(0xae9f70);
    Check(ctx, "FindSkinExtension", 400, [](uint32_t, Gen &gen) { BuildFind(gen); },
          [&](bool isOriginal) {
              drawfx::Note("call", {gFind.node->id, gFind.word, Id(gFind.after)});
              void *found = isOriginal ? original(gFind.node, gFind.word, gFind.after)
                                       : FindSkinExtension(reinterpret_cast<cIObjectGraphNode *>(gFind.node), gFind.word,
                                                           reinterpret_cast<cISkinExtension *>(gFind.after));
              drawfx::Note("found", Id(found));
          });
}

DIFF_TEST(SG_UtilitiesSkin_FindSkinExtensionByName, 0xaea100)
{
    using Fn = void *(__cdecl *)(void *, const char *, void *);
    const Fn original = ctx.Original<Fn>(0xaea100);
    Check(ctx, "FindSkinExtensionByName", 400, [](uint32_t, Gen &gen) { BuildFind(gen); },
          [&](bool isOriginal) {
              NoteT("call", gFind.name, {gFind.node->id, Id(gFind.after)});
              void *found = isOriginal ? original(gFind.node, gFind.name, gFind.after)
                                       : FindSkinExtensionByName(reinterpret_cast<cIObjectGraphNode *>(gFind.node),
                                                                 gFind.name,
                                                                 reinterpret_cast<cISkinExtension *>(gFind.after));
              drawfx::Note("found", Id(found));
          });
}

namespace su7 {
struct AddCase {
    Node *node;
    const char *name;
    uint32_t word;
    int32_t filter;
};
AddCase gAdd;
} // namespace su7

DIFF_TEST(SG_UtilitiesSkin_AddSkinExtension, 0xae9da0)
{
    using Fn = void *(__cdecl *)(void *, const char *, uint32_t, int32_t);
    const Fn original = ctx.Original<Fn>(0xae9da0);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    Check(
        ctx, "AddSkinExtension", 400,
        [](uint32_t, Gen &gen) {
            W.falseWord = gen.OneIn(3) ? 0x100 : 0;
            Node &node = NewNode(kTree, "root");
            SkinPool(1);
            W.boneCount = 0;
            for (unsigned k = gen.R(6); k; --k) {
                Node &b = NewNode(kTransform | kTree, gen.Pick(kTagPool));
                b.qiMiss = gen.R(3);
                if (!gen.OneIn(3)) {
                    b.kinds |= kShapeRef;
                    for (unsigned s = gen.R(4); s; --s) GiveShape(b, gen.OneIn(6) ? nullptr : &NewShape(gen));
                }
                W.boneList[W.boneCount++] = &b;
            }
            static const int32_t kFilters[] = {-1, 0, 5, -7};
            gAdd = {&node, gen.Pick(kSkinNames), uint32_t(gen.g()), gen.Pick(kFilters)};
            if (gen.OneIn(6)) gAdd.name = nullptr;
        },
        [&](bool isOriginal) {
            NoteT("call", gAdd.name, {gAdd.node->id, gAdd.word, uint32_t(gAdd.filter)});
            void *skin = isOriginal ? original(gAdd.node, gAdd.name, gAdd.word, gAdd.filter)
                                    : AddSkinExtension(reinterpret_cast<cICompositionTreeNode *>(gAdd.node), gAdd.name,
                                                       gAdd.word, gAdd.filter);
            drawfx::Note("skin", Id(skin));
        });
}

namespace su7 {
struct RebindCase {
    Ext *skin;
    const char *material, *subset;
};
RebindCase gRebind;
} // namespace su7

DIFF_TEST(SG_UtilitiesSkin_RebindSkin, 0xaea1f0)
{
    using Fn = void(__cdecl *)(void *, const char *, const char *);
    const Fn original = ctx.Original<Fn>(0xaea1f0);
    Check(
        ctx, "RebindSkin", 400,
        [](uint32_t, Gen &gen) {
            W.falseWord = gen.OneIn(3) ? 0x100 : 0;
            Node *holders[3];
            const unsigned holderCount = 1 + gen.R(3);
            for (unsigned i = 0; i < holderCount; ++i) holders[i] = &NewNode(kShapeRef | kTree, "h");
            Shape *shapes[6];
            const unsigned shapeCount = 1 + gen.R(6);
            for (unsigned i = 0; i < shapeCount; ++i) {
                Shape &s = NewShape(gen);
                s.clone = &NewShape(gen); // never null: the original binds it unchecked
                shapes[i] = &s;
            }
            Ext &skin = NewExt(kSkin);
            skin.name.Put("skin");
            for (unsigned k = gen.R(6); k; --k) {
                Node *h = holders[gen.R(holderCount)];
                Shape *s = gen.OneIn(7) ? nullptr : shapes[gen.R(shapeCount)];
                skin.pairs[skin.pairCount][0] = h;
                skin.pairs[skin.pairCount][1] = s;
                ++skin.pairCount;
                Count(h, 1);
                Count(s, 1);
                if (s && !gen.OneIn(3) && h->shapeCount < 16) GiveShape(*h, s);
            }
            if (gen.OneIn(3)) {
                skin.mapName[0].Put("old");
                skin.mapKey[0].Put("key");
                skin.mapCount = 1;
            }
            gRebind = {&skin, gen.Pick(kMaterials), gen.Pick(kSubsets)};
        },
        [&](bool isOriginal) {
            NoteT("call material", gRebind.material, {gRebind.skin->id});
            NoteT("call subset", gRebind.subset);
            if (isOriginal) original(gRebind.skin, gRebind.material, gRebind.subset);
            else RebindSkin(reinterpret_cast<cISkinExtension *>(gRebind.skin), gRebind.material, gRebind.subset);
        });
}

namespace su7 {
// A skin with pairs (holders never null) and refs (null, or an extension of the world that is not a skin: an owned one
// on `node` or another node, or one without an owner).
void FillUndoSkin(Gen &gen, Ext &skin, Node &node, Node &other)
{
    Node *holders[2] = {&NewNode(kShapeRef | kTree, "h"), &NewNode(kShapeRef | kTree, "h")};
    for (unsigned k = gen.R(5); k; --k) {
        Node *h = holders[gen.R(2)];
        Shape *s = gen.OneIn(6) ? nullptr : &NewShape(gen);
        skin.pairs[skin.pairCount][0] = h;
        skin.pairs[skin.pairCount][1] = s;
        ++skin.pairCount;
        Count(h, 1);
        Count(s, 1);
        if (s && !gen.OneIn(3)) GiveShape(*h, s);
    }
    for (unsigned k = gen.R(4); k; --k) {
        void *ref = nullptr;
        const unsigned r = gen.R(4);
        if (r == 1) {
            Ext &e = NewExt(kList);
            e.listName = "ref";
            Attach(gen.OneIn(2) ? node : other, e);
            ref = BaseOf(&e);
        } else if (r == 2) {
            Ext &e = NewExt(kPlain);
            ref = BaseOf(&e);
        } else if (r == 3) {
            // one of the node's own non-skin extensions, when there is one
            for (Ext *e = node.firstExt; e; e = e->next)
                if (e->kind != kSkin && gen.OneIn(2)) ref = BaseOf(e);
        }
        skin.refList[skin.refCount++] = ref;
        Count(ref, 1);
    }
}

// A node's chain: skins (words 1-3, owned by the node), lists, plain extensions.
void FillChain(Gen &gen, Node &node, Node &other, unsigned max)
{
    for (unsigned k = gen.R(max + 1); k; --k) {
        const unsigned r = gen.R(4);
        Ext &e = NewExt(r < 2 ? kSkin : r == 2 ? kList : kPlain);
        e.listName = "list";
        Attach(node, e);
    }
    for (Ext *e = node.firstExt; e; e = e->next)
        if (e->kind == kSkin) {
            e->word = 1 + gen.R(3);
            e->name.Put("s");
            FillUndoSkin(gen, *e, node, other);
        }
}
Ext *gUndo;
Node *gUndoNode;
uint32_t gUndoWord;
} // namespace su7

DIFF_TEST(SG_UtilitiesSkin_UndoSkin, 0xaea360)
{
    using Fn = void(__cdecl *)(void *);
    const Fn original = ctx.Original<Fn>(0xaea360);
    Check(
        ctx, "UndoSkin", 400,
        [](uint32_t, Gen &gen) {
            W.falseWord = gen.OneIn(3) ? 0x100 : 0;
            Node &node = NewNode(kTree, "n");
            Node &other = NewNode(kTree, "o");
            for (unsigned k = gen.R(3); k; --k) {
                Ext &e = NewExt(kList);
                e.listName = "x";
                Attach(node, e);
            }
            Ext &skin = NewExt(kSkin);
            skin.name.Put("s");
            Attach(node, skin);
            for (unsigned k = gen.R(3); k; --k) {
                Ext &e = NewExt(kPlain);
                Attach(node, e);
            }
            FillUndoSkin(gen, skin, node, other);
            gUndo = &skin;
        },
        [&](bool isOriginal) {
            drawfx::Note("call", gUndo->id);
            if (isOriginal) original(gUndo);
            else UndoSkin(reinterpret_cast<cISkinExtension *>(gUndo));
        });
}

DIFF_TEST(SG_UtilitiesSkin_UndoSkinsWithWord, 0xaea4b0)
{
    using Fn = void(__cdecl *)(void *, uint32_t);
    const Fn original = ctx.Original<Fn>(0xaea4b0);
    Check(
        ctx, "UndoSkinsWithWord", 400,
        [](uint32_t, Gen &gen) {
            W.falseWord = gen.OneIn(3) ? 0x100 : 0;
            Node &node = NewNode(kTree, "n");
            Node &other = NewNode(kTree, "o");
            node.findMiss = gen.R(3);
            Ext &miss = NewExt(kSkin);
            miss.word = 9;
            W.missSkin = &miss;
            FillChain(gen, node, other, 6);
            gUndoNode = &node;
            gUndoWord = gen.R(4);
        },
        [&](bool isOriginal) {
            drawfx::Note("call", {gUndoNode->id, gUndoWord});
            if (isOriginal) original(gUndoNode, gUndoWord);
            else UndoSkinsWithWord(reinterpret_cast<cIObjectGraphNode *>(gUndoNode), gUndoWord);
        });
}

DIFF_TEST(SG_UtilitiesSkin_UndoAllSkins, 0xaea5c0)
{
    using Fn = void(__cdecl *)(void *);
    const Fn original = ctx.Original<Fn>(0xaea5c0);
    Check(
        ctx, "UndoAllSkins", 400,
        [](uint32_t, Gen &gen) {
            W.falseWord = gen.OneIn(3) ? 0x100 : 0;
            Node &node = NewNode(kTree, "n");
            Node &other = NewNode(kTree, "o");
            // A miss that writes an extension (mode 2) would hand back a skin forever: modes 0 and 1 only.
            node.findMiss = gen.R(2);
            FillChain(gen, node, other, 5);
            gUndoNode = &node;
        },
        [&](bool isOriginal) {
            drawfx::Note("call", gUndoNode->id);
            if (isOriginal) original(gUndoNode);
            else UndoAllSkins(reinterpret_cast<cIObjectGraphNode *>(gUndoNode));
        });
}

namespace su7 {
struct SetUpCase {
    Node *node, *tree;
    Ext *skins[4];
    unsigned skinCount;
    bool bones;
    int32_t filter;
};
SetUpCase gSetUp;
const int32_t kFilters[] = {-1, -5, 0, 3, 31};

// A SetUpSkinning world: a tree with lists and items, skins, bones (some with IK lists of their own).
void BuildSetUp(Gen &gen)
{
    W.falseWord = gen.OneIn(3) ? 0x100 : 0;
    const bool plain = gen.OneIn(5); // null bone tags allowed: no lists, item tags empty or null
    gSetUp = {};
    gSetUp.node = gen.OneIn(12) ? nullptr : &NewNode(kTree, "node");
    uint32_t words[4];
    const unsigned wordCount = gen.OneIn(12) ? 0 : PickWords(gen, words, 1 + gen.R(3));
    for (unsigned i = 0; i < wordCount; ++i) {
        Ext &s = NewExt(kSkin);
        s.word = words[i];
        s.name.Put("skin");
        gSetUp.skins[gSetUp.skinCount++] = &s;
    }
    MakeBones(gen, gen.R(6), plain, false);
    if (!plain)
        for (uint32_t i = 0; i < W.boneCount; ++i)
            if (gen.OneIn(2)) BoneLists(gen, *W.boneList[i]);
    gSetUp.bones = !gen.OneIn(12);
    if (!gen.OneIn(12)) {
        Node &tree = NewNode(kTree, "tree");
        tree.findMiss = gen.R(3);
        MakeTree(gen, tree, words, wordCount, !plain, plain);
        gSetUp.tree = &tree;
    }
    gSetUp.filter = gen.Pick(kFilters);
}
} // namespace su7

DIFF_TEST(SG_UtilitiesSkin_SetUpSkinning, 0xae8fd0)
{
    using Fn = bool(__cdecl *)(void *, void *, void *, void *, int32_t);
    const Fn original = ctx.Original<Fn>(0xae8fd0);
    Check(ctx, "SetUpSkinning", 600, [](uint32_t, Gen &gen) { BuildSetUp(gen); },
          [&](bool isOriginal) {
              SkinVector skins;
              for (unsigned i = 0; i < gSetUp.skinCount; ++i)
                  skins.push_back(reinterpret_cast<cISkinExtension *>(gSetUp.skins[i]));
              void *bones = gSetUp.bones ? &W.bones : nullptr;
              drawfx::Note("call", {Id(gSetUp.node), Id(gSetUp.tree), gSetUp.skinCount, Id(bones), uint32_t(gSetUp.filter)});
              const bool done =
                  isOriginal ? original(gSetUp.node, gSetUp.tree, &skins, bones, gSetUp.filter)
                             : SetUpSkinning(reinterpret_cast<cICompositionTreeNode *>(gSetUp.node),
                                             reinterpret_cast<cICompositionTreeNode *>(gSetUp.tree), &skins,
                                             static_cast<cIFindBonesTraversal *>(bones), gSetUp.filter);
              drawfx::Note("done", uint32_t(done) & 0xff);
              drawfx::Note("skins", uint32_t(skins.size()));
          });
}

namespace su7 {
struct LoadCase {
    Node *node;
    const char *name;
    Node *resource;
    uint32_t word;
    int32_t filter;
};
LoadCase gLoad;

// Items of a resource tree that carry shapes: the item itself a shape-ref node, or a shape-ref child.
void ShapeItems(Gen &gen, Node &tree)
{
    for (uint32_t i = 0; i < tree.itemCount; ++i) {
        Node &item = *tree.items[i];
        const unsigned r = gen.R(3);
        if (r == 0) {
            item.kinds |= kShapeRef;
            for (unsigned s = gen.R(4); s; --s) GiveShape(item, &NewShape(gen));
        } else if (r == 1) {
            Node &c = NewNode(gen.OneIn(5) ? kTree : kShapeRef | kTree, "c");
            c.qiMiss = gen.R(3);
            c.parent = &item;
            item.firstChild = &c;
            for (unsigned s = gen.R(4); s; --s) GiveShape(c, &NewShape(gen));
        }
    }
}
} // namespace su7

DIFF_TEST(SG_UtilitiesSkin_LoadObjectModel, 0xae8a20)
{
    using Fn = void *(__cdecl *)(void *, const char *, void *, uint32_t, int32_t);
    const Fn original = ctx.Original<Fn>(0xae8a20);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    shape::StandIn parse{0xae6e60, &StringKeyParser::ParseKey, &ParseStand};
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&ImageNew)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&ImageSizedDelete)};
    Check(
        ctx, "LoadObjectModel", 600,
        [](uint32_t, Gen &gen) {
            W.falseWord = gen.OneIn(3) ? 0x100 : 0;
            static const char *const kNames[] = {"lamp", "Body", "", "a_b"};
            Node &node = NewNode(kTree, "node");
            SkinPool(1);
            RefPool(6);
            MakeBones(gen, gen.R(6), false, true);
            for (uint32_t i = 0; i < W.boneCount; ++i)
                if (gen.OneIn(3)) BoneLists(gen, *W.boneList[i]);
            Node &res = NewNode(kTree, "res", true);
            res.resWord = 0x4000 + gen.R(9);
            res.findMiss = gen.R(3);
            static const uint32_t kNoWords[1] = {};
            const uint32_t word = gen.R(9);
            const uint32_t words[1] = {word};
            MakeTree(gen, res, gen.OneIn(2) ? words : kNoWords, 1, !gen.OneIn(3), false);
            ShapeItems(gen, res);
            // Items' tags lower-cased (the original's copy is compared): some match a lower-case bone.
            const bool given = gen.OneIn(2);
            W.loaded = &res;
            W.loadMode = gen.R(3);
            W.parseResult = !gen.OneIn(4);
            static const int32_t kModelFilters[] = {-1, 0, 4};
            gLoad = {&node, gen.Pick(kNames), given ? &res : nullptr, word, gen.Pick(kModelFilters)};
            if (given && gen.OneIn(6)) gLoad.name = nullptr;
        },
        [&](bool isOriginal) {
            NoteT("call", gLoad.name, {gLoad.node->id, Id(gLoad.resource), gLoad.word, uint32_t(gLoad.filter)});
            void *skin = isOriginal ? original(gLoad.node, gLoad.name, gLoad.resource, gLoad.word, gLoad.filter)
                                    : LoadObjectModel(reinterpret_cast<cICompositionTreeNode *>(gLoad.node), gLoad.name,
                                                      reinterpret_cast<cIResourceNode *>(gLoad.resource), gLoad.word,
                                                      gLoad.filter);
            drawfx::Note("skin", Id(skin));
        });
}

namespace su7 {
struct PersonCase {
    Node *node;
    Ext *sources[4];
    unsigned sourceCount;
    int32_t filter;
};
PersonCase gPerson;
} // namespace su7

DIFF_TEST(SG_UtilitiesSkin_LoadPersonPart, 0xae97a0)
{
    using Fn = bool(__cdecl *)(void *, void *, int32_t);
    const Fn original = ctx.Original<Fn>(0xae97a0);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&ImageNew)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&ImageSizedDelete)};
    Check(
        ctx, "LoadPersonPart", 600,
        [](uint32_t, Gen &gen) {
            W.falseWord = gen.OneIn(3) ? 0x100 : 0;
            gPerson = {};
            gPerson.node = gen.OneIn(10) ? nullptr : &NewNode(kTree, "node");
            uint32_t words[4];
            const unsigned wordCount = gen.OneIn(10) ? 0 : PickWords(gen, words, 1 + gen.R(3));
            SkinPool(wordCount);
            RefPool(6);
            MakeBones(gen, gen.R(6), false, true);
            for (uint32_t i = 0; i < W.boneCount; ++i)
                if (gen.OneIn(3)) BoneLists(gen, *W.boneList[i]);
            // The owners: the tree (a composition tree node) or a node that is not one.
            Node &tree = NewNode(kTree, "tree");
            tree.findMiss = gen.R(3);
            MakeTree(gen, tree, words, wordCount, !gen.OneIn(3), false);
            Node &notTree = NewNode(kTransform, "nt");
            notTree.qiMiss = gen.R(3);
            Node *firsts[4];
            unsigned firstCount = 0;
            for (unsigned k = 1 + gen.R(4); k; --k) {
                Node &f = NewNode(kShapeRef | kTree, gen.OneIn(3) ? nullptr : gen.Pick(kTagPool));
                if (!f.tag && !gen.OneIn(3)) {
                    Node &p = NewNode(kTree, gen.OneIn(4) ? nullptr : gen.Pick(kBoneUpper));
                    f.parent = &p;
                }
                firsts[firstCount++] = &f;
            }
            for (unsigned i = 0; i < wordCount; ++i) {
                Ext &s = NewExt(kSkin);
                s.word = words[i];
                s.name.Put(gen.OneIn(5) ? nullptr : gen.Pick(kSkinNames));
                s.owner = gen.OneIn(3) ? &notTree : &tree;
                for (unsigned k = gen.R(4); k; --k) {
                    Node *f = firsts[gen.R(firstCount)];
                    Shape *sh = gen.OneIn(6) ? nullptr : &NewShape(gen);
                    s.pairs[s.pairCount][0] = f;
                    s.pairs[s.pairCount][1] = sh;
                    ++s.pairCount;
                    Count(f, 1);
                    Count(sh, 1);
                }
                for (unsigned k = gen.R(3); k; --k) {
                    s.mapName[s.mapCount].Put(gen.Pick(kMaterials));
                    s.mapKey[s.mapCount].Put(gen.Pick(kSubsets));
                    ++s.mapCount;
                }
                gPerson.sources[gPerson.sourceCount++] = &s;
            }
            gPerson.filter = gen.Pick(kFilters);
        },
        [&](bool isOriginal) {
            SkinVector sources;
            for (unsigned i = 0; i < gPerson.sourceCount; ++i)
                sources.push_back(reinterpret_cast<cISkinExtension *>(gPerson.sources[i]));
            drawfx::Note("call", {Id(gPerson.node), gPerson.sourceCount, uint32_t(gPerson.filter)});
            const bool done = isOriginal ? original(gPerson.node, &sources, gPerson.filter)
                                         : LoadPersonPart(reinterpret_cast<cICompositionTreeNode *>(gPerson.node), &sources,
                                                          gPerson.filter);
            drawfx::Note("done", uint32_t(done) & 0xff);
        });
}

DIFF_TEST(SG_UtilitiesSkin_IidFilterConstructor, 0xaea7a0)
{
    using Fn = void *(__thiscall *)(void *, uint32_t);
    const Fn original = ctx.Original<Fn>(0xaea7a0);
    std::mt19937 rng(0xaea7a0);
    for (int i = 0; i < 300; ++i) {
        const uint32_t iid = i < 3 ? uint32_t(i) : uint32_t(rng());
        alignas(4) uint8_t a[16], b[16];
        std::memset(a, 0xcc, sizeof(a));
        std::memset(b, 0xcc, sizeof(b));
        void *ra = original(a, iid);
        void *rb = reinterpret_cast<nRZSceneGraph::IidFilterConstructor *>(b)->Construct(iid);
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("IidFilter %u: returned %p / %p", unsigned(i), ra, rb);
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("IidFilter %u: the objects differ", unsigned(i));
        uint32_t vt;
        std::memcpy(&vt, a, 4);
        if (vt != drawfx::P(GameAddress<void>(0xe7cf54))) ctx.Fail("IidFilter %u: vtable %08x", unsigned(i), vt);
    }
}

DIFF_TEST(SG_UtilitiesSkin_MaskFilterConstructor, 0xaea7c0)
{
    using Fn = void *(__thiscall *)(void *, uint32_t, uint32_t);
    const Fn original = ctx.Original<Fn>(0xaea7c0);
    std::mt19937 rng(0xaea7c0);
    for (int i = 0; i < 300; ++i) {
        const uint32_t mask = i < 4 ? uint32_t(i) - 1 : uint32_t(rng());
        const uint32_t iid = uint32_t(rng());
        alignas(4) uint8_t a[16], b[16];
        std::memset(a, 0xcc, sizeof(a));
        std::memset(b, 0xcc, sizeof(b));
        void *ra = original(a, mask, iid);
        void *rb = reinterpret_cast<nRZSceneGraph::MaskFilterConstructor *>(b)->Construct(mask, iid);
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("MaskFilter %u: returned %p / %p", unsigned(i), ra, rb);
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("MaskFilter %u: the objects differ", unsigned(i));
        uint32_t w[3];
        std::memcpy(w, a, 12);
        if (w[0] != drawfx::P(GameAddress<void>(nRZSceneGraph::cBranchNodesMaskBitAndIIDFilter::kVTable)) || w[1] != mask ||
            w[2] != iid)
            ctx.Fail("MaskFilter %u: words %08x %08x %08x", unsigned(i), w[0], w[1], w[2]);
    }
}
