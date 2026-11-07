// Differential tests for the application's property manager, cGZPropertyManagerT<string traits, cGZPropertySetString>, in
// the standalone shape (lane PU, src/gz/cGZPropertyManager.size.cpp). The original side is a manager from the original
// constructor 0xc26045 (the game's vtables: every slot call lands on the original's RVA, checked against the table below), ours
// one from our constructor (the hook's adapter: our vtables, every slot ours). The internal helpers (0xc28d0f, 0x1bf595,
// 0x1bf539, the destructor 0xc26651) are called by RVA on one side and as our members on the other.
//
// Method: two worlds (A original, B ours), each with its own fake message targets (counted; DoMessage logs the message's type,
// key and value, and keeps the message to check that the manager let go of it), fake source sets and values. Random sequences
// over a small key pool (case variants, the empty key, a key past the small-string buffer) run on both: SetProperty (values
// of two types, so Assign refusals happen), Add/Remove/RemoveAll notifications, Clear, RemoveProperty, CopyFrom, the getters,
// Shutdown, QueryInterface / AddRef / Release on every face, the inherited service slots, and the helpers. After every step:
// the results, the logs, the targets' counts and the whole manager (refcount, entries, key text, the watch map's contents and
// tree shape, the service fields) are compared. The cases end through each destruction route (Release to zero on every face,
// both deleting destructors, the destructor), compared through the logs (every watcher released, in order), the frees and the
// words left.
#include "difftest.h"
#include "free_scope.h"
#include "service_shape.h"
#include "ts_services_fixture.h" // AllocatorScope

#include "game/game.h"
#include "gz/cGZPropertyManager.h"
#include "gz/cGZPropertySet.h"

#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <string>
#include <vector>

#include <windows.h>

namespace {

using Manager = cGZPropertyManagerString;
using Blob = std::vector<uint32_t>;

constexpr uint32_t kCtor = 0xc26045, kDtor = 0xc26651, kNotify = 0xc28d0f, kSetDefault = 0x1bf595, kAddIfAbsent = 0x1bf539;
constexpr uint32_t kFaces[4] = {0, 0x24, 0x28, 0x2c};
constexpr uint32_t kGameVt[4] = {Manager::kVTable, Manager::kManagerVTable, Manager::kSystemVTable, Manager::kUnknownVTable};
constexpr uint32_t kTargets = 4;

// The original's slots the sequences reach through the game's vtables (face, slot, RVA): checked on every constructed
// original, so each call through the game's vtable is a call of this RVA.
struct GameSlot {
    uint32_t face, slot, rva;
};
const GameSlot kGameSlots[] = {
    {0, 0, 0xc29798},    {0, 1, 0x146a50},    {0, 2, 0x24284d},    {0, 3, 0xc299e2},    {0, 4, 0x1bee9c},
    {0, 5, 0x1bee23},    {0, 6, 0x1bf26d},    {0, 7, 0x1bf3c0},    {0, 8, 0x1bf670},    {0, 9, 0x1bf69a},
    {0, 10, 0x1bf306},   {0, 11, 0x1bf41f},   {0, 12, 0x1bf0d1},   {0, 13, 0xc26790},
    {0x24, 0, 0xc297e2}, {0x24, 1, 0x47e370}, {0x24, 2, 0xc2987a}, {0x24, 3, 0x47e380}, {0x24, 4, 0xc272c0},
    {0x24, 5, 0xc2994a}, {0x24, 6, 0xc298bf},
    {0x28, 0, 0xc297ea}, {0x28, 1, 0xc274ae}, {0x28, 2, 0xc29882}, {0x28, 9, 0xc2a1cc},
    {0x2c, 0, 0xc297f2}, {0x2c, 1, 0xb28835}, {0x2c, 2, 0xc2988a}, {0x2c, 3, 0xc26778},
};

void *At(void *m, uint32_t face) { return static_cast<char *>(m) + face; }
uint32_t Call(void *m, uint32_t face, uint32_t slot, unsigned words, const uint32_t *args)
{
    return svc::CallWords(svc::VTableAt(m, face)[slot], At(m, face), words, args);
}
uint32_t W(const void *p) { return svc::P(p); }

// ---- worlds and fakes --------------------------------------------------------------------------------------------

struct World;

struct FakeTarget {
    void **vt;
    uint32_t refs;
    uint32_t index;
    uint8_t ret;
    World *world;
};

struct World {
    FakeTarget targets[kTargets];
    Blob log;
    std::vector<void *> held; // the messages the targets were sent (one reference each)
    Manager *m = nullptr;
};

// The raw tree nodes (the library's std::map / std::set layout, the same on both sides).
struct InnerNode {
    InnerNode *left, *parent, *right;
    uint8_t color, isnil, pad[2];
    FakeTarget *target;
    uint32_t type;
};
struct OuterNode {
    OuterNode *left, *parent, *right;
    uint8_t color, isnil, pad[2];
    GZServiceString key; // +0x10
    InnerNode *setHead;  // +0x28
    uint32_t setSize;    // +0x2c
};
static_assert(offsetof(OuterNode, setHead) == 0x28 && sizeof(OuterNode) == 0x30);
static_assert(offsetof(InnerNode, target) == 0x10 && sizeof(InnerNode) == 0x18);

OuterNode *Head(Manager *m) { return *reinterpret_cast<OuterNode **>(&m->mWatchers); }

void InOrder(OuterNode *n, OuterNode *head, std::vector<OuterNode *> &out)
{
    if (n == head) return;
    InOrder(n->left, head, out);
    out.push_back(n);
    InOrder(n->right, head, out);
}

void PushText(Blob &b, const char *text, size_t size)
{
    b.push_back(static_cast<uint32_t>(size));
    for (size_t i = 0; i < size; i += 4) {
        uint32_t w = 0;
        std::memcpy(&w, text + i, size - i < 4 ? size - i : 4);
        b.push_back(w);
    }
}

// A key pointer as the ordinal of the map node holding it (0x1000 + n), else a marker.
uint32_t NormKey(World &w, const void *p)
{
    std::vector<OuterNode *> nodes;
    InOrder(Head(w.m)->parent, Head(w.m), nodes);
    for (size_t i = 0; i < nodes.size(); ++i)
        if (&nodes[i]->key == p) return 0x1000 + static_cast<uint32_t>(i);
    return p ? 0x1fff : 0x1ffe;
}

// A value pointer as the index of the entry holding it (0x2000 + i), else a marker.
uint32_t NormValue(World &w, const void *p)
{
    for (size_t i = 0; i < w.m->mEntries.size(); ++i)
        if (&w.m->mEntries[i].mValue == p) return 0x2000 + static_cast<uint32_t>(i);
    return p ? 0x2fff : 0x2ffe;
}

uint32_t MsgSlot(void *msg, int slot) { return svc::CallWords(svc::VTableAt(msg, 0)[slot], msg, 0, nullptr); }

uint32_t __fastcall FtQI(FakeTarget *, void *, uint32_t, void **) { return 0; }
uint32_t __fastcall FtAddRef(FakeTarget *t, void *)
{
    t->world->log.insert(t->world->log.end(), {0xa0u, t->index});
    return ++t->refs;
}
uint32_t __fastcall FtRelease(FakeTarget *t, void *)
{
    t->world->log.insert(t->world->log.end(), {0xb0u, t->index});
    return --t->refs;
}
uint32_t __fastcall FtDoMessage(FakeTarget *t, void *, void *msg)
{
    World &w = *t->world;
    const auto *key = reinterpret_cast<const GZServiceString *>(MsgSlot(msg, 10)); // GetVoid1
    w.log.insert(w.log.end(), {0xd0u, t->index, MsgSlot(msg, 4) /* GetType */, NormKey(w, key),
                               NormValue(w, reinterpret_cast<void *>(MsgSlot(msg, 15))) /* GetVoid2 */,
                               reinterpret_cast<uint32_t *>(msg)[1] /* its count */});
    if (key) PushText(w.log, key->c_str(), key->size());
    MsgSlot(msg, 1); // held until the step ends: the manager's own reference must be gone by then
    w.held.push_back(msg);
    return t->ret;
}
void *gTargetVt[4] = {reinterpret_cast<void *>(&FtQI), reinterpret_cast<void *>(&FtAddRef), reinterpret_cast<void *>(&FtRelease),
                      reinterpret_cast<void *>(&FtDoMessage)};

void InitWorld(World &w, std::mt19937 &rng)
{
    for (uint32_t i = 0; i < kTargets; ++i) w.targets[i] = {gTargetVt, 1, i, static_cast<uint8_t>(rng() % 3), &w};
}

// The messages the targets kept: each one's count (1 when only the target holds it), then released.
void DropHeld(World &w)
{
    for (void *msg : w.held) w.log.insert(w.log.end(), {0xe0u, reinterpret_cast<uint32_t *>(msg)[1]});
    for (void *msg : w.held) MsgSlot(msg, 2);
    w.held.clear();
}

// ---- snapshots -------------------------------------------------------------------------------------------------------

uint32_t TargetCode(const FakeTarget *t, const World &w)
{
    for (uint32_t i = 0; i < kTargets; ++i)
        if (t == &w.targets[i]) return 0x300 + i;
    return 0x3ff;
}

void SnapInner(Blob &b, InnerNode *n, InnerNode *head, const World &w)
{
    if (n == head) return b.push_back(0xfeed);
    b.insert(b.end(), {n->color, n->isnil, TargetCode(n->target, w), n->type});
    SnapInner(b, n->left, head, w);
    SnapInner(b, n->right, head, w);
}

void SnapOuter(Blob &b, OuterNode *n, OuterNode *head, const World &w)
{
    if (n == head) return b.push_back(0xbeef);
    b.insert(b.end(), {n->color, n->isnil, static_cast<uint32_t>(n->key.capacity()), n->setSize, n->setHead->color, n->setHead->isnil});
    PushText(b, n->key.c_str(), n->key.size());
    SnapInner(b, n->setHead->parent, n->setHead, w);
    SnapOuter(b, n->left, head, w);
    SnapOuter(b, n->right, head, w);
}

// Everything but the pointers into each side's heap; the cursor as an entry index when it is meaningful.
Blob Snap(World &w, bool cursorValid)
{
    Manager *m = w.m;
    Blob b;
    b.push_back(m->cGZPropertySetString::mRefCount);
    b.push_back(static_cast<uint32_t>(m->mEntries.size()));
    b.push_back(static_cast<uint32_t>(m->mEntries.capacity()));
    for (const cGZPropertyEntry &e : m->mEntries) b.insert(b.end(), {e.mKey, W(e.mValue.mVTable), e.mValue.mValue});
    if (cursorValid) b.push_back(m->mCursor ? static_cast<uint32_t>(m->mCursor - m->mEntries.data()) : 0xffffffff);
    b.push_back(static_cast<uint32_t>(m->mKeyText.capacity()));
    PushText(b, m->mKeyText.data(), m->mKeyText.size());
    const uint32_t *words = reinterpret_cast<const uint32_t *>(m);
    b.insert(b.end(), {words[0x30 / 4], words[0x34 / 4], words[0x38 / 4], words[0x3c / 4], words[0x40 / 4] & 0xff});
    b.push_back(static_cast<uint32_t>(m->mWatchers.size()));
    OuterNode *head = Head(m);
    b.insert(b.end(), {head->color, head->isnil});
    SnapOuter(b, head->parent, head, w);
    for (const FakeTarget &t : w.targets) b.push_back(t.refs);
    return b;
}

// ---- values and keys ----------------------------------------------------------------------------------------------------

struct Value {
    InlineAny any;
};
Value MakeValue(uint32_t code)
{
    // Two types (Assign refuses the other), small payloads.
    return {{GameAddress<const void>(code & 1 ? gzany::kSint32VTable : gzany::kUint32VTable), code >> 1}};
}

const char *const kKeyTexts[] = {"Volume", "VOLUME", "Music", "", "AVeryLongPropertyNameThatLeavesTheSmallBuffer", "Mus", "volume2"};
constexpr uint32_t kKeyCount = sizeof(kKeyTexts) / sizeof(kKeyTexts[0]);

// ---- the steps -----------------------------------------------------------------------------------------------------------

enum Op : uint32_t {
    kSet, kAdd, kRemove, kRemoveAll, kClear, kRemoveProperty, kCopyFrom, kGetFirst, kGetNext, kGet, kGetValue, kHas, kCount,
    kGetSet, kShutdown, kQI, kRef, kService, kNotifyHelper, kSetDefaultHelper, kAddIfAbsentHelper, kOps
};

struct Step {
    Op op;
    uint32_t key, value, target, type, face, flag, iid, extra;
};

struct Side {
    World w;
    std::vector<GZServiceString> *keys;
    GZServiceString outKey;
    cGZPropertySetString *source = nullptr;
    bool original;
    difftest::Context *ctx;
};

const uint32_t kIids[] = {1, cGZPropertySetString::kIid, cIGZPropertyManagerString::kIid, cIGZSystemService::kIID, 0x12345678, 0};

struct ServiceSlot {
    uint32_t face, slot, words;
    svc::Ret ret;
};
const ServiceSlot kServiceSlots[] = {
    {0x28, 3, 0, svc::kWord}, {0x28, 4, 1, svc::kVoid}, {0x28, 5, 0, svc::kWord}, {0x28, 6, 0, svc::kByte},
    {0x28, 7, 1, svc::kVoid}, {0x28, 8, 0, svc::kByte}, {0x28, 10, 1, svc::kByte}, {0x28, 11, 1, svc::kByte},
    {0x28, 12, 0, svc::kWord}, {0x2c, 4, 0, svc::kWord}, {0x2c, 5, 0, svc::kWord},
};

// One step on one side; returns the result words (normalised).
Blob Apply(Side &s, const Step &st, bool &cursorValid)
{
    Manager *m = s.w.m;
    const GZServiceString *key = &(*s.keys)[st.key];
    Value value = MakeValue(st.value);
    void *target = &s.w.targets[st.target];
    Blob r;
    switch (st.op) {
    case kSet: {
        const uint32_t a[2] = {W(key), W(&value)};
        r.push_back(Call(m, 0, 3, 2, a) & 0xff);
        cursorValid = false;
        break;
    }
    case kAdd: {
        const uint32_t a[4] = {W(target), st.type, W(key), st.flag ? W(&value) : 0};
        r.push_back(Call(m, 0x24, 4, 4, a) & 0xff);
        cursorValid = false;
        break;
    }
    case kRemove: {
        const uint32_t a[3] = {W(target), st.type, W(key)};
        r.push_back(Call(m, 0x24, 5, 3, a) & 0xff);
        break;
    }
    case kRemoveAll: {
        const uint32_t a[1] = {st.flag ? W(target) : 0x5eed};
        r.push_back(Call(m, 0x24, 6, 1, a) & 0xff);
        break;
    }
    case kClear: Call(m, 0, 5, 0, nullptr); cursorValid = false; break;
    case kRemoveProperty: {
        const uint32_t a[1] = {W(key)};
        r.push_back(Call(m, 0, 4, 1, a) & 0xff);
        cursorValid = false;
        break;
    }
    case kCopyFrom: {
        const uint32_t a[1] = {W(s.source)};
        Call(m, 0, 12, 1, a);
        cursorValid = false;
        break;
    }
    case kGetFirst:
    case kGetNext: {
        if (st.op == kGetNext && !cursorValid) break; // past a change the cursor may point into freed entries
        void *out = reinterpret_cast<void *>(0x77);
        const uint32_t a[2] = {W(&s.outKey), W(&out)};
        const uint32_t got = Call(m, 0, st.op == kGetFirst ? 6 : 7, 2, a) & 0xff;
        r.insert(r.end(), {got, NormValue(s.w, out)});
        if (got) PushText(r, s.outKey.c_str(), s.outKey.size());
        cursorValid = cursorValid || st.op == kGetFirst;
        // Past the end GetNext would read beyond the entries (an original bug the set's own tests describe): stop there.
        if (m->mCursor && m->mCursor >= m->mEntries.data() + m->mEntries.size()) cursorValid = false;
        if (!got) cursorValid = false;
        break;
    }
    case kGet: {
        const uint32_t a[1] = {W(key)};
        r.push_back(NormValue(s.w, reinterpret_cast<void *>(Call(m, 0, 8, 1, a))));
        break;
    }
    case kGetValue: {
        Value out = MakeValue(st.value ^ 0x40);
        const uint32_t a[2] = {W(key), W(&out)};
        r.insert(r.end(), {Call(m, 0, 9, 2, a) & 0xff, W(out.any.mVTable), out.any.mValue});
        break;
    }
    case kHas: {
        const uint32_t a[1] = {W(key)};
        r.push_back(Call(m, 0, 10, 1, a) & 0xff);
        break;
    }
    case kCount: r.push_back(Call(m, 0, 11, 0, nullptr)); break;
    case kGetSet: r.push_back(Call(m, 0x24, 3, 0, nullptr) - W(m)); break;
    case kShutdown: r.push_back(Call(m, 0x28, 9, 0, nullptr) & 0xff); cursorValid = false; break;
    case kQI: {
        void *out = reinterpret_cast<void *>(0x5a5a5a5a);
        const uint32_t a[2] = {kIids[st.iid], W(&out)};
        const uint32_t got = Call(m, kFaces[st.face], 0, 2, a) & 0xff;
        r.insert(r.end(), {got, out == reinterpret_cast<void *>(0x5a5a5a5a) ? 0xffffffffu : W(out) - W(m), m->cGZPropertySetString::mRefCount});
        // Each face's own Release gives the reference back (the count stays above 1).
        if (got) r.push_back(svc::CallWords(svc::VTableAt(out, 0)[2], out, 0, nullptr));
        break;
    }
    case kRef: {
        // AddRef, or Release while the count stays above 1, on any face.
        const bool release = m->cGZPropertySetString::mRefCount > 2 && st.flag;
        r.push_back(Call(m, kFaces[st.face], release ? 2 : 1, 0, nullptr));
        break;
    }
    case kService: {
        const ServiceSlot &slot = kServiceSlots[st.extra % (sizeof(kServiceSlots) / sizeof(kServiceSlots[0]))];
        const uint32_t a[1] = {slot.slot == 7 ? st.flag : st.value};
        r.push_back(svc::Masked(slot.ret, Call(m, slot.face, slot.slot, slot.words, a)));
        break;
    }
    case kNotifyHelper:
        if (s.original) s.ctx->Original<void(__thiscall *)(void *, const void *)>(kNotify)(m, key);
        else m->NotifyWatchers(key);
        break;
    case kSetDefaultHelper:
        if (s.original) s.ctx->Original<void(__thiscall *)(void *, const void *, void *)>(kSetDefault)(m, key, &value);
        else m->SetDefault(key, &value);
        cursorValid = false;
        break;
    case kAddIfAbsentHelper: {
        // A key offset as its callers pass them: the start of an interned key (the getters read the text there). Skipped on
        // both sides while nothing is interned.
        std::vector<uint32_t> starts;
        for (uint32_t i = 0; i < m->mKeyText.size(); ++i)
            if (i == 0 || m->mKeyText[i - 1] == '\0') starts.push_back(i);
        if (starts.empty()) break;
        uint32_t offset = starts[st.extra % starts.size()];
        if (s.original) s.ctx->Original<void(__thiscall *)(void *, const uint32_t *, void *)>(kAddIfAbsent)(m, &offset, &value);
        else m->AddEntryIfAbsent(&offset, &value);
        cursorValid = false;
        break;
    }
    default: break;
    }
    DropHeld(s.w);
    return r;
}

Step RandomStep(std::mt19937 &rng)
{
    static const Op weighted[] = {kSet, kSet, kSet, kAdd, kAdd, kAdd, kAdd, kRemove, kRemove, kRemoveAll, kClear,
                                  kRemoveProperty, kCopyFrom, kGetFirst, kGetNext, kGetNext, kGet, kGetValue, kHas, kCount,
                                  kGetSet, kShutdown, kQI, kQI, kRef, kRef, kService, kNotifyHelper, kSetDefaultHelper,
                                  kAddIfAbsentHelper};
    Step st{};
    st.op = weighted[rng() % (sizeof(weighted) / sizeof(weighted[0]))];
    st.key = rng() % kKeyCount;
    st.value = rng() % 16;
    st.target = rng() % kTargets;
    st.type = rng() % 3 == 0 ? 0x1000 + rng() % 4 : 0x2000;
    st.face = rng() % 4;
    st.flag = rng() % 2;
    st.iid = rng() % (sizeof(kIids) / sizeof(kIids[0]));
    st.extra = rng();
    return st;
}

const char *OpName(Op op)
{
    static const char *const names[] = {"Set", "Add", "Remove", "RemoveAll", "Clear", "RemoveProperty", "CopyFrom", "GetFirst", "GetNext",
                                        "Get", "GetValue", "Has", "Count", "GetSet", "Shutdown", "QI", "Ref", "Service", "Notify",
                                        "SetDefault", "AddIfAbsent"};
    return op < kOps ? names[op] : "?";
}

// ---- construction, destruction and code maps --------------------------------------------------------------------------

Manager *NewOriginal(difftest::Context &ctx, int fill)
{
    void *memory = ::operator new(sizeof(Manager));
    std::memset(memory, fill, sizeof(Manager));
    return ctx.Original<Manager *(__thiscall *)(void *)>(kCtor)(memory);
}
Manager *NewOurs(int fill)
{
    void *memory = ::operator new(sizeof(Manager));
    std::memset(memory, fill, sizeof(Manager));
    return cGZPropertyManagerStringAdapters::Construct(memory, nullptr);
}

// Each side's vtable words as codes: the four faces' (0..3); after destruction, what each side's destructor leaves (the
// set's and the service part's bases; 0x10.. and the service base codes 0x20..).
struct Maps {
    svc::CodeMap a, b;
    Maps()
    {
        Manager *ours = NewOurs(0);
        for (uint32_t f = 0; f < 4; ++f) {
            a.AddRva(kGameVt[f], f);
            b.Add(svc::VTableAt(ours, kFaces[f]), f);
        }
        ours->~Manager();
        // After destruction: at +0 the set base's vtable (the original's destructor ends in the abstract
        // cGZPropertySetT<string traits> one, 0xda3dc0; ours leaves cGZPropertySetString's) and at +0x24 the interface's
        // (the original's 0xe98c30).
        a.AddRva(0xda3dc0, 0x10);
        b.Add(svc::VTableAt(ours, 0), 0x10);
        a.AddRva(Manager::kInterfaceVTable, 0x11);
        b.Add(svc::VTableAt(ours, 0x24), 0x11);
        ::operator delete(ours);
        svc::AddServiceBaseCodes(a, b, 0x20);
    }
    std::string Compare(const Manager *x, const Manager *y) const
    {
        svc::CodeMap ma = a, mb = b;
        ma.Add(*reinterpret_cast<void *const *>(&x->mWatchers), 0x100);
        mb.Add(*reinterpret_cast<void *const *>(&y->mWatchers), 0x100);
        // The cursor (+0x14): a pointer into each side's own entries (after destruction, freed ones; the destructors leave
        // it). The live sequences compare it as an entry index.
        if (x->mCursor) ma.Add(x->mCursor, 0x101);
        if (y->mCursor) mb.Add(y->mCursor, 0x101);
        return svc::Diff(svc::Words(x, sizeof(Manager), ma), svc::Words(y, sizeof(Manager), mb));
    }
};

bool CheckGameSlots(difftest::Context &ctx, Manager *a)
{
    for (const GameSlot &g : kGameSlots) {
        if (svc::VTableAt(a, g.face)[g.slot] != GameAddress<void>(g.rva)) {
            ctx.Fail("the game's face +0x%x slot %u is not 0x%x", g.face, g.slot, g.rva);
            return false;
        }
    }
    return true;
}

} // namespace

// ---- tests ---------------------------------------------------------------------------------------------------------------

// The constructors on fixed and random fills (the construct-only test of src/app/AppInit.*, moved here and strengthened):
// the result is the block, every word of the object compares (the padding after the running flag included; vtables and the
// map head as codes), the 16 bytes past it are untouched, and the map head is a sentinel of the same heap size on both sides.
DIFF_TEST(cGZPropertyManager_Construct, 0xc26045)
{
    constexpr size_t kTail = 16;
    Maps maps;
    for (uint32_t seed = 1; seed <= 8; ++seed) {
        uint8_t *ma = static_cast<uint8_t *>(::operator new(sizeof(Manager) + kTail));
        uint8_t *mb = static_cast<uint8_t *>(::operator new(sizeof(Manager) + kTail));
        std::mt19937 rng(seed);
        for (size_t i = 0; i < sizeof(Manager) + kTail; ++i)
            ma[i] = mb[i] = seed <= 4 ? static_cast<uint8_t>(0x55 * (seed - 1)) : static_cast<uint8_t>(rng());
        Manager *a = ctx.Original<Manager *(__thiscall *)(void *)>(kCtor)(ma);
        Manager *b = cGZPropertyManagerStringAdapters::Construct(mb, nullptr);
        ++ctx.cases;
        if (reinterpret_cast<uint8_t *>(a) != ma || reinterpret_cast<uint8_t *>(b) != mb) ctx.Fail("seed %u: wrong return", seed);
        const std::string diff = maps.Compare(a, b);
        if (!diff.empty()) ctx.Fail("seed %u: object differs at %s", seed, diff.c_str());
        if (std::memcmp(ma + sizeof(Manager), mb + sizeof(Manager), kTail) != 0) ctx.Fail("seed %u: bytes past the object differ", seed);
        CheckGameSlots(ctx, a);
        for (uint32_t f = 0; f < 4; ++f)
            if (svc::VTableAt(b, kFaces[f]) == GameAddress<void *>(kGameVt[f])) ctx.Fail("our face %u has the game's vtable", f);
        OuterNode *ha = Head(a), *hb = Head(b);
        if (ha->left != ha || ha->parent != ha || ha->right != ha || ha->color != 1 || ha->isnil != 1) ctx.Fail("original head unexpected");
        if (hb->left != hb || hb->parent != hb || hb->right != hb || hb->color != 1 || hb->isnil != 1) ctx.Fail("our head differs");
        if (HeapSize(GetProcessHeap(), 0, ha) != HeapSize(GetProcessHeap(), 0, hb)) ctx.Fail("seed %u: head sizes differ", seed);
        if (b->mServiceId != Manager::kServiceId || b->mPriority != 0 || b->mTickPriority != 0) ctx.Fail("our service fields");
        ctx.Original<void(__thiscall *)(void *)>(kDtor)(a);
        b->~Manager();
        ::operator delete(ma);
        ::operator delete(mb);
    }
}

// The sequences (see the top of the file). Each case ends through one of the destruction routes.
DIFF_TEST(cGZPropertyManager_Sequences, 0xc272c0)
{
    svcfx::AllocatorScope allocator;
    Maps maps;
    std::vector<GZServiceString> keys;
    for (const char *text : kKeyTexts) keys.emplace_back(text);
    for (uint32_t seed = 1; seed <= 120; ++seed) {
        std::mt19937 rngA(seed), rngB(seed), rng(seed * 7919);
        auto sa = std::make_unique<Side>(), sb = std::make_unique<Side>();
        sa->original = true;
        sb->original = false;
        for (Side *s : {sa.get(), sb.get()}) {
            s->ctx = &ctx;
            s->keys = &keys;
            s->w.m = s->original ? NewOriginal(ctx, 0xCD) : NewOurs(0xCD);
        }
        InitWorld(sa->w, rngA);
        InitWorld(sb->w, rngB);
        if (!CheckGameSlots(ctx, sa->w.m)) return;
        // A source set for CopyFrom (one of ours on both sides, the same content).
        const uint32_t sourceSize = rng() % 4;
        uint32_t sourceKeys[4], sourceValues[4];
        for (uint32_t i = 0; i < sourceSize; ++i) sourceKeys[i] = rng() % kKeyCount, sourceValues[i] = rng() % 16;
        for (Side *s : {sa.get(), sb.get()}) {
            s->source = new cGZPropertySetString;
            s->source->AddRef();
            for (uint32_t i = 0; i < sourceSize; ++i) {
                Value v = MakeValue(sourceValues[i]);
                s->source->SetProperty(&keys[sourceKeys[i]], &v);
            }
            // One reference held by the test.
            Call(s->w.m, 0, 1, 0, nullptr);
        }
        bool cursorA = false, cursorB = false;
        const int steps = 10 + static_cast<int>(rng() % 50);
        bool failed = false;
        for (int i = 0; i < steps && !failed; ++i) {
            const Step st = RandomStep(rng);
            const Blob ra = Apply(*sa, st, cursorA);
            const Blob rb = Apply(*sb, st, cursorB);
            ++ctx.cases;
            if (ra != rb) ctx.Fail("seed %u step %d %s: results differ", seed, i, OpName(st.op)), failed = true;
            else if (sa->w.log != sb->w.log) ctx.Fail("seed %u step %d %s: target calls differ (%zu/%zu words)", seed, i, OpName(st.op), sa->w.log.size(), sb->w.log.size()), failed = true;
            else if (Snap(sa->w, cursorA) != Snap(sb->w, cursorB)) ctx.Fail("seed %u step %d %s: manager state differs", seed, i, OpName(st.op)), failed = true;
            sa->w.log.clear();
            sb->w.log.clear();
        }
        // The end: 0..3 Release to zero on face route % 4, 4 the +0 deleting destructor, 5 the +0x2c one, 6 the destructor.
        const uint32_t route = rng() % 7, flags = rng() % 2;
        Blob ends[2];
        bool freed[2];
        Blob words[2];
        int k = 0;
        for (Side *s : {sa.get(), sb.get()}) {
            Manager *m = s->w.m;
            Blob &end = ends[k];
            difftest::BeginFrees();
            if (route < 4) {
                for (int n = 0; n < 64; ++n) {
                    const uint32_t r = Call(m, kFaces[route], 2, 0, nullptr);
                    end.push_back(r);
                    if (r == 0) break;
                }
            } else if (route < 6) {
                const uint32_t a[1] = {flags};
                end.push_back(Call(m, route == 4 ? 0 : 0x2c, 13 - (route - 4) * 10, 1, a) - W(m));
            } else if (s->original) {
                ctx.Original<void(__thiscall *)(void *)>(kDtor)(m);
            } else {
                m->~Manager();
            }
            freed[k] = difftest::FreedSinceBegin(m);
            end.insert(end.end(), s->w.log.begin(), s->w.log.end());
            for (const FakeTarget &t : s->w.targets) end.push_back(t.refs);
            ++k;
        }
        ++ctx.cases;
        if (!failed) {
            if (ends[0] != ends[1]) ctx.Fail("seed %u route %u: destruction differs (%zu/%zu words)", seed, route, ends[0].size(), ends[1].size());
            if (freed[0] != freed[1]) ctx.Fail("seed %u route %u: freed %d/%d", seed, route, freed[0], freed[1]);
            for (const FakeTarget &t : sa->w.targets)
                if (t.refs != 1) ctx.Fail("seed %u route %u: a target left with %u references", seed, route, t.refs);
            if (!freed[0] && !freed[1]) {
                const std::string diff = maps.Compare(sa->w.m, sb->w.m);
                if (!diff.empty()) ctx.Fail("seed %u route %u: words after destruction differ at %s", seed, route, diff.c_str());
            }
        }
        for (int j = 0; j < 2; ++j) {
            Side *s = j ? sb.get() : sa.get();
            if (!freed[j]) ::operator delete(s->w.m);
            s->source->Release();
        }
    }
}
