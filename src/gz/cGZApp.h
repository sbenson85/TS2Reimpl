#pragma once
#include "gz/cIGZApp.h"
#include "gz/cIGZFrameWork.h"
#include "gz/cIGZSystemService.h"
#include "rz/RZSystemServiceBase.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>

// gz::cGZApp in the standalone shape (lane EV; docs/subsystems/startup.md, handoff item A1): the application base, a
// cRZSystemService with the cIGZApp face at +0x1c and the application name at +0x20. RVAs 1.18.0.187.
//
// Faces (RTTI): cRZSystemService +0 (cIGZSystemService +0, game vtable 0xe9d118, 13 slots; cRZUnknownMT +4, 0xe9d150,
// 6 slots), cIGZApp +0x1c (0xe9d16c, 15 slots). The compiler's thunks replace the game's: QueryInterface 0xc5a01e (+4)
// and 0xc5a026 (+0x1c), Release 0xc5a02e (+4) and 0xc5a036 (+0x1c), the deleting destructor 0xc59fbf (+4 slot 3, into
// 0xc59fc7). The AddRef thunks 0x1cc325 / 0x1cc32d are folded (shared_vf_*): byte-identical across classes.
//
// Creation: the constructor 0xc59f5a is called only by cTSAppFW's constructor 0xc26147 (ours, hook-shaped: it builds the
// base with appbase::cGZApp::Construct and the game's vtables), and the vtables are stored only by 0xc59f5a and the
// destructor body 0xc59f91. So in game the application object carries the game's vtables (through cTSAppFW's) and the
// adapters serve it; our vtables are exercised by the tests until cTSAppFW (item A2) takes the shape.
namespace gz {

class cGZApp : public rz::cRZSystemService, public cIGZApp {
public:
    static constexpr int32_t kPriority = -2000000; // the app band (cCTGFramework slot 42)

    cGZApp(uint32_t serviceId, const char *name); // 0xc59f5a
    ~cGZApp() override;                           // 0xc59f91; deleting 0xc59fc7 (+4 face slot 3, thunk 0xc59fbf)

    // cIGZUnknown (all three faces)
    bool QueryInterface(uint32_t riid, void **out) override; // 0xc59ff7: cIGZApp (0x66) -> +0x1c, else the base's
    uint32_t AddRef() override;                              // folded 0x1cc320 (jmp to the base's 0x1b9640)
    uint32_t Release() override;                             // folded 0xb359c9 (RemoveRef through the +4 vtable)

    // cIGZApp
    cIGZSystemService *AsIGZSystemService() override;                   // 0xa28af0 (face - 0x1c)
    bool AddApplicationService(cIGZSystemService *service) override;    // 0xc59fe6: Framework()->AddSystemService
    const char *ModuleName() override;                                  // folded 0x1c1c80 (the name's c_str)
    bool PreFrameworkInit() override;                                   // shared 0x144910 (true)
    bool PostFrameworkInit() override;                                  // shared 0x144910
    bool GZRun() override;                                              // shared 0x147480 (false)
    bool PreFrameworkShutdown() override;                               // shared 0x144910
    cIGZFrameWork *Framework() override;                                // 0xc59ff2 (jmp 0x14323f: g_Framework)
    // Slots 11 and 12 are 0x140fe0, a bare `ret` (al undefined): cTSAppFW overrides both, so the base's result is never
    // read. Ours returns false.
    bool AddDynamicLibrariesHere() override;
    bool AddCOMDirectorsHere() override;
    bool AddApplicationServicesHere() override;                         // shared 0x144910
    bool LoadRegistry() override;                                       // shared 0x144910

    // +0x20: the name, a hook-shaped cRZString (game vtable 0xd9b470) built and torn down by its own members.
    union {
        cRZString mName;
    };
};
static_assert(sizeof(cGZApp) == 0x40, "cGZApp size");
static_assert(offsetof(cGZApp, mName) == 0x20, "cGZApp layout: cIGZApp face at +0x1c, the name at +0x20");

// The RVA hooks' entry points (docs/standalone-shape.md, step 6): `this` in ecx (the face the slot is called on), the
// dummy edx, the stack arguments.
struct cGZAppAdapters {
    static cGZApp *__fastcall Construct(void *memory, void *edx, uint32_t serviceId, const char *name); // 0xc59f5a
    static void __fastcall Destruct(cGZApp *self, void *edx);                                             // 0xc59f91
    static cGZApp *__fastcall DeletingDestructor(cGZApp *self, void *edx, uint32_t flags);                // 0xc59fc7
    static bool __fastcall QueryInterface(cGZApp *self, void *edx, uint32_t riid, void **out);            // 0xc59ff7
    static bool __fastcall AddApplicationService(cIGZApp *self, void *edx, cIGZSystemService *service);   // 0xc59fe6
};

} // namespace gz
