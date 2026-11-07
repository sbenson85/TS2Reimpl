// cTSCommonSystem (lane LC). Layout and slots: src/ts/cTSCommonSystem.h.
#include "ts/cTSCommonSystem.h"

#include "app/AppInit.h" // CommonImplSlot5
#include "app/ProductInfo.h"
#include "game/game.h"
#include "game/import.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"        // GZString, gz::AnyBoolean
#include "gz/cIGZCOM.h"
#include "gz/cIGZFrameWork.h"
#include "gz/cIGZMessage2Standard.h"
#include "gz/cIGZString.h"
#include "ifaces/cIGZPropertySetString.h"
#include "misc/HexParse.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject, ScopedRef
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject
#include "rz/StaticGetters2.h"   // ServiceCell
#include "rz/cRZFile.h"
#include "ts/cTSCatalog.h"            // GZStringList
#include "ts/cTSContentRegistry.h"    // cITSContentRegistry
#include "ts/cTSCommonSystemObjects.h"
#include "ts/cTSCustomContentSupport.h" // ContentString
#include "ts/cTSEffectsManager.h"     // cITSEffectsManager
#include "misc/UtilitiesE.h"

#include <cstring>
#include <new>

namespace {

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// The four COM objects Init makes (class, interface).
constexpr uint32_t kCameraControllersClass = 0x6afa13be, kCameraControllersIid = 0x8afa11d4;
constexpr uint32_t kEffectsClass = 0x49822f75, kEffectsIid = 0xc97cf5cd;
constexpr uint32_t kRenderTargetsClass = 0xed0e4590, kRenderTargetsIid = 0xed0e458a;
constexpr uint32_t kStringPoolClass = 0xf074e038, kStringPoolIid = 0xf074e02a;
// The camera controller manager's start and stop (slots 15 and 16); the render target texture manager's (3 and 4); the
// string pool's start (4, with 1).
constexpr int kCameraStart = 15, kCameraStop = 16;
constexpr int kRenderTargetsStart = 3, kRenderTargetsStop = 4;
constexpr int kStringPoolStart = 4;

// The framework's services Init and the constructor ask for: the settings (as the property set, and as the notifier
// whose slot 4 watches a setting: target, message id, name, default value), and the holder Init tells about 0x6aede879.
constexpr uint32_t kSettingsService = 0x0ac1887e, kSettingsIid = 0x4bb0d269, kNotifierIid = 0x6bb22f38;
constexpr int kNotifierWatch = 4;
constexpr uint32_t kHolderService = 0xe5074b6b, kHolderIid = 0x25074b4d;
constexpr int kHolderObject = 4, kHolderObjectAdd = 0;

// The app's path option of the effects directories (CommonImplSlot5).
constexpr uint32_t kEffectsPathOption = 0xac9f786b;

// The settings' values: the game's two static AnyBoolean objects (their value bytes at +4), which other code reads.
constexpr uint32_t kFloorCollisions = 0x1087514;
constexpr uint32_t kWallCollisions = 0x108751c;
// The product the App slots use (the game's static pointer to "Sims2EP9.exe").
constexpr uint32_t kAppProduct = 0x1087510;

// The interface CollectTextureKeys asks its object for.
constexpr uint32_t kSceneNodeIid = 0x4512dd13;

// The registry (HKEY_CURRENT_USER), through the game's import slots.
using HKey = void *;
constexpr uintptr_t kCurrentUser = 0x80000001;
constexpr uint32_t kReadAccess = 0x20019;  // KEY_READ
constexpr uint32_t kAllAccess = 0xf003f;   // KEY_ALL_ACCESS
constexpr uint32_t kRegString = 1, kRegDword = 4;
using RegOpenKeyExA = long(__stdcall *)(HKey, const char *, uint32_t, uint32_t, HKey *);
using RegQueryValueExA = long(__stdcall *)(HKey, const char *, uint32_t *, uint32_t *, void *, uint32_t *);
using RegCloseKey = long(__stdcall *)(HKey);
using RegCreateKeyExA = long(__stdcall *)(HKey, const char *, uint32_t, char *, uint32_t, uint32_t, void *, HKey *, uint32_t *);
using RegSetValueExA = long(__stdcall *)(HKey, const char *, uint32_t, uint32_t, const void *, uint32_t);
using CoCreateGuid = long(__stdcall *)(void *);
HKey CurrentUser() { return reinterpret_cast<HKey>(kCurrentUser); }
long OpenKey(const char *key, HKey *out)
{
    return ImportFunction<RegOpenKeyExA, 0xd9a028>()(CurrentUser(), key, 0, kReadAccess, out);
}
long QueryValue(HKey key, const char *name, uint32_t *type, void *data, uint32_t *size)
{
    return ImportFunction<RegQueryValueExA, 0xd9a054>()(key, name, nullptr, type, data, size);
}
void CloseKey(HKey key) { ImportFunction<RegCloseKey, 0xd9a02c>()(key); }

// A key path's buffer (0x104 characters) and its limit.
constexpr uint32_t kKeySize = 0x104;
constexpr uint32_t kKeyLimit = 0x103;
// The key the "Any" reads and the writes use when the product has none.
const char kFallbackKey[] = "Software\\EA Games\\The Sims 2";

// GetProductPaths (0xcb4690, src/app/ProductInfo.h): the product's registry key into `key` (`*length` out: what it
// wrote). Always true: it writes the one key whatever the product.
bool ProductRegistryKey(const char *product, char *key, uint32_t *length)
{
    return product::GetProductPaths(product, key, length, nullptr, nullptr, nullptr, nullptr, nullptr);
}

// The registry slots' key: the product's (or, with `fallback`, kFallbackKey when it has none; else what the lookup left,
// an empty key when it wrote nothing) with `subkey` appended. False when the subkey doesn't fit: the lookup's length plus
// the subkey's must stay under 0x103 (the lookup's length, not the key's, so after a fallback it is the lookup's own).
bool ProductKey(const char *product, const char *subkey, bool fallback, char (&key)[kKeySize])
{
    std::memset(key, 0, sizeof(key));
    uint32_t length = kKeyLimit;
    const bool found = ProductRegistryKey(product, key, &length);
    if (fallback && !found) std::strcpy(key, kFallbackKey);
    if (subkey) {
        if (length + std::strlen(subkey) >= kKeyLimit) return false;
        std::strcat(key, subkey);
    }
    return true;
}

// The REG_SZ (or, `anyType`, any) value `name` under `key` into `out` (emptied first); false when there is none.
bool ReadString(const char *key, const char *name, cIGZString *out, bool anyType)
{
    HKey handle;
    if (OpenKey(key, &handle) != 0) return false;
    char buffer[kKeySize] = {};
    uint32_t type = kRegString;
    uint32_t size = kKeyLimit;
    bool read = false;
    if (QueryValue(handle, name, &type, buffer, &size) == 0 && (anyType || type == kRegString)) {
        out->Erase(0, 0xffffffffu);
        // ORIGINAL-BUG: a value of no bytes appends 0xffffffff characters (the string throws).
        out->Append(buffer, size - 1);
        read = true;
    }
    CloseKey(handle);
    return read;
}

// A virtual base of an object built with virtual inheritance: the virtual-base table pointer at +vbptr, entry `entry`.
void *VirtualBase(void *object, uint32_t vbptr, int entry)
{
    auto *at = static_cast<uint8_t *>(object) + vbptr;
    const int32_t *table = *reinterpret_cast<int32_t *const *>(at);
    return at + table[entry];
}

// 0xae8980 (GetMaterialParserCached): the expression-function registry, the scene graph system's material parser (service
// 0xe5074b6b, its slot 11); its virtual base (table at +4, entry 2) is the parser's cIBaseParser face: slot 7
// (cBaseParser::RegisterMetaCommand 0xb05b50) files a function (its cIExpressionFunction face) in the function table under
// the name its slot 5 gives, slot 5 (cBaseParser::UnregisterCommand 0xb072f0) removes a command by name, matched by content.
void *ExpressionRegistry()
{
    void *registry = GetMaterialParserCached();
    return VirtualBase(registry, 4, 2);
}
constexpr int kRegistryUnregister = 5, kRegistryRegister = 7;

const char kIsDayName[] = "tsIsDay";
const char kIsLightMaterialName[] = "tsIsLightMaterial";

// A new expression function's face (+4), counted once; null when new failed.
template <class Function> gzexpr::cIExpressionFunction *NewExpressionFunction()
{
    Function *function = new (std::nothrow) Function;
    if (!function) return nullptr;
    gzexpr::cIExpressionFunction *face = function;
    face->AddRef();
    return face;
}

// A new named object for the effects manager (its +0 face) with a zero count; null when new failed.
template <class Object, class Face> cIGZUnknown *NewEffectsObject()
{
    return static_cast<Face *>(new (std::nothrow) Object);
}

// Clears the pointer, then releases what it held.
void Drop(cIGZUnknown *&ref)
{
    if (cIGZUnknown *old = ref) {
        ref = nullptr;
        old->Release();
    }
}

// The framework's service `id` as `iid` into `out` (left null without a framework).
void FrameworkService(uint32_t id, uint32_t iid, void **out)
{
    if (auto *framework = static_cast<cIGZFrameWork *>(GetServiceObject())) framework->GetSystemService(id, iid, out);
}

// The COM director's object of `cls` as `iid` into `field` (released first, the field cleared before the release).
bool MakeObject(cIGZUnknown *&field, uint32_t cls, uint32_t iid)
{
    auto *com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    Drop(field);
    return static_cast<uint8_t>(com->GetClassObject(cls, iid, reinterpret_cast<void **>(&field))) != 0;
}

} // namespace

namespace common_system {

// 0xcbb420
void RegisterExpressionFunctions()
{
    gzexpr::cIExpressionFunction *isDay = NewExpressionFunction<cIsDayFunction>();
    RawSlot<kRegistryRegister, void>(ExpressionRegistry(), isDay);
    gzexpr::cIExpressionFunction *isLightMaterial = NewExpressionFunction<cIsLightMaterialFunction>();
    RawSlot<kRegistryRegister, void>(ExpressionRegistry(), isLightMaterial);
    if (isLightMaterial) isLightMaterial->Release();
    if (isDay) isDay->Release();
}

// 0xcbb610: by name, as the original (whose names are the functions' own literals 0xeb7d18 / 0xeb7d70; the parser compares
// names by content, so our literals find the same entries).
// ORIGINAL-BUG: slot 5 removes commands; the functions are in the parser's function table (slot 7 put them there), so
// nothing is removed and both stay registered.
void UnregisterExpressionFunctions()
{
    RawSlot<kRegistryUnregister, void>(ExpressionRegistry(), kIsDayName);
    RawSlot<kRegistryUnregister, void>(ExpressionRegistry(), kIsLightMaterialName);
}

// 0xcb9e40
bool NewGuid(uint32_t *out)
{
    struct Guid {
        uint32_t data1;
        uint16_t data2, data3;
        uint8_t data4[8];
    } guid;
    if (ImportFunction<CoCreateGuid, 0xd9a6d0>()(&guid) != 0) return false;
    const auto bytes = [&guid](int at) {
        return uint32_t(guid.data4[at]) | uint32_t(guid.data4[at + 1]) << 8 | uint32_t(guid.data4[at + 2]) << 16 |
               uint32_t(guid.data4[at + 3]) << 24;
    };
    const uint32_t words[4] = {guid.data1, uint32_t(guid.data2) | uint32_t(guid.data3) << 16, bytes(0), bytes(4)};
    std::memcpy(out, words, sizeof(words));
    return true;
}

// 0x9c5ae0: the file-system service's cIGZFileSystem face (0x9c59d9, not ours yet), its slot 8.
void EffectsDirectory(void *out)
{
    void *fileSystem = OriginalFunction<void *(__cdecl *)(), 0x9c59d9>()();
    static_cast<cRZString *>(out)->mString.assign(RawSlot<8, const char *>(fileSystem));
}

} // namespace common_system

// ---- construction ----

// 0xcb9330: the base part, the objects null, the settings property set asked for, the counter 1.
cTSCommonSystem::cTSCommonSystem()
    : rz::cRZSystemService(kServiceId, kPriority), mCameraControllers(nullptr), mEffects(nullptr), mRenderTargets(nullptr),
      mStringPool(nullptr), mSettings(nullptr)
{
    FrameworkService(kSettingsService, kSettingsIid, reinterpret_cast<void **>(&mSettings));
    mCounter = 1;
}

// 0xcb93f0: the settings, then the string pool, the render targets, the effects and the camera controllers released.
cTSCommonSystem::~cTSCommonSystem() noexcept
{
    if (cIGZUnknown *settings = mSettings) {
        mSettings = nullptr;
        settings->Release();
    }
    if (mStringPool) mStringPool->Release();
    if (mRenderTargets) mRenderTargets->Release();
    if (mEffects) mEffects->Release();
    if (mCameraControllers) mCameraControllers->Release();
}

// 0xcb9dd0
cTSCommonSystem *__cdecl cTSCommonSystem::Create() { return new (std::nothrow) cTSCommonSystem; }

// 0xcb94a0: the new pointer counted, the old one released (still stored while it is), then the new one stored.
void cTSCommonSystem::SetSettings(cIGZUnknown *settings)
{
    if (settings) settings->AddRef();
    if (mSettings) mSettings->Release();
    mSettings = settings;
}

// ---- cIGZUnknown ----

// 0xcbaed0: cITSCommonSystem -> +0x1c (counted through the primary face's AddRef), else the service's.
bool cTSCommonSystem::QueryInterface(uint32_t riid, void **out)
{
    if (riid != cITSCommonSystem::kIid) return rz::cRZSystemService::QueryInterface(riid, out);
    *out = static_cast<cITSCommonSystem *>(this);
    AddRef();
    return true;
}

uint32_t cTSCommonSystem::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cTSCommonSystem::Release() { return rz::cRZSystemService::Release(); }

// ---- cIGZSystemService ----

// 0xcba7c0
bool cTSCommonSystem::Init()
{
    if (!rz::cRZSystemService::Init()) return false;
    {
        ScopedRef holder{nullptr};
        FrameworkService(kHolderService, kHolderIid, &holder.mObject);
        // ORIGINAL-BUG: the holder is used without a null test.
        void *object = RawSlot<kHolderObject, void *>(holder.mObject);
        RawSlot<kHolderObjectAdd, void>(object, OriginalFunction<uint32_t(__cdecl *)(), 0xcc4350>()());
        holder.ReleaseObject();
    }
    if (MakeObject(mCameraControllers, kCameraControllersClass, kCameraControllersIid)) RawSlot<kCameraStart, void>(mCameraControllers);
    if (MakeObject(mEffects, kEffectsClass, kEffectsIid)) InitEffects();
    common_system::RegisterExpressionFunctions();
    if (MakeObject(mRenderTargets, kRenderTargetsClass, kRenderTargetsIid)) RawSlot<kRenderTargetsStart, void>(mRenderTargets);
    MakeObject(mStringPool, kStringPoolClass, kStringPoolIid);
    // ORIGINAL-BUG: the string pool is started whether or not it was made (a null one is called).
    RawSlot<kStringPoolStart, void>(mStringPool, 1u);

    ScopedRef notifier{nullptr};
    FrameworkService(kSettingsService, kNotifierIid, &notifier.mObject);
    cIGZMessageTarget2 *const target = this;
    {
        GZString name("floorCollisions");
        gz::AnyBoolean value(1);
        // ORIGINAL-BUG: the notifier is used without a null test.
        RawSlot<kNotifierWatch, void>(notifier.mObject, Word(target), kFloorCollisionsMessage, &name, static_cast<cIGZAny *>(&value));
    }
    {
        GZString name("wallCollisions");
        gz::AnyBoolean value(1);
        RawSlot<kNotifierWatch, void>(notifier.mObject, Word(target), kWallCollisionsMessage, &name, static_cast<cIGZAny *>(&value));
    }
    notifier.ReleaseObject();
    return true;
}

// Init's effects part: the manager started, the effects directories that exist and "main.fx" in the file system's
// directory added, the five named objects registered, the renderer handed to 0xb3bbd0.
void cTSCommonSystem::InitEffects()
{
    As<cITSEffectsManager>(mEffects)->Init();
    GZStringList directories;
    appinit::CommonImplSlot5(kEffectsPathOption, Word(&directories), 1, 0, 0xffffffffu);
    for (const GZString &directory : directories) {
        ContentString path(directory.c_str());
        if (cRZFile::PathExists(*path.Interface())) As<cITSEffectsManager>(mEffects)->AddPaths(path.s.mString.c_str(), nullptr);
    }
    ContentString shader;
    common_system::EffectsDirectory(&shader.s);
    shader.s.mString.append("main.fx", 7);
    As<cITSEffectsManager>(mEffects)->AddPaths(nullptr, shader.s.mString.c_str());
    using namespace common_system;
    As<cITSEffectsManager>(mEffects)->SetNamedB("pinToFloor", NewEffectsObject<cPinToFloorConstraint, cITSEffectsConstraint>());
    As<cITSEffectsManager>(mEffects)->SetNamedB("pinToTops", NewEffectsObject<cPinToTopsConstraint, cITSEffectsConstraint>());
    As<cITSEffectsManager>(mEffects)->SetNamedA("wallsAndFloors", NewEffectsObject<cWallsAndFloorsCollider, cITSEffectsCollider>());
    As<cITSEffectsManager>(mEffects)->SetNamedA("walls", NewEffectsObject<cWallsCollider, cITSEffectsCollider>());
    As<cITSEffectsManager>(mEffects)->SetNamedA("floors", NewEffectsObject<cFloorsCollider, cITSEffectsCollider>());
    OriginalFunction<void(__cdecl *)(void *), 0xb3bbd0>()(As<cITSEffectsManager>(mEffects)->GetRendererBase());
}

// 0xcbb560
bool cTSCommonSystem::Shutdown()
{
    if (!rz::cRZSystemService::Shutdown()) return false;
    if (mCameraControllers) {
        RawSlot<kCameraStop, void>(mCameraControllers);
        Drop(mCameraControllers);
    }
    common_system::UnregisterExpressionFunctions();
    if (mEffects) {
        As<cITSEffectsManager>(mEffects)->Shutdown();
        if (mEffects) mEffects->Release();
        mEffects = nullptr;
    }
    if (mRenderTargets) {
        RawSlot<kRenderTargetsStop, void>(mRenderTargets);
        if (mRenderTargets) mRenderTargets->Release();
        mRenderTargets = nullptr;
    }
    Drop(mCameraControllers);
    SetSettings(nullptr);
    Drop(mStringPool);
    return true;
}

// 0xcbae10: both settings' values read into the game's statics.
bool cTSCommonSystem::OnTick(uint32_t)
{
    auto *settings = As<ifaces::cIGZPropertySetString>(mSettings);
    {
        GZString name("floorCollisions");
        settings->GetPropertyValue(&name, GameAddress<void>(kFloorCollisions));
    }
    {
        GZString name("wallCollisions");
        settings->GetPropertyValue(&name, GameAddress<void>(kWallCollisions));
    }
    return true;
}

// ---- cITSCommonSystem ----

// 0xcb9ee0
bool cTSCommonSystem::CollectTextureKeys(cIGZUnknown *object, CommonTextureKeys *out)
{
    cIGZUnknown *node = nullptr;
    bool collected = false;
    if (object->QueryInterface(kSceneNodeIid, reinterpret_cast<void **>(&node))) {
        std::vector<ServiceCell> names;
        OriginalFunction<void(__cdecl *)(std::vector<ServiceCell> *, cIGZUnknown *), 0xae8090>()(&names, node);
        for (ServiceCell &cell : names) {
            StringKeyParser::Result key{};
            ContentString name(reinterpret_cast<const char *>(static_cast<uintptr_t>(Dispatch8(&cell))));
            name.s.mString.append("_txtr", 5);
            StringKeyParser::ParseKey(&key, name.Interface()->ToChar(), 0, nullptr);
            out->push_back(key);
        }
        collected = !names.empty();
        for (ServiceCell &cell : names) cell.Close();
    }
    if (node) node->Release();
    return collected;
}

// 0xcba330
uint32_t cTSCommonSystem::NextId()
{
    if (mCounter) ++mCounter;
    return mCounter;
}

// 0xcbadd0
bool cTSCommonSystem::IsValidId(uint32_t id) { return id != 0 && id < mCounter; }

// 0xcbb390: the product NewestProductExecutable (0xcb4950) names (an empty name when it writes none).
bool cTSCommonSystem::ReadAppDword(const char *name, uint32_t *out, const char *subkey)
{
    char product[kKeySize];
    product[0] = 0;
    product::NewestProductExecutable(product);
    return Common()->ReadProductDword(product, name, out, subkey);
}

// 0xcbb3e0
bool cTSCommonSystem::ReadAppString(const char *name, cIGZString *out, const char *subkey)
{
    return Common()->ReadProductString(*GameAddress<const char *>(kAppProduct), name, out, subkey);
}

// 0xcbb400
bool cTSCommonSystem::ReadAppStringAny(const char *name, cIGZString *out, const char *subkey)
{
    return Common()->ReadProductStringAny(*GameAddress<const char *>(kAppProduct), name, out, subkey);
}

// 0xcbb7a0
bool cTSCommonSystem::WriteAppString(const char *name, cIGZString *value, const char *subkey)
{
    return Common()->WriteProductString(*GameAddress<const char *>(kAppProduct), name, value, subkey);
}

// 0xcbaf20
bool cTSCommonSystem::ReadProductDword(const char *product, const char *name, uint32_t *out, const char *subkey)
{
    char key[kKeySize];
    if (!ProductKey(product, subkey, false, key)) return false;
    HKey handle;
    if (OpenKey(key, &handle) != 0) return false;
    uint32_t value = 0, type = 0, size = 4;
    bool read = false;
    if (QueryValue(handle, name, &type, &value, &size) == 0 && type == kRegDword) {
        *out = value;
        read = true;
    }
    CloseKey(handle);
    return read;
}

// 0xcbb060
bool cTSCommonSystem::ReadProductString(const char *product, const char *name, cIGZString *out, const char *subkey)
{
    if (!name) return false;
    char key[kKeySize];
    if (!ProductKey(product, subkey, false, key)) return false;
    return ReadString(key, name, out, false);
}

// 0xcbb1f0
bool cTSCommonSystem::ReadProductStringAny(const char *product, const char *name, cIGZString *out, const char *subkey)
{
    if (!name) return false;
    char key[kKeySize];
    if (!ProductKey(product, subkey, true, key)) return false;
    return ReadString(key, name, out, true);
}

// 0xcbb650: the value's length (Strlen, no terminator), then its text.
bool cTSCommonSystem::WriteProductString(const char *product, const char *name, cIGZString *value, const char *subkey)
{
    if (!name) return false;
    char key[kKeySize];
    if (!ProductKey(product, subkey, true, key)) return false;
    HKey handle;
    uint32_t disposition;
    if (ImportFunction<RegCreateKeyExA, 0xd9a030>()(CurrentUser(), key, 0, nullptr, 0, kAllAccess, nullptr, &handle, &disposition) != 0)
        return false;
    const uint32_t length = value->Strlen();
    const long set = ImportFunction<RegSetValueExA, 0xd9a024>()(handle, name, 0, kRegString, value->ToChar(), length);
    CloseKey(handle);
    return set == 0;
}

// 0xcba340: with a non-empty value, its length is asked twice (the original's test, then its result).
bool cTSCommonSystem::GetRegionCode(cIGZString *out)
{
    const bool read = Common()->ReadAppString("Region", out, nullptr);
    if (!read || out->Strlen() == 0) {
        ContentString fallback("14726");
        out->Append(*fallback.Interface());
        if (!read) return false;
    }
    return out->Strlen() != 0;
}

// 0xcba460
uint32_t cTSCommonSystem::GetRegion()
{
    static constexpr struct {
        const char *code;
        uint32_t region;
    } kRegions[] = {{"14917", 1},     {"MX08004661", 2}, {"14917EKRTCCD", 3}, {"14917SCCD", 4},   {"EMW-4164", 5},
                    {"14917TH", 6},   {"14917HU", 7},    {"MXHG8004261", 7},  {"MXHG7704637", 7}};
    ContentString code;
    if (!Common()->ReadAppString("Region", code.Interface(), nullptr)) return 1;
    for (const auto &entry : kRegions)
        if (code.s.CompareTo(entry.code, 0xffffffffu, true) == 0) return entry.region;
    return 1;
}

// 0xcba140: the content registry (cached getter 0xb38fb0).
bool cTSCommonSystem::GetContentVersions(cIGZString *out)
{
    auto *registry = static_cast<cITSContentRegistry *>(CachedServiceGetter<0x10faf80, 0x0dc7057a, 0xcdb467ae>());
    if (!registry) return false;
    const uint32_t count = registry->EntryCount();
    if (!count) return false;
    ContentVersion version{0, 0, 0};
    ContentString list;
    // ORIGINAL-BUG: the index is compared as a half-word, so with 0x10000 entries or more the loop never ends.
    for (uint32_t i = 0; uint16_t(i) < count; ++i) {
        if (uint16_t(i) == 0 || !registry->GetEntryVersion(uint16_t(i), &version)) continue;
        ContentString text;
        text.s.Sprintf("%d.%d.%d", version.mId, int32_t(int16_t(version.mMinor)), int32_t(int16_t(version.mMajor)));
        list.s.mString.append(text.s.mString);
        if (uint16_t(i) < count - 1) list.s.mString.append(",", 1);
    }
    if (list.s.Strlen() == 0) return false;
    out->Append(*list.Interface());
    return true;
}

// 0xcba610: the stored text's groups of eight hex digits ('-', '{', '}' skipped) as hexparse::ParseGuidText reads them
// (the original's inlined loop: extra groups are dropped there too, up to the seventh; an eighth overwrote its frame).
bool cTSCommonSystem::GetUserGuid(uint32_t *out)
{
    ContentString text;
    if (Common()->ReadProductString("Sims2.exe", "User", text.Interface(), nullptr)) {
        hexparse::ParseGuidText(out, text.s.ToChar());
        return true;
    }
    if (!common_system::NewGuid(out)) return false;
    text.Interface()->Sprintf("%08x-%04x-%04x-%04x-%04x%08x", out[0], out[1] >> 16, out[1] & 0xffff, out[2] >> 16, out[2] & 0xffff,
                              out[3]);
    Common()->WriteProductString("Sims2.exe", "User", text.Interface(), nullptr);
    return true;
}

// ---- cIGZMessageTarget2 ----

// 0xcb9d00: a standard message with a value (HasData2) of the floor or wall setting's id: the value's byte (an AnyBoolean,
// GetVoid2) into the game's static.
bool cTSCommonSystem::DoMessage(void *message)
{
    if (!message) return false;
    ::cIGZMessage2Standard *standard = nullptr;
    bool handled = false;
    if (static_cast<cIGZUnknown *>(message)->QueryInterface(::cIGZMessage2Standard::kIid, reinterpret_cast<void **>(&standard)) &&
        standard->HasData2()) {
        uint32_t flag = 0;
        switch (standard->GetType()) {
        case kFloorCollisionsMessage: flag = kFloorCollisions; break;
        case kWallCollisionsMessage: flag = kWallCollisions; break;
        default: break;
        }
        if (flag) {
            *GameAddress<uint8_t>(flag + 4) = static_cast<const gz::AnyBoolean *>(standard->GetVoid2())->mValue;
            handled = true;
        }
    }
    if (standard) standard->Release();
    return handled;
}
