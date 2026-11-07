#pragma once
// Our start of the game image (milestone E, design: docs/subsystems/startup.md "E1: taking over the
// process start"). The image's own start is `entry` 0x8248a3 (the /GS cookie, then
// `__scrt_common_main_seh` 0x824727): it initialises the image's CRT, runs its XI and XC initialiser
// tables, calls WinMain and ends the process with the image CRT's `exit`. RunImageStart does the
// same sequence from our source; every step that touches the image's CRT state is a binding in
// ImageCrt, filled by the host (host/main.cpp, `[Startup] OwnStart`) with the image's addresses and
// by the tests with recorders. Nothing here names an image address: the bindings are the list of
// what the standalone build drops with the image.
#define NOMINMAX
#include <windows.h>

#include <cstdint>

namespace winmainstart {

using Initialiser = void(__cdecl *)();       // an XC entry
using CheckedInitialiser = int(__cdecl *)(); // an XI entry: non-zero aborts the start
using TlsCallback = void(__stdcall *)(void *module, unsigned long reason, void *reserved);

struct ImageCrt {
    void(__cdecl *initSecurityCookie)();             // __security_init_cookie (the image's /GS cookie)
    bool(__cdecl *initializeCrt)(int moduleType);    // __scrt_initialize_crt(1 = exe); al
    bool(__cdecl *acquireStartupLock)();             // al: "nested", handed back to the release
    void(__cdecl *releaseStartupLock)(bool nested);
    int *nativeStartupState;                         // 0 not started, 1 initialising, 2 initialised
    CheckedInitialiser *xiFirst, *xiLast;            // the XI table
    Initialiser *xcFirst, *xcLast;                   // the XC table
    int(__cdecl *runCheckedInitialisers)(CheckedInitialiser *, CheckedInitialiser *); // _initterm_e
    void(__cdecl *runInitialisers)(Initialiser *, Initialiser *);                     // _initterm
    TlsCallback *dynamicTlsInit;                     // the dynamic-TLS init and destructor callback slots
    TlsCallback *dynamicTlsDtor;
    bool(__cdecl *isNonwritableInImage)(const void *); // __scrt_is_nonwritable_in_current_image; al
    void(__cdecl *registerTlsDtor)(TlsCallback);       // _register_thread_local_exe_atexit_callback
    uint16_t(__cdecl *showWindowMode)();               // __scrt_get_show_window_mode; ax
    char *(__cdecl *narrowCommandLine)();              // _get_narrow_winmain_command_line
    void *instance;                                    // WinMain's first argument: the image base
    int(__stdcall *winMain)(void *instance, void *previous, char *commandLine, int show);
    bool(__cdecl *isManagedApp)();                     // __scrt_is_managed_app; al
    void(__cdecl *cexit)();                            // _cexit
    bool(__cdecl *uninitializeCrt)(bool terminating, bool fromExit); // __scrt_uninitialize_crt
    void(__cdecl *exit)(int code);                     // exit: the atexit list, then the end of the process
    void(__cdecl *exitNow)(int code);                  // _exit
    void(__cdecl *exitQuick)();                        // _c_exit
    void(__cdecl *fastFail)(unsigned code);            // __scrt_fastfail
    int(__cdecl *exceptionFilter)(unsigned long code, EXCEPTION_POINTERS *info); // _seh_filter_exe
};

// ---- Stage 3: XC entries our build owns (docs/subsystems/startup.md "Stage 3: the static initialisers") ----
//
// An owned XC entry: in its slot the runner calls `run(arg)` instead of the image's initialiser, so every other
// entry still runs in the original order and the owned one's effect lands where the original's did.
struct OwnedInitialiser {
    uint32_t slot;     // index in the XC table (its address is the table's first slot + slot)
    uint32_t imageRva; // the image's initialiser in that slot: skipped only when the slot holds exactly this
    void(__cdecl *run)(const void *arg);
    const void *arg;
};

struct XcRunCounts {
    int image = 0;      // slots that ran the image's initialiser
    int owned = 0;      // slots that ran ours instead
    int mismatched = 0; // owned slots that held another initialiser (another image build): the image's ran
    int unused = 0;     // owned entries no slot matched (slot out of range, or listed twice)
};

// _initterm over the XC table [first, last): each non-null slot in table order, except that a slot named in
// `owned` (any order) runs ours when it holds imageBase + imageRva, and the image's (counted as mismatched)
// when it holds anything else.
XcRunCounts RunInitialisersOwned(Initialiser *first, Initialiser *last, uintptr_t imageBase,
                                 const OwnedInitialiser *owned, size_t ownedCount);

// entry 0x8248a3: the security cookie, then CommonMain. Returns only on the paths the original
// returns on (an XI entry failed: 255; a managed module; an exception whose filter selects the
// handler in a managed module); a native exe ends in `exit` or `_exit`.
int RunImageStart(const ImageCrt &crt);

// __scrt_common_main_seh 0x824727 (WinMain's caller), on the bindings above.
int CommonMain(const ImageCrt &crt);

} // namespace winmainstart
