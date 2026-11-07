#pragma once
#include "app/AppCtor.h"           // appctor::AutoRefElement (the list at +0x78)
#include "app/ExceptionReporter.h" // appbase::ExceptionReporterObject (hook-shaped member at +0x98)
#include "gz/cGZApp.h"
#include "gz/cIGZApp.h"
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZSystemService.h"
#include "rz/cRZCmdLine.h"
#include "rz/cRZMutex.h"
#include "rz/cRZString.h"
#include "ts/cITSAppFW.h"
#include "ts/cTSAppCommonImpl.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <set>

// ts::cTSAppFW in the standalone shape (lane FG; docs/subsystems/startup.md, handoff item A2a): the application object.
// RVAs 1.18.0.187.
//
// Faces (RTTI order): gz::cGZApp +0 (cIGZSystemService +0, game vtable 0xe98464, 13 slots; cRZUnknownMT +4, 0xe9849c, 6;
// cIGZApp +0x1c, 0xe984b8, 15), cITSAppFW +0x40 (0xe984f8, 5), cTSAppCommonImpl +0x44 (cITSAppCommon, 0xe98510, 18),
// cIGZMessageTarget2 +0x5c (0xe9855c, 4). The constructor 0xc26147 also stores the base interfaces' vtables 0xe9844c
// (+0x40) and 0xda49bc (+0x5c) first; the compiler's do the same job here. The compiler's thunks replace the game's:
// QueryInterface 0xc29852 / 0xc2985a / 0xc29862 / 0xc2986a / 0xc29872, AddRef 0xc274bb..0xc274db, Release
// 0xc29897..0xc298b7, the deleting destructor 0xc26780 (+4 slot 3) and 0xc26788 (+0x44 slot 10).
//
// Creation: WinMain 0xc2b87c (the game's, not hooked) builds the one application in its function-local static at
// 0x10fdcf0 through the constructor 0xc26147; the six vtables are stored only by the constructor and the destructor body
// 0xc266b9. The host installs the hooks before the image's entry point (host/main.cpp; ts2host.log shows
// cTSAppFW_Construct=1), so that call reaches our adapter: the live application is this class with our vtables, and the
// game's calls through its vtables reach our members (and the compiler's thunks). The adapters serve direct calls to the
// RVAs (the atexit destructor 0xd988f0 -> 0xc266b9, AddCOMDirectorsHere's callers). WinMain's candidate in
// src/app/WinMainInfra.cpp (unhooked until E1) builds it the same way.
namespace ts {

class cTSAppPropertyFiles; // ts/cTSAppPropertyFiles.h

class cTSAppFW : public gz::cGZApp, public cITSAppFW, public cTSAppCommonImpl, public cIGZMessageTarget2 {
public:
    static constexpr uint32_t kIidMessageTarget = 0x452294aa, kIidAppFW = 0x9af89af3;

    cTSAppFW(uint32_t serviceId, const char *name, cIGZCmdLine *commandLine); // 0xc26147
    ~cTSAppFW() override; // 0xc266b9; deleting 0xc267d9 (+4 slot 3 and +0x44 slot 10)

    // cIGZUnknown (every face; the others are the compiler's thunks)
    bool QueryInterface(uint32_t riid, void **out) override; // 0xc297fa: 0x452294aa -> +0x5c, 0x9af89af3 -> +0x40, cGZApp's, cTSAppCommonImpl's
    uint32_t AddRef() override;                              // 0xc274b6 (jmp to cGZApp's folded 0x1cc320)
    uint32_t Release() override;                             // 0xc29892 (jmp to cGZApp's folded 0xb359c9)

    // cIGZSystemService
    bool Init() override;                 // 0xc27c3e (lane FW): the installation checks, then StartUp
    bool Shutdown() override;             // 0xc2a1e3 (lane FM)
    bool OnTick(uint32_t counter) override; // 0xc28fcc (lane FM)

    // cIGZApp
    bool PreFrameworkInit() override;           // 0xc290d8 (lane FM)
    bool AddDynamicLibrariesHere() override;    // 0xc26c30
    bool AddCOMDirectorsHere() override;        // 0xc26bf5 (al undefined in the original: tail call of a void function)
    bool AddApplicationServicesHere() override; // 0xc267fb

    // cITSAppFW
    bool PreTranslateMessage(void *msg) override; // 0xc296b4
    uint32_t GetWord70() override;                // folded 0x1457d0

    // cITSAppCommon
    void SetAppName(const char *text) override;   // 0xc299da: assign to the name's std::string
    bool InitOptions() override;                  // 0xc2a7bb
    bool SetupUserDataDirectories() override;     // 0xc2a7de (lane FM; +0x70 = 1 in develop mode)

    // cIGZMessageTarget2
    bool DoMessage(void *message) override;       // 0xc27605

    // 0xc27a16 (lane LY): the product's registered name into `out`: HKEY_CURRENT_USER\<the product's registry key>
    // (0xcb4690 for "Sims2.exe"), value "DisplayName"; when the key opens, `out` gets the value's text (the default
    // "The Sims 2 Legacy" when the value is missing), else it is left alone. `this` is not read.
    void RegisteredProductName(cIGZString *out);

    // Lane MB (foundations F4e): the helpers DoMessage, Init, PreFrameworkInit and Shutdown call.
    // 0xc28b7d (ret 0xc; `this` not read): when "<app directory>ExceptionUploader.exe" exists, runs it and waits:
    // its path (in the system code page, '/' made '\'), "-logDir:\"<logs directory>\"", then "-noUpload", "-noLog" and
    // "-esper" as the flags ask. The flags are bytes, each compared with 1 (PreFrameworkInit passes its switch answer's
    // byte as it is); see the definition for the original's argv index.
    void LaunchExceptionUploader(uint8_t noUpload, uint8_t noLog, uint8_t esper);
    // 0xc28de0 (DoMessage 0xb627ad3): the fatal-error report: the error box with the +0x244 text (unless quietCrash),
    // then the uploader without logs (unless noExceptionUpload).
    void ReportFatal();
    // 0xc28e32 (DoMessage 0xb627ad4): the crash report's extra data: whether shutdown began, the host name, the page
    // file usage, and the neighbourhood, lot and family of the current game state when there are any.
    void AddCrashData();
    // 0xc2a3d6 (ret 4; DoMessage 0xc3): unless "noStupidIFFUnlocking" is set (the setting, or the switch where the
    // globals allow it), every entry of the neighbourhood's directory and the NeighborhoodManager.package handed to the
    // package service's slot 27.
    void UnlockNeighborhoodFiles(cIGZString *neighborhood);

    cTSAppPropertyFiles *mPropertyFiles;       // +0x60 built by PreFrameworkInit; DoMessage 0xcd and Shutdown save it
    std::set<uint32_t> mMessageIds;            // +0x64 0xc3, 0xce1505fd, 0xce, 0xcd
    uint8_t mByte6c;                           // +0x6c 1 after construction (OnTick clears it)
    uint8_t mOtherInstance;                    // +0x6d 1 when "Global\The Sims 2" was already held
    uint8_t mOtherBodyShop;                    // +0x6e 1 when "Global\The Sims 2 Body Shop" was already held
    uint8_t pad6f;
    uint32_t mWord70;                          // +0x70 (cITSAppFW slot 4): 1 in develop mode (userDataMode=develop)
    cRZMutex *mMutex;                          // +0x74 "Global\The Sims 2", held for the life of the application
    std::list<appctor::AutoRefElement> mList;  // +0x78 counted references, released by the destructor
    cIGZUnknown *mObject80;                    // +0x80 PreTranslateMessage's delegate (its slot 3); released by the destructor
    uint32_t mWord84;                          // +0x84 never written by the constructor
    uint32_t mWord88, mWord8c, mWord90;        // +0x88.. cleared
    uint32_t mShutdownMessage;                 // +0x94 RegisterWindowMessageA("EA_LAUNCHER_SHUTDOWN_REQUEST_SIMS2EP")
    // The two hook-shaped members are built and torn down by their own Construct / Destruct (game vtables), in the order
    // the original's inlined code has: anonymous unions keep the compiler from doing it a second time.
    union {
        appbase::ExceptionReporterObject mReporter; // +0x98 (crash reporting: `replaced` scope)
    };
    union {
        cRZString mString244;                       // +0x244 (DoMessage 0xce1505fd assigns its text)
    };
    uint8_t mFlags264;                         // +0x264 bit 0 quietCrash, bit 1 noExceptionUpload, bit 2 set by message 0xce
    uint8_t pad265[3];

private:
    // The pieces of DoMessage's 0xce1505fd case (the string-table texts the application keeps).
    void LoadStringTableTexts();
    // Init's second half, once the checks passed: the skin file type, the tick, the +0x80 delegate, the start message,
    // the sysMem override and the property set (the product paths are asked again into the caller's buffers).
    void StartUp(char *productKey, uint32_t *keyLength, char *installDirectory, uint32_t *directoryLength, const char *newerExecutable);
};
static_assert(sizeof(cTSAppFW) == 0x268, "cTSAppFW size");
static_assert(offsetof(cTSAppFW, mPropertyFiles) == 0x60, "cTSAppFW layout: the four faces end at +0x60");
static_assert(offsetof(cTSAppFW, mMessageIds) == 0x64, "cTSAppFW layout");
static_assert(offsetof(cTSAppFW, mByte6c) == 0x6c, "cTSAppFW layout");
static_assert(offsetof(cTSAppFW, mWord70) == 0x70, "cTSAppFW layout");
static_assert(offsetof(cTSAppFW, mMutex) == 0x74, "cTSAppFW layout");
static_assert(offsetof(cTSAppFW, mList) == 0x78, "cTSAppFW layout");
static_assert(offsetof(cTSAppFW, mObject80) == 0x80, "cTSAppFW layout");
static_assert(offsetof(cTSAppFW, mShutdownMessage) == 0x94, "cTSAppFW layout");
static_assert(offsetof(cTSAppFW, mReporter) == 0x98, "cTSAppFW layout");
static_assert(offsetof(cTSAppFW, mString244) == 0x244, "cTSAppFW layout");
static_assert(offsetof(cTSAppFW, mFlags264) == 0x264, "cTSAppFW layout");

// The RVA hooks' entry points (docs/standalone-shape.md, step 6): `this` in ecx (the face the slot is called on), the
// dummy edx, the stack arguments. One statement each (forced inlining of the qualified member), except the constructor
// and the deleting destructor.
struct cTSAppFWAdapters {
    static cTSAppFW *__fastcall Construct(void *memory, void *edx, uint32_t serviceId, const char *name, cIGZCmdLine *commandLine); // 0xc26147
    static void __fastcall Destruct(cTSAppFW *self, void *edx);                                     // 0xc266b9
    static cTSAppFW *__fastcall DeletingDestructor(cTSAppFW *self, void *edx, uint32_t flags);      // 0xc267d9
    static bool __fastcall QueryInterface(cTSAppFW *self, void *edx, uint32_t riid, void **out);    // 0xc297fa
    static bool __fastcall Init(cTSAppFW *self, void *edx);                                         // 0xc27c3e
    static bool __fastcall Shutdown(cTSAppFW *self, void *edx);                                     // 0xc2a1e3
    static bool __fastcall OnTick(cTSAppFW *self, void *edx, uint32_t counter);                     // 0xc28fcc
    static bool __fastcall PreFrameworkInit(cIGZApp *self, void *edx);                              // 0xc290d8
    static bool __fastcall SetupUserDataDirectories(cTSAppCommonImpl *self, void *edx);             // 0xc2a7de
    static bool __fastcall AddDynamicLibrariesHere(cIGZApp *self, void *edx);                       // 0xc26c30
    static bool __fastcall AddCOMDirectorsHere(cIGZApp *self, void *edx);                           // 0xc26bf5
    static bool __fastcall AddApplicationServicesHere(cIGZApp *self, void *edx);                    // 0xc267fb
    static bool __fastcall PreTranslateMessage(cITSAppFW *self, void *edx, void *msg);              // 0xc296b4
    static bool __fastcall InitOptions(cTSAppCommonImpl *self, void *edx);                          // 0xc2a7bb
    static bool __fastcall DoMessage(cIGZMessageTarget2 *self, void *edx, void *message);           // 0xc27605
};

} // namespace ts
