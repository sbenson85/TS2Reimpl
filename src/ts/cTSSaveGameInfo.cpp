// cTSSaveGameInfo (src/ts/cTSSaveGameInfo.h): the save-game description record. Lane DV.
#include "ts/cTSSaveGameInfo.h"

#include "app/AppInit.h"      // appinit::GetLanguageService
#include "app/ReportFinish.h" // cRZVersion
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"
#include "gz/cGZPropertySet.h"
#include "gz/cGZPropertySetResource.h"
#include "gz/cIGZString.h"
#include "ifaces/cIGZAny.h"
#include "ifaces/cIGZPersistResource.h"
#include "ifaces/cIGZPersistResourceManager.h"
#include "ifaces/cITSPersistSystem.h"
#include "ifaces/cIGZPropertySetString.h"
#include "ifaces/cITSSaveGameInfo.h"
#include "rz/RZAutoRef.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/cRZString.h"

#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

namespace {

constexpr int kSlotAppGameName = 37;      // app service: the object whose slot 5 is the game's name text
constexpr int kSlotNameText = 5;          // ... its text (ToChar)
constexpr int kSlotGetSystemService = 5;  // app service: GetSystemService(service, iid, out)
constexpr int kSlotLanguage = 7;          // language service: the current language id

// Describe's texts (game text, read in place).
constexpr uint32_t kDescribeHeader = 0xeb9e3c;          // the header line, newline included
constexpr uint32_t kDescribeHeaderLength = 0x1b;
constexpr uint32_t kGameNameLabel = 0xeb9e58;           // index 0's label
constexpr uint32_t kGameNameLabelLength = 0x10;
constexpr uint32_t kVersionLabel = 0xeb9e6c;            // index 1's label
constexpr uint32_t kVersionLabelLength = 0x13;
constexpr uint32_t kLanguageLabel = 0xeb9e80;           // index 2's label (never shown: Describe)
constexpr uint32_t kLanguageLabelLength = 8;
constexpr uint32_t kDescribeSeparator = 0xdc3728;       // between label and value
constexpr uint32_t kDescribeSeparatorLength = 3;
constexpr uint32_t kDescribeUnsupported = 0xeb9e8c;     // a value of another type
constexpr uint32_t kDescribeUnsupportedLength = 0x13;
constexpr uint32_t kDescribeNewline = 0xd9e644;

// The decimal key of an index, as every accessor builds it: a cRZString Sprintf'd with "%u", then the property set's
// key string made from its text.
const char *FormatIndex(cRZString &number, uint32_t index)
{
    number.Construct();
    number.Sprintf(GameAddress<const char>(cTSSaveGameInfo::kIndexFormat), index);
    return number.mString.c_str();
}

struct IndexKey {
    cRZString number;
    GZServiceString key;

    explicit IndexKey(uint32_t index) : key(FormatIndex(number, index)) {}
    ~IndexKey() { number.Destruct(); }
    IndexKey(const IndexKey &) = delete;
    IndexKey &operator=(const IndexKey &) = delete;
};

// The persist system from the app service, held as the original's RZAutoRef local (released with
// RZAutoRef::ReleaseObject).
struct PersistSystemRef {
    RZAutoRef ref{nullptr};

    PersistSystemRef()
    {
        if (void *service = GetServiceObject())
            VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **), kSlotGetSystemService>(
                service, cTSSaveGameInfo::kPersistSystemService, cTSSaveGameInfo::kPersistSystemIid, &ref.mObject);
    }
    ~PersistSystemRef() { ref.ReleaseObject(); }
    ifaces::cITSPersistSystem *Get() const { return As<ifaces::cITSPersistSystem>(ref.mObject); }
    PersistSystemRef(const PersistSystemRef &) = delete;
    PersistSystemRef &operator=(const PersistSystemRef &) = delete;
};

// A counted local (cRZAutoRefCount): Release on scope exit.
struct CountedLocal {
    void *object = nullptr;
    ~CountedLocal()
    {
        if (object) As<ifaces::cIGZPersistResource>(object)->Release();
    }
};

// A local AnyString built from text: the value's own new cRZString, assigned through the counted-pointer assignment
// (AddRef the new text, Release the old null one), released when the local goes.
struct LocalAnyString {
    AnyString any;
    explicit LocalAnyString(const char *text)
    {
        any.mVTable = GameAddress<const void>(gzany::kStringVTable);
        any.mString = nullptr;
        auto *created = static_cast<cRZString *>(operator new(sizeof(cRZString)));
        if (created) created->Construct(text);
        cIGZString *held = reinterpret_cast<cIGZString *>(created);
        if (held != any.mString) {
            if (held) held->AddRef();
            any.mString = held;
        }
    }
    ~LocalAnyString()
    {
        if (any.mString) any.mString->Release();
        any.mVTable = GameAddress<const void>(gzany::kAnyVTable);
    }
};

} // namespace

// 0xcd7a10: the game's vtable, no references, no property set.
cTSSaveGameInfo *cTSSaveGameInfo::Construct()
{
    mVTable = GameAddress<const void>(kVTable);
    mRefCount = 0;
    mProperties = nullptr;
    return this;
}

// 0xcd7aa0: the framework factory's create function.
cTSSaveGameInfo *cTSSaveGameInfo::Create()
{
    auto *info = static_cast<cTSSaveGameInfo *>(operator new(kSize));
    if (!info) return nullptr;
    return info->Construct();
}

// 0xcd7a30: releases the property set (the field is not cleared).
void cTSSaveGameInfo::Destruct()
{
    mVTable = GameAddress<const void>(kVTable);
    if (mProperties) As<ifaces::cIGZPropertySetString>(mProperties)->Release();
}

// 0xcd7a80 (slot 12).
void *cTSSaveGameInfo::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) operator delete(this, kSize);
    return this;
}

// 0xcd8720 (slot 0): cIGZUnknown (1) or cITSSaveGameInfo; AddRef through the vtable.
bool cTSSaveGameInfo::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid != 1 && riid != kIid) return false;
    *out = this;
    As<ifaces::cITSSaveGameInfo>(this)->AddRef();
    return true;
}

// 0xcd8750 (slot 2): the last reference deletes through slot 12.
uint32_t cTSSaveGameInfo::Release()
{
    if (mRefCount > 1) return --mRefCount;
    VirtualCall<void *(__thiscall *)(void *, uint32_t), 12>(this, 1u);
    return 0;
}

// 0xcd7e30: makes the property set on first use: a new cGZPropertySetResourceString, held through its set face (+0x1c).
bool cTSSaveGameInfo::EnsureProperties()
{
    if (!mProperties) {
        // Our resource class (standalone shape, lane EL), held by its set face.
        auto *resource = new (std::nothrow) cGZPropertySetResourceString;
        cGZPropertySetString *created = resource ? resource->SetFace() : nullptr;
        cGZPropertySetString *old = mProperties;
        if (created != old) {
            if (created) As<ifaces::cIGZPropertySetString>(created)->AddRef();
            mProperties = created;
            if (old) As<ifaces::cIGZPropertySetString>(old)->Release();
        }
    }
    return mProperties != nullptr;
}

// 0xcd7fa0 (slot 3): records the game's name (index 0), the executable's file version (1) and the language (2), each
// through this object's own slots.
void cTSSaveGameInfo::Fill()
{
    if (!EnsureProperties()) return;
    if (!GetServiceObject()) return;
    using Getter = void *(__thiscall *)(void *);
    if (!VirtualCall<Getter, kSlotAppGameName>(GetServiceObject())) return;

    cRZString name;
    name.Construct();
    void *source = VirtualCall<Getter, kSlotAppGameName>(GetServiceObject());
    if (VirtualCall<const char *(__thiscall *)(void *), kSlotNameText>(source)) {
        source = VirtualCall<Getter, kSlotAppGameName>(GetServiceObject());
        cRZString text;
        text.Construct(VirtualCall<const char *(__thiscall *)(void *), kSlotNameText>(source));
        name.mString = text.mString;
        text.Destruct();
    }
    As<ifaces::cITSSaveGameInfo>(this)->SetString(kIndexGameName, &name);

    cRZString version;
    version.Construct();
    appbase::cRZVersion fileVersion;
    fileVersion.Construct();
    fileVersion.GetFileVersion(&version);
    As<ifaces::cITSSaveGameInfo>(this)->SetString(kIndexVersion, &version);
    if (void *language = appinit::GetLanguageService())
        As<ifaces::cITSSaveGameInfo>(this)->SetUint32(
            kIndexLanguage, VirtualCall<uint32_t(__thiscall *)(void *), kSlotLanguage>(language));
    fileVersion.Destruct();
    version.Destruct();
    name.Destruct();
}

// 0xcd8a50 (slot 4): an AnyUint32 under the index's key.
void cTSSaveGameInfo::SetUint32(uint32_t index, uint32_t value)
{
    if (!EnsureProperties()) return;
    IndexKey key(index);
    AnyUint32 any{GameAddress<const void>(gzany::kUint32VTable), value};
    As<ifaces::cIGZPropertySetString>(mProperties)->SetProperty(&key.key, &any);
    any.mVTable = GameAddress<const void>(gzany::kAnyVTable);
}

// 0xcd88c0 (slot 5): an AnyString holding a new copy of value's text under the index's key.
void cTSSaveGameInfo::SetString(uint32_t index, cIGZString *value)
{
    if (!EnsureProperties()) return;
    IndexKey key(index);
    cGZPropertySetString *properties = mProperties;
    LocalAnyString any(value->ToChar());
    As<ifaces::cIGZPropertySetString>(properties)->SetProperty(&key.key, &any.any);
}

// 0xcd7b10 (slot 6): the AnyUint32 under the index's key, when there is one (GetPropertyValue assigns only from the
// same type).
bool cTSSaveGameInfo::GetUint32(uint32_t index, uint32_t *out)
{
    if (!mProperties) return false;
    AnyUint32 any{GameAddress<const void>(gzany::kUint32VTable), 0};
    cRZString number;
    number.Construct();
    number.Sprintf(GameAddress<const char>(kIndexFormat), index);
    bool found;
    {
        GZServiceString key(number.mString.c_str());
        found = As<ifaces::cIGZPropertySetString>(mProperties)->GetPropertyValue(&key, &any);
    }
    if (found) *out = any.mValue;
    number.Destruct();
    return found;
}

// 0xcd7c20 (slot 7): the AnyString's text under the index's key assigned to out (slot 16).
bool cTSSaveGameInfo::GetString(uint32_t index, cIGZString *out)
{
    if (!mProperties) return false;
    AnyString any{GameAddress<const void>(gzany::kStringVTable), nullptr};
    cRZString number;
    number.Construct();
    number.Sprintf(GameAddress<const char>(kIndexFormat), index);
    bool found;
    {
        GZServiceString key(number.mString.c_str());
        found = As<ifaces::cIGZPropertySetString>(mProperties)->GetPropertyValue(&key, &any);
    }
    if (found) *out = *any.mString;
    number.Destruct();
    if (any.mString) any.mString->Release();
    return found;
}

// 0xcd7d50 (slot 8): every key's number, in the set's order.
void cTSSaveGameInfo::GetIndices(IndexList *out)
{
    if (!mProperties) return;
    GZServiceString key;
    void *value;
    bool more = As<ifaces::cIGZPropertySetString>(mProperties)->GetFirst(&key, &value);
    while (more) {
        // atoi: 0x830972 is the UCRT's atoi body (one of four byte-identical wrappers; the difftest shows narrow parsing,
        // "12" -> 12, "4294967295" -> 0x7fffffff).
        out->push_back(std::atoi(key.c_str()));
        more = As<ifaces::cIGZPropertySetString>(mProperties)->GetNext(&key, &value);
    }
}

// 0xcd8140 (slot 9): a text dump of the properties into out (FromChar): a header line, then one line per property in
// the set's order, "label = value\n". The label is the key's text, except keys reading (atoi) as 0 and 1, which get the
// names of the game-name and version entries; an AnyString's value is its whole text, an AnyUint32's its decimal text,
// any other type a fixed placeholder. A set without properties leaves out alone.
void cTSSaveGameInfo::Describe(cIGZString *out)
{
    if (!mProperties) return;
    GZServiceString key;
    void *value;
    if (!As<ifaces::cIGZPropertySetString>(mProperties)->GetFirst(&key, &value)) return;
    std::string text(GameAddress<const char>(kDescribeHeader), kDescribeHeaderLength);
    do {
        std::string label;
        switch (std::atoi(key.c_str())) {
        case kIndexGameName: label.assign(GameAddress<const char>(kGameNameLabel), kGameNameLabelLength); break;
        case kIndexVersion: label.assign(GameAddress<const char>(kVersionLabel), kVersionLabelLength); break;
        case kIndexLanguage:
            label.assign(GameAddress<const char>(kLanguageLabel), kLanguageLabelLength);
            // ORIGINAL-BUG: no break: the language entry's name is overwritten by the key's text ("2").
            [[fallthrough]];
        default: label.assign(key.c_str(), std::strlen(key.c_str())); break;
        }
        text.append(label.c_str());
        text.append(GameAddress<const char>(kDescribeSeparator), kDescribeSeparatorLength);

        const uint32_t type = As<ifaces::cIGZAny>(value)->GetType();
        if (type == gzany::kTypeString) {
            cIGZString *string = static_cast<const AnyString *>(value)->mString;
            const uint32_t length = string->Strlen();
            const std::string copy(string->Data(), length);
            text.append(copy);
        } else if (type == gzany::kTypeUint32) {
            cRZString number;
            number.Construct();
            number.Sprintf(GameAddress<const char>(kIndexFormat), static_cast<const AnyUint32 *>(value)->mValue);
            text.append(number.mString);
            number.Destruct();
        } else {
            text.append(GameAddress<const char>(kDescribeUnsupported), kDescribeUnsupportedLength);
        }
        text.append(GameAddress<const char>(kDescribeNewline), 1);
    } while (As<ifaces::cIGZPropertySetString>(mProperties)->GetNext(&key, &value));
    out->FromChar(text.c_str());
}

// 0xcd8770 (slot 10): the property set's resource saved as record {0xebfee342, group, instance, 0} by the persist
// system; the resource's previous key is put back afterwards when its group was not 0.
bool cTSSaveGameInfo::Save(uint32_t group, uint32_t instance)
{
    if (!EnsureProperties()) return false;
    PersistSystemRef persistSystem;
    if (!persistSystem.ref.mObject) return false;
    CountedLocal resource;
    if (!As<ifaces::cIGZPropertySetString>(mProperties)->QueryInterface(kPersistResourceIid, &resource.object)) return false;
    uint32_t saved[4];
    uint32_t key[4] = {kResourceType, group, instance, 0};
    auto *persist = As<ifaces::cIGZPersistResource>(resource.object);
    persist->GetKey(saved);
    persist->SetKey(key);
    const bool written = persistSystem.Get()->SaveResource(reinterpret_cast<ifaces::cIGZPersistResource *>(resource.object), nullptr);
    if (saved[1] != 0) persist->SetKey(saved);
    return written;
}

// 0xcd7ed0 (slot 11): drops the property set, then asks the persist system's resource manager for record
// {0xebfee342, group, instance, 0} as a property set.
bool cTSSaveGameInfo::Load(uint32_t group, uint32_t instance)
{
    PersistSystemRef persistSystem;
    if (!persistSystem.ref.mObject) return false;
    uint32_t key[4] = {kResourceType, group, instance, 0};
    auto *resources = As<ifaces::cIGZPersistResourceManager>(persistSystem.Get()->GetResourceManager());
    if (cGZPropertySetString *old = mProperties) {
        mProperties = nullptr;
        As<ifaces::cIGZPropertySetString>(old)->Release();
    }
    return resources->GetResource(key, kPropertySetIid, reinterpret_cast<void **>(&mProperties), 0, 0) != 0;
}
