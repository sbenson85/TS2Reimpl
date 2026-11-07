// cTSWantTreeLoader and cTSWantPropertySetLoader (lane IL): see the header. Element, attribute and value names are the
// image's strings, read through their pointer cells at run time.
#include "ts/cTSWantTreeLoader.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"
#include "gz/cGZPropertySet.h"
#include "rz/StaticGetters.h"
#include "ts/cTSWantNode.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <new>

static_assert(offsetof(cTSWantTreeLoader, mManager) == 0x04, "mManager");
static_assert(offsetof(cTSWantTreeLoader, mParser) == 0x08, "mParser");
static_assert(offsetof(cTSWantTreeLoader, mFactor) == 0x0c, "mFactor");
static_assert(offsetof(cTSWantTreeLoader, mSuperZero) == 0x14, "mSuperZero");
static_assert(offsetof(cTSWantTreeLoader, mBoolValue) == 0x1c, "mBoolValue");
static_assert(offsetof(cTSWantTreeLoader, mRangeIndex) == 0x28, "mRangeIndex");
static_assert(offsetof(cTSWantTreeLoader, mRefCount) == 0x2c, "mRefCount");
static_assert(offsetof(cTSWantTreeLoader, mTreeId) == 0x30, "mTreeId");
static_assert(offsetof(cTSWantTreeLoader, mInEditorValues) == 0x38, "mInEditorValues");
static_assert(offsetof(cTSWantTreeLoader, mSeenNode) == 0x39, "mSeenNode");
static_assert(offsetof(cTSWantTreeLoader, mPropertyLoader) == 0x3c, "mPropertyLoader");
static_assert(offsetof(cTSWantTreeLoader, mNode) == 0x44, "mNode");
static_assert(offsetof(cTSWantPropertySetLoader, mParser) == 0x5c, "cTSWantPropertySetLoader::mParser");
static_assert(offsetof(cTSWantPropertySetLoader, mTreeLoader) == 0x60, "cTSWantPropertySetLoader::mTreeLoader");

namespace {

// Pointer cells of the image's strings: element names, attribute names, attribute values.
enum Cell : uint32_t {
    kTree = 0x1015de4, kWantNode = 0x1015de8, kPropertySet = 0x1015dec, kEditorValues = 0x1015df0, kAds = 0x1015df4,
    kBool = 0x1015df8, kRange = 0x1015dfc, kChildren = 0x1015e00, kComment = 0x1015e04,
    // attributes
    kVersion = 0x1015e08, kKey = 0x1015e0c, kWeight = 0x1015e10, kSuperZero = 0x1015e14, kValue = 0x1015e18,
    kMin = 0x1015e1c, kMax = 0x1015e20, kIndex = 0x1015e24,
    // values
    kTrue = 0x1015e28, kFalse = 0x1015e2c,
    // the tree property set's id property
    kIdProperty = 0x1015eb8,
};

const char *Text(uint32_t cell) { return *GameAddress<const char *>(cell); }
bool Is(const char *text, uint32_t cell) { return std::strcmp(text, Text(cell)) == 0; }

struct Mapping {
    uint32_t cell;
    int32_t value;
};
// The <ads> key values: the advertisement factor (age, aspiration, mood, personality, relationship, aspirationScore,
// personData).
constexpr Mapping kFactors[] = {
    {0x1015e30, 0}, {0x1015e34, 1}, {0x1015e38, 2}, {0x1015e3c, 3}, {0x1015e40, 4}, {0x1015e44, 5}, {0x1015e48, 6},
};
// The <bool> key values, in the order the original tests them: life stages, aspirations, the trait poles.
constexpr Mapping kBoolKeys[] = {
    {0x1015e4c, 0xd},  {0x1015e50, 2},    {0x1015e54, 1},    {0x1015e58, 3},    {0x1015e5c, 0},    {0x1015e60, 4},
    {0x1015e64, 2},    {0x1015e68, 1},    {0x1015e6c, 4},    {0x1015e70, 4},    {0x1015e74, 0x20}, {0x1015e78, 0x10},
    {0x1015e7c, 0x10}, {0x1015e80, 0x40}, {0x1015e84, 0x80}, {0x1015e88, 0x100}, {0x1015e8c, 0},   {0x1015e90, 2},
    {0x1015e94, 1},    {0x1015e98, 0xc},  {0x1015e9c, 0xb},  {0x1015ea0, 0xa},  {0x1015ea4, 9},    {0x1015ea8, 4},
    {0x1015eac, 3},    {0x1015eb0, 8},    {0x1015eb4, 7},
};

// The value of the first mapping whose name is `text`; `previous` when none is.
template <size_t N> int32_t Map(const Mapping (&map)[N], const char *text, int32_t previous)
{
    for (const Mapping &m : map)
        if (Is(text, m.cell)) return m.value;
    return previous;
}

constexpr int32_t kNewestVersion = 8; // versions above it and below kOldestVersion are noted once (nothing else)
constexpr int32_t kOldestVersion = 3;
constexpr uint32_t kStreamIid = 0xc56b8f08;
constexpr uint32_t kParserClass = 0x6b6b01d1;
constexpr uint32_t kParserIid = 0x6b6b01cd;

void AddRefObject(void *object) { VirtualCall<uint32_t(__thiscall *)(void *), 1>(object); }
void ReleaseObject(void *object) { VirtualCall<uint32_t(__thiscall *)(void *), 2>(object); }

// A counted pointer field takes `object` (the same object: nothing happens).
void Assign(void *&field, void *object)
{
    void *const old = field;
    if (object == old) return;
    if (object) AddRefObject(object);
    field = object;
    if (old) ReleaseObject(old);
}
// A counted pointer field is cleared, then its object released.
void Drop(void *&field)
{
    if (void *const old = field) {
        field = nullptr;
        ReleaseObject(old);
    }
}

// The nodes are ours (ts/cTSWantNode.h): the loader writes the fields the original writes directly and calls the slots.
cTSWantNode *Fields(void *node) { return static_cast<cTSWantNode *>(static_cast<cITSWantNode *>(node)); }

void *NewNode() { return static_cast<cITSWantNode *>(new cTSWantNode); }
void NodeDetach(void *node) { VirtualCall<void(__thiscall *)(void *), 3>(node); }                 // slot 3 (0x5b3890)
void NodeSetId(void *node, uint32_t id) { VirtualCall<void(__thiscall *)(void *, uint32_t), 4>(node, id); } // +0xa4
void NodeSetParent(void *node, void *parent) { VirtualCall<void(__thiscall *)(void *, void *), 5>(node, parent); }
void NodeAddChild(void *node, void *child) { VirtualCall<void(__thiscall *)(void *, void *), 6>(node, child); }
void NodeRemoveChild(void *node, void *child) { VirtualCall<void(__thiscall *)(void *, void *), 7>(node, child); }
void NodeSetProperties(void *node, void *set) { VirtualCall<void(__thiscall *)(void *, void *), 8>(node, set); }
void *NodeParent(void *node) { return VirtualCall<void *(__thiscall *)(void *), 11>(node); } // +0xa8
// Slot 14 (GetDefinition 0x5b2af0): a node whose definition is not found is dropped (the whole pointer tested, 0x552b3f).
bool NodeKeep(void *node) { return VirtualCall<void *(__thiscall *)(void *), 14>(node) != nullptr; }

// The tree manager's trees by id (cTSWantTreeManager +0x34: std::map<uint32_t, cRZAutoRefCount<cITSWantNode>>; the counted
// pointer is a raw pointer here, counted by hand).
std::map<uint32_t, void *> &Trees(void *manager)
{
    return *reinterpret_cast<std::map<uint32_t, void *> *>(static_cast<uint8_t *>(manager) + 0x34);
}

} // namespace

bool cTSWantTreeLoader::sNewVersionSeen = false;
bool cTSWantTreeLoader::sOldVersionSeen = false;

std::map<std::string, uint32_t> &cTSWantTreeLoader::UnknownStartTags()
{
    static std::map<std::string, uint32_t> tags;
    return tags;
}

std::map<std::string, uint32_t> &cTSWantTreeLoader::UnknownEndTags()
{
    static std::map<std::string, uint32_t> tags;
    return tags;
}

bool cTSWantTreeLoader::QueryInterface(uint32_t iid, void **out)
{
    if (iid != 1 && iid != kIid) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cTSWantTreeLoader::AddRef() { return uint32_t(++mRefCount); }

uint32_t cTSWantTreeLoader::Release()
{
    if (mRefCount > 1) return uint32_t(--mRefCount);
    // The members' releases, last declared first (the fields are not cleared).
    if (mNode) ReleaseObject(mNode);
    if (mProperties) ReleaseObject(mProperties);
    if (mPropertyLoader) ReleaseObject(mPropertyLoader);
    if (mParser) ReleaseObject(mParser);
    delete this;
    return 0;
}

void cTSWantTreeLoader::Reset()
{
    Drop(mParser);
    mManager = nullptr;
    Drop(mPropertyLoader);
    Drop(mProperties);
    Drop(mNode);
}

bool cTSWantTreeLoader::StartElement(const char *name, const char **attributes, uint32_t count, uint32_t a4)
{
    if (mInEditorValues) return true;
    if (Is(name, kEditorValues)) {
        mInEditorValues = true;
        return true;
    }
    if (Is(name, kComment)) return true;
    if (Is(name, kTree)) {
        mVersion = 0;
        for (uint32_t i = 0; i < count; i += 2)
            if (Is(attributes[i], kVersion)) mVersion = std::strtol(attributes[i + 1], nullptr, 0);
        if (mVersion > kNewestVersion) sNewVersionSeen = true;
        else if (mVersion < kOldestVersion) sOldVersionSeen = true;
        return true;
    }
    if (Is(name, kChildren)) return true;
    if (Is(name, kWantNode)) {
        mSeenNode = true;
        void *const node = NewNode();
        AddRefObject(node);
        if (mNode) {
            NodeAddChild(mNode, node);
        } else {
            // A tree's root: kept under the tree id unless the manager already has a tree of that id (then dropped).
            std::map<uint32_t, void *> &trees = Trees(mManager);
            if (trees.find(mTreeId) != trees.end()) {
                ReleaseObject(node);
                return true;
            }
            Assign(trees[mTreeId], node);
        }
        NodeSetParent(node, mNode);
        Assign(mNode, node);
        ReleaseObject(node);
        return true;
    }
    if (Is(name, kPropertySet)) {
        // A property set reader takes over until its end tag (it hands that back: cTSWantPropertySetLoader::EndElement).
        Assign(mPropertyLoader, static_cast<cIGZPropertySetSerializerString *>(new cTSWantPropertySetLoader));
        Assign(mProperties, static_cast<cIGZPropertySetString *>(new cGZPropertySetString));
        auto *const loader = static_cast<cTSWantPropertySetLoader *>(
            static_cast<cIGZPropertySetSerializerString *>(mPropertyLoader));
        loader->mTarget = static_cast<cGZPropertySetString *>(static_cast<cIGZPropertySetString *>(mProperties));
        Assign(loader->mParser, mParser);
        loader->mTreeLoader = this;
        cIGZExpatContentHandler *const face = loader;
        VirtualCall<void(__thiscall *)(void *, const char *), 14>(mParser, name);
        VirtualCall<void(__thiscall *)(void *, cIGZExpatContentHandler *), 8>(mParser, face);
        face->StartElement(name, attributes, count, a4);
        return true;
    }
    if (Is(name, kAds)) {
        mFactor = kNoFactor;
        mWeight = 0;
        mSuperZero = false;
        mBoolKey = 0;
        mBoolValue = false;
        mRangeMax = 0;
        mRangeMin = 0;
        mRangeIndex = 0;
        for (uint32_t i = 0; i < count; i += 2) {
            const char *const attribute = attributes[i];
            const char *const value = attributes[i + 1];
            if (Is(attribute, kKey)) {
                mFactor = uint32_t(Map(kFactors, value, int32_t(mFactor)));
            } else if (Is(attribute, kWeight)) {
                mWeight = std::strtol(value, nullptr, 0);
            } else if (Is(attribute, kSuperZero)) {
                if (Is(value, kTrue)) mSuperZero = true;
                else if (Is(value, kFalse)) mSuperZero = false;
            }
        }
        if (mFactor != kNoFactor && mNode) {
            Fields(mNode)->mWeights[mFactor] = mWeight;
            Fields(mNode)->mSuperZero[mFactor] = mSuperZero;
        }
        return true;
    }
    if (Is(name, kBool)) {
        for (uint32_t i = 0; i < count; i += 2) {
            const char *const attribute = attributes[i];
            const char *const value = attributes[i + 1];
            if (Is(attribute, kValue)) {
                if (Is(value, kTrue)) mBoolValue = true;
                else if (Is(value, kFalse)) mBoolValue = false;
            }
            if (Is(attribute, kKey)) mBoolKey = Map(kBoolKeys, value, mBoolKey);
        }
        return true;
    }
    if (Is(name, kRange)) {
        for (uint32_t i = 0; i < count; i += 2) {
            const char *const attribute = attributes[i];
            const char *const value = attributes[i + 1];
            if (Is(attribute, kMin)) mRangeMin = std::strtol(value, nullptr, 0);
            else if (Is(attribute, kMax)) mRangeMax = std::strtol(value, nullptr, 0);
            else if (Is(attribute, kIndex)) mRangeIndex = std::strtol(value, nullptr, 0);
        }
        return true;
    }
    UnknownStartTags()[name] = mTreeId;
    return true;
}

bool cTSWantTreeLoader::EndElement(const char *name)
{
    if (Is(name, kEditorValues)) {
        mInEditorValues = false;
    } else if (mInEditorValues) {
        return true;
    } else if (Is(name, kComment) || Is(name, kChildren) || Is(name, kAds)) {
    } else if (Is(name, kTree)) {
        mSeenNode = false;
    } else if (Is(name, kWantNode)) {
        if (mNode) Assign(mNode, NodeParent(mNode));
    } else if (Is(name, kPropertySet)) {
        if (mNode) {
            NodeSetId(mNode, mTreeId);
            if (mProperties) NodeSetProperties(mNode, mProperties);
            if (!NodeKeep(mNode)) {
                // The node is dropped: from its parent, or (a root) from the manager's trees if it is the tree there.
                if (void *const parent = NodeParent(mNode)) {
                    NodeRemoveChild(parent, mNode);
                } else {
                    std::map<uint32_t, void *> &trees = Trees(mManager);
                    const auto it = trees.find(mTreeId);
                    if (it != trees.end() && it->second == mNode) {
                        auto entry = trees.extract(it);
                        if (entry.mapped()) ReleaseObject(entry.mapped());
                    }
                }
                NodeDetach(mNode);
                Drop(mNode);
            }
        } else if (!mSeenNode && mProperties) {
            // The tree's own property set: its id.
            const GZServiceString key(Text(kIdProperty));
            gz::AnyUint32 id(0);
            if (static_cast<cIGZPropertySetString *>(mProperties)->GetPropertyValue(&key, &id)) mTreeId = id.mValue;
        }
    } else if (Is(name, kBool)) {
        if (mNode) {
            switch (mFactor) {
            case 0: Fields(mNode)->mAgeFlags[mBoolKey] = mBoolValue; break;
            case 1: Fields(mNode)->mAspirationFlags[mBoolKey] = mBoolValue; break;
            case 3: Fields(mNode)->mPersonalityFlags[mBoolKey] = mBoolValue; break;
            default: break;
            }
        }
    } else if (Is(name, kRange)) {
        if (mNode) {
            cTSWantNode *const node = Fields(mNode);
            switch (mFactor) {
            case 2: node->mMoodRange[0] = mRangeMin, node->mMoodRange[1] = mRangeMax; break;
            case 4: node->mRelationshipRange[0] = mRangeMin, node->mRelationshipRange[1] = mRangeMax; break;
            case 5: node->mAspirationScoreRange[0] = mRangeMin, node->mAspirationScoreRange[1] = mRangeMax; break;
            case 6:
                node->mPersonDataRange[0] = mRangeMin, node->mPersonDataRange[1] = mRangeMax;
                node->mPersonDataIndex = mRangeIndex;
                break;
            default: break;
            }
        }
    } else {
        UnknownEndTags()[name] = mTreeId;
    }
    // Any end tag ends a property set read.
    Drop(mPropertyLoader);
    Drop(mProperties);
    return true;
}

void cTSWantTreeLoader::LoadTree(const void *key, void *manager)
{
    void *const resources = OriginalFunction<void *(__cdecl *)(), 0x20deb5>()();
    void *record = nullptr;
    if (VirtualCall<bool(__thiscall *)(void *, const void *, void **, uint32_t), 39>(resources, key, &record, 0)) {
        void *stream = nullptr;
        if (VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(record, kStreamIid, &stream)) {
            const uint32_t size = VirtualCall<uint32_t(__thiscall *)(void *), 52>(stream);
            auto *const buffer = new uint8_t[size];
            VirtualCall<bool(__thiscall *)(void *, void *, uint32_t), 27>(stream, buffer, size);
            Drop(stream);

            void *parser = nullptr;
            void *const framework = GetDirectorFrameworkObject();
            if (VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t, void **), 3>(framework, kParserClass, kParserIid,
                                                                                        &parser)) {
                auto *const loader = new cTSWantTreeLoader;
                loader->AddRef();
                loader->mManager = manager;
                Assign(loader->mParser, parser);
                VirtualCall<void(__thiscall *)(void *, cIGZExpatContentHandler *), 8>(parser, loader);
                VirtualCall<bool(__thiscall *)(void *, const void *, uint32_t, uint32_t), 3>(parser, buffer, size, 1);
                loader->Reset();
                loader->Release();
            }
            delete[] buffer;
            Drop(parser);
        }
        VirtualCall<void(__thiscall *)(void *), 6>(record);
        if (stream) ReleaseObject(stream);
    }
    if (record) ReleaseObject(record);
}

cTSWantPropertySetLoader::~cTSWantPropertySetLoader() { Drop(mParser); }

bool cTSWantPropertySetLoader::EndElement(const char *name)
{
    const bool result = cGZPropertySetSerializerXmlString::EndElement(name);
    if (Is(name, kPropertySet)) {
        VirtualCall<void(__thiscall *)(void *), 15>(mParser);
        mTreeLoader->EndElement(name);
    }
    return result;
}

cTSWantTreeLoader *__fastcall cTSWantTreeLoaderAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cTSWantTreeLoader;
}

void __fastcall cTSWantTreeLoaderAdapters::Reset(Self *self, void *) { self->Reset(); }

uint32_t __fastcall cTSWantTreeLoaderAdapters::Release(Self *self, void *) { return self->cTSWantTreeLoader::Release(); }

bool __fastcall cTSWantTreeLoaderAdapters::StartElement(Self *self, void *, const char *name, const char **attributes,
                                                        uint32_t count, uint32_t a4)
{
    return self->cTSWantTreeLoader::StartElement(name, attributes, count, a4);
}

bool __fastcall cTSWantTreeLoaderAdapters::EndElement(Self *self, void *, const char *name)
{
    return self->cTSWantTreeLoader::EndElement(name);
}
