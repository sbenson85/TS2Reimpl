// cTSUIServiceManager (lane KM): the whole class in the standalone shape (src/ui/cTSUIServiceManager.h). Built for size.
#include "rz/RZMessageServer.h"
#include "ui/cTSUIServiceManager.h"

#include "game/game.h"
#include "game/original.h"
#include "rz/cRZMessage2Standard.h"
#include "game/vtable.h"
#include "gz/cIGZCOM.h"
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZMessage2Standard.h"
#include "ifaces/cIGZMessageServer.h"
#include "misc/UtilitiesC.h"     // ServiceQueryHolder, KeyMapSlot5Walker
#include "rz/MiscHelpersB.h"     // KeyMapWalker
#include "rz/RZFrameworkHelpers.h" // RZUnknownSlot
#include "rz/RZMemory.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject, ScopedRef
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject

#include <algorithm>

namespace {

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// The UI services' slots (interface 0x0bd09a11): 3 initialise (bool), 4 shut down, 5 tick.
bool UIServiceInit(cIGZUnknown *service) { return RawSlot<3, bool>(service); }
void UIServiceShutdown(cIGZUnknown *service) { RawSlot<4>(service); }
void UIServiceTick(cIGZUnknown *service, uint32_t counter) { RawSlot<5>(service, counter); }

// The original UI set-up steps the init message runs (not replaced yet; all thiscall on the manager unless noted).
void SetUpStep7408f4(cTSUIServiceManager *self) { OriginalFunction<void(__thiscall *)(void *), 0x7408f4>()(self); }
void SetUpWindows7406c1(cTSUIServiceManager *self, void *winMgr)
{
    OriginalFunction<void(__thiscall *)(void *, void *), 0x7406c1>()(self, winMgr);
}
void SetUpStep740c99(cTSUIServiceManager *self) { OriginalFunction<void(__thiscall *)(void *), 0x740c99>()(self); }
void SetUpStep7436b1() { OriginalFunction<void(__cdecl *)(), 0x7436b1>()(); }
void LoadCursors(cTSUIServiceManager *self) { OriginalFunction<void(__thiscall *)(void *), 0x73ffae>()(self); }
void Message740422(cTSUIServiceManager *self) { OriginalFunction<void(__thiscall *)(void *), 0x740422>()(self); }
void *ConstructMessage(void *memory) { return static_cast<cRZMessage2Standard *>(memory)->Construct(); }
void *MessageServer() { return GetCachedMessageServer(); }

std::list<UIServiceRef>::iterator FindService(std::list<UIServiceRef> &list, cIGZUnknown *service)
{
    return std::find_if(list.begin(), list.end(), [service](const UIServiceRef &held) { return held.Get() == service; });
}

constexpr uint32_t kWinMgrServiceId = 0xc2c2eb0f, kWinMgrIid = 0x22c2eb1f;
constexpr uint32_t kCell10de488 = 0x10de488;

} // namespace

cTSUIServiceManager::cTSUIServiceManager() : rz::cRZSystemService(kServiceId, kPriority)
{
    mMessageTypes.insert(kMsgAppInit);
    mMessageTypes.insert(kMsgAppShutdown);
    mMessageTypes.insert(kMsgD03ffba0);
}

// The message types, the held services (last first), the tick list (its pointers are not counted), the service part.
cTSUIServiceManager::~cTSUIServiceManager() noexcept {}

cITSUIServiceManager *__cdecl cTSUIServiceManager::Create() { return new cTSUIServiceManager; }

bool cTSUIServiceManager::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid == cITSUIServiceManager::kIid) {
        *out = static_cast<cITSUIServiceManager *>(this);
        AddRef();
        return true;
    }
    return rz::cRZSystemService::QueryInterface(riid, out);
}

bool cTSUIServiceManager::Init()
{
    As<ifaces::cIGZFrameWork>(GetServiceObject())->AddToTick(As<ifaces::cIGZSystemService>(static_cast<rz::cRZSystemService *>(this)));
    reinterpret_cast<KeyMapSlot5Walker *>(&mMessageTypes)
        ->CallSlot5ForEachKey(Word(static_cast<cIGZMessageTarget2 *>(this)), nullptr);
    return true;
}

bool cTSUIServiceManager::Shutdown()
{
    reinterpret_cast<KeyMapWalker *>(&mMessageTypes)->CallSlot6ForEachKey(Word(static_cast<cIGZMessageTarget2 *>(this)), nullptr);
    As<ifaces::cIGZFrameWork>(GetServiceObject())->RemoveFromTick(As<ifaces::cIGZSystemService>(static_cast<rz::cRZSystemService *>(this)));
    return true;
}

bool cTSUIServiceManager::OnTick(uint32_t counter)
{
    for (const UIServiceRef &service : mTickServices) UIServiceTick(service.Get(), counter);
    return true;
}

bool cTSUIServiceManager::AddTickService(cIGZUnknown *service)
{
    if (FindService(mTickServices, service) != mTickServices.end()) return false;
    const UIServiceRef held(service); // the argument held around the append, as the original
    mTickServices.push_back(held);
    return true;
}

bool cTSUIServiceManager::RemoveTickService(cIGZUnknown *service)
{
    const auto it = FindService(mTickServices, service);
    if (it == mTickServices.end()) return false;
    mTickServices.erase(it);
    return true;
}

bool cTSUIServiceManager::InitService(uint32_t classId, uint32_t iid, void **out)
{
    UIServiceRef service;
    auto *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    // The original tests the low byte of the result.
    if (!static_cast<uint8_t>(com->GetClassObject(classId, kUIServiceIid, service.AsPPVoid()))) return false;
    if (!UIServiceInit(service.Get())) return false;
    mTickServices.push_back(service);
    return service->QueryInterface(iid, out);
}

void cTSUIServiceManager::ReleaseCell10de488()
{
    auto *const cell = GameAddress<RZUnknownSlot>(kCell10de488);
    if (!cell->mObject) return;
    RawSlot<5>(cell->mObject);
    cell->Assign(nullptr);
}

bool cTSUIServiceManager::DoMessage(void *message)
{
    if (!message) return false;
    UIServiceRef message2;
    bool handled = false;
    auto *const unknown = static_cast<cIGZUnknown *>(message);
    if (unknown->QueryInterface(kMessage2Iid, message2.AsPPVoid())) {
        const uint32_t type = As<ifaces::cIGZMessage2Standard>(message)->GetType();
        if (type == kMsgAppInit) {
            for (unsigned i = 0; i < kServices; ++i) InitService(kServiceIds[i].classId, kServiceIds[i].iid, mServices[i].AsPPVoid());
            SetUpStep7408f4(this);
            ScopedRef winMgr{nullptr};
            if (void *const framework = GetServiceObject())
                As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kWinMgrServiceId, kWinMgrIid, &winMgr.mObject);
            SetUpWindows7406c1(this, winMgr.mObject);
            winMgr.ReleaseObject();
            SetUpStep740c99(this);
            SetUpStep7436b1();
            LoadCursors(this);
            void *const memory = RZMemory::Malloc(0x2c);
            void *const ready = memory ? ConstructMessage(memory) : nullptr;
            if (ready) static_cast<cIGZUnknown *>(ready)->AddRef();
            As<ifaces::cIGZMessage2Standard>(ready)->SetType(kMsgUIReady); // unchecked, as the original
            As<ifaces::cIGZMessageServer>(MessageServer())->MessagePost(ready, 0);
            if (ready) static_cast<cIGZUnknown *>(ready)->Release();
            handled = true;
        } else if (type == kMsgAppShutdown) {
            ReleaseCell10de488();
            ServiceQueryHolder holder;
            holder.QueryWithIdsA();
            if (holder.mObject) {
                if (void *const object = RawSlot<3, void *>(holder.mObject)) {
                    RawSlot<85>(object);
                    RawSlot<5>(object);
                }
            }
            for (const UIServiceRef &service : mTickServices) UIServiceShutdown(service.Get());
            mTickServices.clear();
            for (unsigned i = 0; i < 9; ++i) mServices[i].Assign(nullptr);
            mServices[10].Assign(nullptr); // +0x54 before +0x50, as the original
            mServices[9].Assign(nullptr);
            if (holder.mObject) static_cast<cIGZUnknown *>(holder.mObject)->Release();
            handled = true;
        } else if (type == kMsgD03ffba0) {
            Message740422(this);
            handled = true;
        }
    }
    return handled;
}
