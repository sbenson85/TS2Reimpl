#include "game_allocator.h"
#include "ts_neighbor_genetic_data_table.h"
#include "game/game.h"
#include "misc/UtilitiesC.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZMemory.h"
#include <algorithm>
#include <cstdio>
#include <new>

namespace ngdt {
void *tableVT[22], *persistFaceVT[12], *dataVT[8], *faceVT[12], *serviceVT[40], *segmentVT[12], *managerVT[8], *readerVT[12],
    *writerVT[24], *holderVT[4], *containerVT[16], *idSourceVT[34], *directorVT[13], *frameworkVT[4];

World &W()
{
    static World world;
    return world;
}
Table *T() { return reinterpret_cast<Table *>(W().work.bytes); }

uint32_t IdOf(const void *p)
{
    if (!p) return 0;
    const auto *base = W().work.bytes;
    const auto *at = static_cast<const uint8_t *>(p);
    if (at == base) return kTableId;
    if (at > base && at < base + 0x2c) return kTableFaceId + uint32_t(at - base);
    return static_cast<const Obj *>(p)->id;
}
uint32_t Norm(const void *p) { return IdOf(p); }
void Ev(std::initializer_list<uint32_t> items)
{
    auto &w = W();
    if (w.recording) w.events.insert(w.events.end(), items);
}

void Tick()
{
    auto &w = W();
    if (w.recording && w.ticks++ == w.cfg.throwAt) throw 0x1357;
}

namespace {
constexpr uint32_t kLoadedId = 4;
uint32_t Hash(const std::string &s)
{
    uint32_t h = 2166136261u;
    for (unsigned char c : s) h = (h ^ c) * 16777619u;
    return h ^ uint32_t(s.size());
}
uint32_t *KeyOf(Obj *face)
{
    auto &w = W();
    if (face->id == kNewFaceId) return w.newKey;
    return w.key[face->id - kFaceId];
}
unsigned DataIndex(const Obj *o) { return o->id - kDataId; }

// ---- reference counting ----
uint32_t __fastcall GenericAddRef(void *self, void *)
{
    Tick();
    Ev({evAddRef, IdOf(self)});
    if (self == T()) return ++T()->mRefCount;
    return uint32_t(++static_cast<Obj *>(self)->refs);
}
uint32_t __fastcall GenericRelease(Obj *self, void *)
{
    Ev({evRelease, self->id});
    return uint32_t(--self->refs);
}

// ---- data objects ----
uint8_t __fastcall DataQuery(Obj *self, void *, uint32_t iid, void **out)
{
    Tick();
    auto &w = W();
    Ev({evQuery, self->id, iid});
    const unsigned index = DataIndex(self);
    if (iid != 0x456b8f1d || (w.cfg.queryFail >> index & 1)) return 0;
    *out = &w.face[index];
    ++w.face[index].refs;
    return 0x80;
}
void __fastcall DataSetProperty(Obj *self, void *, std::string *name, uint32_t value)
{
    Tick();
    Ev({evSetProperty, self->id, Hash(*name), value});
}
// Stores text into the caller's std::string the way a game object would: text longer than the small buffer gets a block from
// the game allocator (the table frees it through the allocator service), and a block already held is returned first.
void Assign(std::string *name, const std::string &text)
{
    auto &w = W();
    struct Raw { union { char buffer[16]; char *pointer; }; uint32_t size, capacity; };
    auto *raw = reinterpret_cast<Raw *>(name);
    if (raw->capacity > 15) RZMemory::Free(raw->pointer);
    if (text.size() > 15) {
        w.lastBytes = uint32_t(text.size() + 1);
        w.lastBlock = RZMemory::Malloc(w.lastBytes);
        std::memcpy(w.lastBlock, text.c_str(), w.lastBytes);
        raw->pointer = static_cast<char *>(w.lastBlock);
        raw->capacity = uint32_t(text.size());
    } else {
        std::memset(raw->buffer, 0, 16);
        std::memcpy(raw->buffer, text.c_str(), text.size());
        raw->capacity = 15;
    }
    raw->size = uint32_t(text.size());
}
uint8_t __fastcall DataFirst(Obj *self, void *, std::string *name, uint32_t *value)
{
    Tick();
    auto &w = W();
    const unsigned index = DataIndex(self);
    const auto &list = w.props[index];
    const bool any = !list.empty() && !(w.cfg.emptyProps >> index & 1);
    Ev({evFirstProperty, self->id, any});
    if (!any) return 0;
    Assign(name, list[0].first);
    *value = list[0].second;
    return 1;
}
uint8_t __fastcall DataNext(Obj *self, void *, std::string *name, uint32_t *value)
{
    Tick();
    auto &w = W();
    const auto &list = w.props[DataIndex(self)];
    size_t at = 0;
    while (at < list.size() && list[at].first != *name) ++at;
    const bool more = at + 1 < list.size();
    Ev({evNextProperty, self->id, Hash(*name), more});
    if (!more) return 0;
    Assign(name, list[at + 1].first);
    *value = list[at + 1].second;
    return 1;
}

// ---- persist faces ----
uint32_t *__fastcall FaceGetKey(Obj *self, void *, uint32_t *out)
{
    Tick();
    Ev({evGetKey, self->id});
    std::memcpy(out, KeyOf(self), 16);
    return out;
}
uint8_t __fastcall FaceSetKey(Obj *self, void *, const uint32_t *key)
{
    Tick();
    Ev({evSetKey, self->id, key[0], key[1], key[2], key[3]});
    std::memcpy(KeyOf(self), key, 16);
    return 1;
}
// The table's own persist face: the key lives 8 bytes in (the table's +0x10).
uint32_t *__fastcall TableFaceGetKey(uint8_t *self, void *, uint32_t *out)
{
    Tick();
    Ev({evGetKey, IdOf(self)});
    std::memcpy(out, self + 8, 16);
    return out;
}
uint8_t __fastcall TableFaceSetKey(uint8_t *self, void *, const uint32_t *key)
{
    Tick();
    Ev({evSetKey, IdOf(self), key[0], key[1], key[2], key[3]});
    std::memcpy(self + 8, key, 16);
    return 1;
}

// ---- the persist system ----
uint8_t __fastcall ServiceGetResource(Obj *, void *, const uint32_t *key, uint32_t iid, void **out, uint32_t a, uint32_t b)
{
    Tick();
    auto &w = W();
    const unsigned n = w.getResources++;
    Ev({evGetResource, key[0], key[1], key[2], key[3], iid, a, b});
    if (w.cfg.getResourceFail >> n & 1) {
        if (w.cfg.failWritesOut) { Obj *stale = iid == 0xec2391bf ? &w.loaded : &w.data[key[2] % 24]; ++stale->refs; *out = stale; }
        return 0;
    }
    if (w.cfg.getResourceNull) { *out = nullptr; return 1; }
    Obj *found = iid == 0xec2391bf ? &w.loaded : &w.data[key[2] % 24];
    ++found->refs;
    *out = found;
    return 0x7f;
}
uint32_t __fastcall ServiceSave(Obj *, void *, void *resource, void *segment)
{
    Tick();
    auto &w = W();
    const unsigned n = (std::min)(w.saves++, 2u);
    Ev({evSaveResource, Norm(resource), Norm(segment)});
    return w.cfg.saveResult[n];
}
uint8_t __fastcall ServiceFindSegment(Obj *, void *, const uint32_t *key, uint32_t iid, void **out)
{
    Tick();
    auto &w = W();
    Ev({evFindSegment, key[0], key[1], key[2], key[3], iid});
    if (w.cfg.findSegmentFail) return 0;
    ++w.segment.refs;
    *out = &w.segment;
    return 0x33;
}
uint8_t __fastcall SegmentFlush(Obj *, void *)
{
    Tick();
    Ev({evFlush});
    return W().cfg.flushResult;
}

// ---- the resource manager ----
uint8_t __fastcall ManagerGet(Obj *, void *, const uint32_t *key, uint32_t iid, void **out, uint32_t a, uint32_t b)
{
    Tick();
    auto &w = W();
    const unsigned n = w.managerGets++;
    Ev({evGetResource, kManagerId, key[0], key[1], key[2], key[3], iid, a, b});
    if (w.cfg.managerGetFail >> n & 1) {
        if (w.cfg.failWritesOut) { Obj *stale = &w.data[key[2] % 24]; ++stale->refs; *out = stale; }
        return 0;
    }
    Obj *found = &w.data[key[2] % 24];
    ++found->refs;
    *out = found;
    return 1;
}
uint8_t __fastcall ManagerCreate(Obj *, void *, uint32_t type, uint32_t iid, void **out, uint32_t a, uint32_t b)
{
    Tick();
    auto &w = W();
    Ev({evCreateResource, type, iid, a, b});
    if (w.cfg.managerCreateNull) { *out = nullptr; return 1; }
    Obj *made = &w.data[24 + w.creations++ % 8];
    ++made->refs;
    *out = made;
    return 1;
}

// ---- streams ----
uint8_t __fastcall ReaderGet(Obj *, void *, uint32_t *out)
{
    Tick();
    auto &w = W();
    const unsigned n = w.transfers++;
    const uint32_t value = n == 0 ? w.cfg.version : n == 1 ? w.cfg.count : n - 2 < 16 ? w.cfg.ids[n - 2] : 0;
    const bool ok = !(w.cfg.streamFail >> n & 1);
    Ev({evGetSint32, n, value, ok});
    // The original leaves the output unwritten when the read fails (stack bytes); the table keeps its locals instead.
    // Writing it here regardless keeps both sides equal and is the value the transfer would have given.
    *out = value;
    return ok;
}
uint8_t __fastcall WriterPut(Obj *, void *, uint32_t value)
{
    Tick();
    auto &w = W();
    const unsigned n = w.transfers++;
    Ev({evSetSint32, n, value});
    w.written.push_back(value);
    return !(w.cfg.streamFail >> n & 1);
}
void __fastcall WriterMetadata(Obj *, void *, UtilObjectB *meta)
{
    Tick();
    const auto *words = reinterpret_cast<const uint32_t *>(meta);
    Ev({evMetadata, words[0], words[1], words[2], words[3], words[4]});
    ::operator delete(meta, sizeof(UtilObjectB));
}

// ---- holder / container / id source ----
uint8_t __fastcall HolderQuery(Obj *self, void *, uint32_t iid, void **out)
{
    Tick();
    auto &w = W();
    Ev({evQuery, self->id, iid});
    if (w.cfg.holderFail) return 0;
    ++w.stream.refs;
    *out = &w.stream;
    return 0x11;
}
uint8_t __fastcall ContainerQuery(Obj *self, void *, uint32_t iid, void **out)
{
    Tick();
    auto &w = W();
    Ev({evQuery, self->id, iid});
    if (w.cfg.idSourceFail) return 0;
    ++w.idSource.refs;
    *out = &w.idSource;
    return 0x22;
}
uint8_t __fastcall ContainerGetInterface(Obj *, void *, uint32_t iid, void **out)
{
    Tick();
    auto &w = W();
    Ev({evGetInterface, iid});
    if (w.cfg.segmentFail) return 0;
    ++w.segment.refs;
    *out = &w.segment;
    return 0x44;
}
uint32_t __fastcall ContainerGroup(Obj *, void *)
{
    Tick();
    Ev({evGetGroup});
    return W().cfg.group;
}
void __fastcall IdSourceGetIds(Obj *, void *, uint32_t *vector)
{
    Tick();
    auto &w = W();
    Ev({evGetIds, w.cfg.idCount});
    const uint32_t n = w.cfg.idCount;
    if (n == 0 && !w.cfg.idListEmptyBlock) return;
    w.idBytes = (n + 3) * 4;
    auto *block = static_cast<uint32_t *>(RZMemory::Malloc(w.idBytes));
    w.idBlock = block;
    std::memcpy(block, w.cfg.idList, n * 4);
    // The vector is (begin, end, capacity) and the table frees it through the allocator service.
    vector[0] = uint32_t(reinterpret_cast<uintptr_t>(block));
    vector[1] = uint32_t(reinterpret_cast<uintptr_t>(block + n));
    vector[2] = uint32_t(reinterpret_cast<uintptr_t>(block + n + 3));
}
uint8_t __fastcall IdSourceMap(Obj *, void *, uint32_t id, uint32_t *out)
{
    Tick();
    auto &w = W();
    const unsigned n = w.mapCalls++;
    const bool ok = !(w.cfg.mapFail >> n & 1);
    Ev({evMapId, id, ok});
    if (!ok) return 0;
    *out = id + w.cfg.mapOffset;
    return 0x55;
}

// ---- tables (the table under test, the new table and the loaded table share one vtable) ----
uint8_t __fastcall TableWrite(void *self, void *, Obj *stream)
{
    Tick();
    Ev({evTableWrite, IdOf(self), stream->id});
    return W().cfg.tableWriteResult;
}
uint8_t __fastcall TableRead(void *self, void *, Obj *stream)
{
    Tick();
    Ev({evTableRead, IdOf(self), stream->id});
    return W().cfg.tableReadResult;
}
uint8_t __fastcall TableSet(void *self, void *, uint32_t id, Obj *data)
{
    Tick();
    Ev({evTableSet, IdOf(self), id, IdOf(data)});
    return 1;
}
void *__fastcall TableGet(void *self, void *, uint32_t id)
{
    Tick();
    auto &w = W();
    Ev({evTableGet, IdOf(self), id});
    if (self == T() && (w.cfg.dataMissing >> (id % 32) & 1)) return nullptr;
    return &w.data[id % 24];
}
void *__fastcall TableCreate(void *self, void *)
{
    Tick();
    auto &w = W();
    Ev({evTableCreate, IdOf(self)});
    return &w.data[24 + w.copies++ % 8];
}
void *__fastcall TablePersist(void *self, void *)
{
    Tick();
    Ev({evTablePersist, IdOf(self)});
    return &W().newFace;
}

// ---- framework / allocator ----
void *__fastcall DirectorFramework(void *, void *) { return &W().framework; }
uint32_t __fastcall FrameworkCreate(Obj *, void *, uint32_t cls, uint32_t iid, void **out)
{
    Tick();
    auto &w = W();
    Ev({evCreateInstance, cls, iid});
    ++w.newTable.refs;
    *out = &w.newTable;
    return 1;
}
void InitVTables()
{
    static bool done = false;
    if (done) return;
    done = true;
    auto fn = [](auto f) { return reinterpret_cast<void *>(f); };
    tableVT[1] = fn(&GenericAddRef); tableVT[2] = fn(&GenericRelease); tableVT[3] = fn(&TableWrite); tableVT[4] = fn(&TableRead);
    tableVT[8] = fn(&TableSet); tableVT[12] = fn(&TableGet); tableVT[16] = fn(&TableCreate); tableVT[21] = fn(&TablePersist);
    persistFaceVT[3] = fn(&TableFaceGetKey); persistFaceVT[4] = fn(&TableFaceSetKey);
    dataVT[0] = fn(&DataQuery); dataVT[1] = fn(&GenericAddRef); dataVT[2] = fn(&GenericRelease);
    dataVT[3] = fn(&DataSetProperty); dataVT[6] = fn(&DataFirst); dataVT[7] = fn(&DataNext);
    faceVT[2] = fn(&GenericRelease); faceVT[3] = fn(&FaceGetKey); faceVT[4] = fn(&FaceSetKey);
    serviceVT[5] = fn(&ServiceGetResource); serviceVT[16] = fn(&ServiceSave); serviceVT[34] = fn(&ServiceFindSegment);
    segmentVT[2] = fn(&GenericRelease); segmentVT[10] = fn(&SegmentFlush);
    managerVT[3] = fn(&ManagerGet); managerVT[6] = fn(&ManagerCreate);
    readerVT[2] = fn(&GenericRelease); readerVT[9] = fn(&ReaderGet);
    writerVT[2] = fn(&GenericRelease); writerVT[9] = fn(&WriterPut); writerVT[19] = fn(&WriterMetadata);
    holderVT[0] = fn(&HolderQuery);
    containerVT[0] = fn(&ContainerQuery); containerVT[12] = fn(&ContainerGetInterface); containerVT[14] = fn(&ContainerGroup);
    idSourceVT[2] = fn(&GenericRelease); idSourceVT[30] = fn(&IdSourceGetIds); idSourceVT[32] = fn(&IdSourceMap);
    directorVT[12] = fn(&DirectorFramework); frameworkVT[3] = fn(&FrameworkCreate);
}

bool tableAlive = false;
} // namespace

void SetStreamMode(bool reading)
{
    W().stream.vt = reading ? readerVT : writerVT;
}

void Reset(const Seeded &s, const Config &cfg)
{
    InitVTables();
    if (tableAlive) Cleanup();
    auto &w = W();
    w.recording = false;
    w.cfg = cfg;
    w.events.clear(); w.written.clear();
    w.ticks = 0; w.sawThrow = false;
    w.getResources = w.managerGets = w.transfers = w.saves = w.mapCalls = w.copies = w.creations = 0;
    w.lastBlock = w.idBlock = nullptr;
    w.lastBytes = w.idBytes = 0;
    std::mt19937 rng(s.seed);
    auto *raw = reinterpret_cast<uint8_t *>(&w.work);
    for (size_t i = 0; i < sizeof(w.work); ++i) raw[i] = uint8_t(rng());
    for (unsigned i = 0; i < kDataCount; ++i) {
        w.data[i] = {dataVT, kDataId + i, 1};
        w.face[i] = {faceVT, kFaceId + i, 1};
        for (auto &word : w.key[i]) word = rng();
        w.props[i].clear();
        const unsigned n = (i + s.seed) % 4;
        for (unsigned p = 0; p < n; ++p) {
            // Short names stay in the small buffer; long ones take a heap block the table must free.
            std::string name = (p + i) % 2 ? std::string("prop") + char('a' + p) : std::string(18 + p, char('A' + p));
            w.props[i].emplace_back(name, rng());
        }
    }
    for (auto &word : w.newKey) word = rng();
    w.newTable = {tableVT, kNewTableId, 1}; w.newFace = {faceVT, kNewFaceId, 1}; w.loaded = {tableVT, kLoadedId, 1};
    w.service = {serviceVT, kServiceId, 1}; w.segment = {segmentVT, kSegmentId, 1}; w.manager = {managerVT, kManagerId, 1};
    w.stream = {readerVT, kStreamId, 1}; w.holder = {holderVT, kHolderId, 1}; w.container = {containerVT, kContainerId, 1};
    w.idSource = {idSourceVT, kIdSourceId, 1}; w.framework = {frameworkVT, kFrameworkId, 1};

    Table *t = T();
    t->mVTable = tableVT;
    t->mContainer.mVTable = containerVT;
    t->mPersistVTable = persistFaceVT;
    t->mRefCount = s.refCount;
    t->mModified = s.modified;
    new (&t->mData) NeighborDataMap;
    tableAlive = true;
    std::vector<uint32_t> ids;
    for (uint32_t i = 0; i < 16; ++i) ids.push_back(i);
    for (uint32_t extra : {0x7fffffffu, 0x80000000u, 0xfffffffeu, 0xffffffffu}) ids.push_back(extra);
    std::shuffle(ids.begin(), ids.end(), rng);
    for (unsigned i = 0; i < s.entries && i < ids.size(); ++i) {
        Obj *object = &w.data[ids[i] % 24];
        t->mData.emplace(ids[i], NeighborDataRef(object));
    }
    w.recording = true;
}

void Cleanup()
{
    auto &w = W();
    const bool was = w.recording;
    w.recording = false;
    if (tableAlive) T()->mData.~NeighborDataMap();
    tableAlive = false;
    w.recording = was;
}
void MarkTableFreed() { tableAlive = false; }

namespace {
struct RawNode {
    RawNode *left, *parent, *right;
    char color, isNil;
    uint16_t pad;
    uint32_t key;
    void *value;
};
void Collect(RawNode *node, std::vector<RawNode *> &out)
{
    if (node->isNil) return;
    out.push_back(node);
    Collect(node->left, out);
    Collect(node->right, out);
}
}

void Capture(Blob &b, bool)
{
    const bool alive = tableAlive;
    auto &w = W();
    b.Mark("guards");
    for (unsigned i = 0; i < 16; ++i) { b.Add(w.work.before[i]); b.Add(w.work.after[i]); }
    if (alive) {
        b.Mark("table");
        uint8_t copy[0x2c];
        std::memcpy(copy, w.work.bytes, sizeof(copy));
        auto *words = reinterpret_cast<uint32_t *>(copy);
        const bool hasHead = words[0x24 / 4] != 0;
        words[0x24 / 4] = hasHead;
        for (unsigned i = 0; i < 0x2c / 4; ++i) b.Add(words[i]);
        b.Mark("map");
        auto *head = *reinterpret_cast<RawNode **>(&T()->mData);
        if (head) {
            std::vector<RawNode *> nodes;
            Collect(head->parent, nodes);
            auto index = [&](RawNode *n) -> uint32_t {
                if (n == head) return 0;
                for (size_t i = 0; i < nodes.size(); ++i) if (nodes[i] == n) return uint32_t(i + 1);
                return 0xffff;
            };
            b.Add(uint32_t(nodes.size()));
            b.Add(index(head->left)); b.Add(index(head->parent)); b.Add(index(head->right));
            b.Add(uint8_t(head->color)); b.Add(uint8_t(head->isNil));
            for (auto *n : nodes) {
                b.Add(n->key); b.Add(n->value ? IdOf(n->value) : 0);
                b.Add(uint8_t(n->color)); b.Add(uint8_t(n->isNil));
                b.Add(index(n->left)); b.Add(index(n->parent)); b.Add(index(n->right));
            }
        }
    }
    b.Mark("refs");
    for (auto &o : w.data) b.Add(uint32_t(o.refs));
    for (auto &o : w.face) b.Add(uint32_t(o.refs));
    for (Obj *o : {&w.newTable, &w.newFace, &w.loaded, &w.service, &w.segment, &w.manager, &w.stream, &w.holder, &w.container,
                   &w.idSource, &w.framework})
        b.Add(uint32_t(o->refs));
    for (auto word : w.newKey) b.Add(word);
    for (auto &k : w.key) for (auto word : k) b.Add(word);
    b.Mark("blocks");
    // Whether the last name buffer / id block went back to the allocator: it is the next block handed out for its size.
    auto isFree = [](void *block, uint32_t bytes) {
        if (!block) return false;
        void *probe = RZMemory::Malloc(bytes);
        const bool free = probe == block;
        RZMemory::Free(probe);
        return free;
    };
    b.Add(isFree(w.lastBlock, w.lastBytes));
    b.Add(isFree(w.idBlock, w.idBytes));
    b.Mark("written");
    for (auto v : w.written) b.Add(v);
    b.Mark("events");
    b.Add(uint32_t(w.events.size()));
    for (auto v : w.events) b.Add(v);
}

bool Same(difftest::Context &ctx, const Blob &a, const Blob &b, const char *what, unsigned seed)
{
    ++ctx.cases;
    size_t at = 0;
    while (at < a.w.size() && at < b.w.size() && a.w[at] == b.w[at]) ++at;
    if (at == a.w.size() && at == b.w.size()) return true;
    const char *section = "?";
    for (const auto &mark : a.marks) if (mark.first <= at) section = mark.second;
    ctx.Fail("%s seed %u differs in %s at word %zu (original %x, replacement %x; sizes %zu/%zu)", what, seed, section, at,
        at < a.w.size() ? a.w[at] : 0xdeadbeef, at < b.w.size() ? b.w[at] : 0xdeadbeef, a.w.size(), b.w.size());
    return false;
}

Globals::Globals(difftest::Context &c) : ctx(c)
{
    uint32_t cells[8] = {0x10faf6c, 0x10cc2d8, 0x10c95d4, 0x10c9378, 0x10c9390, 0, 0, 0};
    for (int i = 0; i < 5; ++i) saved[i] = *ctx.image.At<void *>(cells[i]);
    savedGuard[0] = *ctx.image.At<int32_t>(0x10c95d8);
    savedGuard[1] = *ctx.image.At<int32_t>(0x10c93f4);
    InitVTables();
    // The allocator and the director are cached statics: pretend they are initialised.
    // The allocator service is an object with the game's own service vtable over the game's allocator, so the original's
    // frees and ours meet the blocks the fakes took from it.
    auto *ga = GameAddress<GeneralAllocator>(0x10cb098);
    const auto *bytes = reinterpret_cast<const uint8_t *>(ga);
    bool zero = true;
    for (size_t i = 0; i < sizeof(GeneralAllocator); ++i) zero = zero && bytes[i] == 0;
    if (zero) { ga->Construct(nullptr, 0, 0, 0, nullptr, nullptr); builtAllocator = true; }
    std::memset(allocatorService, 0, sizeof(allocatorService));
    const void *serviceVtable = ctx.image.At<void>(0xda3cd4);
    std::memcpy(allocatorService, &serviceVtable, sizeof(serviceVtable));
    *ctx.image.At<void *>(0x10c95d4) = allocatorService;
    *ctx.image.At<int32_t>(0x10c95d8) = INT32_MIN;
    *ctx.image.At<void *>(0x10c9390) = directorVT;
    *ctx.image.At<int32_t>(0x10c93f4) = INT32_MIN;
    *ctx.image.At<void *>(0x10c9378) = nullptr;
    SetService(false);
    SetManager(false);
}
Globals::~Globals()
{
    uint32_t cells[5] = {0x10faf6c, 0x10cc2d8, 0x10c95d4, 0x10c9378, 0x10c9390};
    for (int i = 0; i < 5; ++i) *ctx.image.At<void *>(cells[i]) = saved[i];
    *ctx.image.At<int32_t>(0x10c95d8) = savedGuard[0];
    *ctx.image.At<int32_t>(0x10c93f4) = savedGuard[1];
    if (builtAllocator) {
        auto *ga = GameAddress<GeneralAllocator>(0x10cb098);
        difftest_alloc::ShutdownAndRelease(ga);
    }
}
void Globals::SetService(bool null) { *ctx.image.At<void *>(0x10faf6c) = null ? nullptr : &W().service; }
void Globals::SetManager(bool null) { *ctx.image.At<void *>(0x10cc2d8) = null ? nullptr : &W().manager; }
} // namespace ngdt
