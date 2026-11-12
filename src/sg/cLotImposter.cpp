// nTSSG::cLotImposterDesc and nTSSG::cLotImposter (lane 261108j). Layouts and slots: src/sg/cLotImposter.h.
#include "sg/cLotImposter.h"

#include "app/AppInit.h"        // appinit::HasOptionBits
#include "game/original.h"
#include "game/owned.h"
#include "game/vtable.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "misc/ClassesRound4.h" // BitMask
#include "misc/SmallVirtuals.h" // cBranchNodesIIDFilter's vtable 0xe7cf54, cObjectImposterFilter
#include "rz/ServiceGetters.h"  // CachedServiceGetter
#include "rz/StringKeyParser.h"
#include "sg/SGNodeInterfaces.h"
#include "sg/SGUtilities.h"
#include "sg/cLotImposterDesignModes.h"
#include "sg/cSceneGraphSystem.h"
#include "sg/cShapeRefNode.h"
#include "sg/cTextureManager.h" // ResolveResourceName

#include <cstring>
#include <new>

using nGZSceneGraph::cIResourceNode;
using nGZSceneGraph::cISceneNodeIterator;

namespace {

// The resource manager Load asks for the lot's resources (original, not replaced yet).
void *ResourceManager() { return OriginalFunction<void *(__cdecl *)(), 0x20deb5>()(); }

// The scene graph system (the game's cache 0x10cc458) and its slot 66, the season manager.
void *SeasonManager()
{
    void *system = CachedServiceGetter<0x10cc458, 0x9a353ce8, 0xfa353cdb>();
    return RawSlot<66, void *>(system); // receiver: nTSSG::cITSSGSystem (0x254bc0, ret 0)
}

template <class T> void ReleaseAndNull(T *&p)
{
    if (T *old = p) {
        p = nullptr;
        old->Release();
    }
}

// The three counted iterators a node's child range (slot 44) is made of: the cursor, the begin, the end, released end
// first.
struct ImposterNodeRange {
    cISceneNodeIterator *mCursor = nullptr;
    cISceneNodeIterator *mBegin = nullptr;
    cISceneNodeIterator *mEnd = nullptr;
    ImposterNodeRange() = default;
    ImposterNodeRange(const ImposterNodeRange &) = delete;
    ImposterNodeRange &operator=(const ImposterNodeRange &) = delete;
    ~ImposterNodeRange()
    {
        if (mEnd) mEnd->Release();
        if (mBegin) mBegin->Release();
        if (mCursor) mCursor->Release();
    }
    bool AtEnd() { return mCursor->IsEqual(mEnd); }
};
static_assert(sizeof(ImposterNodeRange) == 0xc, "a child range: cursor, begin, end");

// The filter by interface id the first range takes: an nRZSceneGraph::cBranchNodesIIDFilter (vtable 0xe7cf54, built
// inline; its destructor stores the vtable again).
struct ImposterIidFilter {
    const void *mVTable;
    uint32_t mIid;
    explicit ImposterIidFilter(uint32_t iid) : mVTable(&ts2_rva_e7cf54), mIid(iid) {}
    ImposterIidFilter(const ImposterIidFilter &) = delete;
    ImposterIidFilter &operator=(const ImposterIidFilter &) = delete;
    ~ImposterIidFilter() { *static_cast<const void *volatile *>(&mVTable) = &ts2_rva_e7cf54; }
};

// The second range's filter: an nTSSG::cObjectImposterFilter (one slot, IsIncluded; the image's vtable 0xdbb3bc holds
// the same body, ours in SmallVirtuals.cpp).
template <typename T> const void *MemberCode(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance members only");
    const void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}
const void *const gObjectImposterFilterVTable[1] = {MemberCode(&nTSSG::cObjectImposterFilter::IsIncluded)};

// The resource manager's slots 3 and 5: the resource of a key, as interface `iid`, into `*out` (counted).
bool GetResource(void *manager, const void *key, uint32_t iid, void **out)
{
    // cTSPersistSystem 0x1e0890 or cGZPersistResourceManager 0x9b4d70, both ret 20
    return RawSlot<3, bool>(manager, key, iid, out, 0u, 0u); // receiver: cIGZPersistResourceManager
}

} // namespace

namespace nTSSG {

// ======================================== cLotImposterDesc ========================================

cLotImposterDesc::cLotImposterDesc() : mValue(0)
{
    cGZPersistResource::SetType(kType);
    cGZPersistResource::SetGroup(0);
    cGZPersistResource::SetInstance(0);
}

cLotImposterDesc::~cLotImposterDesc() = default;

cILotImposterDesc *__cdecl cLotImposterDesc::Create()
{
    cLotImposterDesc *desc = new (std::nothrow) cLotImposterDesc;
    return desc; // null stays null (the face is at +0)
}

// 0x36d2d0: cIGZSerializable -> +4, cILotImposterDesc -> this (each counted through the primary face), else the base's.
bool cLotImposterDesc::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid == kSerializableIid) {
        *out = static_cast<cIGZSerializable *>(this);
    } else if (riid == cILotImposterDesc::kIid) {
        *out = static_cast<cILotImposterDesc *>(this);
    } else {
        return cGZPersistResource::QueryInterface(riid, out);
    }
    AddRef();
    return true;
}

uint32_t cLotImposterDesc::AddRef() { return cGZPersistResource::AddRef(); }
uint32_t cLotImposterDesc::Release() { return cGZPersistResource::Release(); }

uint32_t cLotImposterDesc::GetValue() { return mValue; }
uint32_t cLotImposterDesc::SetValue(uint32_t value)
{
    mValue = value;
    return value;
}

// 0x36d390: the version (1), then the value through the face's slot 3; the stream's results are ignored.
bool cLotImposterDesc::Write(ifaces::cIGZOStream *stream)
{
    stream->SetUint16(kVersion);
    stream->SetUint32(static_cast<cILotImposterDesc *>(this)->GetValue());
    return true;
}

// 0x36d340: the version read and ignored, then the value.
bool cLotImposterDesc::Read(ifaces::cIGZIStream *stream)
{
    uint16_t version = 0;
    stream->GetUint16(&version);
    stream->GetUint32(&mValue);
    return true;
}

uint32_t cLotImposterDesc::GetGZCLSID() { return kType; }

// ======================================== cLotImposter ========================================

cLotImposter::cLotImposter() : mNode(nullptr), mDesc(nullptr) {}

cLotImposter::~cLotImposter()
{
    if (mDesc) mDesc->Release();
    if (mNode) mNode->Release();
}

cILotImposter *__cdecl cLotImposter::Create()
{
    cLotImposter *imposter = new (std::nothrow) cLotImposter;
    return imposter ? static_cast<cILotImposter *>(imposter) : nullptr;
}

// 0x36d110: cILotImposter -> +8 (counted), else the base's (interface 1).
bool cLotImposter::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid == cILotImposter::kIid) {
        *out = static_cast<cILotImposter *>(this);
        AddRef();
        return true;
    }
    return rz::cRZUnknown::QueryInterface(riid, out);
}

void cLotImposter::ReleaseResources()
{
    ReleaseAndNull(mNode);
    ReleaseAndNull(mDesc);
}

// 0x36d090
bool cLotImposter::Unload()
{
    ReleaseResources();
    return true;
}

nGZSceneGraph::cIResourceNode *cLotImposter::GetSceneNode() { return mNode; }
cILotImposterDesc *cLotImposter::GetDesc() { return mDesc; }
cILotImposterDesc *cLotImposter::GetDescAgain() { return mDesc; }

// 0x36cac0
bool cLotImposter::HasImposter() { return mDesc && mDesc->GetValue() != 0; }

// 0x36cae0: the node hung on the terrain (0xaed420, nGZSceneGraph::ForEachShapeBind) when there is an imposter to show.
void cLotImposter::AttachToTerrain(void *terrain)
{
    if (mNode && terrain && mDesc && mDesc->GetValue()) nGZSceneGraph::ForEachShapeBind(mNode, terrain, "terrain");
}

// 0x36cb20: the lot's imposter list (its package group's resource 0xac506764 / 0x486d) loaded with every resource it
// names (LoadImposterResources) and its desc taken; without a list, the group's "imposter_cres" scene graph alone. Then
// every shape reference node of the scene gets the "Props" and "Shade" layers, and every object imposter its season's
// variant (with the Seasons option) and its design modes. True when the resources loaded and there is a scene node.
bool cLotImposter::Load(uint32_t lot)
{
    ReleaseResources();
    uint32_t group = 0;
    if (!GetLotGroup(lot, &group)) return false;

    // slot 5: cTSPersistSystem 0x1e04a0 or cGZPersistResourceManager 0x9b4d20, both ret 20
    void *resources = ResourceManager(); // receiver: cIGZPersistResourceManager
    const ImposterKey listKey = {kLotGroupType, group, kLotGroupInstance, 0};
    cIGZUnknown *list = nullptr; // receiver: cITSPersistResKeyList (slot 7 0x147780, ret 0)
    bool ok = RawSlot<5, bool>(resources, &listKey, kKeyListIid, reinterpret_cast<void **>(&list), 0u, 0u);
    if (!ok) {
        StringKeyParser::Result key = {};
        cMapString name;
        nGZSceneGraph::ResolveResourceName(reinterpret_cast<cIGZString *>(&name), "imposter_cres", group, nullptr);
        StringKeyParser::ParseKey(&key, name.mString.c_str(), kSceneTypePreset, nullptr);
        ReleaseAndNull(mNode);
        ok = GetResource(resources, &key, kResourceNodeIid, reinterpret_cast<void **>(&mNode));
    } else {
        ReleaseAndNull(mNode);
        ok = LoadImposterResources(RawSlot<7, const ImposterKeyRange *>(list), &mNode);
        if (ok) {
            // The list's keys are asked for again for every count, every type looked at and the key handed on.
            for (uint32_t i = 0;; ++i) {
                const ImposterKeyRange *keys = RawSlot<7, const ImposterKeyRange *>(list);
                if (i >= static_cast<uint32_t>(keys->mEnd - keys->mBegin)) break;
                if (RawSlot<7, const ImposterKeyRange *>(list)->mBegin[i].mType == cLotImposterDesc::kType) {
                    const ImposterKey *descKey = &RawSlot<7, const ImposterKeyRange *>(list)->mBegin[i];
                    ReleaseAndNull(mDesc);
                    GetResource(resources, descKey, cILotImposterDesc::kIid, reinterpret_cast<void **>(&mDesc));
                    break;
                }
            }
        }
    }

    if (!mNode) {
        ok = false;
    } else {
        ImposterIidFilter shapeFilter(kShapeRefNodeIid);
        ImposterNodeRange shapes;
        RawSlot<44>(mNode, &shapes, 0u, &shapeFilter);
        while (!shapes.AtEnd()) {
            nGZSceneGraph::cIShapeRefNode *ref = nullptr;
            nGZSceneGraph::cISceneNode *node = shapes.mCursor->Get();
            if (reinterpret_cast<cIGZUnknown *>(node)->QueryInterface(kShapeRefNodeIid, reinterpret_cast<void **>(&ref))) {
                ref->AddLayer("Props");
                ref->AddLayer("Shade");
            }
            if (ref) ref->Release();
            shapes.mCursor->Next();
        }

        void *seasons = nullptr;
        if (static_cast<uint8_t>(appinit::HasOptionBits(BitMask(kSeasonsBit)))) seasons = SeasonManager();

        const void *objectFilter = gObjectImposterFilterVTable; // a cObjectImposterFilter: its vtable alone
        ImposterNodeRange objects;
        RawSlot<44>(mNode, &objects, 0u, &objectFilter);
        while (!objects.AtEnd()) {
            { // the node and its modes are let go before the cursor moves on
                cDesignModeNode modes;
                if (modes.Set(reinterpret_cast<cIGZUnknown *>(objects.mCursor->Get()))) {
                    if (seasons) modes.ApplySeason(seasons);
                    modes.ApplyDesignModesToTree();
                }
            }
            objects.mCursor->Next();
        }
    }
    if (list) list->Release();
    return ok;
}

} // namespace nTSSG
