// gz::cGZApp in the standalone shape (lane EV). Built for size, as the game's (0xc59f5a..0xc5a03e are packed).
#include "gz/cGZApp.h"

#include "rz/RZFrameworkHelpers.h"

#include <new>

namespace gz {

namespace {
constexpr uint32_t kIidApp = 0x66; // cIGZApp
} // namespace

// ---- 0xc59f5a: the base service (id, the app band's priority), then the name

cGZApp::cGZApp(uint32_t serviceId, const char *name) : rz::cRZSystemService(serviceId, kPriority)
{
    mName.Construct(name);
}

// ---- 0xc59f91: the name (its vtable stored back, the buffer freed), then the base service's destructor

cGZApp::~cGZApp()
{
    mName.Destruct();
}

// ---- 0xc59ff7

bool cGZApp::QueryInterface(uint32_t riid, void **out)
{
    if (riid == kIidApp) {
        *out = static_cast<cIGZApp *>(this);
        AddRef();
        return true;
    }
    return rz::cRZSystemService::QueryInterface(riid, out);
}

// ---- folded 0x1cc320 / 0xb359c9

uint32_t cGZApp::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cGZApp::Release() { return RemoveRef(); }

// ---- the cIGZApp face

cIGZSystemService *cGZApp::AsIGZSystemService() { return this; }

bool cGZApp::AddApplicationService(cIGZSystemService *service) { return Framework()->AddSystemService(service); }

const char *cGZApp::ModuleName() { return mName.mString.c_str(); }

bool cGZApp::PreFrameworkInit() { return true; }
bool cGZApp::PostFrameworkInit() { return true; }
bool cGZApp::GZRun() { return false; }
bool cGZApp::PreFrameworkShutdown() { return true; }

cIGZFrameWork *cGZApp::Framework() { return reinterpret_cast<cIGZFrameWork *>(static_cast<uintptr_t>(RZGetGlobal10C9378())); }

bool cGZApp::AddDynamicLibrariesHere() { return false; }
bool cGZApp::AddCOMDirectorsHere() { return false; }
bool cGZApp::AddApplicationServicesHere() { return true; }
bool cGZApp::LoadRegistry() { return true; }

// ---- adapters

cGZApp *__fastcall cGZAppAdapters::Construct(void *memory, void *, uint32_t serviceId, const char *name)
{
    return ::new (memory) cGZApp(serviceId, name);
}

void __fastcall cGZAppAdapters::Destruct(cGZApp *self, void *) { self->cGZApp::~cGZApp(); }

cGZApp *__fastcall cGZAppAdapters::DeletingDestructor(cGZApp *self, void *, uint32_t flags)
{
    self->cGZApp::~cGZApp();
    if (flags & 1) ::operator delete(self, sizeof(cGZApp));
    return self;
}

bool __fastcall cGZAppAdapters::QueryInterface(cGZApp *self, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return self->cGZApp::QueryInterface(riid, out);
}

bool __fastcall cGZAppAdapters::AddApplicationService(cIGZApp *self, void *, cIGZSystemService *service)
{
    [[msvc::forceinline_calls]] return static_cast<cGZApp *>(self)->cGZApp::AddApplicationService(service);
}

} // namespace gz
