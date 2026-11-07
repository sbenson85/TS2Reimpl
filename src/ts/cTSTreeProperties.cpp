#include "ts/cTSTreeProperties.h"
#include "game/game.h"
#include "game/vtable.h"
#include "rz/StaticGetters.h"
#include "misc/UtilitiesC.h"
#include "simantics/PrimS.h"
#include "rz/ServiceGetters.h"
#include "ifaces/cTSTreeProperties.h"
#include "ifaces/TreePropertyNameTable.h"
#include <cstring>
#include <new>

namespace {
constexpr uint32_t kClassId = 0x54505250; // TPRP
constexpr uint32_t kTreePropertiesInterface = 0x1b36c800;
constexpr uint32_t kSerializableInterface = 0x1b36c827;
constexpr uint32_t kPersistResourceInterface = 0x456b8f1d;
constexpr uint32_t kNameTableClass = 0xdb4cb0f9;
constexpr uint32_t kNameTableInterface = 0xfb4cb0de;
constexpr uint32_t kLegacyStringReaderInterface = 0x3b2271c0;
constexpr uint32_t kCurrentVersion = 0x55;
constexpr uint32_t kModernStringVersion = 0x3f;
constexpr int32_t kParameterScope = 0;
constexpr int32_t kLocalScope = 1;
constexpr int32_t kMissingResource = -98;
template<class R, class... A> R Call(void *object, unsigned slot, A... args)
{
    return VirtualSlot<R(__thiscall *)(void *, A...)>(object, slot)(object, args...);
}
void ReleaseObject(void *object) { if (object) As<cIGZUnknown>(object)->Release(); }
// Table slot 10 sets a one-based name, with language index zero.
void SetTableName(void *table, cRZString *text, int32_t index) { As<ifaces::TreePropertyNameTable>(table)->SetName(text, index, 0); }
void AttachVersion(void *stream, uint32_t version)
{
    auto *metadata = static_cast<UtilObjectB *>(::operator new(sizeof(UtilObjectB)));
    if (metadata) metadata->Construct(version);
    Call<void>(stream, 19, metadata);
}
struct TemporaryString {
    cRZString value;
    TemporaryString() {
        // The default member constructor owns the std::string lifetime. The game
        // also clears the unused SSO characters, which ordinary construction leaves alone.
        std::memset(value.mString.data(), 0, 16);
        value.mVTable = GameAddress(cRZString::kVTable);
        value.mRefCount = 0;
    }
    explicit TemporaryString(cIGZString *source) {
        const uint32_t length = source->Strlen();
        const char *data = source->Data();
        std::memset(value.mString.data(), 0, 16);
        value.mString.assign(data, length);
        value.mVTable = GameAddress(cRZString::kVTable);
        value.mRefCount = 0;
    }
    ~TemporaryString() { value.mVTable = GameAddress(cRZString::kVTable); }
};
struct Reference {
    void *value = nullptr;
    ~Reference() noexcept { ReleaseObject(value); }
};
}

cTSTreeProperties *cTSTreeProperties::Construct(void *owner, uint16_t tree)
{
    mVTable = GameAddress(0xdc3b90);
    mPersistVTable = GameAddress(0xdc3c1c);
    mRefCount = 0;
    for (auto &field : mPersistFields) field = 0;
    mOwner = mLocalNames = mParameterNames = nullptr;
    // ORIGINAL-BUG: the serialized flag at +0x23 is left untouched, so a fresh
    // object's saved flag depends on the bytes already in its allocation.
    std::memset(&mDescription.mString, 0, sizeof(mDescription.mString));
    mDescription.Construct();
    new (&mParameterFlags) TreeParameterFlags;
    try { Initialize(owner, tree); }
    catch (...) {
        mParameterFlags.~TreeParameterFlags();
        mDescription.Destruct();
        ReleaseObject(mParameterNames);
        ReleaseObject(mLocalNames);
        ReleaseObject(mOwner);
        throw;
    }
    return this;
}

void cTSTreeProperties::CopyFrom(void *other)
{
    Reference queried;
    bool copied = false;
    if (As<cIGZUnknown>(other)->QueryInterface(kTreePropertiesInterface, &queried.value)) {
        auto *source = static_cast<cTSTreeProperties *>(queried.value);
        As<ifaces::TreePropertyNameTable>(mLocalNames)->CopyFrom(source->mLocalNames);
        As<ifaces::TreePropertyNameTable>(mParameterNames)->CopyFrom(source->mParameterNames);
        Call<void>(As<ifaces::TreePropertyNameTable>(mLocalNames)->GetResource(), 9, 0xffffffffu);
        Call<void>(As<ifaces::TreePropertyNameTable>(mParameterNames)->GetResource(), 9, 0xffffffffu);
        reinterpret_cast<SString *>(&mDescription)->AssignText(reinterpret_cast<SString *>(&source->mDescription));
        if (&mParameterFlags != &source->mParameterFlags)
            mParameterFlags.assign(source->mParameterFlags.begin(), source->mParameterFlags.end());
        mFlag23 = source->mFlag23;
        mField24 = source->mField24;
        mField5C = source->mField5C;
        copied = true;
    }
    mModified = copied;
}

bool cTSTreeProperties::Equals(void *other)
{
    Reference queried;
    if (!As<cIGZUnknown>(other)->QueryInterface(kTreePropertiesInterface, &queried.value)) return false;
    auto *source = static_cast<cTSTreeProperties *>(queried.value);
    if (mFlag23 != source->mFlag23 || mField24 != source->mField24 ||
        mDescription.mString != source->mDescription.mString || mParameterFlags != source->mParameterFlags)
        return false;
    const int32_t localCount = As<ifaces::TreePropertyNameTable>(mLocalNames)->Count(0);
    if (localCount != As<ifaces::TreePropertyNameTable>(source->mLocalNames)->Count(0)) return false;
    const int32_t parameterCount = As<ifaces::TreePropertyNameTable>(source->mParameterNames)->Count(0);
    if (As<ifaces::TreePropertyNameTable>(mParameterNames)->Count(0) != parameterCount) return false;
    TemporaryString left, right;
    for (int32_t i = 1; i <= As<ifaces::TreePropertyNameTable>(mLocalNames)->Count(0); ++i) {
        As<ifaces::TreePropertyNameTable>(mLocalNames)->GetName(&left.value, i, 0);
        As<ifaces::TreePropertyNameTable>(source->mLocalNames)->GetName(&right.value, i, 0);
        if (left.value.mString != right.value.mString) return false;
    }
    for (int32_t i = 1; i <= As<ifaces::TreePropertyNameTable>(mParameterNames)->Count(0); ++i) {
        As<ifaces::TreePropertyNameTable>(mParameterNames)->GetName(&left.value, i, 0);
        As<ifaces::TreePropertyNameTable>(source->mParameterNames)->GetName(&right.value, i, 0);
        if (left.value.mString != right.value.mString) return false;
    }
    return true;
}

void cTSTreeProperties::LoadFromService()
{
    if (!As<ifaces::cTSTreeProperties>(this)->GetTree()) return;
    void *service = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    const uint16_t tree = As<ifaces::cTSTreeProperties>(this)->GetTreeId();
    void *resource = As<ifaces::cTSTreeProperties>(this)->GetTree();
    const int32_t result = Call<int32_t>(service, 73, this, resource, uint32_t(tree), 0);
    mModified = mFlag23 = 0;
    if (result == kMissingResource) As<ifaces::cTSTreeProperties>(this)->InitializeParameters();
}
void cTSTreeProperties::SaveToService()
{
    if (!As<ifaces::cTSTreeProperties>(this)->GetTree()) return;
    void *service = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    // The tree id is passed unsigned to storage, signed in the resource key.
    const uint16_t tree = As<ifaces::cTSTreeProperties>(this)->GetTreeId();
    void *resource = As<ifaces::cTSTreeProperties>(this)->GetTree();
    if (Call<int32_t>(service, 72, this, resource, uint32_t(tree)) == 0) {
        resource = As<ifaces::cTSTreeProperties>(this)->GetTree();
        const int32_t signedTree = int16_t(As<ifaces::cTSTreeProperties>(this)->GetTreeId());
        const uint32_t group = Call<uint32_t>(resource, 11);
        const uint32_t type = As<ifaces::cTSTreeProperties>(this)->GetGZCLSID();
        const uint32_t key[4] = {type, group, uint32_t(signedTree), 0};
        Call<void>(Call<void *>(service, 3), 10, key);
    }
    mModified = 0;
}

uint8_t cTSTreeProperties::Initialize(void *owner, uint16_t tree)
{
    if (owner != mOwner) {
        void *old = mOwner;
        if (owner) As<cIGZUnknown>(owner)->AddRef();
        mOwner = owner;
        ReleaseObject(old);
    }
    mTreeId = tree;
    mModified = 0;
    mField5C = 5;
    mField24 = 0;
    void *framework = GetDirectorFrameworkObject();
    if (mLocalNames) { void *old = mLocalNames; mLocalNames = nullptr; ReleaseObject(old); }
    Call<uint32_t>(framework, 3, kNameTableClass, kNameTableInterface, &mLocalNames);
    framework = GetDirectorFrameworkObject();
    if (mParameterNames) { void *old = mParameterNames; mParameterNames = nullptr; ReleaseObject(old); }
    Call<uint32_t>(framework, 3, kNameTableClass, kNameTableInterface, &mParameterNames);
    // ORIGINAL-BUG: factory failures leave null tables which are immediately dereferenced.
    Call<void>(As<ifaces::TreePropertyNameTable>(mLocalNames)->GetResource(), 9, 0xffffffffu);
    Call<void>(As<ifaces::TreePropertyNameTable>(mParameterNames)->GetResource(), 9, 0xffffffffu);
    return 1;
}

void cTSTreeProperties::Destruct() noexcept
{
    mVTable = GameAddress(0xdc3b90);
    mPersistVTable = GameAddress(0xdc3c1c);
    mParameterFlags.~TreeParameterFlags();
    mDescription.Destruct();
    ReleaseObject(mParameterNames);
    ReleaseObject(mLocalNames);
    ReleaseObject(mOwner);
}
cTSTreeProperties *cTSTreeProperties::DeletingDestructor(uint8_t flags) noexcept
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(*this));
    return this;
}
uint8_t cTSTreeProperties::QueryInterface(uint32_t id, void **out)
{
    if (id == 1 || id == kTreePropertiesInterface || id == kSerializableInterface) *out = this;
    else if (id == kPersistResourceInterface) *out = &mPersistVTable;
    else return 0;
    As<ifaces::cTSTreeProperties>(this)->AddRef();
    return 1;
}
uint32_t cTSTreeProperties::Release()
{
    if (mRefCount < 2) { As<ifaces::cTSTreeProperties>(this)->DeletingDestructor(1); return 0; }
    return --mRefCount;
}
uint32_t cTSTreeProperties::GetGZCLSID() { return kClassId; }

bool cTSTreeProperties::Write(void *stream)
{
    AttachVersion(stream, kCurrentVersion);
    Call<bool>(stream, 9, As<ifaces::cTSTreeProperties>(this)->GetGZCLSID());
    Call<bool>(stream, 9, kCurrentVersion);
    Call<bool>(stream, 9, 0u);
    const int32_t parameters = As<ifaces::TreePropertyNameTable>(mParameterNames)->Count(0);
    const int32_t locals = As<ifaces::TreePropertyNameTable>(mLocalNames)->Count(0);
    Call<bool>(stream, 8, parameters);
    Call<bool>(stream, 8, locals);
    TemporaryString name;
    for (int32_t i = 1; i <= parameters; ++i) {
        As<ifaces::TreePropertyNameTable>(mParameterNames)->GetName(&name.value, i, 0);
        Call<bool>(stream, 15, &name.value);
    }
    for (int32_t i = 1; i <= locals; ++i) {
        As<ifaces::TreePropertyNameTable>(mLocalNames)->GetName(&name.value, i, 0);
        Call<bool>(stream, 15, &name.value);
    }
    Call<bool>(stream, 9, uint32_t(mFlag23 != 0));
    for (int32_t i = 0; i < parameters; ++i) Call<bool>(stream, 5, uint32_t(mParameterFlags[i]));
    Call<bool>(stream, 9, mField5C);
    Call<bool>(stream, 9, mField24);
    return Call<uint8_t>(stream, 18) == 0;
}
bool cTSTreeProperties::Read(void *stream)
{
    uint32_t discarded, version;
    int32_t parameters, locals;
    Call<bool>(stream, 9, &discarded);
    Call<bool>(stream, 9, &version);
    Call<bool>(stream, 9, &discarded);
    AttachVersion(stream, version);
    Call<bool>(stream, 8, &parameters);
    Call<bool>(stream, 8, &locals);
    TemporaryString name;
    if (version < kModernStringVersion) {
        Reference legacy;
        if (As<cIGZUnknown>(stream)->QueryInterface(kLegacyStringReaderInterface, &legacy.value)) {
            for (int32_t i = 1; i <= parameters; ++i) {
                Call<void>(legacy.value, 20, &name.value);
                SetTableName(mParameterNames, &name.value, i);
            }
            for (int32_t i = 1; i <= locals; ++i) {
                Call<void>(legacy.value, 20, &name.value);
                SetTableName(mLocalNames, &name.value, i);
            }
        }
    } else {
        for (int32_t i = 1; i <= parameters; ++i) {
            Call<bool>(stream, 15, &name.value);
            SetTableName(mParameterNames, &name.value, i);
        }
        for (int32_t i = 1; i <= locals; ++i) {
            Call<bool>(stream, 15, &name.value);
            SetTableName(mLocalNames, &name.value, i);
        }
    }
    if (version > 0) {
        // This getter reuses the stream argument / name-loop counter stack slot.
        uint32_t flag = version < kModernStringVersion ? uint32_t(reinterpret_cast<uintptr_t>(stream)) :
            uint32_t(locals > 0 ? locals + 1 : 1);
        Call<bool>(stream, 8, &flag);
        mFlag23 = flag != 0;
    }
    mParameterFlags.clear();
    mParameterFlags.insert(mParameterFlags.begin(), uint32_t(parameters), uint8_t(0));
    if (version > 1)
        for (int32_t i = 0; i < parameters; ++i) Call<bool>(stream, 5, &mParameterFlags[i]);
    if (version > 2) Call<bool>(stream, 8, &mField5C);
    if (version > 3) Call<bool>(stream, 8, &mField24);
    return Call<uint8_t>(stream, 18) == 0;
}
void cTSTreeProperties::InitializeParameters()
{
    for (int32_t i = 0; i < 4; ++i) {
        TemporaryString name;
        name.value.mString = "param " + std::to_string(i);
        SetTableName(mParameterNames, &name.value, i + 1);
    }
    mParameterFlags.insert(mParameterFlags.begin(), 4, uint8_t(1));
    mModified = mFlag23 = 1;
}
void *cTSTreeProperties::GetTree() { return Call<void *>(mOwner, 17, uint32_t(mTreeId)); }
void cTSTreeProperties::GetScopeName(int32_t scope, cIGZString *out)
{
    out->FromChar(scope == kParameterScope ? "parameter" : scope == kLocalScope ? "local" : GameAddress<char>(0xdb3ec4));
}
int32_t cTSTreeProperties::GetNameCount(int32_t scope)
{
    void *table = As<ifaces::cTSTreeProperties>(this)->GetNameTable(scope);
    return table ? As<ifaces::TreePropertyNameTable>(table)->Count(0) : 0;
}
void cTSTreeProperties::GetName(int32_t scope, int16_t index, cIGZString *out)
{
    TemporaryString result(out);
    void *table = As<ifaces::cTSTreeProperties>(this)->GetNameTable(scope);
    TemporaryString name;
    if (table) As<ifaces::TreePropertyNameTable>(table)->GetName(&name.value, int32_t(index) + 1, 0);
    if (table && !name.value.mString.empty())
        reinterpret_cast<SString *>(&result.value)->AssignText(reinterpret_cast<SString *>(&name.value));
    else result.value.mString.clear();
    out->Copy(*result.value.AsInterface());
}
void cTSTreeProperties::SetName(int32_t scope, int16_t index, cIGZString *text)
{
    void *table = As<ifaces::cTSTreeProperties>(this)->GetNameTable(scope);
    TemporaryString name(text);
    if (name.value.mString.empty()) name.value.Sprintf(GameAddress<char>(0xd9b648), int32_t(index));
    if (table) {
        SetTableName(table, &name.value, int32_t(index) + 1);
        if (scope == kParameterScope && int32_t(index) == int32_t(mParameterFlags.size())) mParameterFlags.push_back(1);
        mModified = 1;
    }
    mFlag23 = 0;
}
void cTSTreeProperties::RemoveName(int32_t scope, int16_t index)
{
    if (index < 0 || index >= int16_t(As<ifaces::cTSTreeProperties>(this)->GetNameCount(scope))) return;
    void *table = As<ifaces::cTSTreeProperties>(this)->GetNameTable(scope);
    if (table) {
        const int32_t count = As<ifaces::TreePropertyNameTable>(table)->Count(0);
        if (index == count - 1) {
            As<ifaces::TreePropertyNameTable>(table)->RemoveName(int32_t(index) + 1, 0xffffffffu);
            if (scope == kParameterScope) mParameterFlags.pop_back();
        } else {
            TemporaryString empty;
            SetTableName(table, &empty.value, int32_t(index) + 1);
        }
        mModified = 1;
    }
    mFlag23 = 0;
}
bool cTSTreeProperties::GetParameterFlag(int16_t index)
{
    return index < 0 || uint32_t(index) >= mParameterFlags.size() || mParameterFlags[index] != 0;
}
void cTSTreeProperties::SetParameterFlag(int16_t index, uint8_t value)
{
    if (index >= 0 && uint32_t(index) < mParameterFlags.size()) mParameterFlags[index] = value;
}
void cTSTreeProperties::ClearFlag23() { mFlag23 = 0; }
void cTSTreeProperties::SetDescription(const cIGZString *text)
{
    mDescription.AsInterface()->Copy(*text);
    mModified = 1;
}
void *cTSTreeProperties::GetNameTable(int32_t scope)
{
    return scope == kParameterScope ? mParameterNames : scope == kLocalScope ? mLocalNames : nullptr;
}
