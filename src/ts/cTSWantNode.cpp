// cTSWantNode (lane IL): see the header. Property names are the image's strings, read through their pointer cells.
#include "ts/cTSWantNode.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"
#include "gz/cGZPropertySet.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/cRZString.h"
#include "ts/cTSWantDefinition.h"

#include <cstddef>
#include <new>

static_assert(offsetof(cTSWantNode, mRefCount) == 0x04, "mRefCount");
static_assert(offsetof(cTSWantNode, mNodeId) == 0x18, "mNodeId");
static_assert(offsetof(cTSWantNode, mWantId) == 0x1c, "mWantId");
static_assert(offsetof(cTSWantNode, mVisible) == 0x2c, "mVisible");
static_assert(offsetof(cTSWantNode, mRegeneration) == 0x2e, "mRegeneration");
static_assert(offsetof(cTSWantNode, mLifetime) == 0x30, "mLifetime");
static_assert(offsetof(cTSWantNode, mScoreOverride) == 0x38, "mScoreOverride");
static_assert(offsetof(cTSWantNode, mWeights) == 0x3c, "mWeights");
static_assert(offsetof(cTSWantNode, mSuperZero) == 0x58, "mSuperZero");
static_assert(offsetof(cTSWantNode, mMinScore) == 0x60, "mMinScore");
static_assert(offsetof(cTSWantNode, mAgeFlags) == 0x68, "mAgeFlags");
static_assert(offsetof(cTSWantNode, mPersonalityFlags) == 0x78, "mPersonalityFlags");
static_assert(offsetof(cTSWantNode, mMoodRange) == 0x80, "mMoodRange");
static_assert(offsetof(cTSWantNode, mPersonDataRange) == 0x98, "mPersonDataRange");
static_assert(offsetof(cTSWantNode, mId) == 0xa4, "mId");
static_assert(offsetof(cTSWantNode, mParent) == 0xa8, "mParent");
static_assert(offsetof(cTSWantNode, mChildren) == 0xac, "mChildren");
static_assert(offsetof(cTSWantNode, mDepth) == 0xb8, "mDepth");

namespace {

// Pointer cells of the property names SetProperties reads.
enum Cell : uint32_t {
    kNodeIdName = 0x1017308, kWantId = 0x101730c, kSubjectType = 0x1017310, kVisible = 0x1017314,
    kInheritSubject = 0x1017318, kLifetime = 0x101731c, kAdFrequency = 0x1017320, kRegeneration = 0x1017324,
    kObjectField = 0x1017328, kIntegerField = 0x101732c, kScoreOverride = 0x1017330, kMinScore = 0x1017334,
    kMaxScore = 0x1017338, kRelationshipMin = 0x101733c, kRelationshipMax = 0x1017340,
};
constexpr int32_t kSecondsPerHour = 3600;
constexpr uint32_t kSubjectTypes = 6;       // a subjectType of 6 or more is ignored
constexpr uint32_t kObjectGuidField = 0x2f; // the data field the object-subject test compares

const char *Text(uint32_t cell) { return *GameAddress<const char *>(cell); }

bool Get(cIGZPropertySetString *set, uint32_t cell, void *value)
{
    const GZServiceString key(Text(cell));
    return set->GetPropertyValue(&key, value);
}

// The want instance a node is matched against (slot 37): slot 13 its want id, slot 14 its subject.
struct WantSubject {
    int32_t type;    // +0 (2 an object, 3 compared by id)
    int32_t id;      // +4 (subject type 4 compares its low 16 bits)
    int16_t integer; // +8 the value of the integer test
};
uint32_t InstanceWantId(void *instance) { return VirtualCall<uint32_t(__thiscall *)(void *), 13>(instance); }
const WantSubject *Subject(void *instance) { return VirtualCall<const WantSubject *(__thiscall *)(void *), 14>(instance); }

bool Lookup(const std::map<int32_t, bool> &flags, int32_t key)
{
    const auto it = flags.find(key);
    return it != flags.end() && it->second;
}

bool Range(const int32_t (&range)[2], int32_t *min, int32_t *max)
{
    *min = range[0];
    *max = range[1];
    return *min != 0 || *max != 0;
}

} // namespace

WantNodeRef::WantNodeRef(cITSWantNode *node) : mNode(node)
{
    if (mNode) mNode->AddRef();
}

WantNodeRef::WantNodeRef(const WantNodeRef &other) : mNode(other.mNode)
{
    if (mNode) mNode->AddRef();
}

WantNodeRef &WantNodeRef::operator=(const WantNodeRef &other)
{
    if (this != &other && mNode != other.mNode) {
        cITSWantNode *const old = mNode;
        if (other.mNode) other.mNode->AddRef();
        mNode = other.mNode;
        if (old) old->Release();
    }
    return *this;
}

WantNodeRef::~WantNodeRef()
{
    if (mNode) mNode->Release();
}

bool __stdcall WantCompareIntegers(uint32_t op, int32_t value, int32_t operand)
{
    switch (op) {
    case 1: return value == operand;
    case 2: return value < operand;
    case 3: return value > operand;
    case 4: return value <= operand;
    case 5: return value >= operand;
    default: return true;
    }
}

cTSWantNode::cTSWantNode() = default;

cTSWantNode::~cTSWantNode()
{
    // The children first (released first to last, then the block freed), then the parent; the flag maps go with the members
    // (personality, aspiration, age).
    WantNodeList().swap(mChildren);
    if (mParent) mParent->Release();
}

bool cTSWantNode::QueryInterface(uint32_t iid, void **out)
{
    if (iid != 1 && iid != kIid) return false;
    *out = static_cast<cITSWantNode *>(this);
    AddRef();
    return true;
}

uint32_t cTSWantNode::AddRef() { return uint32_t(++mRefCount); }

uint32_t cTSWantNode::Release()
{
    if (mRefCount > 1) return uint32_t(--mRefCount);
    delete this;
    return 0;
}

void cTSWantNode::Detach()
{
    if (cITSWantNode *const parent = mParent) {
        mParent = nullptr;
        parent->Release();
    }
    for (auto it = mChildren.begin(); it != mChildren.end(); ++it) it->Get()->Detach();
    mChildren.clear();
}

void cTSWantNode::SetId(uint32_t id) { mId = id; }

void cTSWantNode::SetParent(cITSWantNode *parent)
{
    if (parent != mParent) {
        cITSWantNode *const old = mParent;
        if (parent) parent->AddRef();
        mParent = parent;
        if (old) old->Release();
    }
    mDepth = parent ? parent->GetDepth() + 1 : 0;
}

void cTSWantNode::AddChild(cITSWantNode *child)
{
    const WantNodeRef held(child);
    mChildren.push_back(held);
}

void cTSWantNode::RemoveChild(cITSWantNode *child)
{
    for (auto it = mChildren.begin(); it != mChildren.end(); ++it)
        if (it->Get() == child) {
            mChildren.erase(it);
            return;
        }
}

void cTSWantNode::SetProperties(cIGZPropertySetString *set)
{
    // One value local per type, reused from property to property (a failed lookup stores nothing).
    gz::AnyUint32 number(0);
    AnySint32 signedNumber{GameAddress<void>(gzany::kSint32VTable), 0};
    gz::AnyBoolean flag(0);

    if (Get(set, kNodeIdName, &number)) mNodeId = number.mValue;
    if (Get(set, kWantId, &number)) {
        mWantId = number.mValue;
        GetDefinition(); // asked and dropped
    }
    if (!Get(set, kSubjectType, &number)) {
        cITSWantDefinition *const definition = GetDefinition();
        mSubjectType = definition ? definition->GetObjectType() : 0;
    } else if (number.mValue < kSubjectTypes) {
        mSubjectType = number.mValue;
    } else if (cITSWantDefinition *const definition = GetDefinition()) {
        // An out-of-range subject type is ignored; the original still copies the definition's name into a local (an
        // unnamed definition's name is made by that, as GetName does).
        alignas(4) uint8_t bytes[sizeof(cRZString)];
        auto *const name = reinterpret_cast<cRZString *>(bytes);
        name->Construct();
        name->Assign(*definition->GetName()->AsInterface());
        name->Destruct();
    }
    if (Get(set, kVisible, &flag)) mVisible = flag.mValue;
    if (Get(set, kInheritSubject, &flag)) mInheritSubject = flag.mValue;
    {
        // regeneration: a boolean, or a uint32 (each asked for its type twice, as the original's inlined casts do).
        const GZServiceString key(Text(kRegeneration));
        if (void *const value = set->GetProperty(&key)) {
            const auto type = [value] { return VirtualCall<uint32_t(__thiscall *)(void *), 4>(value); };
            const uint32_t first = type();
            if (first == gzany::kTypeBoolean) {
                if (type() == gzany::kTypeBoolean) flag.mValue = static_cast<const uint8_t *>(value)[4];
                mRegeneration = flag.mValue;
            } else if (first == gzany::kTypeUint32) {
                if (type() == gzany::kTypeUint32) number.mValue = static_cast<const uint32_t *>(value)[1];
                // ORIGINAL-BUG: a uint32 regeneration value counts as "on" when its sign bit is clear (0 included).
                mRegeneration = int32_t(number.mValue) >= 0;
            }
        }
    }
    if (Get(set, kLifetime, &signedNumber)) mLifetime = signedNumber.mValue * kSecondsPerHour;
    if (Get(set, kAdFrequency, &signedNumber)) {
        mAdFrequency = signedNumber.mValue * kSecondsPerHour;
    } else if (mRegeneration && mLifetime > 0) {
        // A regenerating want without an ad frequency comes back every lifetime, and never expires.
        mAdFrequency = mLifetime;
        mLifetime = 0;
    }
    if (Get(set, kScoreOverride, &signedNumber)) mScoreOverride = signedNumber.mValue;
    if (Get(set, kObjectField, &number)) mObjectField = number.mValue;
    if (Get(set, kIntegerField, &number)) mIntegerField = number.mValue;
    if (Get(set, kMinScore, &signedNumber)) mMinScore = signedNumber.mValue;
    if (Get(set, kMaxScore, &signedNumber)) mMaxScore = signedNumber.mValue;
    if (Get(set, kRelationshipMin, &signedNumber)) mRelationshipRange[0] = signedNumber.mValue;
    if (Get(set, kRelationshipMax, &signedNumber)) mRelationshipRange[1] = signedNumber.mValue;
}

uint32_t cTSWantNode::GetNodeId() { return mNodeId; }
uint32_t cTSWantNode::GetId() { return mId; }
cITSWantNode *cTSWantNode::GetParent() { return mParent; }
WantNodeList *cTSWantNode::GetChildren() { return &mChildren; }
int32_t cTSWantNode::GetDepth() { return mDepth; }

cITSWantDefinition *cTSWantNode::GetDefinition()
{
    void *const app = GetCachedSingleton();
    if (!app) return nullptr;
    void *const wants = VirtualCall<void *(__thiscall *)(void *), 4>(app);
    if (!wants) return nullptr;
    return VirtualCall<cITSWantDefinition *(__thiscall *)(void *, uint32_t), 23>(wants, mWantId);
}

uint32_t cTSWantNode::GetSubjectType() { return mSubjectType; }
uint32_t cTSWantNode::GetObjectField() { return mObjectField; }
uint32_t cTSWantNode::GetIntegerField() { return mIntegerField; }
uint8_t cTSWantNode::IsVisible() { return mVisible; }
uint8_t cTSWantNode::InheritsSubject() { return mInheritSubject; }
uint8_t cTSWantNode::Regenerates() { return mRegeneration; }
int32_t cTSWantNode::GetLifetime() { return mLifetime; }
int32_t cTSWantNode::GetAdFrequency() { return mAdFrequency; }

int32_t cTSWantNode::GetScore()
{
    if (mScoreOverride != kNoScoreOverride) return mScoreOverride;
    cITSWantDefinition *const definition = GetDefinition();
    return definition ? definition->GetScore() : 0;
}

int32_t cTSWantNode::GetInfluence()
{
    cITSWantDefinition *const definition = GetDefinition();
    return definition ? definition->GetInfluence() : 0;
}

void cTSWantNode::GetScoreRange(int32_t *min, int32_t *max)
{
    *min = mMinScore;
    *max = mMaxScore;
}

bool cTSWantNode::HasFactor(int32_t factor) { return mWeights[factor] >= 1 || mSuperZero[factor] != 0; }
uint8_t cTSWantNode::IsSuperZero(int32_t factor) { return mSuperZero[factor]; }
int32_t cTSWantNode::GetWeight(int32_t factor) { return mWeights[factor]; }

bool cTSWantNode::GetAgeFlag(int32_t age)
{
    switch (age) {
    case 0: case 1: case 2: case 3: case 4: case 0xd: return Lookup(mAgeFlags, age);
    default: return false;
    }
}

bool cTSWantNode::GetAspirationFlag(int32_t aspiration)
{
    switch (aspiration) {
    case 1: case 2: case 4: case 0x10: case 0x20: case 0x40: case 0x80: case 0x100:
        return Lookup(mAspirationFlags, aspiration);
    default: return false;
    }
}

bool cTSWantNode::GetPersonalityFlag(int32_t trait)
{
    switch (trait) {
    case 1: case 2: case 3: case 4: case 7: case 8: case 9: case 10: case 11: case 12:
        return Lookup(mPersonalityFlags, trait);
    default: return false;
    }
}

bool cTSWantNode::GetPersonDataRange(int32_t *min, int32_t *max) { return Range(mPersonDataRange, min, max); }
bool cTSWantNode::GetAspirationScoreRange(int32_t *min, int32_t *max) { return Range(mAspirationScoreRange, min, max); }
bool cTSWantNode::GetMoodRange(int32_t *min, int32_t *max) { return Range(mMoodRange, min, max); }
bool cTSWantNode::GetRelationshipRange(int32_t *min, int32_t *max) { return Range(mRelationshipRange, min, max); }
int32_t cTSWantNode::GetPersonDataIndex() { return mPersonDataIndex; }

bool cTSWantNode::Matches(void *instance)
{
    if (mWantId != InstanceWantId(instance)) return false;
    cITSWantDefinition *const definition = GetDefinition();
    // The integer test: the subject's integer against integerField by the definition's operator (the subject is asked
    // first). ORIGINAL-BUG: without a definition this calls through null, as the original does.
    const auto integerTest = [&] {
        const int32_t value = Subject(instance)->integer;
        return WantCompareIntegers(definition->GetIntegerOperator(), value, int32_t(mIntegerField));
    };
    const int32_t objectField = int32_t(mObjectField);
    switch (mSubjectType) {
    case 0:
    case 1: return integerTest();
    case 2:
        if (objectField != 0 && Subject(instance)->id != 0 && Subject(instance)->id != objectField) return false;
        return integerTest();
    case 3:
        if (objectField != 0) {
            const int32_t type = Subject(instance)->type;
            if (type == 2) {
                // An object subject: data field 0x2f of the object (of the one it stands for, if any) must be objectField.
                void *const objects = VirtualCall<void *(__thiscall *)(void *), 24>(GetCachedSingleton());
                void *object = VirtualCall<void *(__thiscall *)(void *, int32_t), 14>(objects, Subject(instance)->id);
                if (!object) return false;
                if (VirtualCall<void *(__thiscall *)(void *), 27>(object))
                    object = VirtualCall<void *(__thiscall *)(void *), 27>(object);
                void *const data = VirtualCall<void *(__thiscall *)(void *), 4>(object);
                if (VirtualCall<int32_t(__thiscall *)(void *, uint32_t), 3>(data, kObjectGuidField) != objectField)
                    return false;
            } else if (type == 3) {
                return Subject(instance)->id == objectField;
            }
        }
        return integerTest();
    case 4:
    case 5:
    case 6: {
        // Subject id (type 4: its low 16 bits) against objectField, then the integer itself against integerField.
        bool match = true;
        if (objectField != 0) {
            const int32_t id = Subject(instance)->id;
            match = (mSubjectType == 4 ? int32_t(int16_t(id)) : id) == objectField;
        }
        if (mIntegerField == 0) return match;
        return match && int32_t(Subject(instance)->integer) == int32_t(mIntegerField);
    }
    default: return false;
    }
}

cTSWantNode *__fastcall cTSWantNodeAdapters::Construct(void *memory, void *) { return ::new (memory) cTSWantNode; }
