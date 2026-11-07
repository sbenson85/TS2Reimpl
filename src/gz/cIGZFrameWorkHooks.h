#pragma once
#include "gz/cIGZUnknown.h"

// cIGZFrameWorkHooks: the startup/shutdown callbacks the framework runs on every registered hook (AddHook, slot 7 of
// cIGZFrameWork). 11 slots. Every COM director carries this face at +4 (cRZCOMDllDirector vtable 0xda26a4); the
// defaults are the shared `mov al, 1; ret` 0x144910. Each runner (cGZFramework 0x142b97 .. 0x142957) copies the hook list
// under the lock and calls the slot on every hook in order, ignoring the results. Lane EC (docs/subsystems/startup.md).
// Slot order = declaration order.
class cIGZFrameWorkHooks : public cIGZUnknown {
public:
    virtual bool PreFrameWorkInit() = 0;          // slot 3, run by 0x142b97 (GZFrameWork_Startup, after PreFrameworkInit)
    virtual bool PreAppInit() = 0;                // slot 4, run by 0x142ad7
    virtual bool PostAppInit() = 0;               // slot 5, run by 0x1429b7
    virtual bool PreAppShutdown() = 0;            // slot 6, run by 0x142b37
    virtual bool PostAppShutdown() = 0;           // slot 7, run by 0x142a17
    virtual bool PostSystemServiceShutdown() = 0; // slot 8, run by 0x142a77
    virtual bool AbortiveQuit() = 0;              // slot 9, run by 0x142957 (from cIGZFrameWork::AbortiveQuit)
    virtual bool OnInstall() = 0;                 // slot 10, no runner found
};
