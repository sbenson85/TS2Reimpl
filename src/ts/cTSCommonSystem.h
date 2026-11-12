#pragma once
// cTSCommonSystem (lane LC): the app's common system service (system service id 0x8af76dc8, priority 0xfff0bdc1;
// cITSCommonSystem iid 0x4af76d8c), the first service of the main menu's list. 0x3c bytes, /O2. RVAs 1.18.0.187. It owns four
// COM objects it makes in Init (the camera controller manager, the effects manager, the render target texture manager and
// the string pool), registers the effects paths and the effects' named constraint objects, registers the "tsIsDay" /
// "tsIsLightMaterial" expression functions, watches the "floorCollisions" / "wallCollisions" settings (their values kept in
// two game statics), and answers registry reads and writes under the products' keys (HKCU), the region code and the user's
// GUID.
//
// Faces (RTTI order): +0 cRZSystemService / cIGZSystemService (game vtable 0xeb7acc, 13 slots), +4 cRZUnknownMT (0xeb7b04,
// 6: slot 3 the deleting destructor's thunk 0xcb960e), +0x1c cITSCommonSystem (0xeb7b20, 22), +0x20 cIGZMessageTarget2
// (0xeb7b7c, 4).
//
// Standalone shape (docs/standalone-shape.md): the factory 0xcb9dd0 (hooked: `new` of ours) is registered by address with the
// app's COM director (its constructor 0xcb4ec0) and is the only caller of the constructor 0xcb9330; the vtables are
// referenced only by the constructor and the destructor 0xcb93f0, so every object carries ours.
//
// The objects Init makes (the expression functions and the effects' five named objects) are ours since lane LF
// (src/ts/cTSCommonSystemObjects.h); the product helpers 0xcb4690 / 0xcb4950 since lane MB (src/app/ProductInfo.h).
// The scenegraph walker 0xae8090 is ours since lane 261111l (nGZSceneGraph::CollectTextureNames).
// Still original: the expression-function registry getter 0xae8980 and 0xb3bbd0 (the effects renderer handed on).
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "rz/StringKeyParser.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class cIGZString;

// CollectTextureKeys' output: the texture keys (16 bytes each; storage from the allocator service, growth 0x1d1370).
using CommonTextureKeys = std::vector<StringKeyParser::Result, GZServiceAllocator<StringKeyParser::Result>>;
static_assert(sizeof(CommonTextureKeys) == 0xc, "MSVC 14.40 vector");

// cITSCommonSystem (iid 0x4af76d8c): slots 3-21. The registry slots read and write values under HKEY_CURRENT_USER, under
// the key of `product` (an executable name, as 0xcb4690 maps it) with `subkey` (null for none) appended.
class cITSCommonSystem : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x4af76d8c;

    virtual cIGZUnknown *GetCameraControllerManager() = 0;        // 3 (shared 0x46390: +0x24)
    virtual cIGZUnknown *GetEffectsManager() = 0;                 // 4 (shared 0x145770: +0x28)
    virtual cIGZUnknown *GetRenderTargetTextureManager() = 0;     // 5 (shared 0x149cf0: +0x2c)
    // 6 (0xcb9ee0, ret 8): the texture key ("<name>_txtr", parsed) of every name the scenegraph walker 0xae8090 collects from
    // `object` (asked for 0x4512dd13) appended to `out`; true when it collected any.
    virtual bool CollectTextureKeys(cIGZUnknown *object, CommonTextureKeys *out) = 0;
    // 7 (0xcba330): the counter incremented, unless it is 0 (it starts at 1); its new value.
    virtual uint32_t NextId() = 0;
    // 8 (0xcbadd0, ret 4): `id` is non-zero and below the counter.
    virtual bool IsValidId(uint32_t id) = 0;
    // 9 (0xcbb390, ret 0xc): ReadProductDword for the product 0xcb4950 names.
    virtual bool ReadAppDword(const char *name, uint32_t *out, const char *subkey) = 0;
    // 10, 11, 12 (0xcbb3e0, 0xcbb400, 0xcbb7a0, ret 0xc): slots 14, 15 and 16 for the product in the game's static 0x1087510.
    virtual bool ReadAppString(const char *name, cIGZString *out, const char *subkey) = 0;
    virtual bool ReadAppStringAny(const char *name, cIGZString *out, const char *subkey) = 0;
    virtual bool WriteAppString(const char *name, cIGZString *value, const char *subkey) = 0;
    // 13 (0xcbaf20, ret 0x10): a REG_DWORD value.
    virtual bool ReadProductDword(const char *product, const char *name, uint32_t *out, const char *subkey) = 0;
    // 14 (0xcbb060, ret 0x10): a REG_SZ value (up to 0x102 characters) into `out`.
    virtual bool ReadProductString(const char *product, const char *name, cIGZString *out, const char *subkey) = 0;
    // 15 (0xcbb1f0, ret 0x10): as 14, of any type, under "Software\EA Games\The Sims 2" when the product has no key.
    virtual bool ReadProductStringAny(const char *product, const char *name, cIGZString *out, const char *subkey) = 0;
    // 16 (0xcbb650, ret 0x10): `value` written as REG_SZ (the key created), with the same fallback key as 15.
    virtual bool WriteProductString(const char *product, const char *name, cIGZString *value, const char *subkey) = 0;
    // 17 (0xcba340, ret 4): the "Region" value into `out`, "14726" appended when there is none (or it is empty).
    virtual bool GetRegionCode(cIGZString *out) = 0;
    // 18 (0xcba460): the region (1-7) of the "Region" value; 1 for none or an unknown code.
    virtual uint32_t GetRegion() = 0;
    // 19 (0xcba140, ret 4): the content registry's entry versions (1..) as "id.minor.major", comma separated, appended to
    // `out`; false when there are none.
    virtual bool GetContentVersions(cIGZString *out) = 0;
    virtual cIGZUnknown *GetStringPool() = 0;                     // 20 (shared 0x149d00: +0x30)
    // 21 (0xcba610, ret 4): the user's GUID (four words) from the "User" value of Sims2.exe's key; when there is none, a new
    // GUID, written there.
    virtual bool GetUserGuid(uint32_t *out) = 0;
};

class cTSCommonSystem : public rz::cRZSystemService, public cITSCommonSystem, public cIGZMessageTarget2 {
public:
    static constexpr uint32_t kVTable = 0xeb7acc; // the game's, for tests
    static constexpr uint32_t kVTable4 = 0xeb7b04;
    static constexpr uint32_t kCommonVTable = 0xeb7b20;
    static constexpr uint32_t kMessageVTable = 0xeb7b7c;
    static constexpr uint32_t kSize = 0x3c;
    static constexpr uint32_t kServiceId = 0x8af76dc8;
    static constexpr int32_t kPriority = static_cast<int32_t>(0xfff0bdc1);
    static constexpr uint32_t kFloorCollisionsMessage = 0x31746e24;
    static constexpr uint32_t kWallCollisionsMessage = 0x31746e38;

    cTSCommonSystem();                    // 0xcb9330
    ~cTSCommonSystem() noexcept override; // 0xcb93f0 (deleting 0xcb9620; +4 thunk 0xcb960e)
    cTSCommonSystem(const cTSCommonSystem &) = delete;
    cTSCommonSystem &operator=(const cTSCommonSystem &) = delete;

    // 0xcb9dd0 (__cdecl): the COM director's factory: a new service (null when new failed).
    static cTSCommonSystem *__cdecl Create();

    // ---- cIGZUnknown (every face) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0xcbaed0; thunks 0xcbaf01 (+4), 0xcbaf09 (+0x1c), 0xcbaf11 (+0x20)
    uint32_t AddRef() override;                              // folded (the base's)
    uint32_t Release() override;                             // folded (the base's)

    // ---- cIGZSystemService ----
    bool Init() override;                   // 0xcba7c0
    bool Shutdown() override;               // 0xcbb560
    bool OnTick(uint32_t counter) override; // 0xcbae10

    // ---- cITSCommonSystem (+0x1c) ----
    cIGZUnknown *GetCameraControllerManager() override { return mCameraControllers; }
    cIGZUnknown *GetEffectsManager() override { return mEffects; }
    cIGZUnknown *GetRenderTargetTextureManager() override { return mRenderTargets; }
    bool CollectTextureKeys(cIGZUnknown *object, CommonTextureKeys *out) override;
    uint32_t NextId() override;
    bool IsValidId(uint32_t id) override;
    bool ReadAppDword(const char *name, uint32_t *out, const char *subkey) override;
    bool ReadAppString(const char *name, cIGZString *out, const char *subkey) override;
    bool ReadAppStringAny(const char *name, cIGZString *out, const char *subkey) override;
    bool WriteAppString(const char *name, cIGZString *value, const char *subkey) override;
    bool ReadProductDword(const char *product, const char *name, uint32_t *out, const char *subkey) override;
    bool ReadProductString(const char *product, const char *name, cIGZString *out, const char *subkey) override;
    bool ReadProductStringAny(const char *product, const char *name, cIGZString *out, const char *subkey) override;
    bool WriteProductString(const char *product, const char *name, cIGZString *value, const char *subkey) override;
    bool GetRegionCode(cIGZString *out) override;
    uint32_t GetRegion() override;
    bool GetContentVersions(cIGZString *out) override;
    cIGZUnknown *GetStringPool() override { return mStringPool; }
    bool GetUserGuid(uint32_t *out) override;

    // ---- cIGZMessageTarget2 (+0x20) ----
    bool DoMessage(void *message) override; // 0xcb9d00

    cITSCommonSystem *Common() { return this; }

    // +0x00 .. +0x1b: cRZSystemService; +0x1c, +0x20: the two faces' vtables
    cIGZUnknown *mCameraControllers; // +0x24 class 0x6afa13be as 0x8afa11d4 (cTSCameraControllerManager)
    cIGZUnknown *mEffects;           // +0x28 class 0x49822f75 as cITSEffectsManager (0xc97cf5cd)
    cIGZUnknown *mRenderTargets;     // +0x2c class 0xed0e4590 as 0xed0e458a (cTSRenderTargetTextureManager)
    cIGZUnknown *mStringPool;        // +0x30 class 0xf074e038 as 0xf074e02a (cTSStringPool)
    cIGZUnknown *mSettings;          // +0x34 the settings property set (framework 0x0ac1887e as 0x4bb0d269)
    uint32_t mCounter;               // +0x38 NextId's counter (starts at 1)

private:
    void SetSettings(cIGZUnknown *settings); // 0xcb94a0 (ret 4): the counted pointer's assignment
    void InitEffects();                      // Init's effects part (inlined there)
};

static_assert(sizeof(cTSCommonSystem) == cTSCommonSystem::kSize, "new 0x3c in the factory");
static_assert(offsetof(cTSCommonSystem, mCameraControllers) == 0x24 && offsetof(cTSCommonSystem, mEffects) == 0x28 &&
                  offsetof(cTSCommonSystem, mRenderTargets) == 0x2c && offsetof(cTSCommonSystem, mStringPool) == 0x30 &&
                  offsetof(cTSCommonSystem, mSettings) == 0x34 && offsetof(cTSCommonSystem, mCounter) == 0x38,
              "cTSCommonSystem layout");

namespace common_system {

// 0xcbb420 (__cdecl): the "tsIsDay" and "tsIsLightMaterial" expression functions made and registered.
void RegisterExpressionFunctions();
// 0xcbb610 (__cdecl): both unregistered by name.
void UnregisterExpressionFunctions();
// 0xcb9e40 (__cdecl): a new GUID (CoCreateGuid) as four words: Data1, Data2 | Data3 << 16, Data4's bytes little-endian;
// false (nothing written) when CoCreateGuid fails.
bool NewGuid(uint32_t *out);
// 0x9c5ae0 (__cdecl): the file system's slot 8 directory assigned to `out` (a cRZString).
void EffectsDirectory(void *out);

} // namespace common_system
