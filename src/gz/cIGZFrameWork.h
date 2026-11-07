#pragma once
#include "gz/cIGZUnknown.h"

#include <cstdint>

class cIGZApp;
class cIGZCmdLine;
class cIGZCOM;
class cIGZFrameWorkHooks;
class cIGZSystemService;

// The framework's interfaces (lane EC, docs/subsystems/startup.md). One framework object exists: the static
// cCTGFramework at 0x10fdb50 (g_Framework 0x10c9378), built by WinMain. The slot comments name cGZFramework's functions
// (vtable 0xd9b544; cCTGFramework 0xe995cc shares all of them but OnTick); "folded" ones are 2..8-byte functions shared
// with unrelated classes. Slot order = declaration order (no overloads). Stream and debug-stream types are not mapped
// (void *).

// The enumerator EnumSystemServices calls: slot 3 with each service's cIGZUnknown and the caller's two words.
class cIGZUnknownEnumerator : public cIGZUnknown {
public:
    virtual void Enumerate(cIGZUnknown *object, uint32_t context, uint32_t flags) = 0; // slot 3 (tentative)
};

class cIGZFrameWork : public cIGZUnknown {
public:
    virtual bool AddSystemService(cIGZSystemService *service) = 0;     // slot 3, 0x14234f (ret 4)
    virtual bool RemoveSystemService(cIGZSystemService *service) = 0;  // slot 4, 0x143431 (ret 4)
    virtual bool GetSystemService(uint32_t serviceId, uint32_t iid, void **ppv) = 0; // slot 5, 0x1428e2 (ret 0xc)
    virtual bool EnumSystemServices(cIGZUnknownEnumerator *enumerator, uint32_t context, uint32_t flags) = 0; // slot 6, 0x14271c
    virtual bool AddHook(cIGZFrameWorkHooks *hook) = 0;                // slot 7, 0x14229a (ret 4)
    virtual bool RemoveHook(cIGZFrameWorkHooks *hook) = 0;             // slot 8, 0x1433cd (ret 4)
    virtual bool AddToTick(cIGZSystemService *service) = 0;            // slot 9, 0x1424fe (ret 4)
    virtual bool RemoveFromTick(cIGZSystemService *service) = 0;       // slot 10, 0x143324 (ret 4)
    virtual bool AddToOnIdle(cIGZSystemService *service) = 0;          // slot 11, 0x142474 (ret 4)
    virtual bool RemoveFromOnIdle(cIGZSystemService *service) = 0;     // slot 12, 0x14327b (ret 4)
    virtual uint32_t GetOnIdleInterval() = 0;                          // slot 13, folded 0x142860 (+0xd0)
    virtual void SetOnIdleInterval(int32_t interval) = 0;              // slot 14, 0x1436e6 (ret 4; at least 1)
    virtual void OnTick(uint32_t counter) = 0;                         // slot 15, 0x142eca; cCTGFramework 0xc2d427
    virtual void OnIdle() = 0;                                         // slot 16, 0x142dbb
    virtual uint8_t IsTickEnabled() = 0;                               // slot 17, 0x142c23 (the raw byte +0xcc)
    virtual void ToggleTick(uint32_t enabled) = 0;                     // slot 18, 0x1438d7 (stores the low byte)
    virtual void Quit(int32_t code) = 0;                               // slot 19, 0x1431fc (ret 4)
    virtual void AbortiveQuit(int32_t code) = 0;                       // slot 20, 0x142270 (ret 4): hooks, then exit()
    virtual cIGZCmdLine *CommandLine() = 0;                            // slot 21, folded 0x142710 (+0xfc)
    virtual uint8_t IsInstall() = 0;                                   // slot 22, 0x142c1c (+0xdc)
    virtual cIGZCOM *GetCOMObject() = 0;                               // slot 23, folded 0x142840 (+8)
    virtual uint32_t GetState() = 0;                                   // slot 24, folded 0x142870 (+0xe0, 0..14)
    virtual void *GetDebugStream() = 0;                                // slot 25, folded 0x142850 (+0x154)
    virtual void *DefaultDebugStream() = 0;                            // slot 26, 0x142717
    virtual void *DebugStream() = 0;                                   // slot 27, 0x142717 (the same function)
    virtual void SetDebugStream(void *stream) = 0;                     // slot 28, folded 0x1436cc (ret 4)
    virtual bool SetDebugLevel(int32_t level) = 0;                     // slot 29, 0x143699 (ret 4)
    virtual int32_t GetDebugLevel() = 0;                               // slot 30, folded 0x142844 (+0x164)
    virtual void *StdOut() = 0;                                        // slot 31, 0x143887 (tentative)
    virtual void *StdErr() = 0;                                        // slot 32, 0x1437e7 (tentative)
    virtual void *StdIn() = 0;                                         // slot 33, 0x143837 (tentative)
    virtual bool GetStream(uint32_t kind, uint32_t iid, void **ppv) = 0; // slot 34, 0x142877 (ret 0xc)
    virtual bool SetStream(uint32_t kind, void *stream) = 0;           // slot 35, 0x1436fb (ret 8)
    virtual void SetApplication(cIGZApp *app) = 0;                     // slot 36, 0x14363e (g_App 0x10c9380)
    virtual cIGZApp *Application() = 0;                                // slot 37, 0x1426ec
    virtual void ReportException(const char *message) = 0;             // slot 38, 0x14358c (ret 4)
    virtual void *ExceptionNotificationObj() = 0;                      // slot 39, folded 0x1427a0 (+0x168)
};

// The Windows face (+4 of the framework; 7 slots). Its cIGZUnknown slots are adjustor thunks to the primary's.
class cIGZFrameWorkW32 : public cIGZUnknown {
public:
    virtual cIGZFrameWork *AsIGZFrameWork() = 0;   // slot 3, folded 0x142700 (this - 4)
    virtual void *GetWindowsInstance() = 0;        // slot 4, folded 0x142950 (+0x174 of the framework)
    virtual void *GetMainHWND() = 0;               // slot 5, 0x142857 (+0x178)
    virtual void SetMainHWND(void *hwnd) = 0;      // slot 6, 0x1436d9 (ret 4)
};
