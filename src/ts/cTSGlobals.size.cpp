#include "ts/cTSGlobals.h"

#include "app/AppServices2.h" // as2::TestingCheatsEnabled, as2::SetTestingCheatsEnabled
#include "game/vtable.h"
#include "rz/StaticGetters.h" // GetDirectorFrameworkObject

#include <new>

namespace {

using ThisFn0 = uint32_t(__thiscall *)(void *);

// Releases what a field holds, clears it, then asks the framework (the director's framework object, fetched first, as the
// original does) to fill it through its slot 3 with the id pair and the field's address.
void Fetch(cTSGlobalsRef &field, uint32_t first, uint32_t second)
{
    void *framework = GetDirectorFrameworkObject();
    if (void *old = field.mObject) {
        field.mObject = nullptr;
        static_cast<cIGZUnknown *>(old)->Release();
    }
    VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t, void *), 3>(framework, first, second, &field);
}

} // namespace

cTSGlobalsRef::~cTSGlobalsRef()
{
    if (mObject) static_cast<cIGZUnknown *>(mObject)->Release();
}

cTSGlobals::cTSGlobals() : rz::cRZSystemService(kServiceId, kPriority), mFlag90(false) {}

cTSGlobals::~cTSGlobals()
{
    // The original calls its Shutdown directly; the fields' destructors then Release what is left, from +0x94 down.
    cTSGlobals::Shutdown();
}

bool cTSGlobals::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != kInterfaceId) return rz::cRZSystemService::QueryInterface(riid, ppv);
    *ppv = static_cast<cITSGlobals *>(this);
    AddRef();
    return true;
}

uint32_t cTSGlobals::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cTSGlobals::Release() { return rz::cRZSystemService::Release(); }

bool cTSGlobals::Init()
{
    Fetch(mField44, 0x1ab66100, 0x1ab66099);
    if (mField44.mObject) VirtualCall<ThisFn0, 3>(mField44.mObject);

    Fetch(mCatalog, 0xaa565060, 0xdadba5f9);
    Fetch(mField7C, 0xac4761c4, 0xac4761be);

    Fetch(mField58, 0x2d1a2550, 0xed1a2513);
    if (mField58.mObject) {
        VirtualCall<ThisFn0, 15>(mField58.mObject);
        // Read again after the call.
        void *result = reinterpret_cast<void *>(static_cast<uintptr_t>(VirtualCall<ThisFn0, 17>(mField58.mObject)));
        mField54.Assign(result);
    }

    Fetch(mGameStateController, 0x0bea1c24, 0xabea1c19);
    if (mGameStateController.mObject) VirtualCall<ThisFn0, 3>(mGameStateController.mObject);
    return true;
}

bool cTSGlobals::Shutdown()
{
    mField20.Assign(nullptr);
    mWantFearService.Assign(nullptr);
    mField28.Assign(nullptr);
    mField2C.Assign(nullptr);
    mNeighborhood.Assign(nullptr);
    mField34.Assign(nullptr);
    mField48.Assign(nullptr);
    mField4C.Assign(nullptr);
    mField50.Assign(nullptr);
    mField94.Assign(nullptr);
    if (void *controller = mGameStateController.mObject) {
        VirtualCall<ThisFn0, 4>(controller);
        mGameStateController.Assign(nullptr);
    }
    if (mField58.mObject) {
        mField54.Assign(nullptr);
        VirtualCall<ThisFn0, 16>(mField58.mObject);
        mField58.Assign(nullptr);
    }
    mField3C.Assign(nullptr);
    mCatalog.Assign(nullptr);
    if (void *object = mField44.mObject) {
        VirtualCall<ThisFn0, 4>(object);
        mField44.Assign(nullptr);
    }
    mField7C.Assign(nullptr);
    mGlobalInventory.Assign(nullptr);
    return true;
}

bool cTSGlobals::TestingCheatsEnabled()
{
    return static_cast<uint8_t>(as2::TestingCheatsEnabled(this, nullptr)) != 0;
}

void cTSGlobals::SetTestingCheatsEnabled(uint32_t value)
{
    as2::SetTestingCheatsEnabled(this, nullptr, value);
}

// ---- adapters --------------------------------------------------------------------------------------------------------

cTSGlobals *__fastcall cTSGlobalsAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cTSGlobals;
}

void *__fastcall cTSGlobalsAdapters::SetNeighborhood(cITSGlobals *self, void *, void *object)
{
    [[msvc::forceinline_calls]] return static_cast<cTSGlobals *>(self)->cTSGlobals::SetNeighborhood(object);
}
