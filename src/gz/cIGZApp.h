#pragma once
#include "gz/cIGZUnknown.h"

#include <cstdint>

class cIGZFrameWork;
class cIGZSystemService;

// cIGZApp: the application as the framework sees it (15 slots; g_App 0x10c9380 holds this face). cGZApp (0x40 bytes,
// ctor 0xc59f5a) is the base implementation: a cRZSystemService with this face at +0x1c and a name string at +0x20;
// cTSAppFW overrides the startup slots. GZFrameWork_Startup calls slots 13, 12, 11 (in that order, state 0), 6
// (PreFrameworkInit, state 1) and 7 (last); Run calls 8 (false: the framework runs its own MainLoop); Shutdown calls 9. Lane EC
// (docs/subsystems/startup.md). Slot order = declaration order.
class cIGZApp : public cIGZUnknown {
public:
    virtual cIGZSystemService *AsIGZSystemService() = 0;          // slot 3, cGZApp 0xa28af0 (this - 0x1c)
    virtual bool AddApplicationService(cIGZSystemService *service) = 0; // slot 4, cGZApp 0xc59fe6 (ret 4)
    virtual const char *ModuleName() = 0;                         // slot 5, folded 0x1c1c80 (tentative)
    virtual bool PreFrameworkInit() = 0;                          // slot 6, cTSAppFW 0xc290d8
    virtual bool PostFrameworkInit() = 0;                         // slot 7, default 0x144910
    virtual bool GZRun() = 0;                                     // slot 8, default 0x147480 (false)
    virtual bool PreFrameworkShutdown() = 0;                      // slot 9, default 0x144910
    virtual cIGZFrameWork *Framework() = 0;                       // slot 10, cGZApp 0xc59ff2 (g_Framework)
    virtual bool AddDynamicLibrariesHere() = 0;                   // slot 11, cTSAppFW 0xc26c30
    virtual bool AddCOMDirectorsHere() = 0;                       // slot 12, cTSAppFW 0xc26bf5
    virtual bool AddApplicationServicesHere() = 0;                // slot 13, cTSAppFW 0xc267fb
    virtual bool LoadRegistry() = 0;                              // slot 14, default 0x144910
};
