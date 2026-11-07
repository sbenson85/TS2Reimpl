// cTSBehavior (lane GU): an object's behaviour context in the standalone shape. Layout and slot list in cTSBehavior.h.
#include "simantics/cTSBehavior.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZString.h"
#include "rz/ServiceGetters.h"
#include "rz/StaticGetters.h"
#include "rz/StaticGetters2.h"

#include <algorithm>
#include <new>

namespace {

// Game text (read at run time, never copied): the kind names of slot 46, the error tree name of slot 34, the empty
// string, and the node description's pieces and formats (slot 32).
constexpr uint32_t kTextPrimitives = 0xdc9734;
constexpr uint32_t kTextGlobal = 0xda6408;
constexpr uint32_t kTextPrivate = 0xdc815c;
constexpr uint32_t kTextSemiGlobal = 0xdc9740;
constexpr uint32_t kTextUnknown = 0xdc4c48;
constexpr uint32_t kTextErrorTree = 0xdc9728;   // 11 characters
constexpr uint32_t kTextEmpty = 0xd9b9d0;
constexpr uint32_t kTextPrimPrefix = 0xdc96fc;  // 6 characters
constexpr uint32_t kFormatPrim = 0xda6db8;      // the prefix, then the opcode
constexpr uint32_t kTextOpen = 0xdb12b4;        // 1 character
constexpr uint32_t kTextClose = 0xdb12b0;       // 1 character
constexpr uint32_t kTextDefault = 0xdc9704;     // 7 characters
constexpr uint32_t kFormatOperands = 0xdc970c;  // the text so far, then eight operands

constexpr uint32_t kStringSetType = 0x53545223;  // "STR#"
constexpr uint32_t kStringSetGroup = 0x7fe59fd0;
constexpr uint32_t kStringSetInstance = 0x8b;
constexpr uint32_t kStringSetIid = 0xfb4cb0de;
constexpr uint32_t kConstantsIid = 0x5b267c89;
constexpr uint32_t kSaveIid = 0x656b8f0c;
constexpr uint32_t kPersistResourceIid = 0x456b8f1d;
constexpr uint32_t kCheckClassId = 0x5b561442;
constexpr uint32_t kCheckIid = 0x3b56143a;
constexpr uint32_t kErrorTreeId = 0x7fff;

// Calls on the objects behind other interfaces (resource services, sources, trees, nodes), by slot.
template <int kSlot, class R = uint32_t, class... A> __forceinline R Call(const void *object, A... args)
{
    return VirtualCall<R(__thiscall *)(void *, A...), kSlot>(object, args...);
}

void AddRefOf(cIGZUnknown *p) { p->AddRef(); }

// A node's operand block as the node's slot 15 returns it (by value through a hidden pointer): 0x18 bytes, the operands
// are eight shorts at +4.
struct NodeOperands {
    uint32_t mHeader;
    int16_t mOperand[8];
    uint32_t mTrailer;
};
static_assert(sizeof(NodeOperands) == 0x18, "operand block size");

} // namespace

// ---- life ----

cTSBehavior::cTSBehavior()
    : mRefCount(0), mGlobalSource(nullptr), mSemiGlobalSource(nullptr), mSemiGlobalGroup(0), mPrivateSource(nullptr),
      mPrivateGroup(0), mPrimitives(nullptr)
{
    // mGlobalGroup (+0x0c) and mOwner (+0x20) are left as the heap had them, as the factory 0x469e70 leaves them.
}

cTSBehavior *cTSBehavior::Create() { return new cTSBehavior; }

// 0x469560 (slot 50 with the delete): the held pointers released last member first.
cTSBehavior::~cTSBehavior()
{
    if (mPrimitives) mPrimitives->Release();
    if (mPrivateSource) mPrivateSource->Release();
    if (mSemiGlobalSource) mSemiGlobalSource->Release();
    if (mGlobalSource) mGlobalSource->Release();
}

// ---- cIGZUnknown ----

// 0x46d020
bool cTSBehavior::QueryInterface(uint32_t riid, void **out)
{
    switch (riid) {
    case 1:
    case kInterfaceId:
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

// folded 0x148fb0
uint32_t cTSBehavior::AddRef() { return ++mRefCount; }

// folded 0x46dba0: the last reference deletes (slot 50 with flag 1).
uint32_t cTSBehavior::Release()
{
    if (mRefCount <= 1) {
        delete this;
        return 0;
    }
    return --mRefCount;
}

// ---- setup ----

// 0x46c350 (slot 3): each given source with its own group (the source's slot 11), the owner word, the primitive set, then
// the breakpoints applied to every source.
bool cTSBehavior::Init(uint32_t owner, cIGZUnknown *primitives, cTSBehaviorSource *global, cTSBehaviorSource *privateSource,
                       cTSBehaviorSource *semiGlobal)
{
    if (global) SetGlobalSource(Call<11>(global), global);
    if (semiGlobal) SetSemiGlobalSource(Call<11>(semiGlobal), semiGlobal);
    if (privateSource) SetPrivateSource(Call<11>(privateSource), privateSource);
    mOwner = owner;
    cIGZUnknown *old = mPrimitives;
    if (primitives != old) {
        if (primitives) primitives->AddRef();
        mPrimitives = primitives;
        if (old) old->Release();
    }
    ApplyAllBreakpoints();
    return true;
}

// 0x46e0e0 (slot 6): the group is stored only when the source changes, before the reference moves.
void cTSBehavior::SetGlobalSource(uint32_t group, cTSBehaviorSource *source)
{
    cTSBehaviorSource *old = mGlobalSource;
    if (old != source) {
        mGlobalGroup = group;
        if (source) source->AddRef();
        mGlobalSource = source;
        if (old) old->Release();
    }
}

// 0x46e2b0 (slot 7): the group after the reference moves (only when the source changes).
void cTSBehavior::SetSemiGlobalSource(uint32_t group, cTSBehaviorSource *source)
{
    cTSBehaviorSource *old = mSemiGlobalSource;
    if (old != source) {
        if (source) source->AddRef();
        mSemiGlobalSource = source;
        if (old) old->Release();
        mSemiGlobalGroup = group;
    }
}

// 0x46e270 (slot 8)
void cTSBehavior::SetPrivateSource(uint32_t group, cTSBehaviorSource *source)
{
    cTSBehaviorSource *old = mPrivateSource;
    if (old != source) {
        if (source) source->AddRef();
        mPrivateSource = source;
        if (old) old->Release();
        mPrivateGroup = group;
    }
}

// ---- getters ----

uint32_t cTSBehavior::GetGlobalGroup() { return mGlobalGroup; }                       // folded 0x145770
uint32_t cTSBehavior::GetSemiGlobalGroup() { return mSemiGlobalGroup; }               // folded 0x149d00
uint32_t cTSBehavior::GetPrivateGroup() { return mPrivateGroup; }                     // folded 0x147d00
cTSBehaviorSource *cTSBehavior::GetGlobalSource() { return mGlobalSource; }           // folded 0x46390
cTSBehaviorSource *cTSBehavior::GetSemiGlobalSource() { return mSemiGlobalSource; }   // folded 0x149cf0
cTSBehaviorSource *cTSBehavior::GetPrivateSource() { return mPrivateSource; }         // folded 0x148ba0
cIGZUnknown *cTSBehavior::GetPrimitives() { return mPrimitives; }                     // folded 0x15e970
uint32_t cTSBehavior::GetOwner() { return mOwner; }                                   // folded 0x15e550
cITSBehavior *cTSBehavior::AsBehavior() { return this; }                              // folded 0x1413e0
cITSBehavior *cTSBehavior::AsBehavior2() { return this; }                             // folded 0x1413e0
void cTSBehavior::Nothing28() {}                                                      // folded 0x140fe0
void cTSBehavior::Nothing42() {}                                                      // folded 0x140fe0

// 0x46a730 (slot 12): the group of the source that holds an id (16 bits, signed): 0 outside 0x100..0x2710.
uint32_t cTSBehavior::GetGroupForId(int16_t treeId)
{
    if (static_cast<uint16_t>(treeId - 0x100) <= 0x2610) {
        const int16_t id = treeId;
        if (id <= 0xfff) return mGlobalGroup;
        if (id <= 0x1fff) return mPrivateGroup;
        return mSemiGlobalGroup;
    }
    return 0;
}

// The first id of a kind (slots 13, 18 and 31 inline it, slot 44 returns it).
static uint32_t BaseIdOf(int16_t kind)
{
    uint32_t id = 0;
    switch (kind) {
    case 0: id = 0; break;
    case 1: id = 0x100; break;
    case 2: id = 0x1000; break;
    case 3: id = 0x2000; break;
    }
    return id;
}

// 0x46a780 (slot 13): through slot 12 (a virtual call, as the original's tail jump).
uint32_t cTSBehavior::GetGroupForKind(uint32_t kind) { return GetGroupForId(BaseIdOf(static_cast<int16_t>(kind))); }

// 0x46bd90 (slot 17): the source that holds an id, as slot 12.
cTSBehaviorSource *cTSBehavior::GetSourceForId(int16_t treeId)
{
    if (static_cast<uint16_t>(treeId - 0x100) <= 0x2610) {
        const int16_t id = treeId;
        if (id <= 0xfff) return mGlobalSource;
        if (id <= 0x1fff) return mPrivateSource;
        return mSemiGlobalSource;
    }
    return nullptr;
}

// 0x46bde0 (slot 18)
cTSBehaviorSource *cTSBehavior::GetSourceForKind(uint32_t kind) { return GetSourceForId(BaseIdOf(static_cast<int16_t>(kind))); }

// 0x46a700, 0x46be60, 0x46bd60 (slots 19..21): a kind's source name through slot 44 and slot 22.
bool cTSBehavior::GetGlobalSourceName(cIGZString *out) { return GetSourceName(static_cast<uint16_t>(GetBaseIdForKind(1)), out); }
bool cTSBehavior::GetSemiGlobalSourceName(cIGZString *out) { return GetSourceName(static_cast<uint16_t>(GetBaseIdForKind(3)), out); }
bool cTSBehavior::GetPrivateSourceName(cIGZString *out) { return GetSourceName(static_cast<uint16_t>(GetBaseIdForKind(2)), out); }

// 0x46be30 (slot 22): the name of the source holding an id (the source's slot 14).
bool cTSBehavior::GetSourceName(uint32_t treeId, cIGZString *out)
{
    cTSBehaviorSource *source = GetSourceForId(treeId);
    if (source && out) {
        Call<14, void>(source, out);
        return true;
    }
    return false;
}

// 0x46a130 (slot 44)
uint32_t cTSBehavior::GetBaseIdForKind(uint32_t kind)
{
    uint32_t id = 0;
    switch (static_cast<int16_t>(kind)) {
    case 0: id = 0; break;
    case 1: id = 0x100; break;
    case 2: id = 0x1000; break;
    case 3: id = 0x2000; break;
    }
    return id;
}

// 0x46a800 (slot 45): the last id of a kind.
uint32_t cTSBehavior::GetMaxIdForKind(uint32_t kind)
{
    uint32_t id = 0;
    switch (static_cast<int16_t>(kind)) {
    case 0: id = 0; break;
    case 1: id = 0xfff; break;
    case 2: id = 0x1fff; break;
    case 3: id = 10000; break;
    }
    return id;
}

// 0x46f1c0 (__cdecl; slot 46 0x46a280 forwards): the kind's name into out (slot 4).
__declspec(noinline) void cTSBehavior::KindName(uint32_t kind, cIGZString *out)
{
    uint32_t text;
    switch (static_cast<int16_t>(kind)) {
    case 0: text = kTextPrimitives; break;
    case 1: text = kTextGlobal; break;
    case 2: text = kTextPrivate; break;
    case 3: text = kTextSemiGlobal; break;
    default: text = kTextUnknown; break;
    }
    out->FromChar(GameAddress<const char>(text));
}

void cTSBehavior::GetKindName(uint32_t kind, cIGZString *out) { KindName(kind, out); }

// 0x46f230 (__cdecl; slot 47 0x46c000 forwards): the kind of an id (16 bits, unsigned compares).
__declspec(noinline) uint32_t cTSBehavior::KindForId(uint16_t treeId)
{
    if (static_cast<uint16_t>(treeId) > 0xff) {
        if (static_cast<uint16_t>(treeId - 0x100) <= 0xeff) return 1;
        if (static_cast<uint16_t>(treeId - 0x2000) <= 0x710) return 3;
        return static_cast<uint16_t>(treeId - 0x1000) > 0xfff ? 0 : 2;
    }
    return 0;
}

uint32_t cTSBehavior::GetKindForId(uint32_t treeId) { return KindForId(treeId); }

// ---- trees and resources ----

// 0x46a1a0 (slot 23): the tree resource {BHAV, group, id} from the resource cache (GetCachedSingletonC, slot 72), told the
// primitive set (tree slot 30). The cache keeps it alive: the reference taken here is released before it is returned.
cIGZUnknown *cTSBehavior::GetTree(uint32_t treeId)
{
    uint32_t key[4];
    key[1] = GetGroupForId(treeId);
    key[2] = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(treeId)));
    key[0] = kTreeType;
    key[3] = 0;
    cIGZUnknown *tree = nullptr;
    void *cache = GetCachedSingletonC();
    cIGZUnknown *result;
    if (!Call<72, bool>(cache, key, kTreeIid, &tree)) {
        result = nullptr;
    } else {
        Call<30, void>(tree, mPrimitives);
        result = tree;
    }
    if (tree) tree->Release();
    return result;
}

// 0x469820 (slot 24): a tree made by the resource database (the service's slot 3, then slot 7 with the tree interface), told
// the primitive set and `name` (tree slots 30 and 33), then asked for `iid` (its slot 0) when the database said so.
bool cTSBehavior::CreateTree(uint32_t treeId, uint32_t iid, void **out, uint32_t name)
{
    uint32_t key[4];
    key[1] = GetGroupForId(treeId);
    key[2] = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(treeId)));
    key[0] = kTreeType;
    key[3] = 0;
    cIGZUnknown *tree = nullptr;
    void *database = Call<3, void *>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>());
    const bool made = Call<7, bool>(database, key, kTreeIid, &tree, 0u, 0u);
    // ORIGINAL-BUG: the tree is used without a null test; a database that answers false without writing one crashes here.
    Call<30, void>(tree, mPrimitives);
    Call<33, void>(tree, name);
    bool result = false;
    if (made) result = tree->QueryInterface(iid, out);
    if (tree) tree->Release();
    return result;
}

// 0x46dc40 (slot 25): the cached tree resource told to drop the key (its slot 26), and the database's cache entry removed
// (slot 10). Always true.
bool cTSBehavior::UncacheTree(uint32_t treeId)
{
    uint32_t key[4];
    key[1] = GetGroupForId(treeId);
    key[2] = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(treeId)));
    key[0] = kTreeType;
    key[3] = 0;
    void *service = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    cIGZUnknown *resource = nullptr;
    void *database = Call<3, void *>(service);
    if (Call<34, bool>(database, key, &resource)) Call<26, void>(resource, key);
    Call<10, void>(Call<3, void *>(service), key);
    if (resource) resource->Release();
    return true;
}

// 0x46dd10 (slot 26): the tree written back: the service's record for the key (slot 34 with the save interface), opened
// (slot 28), the tree's persist face keyed (slot 4) and saved into it (service slot 16).
bool cTSBehavior::SaveTree(uint32_t treeId)
{
    cIGZUnknown *tree = GetTree(treeId);
    if (tree) tree->AddRef();
    bool result;
    if (!tree) {
        result = false;
    } else {
        void *service = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
        uint32_t key[4];
        key[1] = GetGroupForId(treeId);
        key[0] = kTreeType;
        key[2] = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(treeId)));
        key[3] = 0;
        cIGZUnknown *record = nullptr;
        result = Call<34, bool>(service, key, kSaveIid, &record);
        if (result) {
            result = Call<28, bool>(service, record);
            if (result) {
                cIGZUnknown *persist = nullptr;
                tree->QueryInterface(kPersistResourceIid, reinterpret_cast<void **>(&persist));
                void *saver = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
                Call<4, void>(persist, key);
                result = Call<16, bool>(saver, persist, record);
                if (persist) persist->Release();
            }
        }
        if (record) record->Release();
    }
    if (tree) tree->Release();
    return result;
}

// 0x46dbe0 (slot 27): the database's cache entry removed (slot 10), then whether the tree loads again.
bool cTSBehavior::ReloadTree(uint32_t treeId)
{
    uint32_t key[4];
    key[1] = GetGroupForId(treeId);
    key[0] = kTreeType;
    key[2] = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(treeId)));
    key[3] = 0;
    Call<10, void>(Call<3, void *>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>()), key);
    return GetTree(treeId) != nullptr;
}

// 0x46a2a0 (slot 29): the constants resource {BCON, group, id} from the resource cache (its slot 72), its answer passed on.
uint32_t cTSBehavior::GetConstants(uint32_t id, uint32_t iid, void **out)
{
    void *cache = GetCachedSingletonC();
    uint32_t key[4];
    key[1] = GetGroupForId(id);
    key[2] = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(id)));
    key[0] = kConstantsType;
    key[3] = 0;
    return Call<72>(cache, key, iid, out);
}

// 0x46a3b0 (slot 30): the tree's size (tree slot 8) shifted up 16 bits, plus the size of every tree reachable through its
// calls (node opcodes from 0x100, node slot 3), the tree itself included: a depth-first walk over two static vectors (the
// original's are function statics at 0x10daa00 / 0x10daa10, emptied on entry).
uint32_t cTSBehavior::GetTreeSize(uint32_t treeId)
{
    static std::vector<uint16_t> pending;
    static std::vector<uint16_t> visited;
    pending.clear();
    visited.clear();
    pending.push_back(static_cast<uint16_t>(treeId));
    while (!pending.empty()) {
        const uint16_t id = pending.back();
        pending.pop_back();
        if (std::find(visited.begin(), visited.end(), id) != visited.end()) continue;
        visited.push_back(id);
        cIGZUnknown *tree = GetTree(id);
        if (!tree) continue;
        tree->AddRef();
        for (int32_t i = 0; i < static_cast<int16_t>(Call<20>(tree)); ++i) {
            cIGZUnknown *node = Call<22, cIGZUnknown *>(tree, i);
            if (node) node->AddRef();
            if (static_cast<int16_t>(Call<3>(node)) >= 0x100) pending.push_back(static_cast<uint16_t>(Call<3>(node)));
            if (node) node->Release();
        }
        tree->Release();
    }
    uint32_t size = 0;
    cIGZUnknown *root = GetTree(treeId);
    if (root) root->AddRef();
    if (root) size = Call<8>(root) << 16;
    for (const uint16_t id : visited) {
        cIGZUnknown *tree = GetTree(id);
        if (tree) tree->AddRef();
        if (tree) size += Call<8>(tree);
        if (tree) tree->Release();
    }
    if (root) root->Release();
    return size;
}

// 0x46c010 (slot 31): the id looked up (source slot 22, type BHAV) in the private, the semi-global and the global source, in
// that order, until one answers a non-zero word; the flags skip the global and the semi-global source. The word is the result.
uint32_t cTSBehavior::FindTree(uint32_t treeId, bool skipGlobal, bool skipSemiGlobal)
{
    uint32_t found = 0;
    const int32_t kinds[3] = {2, 3, 1};
    for (int i = 0; i < 3; ++i) {
        if (i == 1 && skipSemiGlobal) continue;
        if (i == 2 && skipGlobal) continue;
        cTSBehaviorSource *source = GetSourceForId(BaseIdOf(static_cast<int16_t>(kinds[i])));
        if (source) Call<22, void>(source, kTreeType, treeId, &found);
        if (static_cast<uint16_t>(found) != 0) break;
    }
    return found;
}

// ---- names and text ----

// 0x46b950 (slot 32): a node's text: what the primitive set writes (its slot 5), else the tree or primitive name and the
// operands, then the result copied into out (slot 17).
void cTSBehavior::DescribeNode(uint32_t a, cIGZUnknown *node, cIGZString *out)
{
    const uint32_t length = out->Strlen();
    const char *data = out->Data();
    cRZString text;
    text.Construct();
    text.mString.assign(data, length);
    if (mPrimitives) {
        Call<5, void>(mPrimitives, this, a, node, &text);
    } else {
        text.mString.clear();
    }
    if (text.Strlen() == 0) {
        if (static_cast<int16_t>(Call<3>(node)) < 0x100) {
            text.mString.append(GameAddress<const char>(kTextPrimPrefix), 6);
            text.Sprintf(GameAddress<const char>(kFormatPrim), text.mString.c_str(),
                         static_cast<int32_t>(static_cast<int16_t>(Call<3>(node))));
        } else {
            cRZString name;
            name.Construct();
            GetTreeName(static_cast<uint16_t>(Call<3>(node)), name.AsInterface());
            text.mString.append(name.mString.data(), name.mString.size());
            name.Destruct();
        }
        text.mString.append(GameAddress<const char>(kTextOpen), 1);
        NodeOperands first;
        const NodeOperands *check = Call<15, NodeOperands *>(node, &first);
        if (reinterpret_cast<const int32_t *>(check)[1] == -1 && reinterpret_cast<const int32_t *>(check)[2] == -1) {
            text.mString.append(GameAddress<const char>(kTextDefault), 7);
        } else {
            NodeOperands o[8];
            const NodeOperands *p7 = Call<15, NodeOperands *>(node, &o[0]);
            const NodeOperands *p6 = Call<15, NodeOperands *>(node, &o[1]);
            const NodeOperands *p5 = Call<15, NodeOperands *>(node, &o[2]);
            const NodeOperands *p4 = Call<15, NodeOperands *>(node, &o[3]);
            const NodeOperands *p3 = Call<15, NodeOperands *>(node, &o[4]);
            const NodeOperands *p2 = Call<15, NodeOperands *>(node, &o[5]);
            const NodeOperands *p1 = Call<15, NodeOperands *>(node, &o[6]);
            const NodeOperands *p0 = Call<15, NodeOperands *>(node, &o[7]);
            text.Sprintf(GameAddress<const char>(kFormatOperands), text.mString.c_str(), int32_t(p0->mOperand[0]),
                         int32_t(p1->mOperand[1]), int32_t(p2->mOperand[2]), int32_t(p3->mOperand[3]),
                         int32_t(p4->mOperand[4]), int32_t(p5->mOperand[5]), int32_t(p6->mOperand[6]),
                         int32_t(p7->mOperand[7]));
        }
        text.mString.append(GameAddress<const char>(kTextClose), 1);
    }
    out->Copy(*text.AsInterface());
    text.Destruct();
}

// 0x46bc50 (slot 33): the primitive set's text (its slot 6), or out emptied (slot 26) when there is none.
void cTSBehavior::DescribePrimitive(uint32_t a, uint32_t b, cIGZString *out)
{
    if (mPrimitives) {
        Call<6, void>(mPrimitives, this, a, b, out);
        return;
    }
    out->Erase(0, 0xffffffffu);
}

// 0x46c120 (slot 34): a tree's name: primitives by the primitive set (slot 7), 0x7fff the error text, a loaded tree's own
// name (its slot 32 into out's slot 16), else the source's resource name (slots 16, 19, 23) or the empty string.
void cTSBehavior::GetTreeName(uint32_t treeId, cIGZString *out)
{
    const int16_t id = static_cast<int16_t>(treeId);
    if (id < 0x100) {
        Call<7, void>(mPrimitives, treeId, out);
        return;
    }
    if (id == kErrorTreeId) {
        cRZString error;
        error.Construct(GameAddress<const char>(kTextErrorTree));
        out->Copy(*error.AsInterface());
        error.Destruct();
        return;
    }
    cIGZUnknown *tree = GetTree(treeId);
    if (tree) tree->AddRef();
    if (tree) {
        *out = *Call<32, const cIGZString *>(tree);
        tree->Release();
        return;
    }
    cTSBehaviorSource *source = GetSourceForId(treeId);
    if (source && Call<16, bool>(source)) {
        cIGZUnknown *resource = nullptr;
        if (Call<19, bool>(source, kTreeType, treeId, &resource)) {
            Call<23, void>(source, resource, out);
        } else {
            out->FromChar(GameAddress<const char>(kTextEmpty));
        }
        if (resource) resource->Release();
        return;
    }
    out->FromChar(GameAddress<const char>(kTextEmpty));
}

// 0x46a300 (slot 35): the constants resource's name (its slot 13), or out cleared (slot 17 from an empty string).
void cTSBehavior::GetConstantsName(uint32_t id, cIGZString *out)
{
    cIGZUnknown *constants = nullptr;
    if (!static_cast<uint8_t>(GetConstants(id, kConstantsIid, reinterpret_cast<void **>(&constants)))) {
        cRZString empty;
        empty.Construct();
        out->Copy(*empty.AsInterface());
        empty.Destruct();
    } else {
        Call<13, void>(constants, out);
    }
    if (constants) constants->Release();
}

// 0x469dd0 (slot 36): the count of a fixed string set (STR# 0x8b in group 0x7fe59fd0; its slot 5), 0 when it isn't there.
uint16_t cTSBehavior::GetStringCount()
{
    uint32_t key[4] = {kStringSetType, kStringSetGroup, kStringSetInstance, 0};
    void *service = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    cIGZUnknown *strings = nullptr;
    uint16_t count;
    if (Call<5, bool>(service, key, kStringSetIid, &strings, 0u, 0u)) {
        count = static_cast<uint16_t>(Call<5>(strings, 0u));
    } else {
        count = 0;
    }
    if (strings) strings->Release();
    return count;
}

// 0x46be90 (slot 39): asks the framework's object (class 0x5b561442) about the private source: its slot 5 with a, and when
// that answers 0, its slot 9 with c and b.
bool cTSBehavior::CheckFramework(uint32_t a, uint32_t b, uint32_t c)
{
    cTSBehaviorSource *source = GetPrivateSource();
    cIGZUnknown *checker = nullptr;
    void *framework = GetDirectorFrameworkObject();
    Call<3, void>(framework, kCheckClassId, kCheckIid, &checker);
    bool result;
    if (Call<5>(checker, source, a) != 0) {
        result = false;
    } else {
        result = Call<9, bool>(checker, c, b);
    }
    if (checker) checker->Release();
    return result;
}

// ---- breakpoints ----

cTSBehaviorBreakpoints &cTSBehavior::Breakpoints() { return *GameAddress<cTSBehaviorBreakpoints>(kBreakpoints); }

// 0x46d7f0 (slot 4): every node's breakpoint flag of the tree cleared (node slot 19), then set again for each breakpoint of
// this source and tree; a breakpoint past the tree's nodes is dropped. True when one was dropped.
bool cTSBehavior::ApplyBreakpoints(uint32_t treeId)
{
    cTSBehaviorSource *source = GetSourceForId(treeId);
    cRZString name;
    name.Construct();
    GetSourceName(treeId, name.AsInterface());
    bool dropped = false;
    cIGZUnknown *tree;
    if (source && (tree = GetTree(treeId)) != nullptr) {
        for (int32_t i = 0; i < static_cast<int16_t>(Call<20>(tree)); ++i) {
            cIGZUnknown *node = Call<22, cIGZUnknown *>(tree, i);
            if (node) node->AddRef();
            Call<19, void>(node, 0u);
            if (node) node->Release();
        }
        cTSBehaviorBreakpoints &list = Breakpoints();
        auto it = list.begin();
        while (it != list.end()) {
            if (it->mName.AsInterface()->CompareTo(*name.AsInterface(), false) == 0 &&
                it->mTreeId == static_cast<int16_t>(treeId)) {
                if (it->mNode < 0 || it->mNode >= static_cast<int16_t>(Call<20>(tree))) {
                    it = list.erase(it);
                    dropped = true;
                    continue;
                }
                Call<19, void>(Call<22, void *>(tree, it->mNode), 1u);
            }
            ++it;
        }
    }
    name.Destruct();
    return dropped;
}

// 0x46da40 (__cdecl): every breakpoint of a source applied (slot 4 of its tree); the walk starts over after a tree dropped
// one.
void cTSBehavior::ApplySourceBreakpoints(cTSBehavior *self, cTSBehaviorSource *source)
{
    if (!source) return;
    cRZString name;
    name.Construct();
    Call<14, void>(source, name.AsInterface());
    cTSBehaviorBreakpoints &list = Breakpoints();
    auto it = list.begin();
    while (it != list.end()) {
        if (it->mName.AsInterface()->CompareTo(*name.AsInterface(), false) == 0) {
            cIGZUnknown *tree = self->GetTree(static_cast<uint16_t>(it->mTreeId));
            if (tree) tree->AddRef();
            if (tree && it->mNode >= 0 && it->mNode < static_cast<int16_t>(Call<20>(tree)) &&
                self->ApplyBreakpoints(static_cast<uint16_t>(it->mTreeId))) {
                tree->Release();
                it = list.begin();
                continue;
            }
            if (tree) tree->Release();
        }
        ++it;
    }
    name.Destruct();
}

// 0x46d7c0 (slot 5): the breakpoints of the global, private and semi-global sources applied, when there are any.
void cTSBehavior::ApplyAllBreakpoints()
{
    const cTSBehaviorBreakpoints &list = Breakpoints();
    if (list.begin() == list.end()) return;
    ApplySourceBreakpoints(this, mGlobalSource);
    ApplySourceBreakpoints(this, mPrivateSource);
    ApplySourceBreakpoints(this, mSemiGlobalSource);
}

// 0x46de90 (slot 40): a breakpoint at (source name, tree, node) added (on) or removed (off), then the tree's breakpoints
// applied again (slot 4).
void cTSBehavior::SetBreakpoint(uint32_t treeId, uint8_t node, bool on)
{
    GetSourceForId(treeId);
    cRZString name;
    name.Construct();
    GetSourceName(treeId, name.AsInterface());
    cTSBehaviorBreakpoints &list = Breakpoints();
    auto it = list.begin();
    for (; it != list.end(); ++it) {
        if (it->mName.AsInterface()->CompareTo(*name.AsInterface(), false) == 0 &&
            it->mTreeId == static_cast<int16_t>(treeId) && it->mNode == static_cast<int32_t>(node))
            break;
    }
    if (it != list.end()) {
        if (!on) list.erase(it);
    } else if (on) {
        cTSBehaviorBreakpoint added;
        added.mName.Copy(*name.AsInterface());
        added.mTreeId = static_cast<int16_t>(treeId);
        added.mNode = node;
        list.push_back(added);
    }
    ApplyBreakpoints(treeId);
    name.Destruct();
}

// 0x46c3e0 (slot 41): the breakpoint file read by the original loader 0x46f290 (text file I/O; not ported).
void cTSBehavior::LoadBreakpoints(void *file) { OriginalFunction<void(__cdecl *)(void *), 0x46f290>()(file); }

// 0x46c5a0 (slot 43): the private trees dumped by the original writer 0x46c610 (file output; not ported), then, unless the
// target's slot 3 says so, the semi-global and global ones.
void cTSBehavior::DumpTrees(cIGZUnknown *target)
{
    using Dump = void(__cdecl *)(cTSBehavior *, cTSBehaviorSource *, cIGZUnknown *);
    OriginalFunction<Dump, 0x46c610>()(this, GetSourceForId(0x1000), target);
    if (!Call<3, bool>(target)) {
        if (cTSBehaviorSource *semi = GetSourceForId(0x2000)) OriginalFunction<Dump, 0x46c610>()(this, semi, target);
        if (cTSBehaviorSource *global = GetSourceForId(0x100)) OriginalFunction<Dump, 0x46c610>()(this, global, target);
    }
}

// ---- adapters (hooks kept for byte matches) ----

using A = cTSBehaviorAdapters;
[[msvc::forceinline_calls]] bool __fastcall A::QueryInterface(Self *self, void *, uint32_t riid, void **out) { return self->Self::QueryInterface(riid, out); }
[[msvc::forceinline_calls]] void __fastcall A::ApplyAllBreakpoints(Self *self, void *) { self->Self::ApplyAllBreakpoints(); }
[[msvc::forceinline_calls]] void __fastcall A::SetGlobalSource(Self *self, void *, uint32_t group, cTSBehaviorSource *source) { self->Self::SetGlobalSource(group, source); }
[[msvc::forceinline_calls]] void __fastcall A::SetSemiGlobalSource(Self *self, void *, uint32_t group, cTSBehaviorSource *source) { self->Self::SetSemiGlobalSource(group, source); }
[[msvc::forceinline_calls]] void __fastcall A::SetPrivateSource(Self *self, void *, uint32_t group, cTSBehaviorSource *source) { self->Self::SetPrivateSource(group, source); }
[[msvc::forceinline_calls]] uint32_t __fastcall A::GetGroupForId(Self *self, void *, int16_t treeId) { return self->Self::GetGroupForId(treeId); }
[[msvc::forceinline_calls]] cTSBehaviorSource *__fastcall A::GetSourceForId(Self *self, void *, int16_t treeId) { return self->Self::GetSourceForId(treeId); }
[[msvc::forceinline_calls]] bool __fastcall A::GetSourceName(Self *self, void *, uint32_t treeId, cIGZString *out) { return self->Self::GetSourceName(treeId, out); }
[[msvc::forceinline_calls]] void __fastcall A::DescribePrimitive(Self *self, void *, uint32_t a, uint32_t b, cIGZString *out) { self->Self::DescribePrimitive(a, b, out); }
[[msvc::forceinline_calls]] void __fastcall A::LoadBreakpoints(Self *self, void *, void *file) { self->Self::LoadBreakpoints(file); }
[[msvc::forceinline_calls]] uint32_t __fastcall A::GetKindForId(Self *self, void *, uint32_t treeId) { return self->Self::GetKindForId(treeId); }
